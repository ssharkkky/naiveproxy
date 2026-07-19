#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
backend_test="$repo_dir/src/out/Release/naive_connect_udp_backend_test"
runner="$repo_dir/src/out/Release/naive_socks5_udp_runner"
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/naive-m3.XXXXXX")
runner_pid=""

cleanup() {
  if [ -n "$runner_pid" ]; then
    kill "$runner_pid" 2>/dev/null || true
    wait "$runner_pid" 2>/dev/null || true
  fi
  rm -r "$tmp_dir"
}
trap cleanup EXIT INT TERM

ninja -C "$repo_dir/src/out/Release" naive_connect_udp_backend_test \
  naive_socks5_udp_runner
"$backend_test"

cap_log="$tmp_dir/association-cap.log"
"$runner" --listen-host=127.0.0.1 --proxy-scheme=quic \
  --max-udp-associations=2 >"$cap_log" 2>&1 &
runner_pid=$!
ready=""
i=0
while [ "$i" -lt 100 ]; do
  ready=$(grep 'M2_SOCKS5_UDP_READY' "$cap_log" 2>/dev/null || true)
  [ -n "$ready" ] && break
  kill -0 "$runner_pid" 2>/dev/null || {
    cat "$cap_log"
    exit 1
  }
  sleep 0.05
  i=$((i + 1))
done
[ -n "$ready" ] || {
  cat "$cap_log"
  exit 1
}
port=$(printf '%s\n' "$ready" | sed -n 's/.* port=\([0-9][0-9]*\).*/\1/p')
python3 "$script_dir/socks5_udp_m2.py" --host 127.0.0.1 --port "$port" \
  --mode association-cap
kill "$runner_pid" 2>/dev/null || true
wait "$runner_pid" 2>/dev/null || true
runner_pid=""

# G3-G5 append their full-path cases here. Keeping this entry point alive from
# G0 makes every subsequent gate extend the same M3 verification surface.
echo M3_G0_TEST_SKELETON_OK
