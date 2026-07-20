#!/usr/bin/env python3
"""Association-cap, churn, resource-recovery, and soak probe."""

import argparse
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import time


REPO_TESTS = str(Path(__file__).resolve().parents[1])
sys.path.insert(0, REPO_TESTS)

from socks5_udp_m2 import open_control, send_command, udp_associate, udp_packet  # noqa: E402


def process_metrics(pid):
    lsof = shutil.which("lsof")
    assert lsof is not None, "lsof is required for resource metrics"
    rss = int(
        subprocess.check_output(
            ["/bin/ps", "-o", "rss=", "-p", str(pid)], text=True
        ).strip()
    )
    descriptors = len(
        subprocess.check_output([lsof, "-p", str(pid)], text=True)
        .splitlines()
    ) - 1
    return rss, descriptors


def close_all(associations):
    for control, udp, _ in associations:
        control.close()
        udp.close()
    associations.clear()


def verify_client_association_cap(args):
    associations = []
    try:
        for _ in range(256):
            associations.append(
                udp_associate("127.0.0.1", args.socks_port, socket.AF_INET)
            )
        rejected = open_control("127.0.0.1", args.socks_port, socket.AF_INET)
        try:
            reply = send_command(rejected, 3, "0.0.0.0", 0)
            assert reply[0] == 1, reply
        finally:
            rejected.close()

        for control, udp, _ in associations[:64]:
            control.close()
            udp.close()
        del associations[:64]
        for _ in range(64):
            associations.append(
                udp_associate("127.0.0.1", args.socks_port, socket.AF_INET)
            )
        assert len(associations) == 256
        runner_peak = process_metrics(args.runner_pid)
        caddy_peak = process_metrics(args.caddy_pid)
        print("M6_G3_ASSOCIATION_CAP_REUSE_OK active=256 rejected=1 reused=64")
        print(
            f"M6_G3_RESOURCE_PEAK process=runner rss_kib={runner_peak[0]} "
            f"fd={runner_peak[1]}"
        )
        print(
            f"M6_G3_RESOURCE_PEAK process=caddy rss_kib={caddy_peak[0]} "
            f"fd={caddy_peak[1]}"
        )
    finally:
        close_all(associations)


def run_wave(args, wave):
    associations = [
        udp_associate("127.0.0.1", args.socks_port, socket.AF_INET)
        for _ in range(args.wave_size)
    ]
    try:
        packets = []
        for index, (_, udp, relay) in enumerate(associations):
            payload = b"m6-stress" + wave.to_bytes(4, "big") + index.to_bytes(2, "big")
            packet = udp_packet("127.0.0.1", args.echo_port, payload)
            udp.settimeout(5)
            udp.sendto(packet, relay)
            packets.append(packet)
        for (_, udp, _), packet in zip(associations, packets):
            response, _ = udp.recvfrom(65535)
            assert response == packet, (len(response), len(packet))
        return len(associations)
    finally:
        close_all(associations)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--socks-port", type=int, required=True)
    parser.add_argument("--echo-port", type=int, required=True)
    parser.add_argument("--runner-pid", type=int, required=True)
    parser.add_argument("--caddy-pid", type=int, required=True)
    parser.add_argument("--duration-seconds", type=float, default=60)
    parser.add_argument("--wave-size", type=int, default=16)
    args = parser.parse_args()
    assert args.duration_seconds > 0
    assert 1 <= args.wave_size <= 32

    runner_before = process_metrics(args.runner_pid)
    caddy_before = process_metrics(args.caddy_pid)
    verify_client_association_cap(args)

    deadline = time.monotonic() + args.duration_seconds
    waves = 0
    datagrams = 0
    while time.monotonic() < deadline or waves == 0:
        datagrams += run_wave(args, waves)
        waves += 1

    time.sleep(2)
    runner_after = process_metrics(args.runner_pid)
    caddy_after = process_metrics(args.caddy_pid)
    for label, before, after in (
        ("runner", runner_before, runner_after),
        ("caddy", caddy_before, caddy_after),
    ):
        rss_limit = max(before[0] + 65536, int(before[0] * 1.5))
        assert after[0] <= rss_limit, (label, "rss", before[0], after[0], rss_limit)
        assert after[1] <= before[1] + 32, (
            label,
            "fd",
            before[1],
            after[1],
        )
        print(
            f"M6_G3_RESOURCE_SAMPLE process={label} "
            f"rss_before_kib={before[0]} rss_after_kib={after[0]} "
            f"fd_before={before[1]} fd_after={after[1]}"
        )
    print(f"M6_G3_CHURN_OK waves={waves} datagrams={datagrams}")
    print("M6_G3_RESOURCE_RECOVERY_OK")
    print("M6_G3_STRESS_SMOKE_OK")


if __name__ == "__main__":
    main()
