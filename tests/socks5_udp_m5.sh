#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
forwardproxy_dir=${M5_FORWARDPROXY_DIR:-/path/to/naive-forwardproxy-m4}
caddy_dir=${M5_CADDY_DIR:-/path/to/caddy-naive-udp-m4}
caddy_bin=${M5_CADDY_BIN:-$forwardproxy_dir/build/m4-caddy}
go_bin=${GO_BIN:-/path/to/naive-m4/go1.25.12/bin/go}
go_cache_root=${M5_GO_CACHE_ROOT:-${TMPDIR:-/tmp}/naive-m5-go}

export GOPATH="$go_cache_root/gopath"
export GOMODCACHE="$go_cache_root/modcache"
export GOCACHE="$go_cache_root/buildcache"
export PYTHONDONTWRITEBYTECODE=1

expected_m3=2bb83aec36
expected_client=333b7cb253
expected_forwardproxy=8f044e278c70d7479c644eb0ebfffc6bb4b7b3c7
expected_caddy=cce894a8a0e987eb1722cf99729499bdaba6c38d

test "$(git -C "$repo_dir" branch --show-current)" = codex/native-udp-foundation
test "$(git -C "$forwardproxy_dir" branch --show-current)" = codex/native-udp-server
test "$(git -C "$caddy_dir" branch --show-current)" = codex/enable-h3-datagrams
test "$(git -C "$repo_dir" rev-parse --short=10 2bb83aec36)" = "$expected_m3"
test "$(git -C "$repo_dir" rev-parse --short=10 "$expected_client")" = \
  "$expected_client"
unexpected_client_changes=$(git -C "$repo_dir" diff --name-only \
  "$expected_m3".."$expected_client" -- src/net |
  sed '/^src\/net\/tools\/naive\/naive_proxy_bin\.cc$/d')
test -z "$unexpected_client_changes"
git -C "$repo_dir" diff --quiet "$expected_client"..HEAD -- src/net
git -C "$repo_dir" diff --quiet -- src/net
test "$(git -C "$forwardproxy_dir" rev-parse "$expected_forwardproxy")" = \
  "$expected_forwardproxy"
unexpected_server_changes=$(git -C "$forwardproxy_dir" diff --name-only \
  "$expected_forwardproxy"..HEAD | sed '/^tests\/m5\//d')
test -z "$unexpected_server_changes"
test "$(git -C "$caddy_dir" rev-parse HEAD)" = "$expected_caddy"
git -C "$forwardproxy_dir" diff --quiet
git -C "$caddy_dir" diff --quiet
test -z "$(git -C "$forwardproxy_dir" status --porcelain)"
test -z "$(git -C "$caddy_dir" status --porcelain)"

ninja -C "$repo_dir/src/out/Release" naive naive_socks5_udp_m3_runner
test -x "$repo_dir/src/out/Release/naive"
test -x "$repo_dir/src/out/Release/naive_socks5_udp_m3_runner"
test -x "$caddy_bin"
test "$("$go_bin" version | awk '{print $3}')" = go1.25.12
"$go_bin" version -m "$caddy_bin" | grep -q '^.*go1\.25\.12$'
command -v shasum >/dev/null

python3 -m json.tool "$script_dir/m5/contract.json" >/dev/null
python3 -m unittest discover -s "$script_dir/m5" -p 'test_*.py'
python3 "$script_dir/m5/topology.py" --contract
"$script_dir/m5/macos_trust_fixture.sh" --contract

(
  cd "$script_dir/m5"
  "$go_bin" test ./...
  "$go_bin" run ./cmd/socks-h3-probe --contract
)

printf 'M5_G0_INPUTS_OK naive_rev=%s naive_runtime_rev=%s forwardproxy_rev=%s forwardproxy_runtime_base=%s caddy_rev=%s\n' \
  "$(git -C "$repo_dir" rev-parse --short=10 HEAD)" \
  "$(git -C "$repo_dir" rev-parse --short=10 "$expected_client")" \
  "$(git -C "$forwardproxy_dir" rev-parse --short=7 HEAD)" \
  "$(git -C "$forwardproxy_dir" rev-parse --short=7 "$expected_forwardproxy")" \
  "$(git -C "$caddy_dir" rev-parse --short=8 HEAD)"
printf 'M5_G0_BINARIES naive=%s sha256=%s runner=%s sha256=%s caddy=%s sha256=%s\n' \
  "$repo_dir/src/out/Release/naive" \
  "$(shasum -a 256 "$repo_dir/src/out/Release/naive" | awk '{print $1}')" \
  "$repo_dir/src/out/Release/naive_socks5_udp_m3_runner" \
  "$(shasum -a 256 "$repo_dir/src/out/Release/naive_socks5_udp_m3_runner" | awk '{print $1}')" \
  "$caddy_bin" \
  "$(shasum -a 256 "$caddy_bin" | awk '{print $1}')"
echo M5_G0_PRODUCT_CONTRACT_OK
"$script_dir/m5/g1_cross_repo_echo.sh"
"$script_dir/m5/g2_product_matrix.sh"
"$script_dir/m5/g3_product_security.sh"
"$script_dir/m5/g4_lifecycle_matrix.sh"
"$script_dir/m5/g5_production_binary.sh"
echo M5_G4_LIFECYCLE_MATRIX_OK
