#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo 'Usage: ios/game/test-headless.sh EXTERNAL_OUTPUT [REAL_GAME_BUILD]' >&2
  exit 2
fi
source_root=$(cd "$(dirname "$0")/../.." && pwd)
output=$1
python3 - "$output" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]).resolve()
if len(p.parts)<3 or p.parts[1]!='Volumes' or not Path('/Volumes',p.parts[2]).is_mount():
    raise SystemExit('Headless test output must be on a mounted external disk')
PY
mkdir -p "$output"
flags=(-std=c++17 -O2 -g -Wall -Wextra -Werror -I"$source_root/linux-port/src")
sources=("$source_root/ios/game/scripted-input-fixture.cpp" "$source_root/ios/game/offline-services.cpp" "$source_root/linux-port/src/hle/kernel/pure.cpp")
xcrun clang++ "${flags[@]}" "$source_root/ios/game/headless-config-fixture.cpp" -o "$output/config"
"$output/config" > "$output/config-results.json"
xcrun clang++ "${flags[@]}" "$source_root/ios/game/vblank-clock-fixture.cpp" -o "$output/vblank-clock"
"$output/vblank-clock" > "$output/vblank-clock-results.json"
xcrun clang++ "${flags[@]}" "${sources[@]}" -o "$output/input"
"$output/input" "$output/saves" > "$output/input-results.json"
xcrun clang++ "${flags[@]}" -fsanitize=address,undefined "${sources[@]}" -o "$output/input-asan-ubsan"
"$output/input-asan-ubsan" "$output/asan-saves" > "$output/asan-ubsan-results.json" 2> "$output/asan-ubsan.log"
for platform in mac ios simulator; do
  case "$platform" in
    mac) sdk=macosx; target=arm64-apple-macos15 ;;
    ios) sdk=iphoneos; target=arm64-apple-ios18 ;;
    simulator) sdk=iphonesimulator; target=arm64-apple-ios18-simulator ;;
  esac
  mkdir -p "$output/$platform"
  for path in "${sources[@]}" "$source_root/ios/game/headless-config-fixture.cpp"; do
    name=$(basename "$path" .cpp)
    xcrun --sdk "$sdk" clang++ "${flags[@]}" -target "$target" -isysroot "$(xcrun --sdk "$sdk" --show-sdk-path)" -c "$path" -o "$output/$platform/$name.o"
  done
done
if [[ $# == 2 ]]; then
  python3 - "$output" "$2" <<'PY'
from pathlib import Path
import hashlib,json,os,subprocess,sys
root=Path(sys.argv[1]);game=Path(sys.argv[2]);checks=0
scripts={'valid':b'10 CROSS\n20 0\n','empty':b'','invalid':b'10 UNKNOWN_BUTTON\n','oversize':b'#'*(1024*1024+1)}
for name,data in scripts.items():(root/(name+'.txt')).write_bytes(data)
def run(name, settings):
    global checks
    env={k:v for k,v in os.environ.items() if k not in ['DCP_CHECKPOINT_SCENE','DCP_MAX_TRACE_ENTRIES','DCP_MAX_VBLANKS','DCP_CHECKPOINT_SETTLE_FRAMES','DCP_INPUT_SCRIPT']}
    env.update(DCP_GAME_MODE='checkpoint',DCP_SAVE_ROOT=str(root/('runtime-saves-'+name)),**settings)
    result=subprocess.run([str(game/'game-boot')],env=env,capture_output=True,text=True,timeout=30,check=True)
    report=json.loads(result.stdout);(root/(name+'-runtime.json')).write_text(json.dumps(report,indent=2)+'\n')
    assert not report['booted'] and not report['checkpoint_complete']
    checks+=2
    return report
a=run('inn-budget',{'DCP_CHECKPOINT_SCENE':'INN','DCP_MAX_TRACE_ENTRIES':'1','DCP_MAX_VBLANKS':'100'})
assert a['checkpoint_scene']=='INN' and 'trace budget' in a['error'];checks+=2
a=run('bad-scene',{'DCP_CHECKPOINT_SCENE':'../TITLE'})
assert 'Invalid DCP_CHECKPOINT_SCENE' in a['error'] and a['steps']==0;checks+=2
for name in ['valid','empty']:
    a=run(name,{'DCP_INPUT_SCRIPT':str(root/(name+'.txt')),'DCP_MAX_TRACE_ENTRIES':'1'})
    assert a['input_script_sha256']==hashlib.sha256(scripts[name]).hexdigest()
    assert 'trace budget' in a['error'];checks+=2
for name,reason in [('invalid','Invalid input script line 1'),('oversize','exceeds 1 MiB')]:
    a=run(name,{'DCP_INPUT_SCRIPT':str(root/(name+'.txt'))})
    assert reason in a['error'] and a['steps']==0;checks+=2
a=run('long-settle',{'DCP_CHECKPOINT_SETTLE_FRAMES':'600','DCP_MAX_TRACE_ENTRIES':'1'})
assert a['checkpoint_settle_frames']==600 and 'trace budget' in a['error'];checks+=2
a=run('short-settle',{'DCP_CHECKPOINT_SETTLE_FRAMES':'119'})
assert 'Invalid DCP_CHECKPOINT_SETTLE_FRAMES' in a['error'] and a['steps']==0;checks+=2
(root/'runtime-results.json').write_text(json.dumps({'checks':checks,'failed':0,'cases':8,'boot_acceptance':False},indent=2)+'\n')
print((root/'runtime-results.json').read_text())
PY
fi
cat "$output/config-results.json" "$output/input-results.json" "$output/asan-ubsan-results.json"
