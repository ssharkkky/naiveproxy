#!/usr/bin/env python3
"""Lifecycle, reconnect, idle, and no-replay probes for M5-G4."""

import argparse
import os
from pathlib import Path
import socket
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from socks5_udp_m2 import expect_no_udp, udp_associate, udp_packet


def associate(args):
    return udp_associate("127.0.0.1", args.socks_port, socket.AF_INET)


def roundtrip(udp, relay, host, port, payload, force_domain=False):
    packet = udp_packet(host, port, payload, force_domain=force_domain)
    udp.settimeout(5)
    udp.sendto(packet, relay)
    response, _ = udp.recvfrom(65535)
    assert response == packet, (response.hex(), packet.hex())
    return packet


def run_control_close(args):
    idle_control, idle_udp, idle_relay = associate(args)
    idle_control.close()
    time.sleep(0.2)
    idle_udp.sendto(
        udp_packet("127.0.0.1", args.target_port, b"m5-g4-idle-control"),
        idle_relay,
    )
    expect_no_udp(
        idle_udp, "idle relay after control close", allow_connection_refused=True
    )
    idle_udp.close()

    open_control, open_udp, open_relay = associate(args)
    roundtrip(
        open_udp,
        open_relay,
        "127.0.0.1",
        args.target_port,
        b"m5-g4-open-before-control-close",
    )
    open_control.close()
    time.sleep(0.3)
    open_udp.sendto(
        udp_packet(
            "127.0.0.1", args.target_port, b"m5-g4-open-after-control-close"
        ),
        open_relay,
    )
    expect_no_udp(
        open_udp, "open relay after control close", allow_connection_refused=True
    )
    open_udp.close()

    pending_control, pending_udp, pending_relay = associate(args)
    pending_udp.sendto(
        udp_packet(
            "127.0.0.1", args.pending_port, b"m5-g4-pending-control-close"
        ),
        pending_relay,
    )
    time.sleep(0.2)
    pending_control.close()
    time.sleep(0.3)
    expect_no_udp(
        pending_udp,
        "pending relay after control close",
        allow_connection_refused=True,
    )
    pending_udp.close()
    print("M5_G4_CONTROL_CLOSE_OK")


def wait_for_file(path, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if os.path.exists(path):
            return
        time.sleep(0.05)
    raise AssertionError(f"timed out waiting for {path}")


def run_restart(args):
    control, udp, relay = associate(args)
    try:
        roundtrip(
            udp,
            relay,
            "127.0.0.1",
            args.target_port,
            b"m5-g4-restart-before-0001",
        )
        with open(args.ready_file, "x", encoding="ascii") as ready:
            ready.write("ready\n")
        wait_for_file(args.continue_file, 15)

        ambiguous = udp_packet(
            "127.0.0.1", args.target_port, b"m5-g4-restart-ambiguous-0002"
        )
        udp.sendto(ambiguous, relay)
        expect_no_udp(udp, "ambiguous datagram during server restart")
        time.sleep(1.2)
        roundtrip(
            udp,
            relay,
            "127.0.0.1",
            args.target_port,
            b"m5-g4-restart-fresh-0003",
        )
    finally:
        control.close()
        udp.close()
    print("M5_G4_SERVER_RESTART_OK")


def run_session_reconnect(args):
    control, udp, relay = associate(args)
    try:
        roundtrip(
            udp,
            relay,
            "127.0.0.1",
            args.target_port,
            b"m5-g4-session-a-before-0011",
        )
        roundtrip(
            udp,
            relay,
            "localhost",
            args.target_port,
            b"m5-g4-session-b-before-0012",
            force_domain=True,
        )
        time.sleep(args.pause_seconds)
        ambiguous = udp_packet(
            "127.0.0.1", args.target_port, b"m5-g4-session-ambiguous-0013"
        )
        udp.sendto(ambiguous, relay)
        expect_no_udp(udp, "datagram during session cooldown")
        time.sleep(1.2)
        roundtrip(
            udp,
            relay,
            "127.0.0.1",
            args.target_port,
            b"m5-g4-session-a-fresh-0014",
        )
        roundtrip(
            udp,
            relay,
            "localhost",
            args.target_port,
            b"m5-g4-session-b-fresh-0015",
            force_domain=True,
        )
    finally:
        control.close()
        udp.close()
    print("M5_G4_QUIC_RECONNECT_OK")


def run_idle(args):
    control, udp, relay = associate(args)
    try:
        roundtrip(
            udp,
            relay,
            "127.0.0.1",
            args.target_port,
            args.before_payload.encode("ascii"),
        )
        time.sleep(args.pause_seconds)
        roundtrip(
            udp,
            relay,
            "127.0.0.1",
            args.target_port,
            args.after_payload.encode("ascii"),
        )
    finally:
        control.close()
        udp.close()
    print(args.marker)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--socks-port", type=int, required=True)
    parser.add_argument(
        "--mode",
        choices=("control-close", "restart", "session-reconnect", "idle"),
        required=True,
    )
    parser.add_argument("--target-port", type=int, required=True)
    parser.add_argument("--pending-port", type=int)
    parser.add_argument("--ready-file")
    parser.add_argument("--continue-file")
    parser.add_argument("--pause-seconds", type=float, default=2.1)
    parser.add_argument("--before-payload", default="m5-g4-idle-before-0021")
    parser.add_argument("--after-payload", default="m5-g4-idle-after-0022")
    parser.add_argument("--marker", default="M5_G4_IDLE_RECONNECT_OK")
    args = parser.parse_args()
    if args.mode == "control-close":
        assert args.pending_port
        run_control_close(args)
    elif args.mode == "restart":
        assert args.ready_file and args.continue_file
        run_restart(args)
    elif args.mode == "session-reconnect":
        run_session_reconnect(args)
    else:
        run_idle(args)


if __name__ == "__main__":
    main()
