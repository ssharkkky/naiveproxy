#!/usr/bin/env python3
"""Test-only deterministic UDP impairment shaper.

Logs retain only sequence, direction, size, configured ceiling, action, reason,
and relative delay. Packet bytes and endpoint addresses are never logged.
"""

import argparse
import heapq
import json
import pathlib
import random
import selectors
import socket
import time


def read_ceiling(path):
    try:
        value = int(path.read_text(encoding="ascii").strip())
    except (OSError, ValueError):
        return 0
    return max(0, value)


class ImpairmentPolicy:
    def __init__(self, args):
        self.random = random.Random(args.seed)
        self.loss_percent = args.loss_percent
        self.reorder_percent = args.reorder_percent
        self.delay_ms = args.delay_ms
        self.jitter_ms = args.jitter_ms
        self.reorder_delay_ms = args.reorder_delay_ms
        self.bandwidth_kbps = args.bandwidth_kbps
        self.next_available = {"c2s": 0.0, "s2c": 0.0}

    def decide(self, direction, size, now):
        if self.loss_percent > 0 and self.random.random() * 100 < self.loss_percent:
            return True, 0.0, "loss"

        delay_ms = self.delay_ms
        if self.jitter_ms > 0:
            delay_ms += self.random.uniform(-self.jitter_ms, self.jitter_ms)
        delay_ms = max(0.0, delay_ms)
        reason = "delay" if delay_ms > 0 else "none"

        if (
            self.reorder_percent > 0
            and self.random.random() * 100 < self.reorder_percent
        ):
            delay_ms += self.reorder_delay_ms
            reason = "reorder"

        send_at = now + delay_ms / 1000.0
        if self.bandwidth_kbps > 0:
            send_at = max(send_at, self.next_available[direction])
            serialization = size * 8 / (self.bandwidth_kbps * 1000)
            self.next_available[direction] = send_at + serialization
            send_at = self.next_available[direction]
            if reason == "none":
                reason = "bandwidth"
        return False, send_at, reason


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--listen-host", default="127.0.0.1")
    parser.add_argument("--listen-port", type=int, required=True)
    parser.add_argument("--server-host", default="127.0.0.1")
    parser.add_argument("--server-port", type=int, required=True)
    parser.add_argument("--ceiling-file", type=pathlib.Path, required=True)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--loss-percent", type=float, default=0)
    parser.add_argument("--reorder-percent", type=float, default=0)
    parser.add_argument("--delay-ms", type=float, default=0)
    parser.add_argument("--jitter-ms", type=float, default=0)
    parser.add_argument("--reorder-delay-ms", type=float, default=25)
    parser.add_argument("--bandwidth-kbps", type=float, default=0)
    parser.add_argument("--profiles-file", type=pathlib.Path)
    parser.add_argument("--profile")
    args = parser.parse_args()
    if args.profile:
        if not args.profiles_file:
            parser.error("--profile requires --profiles-file")
        document = json.loads(args.profiles_file.read_text(encoding="utf-8"))
        matches = [item for item in document["profiles"] if item["id"] == args.profile]
        if len(matches) != 1:
            parser.error("profile must name exactly one configured profile")
        profile = matches[0]
        for key in (
            "seed",
            "loss_percent",
            "reorder_percent",
            "delay_ms",
            "jitter_ms",
            "bandwidth_kbps",
        ):
            setattr(args, key, profile[key])
        args.reorder_delay_ms = document["reorder_delay_ms"]
    for value in (args.loss_percent, args.reorder_percent):
        if not 0 <= value <= 100:
            parser.error("percent values must be between 0 and 100")
    for value in (
        args.delay_ms,
        args.jitter_ms,
        args.reorder_delay_ms,
        args.bandwidth_kbps,
    ):
        if value < 0:
            parser.error("delay and bandwidth values must be non-negative")
    return args


def main():
    args = parse_args()
    server = (args.server_host, args.server_port)
    client = None
    sequence = 0
    pending = []
    policy = ImpairmentPolicy(args)
    selector = selectors.DefaultSelector()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.listen_host, args.listen_port))
    sock.setblocking(False)
    selector.register(sock, selectors.EVENT_READ)
    started = time.monotonic()
    profile = args.profile or "custom"
    print(f"READY udp-shaper profile={profile} seed={args.seed}", flush=True)

    while True:
        now = time.monotonic()
        while pending and pending[0][0] <= now:
            _, queued_sequence, destination, packet, direction, size, ceiling, reason = (
                heapq.heappop(pending)
            )
            sock.sendto(packet, destination)
            elapsed_ms = int((now - started) * 1000)
            print(
                f"PACKET sequence={queued_sequence} direction={direction} "
                f"size={size} ceiling={ceiling} action=forward reason={reason} "
                f"elapsed_ms={elapsed_ms}",
                flush=True,
            )

        timeout = None
        if pending:
            timeout = max(0.0, pending[0][0] - time.monotonic())
        events = selector.select(timeout)
        if not events:
            continue

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
        if ceiling > 0 and len(packet) > ceiling:
            print(
                f"PACKET sequence={sequence} direction={direction} "
                f"size={len(packet)} ceiling={ceiling} action=drop "
                "reason=ceiling elapsed_ms=0",
                flush=True,
            )
            continue

        drop, send_at, reason = policy.decide(
            direction, len(packet), time.monotonic()
        )
        if drop:
            print(
                f"PACKET sequence={sequence} direction={direction} "
                f"size={len(packet)} ceiling={ceiling} action=drop "
                f"reason={reason} elapsed_ms=0",
                flush=True,
            )
            continue
        if destination is None:
            continue
        heapq.heappush(
            pending,
            (
                send_at,
                sequence,
                destination,
                packet,
                direction,
                len(packet),
                ceiling,
                reason,
            ),
        )


if __name__ == "__main__":
    main()
