#!/usr/bin/env python3
"""Link new capture/stat callers against current and pre-guard cached archives."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess

p=argparse.ArgumentParser()
p.add_argument('--build',type=Path,required=True)
p.add_argument('--old-build',type=Path,required=True)
p.add_argument('--recipe',type=Path,required=True,help='Original renderer Ninja directory for platform/framework link flags')
p.add_argument('--sdl-source',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();out=a.output.resolve();source=Path(__file__).resolve().parents[2]
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount():p.error('Output must be on a mounted external disk')
out.mkdir(parents=True,exist_ok=True)
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
report=dict(old_archive_sha256=sha(a.old_build/'libopencrown-mobile-replay.a'),cases={})
text=subprocess.check_output(['ninja','-C',str(a.recipe),'-t','commands','replay-fixture'],text=True).splitlines()[-1]
tokens=shlex.split(text);assert tokens[:2]==[':','&&'];link=tokens[2:tokens.index('&&',2)]
probes={
 'capture':('#include "render/capture.h"\nnamespace dcp::hle { uint64_t vblank_count_public(){return 0;} }\nint main(){return dcp::render::initialize_aot_capture_v2(nullptr,0,{})?1:0;}\n','initialize_aot_capture_v2'),
 'backend':('#include "render/backend.h"\nnamespace dcp::hle { uint64_t vblank_count_public(){return 0;} }\nusing B=dcp::render::Backend; using P=dcp::render::TextureStats(B::*)() const; P volatile getter=&B::texture_stats_v2; int main(){return getter==nullptr;}\n','texture_stats_v2'),
 'replay':('#include "render/replay.h"\nnamespace dcp::hle { uint64_t vblank_count_public(){return 0;} }\nusing R=dcp::mobile::Replay; using P=dcp::render::TextureStats(R::*)() const; P volatile getter=&R::texture_stats_v2; int main(){return getter==nullptr;}\n','texture_stats_v2')
}
for name,(code,symbol) in probes.items():
 cpp=out/(name+'.cpp');cpp.write_text(code);obj=out/(name+'.o')
 compile=['xcrun','clang++','-std=c++20','-O2','-DDCP_AOT','-I'+str(source/'linux-port/src'),'-I'+str(source/'ios'),'-I'+str(a.sdl_source/'include'),'-c',cpp,'-o',obj]
 subprocess.run(list(map(str,compile)),check=True,capture_output=True,text=True)
 case={}
 for mode,directory in [('current',a.build),('old',a.old_build)]:
  command=[]
  for token in link:
   if token.endswith('.o'):command.append(str(obj))
   elif token in ('libopencrown-mobile-replay.a','libopencrown-mobile-metal.a'):command.append(str(directory/token))
   else:command.append(token)
  binary=out/(name+'-'+mode);command[command.index('-o')+1]=str(binary)
  command+=['-Wl,-undefined,error']
  result=subprocess.run(command,cwd=a.recipe,capture_output=True,text=True)
  (out/(name+'-'+mode+'-link.log')).write_text(result.stdout+result.stderr)
  case[mode]=dict(link_exit_code=result.returncode,expected_symbol=symbol)
  if mode=='current':
   if result.returncode:raise SystemExit('Current cache failed linking: '+name)
   run=subprocess.run([binary],capture_output=True,text=True,timeout=15)
   if run.returncode:raise SystemExit('Current cache probe failed: '+name)
   case[mode]['run_exit_code']=run.returncode
  elif result.returncode==0 or symbol not in result.stderr:
   raise SystemExit('Old cache was not rejected for the changed ABI: '+name)
 report['cases'][name]=case
 (out/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
 print(name,'current links/runs; old cache rejected for '+symbol,flush=True)
assert sha(a.old_build/'libopencrown-mobile-replay.a')==report['old_archive_sha256']
