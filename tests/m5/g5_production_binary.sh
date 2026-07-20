#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../.." && pwd)
forwardproxy_dir=${M5_FORWARDPROXY_DIR:-/path/to/naive-forwardproxy-m4}
caddy_dir=${M5_CADDY_DIR:-/path/to/caddy-naive-udp-m4}
caddy_bin=${M5_CADDY_BIN:-$forwardproxy_dir/build/m4-caddy}
go_bin=${GO_BIN:-/path/to/naive-m4/go1.25.12/bin/go}
naive_bin="$repo_dir/src/out/Release/naive"
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/naive-m5-g5.XXXXXX")
caddy_pid=
naive_pid=
tap_pid=
trust_pid=
echo4_pid=
echo6_pid=
dns_pid=
h3v4_pid=
h3v6_pid=
http_pid=
trust_keychain=${M5_TRUST_KEYCHAIN:-$HOME/Library/Keychains/login.keychain-db}
ca_fingerprint=
ca_certificate=
server_certificate=
server_private_key=
socks_port=

cleanup() {
  for pid in "$trust_pid" "$naive_pid" "$tap_pid" "$caddy_pid" \
    "$echo4_pid" "$echo6_pid" "$dns_pid" "$h3v4_pid" "$h3v6_pid" \
    "$http_pid"; do
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
    fi
  done
  if [ -n "$ca_certificate" ]; then
    security remove-trusted-cert "$ca_certificate" >/dev/null 2>&1 || true
  fi
  if [ -n "$ca_fingerprint" ]; then
    security delete-certificate -Z "$ca_fingerprint" "$trust_keychain" \
      >/dev/null 2>&1 || true
  fi
  if [ "${M5_KEEP_ARTIFACTS:-0}" = 1 ]; then
    printf '%s\n' "M5_G5_ARTIFACTS=$tmp_dir" >&2
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
  attempts=${4:-240}
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

reserve_tcp_port() {
  python3 - <<'PY'
import socket

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.bind(("127.0.0.1", 0))
print(sock.getsockname()[1])
sock.close()
PY
}

reserve_udp_port() {
  python3 - <<'PY'
import socket

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("127.0.0.1", 0))
print(sock.getsockname()[1])
sock.close()
PY
}

start_tap() {
  label=$1
  output=$2
  python3 -u "$script_dir/udp_size_tap.py" --bind-port "$tap_port" \
    --upstream-port "$proxy_port" --output "$output" \
    >"$tmp_dir/tap-$label.log" 2>&1 &
  tap_pid=$!
  wait_for_log 'M5_G5_SIZE_TAP_READY' "$tmp_dir/tap-$label.log" \
    "$tap_pid" 100 || fail_with_logs "UDP size tap did not become ready"
}

stop_tap() {
  kill "$tap_pid"
  wait "$tap_pid" 2>/dev/null || true
  tap_pid=
}

start_naive() {
  label=$1
  socks_port=$(reserve_tcp_port)
  naive_log="$tmp_dir/naive-$label.log"
  net_log="$tmp_dir/netlog-$label.json"
  "$naive_bin" --listen="socks://127.0.0.1:$socks_port" \
    --proxy="quic://m5-user:m5-pass@m5-proxy.localhost:$tap_port" \
    --host-resolver-rules='MAP m5-proxy.localhost 127.0.0.1' --log \
    --log-net-log="$net_log" >"$naive_log" 2>&1 &
  naive_pid=$!
  wait_for_log 'Listening on socks://127.0.0.1:' "$naive_log" "$naive_pid" 240 || \
    fail_with_logs "production naive did not become ready: $label"
}

stop_naive() {
  kill "$naive_pid"
  wait "$naive_pid" 2>/dev/null || true
  naive_pid=
}

install_temporary_trust() {
  security add-trusted-cert -r trustRoot -p ssl -k "$trust_keychain" \
    "$ca_certificate" &
  trust_pid=$!
  printf '%s\n' M5_G5_TRUST_CONFIRMATION_PENDING >&2
  timeout=${M5_TRUST_CONFIRM_TIMEOUT_SECONDS:-180}
  elapsed=0
  while kill -0 "$trust_pid" 2>/dev/null; do
    if [ "$elapsed" -ge "$timeout" ]; then
      kill "$trust_pid" 2>/dev/null || true
      wait "$trust_pid" 2>/dev/null || true
      trust_pid=
      return 1
    fi
    sleep 1
    elapsed=$((elapsed + 1))
  done
  wait "$trust_pid"
  result=$?
  trust_pid=
  return "$result"
}

baseline_rows() {
  rows=$(wc -l <"$baseline" | tr -d ' ')
  [ "$rows" -gt 0 ] || fail_with_logs "traffic baseline is empty"
  printf '%s' "$rows"
}

record_window() {
  category=$1
  start=$2
  end=$3
  [ "$end" -gt "$start" ] || fail_with_logs "empty baseline window: $category"
  printf '%s rows=%s-%s\n' "$category" "$start" "$end" \
    >>"$tmp_dir/baseline-windows.log"
}

expected_forwardproxy=${M5_EXPECTED_FORWARDPROXY:-8f044e278c70d7479c644eb0ebfffc6bb4b7b3c7}
expected_caddy=${M5_EXPECTED_CADDY:-cce894a8a0e987eb1722cf99729499bdaba6c38d}
expected_client=${M5_EXPECTED_CLIENT:-333b7cb253}
test "$(git -C "$forwardproxy_dir" rev-parse "$expected_forwardproxy")" = \
  "$expected_forwardproxy"
unexpected_server_changes=$(git -C "$forwardproxy_dir" diff --name-only \
  "$expected_forwardproxy"..HEAD | sed '/^tests\/m5\//d')
test -z "$unexpected_server_changes"
test "$(git -C "$caddy_dir" rev-parse HEAD)" = "$expected_caddy"
git -C "$repo_dir" diff --quiet "$expected_client"..HEAD -- src/net
git -C "$repo_dir" diff --quiet -- src/net
ninja -C "$repo_dir/src/out/Release" naive >/dev/null
test -x "$naive_bin"
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
echo_port=$(topology_value echo_ipv4)
dns_port=$(topology_value dns)
h3_port=$(topology_value http3)
tap_port=$(reserve_udp_port)
http_port=$(reserve_tcp_port)

ca_private_key="$tmp_dir/m5-ca.key"
ca_certificate="$tmp_dir/m5-ca.pem"
server_private_key="$tmp_dir/m5-server.key"
server_request="$tmp_dir/m5-server.csr"
server_certificate="$tmp_dir/m5-server.pem"
server_extension="$tmp_dir/m5-server.ext"
h3_private_key="$tmp_dir/m5-h3.key"
h3_request="$tmp_dir/m5-h3.csr"
h3_certificate="$tmp_dir/m5-h3.pem"
h3_extension="$tmp_dir/m5-h3.ext"

openssl req -x509 -newkey rsa:2048 -nodes -days 1 -sha256 \
  -subj '/CN=Naive M5 Test Root' \
  -addext 'basicConstraints=critical,CA:TRUE' \
  -addext 'keyUsage=critical,keyCertSign,cRLSign' \
  -keyout "$ca_private_key" -out "$ca_certificate" >/dev/null 2>&1
ca_fingerprint=$(openssl x509 -in "$ca_certificate" -noout \
  -fingerprint -sha1 | sed 's/^.*=//; s/://g')
openssl req -newkey rsa:2048 -nodes -sha256 -subj '/CN=m5-proxy.localhost' \
  -keyout "$server_private_key" -out "$server_request" >/dev/null 2>&1
printf '%s\n' 'subjectAltName=DNS:m5-proxy.localhost' \
  'basicConstraints=critical,CA:FALSE' \
  'keyUsage=critical,digitalSignature,keyEncipherment' \
  'extendedKeyUsage=serverAuth' >"$server_extension"
openssl x509 -req -in "$server_request" -CA "$ca_certificate" \
  -CAkey "$ca_private_key" -CAcreateserial -days 1 -sha256 \
  -extfile "$server_extension" -out "$server_certificate" >/dev/null 2>&1
openssl req -newkey rsa:2048 -nodes -sha256 -subj '/CN=m5-h3.localhost' \
  -keyout "$h3_private_key" -out "$h3_request" >/dev/null 2>&1
printf '%s\n' 'subjectAltName=DNS:m5-h3.localhost' \
  'basicConstraints=critical,CA:FALSE' \
  'keyUsage=critical,digitalSignature,keyEncipherment' \
  'extendedKeyUsage=serverAuth' >"$h3_extension"
openssl x509 -req -in "$h3_request" -CA "$ca_certificate" \
  -CAkey "$ca_private_key" -CAserial "$tmp_dir/m5-ca.srl" -days 1 -sha256 \
  -extfile "$h3_extension" -out "$h3_certificate" >/dev/null 2>&1

(
  cd "$script_dir"
  "$go_bin" build -trimpath -o "$tmp_dir/h3-origin" ./cmd/h3-origin
  "$go_bin" build -trimpath -o "$tmp_dir/socks-h3-probe" ./cmd/socks-h3-probe
)

python3 -u "$repo_dir/tests/masque_udp_echo.py" --host=127.0.0.1 \
  --port="$echo_port" >"$tmp_dir/echo4.log" 2>&1 &
echo4_pid=$!
python3 -u "$repo_dir/tests/masque_udp_echo.py" --host=::1 \
  --port="$echo_port" >"$tmp_dir/echo6.log" 2>&1 &
echo6_pid=$!
python3 -u "$repo_dir/tests/masque_udp_dns.py" --host=127.0.0.1 \
  --port="$dns_port" >"$tmp_dir/dns.log" 2>&1 &
dns_pid=$!
"$tmp_dir/h3-origin" --bind="127.0.0.1:$h3_port" \
  --cert="$h3_certificate" --key="$h3_private_key" >"$tmp_dir/h3v4.log" 2>&1 &
h3v4_pid=$!
"$tmp_dir/h3-origin" --bind="[::1]:$h3_port" \
  --cert="$h3_certificate" --key="$h3_private_key" >"$tmp_dir/h3v6.log" 2>&1 &
h3v6_pid=$!
mkdir "$tmp_dir/http"
printf '%s\n' m5-production-tcp-ok >"$tmp_dir/http/index.html"
python3 -u -m http.server "$http_port" --bind 127.0.0.1 \
  --directory "$tmp_dir/http" >"$tmp_dir/http.log" 2>&1 &
http_pid=$!
wait_for_log '^READY ' "$tmp_dir/echo4.log" "$echo4_pid" 100 || \
  fail_with_logs "IPv4 echo fixture did not become ready"
wait_for_log '^READY ' "$tmp_dir/echo6.log" "$echo6_pid" 100 || \
  fail_with_logs "IPv6 echo fixture did not become ready"
wait_for_log '^READY ' "$tmp_dir/dns.log" "$dns_pid" 100 || \
  fail_with_logs "DNS fixture did not become ready"
wait_for_log 'M5_G2_H3_ORIGIN_READY' "$tmp_dir/h3v4.log" "$h3v4_pid" 200 || \
  fail_with_logs "IPv4 HTTP/3 fixture did not become ready"
wait_for_log 'M5_G2_H3_ORIGIN_READY' "$tmp_dir/h3v6.log" "$h3v6_pid" 200 || \
  fail_with_logs "IPv6 HTTP/3 fixture did not become ready"

M5_PROXY_PORT="$proxy_port" M5_SERVER_CERT="$server_certificate" \
M5_SERVER_KEY="$server_private_key" M5_ACCESS_LOG="$tmp_dir/access.log" \
XDG_DATA_HOME="$tmp_dir/caddy-data" XDG_CONFIG_HOME="$tmp_dir/caddy-config" \
  "$caddy_bin" run --config "$forwardproxy_dir/tests/m5/Caddyfile-trusted" \
    --adapter caddyfile >"$tmp_dir/caddy.log" 2>&1 &
caddy_pid=$!
wait_for_log 'server running' "$tmp_dir/caddy.log" "$caddy_pid" 240 || \
  fail_with_logs "trusted production Caddy did not become ready"

start_tap negative "$tmp_dir/negative-shape.csv"
start_naive negative
python3 "$script_dir/g3_matrix.py" --mode target-failure \
  --socks-port "$socks_port" --target-port "$echo_port" \
  --marker M5_G5_UNTRUSTED_CERT_REJECTED_OK
if grep -q '"status":200' "$tmp_dir/access.log" 2>/dev/null; then
  fail_with_logs "untrusted production client reached CONNECT-UDP success"
fi
stop_naive
stop_tap

if [ "${M5_G5_STOP_AFTER_NEGATIVE:-0}" = 1 ]; then
  echo M5_G5_NEGATIVE_ONLY_OK
  exit 0
fi

install_temporary_trust || \
  fail_with_logs "temporary macOS trust confirmation did not complete"
security verify-cert -c "$server_certificate" -p ssl \
  -s m5-proxy.localhost >/dev/null

baseline="$tmp_dir/no-padding-baseline.csv"
start_tap positive "$baseline"
start_naive positive

start=$(baseline_rows)
python3 "$repo_dir/tests/socks5_udp_m3.py" --host 127.0.0.1 \
  --port "$socks_port" --target-host 127.0.0.1 --target-port "$echo_port" \
  --payload m5-g5-production-echo --marker M5_G5_PRODUCTION_ECHO_OK
end=$(baseline_rows)
record_window echo "$start" "$end"

start=$(baseline_rows)
python3 "$repo_dir/tests/socks5_udp_m3.py" --host 127.0.0.1 \
  --port "$socks_port" --mode dns --dns-port "$dns_port"
end=$(baseline_rows)
record_window dns "$start" "$end"

start=$(baseline_rows)
"$tmp_dir/socks-h3-probe" --socks="127.0.0.1:$socks_port" \
  --target-host=m5-h3.localhost --target-port="$h3_port" --force-domain \
  --server-name=m5-h3.localhost --ca-cert="$ca_certificate" --timeout=12s
end=$(baseline_rows)
record_window http3 "$start" "$end"

tcp_response=$(curl --silent --show-error --max-time 10 \
  --proxy "socks5h://127.0.0.1:$socks_port" \
  "http://127.0.0.1:$http_port/")
[ "$tcp_response" = m5-production-tcp-ok ] || \
  fail_with_logs "production TCP SOCKS regression failed"
echo M5_G5_PRODUCTION_TCP_OK

python3 "$script_dir/g4_matrix.py" --mode idle \
  --socks-port "$socks_port" --target-port "$echo_port" --pause-seconds=125 \
  --before-payload=m5-g4-server-idle-before-0031 \
  --after-payload=m5-g4-server-idle-after-0032 \
  --marker=M5_G4_SERVER_IDLE_RECONNECT_OK
grep -q 'idle_expired' "$tmp_dir/caddy.log" || \
  fail_with_logs "production server idle expiry was not observed"

sleep 0.5
grep -q 'QUIC_PROXY_DATAGRAM_CLIENT_SOCKET' "$net_log" || \
  fail_with_logs "production NetLog lacks QUIC proxy datagram source"
grep -q 'CONNECT-UDP \[redacted\] HTTP/3' "$net_log" || \
  fail_with_logs "production NetLog lacks redacted CONNECT-UDP evidence"
grep -q 'connect-udp association event' "$tmp_dir/caddy.log" || \
  fail_with_logs "production server lacks CONNECT-UDP lifecycle evidence"

python3 - "$baseline" "$tmp_dir/baseline-windows.log" <<'PY'
import csv
import sys

baseline, windows = sys.argv[1:]
expected = [
    "relative_time_us",
    "direction",
    "packet_size",
    "connection_age_us",
]
with open(baseline, newline="", encoding="ascii") as source:
    reader = csv.DictReader(source)
    assert reader.fieldnames == expected, reader.fieldnames
    rows = list(reader)
assert rows
directions = {row["direction"] for row in rows}
assert directions == {"client_to_server", "server_to_client"}, directions
previous = -1
for row in rows:
    relative = int(row["relative_time_us"])
    age = int(row["connection_age_us"])
    size = int(row["packet_size"])
    assert relative >= previous and age >= 0 and 0 < size <= 65535
    previous = relative
with open(windows, encoding="ascii") as source:
    entries = [line.split()[0] for line in source if line.strip()]
assert entries == ["echo", "dns", "http3"], entries
PY

if grep -E 'm5-g5-production-echo|m5-h3\.localhost|\.well-known/masque/udp|m5-pass|bTUtdXNlcjptNS1wYXNz' \
  "$tmp_dir/caddy.log" "$tmp_dir/access.log" "$naive_log" "$net_log" \
  >/dev/null 2>&1; then
  fail_with_logs "private production UDP data appeared in retained evidence"
fi

stop_naive
stop_tap
security remove-trusted-cert "$ca_certificate"
security delete-certificate -Z "$ca_fingerprint" "$trust_keychain" \
  >/dev/null 2>&1 || true
ca_fingerprint=
if security verify-cert -c "$server_certificate" -p ssl \
  -s m5-proxy.localhost >/dev/null 2>&1; then
  fail_with_logs "temporary production root remained trusted"
fi

echo M5_G4_IDLE_RECONNECT_OK
echo M5_G5_DEFAULT_CERT_VERIFIER_OK
echo M5_G5_PRODUCTION_BINARY_OK
echo M5_G5_H3_DATAGRAM_EVIDENCE_OK
echo M5_G5_NO_PADDING_BASELINE_OK
