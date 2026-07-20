#!/usr/bin/env python3
"""Application UDP/DNS probe for a deterministic impairment profile."""

import argparse
import pathlib
import socket
import struct
import sys
import time


REPO_TESTS = str(pathlib.Path(__file__).resolve().parents[1])
sys.path.insert(0, REPO_TESTS)

from socks5_udp_m2 import udp_associate, udp_packet  # noqa: E402
from socks5_udp_m3 import dns_query, extract_payload  # noqa: E402


def set_profile(path, profile):
    temporary = path.with_suffix(".new")
    temporary.write_text(f"{profile}\n", encoding="ascii")
    temporary.replace(path)
    time.sleep(0.1)


def collect(sock, expected, timeout):
    received = set()
    duplicates = 0
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline and len(received) < len(expected):
        sock.settimeout(max(0.01, deadline - time.monotonic()))
        try:
            packet, _ = sock.recvfrom(65535)
        except (TimeoutError, socket.timeout):
            break
        if packet not in expected:
            raise AssertionError(f"corrupt or cross-target packet bytes={len(packet)}")
        if packet in received:
            duplicates += 1
        received.add(packet)
    return received, duplicates


def dns_roundtrip(sock, relay, dns_port):
    for attempt in range(8):
        query = bytearray(dns_query())
        struct.pack_into("!H", query, 0, 0x6000 + attempt)
        packet = udp_packet("127.0.0.1", dns_port, bytes(query))
        sock.sendto(packet, relay)
        sock.settimeout(0.75)
        try:
            response, _ = sock.recvfrom(65535)
        except (TimeoutError, socket.timeout):
            continue
        payload = extract_payload(response)
        if payload[:2] != query[:2]:
            continue
        assert payload[2] & 0x80
        return attempt + 1
    raise AssertionError("DNS did not recover within the retry budget")


def verify_control_close(args):
    control, udp, relay = udp_associate("127.0.0.1", args.socks_port, socket.AF_INET)
    try:
        warmed = False
        for attempt in range(8):
            packet = udp_packet(
                "127.0.0.1", args.echo_port, b"m6-close-warmup" + bytes([attempt])
            )
            udp.sendto(packet, relay)
            udp.settimeout(0.75)
            try:
                response, _ = udp.recvfrom(65535)
            except (TimeoutError, socket.timeout):
                continue
            if response == packet:
                warmed = True
                break
        assert warmed, "control-close association did not become healthy"
        control.close()
        time.sleep(0.2)
        packet = udp_packet("127.0.0.1", args.echo_port, b"m6-after-control-close")
        udp.sendto(packet, relay)
        udp.settimeout(0.5)
        try:
            response = udp.recvfrom(65535)
        except (TimeoutError, socket.timeout, ConnectionRefusedError):
            return
        raise AssertionError(f"closed control still relayed bytes={len(response[0])}")
    finally:
        control.close()
        udp.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--socks-port", type=int, required=True)
    parser.add_argument("--echo-port", type=int, required=True)
    parser.add_argument("--dns-port", type=int, required=True)
    parser.add_argument("--profile", required=True)
    parser.add_argument("--profile-control-file", type=pathlib.Path, required=True)
    args = parser.parse_args()

    control, udp, relay = udp_associate("127.0.0.1", args.socks_port, socket.AF_INET)
    try:
        set_profile(args.profile_control_file, args.profile)
        warmed = False
        for attempt in range(8):
            warmup = udp_packet(
                "127.0.0.1", args.echo_port, b"m6-warmup-" + bytes([attempt])
            )
            udp.sendto(warmup, relay)
            udp.settimeout(0.75)
            try:
                response, _ = udp.recvfrom(65535)
            except (TimeoutError, socket.timeout):
                continue
            if response == warmup:
                warmed = True
                break
        assert warmed, "target tunnel did not open within the warmup retry budget"

        packets = {
            udp_packet(
                "127.0.0.1",
                args.echo_port,
                b"m6" + bytes([sequence]) + bytes([sequence ^ 0xFF]) * 61,
            )
            for sequence in range(20)
        }
        for packet in packets:
            udp.sendto(packet, relay)
            time.sleep(0.01)
        received, duplicates = collect(udp, packets, 4.0)
        # Reordering outer QUIC packets may still lose unreliable DATAGRAM
        # frames even though every outer packet is eventually forwarded.
        minimum = (
            12
            if args.profile in {"loss", "reorder", "combined"}
            else len(packets)
        )
        assert len(received) >= minimum, (args.profile, len(received), minimum)
        assert duplicates == 0, (args.profile, duplicates)
        dns_attempts = dns_roundtrip(udp, relay, args.dns_port)
        verify_control_close(args)
        print(
            f"M6_G2_PROFILE_COUNTS profile={args.profile} "
            f"sent={len(packets)} received={len(received)} duplicates={duplicates} "
            f"dns_attempts={dns_attempts}"
        )

        set_profile(args.profile_control_file, "none")
        recovery = udp_packet(
            "127.0.0.1", args.echo_port, b"m6-impairment-recovery"
        )
        udp.sendto(recovery, relay)
        udp.settimeout(2)
        while True:
            response, _ = udp.recvfrom(65535)
            if response == recovery:
                break
            if response in packets:
                continue
            raise AssertionError(f"unexpected recovery packet bytes={len(response)}")
        print(f"M6_G2_PROFILE_RECOVERY_OK profile={args.profile}")
        print(f"M6_G2_UDP_DNS_OK profile={args.profile}")
        print(f"M6_G2_CONTROL_CLOSE_OK profile={args.profile}")
        print(f"M6_G2_NO_REPLAY_OK profile={args.profile}")
    finally:
        control.close()
        udp.close()


if __name__ == "__main__":
    main()
