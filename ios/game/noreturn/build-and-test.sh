#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 2 ]]; then echo 'Usage: build-and-test.sh CHECKOUT EXTERNAL_OUTPUT' >&2; exit 2; fi
checkout=$1
output=$2
here=$(cd "$(dirname "$0")" && pwd)
python3 - "$output" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]).resolve()
if len(p.parts)<3 or p.parts[1]!='Volumes' or not Path('/Volumes',p.parts[2]).is_mount():
    raise SystemExit('Tool output must be on mounted external disk')
PY
mkdir -p "$output"
xcrun clang++ -std=c++17 -O2 -Wall -Wextra -Werror -I"$checkout/linux-port/src" "$here/nid-oracle.cpp" "$checkout/linux-port/src/loader/nid.cpp" -o "$output/nid-oracle"
python3 "$here/test_derive.py" --nid-oracle "$output/nid-oracle" --output "$output/synthetic" > "$output/synthetic-result.json"
cat "$output/synthetic-result.json"
