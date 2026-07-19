// socks-h3-probe is an independent M5 application probe. G0 freezes its
// dependency and PacketConn boundary; G2 adds the runtime SOCKS5/H3 path.
package main

import (
	"flag"
	"fmt"
	"os"

	"github.com/quic-go/quic-go/http3"
	"naiveproxy.local/m5/internal/socksudp"
)

func main() {
	contract := flag.Bool("contract", false, "print the frozen G0 contract")
	flag.Parse()
	if !*contract {
		fmt.Fprintln(os.Stderr, "runtime HTTP/3 mode is added in M5-G2")
		os.Exit(2)
	}
	address := socksudp.TargetAddr{
		Type: socksudp.AddressIPv4,
		Host: "127.0.0.1",
		Port: 443,
	}
	if err := address.Validate(); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	fmt.Printf("M5_G0_H3_PROBE_CONTRACT_OK alpn=%s network=%s\n",
		http3.NextProtoH3, address.Network())
}
