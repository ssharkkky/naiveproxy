#!/usr/bin/env python3
"""Product-level RFC 1928 matrix for the M5 production server path."""

import argparse
from pathlib import Path
import socket
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from socks5_udp_m2 import expect_no_udp, udp_associate, udp_packet
from socks5_udp_m3 import dns_query, extract_payload


def associate(port):
    return udp_associate("127.0.0.1", port, socket.AF_INET)


def expect_roundtrip(udp, relay, host, port, payload, force_domain=False):
    packet = udp_packet(host, port, payload, force_domain=force_domain)
    udp.settimeout(5)
    udp.sendto(packet, relay)
    response, _ = udp.recvfrom(65535)
    assert response == packet, (response.hex(), packet.hex())
    return packet


def run_primary_matrix(args):
    control, udp, relay = associate(args.socks_port)
    try:
        expect_roundtrip(udp, relay, "127.0.0.1", args.echo4_port, b"m5-g2-ipv4")
        expect_roundtrip(udp, relay, "::1", args.echo6_port, b"m5-g2-ipv6")
        expect_roundtrip(
            udp,
            relay,
            "localhost",
            args.echo4_port,
            b"m5-g2-domain",
            force_domain=True,
        )
        print("M5_G2_IPV4_IPV6_DOMAIN_OK")

        expect_roundtrip(
            udp,
            relay,
            "127.0.0.1",
            args.echo4_port,
            b"\x00\xffm5\x00binary\x80",
        )
        expect_roundtrip(udp, relay, "127.0.0.1", args.echo4_port, b"")
        expect_roundtrip(udp, relay, "127.0.0.1", args.echo4_port, b"s" * 1200)
        oversized = udp_packet("127.0.0.1", args.echo4_port, b"x" * 4096)
        for index in range(4):
            udp.sendto(oversized, relay)
            expect_no_udp(udp, f"M5 oversized datagram {index + 1}")
        expect_roundtrip(
            udp, relay, "127.0.0.1", args.echo4_port, b"m5-g2-after-oversize"
        )
        print("M5_G2_ZERO_OVERSIZE_OK")

        packets = [
            udp_packet("127.0.0.1", args.echo4_port, b"m5-g2-multi-ipv4"),
            udp_packet("::1", args.echo6_port, b"m5-g2-multi-ipv6"),
            udp_packet(
                "localhost",
                args.echo4_port,
                b"m5-g2-multi-domain",
                force_domain=True,
            ),
        ]
        for packet in packets:
            udp.sendto(packet, relay)
        responses = {udp.recvfrom(65535)[0] for _ in packets}
        assert responses == set(packets), [packet.hex() for packet in responses]
        print("M5_G2_MULTI_TARGET_OK")

        query = dns_query()
        dns_packet = udp_packet("127.0.0.1", args.dns_port, query)
        udp.sendto(dns_packet, relay)
        response, _ = udp.recvfrom(65535)
        payload = extract_payload(response)
        assert payload[:2] == query[:2]
        assert payload[2] & 0x80
        assert payload.endswith(socket.inet_aton("203.0.113.7")), payload.hex()
        print("M5_G2_DNS_OK")
    finally:
        control.close()
        udp.close()


def run_concurrent(args):
    associations = [associate(args.socks_port) for _ in range(4)]
    try:
        relays = [relay for _, _, relay in associations]
        assert len(set(relays)) == len(relays), relays
        packets = []
        for index, (_, udp, relay) in enumerate(associations):
            packet = udp_packet(
                "127.0.0.1",
                args.echo4_port,
                b"m5-g2-concurrent-" + bytes([index, 0, 0xFF - index]),
            )
            udp.settimeout(5)
            udp.sendto(packet, relay)
            packets.append(packet)
        for (_, udp, _), packet in zip(associations, packets):
            response, _ = udp.recvfrom(65535)
            assert response == packet, (response.hex(), packet.hex())
        print("M5_G2_CONCURRENT_ASSOCIATIONS_OK")
    finally:
        for control, udp, _ in associations:
            control.close()
            udp.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--socks-port", type=int, required=True)
    parser.add_argument("--echo4-port", type=int, required=True)
    parser.add_argument("--echo6-port", type=int, required=True)
    parser.add_argument("--dns-port", type=int, required=True)
    args = parser.parse_args()
    run_primary_matrix(args)
    run_concurrent(args)


if __name__ == "__main__":
    main()
