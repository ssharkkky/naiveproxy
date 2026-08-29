#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../.." && pwd)
forwardproxy_dir=${M6_FORWARDPROXY_DIR:-/path/to/naive-forwardproxy-m4}
caddy_dir=${M6_CADDY_DIR:-/path/to/caddy-naive-udp-m4}
caddy_bin=${M6_CADDY_BIN:-$forwardproxy_dir/build/m4-caddy}
runner_bin="$repo_dir/src/out/Release/naive_socks5_udp_m3_runner"
go_bin=${GO_BIN:-/path/to/naive-m4/go1.25.12/bin/go}
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/naive-m6-g1.XXXXXX")
caddy_pid=
runner_pid=
echo4_pid=
echo6_pid=
shaper_pid=
dns_pid=
h3_pid=
h3v6_pid=
h3_port=0

cleanup() {
  for pid in "$runner_pid" "$caddy_pid" "$echo4_pid" "$echo6_pid" \
    "$shaper_pid" "$dns_pid" "$h3_pid" "$h3v6_pid"; do
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

expected_client=474a1e4b0aeb9c64e6d0083eaddd205c887bf608
expected_forwardproxy=964281a9797efd9a4c953f6273c73e397e777864
expected_caddy=dd9a89c11194dcb806d845233995ef040f096464

unexpected_client_changes=$(git -C "$repo_dir" diff --name-only \
  "$expected_client"..HEAD -- src/net | \
  sed -e '/^src\/net\/tools\/naive\/naive_connect_udp_backend_test_bin\.cc$/d' \
      -e '/^src\/net\/tools\/naive\/naive_socks5_udp_fuzz_test_bin\.cc$/d' \
      -e '/^src\/net\/BUILD\.gn$/d')
test -z "$unexpected_client_changes"
unexpected_worktree_changes=$(git -C "$repo_dir" diff --name-only -- src/net | \
  sed -e '/^src\/net\/tools\/naive\/naive_connect_udp_backend_test_bin\.cc$/d' \
      -e '/^src\/net\/tools\/naive\/naive_socks5_udp_fuzz_test_bin\.cc$/d' \
      -e '/^src\/net\/BUILD\.gn$/d')
test -z "$unexpected_worktree_changes"
unexpected_server_changes=$(git -C "$forwardproxy_dir" diff --name-only \
  "$expected_forwardproxy"..HEAD | \
  sed -e '/^tests\/m5\//d' -e '/^tests\/m6\//d' \
      -e '/^native_udp_fuzz_test\.go$/d' -e '/^M4_TOOLCHAIN\.lock$/d')
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
stress_duration=${M6_G3_DURATION_SECONDS:-60}
server_port=$proxy_port
if [ "$probe_mode" = pmtu ] || [ "$probe_mode" = impairment ]; then
  server_port=$(printf '%s\n' "$topology" | python3 -c \
    'import json, sys; print(json.load(sys.stdin)["echo_ipv6"])')
  test "$server_port" != "$proxy_port"
  printf '0\n' >"$tmp_dir/outer-ceiling"
  if [ "$probe_mode" = impairment ]; then
    impairment_profile=${M6_G2_PROFILE:?M6_G2_PROFILE is required}
    printf '%s\n' "$impairment_profile" >"$tmp_dir/profile-control"
    python3 -u "$script_dir/udp_shaper.py" --listen-port "$proxy_port" \
      --server-port "$server_port" --ceiling-file "$tmp_dir/outer-ceiling" \
      --profiles-file "$script_dir/network_profiles.json" \
      --profile="$impairment_profile" \
      --profile-control-file="$tmp_dir/profile-control" \
      >"$tmp_dir/shaper.log" 2>&1 &
  else
    python3 -u "$script_dir/udp_shaper.py" --listen-port "$proxy_port" \
      --server-port "$server_port" --ceiling-file "$tmp_dir/outer-ceiling" \
      >"$tmp_dir/shaper.log" 2>&1 &
  fi
  shaper_pid=$!
  wait_for_log '^READY udp-shaper ' "$tmp_dir/shaper.log" "$shaper_pid" 100 || \
    fail_with_logs "UDP shaper did not become ready"
elif [ "$probe_mode" != ceiling ] && [ "$probe_mode" != stress ]; then
  fail_with_logs "unknown M6_G1_PROBE_MODE"
fi
echo_port=$(printf '%s\n' "$topology" | python3 -c \
  'import json, sys; print(json.load(sys.stdin)["echo_ipv4"])')

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
if [ "$probe_mode" = impairment ]; then
  dns_port=$(printf '%s\n' "$topology" | python3 -c \
    'import json, sys; print(json.load(sys.stdin)["dns"])')
  python3 -u "$repo_dir/tests/masque_udp_dns.py" \
    --host=127.0.0.1 --port="$dns_port" >"$tmp_dir/dns.log" 2>&1 &
  dns_pid=$!
  wait_for_log '^READY ' "$tmp_dir/dns.log" "$dns_pid" 100 || \
    fail_with_logs "DNS fixture did not become ready"

  h3_port=$(printf '%s\n' "$topology" | python3 -c \
    'import json, sys; print(json.load(sys.stdin)["http3"])')
  openssl req -x509 -newkey rsa:2048 -nodes -days 1 -sha256 \
    -subj '/CN=m6-h3.localhost' \
    -addext 'subjectAltName=DNS:m6-h3.localhost' \
    -addext 'basicConstraints=critical,CA:TRUE' \
    -addext 'keyUsage=critical,digitalSignature,keyEncipherment,keyCertSign' \
    -addext 'extendedKeyUsage=serverAuth' \
    -keyout "$tmp_dir/h3.key" -out "$tmp_dir/h3.crt" >/dev/null 2>&1
  export GOPATH="$tmp_dir/go/gopath"
  export GOMODCACHE="${M6_GO_CACHE_ROOT:-${TMPDIR:-/tmp}/naive-m6-go}/modcache"
  export GOCACHE="${M6_GO_CACHE_ROOT:-${TMPDIR:-/tmp}/naive-m6-go}/buildcache"
  (
    cd "$repo_dir/tests/m5"
    "$go_bin" build -trimpath -o "$tmp_dir/h3-origin" ./cmd/h3-origin
    "$go_bin" build -trimpath -o "$tmp_dir/socks-h3-probe" ./cmd/socks-h3-probe
  )
  "$tmp_dir/h3-origin" --bind="127.0.0.1:$h3_port" \
    --cert="$tmp_dir/h3.crt" --key="$tmp_dir/h3.key" \
    >"$tmp_dir/h3.log" 2>&1 &
  h3_pid=$!
  "$tmp_dir/h3-origin" --bind="[::1]:$h3_port" \
    --cert="$tmp_dir/h3.crt" --key="$tmp_dir/h3.key" \
    >"$tmp_dir/h3v6.log" 2>&1 &
  h3v6_pid=$!
  wait_for_log 'M5_G2_H3_ORIGIN_READY' "$tmp_dir/h3.log" "$h3_pid" 200 || \
    fail_with_logs "HTTP/3 application fixture did not become ready"
  wait_for_log 'M5_G2_H3_ORIGIN_READY' "$tmp_dir/h3v6.log" "$h3v6_pid" 200 || \
    fail_with_logs "IPv6 HTTP/3 application fixture did not become ready"
fi

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

runner_ms=60000
if [ "$probe_mode" = stress ]; then
  runner_ms=$(python3 -c \
    'import sys; print(int((float(sys.argv[1]) + 45) * 1000))' \
    "$stress_duration")
fi
"$runner_bin" --proxy-host=127.0.0.1 --proxy-port="$proxy_port" \
  --proxy-user=m5-user --proxy-pass=m5-pass --run-for-ms="$runner_ms" \
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
elif [ "$probe_mode" = pmtu ]; then
  python3 "$script_dir/pmtu_probe.py" --socks-port "$socks_port" \
    --echo-port "$echo_port" --ceiling-file "$tmp_dir/outer-ceiling" | \
    tee "$tmp_dir/probe.log"
  expected_marker=M6_G1C_PMTU_RECOVERY_OK
elif [ "$probe_mode" = stress ]; then
  python3 "$script_dir/stress_probe.py" --socks-port "$socks_port" \
    --echo-port "$echo_port" --runner-pid "$runner_pid" \
    --caddy-pid "$caddy_pid" --duration-seconds "$stress_duration" | \
    tee "$tmp_dir/probe.log"
  expected_marker=M6_G3_STRESS_SMOKE_OK
else
  python3 "$script_dir/impairment_probe.py" --socks-port "$socks_port" \
    --echo-port "$echo_port" --dns-port "$dns_port" \
    --profile="$impairment_profile" \
    --profile-control-file="$tmp_dir/profile-control" | tee "$tmp_dir/probe.log"
  expected_marker="M6_G2_PROFILE_RECOVERY_OK profile=$impairment_profile"

  printf '%s\n' "$impairment_profile" >"$tmp_dir/profile-control"
  sleep 0.1
  "$tmp_dir/socks-h3-probe" --socks="127.0.0.1:$socks_port" \
    --target-host=m6-h3.localhost --target-port="$h3_port" --force-domain \
    --server-name=m6-h3.localhost --ca-cert="$tmp_dir/h3.crt" \
    --timeout=20s | tee "$tmp_dir/h3-probe.log"
  printf 'none\n' >"$tmp_dir/profile-control"
  grep -q 'M5_G2_HTTP3_APPLICATION_OK' "$tmp_dir/h3-probe.log" || \
    fail_with_logs "HTTP/3 application did not complete under impairment"
  echo "M6_G2_HTTP3_APPLICATION_OK profile=$impairment_profile"
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
if [ "$probe_mode" = impairment ]; then
  grep -q "M6_G2_CONTROL_CLOSE_OK profile=$impairment_profile" \
    "$tmp_dir/probe.log" || fail_with_logs "control-close evidence is absent"
  grep -q "M6_G2_NO_REPLAY_OK profile=$impairment_profile" \
    "$tmp_dir/probe.log" || fail_with_logs "no-replay evidence is absent"
fi
association_count=$(grep -c 'connect-udp association event' "$tmp_dir/caddy.log" || true)
test "$association_count" -ge 3 || fail_with_logs "server association evidence is incomplete"

if grep -E "\.well-known/masque/udp|localhost/$echo_port|127\.0\.0\.1/$echo_port|\[::1\]/$echo_port|m6-h3\.localhost/$h3_port|m5-pass|bTUtdXNlcjptNS1wYXNz" \
  "$tmp_dir/caddy.log" "$tmp_dir/access.log" "$tmp_dir/runner.log" \
  "$tmp_dir/netlog.json" >/dev/null 2>&1; then
  fail_with_logs "private G1 target or credential data appeared in evidence"
fi

echo M6_G1_LIVE_CEILING_PRIVACY_OK
if [ "$probe_mode" = ceiling ]; then
  echo M6_G1B_LIVE_CEILING_OK
elif [ "$probe_mode" = pmtu ]; then
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
elif [ "$probe_mode" = stress ]; then
  grep -q 'M6_G3_ASSOCIATION_CAP_REUSE_OK' "$tmp_dir/probe.log" || \
    fail_with_logs "association cap/reuse evidence is absent"
  grep -q 'M6_G3_RESOURCE_RECOVERY_OK' "$tmp_dir/probe.log" || \
    fail_with_logs "resource recovery evidence is absent"
  echo M6_G3_STRESS_HARNESS_OK
else
  grep -q "PROFILE id=$impairment_profile" "$tmp_dir/shaper.log" || \
    fail_with_logs "named impairment profile was not activated"
  grep -q 'PROFILE id=none' "$tmp_dir/shaper.log" || \
    fail_with_logs "impairment removal was not activated"
  case "$impairment_profile" in
    delay)
      grep -q 'reason=delay' "$tmp_dir/shaper.log" || \
        fail_with_logs "delay profile produced no delayed packet"
      ;;
    loss)
      grep -q 'action=drop reason=loss' "$tmp_dir/shaper.log" || \
        fail_with_logs "loss profile produced no deterministic drop"
      ;;
    reorder)
      grep -q 'reason=reorder' "$tmp_dir/shaper.log" || \
        fail_with_logs "reorder profile produced no reordered packet"
      ;;
    bandwidth)
      grep -q 'reason=bandwidth' "$tmp_dir/shaper.log" || \
        fail_with_logs "bandwidth profile produced no serialized packet"
      ;;
    combined)
      grep -q 'action=drop reason=loss' "$tmp_dir/shaper.log" || \
        fail_with_logs "combined profile produced no deterministic loss"
      grep -q 'reason=reorder' "$tmp_dir/shaper.log" || \
        fail_with_logs "combined profile produced no reordered packet"
      ;;
  esac
  echo "M6_G2_PROFILE_OK profile=$impairment_profile"
  echo "M6_G2_TARGET_ISOLATION_OK profile=$impairment_profile"
fi
