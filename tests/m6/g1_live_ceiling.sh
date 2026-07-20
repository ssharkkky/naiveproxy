#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../.." && pwd)
forwardproxy_dir=${M6_FORWARDPROXY_DIR:-/Users/stoneshi/Documents/naive-forwardproxy-m4}
caddy_dir=${M6_CADDY_DIR:-/Users/stoneshi/Documents/caddy-naive-udp-m4}
caddy_bin=${M6_CADDY_BIN:-$forwardproxy_dir/build/m4-caddy}
runner_bin="$repo_dir/src/out/Release/naive_socks5_udp_m3_runner"
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/naive-m6-g1.XXXXXX")
caddy_pid=
runner_pid=
echo4_pid=
echo6_pid=
shaper_pid=

cleanup() {
  for pid in "$runner_pid" "$caddy_pid" "$echo4_pid" "$echo6_pid" \
    "$shaper_pid"; do
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
    fi
  done
  if [ "${M6_KEEP_ARTIFACTS:-0}" = 1 ]; then
    printf '%s\n' "M6_G1_ARTIFACTS=$tmp_dir" >&2
  else
    rm -r "$tmp_dir"
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
  attempts=$4
  index=0
  while [ "$index" -lt "$attempts" ]; do
    if grep -q "$marker" "$log" 2>/dev/null; then
      return 0
    fi
    if ! kill -0 "$pid" 2>/dev/null; then
      return 1
    fi
    sleep 0.05
    index=$((index + 1))
  done
  return 1
}

expected_client=eaf172d9713dafc6519d8c4a6b8ba3a290c222de
expected_forwardproxy=8f044e278c70d7479c644eb0ebfffc6bb4b7b3c7
expected_caddy=cce894a8a0e987eb1722cf99729499bdaba6c38d

unexpected_client_changes=$(git -C "$repo_dir" diff --name-only \
  "$expected_client"..HEAD -- src/net | \
  sed '/^src\/net\/tools\/naive\/naive_connect_udp_backend_test_bin\.cc$/d')
test -z "$unexpected_client_changes"
unexpected_worktree_changes=$(git -C "$repo_dir" diff --name-only -- src/net | \
  sed '/^src\/net\/tools\/naive\/naive_connect_udp_backend_test_bin\.cc$/d')
test -z "$unexpected_worktree_changes"
unexpected_server_changes=$(git -C "$forwardproxy_dir" diff --name-only \
  "$expected_forwardproxy"..HEAD | sed '/^tests\/m5\//d; /^tests\/m6\//d')
test -z "$unexpected_server_changes"
test "$(git -C "$caddy_dir" rev-parse HEAD)" = "$expected_caddy"
git -C "$forwardproxy_dir" diff --quiet
git -C "$caddy_dir" diff --quiet
test -x "$runner_bin"
test -x "$caddy_bin"

export PYTHONDONTWRITEBYTECODE=1

topology=$(python3 "$repo_dir/tests/m5/topology.py")
proxy_port=$(printf '%s\n' "$topology" | python3 -c \
  'import json, sys; print(json.load(sys.stdin)["proxy"])')
probe_mode=${M6_G1_PROBE_MODE:-ceiling}
server_port=$proxy_port
if [ "$probe_mode" = pmtu ]; then
  server_port=$(PYTHONPATH="$repo_dir/tests/m5" python3 -c '
import topology
print(topology.reserve_shared_ipv4_port())')
  test "$server_port" != "$proxy_port"
  printf '0\n' >"$tmp_dir/outer-ceiling"
  python3 -u "$script_dir/udp_shaper.py" --listen-port "$proxy_port" \
    --server-port "$server_port" --ceiling-file "$tmp_dir/outer-ceiling" \
    >"$tmp_dir/shaper.log" 2>&1 &
  shaper_pid=$!
  wait_for_log '^READY udp-shaper ' "$tmp_dir/shaper.log" "$shaper_pid" 100 || \
    fail_with_logs "UDP shaper did not become ready"
elif [ "$probe_mode" != ceiling ]; then
  fail_with_logs "unknown M6_G1_PROBE_MODE"
fi
echo_port=$(python3 -c '
import socket
while True:
    ipv4 = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    ipv4.bind(("127.0.0.1", 0))
    port = ipv4.getsockname()[1]
    ipv6 = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
    try:
        ipv6.bind(("::1", port))
    except OSError:
        ipv4.close()
        ipv6.close()
        continue
    ipv4.close()
    ipv6.close()
    print(port)
    break')

python3 -u "$repo_dir/tests/masque_udp_echo.py" \
  --host=127.0.0.1 --port="$echo_port" >"$tmp_dir/echo4.log" 2>&1 &
echo4_pid=$!
python3 -u "$repo_dir/tests/masque_udp_echo.py" \
  --host=::1 --port="$echo_port" >"$tmp_dir/echo6.log" 2>&1 &
echo6_pid=$!
wait_for_log '^READY ' "$tmp_dir/echo4.log" "$echo4_pid" 100 || \
  fail_with_logs "IPv4 echo fixture did not become ready"
wait_for_log '^READY ' "$tmp_dir/echo6.log" "$echo6_pid" 100 || \
  fail_with_logs "IPv6 echo fixture did not become ready"

M6_SERVER_PORT="$server_port" M6_ACCESS_LOG="$tmp_dir/access.log" \
XDG_DATA_HOME="$tmp_dir/caddy-data" XDG_CONFIG_HOME="$tmp_dir/caddy-config" \
  "$caddy_bin" run --config "$script_dir/Caddyfile" \
    --adapter caddyfile >"$tmp_dir/caddy.log" 2>&1 &
caddy_pid=$!

index=0
while [ "$index" -lt 160 ]; do
  if python3 - "$server_port" <<'PY'
import socket
import sys

probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
probe.settimeout(0.1)
try:
    probe.connect(("127.0.0.1", int(sys.argv[1])))
except OSError:
    raise SystemExit(1)
finally:
    probe.close()
PY
  then
    break
  fi
  kill -0 "$caddy_pid" 2>/dev/null || fail_with_logs "production Caddy exited"
  sleep 0.05
  index=$((index + 1))
done
test "$index" -lt 160 || fail_with_logs "production Caddy did not become ready"

"$runner_bin" --proxy-host=127.0.0.1 --proxy-port="$proxy_port" \
  --proxy-user=m5-user --proxy-pass=m5-pass --run-for-ms=60000 \
  --log-net-log="$tmp_dir/netlog.json" \
  >"$tmp_dir/runner.log" 2>&1 &
runner_pid=$!
wait_for_log 'M3_SOCKS5_UDP_READY' "$tmp_dir/runner.log" "$runner_pid" 200 || \
  fail_with_logs "M3 production runner did not become ready"
socks_port=$(sed -n \
  's/.*M3_SOCKS5_UDP_READY.* port=\([0-9][0-9]*\).*/\1/p' \
  "$tmp_dir/runner.log" | head -1)
test -n "$socks_port" || fail_with_logs "runner did not report a SOCKS port"

if [ "$probe_mode" = ceiling ]; then
  python3 "$script_dir/payload_probe.py" --socks-port "$socks_port" \
    --echo-port "$echo_port" | tee "$tmp_dir/probe.log"
  expected_marker=M6_G1_LIVE_PRODUCT_CEILING_OK
else
  python3 "$script_dir/pmtu_probe.py" --socks-port "$socks_port" \
    --echo-port "$echo_port" --ceiling-file "$tmp_dir/outer-ceiling" | \
    tee "$tmp_dir/probe.log"
  expected_marker=M6_G1C_PMTU_RECOVERY_OK
fi

kill "$runner_pid" 2>/dev/null || true
wait "$runner_pid" 2>/dev/null || true
runner_pid=

grep -q 'M3_PRODUCTION_FACTORY_ELIGIBILITY_OK' "$tmp_dir/runner.log" || \
  fail_with_logs "production backend factory evidence is absent"
grep -q 'CONNECT-UDP \[redacted\] HTTP/3' "$tmp_dir/netlog.json" || \
  fail_with_logs "redacted CONNECT-UDP NetLog evidence is absent"
grep -q "$expected_marker" "$tmp_dir/probe.log" || \
  fail_with_logs "G1 probe marker is absent"
association_count=$(grep -c 'connect-udp association event' "$tmp_dir/caddy.log" || true)
test "$association_count" -ge 3 || fail_with_logs "server association evidence is incomplete"

if grep -E "\.well-known/masque/udp|localhost/$echo_port|127\.0\.0\.1/$echo_port|\[::1\]/$echo_port|m5-pass|bTUtdXNlcjptNS1wYXNz" \
  "$tmp_dir/caddy.log" "$tmp_dir/access.log" "$tmp_dir/runner.log" \
  "$tmp_dir/netlog.json" >/dev/null 2>&1; then
  fail_with_logs "private G1 target or credential data appeared in evidence"
fi

echo M6_G1_LIVE_CEILING_PRIVACY_OK
if [ "$probe_mode" = ceiling ]; then
  echo M6_G1B_LIVE_CEILING_OK
else
  grep -q 'ceiling=1232 action=drop' "$tmp_dir/shaper.log" || \
    fail_with_logs "PMTU shaper did not drop an oversized outer packet"
  awk '
    /direction=c2s/ && /ceiling=1232/ && /action=forward/ {
      for (field = 1; field <= NF; field++) {
        if ($field ~ /^size=/) {
          split($field, value, "=")
          if (value[2] >= 1200 && value[2] <= 1232) found = 1
        }
      }
    }
    END { exit found ? 0 : 1 }
  ' "$tmp_dir/shaper.log" || \
    fail_with_logs "safe payload lacked IPv6-minimum PMTU wire evidence"
  echo M6_G1C_PMTU_OK
fi
