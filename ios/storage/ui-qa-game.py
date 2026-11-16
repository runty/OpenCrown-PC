#!/usr/bin/env python3
"""Run the real game in a separate, already installed restore QA simulator app."""
import hashlib,json,os,shutil,subprocess,sys,time,uuid
from pathlib import Path
if len(sys.argv) not in (2,4,5) or (len(sys.argv)==5 and sys.argv[4] not in ('--restored','--combat')):raise SystemExit('Usage: ui-qa-game.py NEW_EXTERNAL_OUTPUT [QA_BUNDLE EXPECTED_BUILD_ID [--restored|--combat]]')
out=Path(sys.argv[1]).resolve()
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount():raise SystemExit('output must be on a mounted external disk')
out.mkdir(parents=True,exist_ok=False)
sim='F30116F9-F84C-44AA-A1A4-5A942B779628';normal='com.wadscorp.opencrown.mobile'
qa=sys.argv[2] if len(sys.argv)>=4 else 'com.wadscorp.opencrown.qa.restore'
expected_build=sys.argv[3] if len(sys.argv)>=4 else 'c0dee0def54f7c15fe73300d9be1113e683ed2c6'
assert qa.startswith('com.wadscorp.opencrown.qa.') and len(expected_build)==40
def simctl(*args,**kwargs):return subprocess.run(['xcrun','simctl',*map(str,args)],check=True,capture_output=True,text=True,**kwargs)
def docs(bundle):return Path(simctl('get_app_container',sim,bundle,'data').stdout.strip())/'Documents'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
normal_docs=docs(normal);qa_docs=docs(qa)
assert normal_docs!=qa_docs
mode=sys.argv[4] if len(sys.argv)==5 else ''
restored=mode=='--restored';combat=mode=='--combat';reuse=restored or combat
assert (qa_docs/'Game').is_dir() if reuse else not (qa_docs/'Game').exists()
protected=['ProbeSaves','SaveCheckpoints','.ProbeSaves.previous']
def inventory():
 return {str(p.relative_to(qa_docs)):sha(p) for item in protected for p in (qa_docs/item).rglob('*') if p.is_file()}
before=inventory();(out/'before.json').write_text(json.dumps(before,indent=2)+'\n')
# APFS clone, verified by the real launcher's game validator; no source writes.
if not reuse:subprocess.run(['cp','-cR',str(normal_docs/'Game'),str(qa_docs/'Game')],check=True)
saveid=str(uuid.uuid4()).upper();save=qa_docs/'DiagnosticSessions'/saveid/'ProbeSaves'
seed=qa_docs/'ProbeSaves' if restored else Path('/Volumes/office/Build/OpenCrown/mobile-adventure-live/cursor-investigation/adventure-cleared-save' if combat else '/Volumes/office/Build/OpenCrown/mobile-adventure-live/cursor-investigation/touch-tutorial-driver/touch-earned-save')
expected='728f0ecb5c92c57a833b731b9b2943bc059d2abd2383a043a280e6c8cf937bac' if combat else 'df59b0681ff021bf4609361f0abb8d6b50f895a9902db701d09cda91bccb1fb4'
assert sha(seed/'SAVEDATA00/SAVE0.DAT')==expected
shutil.copytree(seed,save);assert sha(save/'SAVEDATA00/SAVE0.DAT')==expected
source=Path(__file__).resolve().parent
shutil.copyfile(source/'ui-qa-ruins-input.txt' if combat else '/Volumes/office/Build/OpenCrown/ipad-town-actual-input.txt',qa_docs/'diagnostic-input.txt')
route=(source/('../host/routes/ruins-first-encounter.txt' if combat else '../host/routes/town-walk.txt')).resolve().read_text()
scene='GOBLIN' if combat else 'TOWN'
values=dict(DCP_DIAGNOSTIC_SECONDS='240' if combat else '180',DCP_DIAGNOSTIC_RESUME_UUID=saveid,DCP_CHECKPOINT_SCENE=scene,DCP_CHECKPOINT_SETTLE_FRAMES='720' if combat else '600',DCP_MAX_TRACE_ENTRIES='9400000000' if combat else '5000000000',DCP_MAX_VBLANKS='90000' if combat else '20000',DCP_CHECKPOINT_INPUT=route,DCP_MUTE_AUDIO='1',DCP_TEXTURE_VERSIONS='1',DCP_TEXTURE_VERSION_VERIFY='1')
env=os.environ.copy();env.update({'SIMCTL_CHILD_'+k:v for k,v in values.items()})
(out/'intent.json').write_text(json.dumps(dict(bundle=qa,session=saveid,environment=values,seed_sha256=expected,scope='Separate Simulator QA app; disposable save; no physical devices or performance claim'),indent=2)+'\n')
try:
 launch=simctl('launch','--terminate-running-process',sim,qa,'--diagnostic-startup',env=env);(out/'launch.txt').write_text(launch.stdout)
 end=time.monotonic()+(280 if combat else 220);last=None
 while time.monotonic()<end:
  time.sleep(3)
  try:report=json.loads((qa_docs/'diagnostics-live.json').read_text())
  except (FileNotFoundError,json.JSONDecodeError):continue
  if report.get('save_session_id')!=saveid or not report.get('diagnostic_startup'):continue
  guest=report.get('guest',{});state=(report.get('phase'),guest.get('current_scene'))
  if state!=last:print(state,report.get('captured_game_frames'),flush=True);last=state
  if state[0]=='stopped':break
 else:raise RuntimeError('Timed out waiting for QA session')
 (out/'final.json').write_text(json.dumps(report,indent=2)+'\n')
 g=report['guest'];v=g['texture_versions'];t=report['textures']
 assert report['build_id']==expected_build and g['booted'] and g['game_code_executed']
 assert g['checkpoint_complete'] and g['current_scene']==scene
 assert not any((g['error'],report['frontend_error'],report['save_error'],report.get('platform_error','')))
 assert report['diagnostic_resumed'] and report['save_session_id']==saveid
 assert v['enabled'] and v['verifying'] and v['version_hits']>0 and not v['proof_failures']
 assert v['version_hits']==v['verified_checks']==t['version_hits'] and v['verified_bytes']==t['version_matched_bytes']
 assert sha(save/'SAVEDATA00/SAVE0.DAT')==expected
 frame=qa_docs/'DiagnosticSessions'/report['session_id']/'last-game-frame.png'
 shutil.copy2(frame,out/'final.png')
 result=dict(success=True,frames=report['captured_game_frames'],scene=g['current_scene'],texture_checks=v['verified_checks'],save_sha256=expected,scope='Separate Simulator QA app, muted; not physical-device or sustained performance evidence')
 (out/'verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)
finally:
 after=inventory();(out/'preservation.json').write_text(json.dumps(dict(protected_qa_files_unchanged=before==after),indent=2)+'\n')
 assert before==after,'Existing synthetic QA saves or checkpoints changed'
