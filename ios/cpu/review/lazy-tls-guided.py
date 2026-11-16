from pathlib import Path
import argparse,hashlib,json,os,shutil,signal,subprocess,time
parser=argparse.ArgumentParser();parser.add_argument('--study-root',type=Path,required=True);parser.add_argument('--reference-run',type=Path,required=True);args=parser.parse_args();r=args.study_root.resolve();old=args.reference_run.resolve()
if len(r.parts)<3 or r.parts[1]!='Volumes' or not Path('/Volumes',r.parts[2]).is_mount():parser.error('study root must be on a mounted external disk')
intent=json.loads((old/'intent.json').read_text());save=r/'save';assert not save.exists();shutil.copytree(old/'save',save)
def inv(p):return {str(x.relative_to(p)):hashlib.sha256(x.read_bytes()).hexdigest() for x in sorted(p.rglob('*')) if x.is_file()}
expected=inv(save);assert expected==inv(old/'save') and expected['SAVEDATA00/SAVE0.DAT']=='728f0ecb5c92c57a833b731b9b2943bc059d2abd2383a043a280e6c8cf937bac';(r/'save-before.json').write_text(json.dumps(expected,indent=2)+'\n');shutil.copy2(old/'step.py',r/'step.py')
cmd=[str(r/'opencrown-aot-host') if x==intent['command'][0] else x.replace(str(old),str(r)) for x in intent['command']];env={k:v for k,v in os.environ.items() if not k.startswith('DCP_')};env.update({k:v.replace(str(old),str(r)) for k,v in intent['environment'].items()});env['DCP_MUTE_AUDIO']='1';(r/'intent.json').write_text(json.dumps({'command':cmd,'env':{k:env[k] for k in intent['environment']},'scope':'Private native guided research; input-only QA harness, disposable save, muted; not device performance or boss-clear proof'},indent=2)+'\n')
records=[json.loads(x) for x in (old/'commands.jsonl').read_text().splitlines()];assert len(records)==10
with (r/'run.log').open('w') as log:
 proc=subprocess.Popen(cmd,env=env,stdout=log,stderr=subprocess.STDOUT);(r/'pid').write_text(str(proc.pid));print('host started',proc.pid,flush=True)
 try:
  deadline=time.monotonic()+360
  while time.monotonic()<deadline:
   if proc.poll() is not None:raise RuntimeError(f'host ended before initial pause: {proc.returncode}')
   try:state=json.loads((r/'host-control.state.json').read_text())
   except (FileNotFoundError,json.JSONDecodeError):time.sleep(1);continue
   if state.get('error'):raise RuntimeError(state['error'])
   if state.get('paused') and state.get('captured_serial')==0:break
   time.sleep(1)
  else:raise RuntimeError('initial pause timeout; inspect owned process and report')
  print('initial pause',state['vblanks'],state['scene'],flush=True)
  for idx,row in enumerate(records):
   if idx==0:(r/'tap').write_text('-544 200\n')
   if idx==1:(r/'tap').write_text('172 190\n')
   f=r/f'input-{idx+1:02}.txt';f.write_text(row['script']);q=subprocess.run(['python3',str(r/'step.py'),str(f)],capture_output=True,text=True,timeout=160);(r/f'step-{idx+1:02}.log').write_text(q.stdout+q.stderr);q.check_returncode();s=json.loads((r/'host-control.state.json').read_text());print('completed step',idx+1,'vblank',s['vblanks'],'scene',s['scene'],flush=True)
  # Extend beyond the prior observed first Harpy hit using only the same
  # ordinary pad script; capture each step for independent visual review.
  extra=(old/'fight-left.txt').read_text()
  for n in range(3):
   f=r/f'input-extra-{n+1}.txt';f.write_text(extra);q=subprocess.run(['python3',str(r/'step.py'),str(f)],capture_output=True,text=True,timeout=160);(r/f'extra-{n+1}.log').write_text(q.stdout+q.stderr);q.check_returncode();s=json.loads((r/'host-control.state.json').read_text());print('completed extra',n+1,'vblank',s['vblanks'],'scene',s['scene'],flush=True)
 finally:
  if proc.poll() is None:proc.send_signal(signal.SIGINT)
  try:code=proc.wait(timeout=60)
  except subprocess.TimeoutExpired:raise RuntimeError('owned host did not stop; inspect same live pid')
  (r/'exit.json').write_text(json.dumps({'exit_code':code})+'\n')
  after=inv(save);(r/'save-after.json').write_text(json.dumps(after,indent=2)+'\n');assert after==expected and inv(old/'save')==expected
  print('host exit',code,'save unchanged',flush=True)
report=json.loads((r/'output/report.json').read_text());g=report['guest'];v=g['texture_versions'];assert not report['host_error'] and not g['error'] and report['unload_acknowledged'] and not report['stop_grace_expired'];assert v['enabled'] and v['verifying'] and v['version_hits']==v['verified_checks'] and not v['proof_failures'];result={'host_exit':code,'checkpoint_complete':g['checkpoint_complete'],'frames':report['rendered_frames'],'scene':g['current_scene'],'verified_texture_checks':v['verified_checks'],'proof_failures':v['proof_failures'],'save_unchanged':True,'scope':'Intentional stop after scripted extension; inspect frames before any gameplay claim'};(r/'verification.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)
