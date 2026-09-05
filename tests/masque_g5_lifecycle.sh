#!/usr/bin/env bash

set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
src_dir="$script_dir/../src"
server_port="${MASQUE_SERVER_PORT:-19664}"
echo_port="${MASQUE_ECHO_PORT:-19004}"
authority="localhost:${server_port}"
test_dir="$(mktemp -d "${TMPDIR:-/tmp}/naive-masque-g5.XXXXXX")"
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

start_server() {
  local log_file="$1"
  shift
  "${NAIVE_BUILD_DIR:-$src_dir/out/Release}/naive_masque_server" \
    --port="$server_port" \
    --server_authority="$authority" \
    --masque_mode=open \
    --certificate_file="$test_dir/cert.pem" \
    --key_file="$test_dir/key.pk8" "$@" >"$log_file" 2>&1 &
  server_pid="$!"

  for _ in {1..50}; do
    if grep -q '^READY ' "$log_file" &&
       grep -q '^READY ' "$test_dir/echo.log"; then
      return
    fi
    if ! kill -0 "$server_pid" 2>/dev/null ||
       ! kill -0 "$echo_pid" 2>/dev/null; then
      cat "$log_file" "$test_dir/echo.log"
      exit 1
    fi
    sleep 0.1
  done
  cat "$log_file" "$test_dir/echo.log"
  exit 1
}

start_server "$test_dir/normal-server.log"
grep '^READY ' "$test_dir/normal-server.log"
grep '^READY ' "$test_dir/echo.log"

"${NAIVE_BUILD_DIR:-$src_dir/out/Release}/naive_connect_udp_runner" \
  --destroy-with-pending-read \
  --log-net-log="$test_dir/pending-read-netlog.json" \
  localhost "$server_port" 127.0.0.1 "$echo_port" g5-pending-read

jq -e '
  (.constants.logSourceType.QUIC_PROXY_DATAGRAM_CLIENT_SOCKET) as $type |
  any(.events[]; .source.type == $type)
' "$test_dir/pending-read-netlog.json" >/dev/null
jq -e '
  any(.events[]; ((.params // {}) | tostring | contains("connect-udp")))
' "$test_dir/pending-read-netlog.json" >/dev/null

"${NAIVE_BUILD_DIR:-$src_dir/out/Release}/naive_connect_udp_runner" \
  --shutdown-session-with-pending-read \
  localhost "$server_port" 127.0.0.1 "$echo_port" g5-session-shutdown

kill "$server_pid"
wait "$server_pid" 2>/dev/null || true
server_pid=""

start_server "$test_dir/ignored-server.log" --ignore_connect_requests=true
grep '^READY ' "$test_dir/ignored-server.log"

"${NAIVE_BUILD_DIR:-$src_dir/out/Release}/naive_connect_udp_runner" \
  --destroy-while-connect-pending \
  localhost "$server_port" 127.0.0.1 "$echo_port" g5-connect-pending

grep '^CONNECT_ACTION ignored$' "$test_dir/ignored-server.log"

echo 'G5_LIFECYCLE_OK'
