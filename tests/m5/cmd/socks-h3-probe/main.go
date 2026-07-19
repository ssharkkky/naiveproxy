// socks-h3-probe is independent of Naive's CONNECT-UDP implementation. It
// adapts RFC 1928 UDP packets to quic-go and performs a real HTTP/3 request.
package main

import (
	"context"
	"crypto/tls"
	"crypto/x509"
	"flag"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"sync"
	"time"

	"github.com/quic-go/quic-go"
	"github.com/quic-go/quic-go/http3"
	"naiveproxy.local/m5/internal/socksudp"
)

type dialResources struct {
	mutex     sync.Mutex
	packet    *socksudp.Conn
	transport *quic.Transport
	target    net.Addr
}

func (r *dialResources) dial(ctx context.Context, server, address string,
	tlsConfig *tls.Config, quicConfig *quic.Config) (*quic.Conn, error) {
	r.mutex.Lock()
	defer r.mutex.Unlock()
	if r.packet != nil || r.transport != nil {
		return nil, fmt.Errorf("probe attempted more than one QUIC dial")
	}
	packet, err := socksudp.Dial(ctx, socksudp.DialOptions{
		Server: server, ControlTimeout: 5 * time.Second,
	})
	if err != nil {
		return nil, err
	}
	target := r.target
	if target == nil {
		target, err = net.ResolveUDPAddr("udp", address)
		if err != nil {
			_ = packet.Close()
			return nil, err
		}
	}
	transport := &quic.Transport{Conn: packet}
	connection, err := transport.Dial(ctx, target, tlsConfig, quicConfig)
	if err != nil {
		_ = transport.Close()
		_ = packet.Close()
		return nil, err
	}
	r.packet = packet
	r.transport = transport
	return connection, nil
}

func (r *dialResources) close() error {
	r.mutex.Lock()
	defer r.mutex.Unlock()
	var first error
	if r.transport != nil {
		first = r.transport.Close()
	}
	if r.packet != nil {
		if err := r.packet.Close(); first == nil {
			first = err
		}
	}
	return first
}

func main() {
	contract := flag.Bool("contract", false, "print the frozen G0 contract")
	socksServer := flag.String("socks", "", "SOCKS5 server address")
	targetHost := flag.String("target-host", "", "controlled HTTP/3 target host")
	targetPort := flag.Uint("target-port", 0, "controlled HTTP/3 target port")
	forceDomain := flag.Bool("force-domain", false, "preserve target as a SOCKS domain")
	serverName := flag.String("server-name", "", "inner HTTP/3 TLS server name")
	caCertificate := flag.String("ca-cert", "", "inner HTTP/3 fixture CA PEM")
	timeout := flag.Duration("timeout", 12*time.Second, "probe timeout")
	flag.Parse()
	if *contract {
		address := socksudp.TargetAddr{Type: socksudp.AddressIPv4,
			Host: "127.0.0.1", Port: 443}
		if err := address.Validate(); err != nil {
			fatal(err)
		}
		fmt.Printf("M5_G0_H3_PROBE_CONTRACT_OK alpn=%s network=%s\n",
			http3.NextProtoH3, address.Network())
		return
	}
	if *socksServer == "" || *targetHost == "" || *targetPort == 0 ||
		*targetPort > 65535 || *serverName == "" || *caCertificate == "" {
		fmt.Fprintln(os.Stderr,
			"runtime mode requires SOCKS, target, server-name, and ca-cert flags")
		os.Exit(2)
	}

	rootPEM, err := os.ReadFile(*caCertificate)
	if err != nil {
		fatal(err)
	}
	roots := x509.NewCertPool()
	if !roots.AppendCertsFromPEM(rootPEM) {
		fatal(fmt.Errorf("inner HTTP/3 CA PEM contains no certificate"))
	}
	ctx, cancel := context.WithTimeout(context.Background(), *timeout)
	defer cancel()
	resources := &dialResources{}
	if *forceDomain {
		resources.target = socksudp.TargetAddr{
			Type: socksudp.AddressDomain, Host: *targetHost, Port: uint16(*targetPort),
		}
	} else {
		resolved, err := net.ResolveUDPAddr("udp",
			net.JoinHostPort(*targetHost, fmt.Sprint(*targetPort)))
		if err != nil {
			fatal(err)
		}
		resources.target = resolved
	}
	transport := &http3.Transport{
		TLSClientConfig: &tls.Config{
			RootCAs:    roots,
			ServerName: *serverName,
			NextProtos: []string{http3.NextProtoH3},
		},
		Dial: func(ctx context.Context, address string, tlsConfig *tls.Config,
			quicConfig *quic.Config) (*quic.Conn, error) {
			return resources.dial(ctx, *socksServer, address, tlsConfig, quicConfig)
		},
	}
	client := http.Client{Transport: transport, Timeout: 10 * time.Second}
	targetURL := "https://" + net.JoinHostPort(*serverName,
		fmt.Sprint(*targetPort)) + "/m5"
	request, err := http.NewRequestWithContext(ctx, http.MethodGet, targetURL, nil)
	if err != nil {
		fatal(err)
	}
	response, err := client.Do(request)
	if err != nil {
		fatal(err)
	}
	body, readErr := io.ReadAll(response.Body)
	closeErr := response.Body.Close()
	if readErr != nil {
		fatal(readErr)
	}
	if closeErr != nil {
		fatal(closeErr)
	}
	if response.StatusCode != http.StatusOK || response.ProtoMajor != 3 ||
		response.Header.Get("X-M5-Origin") != "quic-go-h3" ||
		string(body) != "m5-http3-application-ok" {
		fatal(fmt.Errorf("unexpected HTTP/3 response: status=%d proto=%s body=%q",
			response.StatusCode, response.Proto, body))
	}
	if err := transport.Close(); err != nil {
		fatal(err)
	}
	if err := resources.close(); err != nil {
		fatal(err)
	}
	fmt.Println("M5_G2_HTTP3_CONNECTION_CLOSED")
	fmt.Println("M5_G2_HTTP3_APPLICATION_OK")
}

func fatal(err error) {
	fmt.Fprintln(os.Stderr, err)
	os.Exit(1)
}
