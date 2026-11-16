from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess
parser=argparse.ArgumentParser();parser.add_argument('--study-root',type=Path,required=True);parser.add_argument('--reference-run',type=Path,required=True);args=parser.parse_args();r=args.study_root.resolve();prior=args.reference_run.resolve()
if len(r.parts)<3 or r.parts[1]!='Volumes' or not Path('/Volumes',r.parts[2]).is_mount():parser.error('study root must be on mounted external storage')
intent=json.loads((prior/'intent.json').read_text());source=prior/'save'
def inv(p):return {str(x.relative_to(p)):hashlib.sha256(x.read_bytes()).hexdigest() for x in sorted(p.rglob('*')) if x.is_file()}
expected=inv(source); assert json.loads((r/'archive-verification.json').read_text())['different']==['memory.o']
for i,(mode,verify) in enumerate([('candidate',True),('baseline',False),('candidate',False),('candidate',False),('baseline',False)]):
 run=r/f'run-{i}-{mode}';run.mkdir();shutil.copytree(source,run/'save');assert inv(run/'save')==expected
 cmd=intent['command'].copy();cmd[0]=str(r/mode/'opencrown-aot-host');cmd[cmd.index('--save-root')+1]=str(run/'save');cmd[cmd.index('--output')+1]=str(run/'output');env=dict(os.environ,**intent['env']);env['DCP_MUTE_AUDIO']='1';env['DCP_TEXTURE_VERSION_VERIFY']='1' if verify else '0';(run/'intent.json').write_text(json.dumps({'command':cmd,'env':{**intent['env'],'DCP_TEXTURE_VERSION_VERIFY':env['DCP_TEXTURE_VERSION_VERIFY']},'save':expected},indent=2)+'\n');print('starting',i,mode,'verify',verify,flush=True)
 with (run/'run.log').open('w') as f:p=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT,env=env,timeout=230)
 d=json.loads((run/'output/report.json').read_text());g=d['guest'];v=g['texture_versions'];assert p.returncode==0 and d['success'] and not d['host_error'] and not g['error'] and g['checkpoint_complete'] and g['current_scene']=='TOWN' and v['enabled'] and v['version_hits']>0 and not v['proof_failures'] and (not verify or v['verified_checks']==v['version_hits']);assert inv(run/'save')==expected and inv(source)==expected
 z={'mode':mode,'verify':verify,'cpu_us':g['checkpoint_process_cpu_us'],'elapsed_ns':g['checkpoint_elapsed_ns'],'frames':d['rendered_frames'],'steps':g['steps'],'texture_hits':v['version_hits'],'verified_checks':v['verified_checks']};(run/'verification.json').write_text(json.dumps(z,indent=2)+'\n');print(json.dumps(z),flush=True)
