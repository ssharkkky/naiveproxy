#!/usr/bin/env bash
set -euo pipefail

lock_file=${1:-"$(cd "$(dirname "$0")/.." && pwd)/release/product.lock.json"}
shift || true

python3 - "$lock_file" "$@" <<'PY'
import json
import re
import subprocess
import sys
from pathlib import Path

lock_path = Path(sys.argv[1])
dirs = sys.argv[2:]
data = json.loads(lock_path.read_text())
required = [
    ("naiveproxy.commit", data["naiveproxy"]["commit"]),
    ("server.forwardproxy_commit", data["server"]["forwardproxy_commit"]),
    ("server.caddy_commit", data["server"]["caddy_commit"]),
    ("server.quic_go_commit", data["server"]["quic_go_commit"]),
]
for name, value in required:
    if not re.fullmatch(r"[0-9a-f]{40}", value):
        raise SystemExit(f"{name} is not a full commit SHA")

if data["release_channel"] not in {"experimental", "rc", "stable"}:
    raise SystemExit("invalid release_channel")
if data["defaults"] != {
    "client_quic_congestion": "cubic",
    "server_quic_congestion": "cubic",
}:
    raise SystemExit("CUBIC defaults are required")

expected = [value for _, value in required]
for directory, commit in zip(dirs, expected):
    actual = subprocess.check_output(
        ["git", "-C", directory, "rev-parse", "HEAD"], text=True
    ).strip()
    if actual != commit:
        raise SystemExit(f"{directory}: {actual} != {commit}")

print(f"PRODUCT_LOCK_OK {data['product_version']}")
print("PIN_SET " + " ".join(expected))
PY
