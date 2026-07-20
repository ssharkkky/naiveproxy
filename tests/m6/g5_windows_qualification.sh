#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../.." && pwd)
forwardproxy_dir=${M6_FORWARDPROXY_DIR:?set M6_FORWARDPROXY_DIR}
caddy_dir=${M6_CADDY_DIR:?set M6_CADDY_DIR}
caddy_bin=${M6_CADDY_BIN:?set M6_CADDY_BIN}
go_bin=${GO_BIN:-go}
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/naive-m6-g5d.XXXXXX")

cleanup() {
  find "$tmp_dir" -type f -exec rm -f {} \; >/dev/null 2>&1 || true
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
    printf 'M6 G5d command failed: %s\n' "$label" >&2
    tail -120 "$output" >&2 || true
    exit 1
  fi
  cat "$output"
}

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) ;;
  *) echo "G5d requires a native Windows runner" >&2; exit 2 ;;
esac
case "$(uname -m)" in
  x86_64|amd64) ;;
  *) echo "G5d requires Windows x64" >&2; exit 2 ;;
esac

naive_revision=$(git -C "$repo_dir" rev-parse HEAD)
forwardproxy_revision=$(git -C "$forwardproxy_dir" rev-parse HEAD)
caddy_revision=$(git -C "$caddy_dir" rev-parse HEAD)

git -C "$repo_dir" merge-base --is-ancestor 711e792fd2 HEAD
test "$forwardproxy_revision" = f14924cdedc93c28a2b92c8120538ea5beee28fb
test "$caddy_revision" = dd9a89c11194dcb806d845233995ef040f096464
git -C "$repo_dir" diff --quiet -- src/net
git -C "$forwardproxy_dir" diff --quiet
git -C "$caddy_dir" diff --quiet
test -x "$caddy_bin"
test "$($go_bin env GOVERSION)" = go1.25.12

test -x "$repo_dir/src/out/Release/naive.exe"
echo M6_G5D_WINDOWS_BUILD_OK

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
  M5_G5_DEFAULT_CERT_VERIFIER_OK M5_G5_PRODUCTION_BINARY_OK \
  M5_G5_H3_DATAGRAM_EVIDENCE_OK M5_G5_NO_PADDING_BASELINE_OK \
  M5_G5_TRUST_CLEANUP_OK; do
  grep -q "^$marker" "$tmp_dir/product.log"
done
echo M6_G5D_WINDOWS_PRODUCT_OK

echo M6_G5D_WINDOWS_SHIPPED_CLIENT_OK

(
  cd "$forwardproxy_dir"
  "$go_bin" test -count=1 ./...
) >"$tmp_dir/server.log" 2>&1 || {
  tail -120 "$tmp_dir/server.log" >&2 || true
  exit 1
}
echo M6_G5D_WINDOWS_SERVER_OK

git -C "$repo_dir" diff --check
git -C "$forwardproxy_dir" diff --check
git -C "$caddy_dir" diff --check
echo M6_G5D_WINDOWS_X64_OK
