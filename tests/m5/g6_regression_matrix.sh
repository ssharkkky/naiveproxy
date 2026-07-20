#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../.." && pwd)
forwardproxy_dir=${M5_FORWARDPROXY_DIR:-/Users/stoneshi/Documents/naive-forwardproxy-m4}
caddy_dir=${M5_CADDY_DIR:-/Users/stoneshi/Documents/caddy-naive-udp-m4}
go_root=${M5_GO_ROOT:-/Users/stoneshi/.local/naive-m4/go1.25.12}
go_bin=${GO_BIN:-$go_root/bin/go}
xcaddy_bin=${XCADDY_BIN:-/Users/stoneshi/.local/naive-m4/bin/xcaddy}
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/naive-m5-g6.XXXXXX")
caddy_bin="$tmp_dir/m5-g6-caddy"

cleanup() {
  find "$tmp_dir" -type f -exec unlink {} \; >/dev/null 2>&1 || true
  find "$tmp_dir" -depth -type d -exec rmdir {} \; >/dev/null 2>&1 || true
}

on_signal() {
  trap - EXIT HUP INT TERM
  cleanup
  exit 130
}

trap cleanup EXIT
trap on_signal HUP INT TERM

expected_client=333b7cb253
expected_forwardproxy=8f044e278c70d7479c644eb0ebfffc6bb4b7b3c7
expected_caddy=cce894a8a0e987eb1722cf99729499bdaba6c38d

test "$(git -C "$repo_dir" branch --show-current)" = \
  codex/native-udp-foundation
test "$(git -C "$forwardproxy_dir" branch --show-current)" = \
  codex/native-udp-server
test "$(git -C "$caddy_dir" branch --show-current)" = \
  codex/enable-h3-datagrams
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

ninja -C "$repo_dir/src/out/Release" naive naive_masque_server \
  naive_masque_client naive_masque_probe naive_connect_udp_runner \
  naive_socks5_udp_test naive_socks5_server_socket_state_test \
  naive_socks5_udp_association_test naive_socks5_udp_runner \
  naive_connect_udp_backend_test naive_socks5_udp_m3_runner
"$repo_dir/tests/masque_g1_smoke.sh"
"$repo_dir/tests/masque_g2_naive_tunnel.sh"
"$repo_dir/tests/masque_g3_basic_auth.sh"
"$repo_dir/tests/masque_g5_lifecycle.sh"
"$repo_dir/tests/socks5_udp_m2.sh"
"$repo_dir/tests/socks5_udp_m3.sh"
(
  cd "$repo_dir/src"
  ../tests/basic.sh out/Release/naive
)

GO_BIN="$go_bin" XCADDY_BIN="$xcaddy_bin" \
CADDY_SOURCE_DIR="$caddy_dir" \
  "$forwardproxy_dir/scripts/build-naive-caddy.sh" "$caddy_bin"
test -x "$caddy_bin"
"$go_bin" version -m "$caddy_bin" | grep -q '^.*go1\.25\.12$'

M5_CADDY_BIN="$caddy_bin" "$repo_dir/tests/socks5_udp_m5.sh"

repeat=1
while [ "$repeat" -le 3 ]; do
  M5_CADDY_BIN="$caddy_bin" "$script_dir/g1_cross_repo_echo.sh"
  M5_CADDY_BIN="$caddy_bin" "$script_dir/g2_product_matrix.sh"
  M5_CADDY_BIN="$caddy_bin" "$script_dir/g3_product_security.sh"
  M5_CADDY_BIN="$caddy_bin" "$script_dir/g4_lifecycle_matrix.sh"
  printf 'M5_G6_FRESH_ROOT_OK run=%s\n' "$repeat"
  repeat=$((repeat + 1))
done

(
  cd "$forwardproxy_dir"
  PATH="$go_root/bin:$PATH" ./scripts/test-m4.sh
  GO_BIN="$go_bin" CADDY_BIN="$caddy_bin" \
    ./scripts/test-m4-g5-server.sh
  PATH="$go_root/bin:$PATH" "$go_bin" test -count=1 ./...
  PATH="$go_root/bin:$PATH" "$go_bin" test -race -count=1 ./...
)
(
  cd "$caddy_dir"
  PATH="$go_root/bin:$PATH" "$go_bin" test -count=1 ./modules/caddyhttp
)

git -C "$repo_dir" diff --check
git -C "$forwardproxy_dir" diff --check
git -C "$caddy_dir" diff --check

if git -C "$repo_dir" diff --name-only cd9a676df9..HEAD |
  grep -E '(^|/)(node_modules|vendor|__pycache__)(/|$)|\.(key|pem|crt|cer|log|pcap|pcapng|qlog)$' \
  >/dev/null 2>&1; then
  echo "generated or private artifact entered the M5 diff" >&2
  exit 1
fi

unexpected_client_untracked=$(git -C "$repo_dir" status --porcelain |
  sed '/^?? \.DS_Store$/d; /^?? src\/tmp\/$/d; /^?? tests\/m5\/g6_regression_matrix\.sh$/d')
test -z "$unexpected_client_untracked"
test -z "$(git -C "$forwardproxy_dir" status --porcelain)"
test -z "$(git -C "$caddy_dir" status --porcelain)"

echo M5_G6_LOCAL_REGRESSIONS_OK
