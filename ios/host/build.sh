#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 3 ]]; then
    echo 'Usage: ios/host/build.sh EXTERNAL_OUTPUT FROZEN_GAME_CACHE MAC_RENDER_CACHE' >&2
    exit 2
fi
python3 - "$1" "$2" "$3" <<'PY'
from pathlib import Path
import sys
for argument in sys.argv[1:]:
    path=Path(argument).resolve()
    if len(path.parts)<3 or path.parts[1]!='Volumes' or not Path('/Volumes',path.parts[2]).is_mount():
        raise SystemExit('Host build and cache paths must be on mounted external disks')
PY
source_dir=$(cd "$(dirname "$0")" && pwd)
cmake -G Ninja -S "$source_dir" -B "$1" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DDCP_GAME_CACHE="$2" -DDCP_RENDER_CACHE="$3"
cmake --build "$1" --parallel 8
