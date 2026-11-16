#!/usr/bin/env python3
"""Private host comparison; QA sharding disables the exact headless trace budget."""
import hashlib,json,os,shutil,subprocess,sys
from pathlib import Path
if len(sys.argv)!=3 or sys.argv[1] not in ('baseline','candidate','verify'):raise SystemExit('Usage: run-game.py baseline|candidate|verify NEW_OUTPUT')
mode=sys.argv[1];out=Path(sys.argv[2]).resolve()
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount():raise SystemExit('output must be on a mounted external disk')
out.mkdir(parents=True,exist_ok=False);root=out.parent
seed=Path('/Volumes/office/Build/OpenCrown/mobile-adventure-live/cursor-investigation/touch-tutorial-driver/touch-earned-save');expected='df59b0681ff021bf4609361f0abb8d6b50f895a9902db701d09cda91bccb1fb4'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(seed/'SAVEDATA00/SAVE0.DAT')==expected
save=out/'save';shutil.copytree(seed,save)
route=(Path(__file__).resolve().parent/'../../../host/routes/town-walk.txt').resolve().read_text()
env={k:v for k,v in os.environ.items() if not k.startswith('DCP_')}
env.update(DCP_CHECKPOINT_SCENE='TOWN',DCP_CHECKPOINT_SETTLE_FRAMES='600',DCP_MAX_TRACE_ENTRIES='5000000000',DCP_MAX_VBLANKS='20000',DCP_INPUT_SCRIPT='/Volumes/office/Build/OpenCrown/ipad-town-actual-input.txt',DCP_CHECKPOINT_INPUT=route,DCP_MUTE_AUDIO='1',DCP_TEXTURE_VERSIONS='1',DCP_TEXTURE_VERSION_VERIFY='1' if mode=='verify' else '0')
if mode!='baseline':env['DCP_QA_TRACE_SHARDS']='1'
cmd=[str(root/mode/'opencrown-aot-host'),'--game-root','/Volumes/phil/Games/PS4/DragonsCrownPro/CUSA10454','--save-root',str(save),'--output',str(out/'output'),'--seconds','180','--every','0','--offscreen']
(out/'intent.json').write_text(json.dumps(dict(mode=mode,command=cmd,env={k:v for k,v in env.items() if k.startswith('DCP_')},scope='Private native QA; QA sharding skips headless trace budget, external timeout remains; not device speed'),indent=2)+'\n')
with (out/'run.log').open('w') as log:p=subprocess.run(cmd,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=205)
j=json.loads((out/'output/report.json').read_text());g=j['guest'];v=g['texture_versions'];t=j['textures']
assert p.returncode==0 and j['success'] and j['unload_acknowledged'] and not j['host_error'] and not g['error'] and not j['timeout']
assert g['checkpoint_complete'] and g['current_scene']=='TOWN' and v['enabled'] and not v['proof_failures'] and v['version_hits']>0
assert v['verifying']==(mode=='verify') and v['version_hits']==t['version_hits']
if mode!='baseline':assert g['trace_sharded'] and g['steps']>0
if mode=='verify':assert g['trace_shadow']==g['steps'] and v['verified_checks']==v['version_hits'] and v['verified_bytes']==t['version_matched_bytes']
assert sha(save/'SAVEDATA00/SAVE0.DAT')==expected
result=dict(mode=mode,frames=j['consumed_frames'],steps=g['steps'],trace_shadow=g.get('trace_shadow'),sharded=g.get('trace_sharded',False),cpu_us=g['checkpoint_process_cpu_us'],checkpoint_flip=g['checkpoint_observed_flip'],texture_hits=v['version_hits'],save_sha256=expected)
(out/'verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)
