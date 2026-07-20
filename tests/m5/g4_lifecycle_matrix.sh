#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../.." && pwd)
forwardproxy_dir=${M5_FORWARDPROXY_DIR:-/path/to/naive-forwardproxy-m4}
caddy_dir=${M5_CADDY_DIR:-/path/to/caddy-naive-udp-m4}
caddy_bin=${M5_CADDY_BIN:-$forwardproxy_dir/build/m4-caddy}
runner_bin="$repo_dir/src/out/Release/naive_socks5_udp_m3_runner"
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/naive-m5-g4.XXXXXX")
caddy_pid=
runner_pid=
client_pid=
echo4_pid=
echo6_pid=
caddy_log=
runner_log=
socks_port=

cleanup() {
  for pid in "$client_pid" "$runner_pid" "$caddy_pid" \
    "$echo4_pid" "$echo6_pid"; do
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
    fi
  done
  if [ "${M5_KEEP_ARTIFACTS:-0}" = 1 ]; then
    printf '%s\n' "M5_G4_ARTIFACTS=$tmp_dir" >&2
  else
    find "$tmp_dir" -type f -exec unlink {} \; >/dev/null 2>&1 || true
    find "$tmp_dir" -depth -type d -exec rmdir {} \; >/dev/null 2>&1 || true
  fi
}

on_signal() {
  trap - EXIT HUP INT TERM
  cleanup
  exit 130
}

trap cleanup EXIT
trap on_signal HUP INT TERM

fail_with_logs() {
  printf '%s\n' "$1" >&2
  for log in "$tmp_dir"/*.log; do
    if [ -f "$log" ]; then
      printf '%s\n' "--- $(basename "$log") ---" >&2
      tail -80 "$log" >&2 || true
    fi
  done
  exit 1
}

wait_for_log() {
  marker=$1
  log=$2
  pid=$3
  attempts=${4:-200}
  i=0
  while [ "$i" -lt "$attempts" ]; do
    if grep -q "$marker" "$log" 2>/dev/null; then
      return 0
    fi
    if ! kill -0 "$pid" 2>/dev/null; then
      return 1
    fi
    sleep 0.05
    i=$((i + 1))
  done
  return 1
}

start_caddy() {
  label=$1
  config=$2
  caddy_log="$tmp_dir/caddy-$label.log"
  access_log="$tmp_dir/access-$label.log"
  M5_PROXY_PORT="$proxy_port" M5_ACCESS_LOG="$access_log" \
  M5_SERVER_CERT="${server_certificate:-unused}" \
  M5_SERVER_KEY="${server_private_key:-unused}" \
  XDG_DATA_HOME="$tmp_dir/caddy-data" XDG_CONFIG_HOME="$tmp_dir/caddy-config" \
    "$caddy_bin" run --config "$forwardproxy_dir/tests/m5/$config" \
      --adapter caddyfile >"$caddy_log" 2>&1 &
  caddy_pid=$!
  i=0
  while [ "$i" -lt 200 ]; do
    if python3 - "$proxy_port" <<'PY'
import socket
import sys

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.settimeout(0.1)
try:
    sock.connect(("127.0.0.1", int(sys.argv[1])))
except OSError:
    raise SystemExit(1)
finally:
    sock.close()
PY
    then
      return
    fi
    kill -0 "$caddy_pid" 2>/dev/null || fail_with_logs "Caddy exited: $label"
    sleep 0.05
    i=$((i + 1))
  done
  fail_with_logs "Caddy did not become ready: $label"
}

stop_caddy() {
  kill "$caddy_pid"
  wait "$caddy_pid"
  caddy_pid=
}

start_runner() {
  label=$1
  lifetime=$2
  shift 2
  runner_log="$tmp_dir/runner-$label.log"
  "$runner_bin" --proxy-host=127.0.0.1 --proxy-port="$proxy_port" \
    --proxy-user=m5-user --proxy-pass=m5-pass --run-for-ms="$lifetime" \
    --log-net-log="$tmp_dir/netlog-$label.json" "$@" \
    >"$runner_log" 2>&1 &
  runner_pid=$!
  wait_for_log 'M3_SOCKS5_UDP_READY' "$runner_log" "$runner_pid" 200 || \
    fail_with_logs "runner did not become ready: $label"
  socks_port=$(sed -n \
    's/.*M3_SOCKS5_UDP_READY.* port=\([0-9][0-9]*\).*/\1/p' \
    "$runner_log" | head -1)
  [ -n "$socks_port" ] || fail_with_logs "runner omitted SOCKS port: $label"
}

finish_runner() {
  wait "$runner_pid" || fail_with_logs "runner failed"
  runner_pid=
  grep -q 'M3_RUNNER_PROXY_DESTROYED_BEFORE_CONTEXT' "$runner_log" || \
    fail_with_logs "runner destruction evidence is absent"
  grep -q 'M3_NET_LOG_WRITTEN' "$runner_log" || \
    fail_with_logs "runner NetLog evidence is absent"
}

wait_for_file() {
  file=$1
  pid=$2
  attempts=${3:-200}
  i=0
  while [ "$i" -lt "$attempts" ]; do
    [ -e "$file" ] && return
    kill -0 "$pid" 2>/dev/null || return 1
    sleep 0.05
    i=$((i + 1))
  done
  return 1
}

echo_count() {
  payload=$1
  hex=$(printf '%s' "$payload" | xxd -p | tr -d '\n')
  count=$(grep -h -c "hex=$hex" "$tmp_dir/echo4.log" "$tmp_dir/echo6.log" \
    2>/dev/null | awk '{sum += $1} END {print sum + 0}')
  printf '%s' "$count"
}

assert_echo_count() {
  payload=$1
  expected=$2
  actual=$(echo_count "$payload")
  [ "$actual" -eq "$expected" ] || \
    fail_with_logs "payload sequence count mismatch: expected $expected got $actual"
}

expected_forwardproxy=8f044e278c70d7479c644eb0ebfffc6bb4b7b3c7
expected_caddy=cce894a8a0e987eb1722cf99729499bdaba6c38d
expected_client=333b7cb253
test "$(git -C "$forwardproxy_dir" rev-parse "$expected_forwardproxy")" = \
  "$expected_forwardproxy"
unexpected_server_changes=$(git -C "$forwardproxy_dir" diff --name-only \
  "$expected_forwardproxy"..HEAD | sed '/^tests\/m5\//d')
test -z "$unexpected_server_changes"
test "$(git -C "$caddy_dir" rev-parse HEAD)" = "$expected_caddy"
git -C "$repo_dir" diff --quiet "$expected_client"..HEAD -- src/net
git -C "$repo_dir" diff --quiet -- src/net
ninja -C "$repo_dir/src/out/Release" naive_socks5_udp_m3_runner >/dev/null
test -x "$runner_bin"
test -x "$caddy_bin"
export PYTHONDONTWRITEBYTECODE=1

topology=$(python3 "$script_dir/topology.py")
topology_value() {
  printf '%s\n' "$topology" | python3 -c \
    "import json, sys; print(json.load(sys.stdin)['$1'])"
}
proxy_port=$(topology_value proxy)
echo_port=$(topology_value echo_ipv4)
pending_port=$(topology_value dns)

python3 -u "$repo_dir/tests/masque_udp_echo.py" \
  --host=127.0.0.1 --port="$echo_port" >"$tmp_dir/echo4.log" 2>&1 &
echo4_pid=$!
python3 -u "$repo_dir/tests/masque_udp_echo.py" \
  --host=::1 --port="$echo_port" >"$tmp_dir/echo6.log" 2>&1 &
echo6_pid=$!
wait_for_log '^READY ' "$tmp_dir/echo4.log" "$echo4_pid" || \
  fail_with_logs "IPv4 echo fixture did not become ready"
wait_for_log '^READY ' "$tmp_dir/echo6.log" "$echo6_pid" || \
  fail_with_logs "IPv6 echo fixture did not become ready"

start_caddy control Caddyfile
start_runner control 5500
python3 "$script_dir/g4_matrix.py" --mode control-close \
  --socks-port "$socks_port" --target-port "$echo_port" \
  --pending-port "$pending_port"
finish_runner
stop_caddy

restart_run=1
while [ "$restart_run" -le 2 ]; do
  start_caddy "restart-$restart_run-before" Caddyfile
  start_runner "restart-$restart_run" 12000
  ready_file="$tmp_dir/restart-$restart_run.ready"
  continue_file="$tmp_dir/restart-$restart_run.continue"
  python3 "$script_dir/g4_matrix.py" --mode restart \
    --socks-port "$socks_port" --target-port "$echo_port" \
    --ready-file "$ready_file" --continue-file "$continue_file" &
  client_pid=$!
  wait_for_file "$ready_file" "$client_pid" 200 || \
    fail_with_logs "restart client did not reach the pre-restart barrier"
  stop_caddy
  start_caddy "restart-$restart_run-after" Caddyfile
  : >"$continue_file"
  wait "$client_pid" || fail_with_logs "restart client failed"
  client_pid=
  finish_runner
  stop_caddy
  restart_run=$((restart_run + 1))
done

start_caddy session Caddyfile
start_runner session 8500 --shutdown-session-after-ms=2000
python3 "$script_dir/g4_matrix.py" --mode session-reconnect \
  --socks-port "$socks_port" --target-port "$echo_port" --pause-seconds=2.1
finish_runner
grep -q 'M3_SESSION_SHUTDOWN_ISSUED' "$runner_log" || \
  fail_with_logs "outer QUIC session shutdown evidence is absent"
stop_caddy

start_caddy client-idle Caddyfile
start_runner client-idle 37000
python3 "$script_dir/g4_matrix.py" --mode idle \
  --socks-port "$socks_port" --target-port "$echo_port" --pause-seconds=32 \
  --before-payload=m5-g4-client-idle-before-0021 \
  --after-payload=m5-g4-client-idle-after-0022 \
  --marker=M5_G4_CLIENT_IDLE_RECONNECT_OK
finish_runner
idle_associations=$(grep -c 'connect-udp association event' "$caddy_log" || true)
[ "$idle_associations" -ge 2 ] || \
  fail_with_logs "client idle did not create two server associations"
stop_caddy

assert_echo_count m5-g4-open-before-control-close 1
assert_echo_count m5-g4-open-after-control-close 0
assert_echo_count m5-g4-pending-control-close 0
assert_echo_count m5-g4-restart-before-0001 2
assert_echo_count m5-g4-restart-ambiguous-0002 0
assert_echo_count m5-g4-restart-fresh-0003 2
assert_echo_count m5-g4-session-a-before-0011 1
assert_echo_count m5-g4-session-b-before-0012 1
assert_echo_count m5-g4-session-ambiguous-0013 0
assert_echo_count m5-g4-session-a-fresh-0014 1
assert_echo_count m5-g4-session-b-fresh-0015 1
assert_echo_count m5-g4-client-idle-before-0021 1
assert_echo_count m5-g4-client-idle-after-0022 1

echo M5_G4_NO_REPLAY_OK
echo M5_G4_SHORT_LIFECYCLE_OK
