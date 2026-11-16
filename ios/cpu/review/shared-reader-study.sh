#!/bin/bash
# Isolated host study. Does not alter a backend, game or device.
set -euo pipefail
if [[ $# -ne 2 ]]; then echo 'Usage: shared-reader-study.sh NEW_OUTPUT BACKEND_ROOT' >&2; exit 2; fi
out=$1; backend=$2
python3 - "$out" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]).resolve()
if len(p.parts)<3 or p.parts[1]!='Volumes' or not Path('/Volumes',p.parts[2]).is_mount():raise SystemExit('output must be on a mounted external disk')
PY
mkdir "$out"
review=$(cd "$(dirname "$0")" && pwd)
cpu=$(cd "$review/.." && pwd)
mkdir -p "$out/source/ios/cpu/review"
cp "$cpu/runtime.cpp" "$cpu/runtime.h" "$out/source/ios/cpu/"
cp "$review/memory-cache.py" "$review/memory-cache.cpp" "$review/snapshots.py" "$review/snapshots.cpp" "$out/source/ios/cpu/review/"
patch -s -p1 -d "$out/source" < "$review/shared-reader-study.patch"
python3 "$out/source/ios/cpu/review/memory-cache.py" --output "$out/memory-tests" --backend-root "$backend"
python3 "$out/source/ios/cpu/review/snapshots.py" --output "$out/snapshot-tests" --backend-root "$backend"
include="$backend/remill/include"
for mode in native tsan; do
  if [[ "$mode" == tsan ]]; then flags=(-fsanitize=thread -O1 -g); else flags=(-O2); fi
  xcrun clang++ -std=c++17 -pthread "${flags[@]}" -I"$out/source/ios/cpu" -I"$include" "$review/shared-reader-lock-fixture.cpp" -o "$out/lock-$mode"
  "$out/lock-$mode" > "$out/lock-$mode.txt"
done
xcrun clang++ -std=c++17 -O2 -pthread -I"$cpu" -I"$include" "$cpu/runtime.cpp" "$review/shared-reader-contention.cpp" -o "$out/baseline-bench"
xcrun clang++ -std=c++17 -O2 -pthread -I"$out/source/ios/cpu" -I"$include" "$out/source/ios/cpu/runtime.cpp" "$review/shared-reader-contention.cpp" -o "$out/shared-bench"
python3 - "$out" <<'PY'
from pathlib import Path
import json,statistics,subprocess,sys
out=Path(sys.argv[1]);runs={'baseline':[],'shared':[]}
for i in range(6):
 for name in (('baseline','shared') if i%2==0 else ('shared','baseline')):
  runs[name].append(json.loads(subprocess.check_output([str(out/(name+'-bench'))],text=True,timeout=120)))
summary={name:{key:statistics.median(x[key] for x in values) for key in ('single_ns','mixed_ns','writer_max_ns')} for name,values in runs.items()}
(out/'contention-results.json').write_text(json.dumps(dict(summary=summary,runs=runs,scope='Synthetic Mac; same-address readers and writer, not game or device performance'),indent=2)+'\n')
print(json.dumps(summary,indent=2))
PY
for sdk in iphoneos iphonesimulator; do
  target=arm64-apple-ios18
  if [[ "$sdk" == iphonesimulator ]]; then target=arm64-apple-ios18-simulator; fi
  xcrun --sdk "$sdk" clang++ -target "$target" -isysroot "$(xcrun --sdk "$sdk" --show-sdk-path)" -std=c++17 -fsyntax-only -I"$out/source/ios/cpu" -I"$include" "$out/source/ios/cpu/runtime.cpp"
done
