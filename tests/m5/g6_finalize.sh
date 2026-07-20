#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../.." && pwd)
forwardproxy_dir=${M5_FORWARDPROXY_DIR:-/Users/stoneshi/Documents/naive-forwardproxy-m4}
caddy_dir=${M5_CADDY_DIR:-/Users/stoneshi/Documents/caddy-naive-udp-m4}
audit_doc="$repo_dir/docs/m5-agy-audit.md"

audited_client=eaf172d971
expected_forwardproxy=2b2a8ea
expected_caddy=cce894a8a0e987eb1722cf99729499bdaba6c38d

git -C "$repo_dir" merge-base --is-ancestor "$audited_client" HEAD
git -C "$repo_dir" diff --quiet "$audited_client"..HEAD -- src/net
test "$(git -C "$forwardproxy_dir" rev-parse --short=7 HEAD)" = \
  "$expected_forwardproxy"
test "$(git -C "$caddy_dir" rev-parse HEAD)" = "$expected_caddy"

grep -q '| NaiveProxy |.*`eaf172d971`' "$audit_doc"
grep -q '^AUDIT_PASS$' "$audit_doc"
grep -q '^Zero blocker, high, or medium findings\.$' "$audit_doc"

git -C "$repo_dir" diff --check
git -C "$forwardproxy_dir" diff --check
git -C "$caddy_dir" diff --check
test -z "$(git -C "$repo_dir" status --porcelain |
  sed '/^?? \.DS_Store$/d; /^?? src\/tmp\/$/d')"
test -z "$(git -C "$forwardproxy_dir" status --porcelain)"
test -z "$(git -C "$caddy_dir" status --porcelain)"

echo M5_NATIVE_UDP_MVP_OK
