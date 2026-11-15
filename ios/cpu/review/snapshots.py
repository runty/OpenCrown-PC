#!/usr/bin/env python3
"""Verify owned snapshot versions, all writers, unknown escapes and wrap fallback."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p=argparse.ArgumentParser()
p.add_argument('--output',type=Path,required=True)
p.add_argument('--backend-root',type=Path,required=True)
a=p.parse_args();out=a.output.resolve();cpu=Path(__file__).resolve().parents[1]
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount():p.error('Output must be on a mounted external disk')
out.mkdir(parents=True,exist_ok=True)
fixture=Path(__file__).with_suffix('.cpp')
sources=[cpu/'runtime.cpp',cpu/'runtime.h',fixture]
report=dict(sources={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in sources},runs={})
flags=['-std=c++17','-O1','-g','-fno-omit-frame-pointer','-pthread','-Wall','-Wextra','-Werror','-I'+str(cpu),'-I'+str(a.backend_root.resolve()/'remill/include')]
env=dict(os.environ,ASAN_OPTIONS='abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1',TSAN_OPTIONS='halt_on_error=1')
def save():(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
def build(source,mode,extra):
 binary=out/('snapshots-'+mode)
 with (out/(mode+'-build.log')).open('w') as log:subprocess.run(list(map(str,['xcrun','clang++',*flags,*extra,source,fixture,'-o',binary])),check=True,stdout=log,stderr=subprocess.STDOUT,timeout=240)
 return binary
for mode,extra in [('native',[]),('asan-ubsan',['-fsanitize=address,undefined']),('tsan',['-fsanitize=thread']),('wrap',['-DDCP_SNAPSHOT_TEST_VERSION_LIMIT=7'])]:
 binary=build(cpu/'runtime.cpp',mode,extra)
 run=subprocess.run([binary],env=env,text=True,capture_output=True,timeout=120)
 (out/(mode+'-stdout.log')).write_text(run.stdout);(out/(mode+'-stderr.log')).write_text(run.stderr)
 report['runs'][mode]=dict(exit_code=run.returncode,stdout=run.stdout,stderr=run.stderr);save();print(mode,run.returncode,run.stdout.strip(),flush=True)
 if run.returncode:raise SystemExit('Snapshot check failed: '+mode)
text=(cpu/'runtime.cpp').read_text()
needle='changed(pointer, sizeof(T));\n  std::memcpy(pointer, &value, sizeof(value));'
assert text.count(needle)==1
negative=out/'runtime-negative.cpp';negative.write_text(text.replace(needle,'/* fixture negative: omitted scalar invalidation */\n  std::memcpy(pointer, &value, sizeof(value));'))
binary=build(negative,'negative',[])
run=subprocess.run([binary],env=env,text=True,capture_output=True,timeout=120)
report['negative_control']=dict(exit_code=run.returncode,stdout=run.stdout,stderr=run.stderr);save()
if run.returncode!=1 or 'store invalidation missed' not in run.stderr:raise SystemExit('Missing writer negative control not detected')
print('omitted writer correctly rejected; negative source never enters a game',flush=True)
