#!/usr/bin/env python3
"""Bounded TCP/TLS connection churn through a SOCKS5 listener; aggregate output."""

import argparse
import concurrent.futures
import json
import socket
import ssl
import statistics
import struct
import time
from collections import Counter


def receive(sock, length, deadline):
    result = bytearray()
    while len(result) < length:
        sock.settimeout(max(0.001, deadline - time.monotonic()))
        data = sock.recv(length - len(result))
        if not data:
            raise ConnectionError("closed")
        result.extend(data)
    return bytes(result)


def probe(args, index, tls_context):
    started = time.monotonic()
    deadline = started + args.timeout
    phase = "socks"
    sock = None
    try:
        host = args.targets[index % len(args.targets)]
        sock = socket.create_connection((args.socks_host, args.socks_port), args.timeout)
        sock.sendall(b"\x05\x01\x00")
        if receive(sock, 2, deadline) != b"\x05\x00":
            raise ValueError("SOCKS authentication")
        encoded = host.encode("idna")
        sock.sendall(b"\x05\x01\x00\x03" + bytes([len(encoded)]) + encoded + struct.pack("!H", 443))
        version, reply, reserved, atyp = receive(sock, 4, deadline)
        if version != 5 or reply or reserved:
            return {"ok": False, "phase": phase, "error": "socks_reply_" + str(reply), "elapsed": time.monotonic() - started}
        length = {1: 4, 4: 16}.get(atyp)
        if atyp == 3:
            length = receive(sock, 1, deadline)[0]
        if length is None:
            raise ValueError("SOCKS address")
        receive(sock, length + 2, deadline)
        socks_elapsed = time.monotonic() - started
        phase = "tls"
        sock.settimeout(max(0.001, deadline - time.monotonic()))
        sock = tls_context.wrap_socket(sock, server_hostname=host)
        tls_elapsed = time.monotonic() - started
        phase = "http"
        sock.settimeout(max(0.001, deadline - time.monotonic()))
        sock.sendall(("HEAD / HTTP/1.1\r\nHost: " + host + "\r\nConnection: close\r\n\r\n").encode("ascii"))
        response = bytearray()
        while b"\r\n" not in response and len(response) < 8192:
            response.extend(receive(sock, 1, deadline))
        status = int(bytes(response).split(b" ", 2)[1])
        return {"ok": True, "status": status, "socks": socks_elapsed, "tls": tls_elapsed, "elapsed": time.monotonic() - started}
    except (OSError, ValueError, ConnectionError) as error:
        return {"ok": False, "phase": phase, "error": type(error).__name__, "elapsed": time.monotonic() - started}
    finally:
        if sock is not None:
            sock.close()


def distribution(values):
    if not values:
        return None
    values = sorted(values)
    return {"median_ms": round(statistics.median(values) * 1000, 1), "p95_ms": round(values[min(len(values) - 1, int(len(values) * 0.95))] * 1000, 1), "max_ms": round(max(values) * 1000, 1)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--socks-host", default="127.0.0.1")
    parser.add_argument("--socks-port", type=int, default=1080)
    parser.add_argument("--samples", type=int, default=48)
    parser.add_argument("--concurrency", type=int, default=8)
    parser.add_argument("--timeout", type=float, default=10)
    parser.add_argument("--targets", nargs="+", default=["api.github.com", "www.cloudflare.com", "www.reddit.com", "api.openai.com"])
    args = parser.parse_args()
    if not 1 <= args.samples <= 1000 or not 1 <= args.concurrency <= 32 or not 0 < args.timeout <= 60:
        parser.error("invalid probe bounds")
    tls_context = ssl.create_default_context()
    started = time.monotonic()
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.concurrency) as executor:
        results = list(executor.map(lambda index: probe(args, index, tls_context), range(args.samples)))
    good = [result for result in results if result["ok"]]
    print(json.dumps({
        "samples": args.samples, "concurrency": args.concurrency,
        "ok": len(good), "failed": len(results) - len(good),
        "statuses": dict(Counter(result["status"] for result in good)),
        "errors": dict(Counter(result["phase"] + ":" + result["error"] for result in results if not result["ok"])),
        "socks": distribution([result["socks"] for result in good]),
        "tls": distribution([result["tls"] for result in good]),
        "total": distribution([result["elapsed"] for result in results]),
        "wall_seconds": round(time.monotonic() - started, 2),
    }, sort_keys=True), flush=True)
    return 0 if len(good) == len(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
