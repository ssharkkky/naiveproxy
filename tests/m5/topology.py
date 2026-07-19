#!/usr/bin/env python3

"""Allocates the dynamic loopback topology used by the M5 harness."""

import argparse
import json
import socket


FIXED_M4_PORT = 19443


def reserve_shared_ipv4_port() -> int:
    """Find a currently free port for Caddy's shared TCP/UDP listener."""
    for _ in range(100):
        tcp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            tcp.bind(("127.0.0.1", 0))
            port = tcp.getsockname()[1]
            udp.bind(("127.0.0.1", port))
            return port
        except OSError:
            continue
        finally:
            tcp.close()
            udp.close()
    raise RuntimeError("could not reserve a shared loopback TCP/UDP port")


def reserve_udp_port(family: socket.AddressFamily, host: str) -> int:
    udp = socket.socket(family, socket.SOCK_DGRAM)
    try:
        udp.bind((host, 0))
        return udp.getsockname()[1]
    finally:
        udp.close()


def allocate_topology() -> dict[str, int]:
    values = {
        "proxy": reserve_shared_ipv4_port(),
        "echo_ipv4": reserve_udp_port(socket.AF_INET, "127.0.0.1"),
        "echo_ipv6": reserve_udp_port(socket.AF_INET6, "::1"),
        "dns": reserve_udp_port(socket.AF_INET, "127.0.0.1"),
        "http3": reserve_udp_port(socket.AF_INET, "127.0.0.1"),
    }
    ports = list(values.values())
    if len(set(ports)) != len(ports):
        raise RuntimeError(f"topology contains duplicate ports: {values}")
    if FIXED_M4_PORT in ports:
        raise RuntimeError(f"topology reused forbidden fixed port: {values}")
    return values


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--contract", action="store_true")
    args = parser.parse_args()
    topology = allocate_topology()
    print(json.dumps(topology, sort_keys=True))
    if args.contract:
        print("M5_G0_DYNAMIC_TOPOLOGY_OK")


if __name__ == "__main__":
    main()
