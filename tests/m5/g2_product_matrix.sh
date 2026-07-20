#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../.." && pwd)
forwardproxy_dir=${M5_FORWARDPROXY_DIR:-/path/to/naive-forwardproxy-m4}
caddy_dir=${M5_CADDY_DIR:-/path/to/caddy-naive-udp-m4}
caddy_bin=${M5_CADDY_BIN:-$forwardproxy_dir/build/m4-caddy}
go_bin=${GO_BIN:-/path/to/naive-m4/go1.25.12/bin/go}
runner_bin="$repo_dir/src/out/Release/naive_socks5_udp_m3_runner"
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/naive-m5-g2.XXXXXX")
caddy_pid=
runner_pid=
echo4_pid=
echo4v6_pid=
echo6_pid=
dns_pid=
h3v4_pid=
h3v6_pid=

cleanup() {
  for pid in "$runner_pid" "$caddy_pid" "$echo4_pid" "$echo4v6_pid" \
    "$echo6_pid" "$dns_pid" "$h3v4_pid" "$h3v6_pid"; do
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
    fi
  done
  if [ "${M5_KEEP_ARTIFACTS:-0}" = 1 ]; then
    printf '%s\n' "M5_G2_ARTIFACTS=$tmp_dir" >&2
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

expected_forwardproxy=8f044e278c70d7479c644eb0ebfffc6bb4b7b3c7
expected_caddy=cce894a8a0e987eb1722cf99729499bdaba6c38d
test "$(git -C "$forwardproxy_dir" rev-parse "$expected_forwardproxy")" = \
  "$expected_forwardproxy"
unexpected_server_changes=$(git -C "$forwardproxy_dir" diff --name-only \
  "$expected_forwardproxy"..HEAD | sed '/^tests\/m5\//d')
test -z "$unexpected_server_changes"
test "$(git -C "$caddy_dir" rev-parse HEAD)" = "$expected_caddy"
git -C "$repo_dir" diff --quiet 333b7cb253..HEAD -- src/net
git -C "$repo_dir" diff --quiet -- src/net
test -x "$runner_bin"
test -x "$caddy_bin"

export GOPATH="${M5_GO_CACHE_ROOT:-${TMPDIR:-/tmp}/naive-m5-go}/gopath"
export GOMODCACHE="${M5_GO_CACHE_ROOT:-${TMPDIR:-/tmp}/naive-m5-go}/modcache"
export GOCACHE="${M5_GO_CACHE_ROOT:-${TMPDIR:-/tmp}/naive-m5-go}/buildcache"
export PYTHONDONTWRITEBYTECODE=1

topology=$(python3 "$script_dir/topology.py")
topology_value() {
  printf '%s\n' "$topology" | python3 -c \
    "import json, sys; print(json.load(sys.stdin)['$1'])"
}
proxy_port=$(topology_value proxy)
echo4_port=$(topology_value echo_ipv4)
echo6_port=$(topology_value echo_ipv6)
dns_port=$(topology_value dns)
h3_port=$(topology_value http3)

certificate="$tmp_dir/h3-root.pem"
private_key="$tmp_dir/h3-root.key"
openssl req -x509 -newkey rsa:2048 -nodes -days 1 -sha256 \
  -subj '/CN=m5-h3.localhost' \
  -addext 'subjectAltName=DNS:m5-h3.localhost' \
  -addext 'basicConstraints=critical,CA:TRUE' \
  -addext 'keyUsage=critical,digitalSignature,keyEncipherment,keyCertSign' \
  -addext 'extendedKeyUsage=serverAuth' \
  -keyout "$private_key" -out "$certificate" >/dev/null 2>&1

(
  cd "$script_dir"
  "$go_bin" build -trimpath -o "$tmp_dir/h3-origin" ./cmd/h3-origin
  "$go_bin" build -trimpath -o "$tmp_dir/socks-h3-probe" ./cmd/socks-h3-probe
)

python3 -u "$repo_dir/tests/masque_udp_echo.py" \
  --host=127.0.0.1 --port="$echo4_port" >"$tmp_dir/echo4.log" 2>&1 &
echo4_pid=$!
python3 -u "$repo_dir/tests/masque_udp_echo.py" \
  --host=::1 --port="$echo4_port" >"$tmp_dir/echo4v6.log" 2>&1 &
echo4v6_pid=$!
python3 -u "$repo_dir/tests/masque_udp_echo.py" \
  --host=::1 --port="$echo6_port" >"$tmp_dir/echo6.log" 2>&1 &
echo6_pid=$!
python3 -u "$repo_dir/tests/masque_udp_dns.py" \
  --host=127.0.0.1 --port="$dns_port" >"$tmp_dir/dns.log" 2>&1 &
dns_pid=$!
"$tmp_dir/h3-origin" --bind="127.0.0.1:$h3_port" \
  --cert="$certificate" --key="$private_key" >"$tmp_dir/h3v4.log" 2>&1 &
h3v4_pid=$!
"$tmp_dir/h3-origin" --bind="[::1]:$h3_port" \
  --cert="$certificate" --key="$private_key" >"$tmp_dir/h3v6.log" 2>&1 &
h3v6_pid=$!

wait_for_log '^READY ' "$tmp_dir/echo4.log" "$echo4_pid" 100 || \
  fail_with_logs "IPv4 echo fixture did not become ready"
wait_for_log '^READY ' "$tmp_dir/echo4v6.log" "$echo4v6_pid" 100 || \
  fail_with_logs "dual-stack domain echo fixture did not become ready"
wait_for_log '^READY ' "$tmp_dir/echo6.log" "$echo6_pid" 100 || \
  fail_with_logs "IPv6 echo fixture did not become ready"
wait_for_log '^READY ' "$tmp_dir/dns.log" "$dns_pid" 100 || \
  fail_with_logs "DNS fixture did not become ready"
wait_for_log 'M5_G2_H3_ORIGIN_READY' "$tmp_dir/h3v4.log" "$h3v4_pid" 200 || \
  fail_with_logs "IPv4 HTTP/3 fixture did not become ready"
wait_for_log 'M5_G2_H3_ORIGIN_READY' "$tmp_dir/h3v6.log" "$h3v6_pid" 200 || \
  fail_with_logs "IPv6 HTTP/3 fixture did not become ready"

M5_PROXY_PORT="$proxy_port" M5_ACCESS_LOG="$tmp_dir/access.log" \
XDG_DATA_HOME="$tmp_dir/caddy-data" XDG_CONFIG_HOME="$tmp_dir/caddy-config" \
  "$caddy_bin" run --config "$forwardproxy_dir/tests/m5/Caddyfile" \
    --adapter caddyfile >"$tmp_dir/caddy.log" 2>&1 &
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
  kill -0 "$caddy_pid" 2>/dev/null || fail_with_logs "production Caddy exited"
  sleep 0.05
  i=$((i + 1))
done
[ "$i" -lt 160 ] || fail_with_logs "production Caddy did not become ready"

"$runner_bin" --proxy-host=127.0.0.1 --proxy-port="$proxy_port" \
  --proxy-user=m5-user --proxy-pass=m5-pass --run-for-ms=25000 \
  --log-net-log="$tmp_dir/netlog.json" >"$tmp_dir/runner.log" 2>&1 &
runner_pid=$!
wait_for_log 'M3_SOCKS5_UDP_READY' "$tmp_dir/runner.log" "$runner_pid" 200 || \
  fail_with_logs "M3 production runner did not become ready"
socks_port=$(sed -n \
  's/.*M3_SOCKS5_UDP_READY.* port=\([0-9][0-9]*\).*/\1/p' \
  "$tmp_dir/runner.log" | head -1)
[ -n "$socks_port" ] || fail_with_logs "runner did not report a SOCKS port"

python3 "$script_dir/udp_matrix.py" --socks-port "$socks_port" \
  --echo4-port "$echo4_port" --echo6-port "$echo6_port" --dns-port "$dns_port"

"$tmp_dir/socks-h3-probe" --socks="127.0.0.1:$socks_port" \
  --target-host=m5-h3.localhost --target-port="$h3_port" --force-domain \
  --server-name=m5-h3.localhost --ca-cert="$certificate" --timeout=12s

wait "$runner_pid" || fail_with_logs "M3 production runner failed"
runner_pid=
grep -q 'M3_PRODUCTION_FACTORY_ELIGIBILITY_OK' "$tmp_dir/runner.log" || \
  fail_with_logs "production factory evidence is absent"
grep -q 'CONNECT-UDP \[redacted\] HTTP/3' "$tmp_dir/netlog.json" || \
  fail_with_logs "redacted CONNECT-UDP NetLog evidence is absent"
association_count=$(grep -c 'connect-udp association event' "$tmp_dir/caddy.log" || true)
[ "$association_count" -ge 8 ] || fail_with_logs "server association evidence is incomplete"

if grep -E 'm5-g2-|m5-h3\.localhost|\.well-known/masque/udp|m5-pass|bTUtdXNlcjptNS1wYXNz' \
  "$tmp_dir/caddy.log" "$tmp_dir/access.log" "$tmp_dir/runner.log" \
  "$tmp_dir/netlog.json" >/dev/null 2>&1; then
  fail_with_logs "private G2 data appeared in client or server evidence"
fi

echo M5_G2_PRODUCT_MATRIX_OK
