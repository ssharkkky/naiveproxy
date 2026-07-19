#!/usr/bin/env python3
"""Full-path RFC 1928 client for the native M3 CONNECT-UDP backend."""

import argparse
import socket

from socks5_udp_m2 import udp_associate, udp_packet


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", required=True, type=int)
    parser.add_argument("--target-host", required=True)
    parser.add_argument("--target-port", required=True, type=int)
    parser.add_argument("--payload", required=True)
    parser.add_argument("--force-domain", action="store_true")
    parser.add_argument("--username")
    parser.add_argument("--password")
    parser.add_argument("--marker", required=True)
    args = parser.parse_args()

    family = socket.AF_INET6 if ":" in args.host else socket.AF_INET
    control, udp, relay = udp_associate(
        args.host,
        args.port,
        family,
        username=args.username,
        password=args.password,
    )
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


if __name__ == "__main__":
    main()
