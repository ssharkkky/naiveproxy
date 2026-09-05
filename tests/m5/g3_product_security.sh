#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../.." && pwd)
forwardproxy_dir=${M5_FORWARDPROXY_DIR:-/path/to/naive-forwardproxy-m4}
caddy_dir=${M5_CADDY_DIR:-/path/to/caddy-naive-udp-m4}
caddy_bin=${M5_CADDY_BIN:-$forwardproxy_dir/build/m4-caddy}
m3_runner="${NAIVE_BUILD_DIR:-$repo_dir/src/out/Release}/naive_socks5_udp_m3_runner"
m2_runner="${NAIVE_BUILD_DIR:-$repo_dir/src/out/Release}/naive_socks5_udp_runner"
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/naive-m5-g3.XXXXXX")
caddy_pid=
runner_pid=
echo4_pid=
echo6_pid=
dns_pid=

cleanup() {
  for pid in "$runner_pid" "$caddy_pid" "$echo4_pid" "$echo6_pid" "$dns_pid"; do
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
    fi
  done
  if [ "${M5_KEEP_ARTIFACTS:-0}" = 1 ]; then
    printf '%s\n' "M5_G3_ARTIFACTS=$tmp_dir" >&2
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
      tail -60 "$log" >&2 || true
    fi
  done
  exit 1
}

wait_for_log() {
  marker=$1
  log=$2
  pid=$3
  attempts=${4:-160}
  i=0
  while [ "$i" -lt "$attempts" ]; do
    grep -q "$marker" "$log" 2>/dev/null && return 0
    kill -0 "$pid" 2>/dev/null || return 1
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
ninja -C "${NAIVE_BUILD_DIR:-$repo_dir/src/out/Release}" naive_socks5_udp_m3_runner \
  naive_socks5_udp_runner >/dev/null
test -x "$m3_runner"
test -x "$m2_runner"
test -x "$caddy_bin"
export PYTHONDONTWRITEBYTECODE=1

topology=$(python3 "$script_dir/topology.py")
topology_value() {
  printf '%s\n' "$topology" | python3 -c \
    "import json, sys; print(json.load(sys.stdin)['$1'])"
}
proxy_port=$(topology_value proxy)
echo_port=$(topology_value echo_ipv4)
dns_port=$(topology_value dns)

python3 -u "$repo_dir/tests/masque_udp_echo.py" \
  --host=127.0.0.1 --port="$echo_port" >"$tmp_dir/echo4.log" 2>&1 &
echo4_pid=$!
python3 -u "$repo_dir/tests/masque_udp_echo.py" \
  --host=::1 --port="$echo_port" >"$tmp_dir/echo6.log" 2>&1 &
echo6_pid=$!
python3 -u "$repo_dir/tests/masque_udp_dns.py" \
  --host=127.0.0.1 --port="$dns_port" >"$tmp_dir/dns.log" 2>&1 &
dns_pid=$!
wait_for_log '^READY ' "$tmp_dir/echo4.log" "$echo4_pid" || \
  fail_with_logs "IPv4 echo fixture did not become ready"
wait_for_log '^READY ' "$tmp_dir/echo6.log" "$echo6_pid" || \
  fail_with_logs "IPv6 echo fixture did not become ready"
wait_for_log '^READY ' "$tmp_dir/dns.log" "$dns_pid" || \
  fail_with_logs "DNS fixture did not become ready"

start_caddy() {
  label=$1
  config=$2
  caddy_log="$tmp_dir/caddy-$label.log"
  access_log="$tmp_dir/access-$label.log"
  M5_PROXY_PORT="$proxy_port" M5_ALLOWED_PORT="$echo_port" \
  M5_ACCESS_LOG="$access_log" \
  XDG_DATA_HOME="$tmp_dir/caddy-data" XDG_CONFIG_HOME="$tmp_dir/caddy-config" \
    "$caddy_bin" run --config "$forwardproxy_dir/tests/m5/$config" \
      --adapter caddyfile >"$caddy_log" 2>&1 &
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
      return
    fi
    kill -0 "$caddy_pid" 2>/dev/null || fail_with_logs "Caddy $label exited"
    sleep 0.05
    i=$((i + 1))
  done
  fail_with_logs "Caddy $label did not become ready"
}

stop_caddy() {
  kill "$caddy_pid" 2>/dev/null || true
  wait "$caddy_pid" 2>/dev/null || true
  caddy_pid=
}

start_runner() {
  label=$1
  run_for_ms=$2
  shift 2
  runner_log="$tmp_dir/runner-$label.log"
  net_log="$tmp_dir/netlog-$label.json"
  "$m3_runner" --proxy-host=127.0.0.1 --proxy-port="$proxy_port" \
    --run-for-ms="$run_for_ms" --log-net-log="$net_log" "$@" \
    >"$runner_log" 2>&1 &
  runner_pid=$!
  wait_for_log 'M3_SOCKS5_UDP_READY' "$runner_log" "$runner_pid" 160 || \
    fail_with_logs "runner $label did not become ready"
  socks_port=$(sed -n \
    's/.*M3_SOCKS5_UDP_READY.* port=\([0-9][0-9]*\).*/\1/p' \
    "$runner_log" | head -1)
  [ -n "$socks_port" ] || fail_with_logs "runner $label omitted SOCKS port"
}

finish_runner() {
  wait "$runner_pid" || fail_with_logs "runner failed"
  runner_pid=
  grep -q 'M3_PRODUCTION_FACTORY_ELIGIBILITY_OK' "$runner_log" || \
    fail_with_logs "production factory evidence is absent"
  grep -q 'M3_RUNNER_PROXY_DESTROYED_BEFORE_CONTEXT' "$runner_log" || \
    fail_with_logs "runner destruction ordering is absent"
}

run_echo() {
  marker=$1
  python3 "$repo_dir/tests/socks5_udp_m3.py" --host 127.0.0.1 \
    --port "$socks_port" --target-host 127.0.0.1 --target-port "$echo_port" \
    --payload "$marker" --marker "$marker"
}

run_failure() {
  marker=$1
  target_host=$2
  target_port=$3
  force=$4
  shift 4
  force_flag=
  [ "$force" -eq 0 ] || force_flag=--force-domain
  python3 "$script_dir/g3_matrix.py" --socks-port "$socks_port" \
    --mode target-failure --target-host "$target_host" --target-port "$target_port" \
    $force_flag --marker "$marker" "$@"
}

start_caddy base Caddyfile
start_runner auth-correct 3500 --proxy-user=m5-user --proxy-pass=m5-pass
run_echo M5_G3_UPSTREAM_AUTH_CORRECT_OK
finish_runner
start_runner auth-missing 4500
run_failure M5_G3_UPSTREAM_AUTH_MISSING_OK 127.0.0.1 "$echo_port" 0
finish_runner
start_runner auth-wrong 4500 --proxy-user=m5-user --proxy-pass=wrong-pass
run_failure M5_G3_UPSTREAM_AUTH_WRONG_OK 127.0.0.1 "$echo_port" 0
finish_runner
start_runner local-auth 4000 --proxy-user=m5-user --proxy-pass=m5-pass \
  --listen-user=m5-local --listen-pass=m5-local-pass
python3 "$script_dir/g3_matrix.py" --socks-port "$socks_port" \
  --mode local-auth --target-port "$echo_port" \
  --username m5-local --password m5-local-pass
finish_runner
start_runner malformed-privacy 6500 --proxy-user=m5-user --proxy-pass=m5-pass
python3 "$script_dir/g3_matrix.py" --socks-port "$socks_port" \
  --mode malformed --target-port "$echo_port"
python3 "$repo_dir/tests/socks5_udp_m3.py" --host 127.0.0.1 \
  --port "$socks_port" --target-host m5-private-target.localhost \
  --target-port "$echo_port" --force-domain --payload m5-private-payload \
  --marker M5_G3_PRIVACY_TRAFFIC_OK
finish_runner
start_runner admission 12000 --proxy-user=m5-user --proxy-pass=m5-pass
python3 "$script_dir/g3_matrix.py" --socks-port "$socks_port" \
  --mode admission --target-port "$echo_port"
finish_runner
start_runner dns-failure 4500 --proxy-user=m5-user --proxy-pass=m5-pass
run_failure M5_G3_DNS_FAILURE_OK m5-does-not-exist.invalid "$echo_port" 1 \
  --recovery-port "$echo_port"
run_echo M5_G3_DNS_FAILURE_ISOLATION_OK
finish_runner
stop_caddy

start_caddy port Caddyfile-port-deny
start_runner port-policy 5000 --proxy-user=m5-user --proxy-pass=m5-pass
run_failure M5_G3_PORT_POLICY_DENIED_OK 127.0.0.1 "$dns_port" 0 \
  --recovery-port "$echo_port"
finish_runner
stop_caddy

start_caddy acl Caddyfile-acl-deny
start_runner acl-policy 5000 --proxy-user=m5-user --proxy-pass=m5-pass
run_failure M5_G3_ACL_POLICY_DENIED_OK m5-policy.localhost "$echo_port" 1 \
  --recovery-port "$echo_port"
run_echo M5_G3_ACL_POLICY_ISOLATION_OK
finish_runner
stop_caddy

start_caddy upstream Caddyfile-upstream
start_runner upstream-policy 4500 --proxy-user=m5-user --proxy-pass=m5-pass
run_failure M5_G3_UPSTREAM_UNSUPPORTED_OK 127.0.0.1 "$echo_port" 0
finish_runner
stop_caddy

jq -s -e 'any(.[]; .status == 200) and any(.[]; .status == 407) and
           any(.[]; .status == 502) and any(.[]; .status == 503)' \
  "$tmp_dir/access-base.log" >/dev/null || \
  fail_with_logs "base auth status evidence is incomplete"
jq -s -e 'any(.[]; .status == 403) and any(.[]; .status == 200)' \
  "$tmp_dir/access-port.log" >/dev/null || \
  fail_with_logs "port policy status evidence is incomplete"
jq -s -e 'any(.[]; .status == 403) and any(.[]; .status == 200)' \
  "$tmp_dir/access-acl.log" >/dev/null || \
  fail_with_logs "ACL policy status evidence is incomplete"
jq -s -e 'any(.[]; .status == 501)' "$tmp_dir/access-upstream.log" >/dev/null || \
  fail_with_logs "upstream rejection status evidence is incomplete"

run_non_quic_rejection() {
  scheme=$1
  label=$2
  log="$tmp_dir/non-quic-$label.log"
  "$m2_runner" --listen-host=127.0.0.1 --proxy-scheme="$scheme" \
    >"$log" 2>&1 &
  runner_pid=$!
  wait_for_log 'M2_SOCKS5_UDP_READY' "$log" "$runner_pid" 100 || \
    fail_with_logs "non-QUIC runner $label did not become ready"
  port=$(sed -n 's/.* port=\([0-9][0-9]*\).*/\1/p' "$log" | head -1)
  python3 "$repo_dir/tests/socks5_udp_m2.py" --host 127.0.0.1 \
    --port "$port" --mode rejection >/dev/null
  kill "$runner_pid" 2>/dev/null || true
  wait "$runner_pid" 2>/dev/null || true
  runner_pid=
}

run_non_quic_rejection direct direct
run_non_quic_rejection https h2
run_non_quic_rejection mixed mixed
run_non_quic_rejection quic-no-backend no-backend
echo M5_G3_NON_QUIC_REJECTION_OK

encoded=bTUtdXNlcjptNS1wYXNz
double_encoded=YlRVdGRYTmxjanB0TlMxd1lYTno=
wrong_encoded=$(printf 'm5-user:wrong-pass' | base64 | tr -d '\n')
local_encoded=$(printf 'm5-local:m5-local-pass' | base64 | tr -d '\n')
for sentinel in m5-private-target.localhost m5-private-payload \
  m5-does-not-exist.invalid m5-policy.localhost m5-g3-target-failure \
  m5-local-pass m5-pass wrong-pass "$encoded" "$double_encoded" \
  "$wrong_encoded" "$local_encoded" '.well-known/masque/udp'; do
  if grep -F "$sentinel" "$tmp_dir"/caddy-*.log "$tmp_dir"/access-*.log \
    "$tmp_dir"/runner-*.log "$tmp_dir"/netlog-*.json >/dev/null 2>&1; then
    fail_with_logs "private G3 sentinel appeared in retained evidence"
  fi
done

echo M5_G3_AUTH_POLICY_OK
echo M5_G3_PRIVACY_OK
