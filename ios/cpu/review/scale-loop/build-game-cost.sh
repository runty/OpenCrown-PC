#!/bin/bash
# Builds private matched Mac hosts; requires the previously audited guard archive.
set -euo pipefail
if [[ $# -ne 2 ]]; then echo 'Usage: build-game-cost.sh NEW_OUTPUT REMILL_INCLUDE' >&2; exit 2; fi
out=$1; include=$2
python3 - "$out" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]).resolve()
if len(p.parts)<3 or p.parts[1]!='Volumes' or not Path('/Volumes',p.parts[2]).is_mount():raise SystemExit('output must be on a mounted external disk')
PY
mkdir "$out"
mkdir "$out/baseline" "$out/chunk"
review=$(cd "$(dirname "$0")" && pwd)
cpu=$(cd "$review/../.." && pwd)
old=/Volumes/office/Build/OpenCrown/mobile-adventure-live/sse-nan-candidate/scale-guard-game
cxx=/opt/homebrew/opt/llvm/bin/clang++
flags=(-target arm64-apple-macos15 -std=c++20 -O2 -frounding-math -ffp-contract=off -I"$cpu" -I"$include")
"$cxx" "${flags[@]}" -c "$review/baseline-integration.cpp" -o "$out/baseline/memory.o"
"$cxx" "${flags[@]}" -DDCP_BATCH_INTEGRATION -c "$review/batch-chunk-runtime.cpp" -o "$out/chunk/memory.o"
for mode in baseline chunk; do
 cp -c "$old/mac/libcpu-fixtures.a" "$out/$mode/libcpu-fixtures.a"
 ar -r "$out/$mode/libcpu-fixtures.a" "$out/$mode/memory.o"
 xcrun ranlib "$out/$mode/libcpu-fixtures.a"
done
python3 "$review/archive-diff.py" --output "$out/archive-diff.json" "$out/baseline/libcpu-fixtures.a" "$out/chunk/libcpu-fixtures.a"
python3 - "$out" "$old" <<'PY'
import hashlib,json,shlex,subprocess,sys
from pathlib import Path
out,old=map(Path,sys.argv[1:]);diff=json.loads((out/'archive-diff.json').read_text())
assert diff['changed']==['memory.o'] and not diff['added'] and not diff['removed']
host=old/'host';args=shlex.split(subprocess.check_output(['ninja','-C',str(host),'-t','commands','opencrown-aot-host'],text=True).splitlines()[-1]);args=args[2:args.index('&&',2)]
for mode in ('baseline','chunk'):
 a=args.copy();a[a.index('-o')+1]=str(out/mode/'opencrown-aot-host')
 a=[str(out/mode/'libcpu-fixtures.a') if x==str(old/'mac/libcpu-fixtures.a') else x for x in a]
 subprocess.run(a,cwd=host,check=True);(out/mode/'link-command.txt').write_text(shlex.join(a)+'\n')
 print(mode,hashlib.sha256((out/mode/'opencrown-aot-host').read_bytes()).hexdigest())
PY
