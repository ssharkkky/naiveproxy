#!/usr/bin/env python3
"""Single-client UDP forwarder recording only encrypted packet shape metadata."""

import argparse
import csv
import socket
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--bind-host", default="127.0.0.1")
    parser.add_argument("--bind-port", type=int, required=True)
    parser.add_argument("--upstream-host", default="127.0.0.1")
    parser.add_argument("--upstream-port", type=int, required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    upstream = (args.upstream_host, args.upstream_port)
    started_ns = time.monotonic_ns()
    connection_started_ns = None
    client = None
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as relay, open(
        args.output, "x", encoding="ascii", newline=""
    ) as output:
        relay.bind((args.bind_host, args.bind_port))
        writer = csv.writer(output)
        writer.writerow(
            (
                "relative_time_us",
                "direction",
                "packet_size",
                "connection_age_us",
            )
        )
        output.flush()
        print(f"M5_G5_SIZE_TAP_READY port={args.bind_port}", flush=True)
        while True:
            payload, peer = relay.recvfrom(65535)
            now_ns = time.monotonic_ns()
            if peer == upstream:
                if client is None:
                    continue
                destination = client
                direction = "server_to_client"
            else:
                if client is None:
                    client = peer
                    connection_started_ns = now_ns
                elif peer != client:
                    continue
                destination = upstream
                direction = "client_to_server"
            writer.writerow(
                (
                    (now_ns - started_ns) // 1000,
                    direction,
                    len(payload),
                    (now_ns - connection_started_ns) // 1000,
                )
            )
            output.flush()
            relay.sendto(payload, destination)


if __name__ == "__main__":
    main()
