#!/usr/bin/env python3
"""Black-box live payload-ceiling probe through a SOCKS5 UDP relay."""

import argparse
import socket
import sys
import time


REPO_TESTS = str(__file__).rsplit("/m6/", 1)[0]
sys.path.insert(0, REPO_TESTS)

from socks5_udp_m2 import udp_associate, udp_packet  # noqa: E402


def make_payload(size, salt):
    return bytes((index + salt * 17 + size) % 251 for index in range(size))


def drain(sock):
    sock.setblocking(False)
    try:
        while True:
            sock.recvfrom(65535)
    except BlockingIOError:
        pass
    finally:
        sock.setblocking(True)


def roundtrip(sock, relay, host, port, size, salt, force_domain, timeout):
    drain(sock)
    packet = udp_packet(
        host, port, make_payload(size, salt), force_domain=force_domain
    )
    sock.sendto(packet, relay)
    deadline = time.monotonic() + timeout
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return False
        sock.settimeout(remaining)
        try:
            response, _ = sock.recvfrom(65535)
        except (TimeoutError, socket.timeout):
            return False
        if response == packet:
            return True


def measure_target(args, label, host, force_domain):
    control, udp, relay = udp_associate("127.0.0.1", args.socks_port, socket.AF_INET)
    try:
        assert roundtrip(
            udp,
            relay,
            host,
            args.echo_port,
            args.minimum,
            1,
            force_domain,
            args.timeout,
        ), f"{label}: inherited minimum did not round trip"
        assert not roundtrip(
            udp,
            relay,
            host,
            args.echo_port,
            args.maximum,
            2,
            force_domain,
            args.timeout,
        ), f"{label}: maximum did not bracket the ceiling"

        low = args.minimum
        high = args.maximum
        salt = 3
        while high - low > 1:
            candidate = low + (high - low) // 2
            if roundtrip(
                udp,
                relay,
                host,
                args.echo_port,
                candidate,
                salt,
                force_domain,
                args.timeout,
            ):
                low = candidate
            else:
                high = candidate
            salt += 1

        for repeat in range(args.repetitions):
            assert roundtrip(
                udp,
                relay,
                host,
                args.echo_port,
                low,
                salt + repeat,
                force_domain,
                args.timeout,
            ), f"{label}: exact ceiling failed repeat {repeat + 1}"
        for repeat in range(args.repetitions):
            assert not roundtrip(
                udp,
                relay,
                host,
                args.echo_port,
                high,
                salt + args.repetitions + repeat,
                force_domain,
                args.timeout,
            ), f"{label}: ceiling-plus-one unexpectedly passed repeat {repeat + 1}"

        recovery_size = min(64, low)
        assert roundtrip(
            udp,
            relay,
            host,
            args.echo_port,
            recovery_size,
            salt + 2 * args.repetitions,
            force_domain,
            args.timeout,
        ), f"{label}: healthy traffic did not recover after oversize drops"
        print(f"M6_G1_MEASURED_CEILING label={label} bytes={low}")
        return low
    finally:
        control.close()
        udp.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--socks-port", type=int, required=True)
    parser.add_argument("--echo-port", type=int, required=True)
    parser.add_argument("--minimum", type=int, default=1200)
    parser.add_argument("--maximum", type=int, default=4096)
    parser.add_argument("--timeout", type=float, default=0.35)
    parser.add_argument("--repetitions", type=int, default=3)
    args = parser.parse_args()
    assert 0 <= args.minimum < args.maximum <= 65507
    assert args.repetitions > 0

    results = {
        "ipv4": measure_target(args, "ipv4", "127.0.0.1", False),
        "ipv6": measure_target(args, "ipv6", "::1", False),
        "domain": measure_target(args, "domain", "localhost", True),
    }
    assert len(set(results.values())) == 1, results
    print(f"M6_G1_LIVE_PRODUCT_CEILING_OK bytes={results['ipv4']}")


if __name__ == "__main__":
    main()
