#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../.." && pwd)
forwardproxy_dir=${M6_FORWARDPROXY_DIR:-/path/to/naive-forwardproxy-m4}
caddy_dir=${M6_CADDY_DIR:-/path/to/caddy-naive-udp-m4}
go_bin=${GO_BIN:-/path/to/naive-m4/go1.25.12/bin/go}
gn_bin="$repo_dir/src/gn/out/gn"
release_dir="$repo_dir/src/out/Release"
asan_dir="$repo_dir/src/out/ASan"
contract="$script_dir/contract.json"
tmp_dir=$(mktemp -d "${TMPDIR:-/tmp}/naive-m6-g4.XXXXXX")

eval "$(python3 - "$contract" <<'PY'
import json
import shlex
import sys

budget = json.load(open(sys.argv[1], encoding="utf-8"))["g4_fuzz_budget"]
values = {
    "release_seeds": " ".join(str(seed) for seed in budget["client_release_seeds"]),
    "release_iterations": str(budget["client_release_iterations_per_seed"]),
    "sanitizer_seed": str(budget["client_sanitizer_seed"]),
    "sanitizer_iterations": str(budget["client_sanitizer_iterations"]),
    "fuzz_seconds": str(budget["server_fuzz_seconds_per_target"]),
    "race_count": str(budget["server_race_count"]),
}
for name, value in values.items():
    print(f"{name}={shlex.quote(value)}")
PY
)"

if [ -n "${M6_G4_GO_FUZZ_SECONDS:-}" ] && \
   [ "$M6_G4_GO_FUZZ_SECONDS" != "$fuzz_seconds" ]; then
  echo "M6_G4_GO_FUZZ_SECONDS must equal frozen budget $fuzz_seconds" >&2
  exit 2
fi

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

test -x "$gn_bin"
test -x "$go_bin"
test "$(git -C "$caddy_dir" rev-parse HEAD)" = \
  dd9a89c11194dcb806d845233995ef040f096464
cp "$forwardproxy_dir/go.mod" "$tmp_dir/go.mod"
cp "$forwardproxy_dir/go.sum" "$tmp_dir/go.sum"
(
  cd "$forwardproxy_dir"
  "$go_bin" mod edit -modfile="$tmp_dir/go.mod" \
    -replace="github.com/caddyserver/caddy/v2=$caddy_dir"
)
mkdir -p "$asan_dir"
cp "$script_dir/asan_args.gn" "$asan_dir/args.gn"
"$gn_bin" gen "$asan_dir" --root="$repo_dir/src"

ninja -C "$release_dir" naive_socks5_udp_fuzz_test \
  naive_socks5_udp_test naive_connect_udp_backend_test
ninja -C "$asan_dir" naive_socks5_udp_fuzz_test \
  naive_socks5_udp_test naive_connect_udp_backend_test

for seed in $release_seeds; do
  "$release_dir/naive_socks5_udp_fuzz_test" --seed="$seed" \
    --iterations="$release_iterations"
done
echo M6_G4_RELEASE_CODEC_FUZZ_OK

ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  "$asan_dir/naive_socks5_udp_fuzz_test" --seed="$sanitizer_seed" \
    --iterations="$sanitizer_iterations"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  "$asan_dir/naive_socks5_udp_test"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  "$asan_dir/naive_connect_udp_backend_test"
echo M6_G4_ASAN_UBSAN_OK

(
  cd "$forwardproxy_dir"
  export XDG_DATA_HOME="$tmp_dir/data"
  export XDG_CONFIG_HOME="$tmp_dir/config"
  PATH="$(dirname "$go_bin"):$PATH" "$go_bin" test \
    -modfile="$tmp_dir/go.mod" -race \
    -count="$race_count" ./...
  PATH="$(dirname "$go_bin"):$PATH" "$go_bin" test \
    -modfile="$tmp_dir/go.mod" -run='^$' \
    -fuzz=FuzzParseConnectUDPTarget -fuzztime="${fuzz_seconds}s"
  PATH="$(dirname "$go_bin"):$PATH" "$go_bin" test \
    -modfile="$tmp_dir/go.mod" -run='^$' \
    -fuzz=FuzzConnectUDPContextCodec -fuzztime="${fuzz_seconds}s"
)
echo M6_G4_GO_RACE_FUZZ_OK

git -C "$repo_dir" diff --check
git -C "$forwardproxy_dir" diff --check
echo M6_G4_SANITIZER_FUZZ_OK
