#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../.." && pwd)
forwardproxy_dir=${M6_FORWARDPROXY_DIR:-/path/to/naive-forwardproxy-m4}
caddy_dir=${M6_CADDY_DIR:-/path/to/caddy-naive-udp-m4}
go_bin=${GO_BIN:-/path/to/naive-m4/go1.25.12/bin/go}
xcaddy_bin=${XCADDY_BIN:-/path/to/naive-m4/bin/xcaddy}
lima_instance=${M6_LIMA_INSTANCE:-m6g5f}
runner_bin="$repo_dir/src/out/Release/naive_socks5_udp_m3_runner"
tmp_dir=$(mktemp -d "$forwardproxy_dir/build/m6-g5f.XXXXXX")
guest_root="/tmp/naive-m6-g5f-$$"
guest_access="$guest_root/access.log"
caddy_shell_pid=
runner_pid=
echo_pid=
h3_pid=
http_pid=

cleanup() {
  for pid in "$runner_pid" "$echo_pid" "$h3_pid" "$http_pid"; do
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
    fi
  done
  limactl shell "$lima_instance" -- pkill -f m6-caddy-linux-arm64 \
    >/dev/null 2>&1 || true
  if [ -n "$caddy_shell_pid" ]; then
    wait "$caddy_shell_pid" 2>/dev/null || true
  fi
  limactl shell "$lima_instance" -- sh -lc \
    "find '$guest_root' -type f -exec unlink {} \\; 2>/dev/null || true; find '$guest_root' -depth -type d -exec rmdir {} \\; 2>/dev/null || true" \
    >/dev/null 2>&1 || true
  if [ "${M6_KEEP_ARTIFACTS:-0}" = 1 ]; then
    printf '%s\n' "M6_G5F_ARTIFACTS=$tmp_dir" >&2
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
    sleep 0.1
    index=$((index + 1))
  done
  return 1
}

test "$(uname -s)" = Darwin
test "$(uname -m)" = arm64
command -v limactl >/dev/null
test "$(limactl --version)" = 'limactl version 2.1.1'
test "$(limactl list --json | python3 -c \
  'import json,sys; rows=[json.loads(line) for line in sys.stdin if line.strip()]; print(next(row["status"] for row in rows if row["name"]==sys.argv[1]))' \
  "$lima_instance")" = Running
limactl list --json | python3 -c '
import json, sys
rows = [json.loads(line) for line in sys.stdin if line.strip()]
row = next(item for item in rows if item["name"] == sys.argv[1])
assert row["arch"] == "aarch64"
assert any(
    image.get("digest") == sys.argv[2]
    for image in row["config"]["images"]
)
' "$lima_instance" \
  sha512:7a3cdfaefb0cbf3bb6824cd6ae80d6a3e0b0e367609e5fc50c5f714374d31e8d70dd607094a4e9cfe2e6ead7537781782469fe93bcea79611fbce4ddeacc92e1
test "$(limactl shell "$lima_instance" -- uname -m)" = aarch64
test "$(limactl shell "$lima_instance" -- cat /etc/alpine-release)" = 3.23.3
test "$(git -C "$forwardproxy_dir" rev-parse HEAD)" = \
  964281a9797efd9a4c953f6273c73e397e777864
test "$(git -C "$caddy_dir" rev-parse HEAD)" = \
  dd9a89c11194dcb806d845233995ef040f096464
git -C "$repo_dir" merge-base --is-ancestor \
  17c717793c7e5634bf0d5dfa823c9839eb45e832 HEAD
git -C "$repo_dir" diff --quiet -- src/net
git -C "$forwardproxy_dir" diff --quiet
git -C "$caddy_dir" diff --quiet
test -x "$runner_bin"
test "$($go_bin env GOVERSION)" = go1.25.12
"$xcaddy_bin" version | grep -q '^v0\.4\.5 '

linux_caddy="$tmp_dir/m6-caddy-linux-arm64"
PATH="$(dirname "$go_bin"):$PATH" GOOS=linux GOARCH=arm64 CGO_ENABLED=0 \
  "$xcaddy_bin" build v2.11.2 --output "$linux_caddy" \
    --with "github.com/caddyserver/forwardproxy=$forwardproxy_dir" \
    --replace "github.com/caddyserver/caddy/v2=$caddy_dir" \
    >"$tmp_dir/xcaddy.log" 2>&1
test "$($go_bin version -m "$linux_caddy" | awk 'NR == 1 {print $2}')" = \
  go1.25.12
"$repo_dir/src/third_party/llvm-build/Release+Asserts/bin/llvm-readelf" \
  -h "$linux_caddy" | grep -q 'Machine:.*AArch64'

topology=$(python3 "$repo_dir/tests/m5/topology.py")
proxy_port=$(printf '%s\n' "$topology" | python3 -c \
  'import json, sys; print(json.load(sys.stdin)["proxy"])')
echo_port=$(printf '%s\n' "$topology" | python3 -c \
  'import json, sys; print(json.load(sys.stdin)["echo_ipv4"])')
h3_port=$(printf '%s\n' "$topology" | python3 -c \
  'import json, sys; print(json.load(sys.stdin)["http3"])')
http_port=$(python3 - <<'PY'
import socket
sock = socket.socket()
sock.bind(("0.0.0.0", 0))
print(sock.getsockname()[1])
sock.close()
PY
)

openssl req -x509 -newkey rsa:2048 -nodes -days 1 -sha256 \
  -subj '/CN=m5-proxy.localhost' \
  -addext 'subjectAltName=DNS:m5-proxy.localhost,DNS:localhost,IP:127.0.0.1' \
  -keyout "$tmp_dir/server.key" -out "$tmp_dir/server.crt" >/dev/null 2>&1
openssl req -x509 -newkey rsa:2048 -nodes -days 1 -sha256 \
  -subj '/CN=m6-h3.localhost' \
  -addext 'subjectAltName=DNS:m6-h3.localhost' \
  -keyout "$tmp_dir/h3.key" -out "$tmp_dir/h3.crt" >/dev/null 2>&1

export GOPATH="$tmp_dir/go/gopath"
export GOMODCACHE="${M6_GO_CACHE_ROOT:-${TMPDIR:-/tmp}/naive-m6-go}/modcache"
export GOCACHE="${M6_GO_CACHE_ROOT:-${TMPDIR:-/tmp}/naive-m6-go}/buildcache"
(
  cd "$repo_dir/tests/m5"
  "$go_bin" build -trimpath -o "$tmp_dir/h3-origin" ./cmd/h3-origin
  "$go_bin" build -trimpath -o "$tmp_dir/socks-h3-probe" ./cmd/socks-h3-probe
)

python3 -u "$repo_dir/tests/masque_udp_echo.py" --host=0.0.0.0 \
  --port="$echo_port" >"$tmp_dir/echo.log" 2>&1 &
echo_pid=$!
"$tmp_dir/h3-origin" --bind="0.0.0.0:$h3_port" \
  --cert="$tmp_dir/h3.crt" --key="$tmp_dir/h3.key" \
  >"$tmp_dir/h3.log" 2>&1 &
h3_pid=$!
mkdir "$tmp_dir/http"
printf '%s\n' m6-g5f-cross-platform-tcp-ok >"$tmp_dir/http/index.html"
python3 -u -m http.server "$http_port" --bind 0.0.0.0 \
  --directory "$tmp_dir/http" >"$tmp_dir/http.log" 2>&1 &
http_pid=$!
wait_for_log '^READY ' "$tmp_dir/echo.log" "$echo_pid" 100
wait_for_log 'M5_G2_H3_ORIGIN_READY' "$tmp_dir/h3.log" "$h3_pid" 100

limactl shell "$lima_instance" -- mkdir -p "$guest_root/data" "$guest_root/config"
limactl shell "$lima_instance" -- env \
  M5_PROXY_PORT="$proxy_port" \
  M5_SERVER_CERT="$tmp_dir/server.crt" \
  M5_SERVER_KEY="$tmp_dir/server.key" \
  M5_ACCESS_LOG="$guest_access" \
  XDG_DATA_HOME="$guest_root/data" XDG_CONFIG_HOME="$guest_root/config" \
  "$linux_caddy" run --config "$forwardproxy_dir/tests/m5/Caddyfile-trusted" \
    --adapter caddyfile >"$tmp_dir/caddy.log" 2>&1 &
caddy_shell_pid=$!
wait_for_log 'server running' "$tmp_dir/caddy.log" "$caddy_shell_pid" 200

"$runner_bin" --proxy-host=127.0.0.1 --proxy-port="$proxy_port" \
  --proxy-user=m5-user --proxy-pass=m5-pass --run-for-ms=120000 \
  --log-net-log="$tmp_dir/netlog.json" >"$tmp_dir/runner.log" 2>&1 &
runner_pid=$!
wait_for_log 'M3_SOCKS5_UDP_READY' "$tmp_dir/runner.log" "$runner_pid" 200
socks_port=$(sed -n \
  's/.*M3_SOCKS5_UDP_READY.* port=\([0-9][0-9]*\).*/\1/p' \
  "$tmp_dir/runner.log" | head -1)
test -n "$socks_port"

python3 "$repo_dir/tests/socks5_udp_m3.py" --host 127.0.0.1 \
  --port "$socks_port" --target-host host.lima.internal \
  --target-port "$echo_port" --force-domain --payload m6-g5f-udp \
  --marker M6_G5F_UDP_OK
"$tmp_dir/socks-h3-probe" --socks="127.0.0.1:$socks_port" \
  --target-host=host.lima.internal --target-port="$h3_port" --force-domain \
  --server-name=m6-h3.localhost --ca-cert="$tmp_dir/h3.crt" --timeout=20s
tcp_response=$(curl --silent --show-error --max-time 20 --noproxy '' \
  --proxy "socks5h://127.0.0.1:$socks_port" \
  "http://host.lima.internal:$http_port/")
test "$tcp_response" = m6-g5f-cross-platform-tcp-ok

limactl shell "$lima_instance" -- cat "$guest_access" >"$tmp_dir/access.log"
grep -q 'M3_PRODUCTION_FACTORY_ELIGIBILITY_OK' "$tmp_dir/runner.log"
grep -q 'CONNECT-UDP \[redacted\] HTTP/3' "$tmp_dir/netlog.json"
if grep -E 'm6-g5f-udp|m5-pass|bTUtdXNlcjptNS1wYXNz' \
  "$tmp_dir/caddy.log" "$tmp_dir/access.log" "$tmp_dir/runner.log" \
  "$tmp_dir/netlog.json" >/dev/null 2>&1; then
  echo 'private G5f data appeared in evidence' >&2
  exit 1
fi

echo M6_G5F_LINUX_ARM64_SERVER_OK
echo M6_G5F_HTTP3_APPLICATION_OK
echo M6_G5F_TCP_OK
echo M6_G5F_PRIVACY_OK
echo M6_G5F_MACOS_CLIENT_LINUX_SERVER_OK
