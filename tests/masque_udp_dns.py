#!/usr/bin/env python3
"""Deterministic UDP DNS fixture for the M3 full-path test."""

import argparse
import socket
import struct


ANSWER = socket.inet_aton("203.0.113.7")


def question_end(packet):
    offset = 12
    while True:
        if offset >= len(packet):
            raise ValueError("truncated qname")
        length = packet[offset]
        offset += 1
        if length == 0:
            break
        if length & 0xC0 or offset + length > len(packet):
            raise ValueError("invalid qname")
        offset += length
    if offset + 4 > len(packet):
        raise ValueError("truncated question")
    return offset + 4


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", required=True, type=int)
    args = parser.parse_args()
    family = socket.AF_INET6 if ":" in args.host else socket.AF_INET
    with socket.socket(family, socket.SOCK_DGRAM) as sock:
        sock.bind((args.host, args.port))
        print(f"READY dns://{args.host}:{args.port}", flush=True)
        while True:
            query, peer = sock.recvfrom(65535)
            try:
                if len(query) < 12:
                    raise ValueError("truncated header")
                transaction_id, _, qdcount, _, _, _ = struct.unpack(
                    "!HHHHHH", query[:12]
                )
                if qdcount != 1:
                    raise ValueError("expected one question")
                end = question_end(query)
                question = query[12:end]
                response = (
                    struct.pack("!HHHHHH", transaction_id, 0x8180, 1, 1, 0, 0)
                    + question
                    + b"\xc0\x0c"
                    + struct.pack("!HHIH", 1, 1, 60, len(ANSWER))
                    + ANSWER
                )
            except ValueError as error:
                print(f"DROP reason={error}", flush=True)
                continue
            print(f"RX query_bytes={len(query)} response_bytes={len(response)}", flush=True)
            sock.sendto(response, peer)


if __name__ == "__main__":
    main()
