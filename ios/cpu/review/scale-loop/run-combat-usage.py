#!/usr/bin/env python3
"""Private native usage audit. Uses disposable saves and never contacts devices."""
import hashlib,json,os,re,shutil,subprocess,sys
from pathlib import Path
if len(sys.argv)!=2:raise SystemExit('Usage: run-combat-usage.py NEW_OUTPUT_DIRECTORY')
out=Path(sys.argv[1]).resolve()
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount():
 raise SystemExit('Output must be on a mounted external disk')
out.mkdir(parents=True,exist_ok=False)
root=Path('/Volumes/office/Build/OpenCrown/mobile-adventure-live')
seed=root/'cursor-investigation/adventure-cleared-save'
expected='728f0ecb5c92c57a833b731b9b2943bc059d2abd2383a043a280e6c8cf937bac'
def save_hash(path):return hashlib.sha256((path/'SAVEDATA00/SAVE0.DAT').read_bytes()).hexdigest()
assert save_hash(seed)==expected
shutil.copytree(seed,out/'save')
assert save_hash(out/'save')==expected
source=Path(__file__).resolve().parent
host=root/'sse-nan-candidate/exception-usage/host/opencrown-aot-host'
env={k:v for k,v in os.environ.items() if not k.startswith('DCP_')}
env.update(DCP_CHECKPOINT_SCENE='GOBLIN',DCP_CHECKPOINT_SETTLE_FRAMES='720',
 DCP_MAX_TRACE_ENTRIES='9400000000',DCP_MAX_VBLANKS='90000',
 DCP_INPUT_SCRIPT=str(source/'batch-ruins-extended-input.txt'),
 DCP_CHECKPOINT_INPUT=(source/'../../../host/routes/ruins-first-encounter.txt').resolve().read_text(),
 DCP_MUTE_AUDIO='1',DCP_TEXTURE_VERSIONS='1',DCP_TEXTURE_VERSION_VERIFY='1')
cmd=[str(host),'--game-root','/Volumes/phil/Games/PS4/DragonsCrownPro/CUSA10454',
 '--save-root',str(out/'save'),'--output',str(out/'output'),'--seconds','240','--every','0','--offscreen']
(out/'intent.json').write_text(json.dumps(dict(command=cmd,environment={k:v for k,v in env.items() if k.startswith('DCP_')},save_sha256=expected),indent=2)+'\n')
with (out/'run.log').open('w') as log:
 run=subprocess.run(cmd,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=270)
report=json.loads((out/'output/report.json').read_text());g=report['guest'];v=g['texture_versions'];t=report['textures'];text=(out/'run.log').read_text()
assert run.returncode==0 and report['success'] and report['unload_acknowledged'] and not report['host_error'] and not g['error'] and not report['timeout']
assert g['checkpoint_complete'] and g['current_scene']=='GOBLIN'
assert v['enabled'] and v['verifying'] and v['version_hits']>0 and not v['proof_failures']
assert v['version_hits']==v['verified_checks']==t['version_hits'] and v['verified_bytes']==t['version_matched_bytes']
assert save_hash(out/'save')==expected
lines=[x for x in text.splitlines() if x.startswith(('EXCEPTION_USAGE ','BATCH_STATS '))]
assert len(lines)==2 and any(x.startswith('EXCEPTION_USAGE ') for x in lines) and any(x.startswith('BATCH_STATS ') for x in lines),lines
batch=next(x for x in lines if x.startswith('BATCH_STATS '))
fast=re.search(r'\bfast=(\d+)',batch)
assert fast and int(fast.group(1))>0, batch
result=dict(success=True,scene=g['current_scene'],frames=report['consumed_frames'],texture_checks=v['verified_checks'],save_sha256=expected,counters=lines,scope='Muted native first-encounter audit; not device or performance evidence')
(out/'verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)
