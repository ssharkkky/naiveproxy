#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

python3 "$script_dir/contract_test.py"
python3 - "$script_dir/platform_qualification.json" <<'PY'
import json
import sys

document = json.load(open(sys.argv[1], encoding="utf-8"))
for record in document["platforms"]:
    print(f"M6_G5_PLATFORM_STATE id={record['id']} state={record['state']}")
PY
echo M6_G5_PLATFORM_CONTRACT_OK
