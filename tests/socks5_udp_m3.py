#!/usr/bin/env python3
"""Full-path RFC 1928 client for the native M3 CONNECT-UDP backend."""

import argparse
import socket
import struct
import time

from socks5_udp_m2 import expect_no_udp, udp_associate, udp_packet


def associate(args):
    family = socket.AF_INET6 if ":" in args.host else socket.AF_INET
    return udp_associate(
        args.host,
        args.port,
        family,
        username=args.username,
        password=args.password,
    )


def run_echo(args):
    control, udp, relay = associate(args)
    payload = args.payload.encode("utf-8")
    packet = udp_packet(
        args.target_host,
        args.target_port,
        payload,
        force_domain=args.force_domain,
    )
    udp.settimeout(5)
    udp.sendto(packet, relay)
    response, _ = udp.recvfrom(65535)
    assert response == packet, (response.hex(), packet.hex())
    control.close()
    udp.close()
    print(args.marker)


def dns_query():
    qname = b"".join(bytes([len(label)]) + label for label in b"m3.test".split(b"."))
    return (
        struct.pack("!HHHHHH", 0x4D33, 0x0100, 1, 0, 0, 0)
        + qname
        + b"\x00"
        + struct.pack("!HH", 1, 1)
    )


def extract_payload(packet):
    assert packet[:3] == b"\x00\x00\x00", packet.hex()
    atyp = packet[3]
    if atyp == 1:
        offset = 8
    elif atyp == 4:
        offset = 20
    elif atyp == 3:
        offset = 5 + packet[4]
    else:
        raise AssertionError(f"unexpected ATYP={atyp}")
    return packet[offset + 2 :]


def run_dns(args):
    control, udp, relay = associate(args)
    query = dns_query()
    packet = udp_packet("127.0.0.1", args.dns_port, query)
    udp.settimeout(5)
    udp.sendto(packet, relay)
    response, _ = udp.recvfrom(65535)
    payload = extract_payload(response)
    header = struct.unpack("!HHHHHH", payload[:12])
    assert header[0] == 0x4D33 and header[1] & 0x8000
    assert header[2:4] == (1, 1), header
    assert payload.endswith(socket.inet_aton("203.0.113.7")), payload.hex()
    control.close()
    udp.close()
    print("M3_G4_DNS_OK")


def run_multi_target(args):
    control, udp, relay = associate(args)
    packets = [
        udp_packet("127.0.0.1", args.echo_port, b"m3-multi-ipv4"),
        udp_packet("::1", args.echo_port, b"m3-multi-ipv6"),
        udp_packet(
            "localhost",
            args.echo_port,
            b"m3-multi-domain",
            force_domain=True,
        ),
    ]
    udp.settimeout(5)
    for packet in packets:
        udp.sendto(packet, relay)
    responses = {udp.recvfrom(65535)[0] for _ in packets}
    assert responses == set(packets), ([item.hex() for item in responses])
    control.close()
    udp.close()
    print("M3_G4_MULTI_TARGET_OK")


def run_concurrent(args):
    associations = [associate(args) for _ in range(4)]
    assert len({relay for _, _, relay in associations}) == len(associations)
    packets = []
    for index, (_, udp, relay) in enumerate(associations):
        packet = udp_packet(
            "127.0.0.1", args.echo_port, f"m3-concurrent-{index}".encode()
        )
        udp.settimeout(5)
        udp.sendto(packet, relay)
        packets.append(packet)
    for (control, udp, _), packet in zip(associations, packets):
        assert udp.recvfrom(65535)[0] == packet
        control.close()
        udp.close()
    print("M3_G4_CONCURRENT_ASSOCIATIONS_OK")


def assert_control_open(control):
    control.settimeout(0.2)
    try:
        data = control.recv(1)
    except (TimeoutError, socket.timeout):
        return
    assert data != b"", "SOCKS control connection closed unexpectedly"
    raise AssertionError(f"unexpected SOCKS control data: {data.hex()}")


def run_reconnect(args):
    control, udp, relay = associate(args)
    before = udp_packet("127.0.0.1", args.echo_port, b"m3-before-shutdown")
    after = udp_packet("127.0.0.1", args.echo_port, b"m3-after-shutdown")
    udp.settimeout(5)
    udp.sendto(before, relay)
    assert udp.recvfrom(65535)[0] == before
    time.sleep(args.pause_seconds)
    udp.sendto(after, relay)
    assert udp.recvfrom(65535)[0] == after
    control.close()
    udp.close()
    print(args.marker)


def run_zero_oversize(args):
    control, udp, relay = associate(args)
    udp.settimeout(5)
    empty = udp_packet("127.0.0.1", args.echo_port, b"")
    udp.sendto(empty, relay)
    assert udp.recvfrom(65535)[0] == empty

    oversized = udp_packet(
        "127.0.0.1", args.echo_port, b"x" * args.oversize_bytes
    )
    for index in range(4):
        udp.sendto(oversized, relay)
        expect_no_udp(udp, f"oversized CONNECT-UDP payload {index + 1}")

    after = udp_packet("127.0.0.1", args.echo_port, b"m3-after-oversize")
    udp.settimeout(5)
    udp.sendto(after, relay)
    assert udp.recvfrom(65535)[0] == after
    control.close()
    udp.close()
    print("M3_G5_ZERO_OVERSIZE_OK")


def run_expected_target_failure(args):
    control, udp, relay = associate(args)
    first = udp_packet("127.0.0.1", args.echo_port, b"m3-failure-first")
    second = udp_packet("127.0.0.1", args.echo_port, b"m3-failure-second")
    udp.sendto(first, relay)
    expect_no_udp(udp, "first target failure")
    assert_control_open(control)
    time.sleep(args.pause_seconds)
    udp.sendto(second, relay)
    expect_no_udp(udp, "second target failure")
    assert_control_open(control)
    control.close()
    udp.close()
    print(args.marker)


def run_hold_until_proxy_close(args):
    control, udp, relay = associate(args)
    packet = udp_packet("127.0.0.1", args.echo_port, b"m3-pending-read-close")
    udp.settimeout(5)
    udp.sendto(packet, relay)
    assert udp.recvfrom(65535)[0] == packet
    control.settimeout(5)
    assert control.recv(1) == b"", "proxy did not close the control channel"
    control.close()
    udp.close()
    print("M3_G5_BACKEND_DESTRUCTION_OK")


def run_close_pending_connect(args):
    control, udp, relay = associate(args)
    udp.sendto(
        udp_packet("127.0.0.1", args.echo_port, b"m3-pending-connect-close"),
        relay,
    )
    time.sleep(0.2)
    control.close()
    udp.close()
    print("M3_G5_PENDING_CONNECT_CLOSE_OK")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", required=True, type=int)
    parser.add_argument(
        "--mode",
        choices=(
            "echo",
            "dns",
            "multi-target",
            "concurrent",
            "reconnect",
            "zero-oversize",
            "target-failure",
            "hold-until-proxy-close",
            "close-pending-connect",
        ),
        default="echo",
    )
    parser.add_argument("--target-host")
    parser.add_argument("--target-port", type=int)
    parser.add_argument("--payload")
    parser.add_argument("--force-domain", action="store_true")
    parser.add_argument("--echo-port", type=int)
    parser.add_argument("--dns-port", type=int)
    parser.add_argument("--pause-seconds", type=float, default=1.5)
    parser.add_argument("--oversize-bytes", type=int, default=4096)
    parser.add_argument("--username")
    parser.add_argument("--password")
    parser.add_argument("--marker")
    args = parser.parse_args()
    if args.mode == "echo":
        assert args.target_host and args.target_port and args.payload and args.marker
        run_echo(args)
    elif args.mode == "dns":
        assert args.dns_port
        run_dns(args)
    elif args.mode == "multi-target":
        assert args.echo_port
        run_multi_target(args)
    elif args.mode == "concurrent":
        assert args.echo_port
        run_concurrent(args)
    elif args.mode == "reconnect":
        assert args.echo_port and args.marker
        run_reconnect(args)
    elif args.mode == "zero-oversize":
        assert args.echo_port and args.oversize_bytes > 0
        run_zero_oversize(args)
    elif args.mode == "target-failure":
        assert args.echo_port and args.marker
        run_expected_target_failure(args)
    elif args.mode == "hold-until-proxy-close":
        assert args.echo_port
        run_hold_until_proxy_close(args)
    else:
        assert args.echo_port
        run_close_pending_connect(args)


if __name__ == "__main__":
    main()
