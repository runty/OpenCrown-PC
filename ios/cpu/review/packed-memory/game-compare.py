#!/usr/bin/env python3
"""Run matched, muted native Town comparisons with disposable save copies."""
import hashlib,json,os,shutil,subprocess,sys
from pathlib import Path
r=Path('/Volumes/office/Build/OpenCrown/mobile-adventure-live/packed-memory-audit/game-current')
prior=Path('/Volumes/office/Build/OpenCrown/mobile-adventure-live/trace-counter-study/perf-0-baseline')
intent=json.loads((prior/'intent.json').read_text())
candidate='candidate-read'
assert json.loads((r/'archive-read-verification.json').read_text())['different']==['206364d0.o']

def inventory(folder):
 return {str(p.relative_to(folder)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(folder.rglob('*')) if p.is_file()}
source=prior/'save'; expected=inventory(source)
for i,mode in enumerate(('baseline',candidate,candidate,'baseline')):
 run=r/f'read-run-{i}-{mode}'
 if run.exists(): raise SystemExit(f'refusing to overwrite {run}')
 run.mkdir(); shutil.copytree(source,run/'save')
 assert inventory(run/'save')==expected
 command=intent['command'].copy();command[0]=str(r/mode/'opencrown-aot-host')
 command[command.index('--save-root')+1]=str(run/'save');command[command.index('--output')+1]=str(run/'output')
 env=dict(os.environ,**intent['env']);env['DCP_MUTE_AUDIO']='1'
 (run/'intent.json').write_text(json.dumps({'command':command,'env':intent['env'],'save':expected},indent=2)+'\n')
 print('starting',i,mode,flush=True)
 with (run/'run.log').open('w') as log:
  process=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,env=env,timeout=230)
 report=json.loads((run/'output/report.json').read_text());g=report['guest'];v=g['texture_versions']
 assert process.returncode==0 and report['success'] and not report['host_error'] and not g['error']
 assert g['booted'] and g['game_code_executed'] and g['checkpoint_complete'] and g['current_scene']=='TOWN'
 assert v['enabled'] and not v['proof_failures'] and v['version_hits']>0
 assert inventory(run/'save')==expected and inventory(source)==expected
 result={'mode':mode,'checkpoint_cpu_us':g['checkpoint_process_cpu_us'],'checkpoint_elapsed_ns':g['checkpoint_elapsed_ns'],
         'checkpoint_flip':g['checkpoint_observed_flip'],'steps':g['steps'],'frames':report['rendered_frames'],
         'version_hits':v['version_hits'],'save':expected}
 (run/'verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)
