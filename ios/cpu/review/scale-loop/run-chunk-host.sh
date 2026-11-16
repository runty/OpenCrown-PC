#!/bin/bash
# Private standalone fixture; never installs a game or mutates a backend.
set -euo pipefail
if [[ $# -ne 5 ]]; then
  echo 'Usage: run-batch-host.sh OUT AMD_HOST GAME_IMAGE ORIGINAL_OBJECT REMILL_INCLUDE' >&2
  exit 2
fi
out=$1; reference_host=$2; image=$3; original=$4; include=$5
python3 - "$out" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]).resolve()
if len(p.parts)<3 or p.parts[1]!='Volumes' or not Path('/Volumes',p.parts[2]).is_mount():
 raise SystemExit('Output must be on a mounted external build disk')
PY
mkdir -p "$out"
source_dir=$(cd "$(dirname "$0")" && pwd)
cpu_dir=$(cd "$source_dir/../.." && pwd)
cxx=/opt/homebrew/opt/llvm/bin/clang++
common=(-target arm64-apple-macos15 -std=c++20 -frounding-math -ffp-contract=off -I"$cpu_dir" -I"$include")
"$cxx" "${common[@]}" -O2 -c "$source_dir/batch-chunk-runtime.cpp" -o "$out/memory.o"
for mode in lifted prototype; do
  args=(-DDCP_BATCH_TEST)
  if [[ $mode == prototype ]]; then args+=(-DDCP_SCALE_PROTOTYPE); fi
  "$cxx" "${common[@]}" -O2 "${args[@]}" "$source_dir/lifted.cpp" "$out/memory.o" "$original" -o "$out/$mode"
  "$out/$mode" > "$out/$mode-matrix.txt"
done
python3 "$source_dir/compare-amd.py" --host "$reference_host" --image "$image" --output "$out" > "$out/amd-comparison.txt"
"$cxx" "${common[@]}" -O2 -DDCP_BATCH_TEST "$source_dir/fullstate.cpp" "$out/memory.o" "$original" -o "$out/fullstate"
"$out/fullstate" > "$out/fullstate.txt"
"$cxx" "${common[@]}" -O2 "$source_dir/reentrant.cpp" "$out/memory.o" "$original" -o "$out/reentrant"
"$out/reentrant" > "$out/reentrant.txt"
python3 - "$out/reentrant.txt" <<'PY'
from pathlib import Path
import sys
s=Path(sys.argv[1]).read_text().splitlines()
assert s==['original exit=0 signal=0','batch exit=0 signal=0'],s
PY
"$cxx" "${common[@]}" -O2 -DDCP_BATCH_TEST_POLL -c "$source_dir/batch-chunk-runtime.cpp" -o "$out/hook.o"
"$cxx" "${common[@]}" -O2 "$source_dir/signal-reentry.cpp" "$out/hook.o" "$original" -o "$out/signal-reentry"
"$out/signal-reentry" > "$out/signal-reentry.txt"
"$cxx" "${common[@]}" -O2 -pthread "$source_dir/batch-concurrency.cpp" "$out/memory.o" "$original" -o "$out/concurrency"
"$out/concurrency" > "$out/concurrency.txt"
"$cxx" "${common[@]}" -O2 "$source_dir/chunk-boundaries.cpp" "$out/memory.o" "$original" -o "$out/chunk-boundaries"
"$out/chunk-boundaries" > "$out/chunk-boundaries.txt"
"$cxx" "${common[@]}" -O2 "$source_dir/chunk-reentry.cpp" "$out/hook.o" "$original" -o "$out/chunk-reentry"
"$out/chunk-reentry" > "$out/chunk-reentry.txt"
"$cxx" "${common[@]}" -O2 "$source_dir/guard.cpp" "$out/hook.o" "$original" -o "$out/guard"
"$out/guard" > "$out/guard.txt"
"$cxx" "${common[@]}" -O2 -pthread "$source_dir/contention.cpp" "$out/memory.o" "$original" -o "$out/contention"
"$out/contention" 20000 > "$out/contention.txt"
"$cxx" "${common[@]}" -O2 "$source_dir/batch-bench.cpp" "$out/memory.o" "$original" -o "$out/bench"
"$out/bench" > "$out/bench.txt"
for sdk in iphoneos iphonesimulator; do
  target=arm64-apple-ios18
  if [[ $sdk == iphonesimulator ]]; then target=arm64-apple-ios18-simulator; fi
  xcrun --sdk "$sdk" clang++ -target "$target" -isysroot "$(xcrun --sdk "$sdk" --show-sdk-path)" -std=c++20 -fsyntax-only -I"$cpu_dir" -I"$include" "$source_dir/batch-chunk-runtime.cpp"
done
printf 'PASS: AMD matrix, full state, reentrant callback, guest LOCK, snapshot and SDK syntax\n'
