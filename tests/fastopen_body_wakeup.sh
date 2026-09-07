#!/usr/bin/env bash
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
repo_dir="$(CDPATH= cd -- "$script_dir/.." && pwd)"
build_dir="${NAIVE_BUILD_DIR:-$repo_dir/src/out/Release}"
test_dir="$(mktemp -d "${TMPDIR:-/tmp}/naive-fastopen-body.XXXXXX")"
server_pid=""
cleanup() {
  if [ -n "$server_pid" ]; then
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
  fi
  rm -rf "$test_dir"
}
trap cleanup EXIT INT TERM

openssl req -x509 -newkey rsa:2048 -keyout "$test_dir/key.pem" \
  -out "$test_dir/cert.pem" -sha256 -days 2 -nodes -subj '/CN=localhost' \
  -addext 'subjectAltName=DNS:localhost,IP:127.0.0.1' >/dev/null 2>&1
openssl pkcs8 -topk8 -nocrypt -in "$test_dir/key.pem" -outform DER \
  -out "$test_dir/key.pk8"
env CCACHE_DIR="$repo_dir/src/.host_tool_cache" ninja -C "$build_dir" \
  naive_fastopen_fail_runner naive_masque_server

"$build_dir/naive_masque_server" --port=19703 --masque_mode=open \
  --certificate_file="$test_dir/cert.pem" --key_file="$test_dir/key.pk8" \
  --fail_connects --connect_response_status=200 \
  --connect_response_body=body-wakeup >"$test_dir/server.log" 2>&1 &
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
timeout 30 "$build_dir/naive_fastopen_fail_runner" --standard-connect \
  --expect-success --body-probe \
  127.0.0.1 19703 10.255.255.1 19701 >"$test_dir/runner.log" 2>&1 || {
  cat "$test_dir/runner.log"
  cat "$test_dir/server.log"
  exit 1
}
grep -q '^FASTOPEN_BODY_WAKEUP_OK bytes=' "$test_dir/runner.log"
echo FASTOPEN_BODY_WAKEUP_OK
