#!/usr/bin/env python3
"""Compare checked glyph publication with original rendering and guest LOCK."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--output', required=True, type=Path)
p.add_argument('--backend-root', required=True, type=Path)
p.add_argument('--font-source', required=True, type=Path)
p.add_argument('--font-build', required=True, type=Path)
p.add_argument('--font', required=True, type=Path)
a = p.parse_args()
out = a.output.resolve()
if len(out.parts) < 3 or out.parts[1] != 'Volumes' or not Path('/Volumes', out.parts[2]).is_mount():
    p.error('Output must be on a mounted external disk')
if not a.font.is_file(): p.error('Caller-supplied font file is missing')
out.mkdir(parents=True, exist_ok=True)
source = Path(__file__).resolve().parents[2]
files = [source/'ios/cpu/runtime.cpp', source/'ios/game/font-services.cpp', source/'ios/game/font-memory-fixture.cpp']
headers = [source/'ios/cpu/runtime.h', source/'ios/game/font-services.h', source/'linux-port/src/hle/font.cpp']
report = dict(sources={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in files+headers},
              font_sha256=hashlib.sha256(a.font.read_bytes()).hexdigest(), runs={}, sdk_compiles={})
flags = ['-std=c++17','-O1','-g','-pthread','-fno-omit-frame-pointer','-Wall','-Wextra','-Werror',
         '-I'+str(a.backend_root.resolve()/'remill/include'),'-I'+str(source/'linux-port/src'),
         '-I'+str(a.font_source.resolve()/'include')]
def save(): (out/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
def build(command, tag):
    with (out/(tag+'-build.log')).open('w') as f:
        subprocess.run(list(map(str,command)),stdout=f,stderr=subprocess.STDOUT,check=True,timeout=240)
for mode,san in [('native',[]),('asan-ubsan',['-fsanitize=address,undefined']),('tsan',['-fsanitize=thread'])]:
    binary=out/('font-memory-'+mode)
    build(['xcrun','clang++',*flags,*san,'-I'+str(a.font_build.resolve()/'freetype-mac/include'),
           *files,a.font_build/'freetype-mac/libfreetype.a','-o',binary],mode)
    run=subprocess.run([binary,a.font],capture_output=True,text=True,timeout=120,
        env=dict(os.environ,ASAN_OPTIONS='abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1',TSAN_OPTIONS='halt_on_error=1'))
    (out/(mode+'-stdout.log')).write_text(run.stdout)
    (out/(mode+'-stderr.log')).write_text(run.stderr)
    report['runs'][mode]=dict(exit_code=run.returncode,stdout=run.stdout,stderr=run.stderr)
    save();print(mode,run.returncode,run.stdout.strip(),flush=True)
    if run.returncode: raise SystemExit('Font memory check failed: '+mode)
negative=subprocess.run([out/'font-memory-native',a.font,'--unsafe-max'],capture_output=True,text=True,timeout=120)
report['negative_control']=dict(exit_code=negative.returncode,stdout=negative.stdout,stderr=negative.stderr)
save()
if negative.returncode!=1 or 'native glyph output crossed original LOCK' not in negative.stderr:
    raise SystemExit('Unlocked glyph publication negative control was not detected')
print('unlocked glyph publication correctly rejected',flush=True)
for platform,sdk,target in [('ios','iphoneos','arm64-apple-ios18'),('simulator','iphonesimulator','arm64-apple-ios18-simulator')]:
    directory=out/platform;directory.mkdir(exist_ok=True)
    sdk_path=subprocess.check_output(['xcrun','--sdk',sdk,'--show-sdk-path'],text=True).strip()
    for file in files:
        build(['xcrun','--sdk',sdk,'clang++','-target',target,'-isysroot',sdk_path,*flags,
               '-I'+str(a.font_build.resolve()/('freetype-'+platform)/'include'),
               '-c',file,'-o',directory/(file.stem+'.o')],platform+'-'+file.stem)
    report['sdk_compiles'][platform]=dict(target=target,sources=[str(f) for f in files])
    save();print(platform,'compiled',flush=True)
