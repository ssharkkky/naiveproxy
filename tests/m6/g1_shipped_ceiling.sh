#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../.." && pwd)
forwardproxy_dir=${M6_FORWARDPROXY_DIR:-/Users/stoneshi/Documents/naive-forwardproxy-m4}
caddy_dir=${M6_CADDY_DIR:-/Users/stoneshi/Documents/caddy-naive-udp-m4}
caddy_bin=${M6_CADDY_BIN:-$forwardproxy_dir/build/m4-caddy}
naive_bin="$repo_dir/src/out/Release/naive"
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/naive-m6-g1b2.XXXXXX")
trust_keychain=${M6_TRUST_KEYCHAIN:-$HOME/Library/Keychains/login.keychain-db}
caddy_pid=
naive_pid=
trust_pid=
echo4_pid=
echo6_pid=
ca_fingerprint=
ca_certificate=
server_certificate=
socks_port=

cleanup() {
  for pid in "$trust_pid" "$naive_pid" "$caddy_pid" "$echo4_pid" \
    "$echo6_pid"; do
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
  if [ "${M6_KEEP_ARTIFACTS:-0}" = 1 ]; then
    printf '%s\n' "M6_G1B2_ARTIFACTS=$tmp_dir" >&2
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

reserve_tcp_port() {
  python3 - <<'PY'
import socket

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.bind(("127.0.0.1", 0))
print(sock.getsockname()[1])
sock.close()
PY
}

start_naive() {
  label=$1
  socks_port=$(reserve_tcp_port)
  naive_log="$tmp_dir/naive-$label.log"
  net_log="$tmp_dir/netlog-$label.json"
  "$naive_bin" --listen="socks://127.0.0.1:$socks_port" \
    --proxy="quic://m5-user:m5-pass@m5-proxy.localhost:$proxy_port" \
    --host-resolver-rules='MAP m5-proxy.localhost 127.0.0.1' --log \
    --log-net-log="$net_log" >"$naive_log" 2>&1 &
  naive_pid=$!
  wait_for_log 'Listening on socks://127.0.0.1:' "$naive_log" \
    "$naive_pid" 240 || fail_with_logs "shipped naive did not become ready"
}

stop_naive() {
  kill "$naive_pid" 2>/dev/null || true
  wait "$naive_pid" 2>/dev/null || true
  naive_pid=
}

install_temporary_trust() {
  security add-trusted-cert -r trustRoot -p ssl -k "$trust_keychain" \
    "$ca_certificate" &
  trust_pid=$!
  printf '%s\n' M6_G1B2_TRUST_CONFIRMATION_PENDING >&2
  timeout=${M6_TRUST_CONFIRM_TIMEOUT_SECONDS:-180}
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

test "$(uname -s)" = Darwin
command -v security >/dev/null
command -v openssl >/dev/null

expected_client=17c717793c7e5634bf0d5dfa823c9839eb45e832
expected_forwardproxy=f14924cdedc93c28a2b92c8120538ea5beee28fb
expected_caddy=dd9a89c11194dcb806d845233995ef040f096464
unexpected_client_changes=$(git -C "$repo_dir" diff --name-only \
  "$expected_client"..HEAD -- src/net | \
  sed -e '/^src\/net\/tools\/naive\/naive_connect_udp_backend_test_bin\.cc$/d' \
      -e '/^src\/net\/tools\/naive\/naive_socks5_udp_fuzz_test_bin\.cc$/d' \
      -e '/^src\/net\/BUILD\.gn$/d')
test -z "$unexpected_client_changes"
git -C "$repo_dir" diff --quiet -- src/net
unexpected_server_changes=$(git -C "$forwardproxy_dir" diff --name-only \
  "$expected_forwardproxy"..HEAD | \
  sed -e '/^tests\/m5\//d' -e '/^tests\/m6\//d' \
      -e '/^native_udp_fuzz_test\.go$/d' -e '/^M4_TOOLCHAIN\.lock$/d')
test -z "$unexpected_server_changes"
test "$(git -C "$caddy_dir" rev-parse HEAD)" = "$expected_caddy"
git -C "$forwardproxy_dir" diff --quiet
git -C "$caddy_dir" diff --quiet
ninja -C "$repo_dir/src/out/Release" naive >/dev/null
test -x "$naive_bin"
test -x "$caddy_bin"
export PYTHONDONTWRITEBYTECODE=1

topology=$(python3 "$repo_dir/tests/m5/topology.py")
proxy_port=$(printf '%s\n' "$topology" | python3 -c \
  'import json, sys; print(json.load(sys.stdin)["proxy"])')
echo_port=$(printf '%s\n' "$topology" | python3 -c \
  'import json, sys; print(json.load(sys.stdin)["echo_ipv4"])')

ca_private_key="$tmp_dir/m6-g1-ca.key"
ca_certificate="$tmp_dir/m6-g1-ca.pem"
server_private_key="$tmp_dir/m6-g1-server.key"
server_request="$tmp_dir/m6-g1-server.csr"
server_certificate="$tmp_dir/m6-g1-server.pem"
server_extension="$tmp_dir/m6-g1-server.ext"

openssl req -x509 -newkey rsa:2048 -nodes -days 1 -sha256 \
  -subj '/CN=Naive M6 G1 Test Root' \
  -addext 'basicConstraints=critical,CA:TRUE' \
  -addext 'keyUsage=critical,keyCertSign,cRLSign' \
  -keyout "$ca_private_key" -out "$ca_certificate" >/dev/null 2>&1
ca_fingerprint=$(openssl x509 -in "$ca_certificate" -noout \
  -fingerprint -sha256 | sed 's/^.*=//; s/://g')
openssl req -newkey rsa:2048 -nodes -sha256 -subj '/CN=m5-proxy.localhost' \
  -keyout "$server_private_key" -out "$server_request" >/dev/null 2>&1
printf '%s\n' 'subjectAltName=DNS:m5-proxy.localhost' \
  'basicConstraints=critical,CA:FALSE' \
  'keyUsage=critical,digitalSignature,keyEncipherment' \
  'extendedKeyUsage=serverAuth' >"$server_extension"
openssl x509 -req -in "$server_request" -CA "$ca_certificate" \
  -CAkey "$ca_private_key" -CAcreateserial -days 1 -sha256 \
  -extfile "$server_extension" -out "$server_certificate" >/dev/null 2>&1

python3 -u "$repo_dir/tests/masque_udp_echo.py" --host=127.0.0.1 \
  --port="$echo_port" >"$tmp_dir/echo4.log" 2>&1 &
echo4_pid=$!
python3 -u "$repo_dir/tests/masque_udp_echo.py" --host=::1 \
  --port="$echo_port" >"$tmp_dir/echo6.log" 2>&1 &
echo6_pid=$!
wait_for_log '^READY ' "$tmp_dir/echo4.log" "$echo4_pid" 100 || \
  fail_with_logs "IPv4 echo fixture did not become ready"
wait_for_log '^READY ' "$tmp_dir/echo6.log" "$echo6_pid" 100 || \
  fail_with_logs "IPv6 echo fixture did not become ready"

M5_PROXY_PORT="$proxy_port" M5_SERVER_CERT="$server_certificate" \
M5_SERVER_KEY="$server_private_key" M5_ACCESS_LOG="$tmp_dir/access.log" \
XDG_DATA_HOME="$tmp_dir/caddy-data" XDG_CONFIG_HOME="$tmp_dir/caddy-config" \
  "$caddy_bin" run --config "$forwardproxy_dir/tests/m5/Caddyfile-trusted" \
    --adapter caddyfile >"$tmp_dir/caddy.log" 2>&1 &
caddy_pid=$!
wait_for_log 'server running' "$tmp_dir/caddy.log" "$caddy_pid" 240 || \
  fail_with_logs "production Caddy did not become ready"

start_naive negative
python3 "$repo_dir/tests/m5/g3_matrix.py" --mode target-failure \
  --socks-port "$socks_port" --target-port "$echo_port" \
  --marker M6_G1B2_UNTRUSTED_CERT_REJECTED_OK
if grep -q '"status":200' "$tmp_dir/access.log" 2>/dev/null; then
  fail_with_logs "untrusted shipped client reached CONNECT-UDP success"
fi
stop_naive

if [ "${M6_G1B2_STOP_AFTER_NEGATIVE:-0}" = 1 ]; then
  echo M6_G1B2_NEGATIVE_ONLY_OK
  exit 0
fi

install_temporary_trust || \
  fail_with_logs "temporary macOS trust confirmation did not complete"
security verify-cert -c "$server_certificate" -p ssl \
  -s m5-proxy.localhost >/dev/null

start_naive positive
python3 "$script_dir/payload_probe.py" --socks-port "$socks_port" \
  --echo-port "$echo_port" | tee "$tmp_dir/probe.log"
grep -q '^M6_G1_LIVE_PRODUCT_CEILING_OK bytes=1314$' "$tmp_dir/probe.log" || \
  fail_with_logs "shipped client ceiling did not match 1314 bytes"
grep -q 'QUIC_PROXY_DATAGRAM_CLIENT_SOCKET' "$net_log" || \
  fail_with_logs "shipped client NetLog lacks QUIC datagram evidence"
grep -q 'CONNECT-UDP \[redacted\] HTTP/3' "$net_log" || \
  fail_with_logs "shipped client NetLog lacks redacted CONNECT-UDP evidence"

if grep -E "\.well-known/masque/udp|localhost/$echo_port|127\.0\.0\.1/$echo_port|\[::1\]/$echo_port|m5-pass|bTUtdXNlcjptNS1wYXNz" \
  "$tmp_dir/caddy.log" "$tmp_dir/access.log" "$naive_log" "$net_log" \
  >/dev/null 2>&1; then
  fail_with_logs "private G1b2 target or credential data appeared in evidence"
fi

stop_naive
security remove-trusted-cert "$ca_certificate"
security delete-certificate -Z "$ca_fingerprint" "$trust_keychain" \
  >/dev/null 2>&1 || true
if security find-certificate -a -Z "$trust_keychain" 2>/dev/null | \
    grep -q "SHA-256 hash: $ca_fingerprint"; then
  fail_with_logs "temporary G1b2 root certificate remained in keychain"
fi
ca_fingerprint=
if security verify-cert -c "$server_certificate" -p ssl \
  -s m5-proxy.localhost >/dev/null 2>&1; then
  fail_with_logs "temporary G1b2 root remained trusted"
fi

echo M6_G1B2_SHIPPED_CEILING_OK bytes=1314
echo M6_G1B2_DEFAULT_VERIFIER_OK
echo M6_G1B2_TRUST_CLEANUP_OK
