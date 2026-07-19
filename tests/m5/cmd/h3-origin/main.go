// h3-origin is a controlled inner HTTP/3 application fixture for M5-G2.
package main

import (
	"crypto/tls"
	"flag"
	"fmt"
	"net"
	"net/http"
	"os"

	"github.com/quic-go/quic-go/http3"
)

const responseBody = "m5-http3-application-ok"

func main() {
	bind := flag.String("bind", "127.0.0.1:0", "UDP listen address")
	certificate := flag.String("cert", "", "TLS certificate PEM")
	privateKey := flag.String("key", "", "TLS private key PEM")
	flag.Parse()
	if *certificate == "" || *privateKey == "" {
		fmt.Fprintln(os.Stderr, "--cert and --key are required")
		os.Exit(2)
	}
	keyPair, err := tls.LoadX509KeyPair(*certificate, *privateKey)
	if err != nil {
		fmt.Fprintln(os.Stderr, "load HTTP/3 fixture certificate failed")
		os.Exit(1)
	}
	host, _, err := net.SplitHostPort(*bind)
	if err != nil {
		fmt.Fprintln(os.Stderr, "parse HTTP/3 fixture address failed")
		os.Exit(1)
	}
	network := "udp4"
	if net.ParseIP(host).To4() == nil {
		network = "udp6"
	}
	packetConn, err := net.ListenPacket(network, *bind)
	if err != nil {
		fmt.Fprintln(os.Stderr, "bind HTTP/3 fixture failed")
		os.Exit(1)
	}
	defer packetConn.Close()

	handler := http.HandlerFunc(func(writer http.ResponseWriter, request *http.Request) {
		if request.Method != http.MethodGet || request.URL.Path != "/m5" {
			http.NotFound(writer, request)
			return
		}
		writer.Header().Set("Content-Type", "text/plain")
		writer.Header().Set("X-M5-Origin", "quic-go-h3")
		_, _ = writer.Write([]byte(responseBody))
	})
	server := http3.Server{
		Handler: handler,
		TLSConfig: &tls.Config{
			Certificates: []tls.Certificate{keyPair},
			NextProtos:   []string{http3.NextProtoH3},
			MinVersion:   tls.VersionTLS13,
		},
	}
	fmt.Printf("M5_G2_H3_ORIGIN_READY port=%d\n", packetConn.LocalAddr().(*net.UDPAddr).Port)
	if err := server.Serve(packetConn); err != nil && err != http.ErrServerClosed {
		fmt.Fprintln(os.Stderr, "HTTP/3 fixture failed")
		os.Exit(1)
	}
}
