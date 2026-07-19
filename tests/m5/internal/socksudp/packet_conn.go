package socksudp

import (
	"context"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"net"
	"sync"
	"time"
)

const (
	socksVersion        = 0x05
	socksUDPAssociate   = 0x03
	socksReplySucceeded = 0x00
	socksNoAuth         = 0x00
	socksUserPassword   = 0x02
	socksNoMethod       = 0xff
	maxUDPDatagram      = 65535
)

// Conn adapts one RFC 1928 UDP ASSOCIATE relay to net.PacketConn. The TCP
// control connection is retained for the complete packet-connection lifetime.
type Conn struct {
	control net.Conn
	udp     *net.UDPConn
	relay   *net.UDPAddr

	closeOnce sync.Once
	closeErr  error
}

var _ PacketConn = (*Conn)(nil)

// Dial establishes a SOCKS5 UDP association and binds the matching local UDP
// socket. Only RFC 1928 and optional RFC 1929 username/password are used.
func Dial(ctx context.Context, options DialOptions) (*Conn, error) {
	if options.Server == "" {
		return nil, errors.New("SOCKS server is empty")
	}
	if len(options.Username) > 255 || len(options.Password) > 255 {
		return nil, errors.New("SOCKS credentials are too long")
	}
	timeout := options.ControlTimeout
	if timeout <= 0 {
		timeout = 5 * time.Second
	}

	dialer := net.Dialer{Timeout: timeout}
	control, err := dialer.DialContext(ctx, "tcp", options.Server)
	if err != nil {
		return nil, fmt.Errorf("dial SOCKS control: %w", err)
	}
	fail := func(err error) (*Conn, error) {
		_ = control.Close()
		return nil, err
	}
	if err := control.SetDeadline(time.Now().Add(timeout)); err != nil {
		return fail(fmt.Errorf("set SOCKS handshake deadline: %w", err))
	}

	method := byte(socksNoAuth)
	if options.Username != "" || options.Password != "" {
		method = socksUserPassword
	}
	if err := writeFull(control, []byte{socksVersion, 1, method}); err != nil {
		return fail(fmt.Errorf("write SOCKS greeting: %w", err))
	}
	selection := make([]byte, 2)
	if _, err := io.ReadFull(control, selection); err != nil {
		return fail(fmt.Errorf("read SOCKS method: %w", err))
	}
	if selection[0] != socksVersion || selection[1] == socksNoMethod || selection[1] != method {
		return fail(errors.New("SOCKS authentication method rejected"))
	}
	if method == socksUserPassword {
		request := make([]byte, 0, 3+len(options.Username)+len(options.Password))
		request = append(request, 0x01, byte(len(options.Username)))
		request = append(request, options.Username...)
		request = append(request, byte(len(options.Password)))
		request = append(request, options.Password...)
		if err := writeFull(control, request); err != nil {
			return fail(fmt.Errorf("write SOCKS credentials: %w", err))
		}
		reply := make([]byte, 2)
		if _, err := io.ReadFull(control, reply); err != nil {
			return fail(fmt.Errorf("read SOCKS authentication result: %w", err))
		}
		if reply[0] != 0x01 || reply[1] != 0x00 {
			return fail(errors.New("SOCKS authentication failed"))
		}
	}

	request := []byte{socksVersion, socksUDPAssociate, 0x00, byte(AddressIPv4), 0, 0, 0, 0, 0, 0}
	if err := writeFull(control, request); err != nil {
		return fail(fmt.Errorf("write UDP ASSOCIATE: %w", err))
	}
	header := make([]byte, 4)
	if _, err := io.ReadFull(control, header); err != nil {
		return fail(fmt.Errorf("read UDP ASSOCIATE reply: %w", err))
	}
	if header[0] != socksVersion || header[2] != 0x00 {
		return fail(errors.New("invalid UDP ASSOCIATE reply"))
	}
	if header[1] != socksReplySucceeded {
		return fail(fmt.Errorf("UDP ASSOCIATE failed with reply 0x%02x", header[1]))
	}
	host, err := readHost(control, AddressType(header[3]))
	if err != nil {
		return fail(fmt.Errorf("read UDP relay address: %w", err))
	}
	portBytes := make([]byte, 2)
	if _, err := io.ReadFull(control, portBytes); err != nil {
		return fail(fmt.Errorf("read UDP relay port: %w", err))
	}
	port := int(binary.BigEndian.Uint16(portBytes))
	if port == 0 {
		return fail(errors.New("SOCKS UDP relay returned port zero"))
	}
	relay, err := net.ResolveUDPAddr("udp", net.JoinHostPort(host, fmt.Sprint(port)))
	if err != nil {
		return fail(fmt.Errorf("resolve UDP relay: %w", err))
	}
	if relay.IP == nil || relay.IP.IsUnspecified() {
		peerHost, _, splitErr := net.SplitHostPort(control.RemoteAddr().String())
		if splitErr != nil {
			return fail(errors.New("SOCKS UDP relay address is unspecified"))
		}
		relay.IP = net.ParseIP(peerHost)
	}
	network := "udp6"
	local := &net.UDPAddr{IP: net.IPv6unspecified}
	if relay.IP.To4() != nil {
		network = "udp4"
		local = &net.UDPAddr{IP: net.IPv4zero}
	}
	udp, err := net.ListenUDP(network, local)
	if err != nil {
		return fail(fmt.Errorf("bind local UDP socket: %w", err))
	}
	if err := control.SetDeadline(time.Time{}); err != nil {
		_ = udp.Close()
		return fail(fmt.Errorf("clear SOCKS handshake deadline: %w", err))
	}

	conn := &Conn{control: control, udp: udp, relay: relay}
	go conn.watchControl()
	return conn, nil
}

func (c *Conn) watchControl() {
	var buffer [1]byte
	for {
		if _, err := c.control.Read(buffer[:]); err != nil {
			_ = c.Close()
			return
		}
	}
}

func (c *Conn) ReadFrom(payload []byte) (int, net.Addr, error) {
	buffer := make([]byte, maxUDPDatagram)
	for {
		n, source, err := c.udp.ReadFromUDP(buffer)
		if err != nil {
			return 0, nil, err
		}
		if !sameUDPAddress(source, c.relay) {
			continue
		}
		address, datagram, err := parsePacket(buffer[:n])
		if err != nil {
			continue
		}
		if len(payload) < len(datagram) {
			copy(payload, datagram[:len(payload)])
			return len(payload), address, io.ErrShortBuffer
		}
		return copy(payload, datagram), address, nil
	}
}

func (c *Conn) WriteTo(payload []byte, address net.Addr) (int, error) {
	target, err := normalizeTarget(address)
	if err != nil {
		return 0, err
	}
	packet, err := buildPacket(target, payload)
	if err != nil {
		return 0, err
	}
	n, err := c.udp.WriteToUDP(packet, c.relay)
	if err != nil {
		return 0, err
	}
	if n != len(packet) {
		return 0, io.ErrShortWrite
	}
	return len(payload), nil
}

func (c *Conn) Close() error {
	c.closeOnce.Do(func() {
		controlErr := c.control.Close()
		udpErr := c.udp.Close()
		if controlErr != nil && !errors.Is(controlErr, net.ErrClosed) {
			c.closeErr = controlErr
		} else if udpErr != nil && !errors.Is(udpErr, net.ErrClosed) {
			c.closeErr = udpErr
		}
	})
	return c.closeErr
}

func (c *Conn) LocalAddr() net.Addr                       { return c.udp.LocalAddr() }
func (c *Conn) SetDeadline(deadline time.Time) error      { return c.udp.SetDeadline(deadline) }
func (c *Conn) SetReadDeadline(deadline time.Time) error  { return c.udp.SetReadDeadline(deadline) }
func (c *Conn) SetWriteDeadline(deadline time.Time) error { return c.udp.SetWriteDeadline(deadline) }
func (c *Conn) SetReadBuffer(bytes int) error             { return c.udp.SetReadBuffer(bytes) }
func (c *Conn) SetWriteBuffer(bytes int) error            { return c.udp.SetWriteBuffer(bytes) }
func (c *Conn) RelayAddr() net.Addr                       { return c.relay }
func (c *Conn) CloseControl() error                       { return c.Close() }

func normalizeTarget(address net.Addr) (TargetAddr, error) {
	switch value := address.(type) {
	case TargetAddr:
		return value, value.Validate()
	case *TargetAddr:
		if value == nil {
			return TargetAddr{}, errors.New("target address is nil")
		}
		return *value, value.Validate()
	case *net.UDPAddr:
		if value == nil || value.Port <= 0 || value.Port > 65535 {
			return TargetAddr{}, errors.New("invalid UDP target")
		}
		if value.IP.To4() != nil {
			result := TargetAddr{Type: AddressIPv4, Host: value.IP.String(), Port: uint16(value.Port)}
			return result, result.Validate()
		}
		result := TargetAddr{Type: AddressIPv6, Host: value.IP.String(), Port: uint16(value.Port)}
		return result, result.Validate()
	default:
		return TargetAddr{}, errors.New("target must preserve a SOCKS5 UDP address type")
	}
}

func buildPacket(address TargetAddr, payload []byte) ([]byte, error) {
	if err := address.Validate(); err != nil {
		return nil, err
	}
	packet := make([]byte, 0, len(payload)+262)
	packet = append(packet, 0x00, 0x00, 0x00, byte(address.Type))
	switch address.Type {
	case AddressIPv4:
		packet = append(packet, net.ParseIP(address.Host).To4()...)
	case AddressIPv6:
		packet = append(packet, net.ParseIP(address.Host).To16()...)
	case AddressDomain:
		packet = append(packet, byte(len(address.Host)))
		packet = append(packet, address.Host...)
	}
	port := make([]byte, 2)
	binary.BigEndian.PutUint16(port, address.Port)
	packet = append(packet, port...)
	packet = append(packet, payload...)
	if len(packet) > maxUDPDatagram {
		return nil, errors.New("SOCKS5 UDP datagram is too large")
	}
	return packet, nil
}

func parsePacket(packet []byte) (TargetAddr, []byte, error) {
	if len(packet) < 4 || packet[0] != 0 || packet[1] != 0 || packet[2] != 0 {
		return TargetAddr{}, nil, errors.New("invalid SOCKS5 UDP header")
	}
	addressType := AddressType(packet[3])
	offset := 4
	var host string
	switch addressType {
	case AddressIPv4:
		if len(packet) < offset+4 {
			return TargetAddr{}, nil, io.ErrUnexpectedEOF
		}
		host = net.IP(packet[offset : offset+4]).String()
		offset += 4
	case AddressIPv6:
		if len(packet) < offset+16 {
			return TargetAddr{}, nil, io.ErrUnexpectedEOF
		}
		host = net.IP(packet[offset : offset+16]).String()
		offset += 16
	case AddressDomain:
		if len(packet) < offset+1 {
			return TargetAddr{}, nil, io.ErrUnexpectedEOF
		}
		length := int(packet[offset])
		offset++
		if length == 0 || len(packet) < offset+length {
			return TargetAddr{}, nil, errors.New("invalid SOCKS5 UDP domain")
		}
		host = string(packet[offset : offset+length])
		offset += length
	default:
		return TargetAddr{}, nil, errors.New("unsupported SOCKS5 UDP address type")
	}
	if len(packet) < offset+2 {
		return TargetAddr{}, nil, io.ErrUnexpectedEOF
	}
	address := TargetAddr{
		Type: addressType,
		Host: host,
		Port: binary.BigEndian.Uint16(packet[offset : offset+2]),
	}
	if err := address.Validate(); err != nil {
		return TargetAddr{}, nil, err
	}
	return address, packet[offset+2:], nil
}

func readHost(reader io.Reader, addressType AddressType) (string, error) {
	var length int
	switch addressType {
	case AddressIPv4:
		length = 4
	case AddressIPv6:
		length = 16
	case AddressDomain:
		value := make([]byte, 1)
		if _, err := io.ReadFull(reader, value); err != nil {
			return "", err
		}
		length = int(value[0])
		if length == 0 {
			return "", errors.New("empty SOCKS address")
		}
	default:
		return "", errors.New("unsupported SOCKS address type")
	}
	value := make([]byte, length)
	if _, err := io.ReadFull(reader, value); err != nil {
		return "", err
	}
	if addressType == AddressDomain {
		return string(value), nil
	}
	return net.IP(value).String(), nil
}

func writeFull(writer io.Writer, payload []byte) error {
	for len(payload) > 0 {
		n, err := writer.Write(payload)
		if err != nil {
			return err
		}
		if n == 0 {
			return io.ErrShortWrite
		}
		payload = payload[n:]
	}
	return nil
}

func sameUDPAddress(left, right *net.UDPAddr) bool {
	return left != nil && right != nil && left.Port == right.Port && left.IP.Equal(right.IP)
}
