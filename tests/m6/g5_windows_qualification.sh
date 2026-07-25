#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../.." && pwd)
forwardproxy_dir=${M6_FORWARDPROXY_DIR:?set M6_FORWARDPROXY_DIR}
caddy_dir=${M6_CADDY_DIR:?set M6_CADDY_DIR}
caddy_bin=${M6_CADDY_BIN:?set M6_CADDY_BIN}
go_bin=${GO_BIN:-go}
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/naive-m6-g5d.XXXXXX")
product_pid=

terminate_product_tree() {
  [ -n "$product_pid" ] || return 0
  MSYS2_ARG_CONV_EXCL='*' taskkill.exe /PID "$product_pid" /T /F \
    >/dev/null 2>&1 || kill "$product_pid" 2>/dev/null || true
  wait "$product_pid" 2>/dev/null || true
  product_pid=
}

cleanup() {
  terminate_product_tree
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

run_product_logged() {
  output="$tmp_dir/product.log"
  phase_file="$tmp_dir/product.phase"
  timeout_seconds=${M6_G5D_PRODUCT_TIMEOUT_SECONDS:-1200}
  elapsed=0
  previous_phase=

  M5_G5_PHASE_FILE="$phase_file" "$@" >"$output" 2>&1 &
  product_pid=$!
  while kill -0 "$product_pid" 2>/dev/null; do
    if [ -s "$phase_file" ]; then
      current_phase=$(sed -n '1p' "$phase_file")
      if [ "$current_phase" != "$previous_phase" ]; then
        printf 'M6_G5D_PRODUCT_PHASE phase=%s\n' "$current_phase"
        previous_phase=$current_phase
      fi
    fi
    if [ "$elapsed" -ge "$timeout_seconds" ]; then
      printf 'M6_G5D_PRODUCT_TIMEOUT phase=%s seconds=%s\n' \
        "${previous_phase:-unknown}" "$timeout_seconds" >&2
      terminate_product_tree
      tail -120 "$output" >&2 || true
      exit 1
    fi
    sleep 2
    elapsed=$((elapsed + 2))
  done

  if ! wait "$product_pid"; then
    printf 'M6 G5d command failed: product phase=%s\n' \
      "${previous_phase:-unknown}" >&2
    tail -120 "$output" >&2 || true
    exit 1
  fi
  product_pid=
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
test "$forwardproxy_revision" = 964281a9797efd9a4c953f6273c73e397e777864
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
  run_product_logged "$repo_dir/tests/m5/g5_production_binary.sh"
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
