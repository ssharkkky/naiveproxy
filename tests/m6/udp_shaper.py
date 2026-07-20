#!/usr/bin/env python3
"""Test-only UDP forwarding shaper with a live packet-size ceiling."""

import argparse
import pathlib
import socket


def read_ceiling(path):
    try:
        value = int(path.read_text(encoding="ascii").strip())
    except (OSError, ValueError):
        return 0
    return max(0, value)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--listen-host", default="127.0.0.1")
    parser.add_argument("--listen-port", type=int, required=True)
    parser.add_argument("--server-host", default="127.0.0.1")
    parser.add_argument("--server-port", type=int, required=True)
    parser.add_argument("--ceiling-file", type=pathlib.Path, required=True)
    args = parser.parse_args()

    server = (args.server_host, args.server_port)
    client = None
    sequence = 0
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.listen_host, args.listen_port))
    print("READY udp-shaper", flush=True)

    while True:
        packet, peer = sock.recvfrom(65535)
        sequence += 1
        if peer == server:
            direction = "s2c"
            destination = client
        else:
            direction = "c2s"
            client = peer
            destination = server
        ceiling = read_ceiling(args.ceiling_file)
        drop = ceiling > 0 and len(packet) > ceiling
        action = "drop" if drop else "forward"
        print(
            f"PACKET sequence={sequence} direction={direction} "
            f"size={len(packet)} ceiling={ceiling} action={action}",
            flush=True,
        )
        if not drop and destination is not None:
            sock.sendto(packet, destination)


if __name__ == "__main__":
    main()
