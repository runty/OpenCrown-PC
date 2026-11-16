#!/usr/bin/env python3
"""Qualify bounded scalar masked denormal status against captured AMD output."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--review',type=Path,required=True);p.add_argument('--remill',type=Path,required=True);p.add_argument('--amd-broad',type=Path,required=True);p.add_argument('--amd-matrix',type=Path,required=True);p.add_argument('--amd-trap',type=Path,required=True);p.add_argument('--amd-boundary',type=Path,required=True);p.add_argument('--amd-system',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
out=a.output.resolve();assert len(out.parts)>2 and out.parts[1]=='Volumes' and Path('/Volumes',out.parts[2]).is_mount();out.mkdir(parents=True,exist_ok=False)
source=Path(__file__).resolve().parent;cpu=source.parents[1];review=a.review.resolve();mapping=source.parent/'mxcsr-mapping';assert json.loads((review/'result-qualification.json').read_text())['result_bits_pass'];cc='/opt/homebrew/opt/llvm/bin/clang++'
def run(cmd,stdin=None):return subprocess.check_output(list(map(str,cmd)),input=stdin,text=True,stderr=subprocess.STDOUT,env=dict(os.environ,ROSETTA_ADVERTISE_AVX='1'))
for name,path in [('broad',source/'broad-native.cpp'),('matrix',mapping/'native.cpp'),('boundary',mapping/'boundary-native.cpp')]:
 run(['xcrun','clang++','-arch','x86_64','-O1',path,'-o',out/('native-'+name)]);data=run([out/('native-'+name)]);(out/('rosetta-'+name+'.txt')).write_text(data);assert data==getattr(a,'amd_'+name).read_text(),name
assert a.amd_trap.read_text().strip()=='native_child_exit=77'
assert 'x86_64' in a.amd_system.read_text() and 'AMD Ryzen 7 7840HS' in a.amd_system.read_text()
results={}
for mode,flags in [('native',[]),('asan-ubsan',['-fsanitize=address,undefined'])]:
 run([cc,'-std=c++20','-O1','-g',*flags,source/'classify.cpp','-o',out/('classify-'+mode)]);results['classify-'+mode]=run([out/('classify-'+mode)],a.amd_broad.read_text()).strip();assert results['classify-'+mode]=='checks=4096 failures=0'
 suffix='native' if mode=='native' else 'asan-ubsan';objs=[review/'fixed'/('runtime-'+suffix+'.o'),*[review/'fixed'/('raw_'+op+'ss-'+suffix+'.o') for op in ('add','sub','mul','div')]]
 run([cc,'-target','arm64-apple-macos15','-std=c++20','-O1','-g','-frounding-math',*flags,'-I'+str(cpu),'-I'+str(a.remill/'include'),source/'lifted.cpp',*objs,'-o',out/('lifted-'+mode)]);data=run([out/('lifted-'+mode)]);(out/('lifted-'+mode+'.txt')).write_text(data)
 ref=[x.split() for x in a.amd_matrix.read_text().splitlines()];got=[x.split() for x in data.splitlines()];assert len(ref)==len(got)==24 and all(x[:4]==y[:4] for x,y in zip(ref,got));results['lifted-'+mode]='24 exact result/status matches'
for sdk,target in [('iphoneos','arm64-apple-ios18'),('iphonesimulator','arm64-apple-ios18-simulator')]:
 sdkpath=run(['xcrun','--sdk',sdk,'--show-sdk-path']).strip();run(['xcrun','--sdk',sdk,'clang++','-target',target,'-isysroot',sdkpath,'-std=c++20','-fsyntax-only','-I'+str(cpu),'-I'+str(a.remill/'include'),source/'classify.cpp',source/'lifted.cpp']);results[sdk]='syntax pass'
inputs=[source/'status.h',source/'classify.cpp',source/'lifted.cpp',source/'broad-native.cpp',mapping/'native.cpp',mapping/'boundary-native.cpp',mapping/'trap-native.cpp',a.amd_broad,a.amd_matrix,a.amd_trap,a.amd_boundary,a.amd_system,review/'result-qualification.json'];j=dict(results=results,captured_amd_and_rosetta_match_cases=4096,captured_amd_unmasked_invalid='native_child_exit=77 (SIGFPE handler)',denormal_classifier_scope='32-bit scalar ADD/SUB/MUL/DIV; all exceptions masked; sampled operand classes; DAZ/FTZ status-bit classification only.',inputs={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in inputs},scope='AMD input is a captured fixture, not hardware attestation. Classifier and simulated masked status only. DAZ/FTZ result semantics, unmasked trap translation, packed lanes, compiler ordering, actual STMXCSR/LDMXCSR, game/device and performance remain open.')
(out/'result.json').write_text(json.dumps(j,indent=2)+'\n');print(json.dumps({k:v for k,v in j.items() if k!='inputs'}))
