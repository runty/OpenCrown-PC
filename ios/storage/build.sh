#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 1 ]]; then echo 'Usage: ios/storage/build.sh EXTERNAL_OUTPUT' >&2; exit 2; fi
source_root=$(cd "$(dirname "$0")/../.." && pwd)
output=$1
python3 - "$output" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]).resolve()
if len(p.parts)<3 or p.parts[1]!='Volumes' or not Path('/Volumes',p.parts[2]).is_mount():
    raise SystemExit('Storage output must be on a mounted external disk')
PY
mkdir -p "$output"
for platform in mac ios simulator; do
    case "$platform" in
        mac) sdk=macosx; target=arm64-apple-macos15 ;;
        ios) sdk=iphoneos; target=arm64-apple-ios18 ;;
        simulator) sdk=iphonesimulator; target=arm64-apple-ios18-simulator ;;
    esac
    mkdir -p "$output/$platform"
    flags=(-target "$target" -isysroot "$(xcrun --sdk "$sdk" --show-sdk-path)" -std=c++20 -O2 -Wall -Wextra -Werror -I"$source_root/linux-port/src")
    xcrun --sdk "$sdk" clang++ "${flags[@]}" -c "$source_root/ios/storage/storage-core.cpp" -o "$output/$platform/core.o"
    xcrun --sdk "$sdk" clang++ "${flags[@]}" -c "$source_root/linux-port/src/formats/utf_table.cpp" -o "$output/$platform/utf.o"
    xcrun --sdk "$sdk" clang++ "${flags[@]}" -c "$source_root/ios/storage/save-transactions.cpp" -o "$output/$platform/saves.o"
    objects=("$output/$platform/core.o" "$output/$platform/utf.o" "$output/$platform/saves.o")
    if [[ $platform != mac ]]; then
        xcrun --sdk "$sdk" clang++ "${flags[@]}" -fobjc-arc -c "$source_root/ios/storage/storage-ui.mm" -o "$output/$platform/ui.o"
        objects+=("$output/$platform/ui.o")
        xcrun --sdk "$sdk" clang++ "${flags[@]}" -fobjc-arc -c "$source_root/ios/storage/save-transfer-ui.mm" -o "$output/$platform/transfer-ui.o"
        objects+=("$output/$platform/transfer-ui.o")
    fi
    xcrun libtool -static -o "$output/$platform/libstorage.a" "${objects[@]}"
done
xcrun clang++ -std=c++20 -O2 -Wall -Wextra -Werror "$source_root/ios/storage/tests.cpp" "$output/mac/libstorage.a" -o "$output/storage-tests"
xcrun clang++ -std=c++20 -O2 -Wall -Wextra -Werror "$source_root/ios/storage/validate.cpp" "$output/mac/libstorage.a" -o "$output/storage-validate"
"$output/storage-tests" "$output" | tee "$output/results.json"

xcrun clang++ -std=c++20 -O2 -Wall -Wextra -Werror "$source_root/ios/storage/save-service-integration.cpp" "$source_root/ios/game/file-services.cpp" "$source_root/ios/game/offline-services.cpp" "$source_root/linux-port/src/hle/kernel/pure.cpp" -I"$source_root/linux-port/src" "$output/mac/libstorage.a" -o "$output/save-service-integration"
"$output/save-service-integration" "$output" | tee "$output/save-service-results.json"
