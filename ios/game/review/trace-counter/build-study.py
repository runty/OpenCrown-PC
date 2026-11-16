#!/usr/bin/env python3
"""Build private trace-counter hosts from the existing audited bb540ac archive."""
import argparse,hashlib,json,shlex,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('output',type=Path);a=p.parse_args();out=a.output.resolve()
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount():p.error('output must be on a mounted external disk')
out.mkdir(parents=True,exist_ok=False)
review=Path(__file__).resolve().parent;repo=review.parents[3];cpu=repo/'ios/cpu';game=repo/'ios/game'
old=Path('/Volumes/office/Build/OpenCrown/mobile-adventure-live/texture-cache-abi-candidate');metadata=json.loads((old/'core-verification.json').read_text())
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
if sha(game/'runtime.cpp')!=metadata['source_hashes']['ios/game/runtime.cpp']:raise SystemExit('runtime source differs from audited reference')
source=out/'source/ios';(source/'game/review/trace-counter').mkdir(parents=True)
shutil.copy2(game/'runtime.cpp',source/'game/runtime.cpp');shutil.copy2(review/'counter.h',source/'game/review/trace-counter/counter.h')
for path in game.glob('*.h'):(source/'game'/path.name).symlink_to(path.resolve())
(source/'cpu').symlink_to(cpu.resolve(),target_is_directory=True);(source/'platform').symlink_to((repo/'ios/platform').resolve(),target_is_directory=True)
subprocess.run(['patch','-s','-p1','-d',str(out/'source')],input=(review/'runtime-study.patch').read_bytes(),check=True)
for mode in ('baseline','candidate','verify'):
 dest=out/mode;dest.mkdir();subprocess.run(['cp','-c',str(old/'core/mac/libcpu-fixtures.a'),str(dest/'libcpu-fixtures.a')],check=True)
 if mode!='baseline':
  cmd=next(x for x in metadata['platforms']['mac']['commands'] if any('ios/game/runtime.cpp' in s for s in x)).copy()
  cmd=[str(source/'game/runtime.cpp') if 'ios/game/runtime.cpp' in x else x for x in cmd];cmd[cmd.index('-o')+1]=str(dest/'runtime.o')
  cmd[cmd.index('-c'):cmd.index('-c')]=['-DDCP_QA_TRACE_SHARDS']+(['-DDCP_QA_TRACE_SHARDS_VERIFY'] if mode=='verify' else [])
  subprocess.run(cmd,check=True);(dest/'compile-command.txt').write_text(shlex.join(cmd)+'\n')
  subprocess.run(['ar','-r',str(dest/'libcpu-fixtures.a'),str(dest/'runtime.o')],check=True);subprocess.run(['xcrun','ranlib',str(dest/'libcpu-fixtures.a')],check=True)
subprocess.run(['python3',str(review/'archive-diff.py'),'--output',str(out/'archive-diff.json'),str(out/'baseline/libcpu-fixtures.a'),str(out/'candidate/libcpu-fixtures.a')],check=True)
diff=json.loads((out/'archive-diff.json').read_text());assert diff['changed']==['runtime.o'] and not diff['added'] and not diff['removed']
host=old/'host';cmd=shlex.split(subprocess.check_output(['ninja','-C',str(host),'-t','commands','opencrown-aot-host'],text=True).splitlines()[-1]);cmd=cmd[2:cmd.index('&&',2)]
for mode in ('baseline','candidate','verify'):
 a=cmd.copy();a[a.index('-o')+1]=str(out/mode/'opencrown-aot-host');a=[str(out/mode/'libcpu-fixtures.a') if x==str(old/'core/mac/libcpu-fixtures.a') else x for x in a]
 subprocess.run(a,cwd=host,check=True);(out/mode/'link-command.txt').write_text(shlex.join(a)+'\n');print(mode,sha(out/mode/'opencrown-aot-host'),flush=True)
