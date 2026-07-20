#!/usr/bin/env python3
"""Authentication, failure, malformed-input, and admission probes for M5-G3."""

import argparse
from pathlib import Path
import select
import socket
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from socks5_udp_m2 import (
    expect_no_udp,
    find_bindable_non_loopback_ipv4,
    recv_exact,
    udp_associate,
    udp_packet,
)


def associate(args, username=None, password=None):
    return udp_associate(
        "127.0.0.1",
        args.socks_port,
        socket.AF_INET,
        username=username,
        password=password,
    )


def assert_control_open(control):
    control.settimeout(0.2)
    try:
        data = control.recv(1)
    except (TimeoutError, socket.timeout):
        return
    assert data != b"", "SOCKS control connection closed unexpectedly"
    raise AssertionError(f"unexpected SOCKS control data: {data.hex()}")


def roundtrip(udp, relay, target_host, target_port, payload, force_domain=False):
    packet = udp_packet(
        target_host, target_port, payload, force_domain=force_domain
    )
    udp.settimeout(5)
    udp.sendto(packet, relay)
    response, _ = udp.recvfrom(65535)
    assert response == packet, (response.hex(), packet.hex())


def run_local_auth(args):
    control, udp, relay = associate(args, args.username, args.password)
    try:
        roundtrip(
            udp,
            relay,
            "127.0.0.1",
            args.target_port,
            b"m5-g3-local-auth-ok",
        )
    finally:
        control.close()
        udp.close()

    missing = socket.create_connection(("127.0.0.1", args.socks_port), timeout=5)
    try:
        missing.sendall(b"\x05\x01\x00")
        assert recv_exact(missing, 2) == b"\x05\xff"
    finally:
        missing.close()

    wrong = socket.create_connection(("127.0.0.1", args.socks_port), timeout=5)
    try:
        wrong.sendall(b"\x05\x01\x02")
        assert recv_exact(wrong, 2) == b"\x05\x02"
        encoded_user = b"m5-local-wrong"
        encoded_pass = b"m5-local-wrong"
        wrong.sendall(
            b"\x01"
            + bytes([len(encoded_user)])
            + encoded_user
            + bytes([len(encoded_pass)])
            + encoded_pass
        )
        assert recv_exact(wrong, 2) == b"\x01\xff"
        assert wrong.recv(1) == b""
    finally:
        wrong.close()
    print("M5_G3_LOCAL_AUTH_OK")


def run_target_failure(args):
    control, udp, relay = associate(args)
    try:
        packet = udp_packet(
            args.target_host,
            args.target_port,
            b"m5-g3-target-failure",
            force_domain=args.force_domain,
        )
        udp.sendto(packet, relay)
        expect_no_udp(udp, "first target failure")
        assert_control_open(control)
        time.sleep(1.2)
        udp.sendto(packet, relay)
        expect_no_udp(udp, "second target failure")
        assert_control_open(control)
        if args.recovery_port:
            roundtrip(
                udp,
                relay,
                args.recovery_host,
                args.recovery_port,
                b"m5-g3-target-isolation-recovery",
                force_domain=args.recovery_force_domain,
            )
            assert_control_open(control)
    finally:
        control.close()
        udp.close()
    print(args.marker)


def run_malformed(args):
    control, udp, relay = associate(args)
    try:
        roundtrip(
            udp, relay, "127.0.0.1", args.target_port, b"m5-g3-before-malformed"
        )
        malformed = [
            b"\x00\x00",
            b"\x00\x00\x01\x01\x7f\x00\x00\x01\x00\x35fragment",
            b"\x00\x00\x00\x7f\x00\x35invalid-atyp",
            b"\x01\x00\x00\x01\x7f\x00\x00\x01\x00\x35bad-rsv",
        ]
        for index, packet in enumerate(malformed):
            udp.sendto(packet, relay)
            expect_no_udp(udp, f"malformed packet {index}")
            assert_control_open(control)

        spoof = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            spoof.bind(("127.0.0.1", 0))
            spoof.sendto(
                udp_packet(
                    "127.0.0.1", args.target_port, b"m5-g3-spoofed-port"
                ),
                relay,
            )
            expect_no_udp(spoof, "spoofed source port")
            expect_no_udp(udp, "spoofed packet redirected to authorized client")
        finally:
            spoof.close()

        wrong_source = find_bindable_non_loopback_ipv4()
        if wrong_source is not None:
            wrong_ip = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            try:
                wrong_ip.bind((wrong_source, 0))
                wrong_ip.sendto(
                    udp_packet(
                        "127.0.0.1",
                        args.target_port,
                        b"m5-g3-spoofed-ip",
                    ),
                    relay,
                )
                expect_no_udp(wrong_ip, "spoofed source IP")
                expect_no_udp(udp, "wrong-IP packet reached authorized client")
            finally:
                wrong_ip.close()

        for _ in range(64):
            udp.sendto(b"\x00\x00\x00", relay)
        time.sleep(0.1)
        assert_control_open(control)
        roundtrip(
            udp, relay, "127.0.0.1", args.target_port, b"m5-g3-after-malformed"
        )
    finally:
        control.close()
        udp.close()
    print("M5_G3_MALFORMED_ISOLATION_OK")


def run_admission(args):
    associations = [associate(args) for _ in range(33)]
    packets = []
    try:
        for index, (_, udp, relay) in enumerate(associations):
            packet = udp_packet(
                "127.0.0.1",
                args.target_port,
                f"m5-g3-admission-{index}".encode("ascii"),
            )
            udp.setblocking(False)
            udp.sendto(packet, relay)
            packets.append(packet)

        pending = {udp: index for index, (_, udp, _) in enumerate(associations)}
        successful = set()
        deadline = time.monotonic() + 8
        while pending and time.monotonic() < deadline:
            readable, _, _ = select.select(
                list(pending), [], [], min(0.2, deadline - time.monotonic())
            )
            for udp in readable:
                index = pending.pop(udp)
                response, _ = udp.recvfrom(65535)
                assert response == packets[index], (index, response.hex())
                successful.add(index)
            if len(successful) == 32:
                break
        assert len(successful) == 32, len(successful)

        released = min(successful)
        associations[released][0].close()
        associations[released][1].close()
        time.sleep(0.5)
        control, udp, relay = associate(args)
        try:
            roundtrip(
                udp,
                relay,
                "127.0.0.1",
                args.target_port,
                b"m5-g3-admission-reused",
            )
        finally:
            control.close()
            udp.close()
    finally:
        for control, udp, _ in associations:
            control.close()
            udp.close()
    print("M5_G3_ADMISSION_OK")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--socks-port", type=int, required=True)
    parser.add_argument(
        "--mode",
        choices=("local-auth", "target-failure", "malformed", "admission"),
        required=True,
    )
    parser.add_argument("--target-host", default="127.0.0.1")
    parser.add_argument("--target-port", type=int, required=True)
    parser.add_argument("--force-domain", action="store_true")
    parser.add_argument("--recovery-host", default="127.0.0.1")
    parser.add_argument("--recovery-port", type=int)
    parser.add_argument("--recovery-force-domain", action="store_true")
    parser.add_argument("--username")
    parser.add_argument("--password")
    parser.add_argument("--marker", default="M5_G3_TARGET_FAILURE_OK")
    args = parser.parse_args()
    if args.mode == "local-auth":
        run_local_auth(args)
    elif args.mode == "target-failure":
        run_target_failure(args)
    elif args.mode == "malformed":
        run_malformed(args)
    else:
        run_admission(args)


if __name__ == "__main__":
    main()
