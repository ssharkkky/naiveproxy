#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
runner="${NAIVE_BUILD_DIR:-$repo_dir/src/out/Release}/naive_socks5_udp_runner"
codec_test="${NAIVE_BUILD_DIR:-$repo_dir/src/out/Release}/naive_socks5_udp_test"
state_test="${NAIVE_BUILD_DIR:-$repo_dir/src/out/Release}/naive_socks5_server_socket_state_test"
association_test="${NAIVE_BUILD_DIR:-$repo_dir/src/out/Release}/naive_socks5_udp_association_test"
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/naive-m2.XXXXXX")
runner_pid=""

cleanup() {
  if [ -n "$runner_pid" ]; then
    kill "$runner_pid" 2>/dev/null || true
    wait "$runner_pid" 2>/dev/null || true
  fi
  rm -r "$tmp_dir"
}
trap cleanup EXIT INT TERM

ninja -C "${NAIVE_BUILD_DIR:-$repo_dir/src/out/Release}" naive naive_socks5_udp_test \
  naive_socks5_server_socket_state_test naive_socks5_udp_association_test \
  naive_socks5_udp_runner
"$codec_test"
"$state_test"
"$association_test"

run_case() {
  host=$1
  scheme=$2
  mode=$3
  label=$4
  username=${5-}
  password=${6-}
  log="$tmp_dir/$label.log"
  if [ -n "$username$password" ]; then
    "$runner" --listen-host="$host" --proxy-scheme="$scheme" \
      --listen-user="$username" --listen-pass="$password" >"$log" 2>&1 &
  else
    "$runner" --listen-host="$host" --proxy-scheme="$scheme" >"$log" 2>&1 &
  fi
  runner_pid=$!
  ready=""
  i=0
  while [ "$i" -lt 100 ]; do
    ready=$(grep 'M2_SOCKS5_UDP_READY' "$log" 2>/dev/null || true)
    [ -n "$ready" ] && break
    kill -0 "$runner_pid" 2>/dev/null || {
      cat "$log"
      return 1
    }
    sleep 0.05
    i=$((i + 1))
  done
  [ -n "$ready" ] || {
    cat "$log"
    return 1
  }
  port=$(printf '%s\n' "$ready" | sed -n 's/.* port=\([0-9][0-9]*\).*/\1/p')
  if [ -n "$username$password" ]; then
    python3 "$script_dir/socks5_udp_m2.py" --host "$host" --port "$port" \
      --mode "$mode" --username "$username" --password "$password"
  else
    python3 "$script_dir/socks5_udp_m2.py" --host "$host" --port "$port" \
      --mode "$mode"
  fi
  kill "$runner_pid" 2>/dev/null || true
  wait "$runner_pid" 2>/dev/null || true
  runner_pid=""
}

run_case 127.0.0.1 quic success ipv4-success
run_case ::1 quic success ipv6-success
run_case 127.0.0.1 quic success authenticated-success m2-user m2-pass
run_case 127.0.0.1 http rejection non-quic-rejection
run_case 127.0.0.1 quic-no-backend no-backend-rejection no-backend-rejection

echo M2_SOCKS5_UDP_INGRESS_OK
