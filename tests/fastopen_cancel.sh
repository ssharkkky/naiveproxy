#!/usr/bin/env bash
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
repo_dir="$(CDPATH= cd -- "$script_dir/.." && pwd)"
build_dir="${NAIVE_BUILD_DIR:-$repo_dir/src/out/Release}"
test_dir="$(mktemp -d "${TMPDIR:-/tmp}/naive-fastopen-cancel.XXXXXX")"
server_pid=""
cleanup() {
  if [ -n "$server_pid" ]; then
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
  fi
  rm -rf "$test_dir"
}
trap cleanup EXIT INT TERM

openssl req -x509 -newkey rsa:2048 -nodes -days 2 -subj /CN=localhost \
  -addext 'subjectAltName=DNS:localhost,IP:127.0.0.1' \
  -keyout "$test_dir/key.pem" -out "$test_dir/cert.pem" >/dev/null 2>&1
openssl pkcs8 -topk8 -nocrypt -in "$test_dir/key.pem" -outform DER \
  -out "$test_dir/key.pk8"
env CCACHE_DIR="$repo_dir/src/.host_tool_cache" ninja -C "$build_dir" \
  naive_fastopen_fail_runner naive_masque_server

"$build_dir/naive_masque_server" --port=19704 --masque_mode=open \
  --certificate_file="$test_dir/cert.pem" --key_file="$test_dir/key.pk8" \
  --fail_connects --connect_response_status=200 \
  >"$test_dir/server.log" 2>&1 &
server_pid=$!
for _ in $(seq 1 100); do
  if grep -q '^READY ' "$test_dir/server.log"; then break; fi
  if ! kill -0 "$server_pid" 2>/dev/null; then
    cat "$test_dir/server.log"
    exit 1
  fi
  sleep 0.05
done
grep -q '^READY ' "$test_dir/server.log"

timeout 40 "$build_dir/naive_fastopen_fail_runner" --cancel-probe \
  127.0.0.1 19704 10.255.255.1 19701 >"$test_dir/runner.log" 2>&1 || {
  cat "$test_dir/runner.log"
  cat "$test_dir/server.log"
  exit 1
}

grep -q '^FASTOPEN_CANCEL_ACTIVE_OWNER_OK callbacks=0$' "$test_dir/runner.log"
grep -q '^FASTOPEN_CANCEL_DESTROYED_OWNER_OK callbacks=0$' "$test_dir/runner.log"
grep -q '^FASTOPEN_CANCEL_OK$' "$test_dir/runner.log"
[ "$(grep -c '^CONNECT_ACTION fail_200$' "$test_dir/server.log")" -eq 3 ]
echo FASTOPEN_CANCEL_OK
