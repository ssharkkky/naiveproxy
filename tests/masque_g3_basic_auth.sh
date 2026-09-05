#!/usr/bin/env bash

set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
src_dir="$script_dir/../src"
server_port="${MASQUE_SERVER_PORT:-19663}"
echo_port="${MASQUE_ECHO_PORT:-19003}"
authority="localhost:${server_port}"
test_dir="$(mktemp -d "${TMPDIR:-/tmp}/naive-masque-g3.XXXXXX")"
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
  --host=127.0.0.1 --port="$echo_port" >"$test_dir/echo.log" 2>&1 &
echo_pid="$!"

"${NAIVE_BUILD_DIR:-$src_dir/out/Release}/naive_masque_server" \
  --port="$server_port" \
  --server_authority="$authority" \
  --masque_mode=open \
  --basic_user=naive-user \
  --basic_pass=naive-pass \
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

if "${NAIVE_BUILD_DIR:-$src_dir/out/Release}/naive_connect_udp_runner" \
  localhost "$server_port" 127.0.0.1 "$echo_port" g3-missing-auth \
  >"$test_dir/missing.log" 2>&1; then
  echo 'missing credentials unexpectedly succeeded'
  exit 1
fi
grep '^CONNECT_UDP_FAILED ' "$test_dir/missing.log"

if "${NAIVE_BUILD_DIR:-$src_dir/out/Release}/naive_connect_udp_runner" \
  --proxy-user=naive-user --proxy-pass=wrong-pass \
  localhost "$server_port" 127.0.0.1 "$echo_port" g3-wrong-auth \
  >"$test_dir/wrong.log" 2>&1; then
  echo 'wrong credentials unexpectedly succeeded'
  exit 1
fi
grep '^CONNECT_UDP_FAILED ' "$test_dir/wrong.log"

"${NAIVE_BUILD_DIR:-$src_dir/out/Release}/naive_connect_udp_runner" \
  --proxy-user=naive-user --proxy-pass=naive-pass \
  localhost "$server_port" 127.0.0.1 "$echo_port" g3-authenticated-echo

test "$(grep -c '^AUTH_DECISION rejected$' "$test_dir/server.log")" -eq 2
test "$(grep -c '^AUTH_DECISION accepted$' "$test_dir/server.log")" -eq 1
test "$(grep -c 'proxy_authorization=absent$' "$test_dir/server.log")" -eq 1
test "$(grep -c 'proxy_authorization=present$' "$test_dir/server.log")" -eq 2
grep '^AUTH_DECISION ' "$test_dir/server.log"
grep '^RX ' "$test_dir/echo.log"

echo 'G3_BASIC_AUTH_OK'
