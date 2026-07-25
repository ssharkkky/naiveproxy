#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../.." && pwd)
forwardproxy_dir=${M6_FORWARDPROXY_DIR:?set M6_FORWARDPROXY_DIR}
caddy_dir=${M6_CADDY_DIR:?set M6_CADDY_DIR}
caddy_bin=${M6_CADDY_BIN:?set M6_CADDY_BIN}
go_bin=${GO_BIN:-go}
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/naive-m6-g5c.XXXXXX")

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

run_logged() {
  label=$1
  shift
  output="$tmp_dir/$label.log"
  if ! "$@" >"$output" 2>&1; then
    printf 'M6 G5c command failed: %s\n' "$label" >&2
    tail -120 "$output" >&2 || true
    exit 1
  fi
  cat "$output"
}

test "$(uname -s)" = Linux
case "$(uname -m)" in
  x86_64|amd64) ;;
  *) echo "G5c requires a native Linux x64 runner" >&2; exit 2 ;;
esac

naive_revision=$(git -C "$repo_dir" rev-parse HEAD)
forwardproxy_revision=$(git -C "$forwardproxy_dir" rev-parse HEAD)
caddy_revision=$(git -C "$caddy_dir" rev-parse HEAD)

git -C "$repo_dir" merge-base --is-ancestor 711e792fd2 HEAD
test "$forwardproxy_revision" = 964281a9797efd9a4c953f6273c73e397e777864
test "$caddy_revision" = dd9a89c11194dcb806d845233995ef040f096464
git -C "$repo_dir" diff --quiet -- src/net
git -C "$forwardproxy_dir" diff --quiet
git -C "$caddy_dir" diff --quiet
test -x "$caddy_bin"
test "$($go_bin env GOVERSION)" = go1.25.12

ninja -C "$repo_dir/src/out/Release" naive naive_socks5_udp_test \
  naive_connect_udp_backend_test naive_socks5_udp_m3_runner
echo M6_G5C_LINUX_BUILD_OK

M5_FORWARDPROXY_DIR="$forwardproxy_dir" \
M5_CADDY_DIR="$caddy_dir" \
M5_CADDY_BIN="$caddy_bin" \
M5_EXPECTED_CLIENT="$naive_revision" \
M5_EXPECTED_FORWARDPROXY="$forwardproxy_revision" \
M5_EXPECTED_CADDY="$caddy_revision" \
GO_BIN="$go_bin" \
  run_logged product "$repo_dir/tests/m5/g5_production_binary.sh"
for marker in M5_G5_UNTRUSTED_CERT_REJECTED_OK \
  M5_G5_PRODUCTION_ECHO_OK M3_G4_DNS_OK \
  M5_G2_HTTP3_APPLICATION_OK M5_G5_PRODUCTION_TCP_OK \
  M5_G4_CONTROL_CLOSE_OK M5_G4_SERVER_IDLE_RECONNECT_OK \
  M5_G5_DEFAULT_CERT_VERIFIER_OK \
  M5_G5_PRODUCTION_BINARY_OK M5_G5_H3_DATAGRAM_EVIDENCE_OK \
  M5_G5_NO_PADDING_BASELINE_OK M5_G5_TRUST_CLEANUP_OK; do
  grep -q "^$marker" "$tmp_dir/product.log"
done
echo M6_G5C_LINUX_PRODUCT_OK

M6_FORWARDPROXY_DIR="$forwardproxy_dir" \
M6_CADDY_DIR="$caddy_dir" M6_CADDY_BIN="$caddy_bin" \
M6_G2_REPETITIONS=1 \
  run_logged impairment "$script_dir/g2_network_matrix.sh"
grep -q '^M6_G2_NETWORK_IMPAIRMENT_OK$' "$tmp_dir/impairment.log"

M6_FORWARDPROXY_DIR="$forwardproxy_dir" \
M6_CADDY_DIR="$caddy_dir" M6_CADDY_BIN="$caddy_bin" \
M6_G3_TIER=smoke \
  run_logged lifecycle "$script_dir/g3_stress_soak.sh"
grep -q '^M6_G3_STRESS_SMOKE_MATRIX_OK$' "$tmp_dir/lifecycle.log"

(
  cd "$forwardproxy_dir"
  "$go_bin" test -count=1 ./...
) >"$tmp_dir/server.log" 2>&1 || {
  tail -120 "$tmp_dir/server.log" >&2 || true
  exit 1
}
echo M6_G5C_LINUX_SERVER_OK

git -C "$repo_dir" diff --check
git -C "$forwardproxy_dir" diff --check
git -C "$caddy_dir" diff --check
echo M6_G5C_LINUX_X64_OK
