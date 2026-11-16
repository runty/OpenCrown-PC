#!/usr/bin/env python3
"""Exercise muted game audio and background/foreground in an isolated QA app."""
import hashlib,json,os,shutil,subprocess,sys,time,uuid
from pathlib import Path
if len(sys.argv)!=2:raise SystemExit('Usage: ui-qa-audio-lifecycle.py NEW_EXTERNAL_OUTPUT')
out=Path(sys.argv[1]).resolve()
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount():raise SystemExit('output must be on a mounted external disk')
out.mkdir(parents=True,exist_ok=False)
sim='F30116F9-F84C-44AA-A1A4-5A942B779628';bundle='com.wadscorp.opencrown.qa.cachefixed';revision='66c3c3f948eee0520d6a151dd31c083e835c050d'
def run(*args,**kwargs):return subprocess.run(['xcrun','simctl',*map(str,args)],check=True,capture_output=True,text=True,**kwargs)
docs=Path(run('get_app_container',sim,bundle,'data').stdout.strip())/'Documents'
assert (docs/'Game').is_dir() and (docs/'ProbeSaves').is_dir()
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def inventory():return {str(p.relative_to(docs)):sha(p) for n in ('ProbeSaves','SaveCheckpoints','.ProbeSaves.previous') for p in (docs/n).rglob('*') if p.is_file()}
before=inventory();(out/'before.json').write_text(json.dumps(before,indent=2)+'\n')
seed=docs/'ProbeSaves';expected='df59b0681ff021bf4609361f0abb8d6b50f895a9902db701d09cda91bccb1fb4';assert sha(seed/'SAVEDATA00/SAVE0.DAT')==expected
saveid=str(uuid.uuid4()).upper();save=docs/'DiagnosticSessions'/saveid/'ProbeSaves';shutil.copytree(seed,save)
input_file=docs/'diagnostic-input.txt';original=input_file.read_bytes();shutil.copyfile('/Volumes/office/Build/OpenCrown/ipad-town-actual-input.txt',input_file)
values=dict(DCP_DIAGNOSTIC_SECONDS='210',DCP_DIAGNOSTIC_RESUME_UUID=saveid,DCP_CHECKPOINT_SCENE='TOWN',DCP_CHECKPOINT_SETTLE_FRAMES='1200',DCP_MAX_TRACE_ENTRIES='9400000000',DCP_MAX_VBLANKS='90000',DCP_CHECKPOINT_INPUT='0 0',DCP_MUTE_AUDIO='1',DCP_CHECKPOINT_AUDIO='platform',DCP_TEXTURE_VERSIONS='1',DCP_TEXTURE_VERSION_VERIFY='1')
env=os.environ.copy();env.update({'SIMCTL_CHILD_'+k:v for k,v in values.items()})
(out/'intent.json').write_text(json.dumps(dict(bundle=bundle,session=saveid,environment=values,scope='Muted real audio in Simulator; no physical-device or audible-quality claim'),indent=2)+'\n')
def report():return json.loads((docs/'diagnostics-live.json').read_text())
def retain(name,value):(out/(name+'.json')).write_text(json.dumps(value,indent=2)+'\n')
completed=False
try:
 (out/'launch.txt').write_text(run('launch','--terminate-running-process',sim,bundle,'--diagnostic-startup',env=env).stdout)
 deadline=time.monotonic()+250;did_cycle=False;last=None
 while time.monotonic()<deadline:
  time.sleep(2)
  try:r=report()
  except (FileNotFoundError,json.JSONDecodeError):continue
  if r.get('save_session_id')!=saveid or r.get('build_id')!=revision:continue
  g=r['guest'];state=(r['phase'],g.get('current_scene'))
  if state!=last:print(state,r.get('captured_game_frames'),flush=True);last=state
  if g.get('current_scene')=='TOWN' and not did_cycle:
   retain('before-background',r);assert r['audio']['running'] and r['audio']['output_muted'] and r['audio']['rendered_frames']>0
   run('launch',sim,'com.apple.Preferences');time.sleep(4);background=report();retain('background',background)
   time.sleep(5);held=report();retain('background-held',held)
   assert background['session_id']==r['session_id']==held['session_id']
   assert not background['audio']['running'] and background['audio']['paused']
   assert not held['audio']['running'] and held['audio']['paused']
   assert held['captured_game_frames']-background['captured_game_frames']<=2
   run('launch',sim,bundle,env=dict(os.environ,SIMCTL_CHILD_DCP_MUTE_AUDIO='1'));time.sleep(5);resumed=report();retain('resumed',resumed)
   assert resumed['session_id']==r['session_id'] and resumed['captured_game_frames']>held['captured_game_frames']+30
   assert resumed['audio']['running'] and resumed['audio']['output_muted'] and resumed['audio']['rendered_frames']>held['audio']['rendered_frames']
   did_cycle=True;print('muted audio stopped in background and resumed',flush=True)
  if r['phase']=='stopped':break
 else:raise RuntimeError('timed out waiting for QA session')
 retain('final',r);g=r['guest'];v=g['texture_versions'];t=r['textures']
 assert did_cycle and g['checkpoint_complete'] and g['current_scene']=='TOWN' and g['booted'] and g['game_code_executed']
 assert not any((g['error'],r['frontend_error'],r['save_error'],r.get('last_platform_error','')))
 assert v['enabled'] and v['verifying'] and v['version_hits']==v['verified_checks']==t['version_hits']>0 and not v['proof_failures']
 assert sha(save/'SAVEDATA00/SAVE0.DAT')==expected
 shutil.copy2(docs/'DiagnosticSessions'/r['session_id']/'last-game-frame.png',out/'final.png')
 result=dict(success=True,frames=r['captured_game_frames'],verified_texture_checks=v['verified_checks'],audio=r['audio'],save_sha256=expected,scope='Muted iPhone Simulator audio/background test; no device or audible quality claim')
 retain('verification',result);completed=True;print(json.dumps(result),flush=True)
finally:
 if not completed:
  subprocess.run(['xcrun','simctl','terminate',sim,bundle],capture_output=True,text=True)
 input_file.write_bytes(original)
 after=inventory();retain('preservation',dict(protected_qa_files_unchanged=before==after,input_restored=input_file.read_bytes()==original))
 assert before==after,'existing QA save/checkpoint changed'
