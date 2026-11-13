#!/bin/bash
set -euo pipefail

build_dir=""
dump=""
state_dir=""
frames="3600"
input=""
while (($#)); do
    case "$1" in
        --build-dir) build_dir="${2:?missing build directory}"; shift 2 ;;
        --dump) dump="${2:?missing dump directory}"; shift 2 ;;
        --state-dir) state_dir="${2:?missing state directory}"; shift 2 ;;
        --frames) frames="${2:?missing frame count}"; shift 2 ;;
        --input) input="${2:?missing input script}"; shift 2 ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
done
if [[ -z "$build_dir" || -z "$dump" || -z "$state_dir" ]]; then
    echo "Usage: macos/run-headless.sh --build-dir <build> --dump <dump> --state-dir <test state> [--frames N] [--input script]" >&2
    exit 2
fi
if [[ ! "$frames" =~ ^[1-9][0-9]*$ ]]; then
    echo "--frames must be a positive integer" >&2
    exit 2
fi
if [[ ! -x "$build_dir/dcp_run" || ! -f "$dump/eboot.bin" ]]; then
    echo "Runner or eboot.bin is missing" >&2
    exit 2
fi
mkdir -p "$state_dir/user" "$state_dir/saves"
state_dir="$(cd "$state_dir" && pwd)"
args=("$dump/eboot.bin" --frames "$frames")
if [[ -n "$input" ]]; then args+=(--input "$input"); fi
exec env ROSETTA_ADVERTISE_AVX="${ROSETTA_ADVERTISE_AVX:-1}"     DCP_USER_DIR="$state_dir/user" DCP_SAVE_ROOT="$state_dir/saves"     "$build_dir/dcp_run" "${args[@]}"
