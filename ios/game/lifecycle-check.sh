#!/bin/bash
# Link the actual cached translated runtime; never build/install onto a device.
set -euo pipefail
if [ "$#" -lt 2 ] || [ "$#" -gt 4 ]; then
  echo 'usage: lifecycle-check.sh EXTERNAL_GAME_BUILD EXTERNAL_TEST_OUTPUT [paused_sessions=20] [cycle_sessions=5]' >&2
  exit 2
fi
source_dir=$(cd "$(dirname "$0")" && pwd)
build=$1
output=$2
for path in "$build" "$output"; do
  case "$path" in
    /Volumes/office/*) test -d /Volumes/office && mount | /usr/bin/grep -q ' on /Volumes/office ' ;;
    /Volumes/Office/*) test -d /Volumes/Office && mount | /usr/bin/grep -q ' on /Volumes/Office ' ;;
    /Volumes/phil/*) test -d /Volumes/phil && mount | /usr/bin/grep -q ' on /Volumes/phil ' ;;
    *) echo 'Build and test paths must be on a mounted external disk' >&2; exit 2 ;;
  esac
done
archive=$build/mac/libcpu-fixtures.a
freetype=$build/mac/libfreetype.a
if [ ! -f "$archive" ] || [ ! -f "$freetype" ]; then
  echo 'Missing Mac runtime/FreeType archives; rebuild ios/game/build.py first' >&2
  exit 2
fi
mkdir -p "$output"
extra_libraries=()
if [[ -f "$build/mac/libdcp-fp80.a" ]]; then extra_libraries+=("$build/mac/libdcp-fp80.a"); fi
xcrun clang++ -target arm64-apple-macos15.0 -std=c++17 -O2 -Wall -Wextra -Werror -pthread \
  "$source_dir/lifecycle-fixture.cpp" "$archive" "$freetype" "${extra_libraries[@]}" -o "$output/lifecycle-fixture"
"$output/lifecycle-fixture" "$output" "${3:-20}" "${4:-5}" | tee "$output/lifecycle-check.log"
