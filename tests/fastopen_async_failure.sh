#!/usr/bin/env bash

set -euo pipefail

# F1 regression: a Fast Open CONNECT response that fails asynchronously
# (non-200, stream not FIN'd) must complete any pending application read with
# an error instead of leaving it pending indefinitely.
#
# Setup (all local, controlled):
#   - a non-loopback target host for the URL shape: Chromium's implicit
#     proxy bypass rules send loopback (127.0.0.0/8) URLs DIRECT, so the
#     target must be non-loopback to exercise the quic:// proxy. Both
#     exchanges fail at the CONNECT layer before any origin dial.
#   - controlled MASQUE server: --fail_connects makes every CONNECT fail
#     asynchronously: 502 delivered after a 500 ms delay, without FIN, so
#     the stream stays open and any client I/O pending across the response
#     must be completed by the client, not by a stream close.
#   - runner with the real production NaiveProxyDelegate over a quic://
#     proxy chain (MockCertVerifier, deterministic test context)
#
# Exchange 1: padding not negotiated yet, so the client does not enable Fast
#   Open. Connect() blocks until the delayed 502 arrives and then fails with
#   the tunnel error; the delegate parses the CONNECT response and learns the
#   (absent) padding support.
#
# Exchange 2: padding state known, so the client enables Fast Open: Connect()
#   returns OK before the response arrives and the application I/O (early
#   data write and/or data read) is pending when the delayed 502 (no FIN)
#   arrives. The pending I/O must complete with an error within the watchdog
#   instead of hanging. Without the fix it hangs until the watchdog fires.

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
repo_dir="$(CDPATH= cd -- "$script_dir/.." && pwd)"
src_dir="$repo_dir/src"
runner="$src_dir/out/Release/naive_fastopen_fail_runner"
masque_server="$src_dir/out/Release/naive_masque_server"
# Non-loopback target: loopback URLs are implicitly bypassed (DIRECT).
target_host="10.255.255.1"
target_port="${FASTOPEN_TARGET_PORT:-19701}"
server_port="${FASTOPEN_MASQUE_PORT:-19702}"
test_dir="$(mktemp -d "${TMPDIR:-/tmp}/naive-fastopen-fail.XXXXXX")"
server_pid=""

cleanup() {
  if [ -n "$server_pid" ]; then
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
  fi
  rm -rf "$test_dir"
}
trap cleanup EXIT INT TERM

# Self-signed certificate for the controlled endpoint (test-only context).
openssl req -x509 -newkey rsa:2048 \
  -keyout "$test_dir/key.pem" \
  -out "$test_dir/cert.pem" \
  -sha256 -days 2 -nodes -subj '/CN=localhost' \
  -addext 'subjectAltName=DNS:localhost,IP:127.0.0.1,IP:::1' \
  >/dev/null 2>&1
openssl pkcs8 -topk8 -nocrypt \
  -in "$test_dir/key.pem" -outform DER -out "$test_dir/key.pk8"

env CCACHE_DIR="$src_dir/.host_tool_cache" \
  ninja -C "$src_dir/out/Release" \
  naive_fastopen_fail_runner naive_masque_server

"$masque_server" --port="$server_port" --masque_mode=open \
  --certificate_file="$test_dir/cert.pem" \
  --key_file="$test_dir/key.pk8" \
  --fail_connects \
  >"$test_dir/server.log" 2>&1 &
server_pid=$!

for _ in $(seq 1 50); do
  if grep -q '^READY ' "$test_dir/server.log" 2>/dev/null; then
    break
  fi
  if ! kill -0 "$server_pid" 2>/dev/null; then
    cat "$test_dir/server.log"
    exit 1
  fi
  sleep 0.1
done
grep '^READY ' "$test_dir/server.log"

set +e
# The controlled MASQUE server binds [::]; use the IPv6 loopback for the
# proxy. The target host is only used for URL shape (the tunnel fails at the
# proxy layer before any origin dial) and must be non-loopback so the request
# goes through the quic:// proxy instead of the implicit DIRECT bypass.
# Use an IPv4 loopback proxy: the vendored SchemeHostPort
# (CHECK_CANONICALIZATION) rejects unbracketed IPv6 literals (e.g. "::1")
# and silently constructs an empty destination, which the host resolver
# then rejects with ERR_NAME_NOT_RESOLVED. The MASQUE server binds
# dual-stack, so IPv4 loopback is accepted.
timeout 90 "$runner" 127.0.0.1 "$server_port" "$target_host" "$target_port" \
  >"$test_dir/runner.log" 2>&1
runner_rc=$?
set -e

if [ "$runner_rc" -ne 0 ]; then
  echo "runner failed (rc=$runner_rc); logs:"
  cat "$test_dir/runner.log"
  echo "--- server log:"
  cat "$test_dir/server.log"
  exit 1
fi

# Exchange 1: 502 (with FIN) must fail the tunnel (the runner asserts the
# negative error and padding learning), and the delegate must have learned
# the padding negotiation state.
grep -q '^EXCHANGE_START n=1' "$test_dir/runner.log"
grep -q '^EXCHANGE_COMPLETE n=1 error=-[0-9]*' "$test_dir/runner.log"
grep -q '^PADDING_LEARNED' "$test_dir/runner.log"

# Exchange 2: the Fast Open async failure (delayed 502, stream not FIN'd)
# must have completed the pending application I/O instead of hanging. The
# exact error depends on which I/O was pending when the failure landed
# (tunnel read vs. stream-close write completion), so assert a negative
# error and the handled marker rather than a fixed code.
grep -q '^EXCHANGE_START n=2' "$test_dir/runner.log"
grep -q '^EXCHANGE_COMPLETE n=2 error=-[0-9]*' "$test_dir/runner.log"
grep -q '^FASTOPEN_ASYNC_FAILURE_HANDLED error=-[0-9]*' "$test_dir/runner.log"

# Server side: both exchanges failed at the controlled CONNECT layer (two
# 502 actions, one per exchange).
[ "$(grep -c '^CONNECT_ACTION fail_502' "$test_dir/server.log")" -eq 2 ]

echo 'FASTOPEN_ASYNC_FAILURE_OK'
