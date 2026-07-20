#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../.." && pwd)
forwardproxy_dir=${M6_FORWARDPROXY_DIR:-/path/to/naive-forwardproxy-m4}
caddy_dir=${M6_CADDY_DIR:-/path/to/caddy-naive-udp-m4}
go_bin=${GO_BIN:-/path/to/naive-m4/go1.25.12/bin/go}
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/naive-m6-g1d.XXXXXX")

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
  if ! "$@" >"$output"; then
    printf '%s\n' "M6 G1d command failed: $label" >&2
    tail -120 "$output" >&2 || true
    exit 1
  fi
  cat "$output"
}

run_logged shipped "$script_dir/g1_shipped_ceiling.sh"
grep -q '^M6_G1B2_SHIPPED_CEILING_OK bytes=1314$' "$tmp_dir/shipped.log"
grep -q '^M6_G1B2_DEFAULT_VERIFIER_OK$' "$tmp_dir/shipped.log"
grep -q '^M6_G1B2_TRUST_CLEANUP_OK$' "$tmp_dir/shipped.log"

run=1
while [ "$run" -le 3 ]; do
  run_logged "ceiling-$run" "$script_dir/g1_live_ceiling.sh"
  grep -q '^M6_G1B_LIVE_CEILING_OK$' "$tmp_dir/ceiling-$run.log"

  if ! M6_G1_PROBE_MODE=pmtu "$script_dir/g1_live_ceiling.sh" \
    >"$tmp_dir/pmtu-$run.log" 2>&1; then
    tail -120 "$tmp_dir/pmtu-$run.log" >&2 || true
    exit 1
  fi
  cat "$tmp_dir/pmtu-$run.log"
  grep -q '^M6_G1C_PMTU_OK$' "$tmp_dir/pmtu-$run.log"

  run_logged "m3-$run" "$repo_dir/tests/socks5_udp_m3.sh"
  grep -q '^M3_NATIVE_UDP_CLIENT_OK$' "$tmp_dir/m3-$run.log"

  run_logged "tcp-$run" "$repo_dir/tests/basic.sh" \
    "$repo_dir/src/out/Release/naive"

  (
    cd "$forwardproxy_dir"
    PATH="$(dirname "$go_bin"):$PATH" "$go_bin" test -count=1 ./...
  ) >"$tmp_dir/server-$run.log" 2>&1 || {
    tail -120 "$tmp_dir/server-$run.log" >&2 || true
    exit 1
  }
  cat "$tmp_dir/server-$run.log"

  (
    cd "$caddy_dir"
    PATH="$(dirname "$go_bin"):$PATH" "$go_bin" test -count=1 \
      ./modules/caddyhttp
  ) >"$tmp_dir/caddy-$run.log" 2>&1 || {
    tail -120 "$tmp_dir/caddy-$run.log" >&2 || true
    exit 1
  }
  cat "$tmp_dir/caddy-$run.log"

  printf 'M6_G1D_REGRESSION_RUN_OK run=%s\n' "$run"
  run=$((run + 1))
done

python3 "$script_dir/contract_test.py"
git -C "$repo_dir" diff --check
git -C "$forwardproxy_dir" diff --check
git -C "$caddy_dir" diff --check
echo M6_G1_PAYLOAD_PMTU_OK
