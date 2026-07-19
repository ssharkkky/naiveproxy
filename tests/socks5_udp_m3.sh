#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
backend_test="$repo_dir/src/out/Release/naive_connect_udp_backend_test"

ninja -C "$repo_dir/src/out/Release" naive_connect_udp_backend_test
"$backend_test"

# G1-G5 append their deterministic and full-path cases here. Keeping this
# entry point alive from G0 makes every subsequent gate extend the same M3
# verification surface.
echo M3_G0_TEST_SKELETON_OK
