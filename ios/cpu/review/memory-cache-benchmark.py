#!/usr/bin/env python3
"""Compare identical mapped-read workloads against an isolated committed baseline."""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
import subprocess
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--backend-root',type=Path,required=True);p.add_argument('--baseline',default='cab3d2b');p.add_argument('--runs',type=int,default=3);a=p.parse_args()
r=a.output.resolve();source=Path(__file__).resolve().parents[1];repo=source.parents[1]
if len(r.parts)<3 or r.parts[1]!='Volumes' or not Path('/Volumes',r.parts[2]).is_mount():p.error('Output must be on a mounted external disk')
if a.runs<1:p.error('runs must be positive')
r.mkdir(parents=True,exist_ok=True);baseline=r/'baseline';baseline.mkdir(exist_ok=True)
revision=subprocess.check_output(['git','-C',str(repo),'rev-parse',a.baseline+'^{commit}'],text=True).strip()
for name in ['runtime.cpp','runtime.h']:(baseline/name).write_bytes(subprocess.check_output(['git','-C',str(repo),'show',revision+':ios/cpu/'+name]))
for name,directory in [('baseline',baseline),('current',source)]:
 command=['xcrun','clang++','-std=c++17','-O2','-pthread','-I'+str(directory),'-I'+str(a.backend_root.resolve()/'remill/include'),directory/'runtime.cpp',Path(__file__).with_suffix('.cpp'),'-o',r/name/'benchmark'] if name=='baseline' else ['xcrun','clang++','-std=c++17','-O2','-pthread','-I'+str(directory),'-I'+str(a.backend_root.resolve()/'remill/include'),directory/'runtime.cpp',Path(__file__).with_suffix('.cpp'),'-o',r/'benchmark-current']
 with (r/(name+'-build.log')).open('w') as f:subprocess.run([str(x) for x in command],stdout=f,stderr=subprocess.STDOUT,check=True)
runs={'baseline':[],'current':[]}
for iteration in range(a.runs):
 for name in (['baseline','current'] if iteration%2==0 else ['current','baseline']):
  executable=baseline/'benchmark' if name=='baseline' else r/'benchmark-current'
  result=subprocess.check_output([str(executable)],text=True,timeout=120);runs[name].append(json.loads(result));(r/f'{name}-{iteration}.json').write_text(result)
summary={'baseline_revision':revision,'runs_per_variant':a.runs,'runtime_sha256':{str(directory):{name:hashlib.sha256((directory/name).read_bytes()).hexdigest() for name in ['runtime.cpp','runtime.h']} for directory in [baseline,source]},'phases':{},'runs':runs}
for phase in runs['baseline'][0]['phases']:
 name=phase['name'];medians={variant:statistics.median(next(p['cpu_ns'] for p in run['phases'] if p['name']==name) for run in data) for variant,data in runs.items()}
 summary['phases'][name]={'median_cpu_ns':medians,'baseline_over_current':medians['baseline']/medians['current']}
(r/'results.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary['phases'],indent=2))
