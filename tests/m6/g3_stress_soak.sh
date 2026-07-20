#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/../.." && pwd)
tier=${M6_G3_TIER:-smoke}
duration_override=0
duration=$(python3 - "$script_dir/soak_tiers.json" "$tier" <<'PY'
import json
import sys

document = json.load(open(sys.argv[1], encoding="utf-8"))
if sys.argv[2] not in document["tiers"]:
    raise SystemExit(f"unknown soak tier: {sys.argv[2]}")
print(document["tiers"][sys.argv[2]])
PY
)

if [ -n "${M6_G3_DURATION_SECONDS:-}" ]; then
  duration_override=1
  duration=$M6_G3_DURATION_SECONDS
fi

"$repo_dir/src/out/Release/naive_connect_udp_backend_test" | \
  grep -q M3_G2_MULTI_TARGET_LIMITS_OK
M6_G1_PROBE_MODE=stress M6_G3_DURATION_SECONDS="$duration" \
  "$script_dir/g1_live_ceiling.sh"

printf 'M6_G3_TIER_OK tier=%s duration_seconds=%s\n' "$tier" "$duration"
if [ "$tier" = qualification ] && [ "$duration_override" -eq 0 ]; then
  echo M6_G3_STRESS_SOAK_OK
elif [ "$tier" = extended ] && [ "$duration_override" -eq 0 ]; then
  echo M6_G3_EXTENDED_SOAK_OK
else
  echo M6_G3_STRESS_SMOKE_MATRIX_OK
fi
