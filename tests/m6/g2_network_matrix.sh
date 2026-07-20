#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repetitions=${M6_G2_REPETITIONS:-3}
test "$repetitions" -ge 1

profiles=$(python3 - "$script_dir/network_profiles.json" <<'PY'
import json
import sys

document = json.load(open(sys.argv[1], encoding="utf-8"))
print(" ".join(profile["id"] for profile in document["profiles"]))
PY
)

run=1
while [ "$run" -le "$repetitions" ]; do
  for profile in $profiles; do
    output_file=$(mktemp "${TMPDIR:-/tmp}/naive-m6-g2-output.XXXXXX")
    if ! M6_G1_PROBE_MODE=impairment M6_G2_PROFILE="$profile" \
      "$script_dir/g1_live_ceiling.sh" >"$output_file" 2>&1; then
      cat "$output_file" >&2
      unlink "$output_file"
      exit 1
    fi
    cat "$output_file"
    grep -q "M6_G2_PROFILE_OK profile=$profile" "$output_file"
    grep -q "M6_G2_UDP_DNS_OK profile=$profile" "$output_file"
    grep -q "M6_G2_HTTP3_APPLICATION_OK profile=$profile" "$output_file"
    grep -q "M6_G2_CONTROL_CLOSE_OK profile=$profile" "$output_file"
    grep -q "M6_G2_NO_REPLAY_OK profile=$profile" "$output_file"
    grep -q "M6_G2_TARGET_ISOLATION_OK profile=$profile" "$output_file"
    unlink "$output_file"
  done
  printf 'M6_G2_FRESH_ROOT_MATRIX_OK run=%s\n' "$run"
  run=$((run + 1))
done

echo M6_G2_NETWORK_IMPAIRMENT_OK
