#!/usr/bin/env bash
set -euo pipefail
if [[ $# != 1 ]]; then
  echo 'Usage: test-direct-memory.sh EXTERNAL_OUTPUT' >&2
  exit 2
fi
source_dir=$(cd "$(dirname "$0")" && pwd)
output=$1
python3 - "$output" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]).resolve()
if len(p.parts)<3 or p.parts[1]!='Volumes' or not Path('/Volumes',p.parts[2]).is_mount():
    raise SystemExit('Test output must be on a mounted external disk')
PY
mkdir -p "$output"
flags=(-std=c++17 -O2 -g -Wall -Wextra -Werror -UNDEBUG)
xcrun clang++ "${flags[@]}" "$source_dir/direct-memory-fixture.cpp" -o "$output/native"
"$output/native" > "$output/native.json"
xcrun clang++ "${flags[@]}" -fsanitize=thread "$source_dir/direct-memory-fixture.cpp" -o "$output/tsan"
"$output/tsan" > "$output/tsan.json" 2> "$output/tsan.log"
for platform in mac ios simulator; do
  case "$platform" in
    mac) sdk=macosx; target=arm64-apple-macos15 ;;
    ios) sdk=iphoneos; target=arm64-apple-ios18 ;;
    simulator) sdk=iphonesimulator; target=arm64-apple-ios18-simulator ;;
  esac
  xcrun --sdk "$sdk" clang++ "${flags[@]}" -target "$target" \
    -isysroot "$(xcrun --sdk "$sdk" --show-sdk-path)" \
    -c "$source_dir/direct-memory-fixture.cpp" -o "$output/$platform.o"
done
cat "$output/native.json" "$output/tsan.json"
