#!/bin/bash
set -euo pipefail

source_dir="$(cd "$(dirname "$0")/../linux-port" && pwd)"
build_dir=""
mode="probes"
while (($#)); do
    case "$1" in
        --build-dir) build_dir="${2:?--build-dir needs a path}"; shift 2 ;;
        --mode) mode="${2:?--mode needs probes, headless or rendered}"; shift 2 ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
done
if [[ -z "$build_dir" ]]; then
    echo "Usage: macos/build.sh --build-dir <external build path> [--mode probes|headless|rendered]" >&2
    exit 2
fi
case "$mode" in
    probes) options=(-DDCP_PROBES_ONLY=ON -DDCP_RENDER=OFF) ;;
    headless) options=(-DDCP_PROBES_ONLY=OFF -DDCP_RENDER=OFF) ;;
    rendered) options=(-DDCP_PROBES_ONLY=OFF -DDCP_RENDER=ON) ;;
    *) echo "Unknown mode: $mode" >&2; exit 2 ;;
esac
cmake -S "$source_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=RelWithDebInfo     -DCMAKE_OSX_ARCHITECTURES=x86_64 "${options[@]}"
cmake --build "$build_dir" --parallel "$(sysctl -n hw.logicalcpu)"
