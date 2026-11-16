#!/usr/bin/env python3
"""Capture masked SSE exception status against x86 under Rosetta.

Requires the separately generated, qualified sse-nan-results.py review objects.
This is a failure-preserving probe, not a substitute for a CPU fidelity suite.
"""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--review',type=Path,required=True);p.add_argument('--remill',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
out=a.output.resolve();assert len(out.parts)>2 and out.parts[1]=='Volumes' and Path('/Volumes',out.parts[2]).is_mount();out.mkdir(parents=True,exist_ok=False)
source=Path(__file__).resolve().parent;cpu=source.parents[1];review=a.review.resolve();meta=json.loads((review/'result-qualification.json').read_text());assert meta['result_bits_pass'] and all(x['fixed_strict_result_mismatches']==0 for x in meta['runs'].values())
objects=[review/'fixed/runtime-native.o',*[review/'fixed'/('raw_'+op+'ss-native.o') for op in ('add','sub','mul','div')]]
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
def run(cmd):subprocess.run(list(map(str,cmd)),check=True)
run(['xcrun','clang++','-arch','x86_64','-O1',source/'native.cpp','-o',out/'native'])
for name,flag in [('lifted',0),('polluted',1)]:run(['/opt/homebrew/opt/llvm/bin/clang++','-target','arm64-apple-macos15','-std=c++20','-O1','-DDCP_PRESEED_HOST_INVALID='+str(flag),'-I'+str(cpu),'-I'+str(a.remill/'include'),source/'lifted.cpp',*objects,'-o',out/name])
rows={}
for name in ('native','lifted','polluted'):
 raw=subprocess.check_output([str(out/name)],text=True,env=dict(os.environ,ROSETTA_ADVERTISE_AVX='1'));(out/(name+'.txt')).write_text(raw);rows[name]=[x.split() for x in raw.splitlines()]
n,l,poll=rows['native'],rows['lifted'],rows['polluted'];assert len(n)==len(l)==len(poll)==22
assert all(x[:2]==y[:2]==z[:2] for x,y,z in zip(n,l,poll))
values=sum(x[2]==y[2] for x,y in zip(n,l));diff=sum(x[3]!=y[3] for x,y in zip(n,l));host=sum((int(x[3],16)&63)==((int(y[4],16)|int(y[1]))&63) for x,y in zip(n,l));assert (values,diff,host)==(22,13,22)
assert n[0][0:2]==['add_exact','0'] and int(n[0][3],16)&1==0 and int(poll[0][4],16)&1==1
j=dict(cases=22,exact_result_matches=values,guest_mxcsr_mismatches=diff,cleared_host_flags_match_including_seeded_invalid=host,host_pollution_control='Exact 1+2 inherits preseeded host invalid despite clean guest reference.',inputs={str(x):sha(x) for x in [source/'native.cpp',source/'lifted.cpp',*objects,review/'result-qualification.json']},scope='Four scalar 32-bit forms, eleven operand cases, two sticky seeds, masked exceptions, nearest rounding and FTZ/DAZ off. Rosetta reference; no hardware x86, trap, denormal-operand, packed lane, concurrency or gameplay proof.')
(out/'result.json').write_text(json.dumps(j,indent=2)+'\n');print(json.dumps({k:v for k,v in j.items() if k!='inputs'}))
