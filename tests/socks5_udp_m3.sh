#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
CCACHE_DIR=${CCACHE_DIR:-"$repo_dir/src/.host_tool_cache"}
export CCACHE_DIR
backend_test="$repo_dir/src/out/Release/naive_connect_udp_backend_test"
association_test="$repo_dir/src/out/Release/naive_socks5_udp_association_test"
runner="$repo_dir/src/out/Release/naive_socks5_udp_runner"
m3_runner="$repo_dir/src/out/Release/naive_socks5_udp_m3_runner"
masque_server="$repo_dir/src/out/Release/naive_masque_server"
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/naive-m3.XXXXXX")
runner_pid=""
m3_runner_pid=""
server_pid=""
echo_pid=""
echo6_pid=""
dns_pid=""

cleanup() {
  if [ -n "$runner_pid" ]; then
    kill "$runner_pid" 2>/dev/null || true
    wait "$runner_pid" 2>/dev/null || true
  fi
  if [ -n "$m3_runner_pid" ]; then
    kill "$m3_runner_pid" 2>/dev/null || true
    wait "$m3_runner_pid" 2>/dev/null || true
  fi
  if [ -n "$server_pid" ]; then
    kill "$server_pid" 2>/dev/null || true
    wait "$server_pid" 2>/dev/null || true
  fi
  if [ -n "$echo_pid" ]; then
    kill "$echo_pid" 2>/dev/null || true
    wait "$echo_pid" 2>/dev/null || true
  fi
  if [ -n "$echo6_pid" ]; then
    kill "$echo6_pid" 2>/dev/null || true
    wait "$echo6_pid" 2>/dev/null || true
  fi
  if [ -n "$dns_pid" ]; then
    kill "$dns_pid" 2>/dev/null || true
    wait "$dns_pid" 2>/dev/null || true
  fi
  rm -r "$tmp_dir"
}
trap cleanup EXIT INT TERM

ninja -C "$repo_dir/src/out/Release" naive_connect_udp_backend_test \
  naive_socks5_udp_association_test \
  naive_socks5_udp_runner naive_socks5_udp_m3_runner naive_masque_server naive
"$backend_test"
"$association_test"

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

run_rejection_case() {
  scheme=$1
  label=$2
  log="$tmp_dir/reject-$label.log"
  "$runner" --listen-host=127.0.0.1 --proxy-scheme="$scheme" \
    >"$log" 2>&1 &
  runner_pid=$!
  ready=""
  i=0
  while [ "$i" -lt 100 ]; do
    ready=$(grep 'M2_SOCKS5_UDP_READY' "$log" 2>/dev/null || true)
    [ -n "$ready" ] && break
    kill -0 "$runner_pid" 2>/dev/null || {
      cat "$log"
      exit 1
    }
    sleep 0.05
    i=$((i + 1))
  done
  [ -n "$ready" ] || {
    cat "$log"
    exit 1
  }
  port=$(printf '%s\n' "$ready" | sed -n 's/.* port=\([0-9][0-9]*\).*/\1/p')
  python3 "$script_dir/socks5_udp_m2.py" --host 127.0.0.1 \
    --port "$port" --mode rejection
  kill "$runner_pid" 2>/dev/null || true
  wait "$runner_pid" 2>/dev/null || true
  runner_pid=""
  echo "M3_G3_${label}_REJECTION_OK"
}

run_rejection_case direct DIRECT
run_rejection_case https H2
run_rejection_case mixed MIXED_CHAIN
run_rejection_case quic-no-backend NO_BACKEND

openssl req -x509 -newkey rsa:2048 \
  -keyout "$tmp_dir/key.pem" -out "$tmp_dir/cert.pem" \
  -sha256 -days 2 -nodes -subj '/CN=localhost' \
  -addext 'subjectAltName=DNS:localhost,IP:127.0.0.1,IP:::1' \
  >/dev/null 2>&1
openssl pkcs8 -topk8 -nocrypt -in "$tmp_dir/key.pem" \
  -outform DER -out "$tmp_dir/key.pk8"

server_port=$(python3 -c 'import socket; s=socket.socket(socket.AF_INET6, socket.SOCK_DGRAM); s.bind(("::1", 0)); print(s.getsockname()[1]); s.close()')
echo_port=$(python3 -c 'import socket
while True:
 s4=socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s4.bind(("127.0.0.1", 0)); p=s4.getsockname()[1]
 s6=socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
 try: s6.bind(("::1", p)); print(p); s4.close(); s6.close(); break
 except OSError: s4.close(); s6.close()')
dns_port=$(python3 -c 'import socket; s=socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')
python3 -u "$script_dir/masque_udp_echo.py" --host=127.0.0.1 \
  --port="$echo_port" >"$tmp_dir/echo.log" 2>&1 &
echo_pid=$!
python3 -u "$script_dir/masque_udp_echo.py" --host=::1 \
  --port="$echo_port" >"$tmp_dir/echo6.log" 2>&1 &
echo6_pid=$!
python3 -u "$script_dir/masque_udp_dns.py" --host=127.0.0.1 \
  --port="$dns_port" >"$tmp_dir/dns.log" 2>&1 &
dns_pid=$!

run_real_case() {
  label=$1
  user=${2-}
  pass=${3-}
  server_log="$tmp_dir/server-$label.log"
  runner_log="$tmp_dir/runner-$label.log"
  if [ -n "$user$pass" ]; then
    "$masque_server" --port="$server_port" \
      --server_authority="[::1]:$server_port" --masque_mode=open \
      --basic_user="$user" --basic_pass="$pass" \
      --certificate_file="$tmp_dir/cert.pem" --key_file="$tmp_dir/key.pk8" \
      >"$server_log" 2>&1 &
  else
    "$masque_server" --port="$server_port" \
      --server_authority="[::1]:$server_port" --masque_mode=open \
      --certificate_file="$tmp_dir/cert.pem" --key_file="$tmp_dir/key.pk8" \
      >"$server_log" 2>&1 &
  fi
  server_pid=$!
  i=0
  while [ "$i" -lt 100 ]; do
    grep -q '^READY ' "$server_log" 2>/dev/null && \
      grep -q '^READY ' "$tmp_dir/echo.log" 2>/dev/null && \
      grep -q '^READY ' "$tmp_dir/echo6.log" 2>/dev/null && \
      grep -q '^READY ' "$tmp_dir/dns.log" 2>/dev/null && break
    kill -0 "$server_pid" 2>/dev/null || {
      cat "$server_log" "$tmp_dir/echo.log"
      exit 1
    }
    sleep 0.05
    i=$((i + 1))
  done
  grep -q '^READY ' "$server_log"
  grep -q '^READY ' "$tmp_dir/echo.log"
  grep -q '^READY ' "$tmp_dir/echo6.log"
  grep -q '^READY ' "$tmp_dir/dns.log"

  if [ -n "$user$pass" ]; then
    "$m3_runner" --proxy-host=::1 --proxy-port="$server_port" \
      --proxy-user="$user" --proxy-pass="$pass" --run-for-ms=8000 \
      >"$runner_log" 2>&1 &
  else
    "$m3_runner" --proxy-host=::1 --proxy-port="$server_port" \
      --run-for-ms=15000 --log-net-log="$tmp_dir/m3-netlog.json" \
      --net-log-everything >"$runner_log" 2>&1 &
  fi
  m3_runner_pid=$!
  ready=""
  i=0
  while [ "$i" -lt 100 ]; do
    ready=$(grep 'M3_SOCKS5_UDP_READY' "$runner_log" 2>/dev/null || true)
    [ -n "$ready" ] && break
    kill -0 "$m3_runner_pid" 2>/dev/null || {
      cat "$runner_log" "$server_log"
      exit 1
    }
    sleep 0.05
    i=$((i + 1))
  done
  [ -n "$ready" ]
  port=$(printf '%s\n' "$ready" | sed -n 's/.* port=\([0-9][0-9]*\).*/\1/p')
  python3 "$script_dir/socks5_udp_m3.py" --host 127.0.0.1 --port "$port" \
    --target-host 127.0.0.1 --target-port "$echo_port" \
    --payload "m3-g3-$label" --marker "M3_G3_${label}_ECHO_OK"
  if [ -z "$user$pass" ]; then
    echo M3_G4_IPV4_OK
    python3 "$script_dir/socks5_udp_m3.py" --host 127.0.0.1 \
      --port "$port" --target-host ::1 --target-port "$echo_port" \
      --payload m3-g4-ipv6 --marker M3_G4_IPV6_OK
    python3 "$script_dir/socks5_udp_m3.py" --host 127.0.0.1 \
      --port "$port" --target-host localhost --target-port "$echo_port" \
      --payload m3-g4-domain --force-domain --marker M3_G4_DOMAIN_OK
    python3 "$script_dir/socks5_udp_m3.py" --host 127.0.0.1 \
      --port "$port" --mode dns --dns-port "$dns_port"
    python3 "$script_dir/socks5_udp_m3.py" --host 127.0.0.1 \
      --port "$port" --mode multi-target --echo-port "$echo_port"
    python3 "$script_dir/socks5_udp_m3.py" --host 127.0.0.1 \
      --port "$port" --mode concurrent --echo-port "$echo_port"
  fi
  wait "$m3_runner_pid"
  m3_runner_pid=""
  grep -q 'M3_PRODUCTION_FACTORY_ELIGIBILITY_OK' "$runner_log"
  grep -q 'M3_RUNNER_PROXY_DESTROYED_BEFORE_CONTEXT' "$runner_log"
  grep -q 'protocol=connect-udp' "$server_log"
  grep -q 'capsule_protocol=?1' "$server_log"
  if [ -n "$user$pass" ]; then
    grep -q 'AUTH_DECISION accepted' "$server_log"
    echo M3_G4_AUTH_OK
  else
    jq -e '
      (.constants.logSourceType.QUIC_PROXY_DATAGRAM_CLIENT_SOCKET) as $source |
      (.constants.logEventTypes.SOCKET_BYTES_SENT) as $sent |
      any(.events[];
          .source.type == $source and .type == $sent and
          (.params.byte_count // 0) > 0 and
          ((.params | has("bytes")) | not))
    ' "$tmp_dir/m3-netlog.json" >/dev/null
    grep -q 'CONNECT-UDP \[redacted\] HTTP/3' "$tmp_dir/m3-netlog.json"
    if grep -q "well-known/masque/udp/127.0.0.1/$echo_port" \
      "$tmp_dir/m3-netlog.json"; then
      echo 'sensitive UDP destination found in NetLog' >&2
      exit 1
    fi
    if grep -q 'm3-g4-domain' "$tmp_dir/m3-netlog.json"; then
      echo 'sensitive UDP payload found in NetLog' >&2
      exit 1
    fi
    echo M3_G4_NETLOG_REDACTION_OK
  fi
  kill "$server_pid" 2>/dev/null || true
  wait "$server_pid" 2>/dev/null || true
  server_pid=""
}

run_real_case IPV4
run_real_case AUTH m3-user m3-pass

start_g5_server() {
  g5_label=$1
  shift
  server_log="$tmp_dir/server-g5-$g5_label.log"
  "$masque_server" --port="$server_port" \
    --server_authority="[::1]:$server_port" --masque_mode=open \
    --certificate_file="$tmp_dir/cert.pem" --key_file="$tmp_dir/key.pk8" \
    "$@" >"$server_log" 2>&1 &
  server_pid=$!
  i=0
  while [ "$i" -lt 100 ]; do
    grep -q '^READY ' "$server_log" 2>/dev/null && break
    kill -0 "$server_pid" 2>/dev/null || {
      cat "$server_log"
      exit 1
    }
    sleep 0.05
    i=$((i + 1))
  done
  grep -q '^READY ' "$server_log"
}

start_g5_runner() {
  g5_label=$1
  run_for_ms=$2
  shift 2
  runner_log="$tmp_dir/runner-g5-$g5_label.log"
  "$m3_runner" --proxy-host=::1 --proxy-port="$server_port" \
    --run-for-ms="$run_for_ms" "$@" >"$runner_log" 2>&1 &
  m3_runner_pid=$!
  ready=""
  i=0
  while [ "$i" -lt 100 ]; do
    ready=$(grep 'M3_SOCKS5_UDP_READY' "$runner_log" 2>/dev/null || true)
    [ -n "$ready" ] && break
    kill -0 "$m3_runner_pid" 2>/dev/null || {
      cat "$runner_log" "$server_log"
      exit 1
    }
    sleep 0.05
    i=$((i + 1))
  done
  [ -n "$ready" ]
  port=$(printf '%s\n' "$ready" | sed -n 's/.* port=\([0-9][0-9]*\).*/\1/p')
}

finish_g5_case() {
  wait "$m3_runner_pid"
  m3_runner_pid=""
  grep -q 'M3_RUNNER_PROXY_DESTROYED_BEFORE_CONTEXT' "$runner_log"
  kill "$server_pid" 2>/dev/null || true
  wait "$server_pid" 2>/dev/null || true
  server_pid=""
}

start_g5_server session-reconnect
start_g5_runner session-reconnect 4500 \
  --shutdown-session-after-ms=1500 \
  --log-net-log="$tmp_dir/g5-session-netlog.json" --net-log-everything
python3 "$script_dir/socks5_udp_m3.py" --host 127.0.0.1 --port "$port" \
  --mode reconnect --echo-port "$echo_port" --pause-seconds 2.7 \
  --marker M3_G5_SESSION_RECONNECT_OK
finish_g5_case
grep -q '^M3_SESSION_SHUTDOWN_ISSUED$' "$runner_log"
test "$(grep -c 'protocol=connect-udp' "$server_log")" -eq 2
test "$(grep -c 'hex=6d332d6265666f72652d73687574646f776e$' \
  "$tmp_dir/echo.log")" -eq 1
test "$(grep -c 'hex=6d332d61667465722d73687574646f776e$' \
  "$tmp_dir/echo.log")" -eq 1
jq -e '
  (.constants.logEventTypes.NAIVE_CONNECT_UDP_BACKEND_COUNTER) as $counter |
  any(.events[];
      .type == $counter and .params.reason == "target_failure" and
      .params.count == "1" and
      ((.params | keys | sort) ==
       ["association_id", "count", "reason"]))
' "$tmp_dir/g5-session-netlog.json" >/dev/null
echo M3_G5_RECONNECT_OK

start_g5_server idle-reconnect
start_g5_runner idle-reconnect 2200 --target-idle-timeout-ms=200
python3 "$script_dir/socks5_udp_m3.py" --host 127.0.0.1 --port "$port" \
  --mode reconnect --echo-port "$echo_port" --pause-seconds 0.6 \
  --marker M3_G5_IDLE_RECONNECT_OK
finish_g5_case
test "$(grep -c 'protocol=connect-udp' "$server_log")" -eq 2

start_g5_server payload-policy
start_g5_runner payload-policy 3000 \
  --log-net-log="$tmp_dir/g5-policy-netlog.json" --net-log-everything
python3 "$script_dir/socks5_udp_m3.py" --host 127.0.0.1 --port "$port" \
  --mode zero-oversize --echo-port "$echo_port"
python3 "$script_dir/socks5_udp_m3.py" --host 127.0.0.1 --port "$port" \
  --mode hold-until-proxy-close --echo-port "$echo_port"
finish_g5_case
jq -e '
  (.constants.logEventTypes.NAIVE_CONNECT_UDP_BACKEND_COUNTER) as $counter |
  [.events[] |
   select(.type == $counter and .params.reason == "oversize_drop") |
   .params.count] == ["1", "2", "4"]
' "$tmp_dir/g5-policy-netlog.json" >/dev/null

start_g5_server connect-timeout --ignore_connect_requests=true
start_g5_runner connect-timeout 4000 --connect-timeout-ms=200 \
  --log-net-log="$tmp_dir/g5-timeout-netlog.json" --net-log-everything
python3 "$script_dir/socks5_udp_m3.py" --host 127.0.0.1 --port "$port" \
  --mode close-pending-connect --echo-port "$echo_port"
python3 "$script_dir/socks5_udp_m3.py" --host 127.0.0.1 --port "$port" \
  --mode target-failure --echo-port "$echo_port" --pause-seconds 1.3 \
  --marker M3_G5_CONNECT_TIMEOUT_OK
finish_g5_case
test "$(grep -c '^CONNECT_ACTION ignored$' "$server_log")" -ge 3
jq -e '
  (.constants.logEventTypes.NAIVE_CONNECT_UDP_BACKEND_COUNTER) as $counter |
  any(.events[];
      .type == $counter and .params.reason == "connect_timeout")
' "$tmp_dir/g5-timeout-netlog.json" >/dev/null

run_g5_auth_failure() {
  auth_label=$1
  expected_header=$2
  shift 2
  start_g5_server "auth-$auth_label" --basic_user=m3-user --basic_pass=m3-pass
  start_g5_runner "auth-$auth_label" 4000 "$@"
  python3 "$script_dir/socks5_udp_m3.py" --host 127.0.0.1 --port "$port" \
    --mode target-failure --echo-port "$echo_port" --pause-seconds 1.2 \
    --marker "M3_G5_AUTH_${auth_label}_OK"
  finish_g5_case
  test "$(grep -c '^AUTH_DECISION rejected$' "$server_log")" -eq 2
  test "$(grep -c "proxy_authorization=$expected_header" "$server_log")" \
    -eq 2
}

run_g5_auth_failure MISSING absent
run_g5_auth_failure WRONG present --proxy-user=m3-user --proxy-pass=wrong-pass
echo M3_G5_AUTH_FAILURES_OK
echo M3_G5_LIFECYCLE_OK
echo M3_G5_LIMITS_OK

kill "$echo_pid" 2>/dev/null || true
wait "$echo_pid" 2>/dev/null || true
echo_pid=""
kill "$echo6_pid" 2>/dev/null || true
wait "$echo6_pid" 2>/dev/null || true
echo6_pid=""
kill "$dns_pid" 2>/dev/null || true
wait "$dns_pid" 2>/dev/null || true
dns_pid=""

echo M3_G3_PRODUCTION_WIRING_OK

# This cumulative entry point has remained the M3 verification surface since
# G0 and now covers the full G0-G5 contract.
echo M3_G0_TEST_SKELETON_OK
echo M3_NATIVE_UDP_CLIENT_OK
