// Package socksudp defines the independent SOCKS5 UDP PacketConn contract
// used by the M5 application probe. It must not import NaiveProxy tunnel code.
package socksudp

import (
	"fmt"
	"net"
	"time"
)

// AddressType preserves the RFC 1928 wire identity of a target.
type AddressType byte

const (
	AddressIPv4   AddressType = 0x01
	AddressDomain AddressType = 0x03
	AddressIPv6   AddressType = 0x04
)

// TargetAddr is the address returned by the future PacketConn adapter.
type TargetAddr struct {
	Type AddressType
	Host string
	Port uint16
}

func (a TargetAddr) Network() string { return "socks5-udp" }

func (a TargetAddr) String() string {
	return net.JoinHostPort(a.Host, fmt.Sprint(a.Port))
}

// Validate enforces the target identity before RFC 1928 serialization.
func (a TargetAddr) Validate() error {
	if a.Port == 0 {
		return fmt.Errorf("target port is zero")
	}
	switch a.Type {
	case AddressIPv4:
		ip := net.ParseIP(a.Host)
		if ip == nil || ip.To4() == nil {
			return fmt.Errorf("invalid IPv4 target")
		}
	case AddressIPv6:
		ip := net.ParseIP(a.Host)
		if ip == nil || ip.To4() != nil {
			return fmt.Errorf("invalid IPv6 target")
		}
	case AddressDomain:
		if len(a.Host) == 0 || len(a.Host) > 255 {
			return fmt.Errorf("invalid domain target")
		}
	default:
		return fmt.Errorf("unsupported target address type")
	}
	return nil
}

// DialOptions freezes the independent probe's control-plane inputs.
type DialOptions struct {
	Server         string
	Username       string
	Password       string
	ControlTimeout time.Duration
}

// PacketConn is the contract the G2 HTTP/3 probe will implement. The retained
// TCP control channel owns the UDP association lifetime.
type PacketConn interface {
	net.PacketConn
	RelayAddr() net.Addr
	CloseControl() error
}
