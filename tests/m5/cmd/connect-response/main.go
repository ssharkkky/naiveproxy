// Controlled HTTP/2 CONNECT response fixture. Never linked into the product.
package main

import (
	"flag"
	"fmt"
	"log"
	"net"
	"net/http"
	"time"
)

func main() {
	address := flag.String("listen", "127.0.0.1:0", "test listener")
	cert := flag.String("cert", "", "test certificate")
	key := flag.String("key", "", "test key")
	status := flag.Int("status", 502, "CONNECT response code")
	flag.Parse()
	listener, err := net.Listen("tcp", *address)
	if err != nil {
		log.Fatal(err)
	}
	server := &http.Server{ReadHeaderTimeout: 5 * time.Second, Handler: http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodConnect || r.ProtoMajor != 2 {
			w.WriteHeader(http.StatusBadRequest)
			return
		}
		select {
		case <-r.Context().Done():
			return
		case <-time.After(500 * time.Millisecond):
		}
		w.Header().Set("Padding-Type-Reply", "0")
		w.WriteHeader(*status)
		if err := http.NewResponseController(w).Flush(); err != nil {
			return
		}
		// Leave the stream open so the response itself must complete CONNECT.
		<-r.Context().Done()
	})}
	fmt.Printf("READY %s\n", listener.Addr())
	log.Fatal(server.ServeTLS(listener, *cert, *key))
}
