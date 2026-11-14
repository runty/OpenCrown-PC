#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 1 ]]; then echo 'Usage: ios/game/test-ime.sh EXTERNAL_OUTPUT' >&2; exit 2; fi
source_root=$(cd "$(dirname "$0")/../.." && pwd)
output=$1
python3 - "$output" <<'PY'
from pathlib import Path
import sys
p = Path(sys.argv[1]).resolve()
if len(p.parts)<3 or p.parts[1]!='Volumes' or not Path('/Volumes',p.parts[2]).is_mount():
    raise SystemExit('IME output must be on a mounted external disk')
PY
mkdir -p "$output"
sources=("$source_root/ios/game/ime-fixture.cpp" "$source_root/ios/game/ime-services.cpp")
flags=(-std=c++17 -O2 -g -Wall -Wextra -Werror -fno-omit-frame-pointer)
xcrun clang++ "${flags[@]}" "${sources[@]}" -o "$output/ime"
"$output/ime" > "$output/results.json"
xcrun clang++ "${flags[@]}" -fsanitize=address,undefined "${sources[@]}" -o "$output/ime-asan-ubsan"
"$output/ime-asan-ubsan" > "$output/asan-ubsan-results.json" 2> "$output/asan-ubsan.log"
xcrun clang++ "${flags[@]}" -fsanitize=thread "${sources[@]}" -o "$output/ime-tsan"
"$output/ime-tsan" > "$output/tsan-results.json" 2> "$output/tsan.log"
for platform in mac ios simulator; do
    case "$platform" in
        mac) sdk=macosx; target=arm64-apple-macos15 ;;
        ios) sdk=iphoneos; target=arm64-apple-ios18 ;;
        simulator) sdk=iphonesimulator; target=arm64-apple-ios18-simulator ;;
    esac
    mkdir -p "$output/$platform"
    for name in ime-services ime-fixture; do
        xcrun --sdk "$sdk" clang++ "${flags[@]}" -target "$target" -isysroot "$(xcrun --sdk "$sdk" --show-sdk-path)" -c "$source_root/ios/game/$name.cpp" -o "$output/$platform/$name.o"
    done
done
cat "$output/results.json" "$output/asan-ubsan-results.json" "$output/tsan-results.json"
