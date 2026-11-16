#!/bin/bash
# Standalone private evidence; does not alter a device or application.
set -euo pipefail
if [[ $# -ne 5 ]]; then
  echo 'Usage: run-fp-observer.sh OUT AMD_HOST GAME_IMAGE ORIGINAL_OBJECT REMILL_INCLUDE' >&2
  exit 2
fi
out=$1; host=$2; image=$3; original=$4; include=$5
python3 - "$out" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]).resolve()
if len(p.parts)<3 or p.parts[1]!='Volumes' or not Path('/Volumes',p.parts[2]).is_mount():
 raise SystemExit('Output must be on a mounted external disk')
PY
mkdir -p "$out"
source_dir=$(cd "$(dirname "$0")" && pwd)
cpu_dir=$(cd "$source_dir/../.." && pwd)
clang=/opt/homebrew/opt/llvm/bin/clang
cxx=/opt/homebrew/opt/llvm/bin/clang++
common=(-target arm64-apple-macos15 -std=c++20 -O2 -frounding-math -ffp-contract=off -I"$cpu_dir" -I"$include")
"$clang" -target arm64-apple-macos15 -std=c11 -O2 -I"$cpu_dir/fp80" -c "$cpu_dir/fp80/exceptions.c" -o "$out/exceptions.o"
"$cxx" "${common[@]}" -c "$source_dir/batch-chunk-runtime.cpp" -o "$out/memory.o"
"$cxx" "${common[@]}" "$source_dir/fp-observer.cpp" "$out/exceptions.o" "$out/memory.o" "$original" -o "$out/fp-observer"
"$out/fp-observer" | tee "$out/observer.txt"
python3 "$source_dir/fp-reference.py" --host "$host" --image "$image" --output "$out/x86-status.txt"
python3 - "$out/x86-status.txt" <<'PY'
from pathlib import Path
import sys
rows=[line.split() for line in Path(sys.argv[1]).read_text().splitlines()]
assert len(rows)==4096 and rows[0]==['00000000','00000000','00000000','00','00']
assert all(row[4]=='00' for row in rows)
print('PASS: x86 x87 flags clear in all 4,096 cases; zero-input MXCSR clear')
PY
