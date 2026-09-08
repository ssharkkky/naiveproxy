package main

import (
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/url"
	"os"
	"sort"
	"sync"
	"time"

	"golang.org/x/net/dns/dnsmessage"
	"naiveproxy.local/m5/internal/socksudp"
)

func dnsProbe(server, resolver string) error {
	ctx, cancel := context.WithTimeout(context.Background(), 6*time.Second)
	defer cancel()
	c, err := socksudp.Dial(ctx, socksudp.DialOptions{Server: server})
	if err != nil {
		return err
	}
	defer c.Close()
	c.SetDeadline(time.Now().Add(6 * time.Second))
	id := uint16(time.Now().UnixNano())
	m := dnsmessage.Message{Header: dnsmessage.Header{ID: id, RecursionDesired: true}, Questions: []dnsmessage.Question{{Name: dnsmessage.MustNewName("example.com."), Type: dnsmessage.TypeA, Class: dnsmessage.ClassINET}}}
	b, err := m.Pack()
	if err != nil {
		return err
	}
	if _, err = c.WriteTo(b, &net.UDPAddr{IP: net.ParseIP(resolver), Port: 53}); err != nil {
		return err
	}
	buf := make([]byte, 4096)
	n, _, err := c.ReadFrom(buf)
	if err != nil {
		return err
	}
	var reply dnsmessage.Message
	if err = reply.Unpack(buf[:n]); err != nil {
		return err
	}
	if reply.ID != id || !reply.Response || reply.RCode != dnsmessage.RCodeSuccess || len(reply.Answers) == 0 {
		return fmt.Errorf("invalid DNS response")
	}
	return nil
}

func main() {
	server := flag.String("socks", "127.0.0.1:1080", "Naive SOCKS5 listener")
	samples := flag.Int("samples", 96, "TCP samples")
	concurrency := flag.Int("concurrency", 8, "parallel TCP probes")
	failure := flag.Bool("failure", false, "probe a closed target port")
	resolver := flag.String("resolver", "1.1.1.1", "UDP DNS resolver")
	flag.Parse()
	if *samples < 1 || *samples > 1000 || *concurrency < 1 || *concurrency > 32 {
		os.Exit(2)
	}
	proxyURL := &url.URL{Scheme: "socks5h", Host: *server}
	transport := &http.Transport{Proxy: http.ProxyURL(proxyURL), DisableKeepAlives: true}
	defer transport.CloseIdleConnections()
	client := &http.Client{Transport: transport, Timeout: 8 * time.Second, CheckRedirect: func(*http.Request, []*http.Request) error { return http.ErrUseLastResponse }}
	targets := []string{"https://api.github.com/", "https://www.cloudflare.com/", "https://www.reddit.com/", "https://api.openai.com/"}
	var mu sync.Mutex
	var wg sync.WaitGroup
	jobs := make(chan int)
	ok := 0
	statuses := map[int]int{}
	tcpErrors := map[string]int{}
	timings := make([]float64, 0, *samples)
	started := time.Now()
	for worker := 0; worker < *concurrency; worker++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for i := range jobs {
				t := time.Now()
				request, _ := http.NewRequest(http.MethodHead, targets[i%len(targets)], nil)
				response, err := client.Do(request)
				if response != nil {
					io.Copy(io.Discard, io.LimitReader(response.Body, 4096))
					response.Body.Close()
				}
				mu.Lock()
				timings = append(timings, float64(time.Since(t).Microseconds())/1000)
				if err == nil {
					ok++
					statuses[response.StatusCode]++
				} else {
					tcpErrors[fmt.Sprintf("%T", err)]++
				}
				mu.Unlock()
			}
		}()
	}
	for i := 0; i < *samples; i++ {
		jobs <- i
	}
	close(jobs)
	wg.Wait()
	sort.Float64s(timings)
	udpOK := 0
	udpErrors := map[string]int{}
	for i := 0; i < 4; i++ {
		if err := dnsProbe(*server, *resolver); err == nil {
			udpOK++
		} else {
			kind := fmt.Sprintf("%T", err)
			var ne net.Error
			if errors.As(err, &ne) && ne.Timeout() {
				kind = "timeout"
			}
			udpErrors[kind]++
		}
	}
	row := map[string]any{"tcp_samples": *samples, "tcp_ok": ok, "udp_samples": 4, "udp_ok": udpOK, "statuses": statuses, "errors": tcpErrors, "udp_errors": udpErrors, "median_ms": timings[len(timings)/2], "p95_ms": timings[(len(timings)-1)*95/100], "max_ms": timings[len(timings)-1], "wall_s": time.Since(started).Seconds()}
	passed := ok == *samples && udpOK == 4
	if *failure {
		t := time.Now()
		resp, err := client.Get("http://1.1.1.1:81/")
		if resp != nil {
			resp.Body.Close()
		}
		elapsed := time.Since(t)
		bounded := err != nil && elapsed < 6*time.Second
		row["failed_target_bounded"] = bounded
		row["failed_target_ms"] = float64(elapsed.Microseconds()) / 1000
		passed = passed && bounded
	}
	json.NewEncoder(os.Stdout).Encode(row)
	if !passed {
		os.Exit(1)
	}
}
