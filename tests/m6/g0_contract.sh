#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../.." && pwd)
forwardproxy_dir=${M6_FORWARDPROXY_DIR:-/Users/stoneshi/Documents/naive-forwardproxy-m4}
caddy_dir=${M6_CADDY_DIR:-/Users/stoneshi/Documents/caddy-naive-udp-m4}
go_bin=${GO_BIN:-/Users/stoneshi/.local/naive-m4/go1.25.12/bin/go}
xcaddy_bin=${XCADDY_BIN:-/Users/stoneshi/.local/naive-m4/bin/xcaddy}
contract="$script_dir/contract.json"
audit_doc="$repo_dir/docs/m5-agy-audit.md"

expected_m5_final=e70ee79e056020b288e402b0a8fecfa67fb5aab4
expected_m5_audited=eaf172d9713dafc6519d8c4a6b8ba3a290c222de
expected_g0=80d37395a6
expected_forwardproxy_runtime=8f044e278c70d7479c644eb0ebfffc6bb4b7b3c7
expected_forwardproxy_fixture=2b2a8ea
expected_caddy=cce894a8a0e987eb1722cf99729499bdaba6c38d

test -f "$contract"
python3 "$script_dir/contract_test.py"
echo M6_G0_RELEASE_CONTRACT_OK
echo M6_G0_PLATFORM_CONTRACT_OK

test "$(git -C "$repo_dir" branch --show-current)" = \
  codex/native-udp-foundation
git -C "$repo_dir" merge-base --is-ancestor "$expected_m5_final" HEAD
git -C "$repo_dir" merge-base --is-ancestor "$expected_g0" HEAD
git -C "$repo_dir" diff --quiet "$expected_m5_audited".."$expected_g0" -- \
  src/net

test "$(git -C "$forwardproxy_dir" branch --show-current)" = \
  codex/native-udp-server
git -C "$forwardproxy_dir" merge-base --is-ancestor \
  "$expected_forwardproxy_runtime" HEAD
git -C "$forwardproxy_dir" merge-base --is-ancestor \
  "$expected_forwardproxy_fixture" HEAD
unexpected_server_changes=$(git -C "$forwardproxy_dir" diff --name-only \
  "$expected_forwardproxy_runtime"..HEAD | \
  sed '/^tests\/m5\//d; /^tests\/m6\//d; /^native_udp_fuzz_test\.go$/d')
test -z "$unexpected_server_changes"
git -C "$forwardproxy_dir" diff --quiet

test "$(git -C "$caddy_dir" branch --show-current)" = \
  codex/enable-h3-datagrams
test "$(git -C "$caddy_dir" rev-parse HEAD)" = "$expected_caddy"
git -C "$caddy_dir" diff --quiet

grep -q '| NaiveProxy |.*`eaf172d971`' "$audit_doc"
grep -q '^AUDIT_PASS$' "$audit_doc"
grep -q '^Zero blocker, high, or medium findings\.$' "$audit_doc"
echo M6_G0_M5_BASELINE_OK

test "$(uname -s)" = Darwin
test "$(uname -m)" = arm64
test "$(python3 -c 'import sys; print(sys.version_info.major)')" = 3
command -v ninja >/dev/null
test -x "$repo_dir/src/third_party/llvm-build/Release+Asserts/bin/clang"
"$repo_dir/src/third_party/llvm-build/Release+Asserts/bin/clang" \
  --version | grep -q 20b6ec66967ac2a8f932863c1abf251e5b17a843
test "$($go_bin env GOVERSION)" = go1.25.12
"$xcaddy_bin" version | grep -q '^v0\.4\.5 '

for target in naive naive_socks5_udp_test naive_connect_udp_backend_test \
  naive_socks5_udp_m3_runner; do
  test -x "$repo_dir/src/out/Release/$target"
done
echo M6_G0_TOOLCHAIN_OK

git -C "$repo_dir" diff --check
git -C "$forwardproxy_dir" diff --check
git -C "$caddy_dir" diff --check

echo M6_G0_CONTRACT_OK
