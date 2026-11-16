#!/usr/bin/env python3
"""Alternating private native Town runs; never touches devices or live saves."""
import hashlib,json,os,shutil,statistics,subprocess,sys
from pathlib import Path
if len(sys.argv) not in (2,3) or (len(sys.argv)==3 and sys.argv[2]!='--reverse'):raise SystemExit('Usage: game-cost.py NEW_RUN_DIRECTORY [--reverse]')
out=Path(sys.argv[1]).resolve()
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount():raise SystemExit('output must be on a mounted external disk')
out.mkdir(parents=True,exist_ok=False)
root=out.parent;seed=Path('/Volumes/office/Build/OpenCrown/mobile-adventure-live/cursor-investigation/touch-tutorial-driver/touch-earned-save')
expected='df59b0681ff021bf4609361f0abb8d6b50f895a9902db701d09cda91bccb1fb4'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(seed/'SAVEDATA00/SAVE0.DAT')==expected
route=(Path(__file__).resolve().parent/'../../../host/routes/town-walk.txt').resolve().read_text()
results=[]
order=('chunk','baseline','baseline','chunk') if len(sys.argv)==3 else ('baseline','chunk','chunk','baseline')
for index,mode in enumerate(order):
 run=out/f'{index}-{mode}';run.mkdir();save=run/'save';shutil.copytree(seed,save)
 env={k:v for k,v in os.environ.items() if not k.startswith('DCP_')}
 env.update(DCP_CHECKPOINT_SCENE='TOWN',DCP_CHECKPOINT_SETTLE_FRAMES='600',DCP_MAX_TRACE_ENTRIES='5000000000',DCP_MAX_VBLANKS='20000',DCP_INPUT_SCRIPT='/Volumes/office/Build/OpenCrown/ipad-town-actual-input.txt',DCP_CHECKPOINT_INPUT=route,DCP_MUTE_AUDIO='1',DCP_TEXTURE_VERSIONS='1',DCP_TEXTURE_VERSION_VERIFY='0')
 cmd=[str(root/mode/'opencrown-aot-host'),'--game-root','/Volumes/phil/Games/PS4/DragonsCrownPro/CUSA10454','--save-root',str(save),'--output',str(run/'output'),'--seconds','180','--every','0','--offscreen']
 (run/'intent.json').write_text(json.dumps(dict(mode=mode,command=cmd,env={k:v for k,v in env.items() if k.startswith('DCP_')},scope='Native uninstrumented proxy; not device or sustained performance'),indent=2)+'\n')
 with (run/'run.log').open('w') as log:p=subprocess.run(cmd,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=205)
 j=json.loads((run/'output/report.json').read_text());g=j['guest'];v=g['texture_versions']
 assert p.returncode==0 and j['success'] and j['unload_acknowledged'] and not j['host_error'] and not g['error'] and not j['timeout']
 assert g['checkpoint_complete'] and g['current_scene']=='TOWN' and v['enabled'] and not v['verifying'] and not v['proof_failures']
 assert v['version_hits']>0 and sha(save/'SAVEDATA00/SAVE0.DAT')==expected
 item=dict(mode=mode,frames=j['consumed_frames'],cpu_us=g['checkpoint_process_cpu_us'],wall_ns=g['checkpoint_elapsed_ns'],checkpoint_flip=g['checkpoint_observed_flip'],texture_hits=v['version_hits'],save_sha256=expected)
 results.append(item);(run/'verification.json').write_text(json.dumps(item,indent=2)+'\n');print(json.dumps(item),flush=True)
summary={k:statistics.median(x['cpu_us'] for x in results if x['mode']==k) for k in ('baseline','chunk')}
(out/'result.json').write_text(json.dumps(dict(results=results,median_cpu_us=summary,scope='Two alternating native runs per variant; not device or sustained performance'),indent=2)+'\n')
print('median CPU microseconds',summary,flush=True)
