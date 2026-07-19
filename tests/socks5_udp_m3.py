#!/usr/bin/env python3
"""Full-path RFC 1928 client for the native M3 CONNECT-UDP backend."""

import argparse
import socket
import struct

from socks5_udp_m2 import udp_associate, udp_packet


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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", required=True, type=int)
    parser.add_argument(
        "--mode",
        choices=("echo", "dns", "multi-target", "concurrent"),
        default="echo",
    )
    parser.add_argument("--target-host")
    parser.add_argument("--target-port", type=int)
    parser.add_argument("--payload")
    parser.add_argument("--force-domain", action="store_true")
    parser.add_argument("--echo-port", type=int)
    parser.add_argument("--dns-port", type=int)
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
    else:
        assert args.echo_port
        run_concurrent(args)


if __name__ == "__main__":
    main()
