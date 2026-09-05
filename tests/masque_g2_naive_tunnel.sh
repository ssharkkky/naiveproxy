#!/usr/bin/env bash

set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
src_dir="$script_dir/../src"
server_port="${MASQUE_SERVER_PORT:-19662}"
echo_port="${MASQUE_ECHO_PORT:-19002}"
authority="[::1]:${server_port}"
test_dir="$(mktemp -d "${TMPDIR:-/tmp}/naive-masque-g2.XXXXXX")"
server_pid=""
echo_pid=""

cleanup() {
  if [[ -n "$server_pid" ]]; then
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
  fi
  if [[ -n "$echo_pid" ]]; then
    kill "$echo_pid" 2>/dev/null || true
    wait "$echo_pid" 2>/dev/null || true
  fi
  rm -rf "$test_dir"
}
trap cleanup EXIT INT TERM

openssl req -x509 -newkey rsa:2048 \
  -keyout "$test_dir/key.pem" \
  -out "$test_dir/cert.pem" \
  -sha256 -days 2 -nodes -subj '/CN=localhost' \
  -addext 'subjectAltName=DNS:localhost,IP:127.0.0.1,IP:::1' \
  >/dev/null 2>&1
openssl pkcs8 -topk8 -nocrypt \
  -in "$test_dir/key.pem" -outform DER -out "$test_dir/key.pk8"

env CCACHE_DIR="$src_dir/.host_tool_cache" \
  ninja -C "${NAIVE_BUILD_DIR:-$src_dir/out/Release}" \
  naive_masque_server naive_connect_udp_runner

python3 -u "$script_dir/masque_udp_echo.py" \
  --host=::1 --port="$echo_port" >"$test_dir/echo.log" 2>&1 &
echo_pid="$!"

"${NAIVE_BUILD_DIR:-$src_dir/out/Release}/naive_masque_server" \
  --port="$server_port" \
  --server_authority="$authority" \
  --masque_mode=open \
  --certificate_file="$test_dir/cert.pem" \
  --key_file="$test_dir/key.pk8" >"$test_dir/server.log" 2>&1 &
server_pid="$!"

for _ in {1..50}; do
  if grep -q '^READY ' "$test_dir/server.log" &&
     grep -q '^READY ' "$test_dir/echo.log"; then
    break
  fi
  if ! kill -0 "$server_pid" 2>/dev/null ||
     ! kill -0 "$echo_pid" 2>/dev/null; then
    cat "$test_dir/server.log" "$test_dir/echo.log"
    exit 1
  fi
  sleep 0.1
done

grep '^READY ' "$test_dir/server.log"
grep '^READY ' "$test_dir/echo.log"

if ! "${NAIVE_BUILD_DIR:-$src_dir/out/Release}/naive_connect_udp_runner" \
  ::1 "$server_port" ::1 "$echo_port" g2-naive-tunnel-echo \
  >"$test_dir/runner.log" 2>&1; then
  cat "$test_dir/runner.log" "$test_dir/server.log" "$test_dir/echo.log"
  exit 1
fi
cat "$test_dir/runner.log"
grep -q '^CONNECT_UDP_TCP_PADDING_CACHE_ISOLATION_OK$' \
  "$test_dir/runner.log"

grep '^CONNECT_HEADERS ' "$test_dir/server.log"
grep '^RX ' "$test_dir/echo.log"

echo 'G2_NAIVE_TUNNEL_OK'
