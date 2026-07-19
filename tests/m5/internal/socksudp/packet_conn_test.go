package socksudp

import (
	"context"
	"encoding/binary"
	"fmt"
	"io"
	"net"
	"testing"
	"time"
)

func TestPacketCodecRoundTrip(t *testing.T) {
	t.Parallel()
	tests := []struct {
		name    string
		address TargetAddr
		payload []byte
	}{
		{"ipv4", TargetAddr{AddressIPv4, "192.0.2.1", 53}, []byte{0, 1, 2}},
		{"ipv6", TargetAddr{AddressIPv6, "2001:db8::1", 443}, nil},
		{"domain", TargetAddr{AddressDomain, "m5-target.localhost", 65535}, []byte("payload")},
	}
	for _, test := range tests {
		test := test
		t.Run(test.name, func(t *testing.T) {
			t.Parallel()
			packet, err := buildPacket(test.address, test.payload)
			if err != nil {
				t.Fatalf("buildPacket() error = %v", err)
			}
			address, payload, err := parsePacket(packet)
			if err != nil {
				t.Fatalf("parsePacket() error = %v", err)
			}
			if address != test.address {
				t.Fatalf("address = %#v, want %#v", address, test.address)
			}
			if string(payload) != string(test.payload) {
				t.Fatalf("payload = %x, want %x", payload, test.payload)
			}
		})
	}
}

func TestPacketCodecRejectsMalformed(t *testing.T) {
	t.Parallel()
	for _, packet := range [][]byte{
		nil,
		{0, 0, 1, byte(AddressIPv4), 127, 0, 0, 1, 0, 53},
		{0, 0, 0, 0xff},
		{0, 0, 0, byte(AddressDomain), 0},
		{0, 0, 0, byte(AddressIPv6), 0},
	} {
		if _, _, err := parsePacket(packet); err == nil {
			t.Fatalf("parsePacket(%x) unexpectedly succeeded", packet)
		}
	}
}

func TestDialPacketConnNoAuthAndUserPassword(t *testing.T) {
	for _, test := range []struct {
		name     string
		username string
		password string
	}{
		{"no-auth", "", ""},
		{"user-password", "m5-user", "m5-pass"},
	} {
		t.Run(test.name, func(t *testing.T) {
			server, stop := startTestSOCKSRelay(t, test.username, test.password)
			defer stop()
			ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
			defer cancel()
			conn, err := Dial(ctx, DialOptions{
				Server:         server,
				Username:       test.username,
				Password:       test.password,
				ControlTimeout: 2 * time.Second,
			})
			if err != nil {
				t.Fatalf("Dial() error = %v", err)
			}
			defer conn.Close()
			if conn.RelayAddr() == nil {
				t.Fatal("RelayAddr() is nil")
			}
			target := TargetAddr{AddressDomain, "m5-target.localhost", 443}
			payload := []byte{0, 1, 2, 0xff}
			if n, err := conn.WriteTo(payload, target); err != nil || n != len(payload) {
				t.Fatalf("WriteTo() = (%d, %v)", n, err)
			}
			if err := conn.SetReadDeadline(time.Now().Add(2 * time.Second)); err != nil {
				t.Fatal(err)
			}
			buffer := make([]byte, 64)
			n, address, err := conn.ReadFrom(buffer)
			if err != nil {
				t.Fatalf("ReadFrom() error = %v", err)
			}
			if got, ok := address.(TargetAddr); !ok || got != target {
				t.Fatalf("ReadFrom() address = %#v", address)
			}
			if string(buffer[:n]) != string(payload) {
				t.Fatalf("ReadFrom() payload = %x", buffer[:n])
			}
		})
	}
}

func startTestSOCKSRelay(t *testing.T, username, password string) (string, func()) {
	t.Helper()
	udp, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil {
		t.Fatal(err)
	}
	tcp, err := net.Listen("tcp4", "127.0.0.1:0")
	if err != nil {
		udp.Close()
		t.Fatal(err)
	}
	done := make(chan struct{})
	go func() {
		defer close(done)
		control, acceptErr := tcp.Accept()
		if acceptErr != nil {
			return
		}
		defer control.Close()
		greeting := make([]byte, 3)
		if _, err := io.ReadFull(control, greeting); err != nil {
			return
		}
		method := byte(socksNoAuth)
		if username != "" || password != "" {
			method = socksUserPassword
		}
		if greeting[0] != socksVersion || greeting[1] != 1 || greeting[2] != method {
			return
		}
		if _, err := control.Write([]byte{socksVersion, method}); err != nil {
			return
		}
		if method == socksUserPassword {
			header := make([]byte, 2)
			if _, err := io.ReadFull(control, header); err != nil || header[0] != 1 {
				return
			}
			user := make([]byte, int(header[1]))
			if _, err := io.ReadFull(control, user); err != nil {
				return
			}
			length := make([]byte, 1)
			if _, err := io.ReadFull(control, length); err != nil {
				return
			}
			pass := make([]byte, int(length[0]))
			if _, err := io.ReadFull(control, pass); err != nil {
				return
			}
			if string(user) != username || string(pass) != password {
				_, _ = control.Write([]byte{1, 1})
				return
			}
			if _, err := control.Write([]byte{1, 0}); err != nil {
				return
			}
		}
		request := make([]byte, 10)
		if _, err := io.ReadFull(control, request); err != nil {
			return
		}
		if request[0] != socksVersion || request[1] != socksUDPAssociate {
			return
		}
		relay := udp.LocalAddr().(*net.UDPAddr)
		reply := []byte{socksVersion, socksReplySucceeded, 0, byte(AddressIPv4)}
		reply = append(reply, relay.IP.To4()...)
		port := make([]byte, 2)
		binary.BigEndian.PutUint16(port, uint16(relay.Port))
		reply = append(reply, port...)
		if _, err := control.Write(reply); err != nil {
			return
		}
		_, _ = io.Copy(io.Discard, control)
	}()
	go func() {
		buffer := make([]byte, maxUDPDatagram)
		for {
			n, peer, err := udp.ReadFromUDP(buffer)
			if err != nil {
				return
			}
			_, _ = udp.WriteToUDP(buffer[:n], peer)
		}
	}()
	return tcp.Addr().String(), func() {
		_ = tcp.Close()
		_ = udp.Close()
		select {
		case <-done:
		case <-time.After(2 * time.Second):
			t.Errorf("SOCKS control goroutine did not stop: %s", fmt.Sprint(tcp.Addr()))
		}
	}
}
