#!/usr/bin/env python3
"""Reproduce the private fixed-leaf experiment; never modifies game inputs."""
import argparse, hashlib, json, os, subprocess
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--game', type=Path, required=True)
p.add_argument('--backend', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--iterations', type=int, default=2000)
a = p.parse_args()
out = a.output.resolve()
if len(out.parts) < 3 or out.parts[1] != 'Volumes' or not Path('/Volumes', out.parts[2]).is_mount():
    p.error('output must be on a mounted external disk')
if a.iterations < 0: p.error('iterations must be nonnegative')
out.mkdir(parents=True, exist_ok=True)
source = Path(__file__).resolve().parent
cpu = source.parents[1]
inputs = {'trace': a.game/'traces/206364d0.ll', 'lowered': a.game/'vector-memory/206364d0.ll',
          'image': a.game/'eboot.image'}
expected = {'trace':'fba730e66c7bcc3ba7d01f472327e725ff23c8de73919a0a6b127ba647076084',
            'lowered':'caf7468faf0357aa3af15eb29c83291068a85ffcc70ae8faff4292772a790a10',
            'image':'e8cd71e2586ab1e8c91f705ed78c9fe7174abf385622b531c96f318210b06f02'}
for name, path in inputs.items():
    if hashlib.sha256(path.read_bytes()).hexdigest() != expected[name]:
        raise SystemExit(f'{name} input hash does not match the prior lowering')
reference = out/'reference.ll'
text = inputs['trace'].read_text()
assert text.count('@sub_206364d0') > 0
reference.write_text(text.replace('@sub_206364d0', '@reference_sub_206364d0'))
sdk = subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'], text=True).strip()
clang='/opt/homebrew/opt/llvm/bin/clang'
compiler=clang+'++'
manifest = {'inputs':expected, 'source':{str(x):hashlib.sha256(x.read_bytes()).hexdigest()
    for x in [source/'leaf-run.py',source/'leaf-harness.cpp',source/'leaf-helpers.cpp',
              source/'read256-study.cpp',cpu/'runtime.cpp',cpu/'runtime.h']}, 'runs':{}}
for mode, flags in [('native',[]),('asan-ubsan',['-fsanitize=address,undefined']),('tsan',['-fsanitize=thread'])]:
    objects=[]
    with (out/(mode+'-build.log')).open('w') as log:
        for label, ir in [('reference',reference),('coalesced',inputs['lowered'])]:
            obj=out/(mode+'-'+label+'.o')
            subprocess.run([clang,'-target','arm64-apple-macos14','-isysroot',sdk,'-O2',
                '-funwind-tables',*flags,'-c',str(ir),'-o',str(obj)],stdout=log,stderr=subprocess.STDOUT,check=True)
            objects.append(obj)
        exe=out/mode
        subprocess.run([compiler,'-isysroot',sdk,'-std=c++17','-O2','-g','-pthread',*flags,
            '-I'+str(cpu),'-I'+str(a.backend/'remill/include'),str(source/'leaf-helpers.cpp'),
            str(source/'leaf-harness.cpp'),*[str(x) for x in objects],'-o',str(exe)],
            stdout=log,stderr=subprocess.STDOUT,check=True)
    env=dict(os.environ,ASAN_OPTIONS='abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1',TSAN_OPTIONS='halt_on_error=1')
    command=[str(exe),str(inputs['image'])]
    if mode == 'native' and a.iterations: command.append(str(a.iterations))
    result=subprocess.run(command,capture_output=True,text=True,env=env,timeout=180)
    (out/(mode+'-stdout.log')).write_text(result.stdout)
    (out/(mode+'-stderr.log')).write_text(result.stderr)
    manifest['runs'][mode]={'exit_code':result.returncode,'records':[json.loads(x) for x in result.stdout.splitlines()]}
    (out/'results.json').write_text(json.dumps(manifest,indent=2)+'\n')
    result.check_returncode()
    print(mode, 'passed', flush=True)
