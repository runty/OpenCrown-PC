#!/usr/bin/env python3
"""Evaluate an isolated partial ARM FPSR to x86 MXCSR status mapping."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--review',type=Path,required=True);p.add_argument('--remill',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
out=a.output.resolve();assert len(out.parts)>2 and out.parts[1]=='Volumes' and Path('/Volumes',out.parts[2]).is_mount();out.mkdir(parents=True,exist_ok=False)
source=Path(__file__).resolve().parent;cpu=source.parents[1];review=a.review.resolve();meta=json.loads((review/'result-qualification.json').read_text());assert meta['result_bits_pass'];compiler='/opt/homebrew/opt/llvm/bin/clang++'
objects=[review/'fixed/runtime-native.o',*[review/'fixed'/('raw_'+op+'ss-native.o') for op in ('add','sub','mul','div')]]
def run(cmd):return subprocess.check_output(list(map(str,cmd)),text=True,stderr=subprocess.STDOUT,env=dict(os.environ,ROSETTA_ADVERTISE_AVX='1'))
def build(test,flags=(),extra=()):
 binary=out/(test+('-'+flags[0].split('=')[-1].replace(',','-') if flags else ''))
 run([compiler,'-std=c++20','-O1','-g','-frounding-math',*flags,*extra,source/(test+'.cpp'),'-o',binary]);return binary
results={}
for name,expected in [('bit-test','checks=256 failures=0'),('register-test','checks=128 failures=0')]:
 for flags in ([],['-fsanitize=address,undefined']):
  k=name+('-asan-ubsan' if flags else '-native');results[k]=run([build(name,flags)]).strip();assert results[k]==expected
results['thread-tsan']=run([build('thread-test',['-fsanitize=thread'],['-pthread'])]).strip();assert results['thread-tsan']=='checks=4000 failures=0'
for sdk,target in [('iphoneos','arm64-apple-ios18'),('iphonesimulator','arm64-apple-ios18-simulator')]:
 sdkpath=run(['xcrun','--sdk',sdk,'--show-sdk-path']).strip();run(['xcrun','--sdk',sdk,'clang++','-target',target,'-isysroot',sdkpath,'-std=c++20','-fsyntax-only',*[source/(name+'.cpp') for name in ('bit-test','register-test','thread-test')]]);results[sdk]='syntax pass'
for name in ('native','boundary-native','trap-mask-readback','trap-native'):run(['xcrun','clang++','-arch','x86_64','-O1',source/(name+'.cpp'),'-o',out/name])
for name,objs in [('lifted',objects),('boundary-lifted',[objects[0],objects[1],objects[4]])]:
 run([compiler,'-target','arm64-apple-macos15','-std=c++20','-O1','-g','-frounding-math','-I'+str(cpu),'-I'+str(a.remill/'include'),source/(name+'.cpp'),*objs,'-o',out/name])
raw={name:run([out/name]) for name in ('native','lifted','boundary-native','boundary-lifted','trap-mask-readback')}
for name,data in raw.items():(out/(name+'.txt')).write_text(data)
n=[x.split() for x in raw['native'].splitlines()];l=[x.split() for x in raw['lifted'].splitlines()];assert len(n)==len(l)==24 and all(x[:2]==y[:2] for x,y in zip(n,l))
bad=[dict(name=x[0],sticky=x[1],reference_result=x[2],reference_mxcsr=x[3],mapped_result=y[2],mapped_mxcsr=y[3]) for x,y in zip(n,l) if x[:4]!=y[:4]]
assert len(bad)==2 and all(x['name']=='add_denormal' and x['reference_result']==x['mapped_result'] and int(x['reference_mxcsr'],16)^int(x['mapped_mxcsr'],16)==2 for x in bad)
assert raw['boundary-native']==raw['boundary-lifted']=='guest_mxcsr=00001f85\n'
trap=subprocess.run([str(out/'trap-native')],text=True,capture_output=True,env=dict(os.environ,ROSETTA_ADVERTISE_AVX='1'));(out/'trap-native.txt').write_text(trap.stdout+trap.stderr)
assert trap.returncode==1 and trap.stdout.strip()=='native_child_exit=78' and raw['trap-mask-readback'].strip()=='before=00001f80 after=00001f81'
files=[*source.glob('*.cpp'),source/'status.h',*objects,review/'result-qualification.json'];j=dict(results=results,masked_cases=24,matching_result_and_status=22,strict_masked_status_pass=False,denormal_mismatches=bad,boundary_sequence=raw['boundary-lifted'].strip(),unmasked_native_probe_exit=trap.returncode,unmasked_reference_available=False,unmasked_detail='Rosetta readback forces invalid mask on; failed unmasked trap probe is retained.',inputs={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in files},scope='Experimental unintegrated status adapter; direct Mac FPSR, x86 under Rosetta. No denormal detection, DAZ/FTZ, priority, traps, actual STMXCSR/LDMXCSR integration, app/device or performance qualification.')
(out/'result.json').write_text(json.dumps(j,indent=2)+'\n');print(json.dumps({k:v for k,v in j.items() if k not in ('inputs','denormal_mismatches')}))
