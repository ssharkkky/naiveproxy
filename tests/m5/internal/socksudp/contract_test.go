package socksudp

import (
	"strings"
	"testing"
)

func TestTargetAddrValidation(t *testing.T) {
	t.Parallel()
	tests := []struct {
		name    string
		address TargetAddr
		valid   bool
	}{
		{"ipv4", TargetAddr{AddressIPv4, "127.0.0.1", 443}, true},
		{"ipv6", TargetAddr{AddressIPv6, "::1", 443}, true},
		{"domain", TargetAddr{AddressDomain, "m5-target.localhost", 443}, true},
		{"zero port", TargetAddr{AddressIPv4, "127.0.0.1", 0}, false},
		{"wrong ipv4", TargetAddr{AddressIPv4, "::1", 443}, false},
		{"wrong ipv6", TargetAddr{AddressIPv6, "127.0.0.1", 443}, false},
		{"empty domain", TargetAddr{AddressDomain, "", 443}, false},
		{"long domain", TargetAddr{AddressDomain, strings.Repeat("x", 256), 443}, false},
		{"unknown type", TargetAddr{AddressType(0xff), "target", 443}, false},
	}
	for _, test := range tests {
		test := test
		t.Run(test.name, func(t *testing.T) {
			t.Parallel()
			err := test.address.Validate()
			if test.valid && err != nil {
				t.Fatalf("Validate() error = %v", err)
			}
			if !test.valid && err == nil {
				t.Fatal("Validate() unexpectedly succeeded")
			}
		})
	}
}

func TestTargetAddrIdentity(t *testing.T) {
	t.Parallel()
	address := TargetAddr{AddressDomain, "m5-target.localhost", 443}
	if got := address.Network(); got != "socks5-udp" {
		t.Fatalf("Network() = %q", got)
	}
	if got := address.String(); got != "m5-target.localhost:443" {
		t.Fatalf("String() = %q", got)
	}
}
