#!/bin/sh

# M6-G6 release-candidate regression matrix.
#
# Runs the complete M1-M6 client/product/server matrices, all 56 TCP cases,
# legacy/privacy/race tests, and three clean-root product repetitions
# against the exact M6 release pins:
#   client runtime   474a1e4b0a (docs-only commits may follow; src/net frozen)
#   forwardproxy     964281a9 (build lock e9663e4 pins Caddy dd9a89c1)
#   Caddy            dd9a89c1 (TLS module race fix, pending G6 audit)
#
# The M5 product scripts receive the release Caddy pin through
# M5_EXPECTED_CADDY; their audited default pin stays cce894a8.
#
# Exit marker: M6_G6_LOCAL_RELEASE_CHECKLIST_OK

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../.." && pwd)
forwardproxy_dir=${M6_FORWARDPROXY_DIR:-/path/to/naive-forwardproxy-m4}
caddy_dir=${M6_CADDY_DIR:-/path/to/caddy-naive-udp-m4}
go_root=${M6_GO_ROOT:-/path/to/naive-m4/go1.25.12}
go_bin=${GO_BIN:-$go_root/bin/go}
xcaddy_bin=${XCADDY_BIN:-/path/to/naive-m4/bin/xcaddy}
release_dir="$repo_dir/src/out/Release"
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/naive-m6-g6.XXXXXX")
caddy_bin="$tmp_dir/m6-g6-caddy"

cleanup() {
  # Go module extractions are read-only by design; make them writable so the
  # temporary root (including the pre-seeded isolated Go cache) is removed
  # completely.
  chmod -R u+w "$tmp_dir" >/dev/null 2>&1 || true
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

# --- exact release pins ---
audited_client=eaf172d9713dafc6519d8c4a6b8ba3a290c222de
client_rc=474a1e4b0aeb9c64e6d0083eaddd205c887bf608
forwardproxy_base=8f044e278c70d7479c644eb0ebfffc6bb4b7b3c7
forwardproxy_rc=964281a9797efd9a4c953f6273c73e397e777864
caddy_rc=dd9a89c11194dcb806d845233995ef040f096464

test "$(git -C "$repo_dir" branch --show-current)" = \
  codex/native-udp-foundation
git -C "$repo_dir" merge-base --is-ancestor "$audited_client" HEAD
git -C "$repo_dir" diff --quiet "$client_rc"..HEAD -- src/net
test "$(git -C "$forwardproxy_dir" branch --show-current)" = \
  codex/native-udp-server
test "$(git -C "$forwardproxy_dir" rev-parse HEAD)" = "$forwardproxy_rc"
test "$(git -C "$caddy_dir" branch --show-current)" = \
  codex/enable-h3-datagrams
test "$(git -C "$caddy_dir" rev-parse HEAD)" = "$caddy_rc"
git -C "$forwardproxy_dir" diff --quiet
git -C "$caddy_dir" diff --quiet
# The M6 owner runtime delta over the M4 audited base stays inside the
# frozen file set: one production fix, the toolchain lock, the test-only
# RFC 9298 client, tests, scripts, and M5 fixtures.
unexpected_server_changes=$(git -C "$forwardproxy_dir" diff --name-only \
  "$forwardproxy_base"..HEAD |
  grep -vE '^forwardproxy\.go$|^M4_TOOLCHAIN\.lock$|^cmd/m4-rfc9298-client/main\.go$|_test\.go$|^scripts/|^tests/m5/' || true)
test -z "$unexpected_server_changes"

# --- toolchain pins ---
test "$("$go_bin" version | awk '{print $3}' | sed 's/^go//')" = "1.25.12"
test "$("$xcaddy_bin" version | awk '{print $1}')" = "v0.4.5"

# --- 1. rebuild every named client Release target from the pinned runtime ---
ninja -C "$release_dir" naive naive_masque_server naive_masque_client \
  naive_masque_probe naive_connect_udp_runner naive_socks5_udp_test \
  naive_socks5_server_socket_state_test naive_socks5_udp_association_test \
  naive_socks5_udp_runner naive_connect_udp_backend_test \
  naive_socks5_udp_m3_runner

# --- 2. M1-M3 client matrix and all 56 TCP cases ---
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

# --- 3. build the release-candidate server from the exact pins ---
GO_BIN="$go_bin" XCADDY_BIN="$xcaddy_bin" \
CADDY_SOURCE_DIR="$caddy_dir" \
  "$forwardproxy_dir/scripts/build-naive-caddy.sh" "$caddy_bin"
test -x "$caddy_bin"
"$go_bin" version -m "$caddy_bin" | grep -q '^.*go1\.25\.12$'

# --- 4. shipped production gate plus three clean-root product repetitions ---
# Pre-seed the shipped-binary gate's isolated Go cache with the exact pinned
# fixture modules (tests/m5 go.mod/go.sum) in a fresh per-run root. The gate
# keeps its fresh-root isolation, but its fixture build no longer depends on
# a mid-run network fetch.
m5_go_cache_root="$tmp_dir/m5-go-cache"
(
  cd "$repo_dir/tests/m5"
  GOPATH="$m5_go_cache_root/gopath" \
  GOMODCACHE="$m5_go_cache_root/modcache" \
  GOCACHE="$m5_go_cache_root/buildcache" \
  "$go_bin" mod download
) 

M5_CADDY_BIN="$caddy_bin" M5_EXPECTED_CADDY="$caddy_rc" \
M5_GO_CACHE_ROOT="$m5_go_cache_root" \
M5_EXPECTED_CLIENT="$client_rc" \
M5_EXPECTED_FORWARDPROXY="$forwardproxy_rc" \
  "$repo_dir/tests/m5/g5_production_binary.sh"

repeat=1
while [ "$repeat" -le 3 ]; do
  M5_CADDY_BIN="$caddy_bin" M5_EXPECTED_CADDY="$caddy_rc" \
  M5_EXPECTED_CLIENT="$client_rc" \
  M5_EXPECTED_FORWARDPROXY="$forwardproxy_rc" \
    "$script_dir/../m5/g1_cross_repo_echo.sh"
  M5_CADDY_BIN="$caddy_bin" M5_EXPECTED_CADDY="$caddy_rc" \
  M5_EXPECTED_CLIENT="$client_rc" \
  M5_EXPECTED_FORWARDPROXY="$forwardproxy_rc" \
    "$script_dir/../m5/g2_product_matrix.sh"
  M5_CADDY_BIN="$caddy_bin" M5_EXPECTED_CADDY="$caddy_rc" \
  M5_EXPECTED_CLIENT="$client_rc" \
  M5_EXPECTED_FORWARDPROXY="$forwardproxy_rc" \
    "$script_dir/../m5/g3_product_security.sh"
  M5_CADDY_BIN="$caddy_bin" M5_EXPECTED_CADDY="$caddy_rc" \
  M5_EXPECTED_CLIENT="$client_rc" \
  M5_EXPECTED_FORWARDPROXY="$forwardproxy_rc" \
    "$script_dir/../m5/g4_lifecycle_matrix.sh"
  printf 'M6_G6_PRODUCT_REPETITION_OK run=%s\n' "$repeat"
  repeat=$((repeat + 1))
done

# --- 5. M6 gates on the release runtime ---
# Pre-seed the M6 gates' isolated Go cache with the exact pinned fixture
# modules (tests/m5 go.mod/go.sum) in a fresh per-run root, mirroring the
# shipped-binary gate. The impairment, stress, and ceiling probes build the
# h3-origin/socks-h3-probe fixtures from this cache and never touch a shared
# module cache that a prior run could leave half-extracted.
m6_go_cache_root="$tmp_dir/m6-go-cache"
(
  cd "$repo_dir/tests/m5"
  GOPATH="$m6_go_cache_root/gopath" \
  GOMODCACHE="$m6_go_cache_root/modcache" \
  GOCACHE="$m6_go_cache_root/buildcache" \
  "$go_bin" mod download
)
M6_GO_CACHE_ROOT="$m6_go_cache_root" \
  "$script_dir/g2_network_matrix.sh"
M6_GO_CACHE_ROOT="$m6_go_cache_root" \
  env -u M6_G3_DURATION_SECONDS M6_G3_TIER=qualification \
  "$script_dir/g3_stress_soak.sh"
"$script_dir/g4_sanitizer_fuzz.sh"

# --- 6. server owner, legacy, privacy, and race regressions ---
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

# --- 7. hygiene: whitespace, forbidden artifacts, and tree state ---
git -C "$repo_dir" diff --check
git -C "$forwardproxy_dir" diff --check
git -C "$caddy_dir" diff --check

if git -C "$repo_dir" diff --name-only "$audited_client"..HEAD |
  grep -E '(^|/)(node_modules|vendor|__pycache__)(/|$)|\.(key|pem|crt|cer|log|pcap|pcapng|qlog)$' \
  >/dev/null 2>&1; then
  echo "generated or private artifact entered the M6 client diff" >&2
  exit 1
fi
if git -C "$forwardproxy_dir" diff --name-only "$forwardproxy_base"..HEAD |
  grep -E '(^|/)(node_modules|vendor|__pycache__)(/|$)|\.(key|pem|crt|cer|log|pcap|pcapng|qlog)$' \
  >/dev/null 2>&1; then
  echo "generated or private artifact entered the M6 forwardproxy diff" >&2
  exit 1
fi
if git -C "$caddy_dir" diff --name-only \
  cce894a8a0e987eb1722cf99729499bdaba6c38d..HEAD |
  grep -E '(^|/)(node_modules|vendor|__pycache__)(/|$)|\.(key|pem|crt|cer|log|pcap|pcapng|qlog)$' \
  >/dev/null 2>&1; then
  echo "generated or private artifact entered the M6 Caddy diff" >&2
  exit 1
fi

unexpected_client_untracked=$(git -C "$repo_dir" status --porcelain |
  grep '^?? ' |
  sed '/^?? \.DS_Store$/d; /^?? audit_results\.txt$/d; /^?? dns\.log$/d; /^?? h3\.log$/d; /^?? http\.log$/d; /^?? new_src_files\.txt$/d; /^?? new_test_files\.txt$/d; /^?? src\/android-ndk-r24-linux\.zip$/d; /^?? src\/android-ndk-r24\//d; /^?? tests\/m6\/g6_release_matrix\.sh$/d')
test -z "$unexpected_client_untracked"
test -z "$(git -C "$forwardproxy_dir" status --porcelain)"
test -z "$(git -C "$caddy_dir" status --porcelain)"

echo M6_G6_LOCAL_RELEASE_CHECKLIST_OK
