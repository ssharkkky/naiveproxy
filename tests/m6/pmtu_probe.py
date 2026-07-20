#!/usr/bin/env python3
"""Exercise a lowered/restored outer-QUIC packet ceiling through SOCKS UDP."""

import argparse
import pathlib
import socket
import sys
import time


REPO_TESTS = str(__file__).rsplit("/m6/", 1)[0]
sys.path.insert(0, REPO_TESTS)
sys.path.insert(0, str(pathlib.Path(__file__).parent))

from payload_probe import roundtrip  # noqa: E402
from socks5_udp_m2 import udp_associate, udp_packet  # noqa: E402


def set_ceiling(path, value):
    temporary = path.with_suffix(".new")
    temporary.write_text(f"{value}\n", encoding="ascii")
    temporary.replace(path)
    time.sleep(0.1)


def assert_no_delayed_packet(sock, timeout):
    sock.settimeout(timeout)
    try:
        packet = sock.recvfrom(65535)
    except (TimeoutError, socket.timeout):
        return
    raise AssertionError(f"ambiguous datagram was replayed: bytes={len(packet[0])}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--socks-port", type=int, required=True)
    parser.add_argument("--echo-port", type=int, required=True)
    parser.add_argument("--ceiling-file", type=pathlib.Path, required=True)
    parser.add_argument("--live-payload", type=int, default=1314)
    parser.add_argument("--safe-payload", type=int, default=1200)
    # An IPv6 path MTU of 1280 leaves 1232 bytes after IPv6 and UDP headers.
    parser.add_argument("--outer-ceiling", type=int, default=1232)
    parser.add_argument("--timeout", type=float, default=0.5)
    args = parser.parse_args()

    control, udp, relay = udp_associate("127.0.0.1", args.socks_port, socket.AF_INET)
    other_control, other_udp, other_relay = udp_associate(
        "127.0.0.1", args.socks_port, socket.AF_INET
    )
    try:
        set_ceiling(args.ceiling_file, 0)
        assert roundtrip(
            udp,
            relay,
            "127.0.0.1",
            args.echo_port,
            args.live_payload,
            1,
            False,
            args.timeout,
        )

        set_ceiling(args.ceiling_file, args.outer_ceiling)
        assert roundtrip(
            udp,
            relay,
            "127.0.0.1",
            args.echo_port,
            args.safe_payload,
            2,
            False,
            args.timeout,
        ), "safe release payload failed under an IPv6-minimum-sized path"
        assert not roundtrip(
            udp,
            relay,
            "127.0.0.1",
            args.echo_port,
            args.live_payload,
            3,
            False,
            args.timeout,
        ), "live maximum unexpectedly survived the lowered outer ceiling"
        assert roundtrip(
            other_udp,
            other_relay,
            "::1",
            args.echo_port,
            64,
            4,
            False,
            args.timeout,
        ), "unrelated association did not remain healthy under lower PMTU"

        set_ceiling(args.ceiling_file, 0)
        assert_no_delayed_packet(udp, args.timeout)
        assert roundtrip(
            udp,
            relay,
            "127.0.0.1",
            args.echo_port,
            args.live_payload,
            5,
            False,
            args.timeout,
        ), "live maximum did not recover after restoring the outer ceiling"
        print(f"M6_G1_PMTU_SAFE_PAYLOAD_OK bytes={args.safe_payload}")
        print("M6_G1_PMTU_ISOLATION_OK")
        print("M6_G1_PMTU_NO_REPLAY_OK")
        print("M6_G1C_PMTU_RECOVERY_OK")
    finally:
        control.close()
        udp.close()
        other_control.close()
        other_udp.close()


if __name__ == "__main__":
    main()
