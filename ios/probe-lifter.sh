#!/usr/bin/env bash
# Lift a synthetic x86-64 addition and cross-compile it to an iOS object.
set -euo pipefail
if [[ $# -ne 3 ]]; then
    echo 'Usage: ios/probe-lifter.sh REMILL_LIFT LLVM_PREFIX EXTERNAL_OUTPUT_DIR' >&2
    exit 2
fi
lifter=$1
llvm_prefix=$2
output=$3
python3 - "$output" <<'PY'
from pathlib import Path
import sys
p = Path(sys.argv[1]).resolve()
if len(p.parts) < 3 or p.parts[1] != 'Volumes' or not Path('/Volumes', p.parts[2]).is_mount():
    raise SystemExit('Output must be on a mounted external disk')
PY
mkdir -p "$output"
# mov rax,rdi; add rax,rsi; ret. These bytes are synthetic, independent of game code.
"$lifter" --os linux --arch amd64 --bytes 4889f84801f0c3 --address 4096 \
    --signature 'rax(rdi,rsi)' --ir_out "$output/add.ll" --bc_out "$output/add.bc" \
    > "$output/lift.log" 2>&1
"$llvm_prefix/bin/clang" -target arm64-apple-ios18.0 \
    -isysroot "$(xcrun --sdk iphoneos --show-sdk-path)" -O2 \
    -c "$output/add.ll" -o "$output/add.o" > "$output/compile.log" 2>&1
file "$output/add.o"
"$llvm_prefix/bin/llvm-nm" --undefined-only "$output/add.o" > "$output/required-runtime.txt"
cat "$output/required-runtime.txt"
