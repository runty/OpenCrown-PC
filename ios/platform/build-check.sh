#!/bin/bash
# Compile and link platform adapters without signing, installing, or launching.
set -euo pipefail
source_dir=$(cd "$(dirname "$0")" && pwd)
out=${1:-/Volumes/office/Build/OpenCrown/platform-probe}
case "$out" in
  /Volumes/office/*) test -d /Volumes/office && mount | /usr/bin/grep -q ' on /Volumes/office ' ;;
  /Volumes/Office/*) test -d /Volumes/Office && mount | /usr/bin/grep -q ' on /Volumes/Office ' ;;
  /Volumes/phil/*) test -d /Volumes/phil && mount | /usr/bin/grep -q ' on /Volumes/phil ' ;;
  *) echo 'Output must be on a mounted external build disk' >&2; exit 1 ;;
esac
mkdir -p "$out"
clang++ -std=c++17 -O2 -Wall -Wextra -Werror -pthread "$source_dir/core.cpp" "$source_dir/probe.cpp" -o "$out/core-probe"
"$out/core-probe" | tee "$out/core-result.txt"
clang++ -std=c++17 -O2 -Wall -Wextra -Werror -pthread "$source_dir/core.cpp" "$source_dir/pointer-probe.cpp" -o "$out/pointer-probe"
"$out/pointer-probe" | tee "$out/pointer-result.txt"
for sdk in iphoneos iphonesimulator; do
  target=arm64-apple-ios18.0
  if [ "$sdk" = iphonesimulator ]; then target=$target-simulator; fi
  sysroot=$(xcrun --sdk "$sdk" --show-sdk-path)
  mkdir -p "$out/$sdk"
  flags=(-target "$target" -isysroot "$sysroot" -std=c++17 -O2 -Wall -Wextra -Werror)
  xcrun --sdk "$sdk" clang++ "${flags[@]}" -c "$source_dir/core.cpp" -o "$out/$sdk/core.o"
  xcrun --sdk "$sdk" clang++ "${flags[@]}" -fobjc-arc -fblocks -c "$source_dir/platform.mm" -o "$out/$sdk/platform.o"
  xcrun --sdk "$sdk" ar rcs "$out/$sdk/libdcp-platform.a" "$out/$sdk/core.o" "$out/$sdk/platform.o"
  printf '%s\n' '#include "platform.h"' 'int main() { DcpPlatformAudioStats s; dcp_platform_audio_stats(&s); return 0; }' > "$out/link-probe.cpp"
  xcrun --sdk "$sdk" clang++ "${flags[@]}" -I "$source_dir" "$out/link-probe.cpp" "$out/$sdk/libdcp-platform.a" \
    -framework Foundation -lobjc -framework UIKit -framework GameController -framework AVFoundation -framework AudioToolbox -o "$out/$sdk/link-probe"
  echo "PASS: $sdk arm64 iOS18 compile and framework link"
done
