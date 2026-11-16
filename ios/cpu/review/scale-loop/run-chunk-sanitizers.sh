#!/bin/bash
set -euo pipefail
if [[ $# -ne 3 ]]; then echo 'Usage: run-batch-sanitizers.sh OUT ORIGINAL_OBJECT REMILL_INCLUDE' >&2; exit 2; fi
out=$1; original=$2; include=$3
python3 - "$out" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]).resolve()
if len(p.parts)<3 or p.parts[1]!='Volumes' or not Path('/Volumes',p.parts[2]).is_mount():raise SystemExit('Output must be on a mounted external build disk')
PY
mkdir -p "$out"
src=$(cd "$(dirname "$0")" && pwd); cpu=$(cd "$src/../.." && pwd); cxx=/opt/homebrew/opt/llvm/bin/clang++
common=(-target arm64-apple-macos15 -std=c++20 -O1 -g -frounding-math -ffp-contract=off -fno-omit-frame-pointer -I"$cpu" -I"$include")
"$cxx" "${common[@]}" -fsanitize=address,undefined -c "$src/batch-chunk-runtime.cpp" -o "$out/memory-asan.o"
"$cxx" "${common[@]}" -fsanitize=address,undefined -DDCP_BATCH_TEST "$src/fullstate.cpp" "$out/memory-asan.o" "$original" -o "$out/fullstate-asan"
"$out/fullstate-asan" > "$out/fullstate-asan.txt"
"$cxx" "${common[@]}" -fsanitize=address,undefined -DDCP_BATCH_TEST_POLL -c "$src/batch-chunk-runtime.cpp" -o "$out/hook-asan.o"
"$cxx" "${common[@]}" -fsanitize=address,undefined "$src/signal-reentry.cpp" "$out/hook-asan.o" "$original" -o "$out/reentry-asan"
"$out/reentry-asan" > "$out/reentry-asan.txt"
"$cxx" "${common[@]}" -fsanitize=address,undefined "$src/chunk-boundaries.cpp" "$out/memory-asan.o" "$original" -o "$out/chunk-boundaries-asan"
"$out/chunk-boundaries-asan" > "$out/chunk-boundaries-asan.txt"
"$cxx" "${common[@]}" -fsanitize=address,undefined "$src/chunk-reentry.cpp" "$out/hook-asan.o" "$original" -o "$out/chunk-reentry-asan"
"$out/chunk-reentry-asan" > "$out/chunk-reentry-asan.txt"
"$cxx" "${common[@]}" -fsanitize=address,undefined "$src/guard.cpp" "$out/hook-asan.o" "$original" -o "$out/guard-asan"
"$out/guard-asan" > "$out/guard-asan.txt"
"$cxx" "${common[@]}" -fsanitize=thread -c "$src/batch-chunk-runtime.cpp" -o "$out/memory-tsan.o"
"$cxx" "${common[@]}" -fsanitize=thread -pthread "$src/batch-concurrency.cpp" "$out/memory-tsan.o" "$original" -o "$out/concurrency-tsan"
"$out/concurrency-tsan" > "$out/concurrency-tsan.txt"
"$cxx" "${common[@]}" -fsanitize=thread -pthread "$src/contention.cpp" "$out/memory-tsan.o" "$original" -o "$out/contention-tsan"
"$out/contention-tsan" 1000 > "$out/contention-tsan.txt"
printf 'PASS: ASan/UBSan full-state and reentry; TSan atomic/snapshot fixture\n'
