#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../.." && pwd)
forwardproxy_dir=${M5_FORWARDPROXY_DIR:-/path/to/naive-forwardproxy-m4}
caddy_dir=${M5_CADDY_DIR:-/path/to/caddy-naive-udp-m4}
caddy_bin=${M5_CADDY_BIN:-$forwardproxy_dir/build/m4-caddy}
runner_bin="$repo_dir/src/out/Release/naive_socks5_udp_m3_runner"
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/naive-m5-g1.XXXXXX")
caddy_pid=
runner_pid=
echo_pid=

cleanup() {
  for pid in "$runner_pid" "$caddy_pid" "$echo_pid"; do
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
    fi
  done
  if [ "${M5_KEEP_ARTIFACTS:-0}" = 1 ]; then
    printf '%s\n' "M5_G1_ARTIFACTS=$tmp_dir" >&2
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
  message=$1
  printf '%s\n' "$message" >&2
  for log in "$tmp_dir/caddy.log" "$tmp_dir/access.log" \
    "$tmp_dir/runner.log" "$tmp_dir/echo.log"; do
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

expected_forwardproxy=${M5_EXPECTED_FORWARDPROXY:-8f044e278c70d7479c644eb0ebfffc6bb4b7b3c7}
expected_caddy=${M5_EXPECTED_CADDY:-cce894a8a0e987eb1722cf99729499bdaba6c38d}
test "$(git -C "$forwardproxy_dir" rev-parse "$expected_forwardproxy")" = \
  "$expected_forwardproxy"
unexpected_server_changes=$(git -C "$forwardproxy_dir" diff --name-only \
  "$expected_forwardproxy"..HEAD | sed '/^tests\/m5\//d')
test -z "$unexpected_server_changes"
test "$(git -C "$caddy_dir" rev-parse HEAD)" = "$expected_caddy"
git -C "$repo_dir" diff --quiet ${M5_EXPECTED_CLIENT:-333b7cb253}..HEAD -- src/net
git -C "$repo_dir" diff --quiet -- src/net
test -x "$runner_bin"
test -x "$caddy_bin"

topology=$(python3 "$script_dir/topology.py")
proxy_port=$(printf '%s\n' "$topology" | python3 -c \
  'import json, sys; print(json.load(sys.stdin)["proxy"])')
echo_port=$(printf '%s\n' "$topology" | python3 -c \
  'import json, sys; print(json.load(sys.stdin)["echo_ipv4"])')
caddy_log="$tmp_dir/caddy.log"
access_log="$tmp_dir/access.log"
runner_log="$tmp_dir/runner.log"
net_log="$tmp_dir/netlog.json"
echo_log="$tmp_dir/echo.log"
payload=m5-private-payload-g1
proxy_user=m5-user
proxy_pass=m5-pass
encoded_credentials=bTUtdXNlcjptNS1wYXNz

python3 -u "$repo_dir/tests/masque_udp_echo.py" \
  --host=127.0.0.1 --port="$echo_port" >"$echo_log" 2>&1 &
echo_pid=$!
wait_for_log '^READY ' "$echo_log" "$echo_pid" 100 || \
  fail_with_logs "IPv4 echo fixture did not become ready"

M5_PROXY_PORT="$proxy_port" M5_ACCESS_LOG="$access_log" \
XDG_DATA_HOME="$tmp_dir/caddy-data" XDG_CONFIG_HOME="$tmp_dir/caddy-config" \
  "$caddy_bin" run --config "$forwardproxy_dir/tests/m5/Caddyfile" \
    --adapter caddyfile \
  >"$caddy_log" 2>&1 &
caddy_pid=$!

i=0
while [ "$i" -lt 160 ]; do
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
    break
  fi
  kill -0 "$caddy_pid" 2>/dev/null || \
    fail_with_logs "production Caddy exited during startup"
  sleep 0.05
  i=$((i + 1))
done
[ "$i" -lt 160 ] || fail_with_logs "production Caddy did not become ready"

"$runner_bin" --proxy-host=127.0.0.1 --proxy-port="$proxy_port" \
  --proxy-user="$proxy_user" --proxy-pass="$proxy_pass" --run-for-ms=3500 \
  --log-net-log="$net_log" >"$runner_log" 2>&1 &
runner_pid=$!
wait_for_log 'M3_SOCKS5_UDP_READY' "$runner_log" "$runner_pid" 160 || \
  fail_with_logs "M3 production runner did not become ready"
socks_port=$(sed -n \
  's/.*M3_SOCKS5_UDP_READY.* port=\([0-9][0-9]*\).*/\1/p' \
  "$runner_log" | head -1)
[ -n "$socks_port" ] || fail_with_logs "runner did not report a SOCKS port"

python3 "$repo_dir/tests/socks5_udp_m3.py" \
  --host 127.0.0.1 --port "$socks_port" \
  --target-host 127.0.0.1 --target-port "$echo_port" \
  --payload "$payload" --marker M5_G1_IPV4_ECHO_BYTES_OK

wait "$runner_pid" || fail_with_logs "M3 production runner failed"
runner_pid=
wait_for_log 'connect-udp association event' "$caddy_log" "$caddy_pid" 100 || \
  fail_with_logs "production server did not close the CONNECT-UDP association"

grep -q 'M3_PRODUCTION_FACTORY_ELIGIBILITY_OK' "$runner_log" || \
  fail_with_logs "runner did not prove the production backend factory"
grep -q 'M3_RUNNER_PROXY_DESTROYED_BEFORE_CONTEXT' "$runner_log" || \
  fail_with_logs "runner did not prove destruction ordering"
grep -q 'M3_NET_LOG_WRITTEN' "$runner_log" || \
  fail_with_logs "runner did not flush NetLog"
grep -q 'connect-udp association event' "$caddy_log" || \
  fail_with_logs "server lifecycle evidence is absent"
jq -e --arg authority "127.0.0.1:$proxy_port" '
  any(inputs;
      .request.method == "CONNECT" and
      .request.proto == "connect-udp" and
      .request.uri == $authority and
      .request.headers["Proxy-Authorization"] == ["REDACTED"] and
      .status == 200)
' /dev/null "$access_log" >/dev/null || \
  fail_with_logs "access log lacks a successful target-redacted CONNECT-UDP request"
grep -q "RX bytes=${#payload} " "$echo_log" || \
  fail_with_logs "echo target did not observe the expected byte count"

jq -e '
  (.constants.logSourceType.QUIC_PROXY_DATAGRAM_CLIENT_SOCKET) as $source |
  (.constants.logEventTypes.SOCKET_BYTES_SENT) as $sent |
  any(.events[];
      .source.type == $source and .type == $sent and
      (.params.byte_count // 0) > 0 and
      ((.params | has("bytes")) | not))
' "$net_log" >/dev/null || \
  fail_with_logs "NetLog lacks a redacted QUIC proxy datagram send event"
grep -q 'CONNECT-UDP \[redacted\] HTTP/3' "$net_log" || \
  fail_with_logs "NetLog lacks redacted CONNECT-UDP request evidence"

if grep -E "$payload|127\.0\.0\.1/$echo_port|$proxy_user:$proxy_pass|$proxy_pass|$encoded_credentials" \
  "$caddy_log" "$access_log" "$runner_log" "$net_log" >/dev/null 2>&1; then
  fail_with_logs "private M5 data appeared in client or server logs"
fi

echo M5_G1_AUTHENTICATED_CONNECT_UDP_OK
echo M5_G1_H3_DATAGRAM_EVIDENCE_OK
echo M5_G1_LOG_PRIVACY_OK
echo M5_G1_CROSS_REPO_ECHO_OK
