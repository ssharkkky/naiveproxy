#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "$0")/.." && pwd)
build_dir=${NAIVE_BUILD_DIR:-$repo_dir/src/out/Release}
go_bin=${GO_BIN:-go}
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/naive-connect-response.XXXXXX")
server_pid=
cleanup() {
  if [ -n "$server_pid" ]; then
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
  fi
  rm -rf "$test_dir"
}
trap cleanup EXIT
trap 'exit 130' INT TERM

env CCACHE_DIR="$repo_dir/src/.host_tool_cache" ninja -C "$build_dir" \
  naive_fastopen_fail_runner naive_masque_server
(cd "$repo_dir/tests/m5" && "$go_bin" build -o "$test_dir/h2-response" ./cmd/connect-response)
openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=localhost \
  -addext 'subjectAltName=DNS:localhost,IP:127.0.0.1' \
  -keyout "$test_dir/key.pem" -out "$test_dir/cert.pem" >/dev/null 2>&1
openssl pkcs8 -topk8 -nocrypt -in "$test_dir/key.pem" -outform DER -out "$test_dir/key.pk8"

for protocol in h3 h2; do
  for status in 502 504 200; do
    port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()')
    args=(--standard-connect)
    if [ "$status" = 200 ]; then args+=(--expect-success); fi
    if [ "$protocol" = h3 ]; then
      "$build_dir/naive_masque_server" --port="$port" --masque_mode=open \
        --certificate_file="$test_dir/cert.pem" --key_file="$test_dir/key.pk8" \
        --fail_connects --connect_response_status="$status" >"$test_dir/server.log" 2>&1 &
    else
      args+=(--https-proxy)
      "$test_dir/h2-response" -listen "127.0.0.1:$port" \
        -cert "$test_dir/cert.pem" -key "$test_dir/key.pem" -status "$status" >"$test_dir/server.log" 2>&1 &
    fi
    server_pid=$!
    for _ in $(seq 1 100); do
      if rg -q '^READY ' "$test_dir/server.log"; then break; fi
      kill -0 "$server_pid"
      sleep 0.05
    done
    rg -q '^READY ' "$test_dir/server.log"
    if ! timeout 40 "$build_dir/naive_fastopen_fail_runner" "${args[@]}" \
        127.0.0.1 "$port" 192.0.2.1 443 >"$test_dir/runner.log" 2>&1; then
      cat "$test_dir/runner.log"
      cat "$test_dir/server.log"
      exit 1
    fi
    if [ "$protocol" = h3 ]; then
      test "$(grep -c "^CONNECT_ACTION fail_$status$" "$test_dir/server.log")" = 2
    fi
    rg '^STANDARD_CONNECT' "$test_dir/runner.log"
    echo "CONNECT_RESPONSE_${protocol}_${status}_OK"
    kill "$server_pid"
    wait "$server_pid" 2>/dev/null || true
    server_pid=
  done
done
echo CONNECT_RESPONSE_MATRIX_OK
