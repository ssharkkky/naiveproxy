#!/usr/bin/env python3

import argparse
import socket


def main() -> None:
    parser = argparse.ArgumentParser(description="Local UDP echo for MASQUE tests")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=19000)
    args = parser.parse_args()

    family = socket.AF_INET6 if ":" in args.host else socket.AF_INET
    with socket.socket(family, socket.SOCK_DGRAM) as sock:
        sock.bind((args.host, args.port))
        print(f"READY udp://{args.host}:{args.port}", flush=True)
        while True:
            payload, peer = sock.recvfrom(65535)
            print(
                f"RX bytes={len(payload)} peer={peer[0]}:{peer[1]} "
                f"hex={payload.hex()}",
                flush=True,
            )
            sock.sendto(payload, peer)


if __name__ == "__main__":
    main()
