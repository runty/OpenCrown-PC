#!/usr/bin/env bash
# Build the pinned caller-supplied FreeType source for headless Mac/iPhoneOS fonts.
set -euo pipefail
if [[ $# -ne 2 ]]; then
    echo 'Usage: ios/game/font-build.sh FREETYPE_SOURCE EXTERNAL_OUTPUT' >&2
    exit 2
fi
font_source=$1
build_root=$2
python3 - "$build_root" <<'PY'
from pathlib import Path
import sys
path = Path(sys.argv[1]).resolve()
if len(path.parts) < 3 or path.parts[1] != 'Volumes' or not Path('/Volumes', path.parts[2]).is_mount():
    raise SystemExit('Font build output must be on a mounted external disk')
PY
[[ -f "$font_source/include/ft2build.h" ]] || { echo 'FreeType source is missing' >&2; exit 1; }
common=(-G Ninja -S "$font_source" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
    -DBUILD_SHARED_LIBS=OFF -DFT_DISABLE_ZLIB=ON -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_PNG=ON
    -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON)
cmake "${common[@]}" -B "$build_root/freetype-mac" -DCMAKE_OSX_SYSROOT=macosx -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build "$build_root/freetype-mac" --parallel 8
cmake "${common[@]}" -B "$build_root/freetype-ios" -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_OSX_DEPLOYMENT_TARGET=18.0
cmake --build "$build_root/freetype-ios" --parallel 8
cp "$font_source/docs/FTL.TXT" "$build_root/FreeType-FTL.txt"
cp "$font_source/LICENSE.TXT" "$build_root/FreeType-LICENSE.txt"
