#!/usr/bin/env python3
"""Test experimental native-boundary fenv isolation; no runtime integration."""
import argparse, hashlib, json, os, subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--review',type=Path,required=True);p.add_argument('--remill',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
out=a.output.resolve();assert len(out.parts)>2 and out.parts[1]=='Volumes' and Path('/Volumes',out.parts[2]).is_mount();out.mkdir(parents=True,exist_ok=False)
source=Path(__file__).resolve().parent;cpu=source.parents[1];review=a.review.resolve();meta=json.loads((review/'result-qualification.json').read_text());assert meta['result_bits_pass'];objects=[review/'fixed/runtime-native.o',review/'fixed/raw_addss-native.o'];compiler='/opt/homebrew/opt/llvm/bin/clang++'
def run(cmd):return subprocess.check_output(list(map(str,cmd)),text=True,stderr=subprocess.STDOUT,env=dict(os.environ))
results={}
for mode,flags in [('native',[]),('asan-ubsan',['-fsanitize=address,undefined']),('tsan',['-fsanitize=thread'])]:
 binary=out/mode;run([compiler,'-std=c++20','-O1','-g','-frounding-math','-pthread',*flags,source/'test.cpp','-o',binary]);results[mode]=run([binary]).strip();assert results[mode]=='checks=22000 failures=0'
for sdk,minimum in [('iphoneos','-miphoneos-version-min=18.0'),('iphonesimulator','-mios-simulator-version-min=18.0')]:
 path=run(['xcrun','--sdk',sdk,'--show-sdk-path']).strip();run(['xcrun','--sdk',sdk,'clang++','-std=c++20','-fsyntax-only','-isysroot',path,minimum,source/'test.cpp']);results[sdk]='syntax pass'
run([compiler,'-target','arm64-apple-macos15','-std=c++20','-O1','-frounding-math','-I'+str(cpu),'-I'+str(a.remill/'include'),source/'lifted-integration.cpp',*objects,'-o',out/'lifted-integration']);results['lifted_integration']=run([out/'lifted-integration']).strip();assert results['lifted_integration']=='lifted result=ffc00000 guest_mxcsr=00001f80 host_invalid_and_callback_divzero=1 native_overflow_isolated=1'
inputs=[source/'boundary.h',source/'test.cpp',source/'lifted-integration.cpp',*objects,review/'result-qualification.json'];j=dict(results=results,inputs={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in inputs},scope='Experimental unintegrated fenv boundary, Mac execution and iOS/Simulator syntax only. No guest MXCSR mapping/traps, DAZ/FTZ, HLE integration, gameplay or hardware proof.')
(out/'result.json').write_text(json.dumps(j,indent=2)+'\n');print(json.dumps(results))
