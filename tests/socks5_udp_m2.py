#!/usr/bin/env python3
"""Independent RFC 1928 wire oracle for the M2 SOCKS5 UDP ingress."""

import argparse
import ipaddress
import socket
import struct
import time


def recv_exact(sock, size):
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise AssertionError(f"unexpected EOF after {len(data)}/{size} bytes")
        data.extend(chunk)
    return bytes(data)


def address_bytes(host, force_domain=False):
    if force_domain:
        encoded = host.encode("ascii")
        return 3, bytes([len(encoded)]) + encoded
    try:
        return 1, socket.inet_pton(socket.AF_INET, host)
    except OSError:
        pass
    try:
        return 4, socket.inet_pton(socket.AF_INET6, host)
    except OSError:
        encoded = host.encode("ascii")
        return 3, bytes([len(encoded)]) + encoded


def read_reply(control):
    header = recv_exact(control, 4)
    assert header[0] == 5 and header[2] == 0, header.hex()
    atyp = header[3]
    if atyp == 1:
        raw = recv_exact(control, 4)
        host = socket.inet_ntop(socket.AF_INET, raw)
    elif atyp == 4:
        raw = recv_exact(control, 16)
        host = socket.inet_ntop(socket.AF_INET6, raw)
    elif atyp == 3:
        length = recv_exact(control, 1)[0]
        host = recv_exact(control, length).decode("ascii")
    else:
        raise AssertionError(f"unexpected reply ATYP={atyp}")
    port = struct.unpack("!H", recv_exact(control, 2))[0]
    return header[1], atyp, host, port


def open_control(
    host, port, family, fragmented=False, username=None, password=None
):
    control = socket.socket(family, socket.SOCK_STREAM)
    control.settimeout(5)
    control.connect((host, port))
    method = 2 if username is not None or password is not None else 0
    greeting = bytes([5, 1, method])
    if fragmented:
        for byte in greeting:
            control.sendall(bytes([byte]))
            time.sleep(0.01)
    else:
        control.sendall(greeting)
    assert recv_exact(control, 2) == bytes([5, method])
    if method == 2:
        encoded_user = (username or "").encode("utf-8")
        encoded_pass = (password or "").encode("utf-8")
        assert len(encoded_user) <= 255 and len(encoded_pass) <= 255
        control.sendall(
            bytes([1, len(encoded_user)])
            + encoded_user
            + bytes([len(encoded_pass)])
            + encoded_pass
        )
        assert recv_exact(control, 2) == b"\x01\x00"
    return control


def send_command(control, command, host, port, force_domain=False):
    atyp, encoded = address_bytes(host, force_domain)
    control.sendall(bytes([5, command, 0, atyp]) + encoded + struct.pack("!H", port))
    return read_reply(control)


def udp_packet(host, port, payload, frag=0, force_domain=False):
    atyp, encoded = address_bytes(host, force_domain)
    return b"\x00\x00" + bytes([frag, atyp]) + encoded + struct.pack("!H", port) + payload


def expect_no_udp(sock, description, allow_connection_refused=False):
    sock.settimeout(0.25)
    try:
        packet = sock.recvfrom(65535)
    except (TimeoutError, socket.timeout):
        return
    except ConnectionRefusedError:
        if allow_connection_refused:
            return
        raise
    raise AssertionError(f"{description}: unexpected UDP packet {packet!r}")


def find_bindable_non_loopback_ipv4():
    candidates = []
    try:
        candidates.extend(
            entry[4][0]
            for entry in socket.getaddrinfo(
                socket.gethostname(), 0, socket.AF_INET, socket.SOCK_DGRAM
            )
        )
    except socket.gaierror:
        pass
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        probe.connect(("192.0.2.1", 9))
        candidates.append(probe.getsockname()[0])
    except OSError:
        pass
    finally:
        probe.close()
    for candidate in dict.fromkeys(candidates):
        if ipaddress.ip_address(candidate).is_loopback:
            continue
        test = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            test.bind((candidate, 0))
            return candidate
        except OSError:
            pass
        finally:
            test.close()
    return None


def udp_associate(
    host, port, family, requested_port=0, username=None, password=None
):
    udp = socket.socket(family, socket.SOCK_DGRAM)
    udp.bind((host, 0))
    control = open_control(
        host,
        port,
        family,
        fragmented=True,
        username=username,
        password=password,
    )
    unspecified = "0.0.0.0" if family == socket.AF_INET else "::"
    request_port = requested_port or 0
    reply, atyp, relay_host, relay_port = send_command(
        control, 3, unspecified, request_port
    )
    assert reply == 0, reply
    assert relay_port != 0
    assert atyp == (1 if family == socket.AF_INET else 4)
    assert socket.inet_pton(family, relay_host) == socket.inet_pton(family, host)
    return control, udp, (relay_host, relay_port)


def run_success(host, port, username=None, password=None):
    family = socket.AF_INET6 if ":" in host else socket.AF_INET

    # A stalled partial handshake must not block the accept loop.
    stalled = socket.socket(family, socket.SOCK_STREAM)
    stalled.settimeout(5)
    stalled.connect((host, port))
    stalled.sendall(b"\x05")
    bind_control = open_control(
        host, port, family, username=username, password=password
    )
    reply = send_command(bind_control, 2, "127.0.0.1", 1)
    assert reply[0] == 7, reply
    bind_control.close()
    unsupported_control = open_control(
        host, port, family, username=username, password=password
    )
    reply = send_command(unsupported_control, 0x7F, "127.0.0.1", 1)
    assert reply[0] == 7, reply
    unsupported_control.close()
    stalled.close()

    # CONNECT keeps the byte-identical legacy zero BND response.
    connect_control = open_control(
        host, port, family, username=username, password=password
    )
    reply = send_command(connect_control, 1, "127.0.0.1", 1)
    assert reply == (0, 1, "0.0.0.0", 0), reply
    connect_control.close()
    print("M2_G2_HANDSHAKE_OK")
    print("M2_G3_BRANCHING_OK")

    control, udp, relay = udp_associate(
        host, port, family, username=username, password=password
    )
    udp.settimeout(2)

    # An invalid packet from another same-IP port must not pin a wildcard
    # association before the first valid packet arrives.
    prepin = socket.socket(family, socket.SOCK_DGRAM)
    prepin.bind((host, 0))
    prepin.sendto(b"\x00\x00\x00", relay)
    expect_no_udp(prepin, "invalid packet before wildcard port learning")
    prepin.close()

    target_host = "example.test"
    payload = b"m2-domain-echo\x00\xff"
    packet = udp_packet(target_host, 53, payload, force_domain=True)
    udp.sendto(packet, relay)
    echoed, _ = udp.recvfrom(65535)
    assert echoed == packet
    ipv6_packet = udp_packet("2001:db8::7", 443, b"m2-ipv6-target")
    udp.sendto(ipv6_packet, relay)
    assert udp.recvfrom(65535)[0] == ipv6_packet
    print("M2_G4_RELAY_OK")

    # Malformed and fragmented input is dropped without killing association.
    udp.sendto(b"\x00\x00\x01\x01\x7f\x00\x00\x01\x00\x35bad", relay)
    expect_no_udp(udp, "fragmented packet")
    udp.sendto(b"\x00\x00\x00", relay)
    expect_no_udp(udp, "truncated packet")
    second = udp_packet("127.0.0.1", 65535, b"after-drop")
    udp.settimeout(2)
    udp.sendto(second, relay)
    assert udp.recvfrom(65535)[0] == second

    # Once the wildcard port is learned, another source port cannot inject.
    spoof = socket.socket(family, socket.SOCK_DGRAM)
    spoof.bind((host, 0))
    spoof.sendto(udp_packet("127.0.0.1", 53, b"spoof"), relay)
    expect_no_udp(spoof, "spoof source")
    expect_no_udp(udp, "spoof packet redirected to authorized client")
    spoof.close()

    # On IPv4, also prove that a different source IP is rejected. macOS does
    # not bind arbitrary 127/8 aliases, so use a real secondary local address
    # when one is available.
    if family == socket.AF_INET:
        wrong_source = find_bindable_non_loopback_ipv4()
        if wrong_source is not None:
            wrong_ip = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            wrong_ip.bind((wrong_source, 0))
            wrong_ip.sendto(udp_packet("127.0.0.1", 53, b"wrong-ip"), relay)
            expect_no_udp(wrong_ip, "wrong source IP")
            expect_no_udp(
                udp, "wrong-IP packet redirected to authorized client"
            )
            wrong_ip.close()
            print("M2_G5_WRONG_SOURCE_IP_OK")
    print("M2_G5_SOURCE_AUTH_OK")

    # A burst of malformed datagrams must neither recurse without bound nor
    # starve the next valid packet.
    for _ in range(64):
        udp.sendto(b"\x00\x00\x00", relay)
    time.sleep(0.1)
    udp.settimeout(2)
    after_flood = udp_packet("127.0.0.1", 53, b"after-malformed-flood")
    udp.sendto(after_flood, relay)
    assert udp.recvfrom(65535)[0] == after_flood
    print("M2_G5_ASSOCIATION_OK")

    # Closing the TCP control channel tears down the relay.
    control.close()
    time.sleep(0.2)
    udp.sendto(second, relay)
    expect_no_udp(udp, "relay after control close", allow_connection_refused=True)
    udp.close()

    # A nonzero requested source port is enforced.
    fixed_udp = socket.socket(family, socket.SOCK_DGRAM)
    fixed_udp.bind((host, 0))
    fixed_port = fixed_udp.getsockname()[1]
    fixed_control = open_control(
        host, port, family, username=username, password=password
    )
    unspecified = "0.0.0.0" if family == socket.AF_INET else "::"
    reply, _, relay_host, relay_port = send_command(
        fixed_control, 3, unspecified, fixed_port
    )
    assert reply == 0
    wrong_port = socket.socket(family, socket.SOCK_DGRAM)
    wrong_port.bind((host, 0))
    wrong_port.sendto(
        udp_packet("127.0.0.1", 53, b"wrong-requested-port"),
        (relay_host, relay_port),
    )
    expect_no_udp(wrong_port, "wrong requested source port")
    wrong_port.close()
    fixed_udp.settimeout(2)
    fixed_packet = udp_packet("127.0.0.1", 53, b"fixed-port")
    fixed_udp.sendto(fixed_packet, (relay_host, relay_port))
    assert fixed_udp.recvfrom(65535)[0] == fixed_packet
    fixed_control.close()
    fixed_udp.close()

    # Two live associations must retain independent relay endpoints and
    # response routing.
    first_control, first_udp, first_relay = udp_associate(
        host, port, family, username=username, password=password
    )
    second_control, second_udp, second_relay = udp_associate(
        host, port, family, username=username, password=password
    )
    assert first_relay != second_relay
    first_packet = udp_packet("127.0.0.1", 1001, b"association-one")
    second_packet = udp_packet("127.0.0.1", 1002, b"association-two")
    first_udp.settimeout(2)
    second_udp.settimeout(2)
    first_udp.sendto(first_packet, first_relay)
    second_udp.sendto(second_packet, second_relay)
    assert second_udp.recvfrom(65535)[0] == second_packet
    assert first_udp.recvfrom(65535)[0] == first_packet
    first_control.close()
    second_control.close()
    first_udp.close()
    second_udp.close()
    print("M2_G5_CONCURRENCY_OK")
    print("M2_G5_LIFECYCLE_OK")
    if username is not None or password is not None:
        print("M2_G2_AUTHENTICATED_UDP_OK")


def run_rejection(
    host, port, marker, username=None, password=None
):
    family = socket.AF_INET6 if ":" in host else socket.AF_INET
    control = open_control(
        host, port, family, username=username, password=password
    )
    unspecified = "0.0.0.0" if family == socket.AF_INET else "::"
    reply = send_command(control, 3, unspecified, 0)
    assert reply == (1, 1, "0.0.0.0", 0), reply
    control.settimeout(2)
    assert control.recv(1) == b""
    control.close()
    print(marker)


def run_association_cap(host, port):
    family = socket.AF_INET6 if ":" in host else socket.AF_INET
    first_control, first_udp, _ = udp_associate(host, port, family)
    second_control, second_udp, _ = udp_associate(host, port, family)

    rejected = open_control(host, port, family)
    unspecified = "0.0.0.0" if family == socket.AF_INET else "::"
    reply = send_command(rejected, 3, unspecified, 0)
    assert reply == (1, 1, "0.0.0.0", 0), reply
    rejected.settimeout(2)
    assert rejected.recv(1) == b""
    rejected.close()

    first_control.close()
    first_udp.close()
    time.sleep(0.2)
    replacement_control, replacement_udp, _ = udp_associate(
        host, port, family
    )
    replacement_control.close()
    replacement_udp.close()
    second_control.close()
    second_udp.close()
    print("M3_G2_ACTIVE_ASSOCIATION_LIMIT_OK")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", required=True, type=int)
    parser.add_argument(
        "--mode",
        choices=(
            "success",
            "rejection",
            "no-backend-rejection",
            "association-cap",
        ),
        required=True,
    )
    parser.add_argument("--username")
    parser.add_argument("--password")
    args = parser.parse_args()
    if args.mode == "success":
        run_success(args.host, args.port, args.username, args.password)
    elif args.mode == "association-cap":
        run_association_cap(args.host, args.port)
    else:
        marker = (
            "M2_G3_NON_QUIC_REJECTION_OK"
            if args.mode == "rejection"
            else "M2_G3_NO_BACKEND_REJECTION_OK"
        )
        run_rejection(
            args.host, args.port, marker, args.username, args.password
        )


if __name__ == "__main__":
    main()
