#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
backend_test="$repo_dir/src/out/Release/naive_connect_udp_backend_test"
runner="$repo_dir/src/out/Release/naive_socks5_udp_runner"
m3_runner="$repo_dir/src/out/Release/naive_socks5_udp_m3_runner"
masque_server="$repo_dir/src/out/Release/naive_masque_server"
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/naive-m3.XXXXXX")
runner_pid=""
m3_runner_pid=""
server_pid=""
echo_pid=""

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
  rm -r "$tmp_dir"
}
trap cleanup EXIT INT TERM

ninja -C "$repo_dir/src/out/Release" naive_connect_udp_backend_test \
  naive_socks5_udp_runner naive_socks5_udp_m3_runner naive_masque_server naive
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

server_port=19665
echo_port=19005
python3 -u "$script_dir/masque_udp_echo.py" --host=127.0.0.1 \
  --port="$echo_port" >"$tmp_dir/echo.log" 2>&1 &
echo_pid=$!

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
      grep -q '^READY ' "$tmp_dir/echo.log" 2>/dev/null && break
    kill -0 "$server_pid" 2>/dev/null || {
      cat "$server_log" "$tmp_dir/echo.log"
      exit 1
    }
    sleep 0.05
    i=$((i + 1))
  done
  grep -q '^READY ' "$server_log"

  if [ -n "$user$pass" ]; then
    "$m3_runner" --proxy-host=::1 --proxy-port="$server_port" \
      --proxy-user="$user" --proxy-pass="$pass" --run-for-ms=3000 \
      >"$runner_log" 2>&1 &
  else
    "$m3_runner" --proxy-host=::1 --proxy-port="$server_port" \
      --run-for-ms=3000 >"$runner_log" 2>&1 &
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
  wait "$m3_runner_pid"
  m3_runner_pid=""
  grep -q 'M3_PRODUCTION_FACTORY_ELIGIBILITY_OK' "$runner_log"
  grep -q 'M3_RUNNER_PROXY_DESTROYED_BEFORE_CONTEXT' "$runner_log"
  grep -q 'protocol=connect-udp' "$server_log"
  grep -q 'capsule_protocol=?1' "$server_log"
  if [ -n "$user$pass" ]; then
    grep -q 'AUTH_DECISION accepted' "$server_log"
  fi
  kill "$server_pid" 2>/dev/null || true
  wait "$server_pid" 2>/dev/null || true
  server_pid=""
}

run_real_case IPV4
run_real_case AUTH m3-user m3-pass

kill "$echo_pid" 2>/dev/null || true
wait "$echo_pid" 2>/dev/null || true
echo_pid=""

echo M3_G3_PRODUCTION_WIRING_OK

# G4-G5 append their broader interoperability and lifecycle cases here. This
# cumulative entry point has remained the M3 verification surface since G0.
echo M3_G0_TEST_SKELETON_OK
