#!/usr/bin/env python3
"""Construct private mobile core archives with only the lazy-TLS memory object."""
import argparse,hashlib,json,shlex,shutil,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve()
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount():p.error('output must be on a mounted external disk')
repo=Path(__file__).resolve().parents[3];old=Path('/Volumes/office/Build/OpenCrown/mobile-adventure-live/texture-cache-abi-candidate');meta=json.loads((old/'core-verification.json').read_text());sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
if subprocess.check_output(['git','-C',str(repo),'status','--porcelain'],text=True).strip():raise SystemExit('source must be clean and committed')
revision=subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],text=True).strip()
ref=json.loads((old/'core/simulator/artifact.json').read_text());other=ref['native_support_overrides']['sources']
for name,want in other.items():
 if name!='ios/cpu/runtime.cpp' and sha(repo/name)!=want:raise SystemExit('native support differs from reference: '+name)
if sha(repo/'ios/cpu/runtime.h')!=sha(Path('/Volumes/office/Build/OpenCrown/mobile-adventure-live/trace-counter-study/source/ios/cpu/runtime.h')):raise SystemExit('memory header differs from audited reference')
out.mkdir(parents=True,exist_ok=False);core=out/'core';core.mkdir();(core/'ThirdPartyNotices').symlink_to((old/'core/ThirdPartyNotices').resolve(),target_is_directory=True)
report={'revision':revision,'scope':'Private research artifacts only; not device qualified','platforms':{}}
for platform in ('ios','simulator'):
 dest=core/platform;dest.mkdir();base=old/'core'/platform;artifact=json.loads((base/'artifact.json').read_text());
 for name in ('libcpu-fixtures.a','libfreetype.a','libdcp-fp80.a'):
  if sha(base/name)!=artifact['libraries'][name]:raise SystemExit('reference library changed: '+name)
  subprocess.run(['cp','-c',str(base/name),str(dest/name)],check=True)
 cmd=next(c for c in meta['platforms'][platform]['commands'] if any(x.endswith('/ios/cpu/runtime.cpp') for x in c)).copy()
 sourceindex=next(i for i,x in enumerate(cmd) if x.endswith('/ios/cpu/runtime.cpp'));cmd[sourceindex]=str(repo/'ios/cpu/runtime.cpp');cmd[cmd.index('-o')+1]=str(dest/'memory.o')
 for i,x in enumerate(cmd):
  if x.startswith('-I') and x.endswith('/ios/cpu'):cmd[i]='-I'+str(repo/'ios/cpu')
 subprocess.run(cmd,check=True);(dest/'compile-command.txt').write_text(shlex.join(cmd)+'\n')
 subprocess.run(['/opt/homebrew/opt/llvm/bin/llvm-ar','rcs',str(dest/'libcpu-fixtures.a'),str(dest/'memory.o')],check=True)
 diff=dest/'archive-diff.json';subprocess.run(['python3',str(repo/'ios/game/review/trace-counter/archive-diff.py'),str(base/'libcpu-fixtures.a'),str(dest/'libcpu-fixtures.a'),'--output',str(diff)],check=True);d=json.loads(diff.read_text());assert d['changed']==['memory.o'] and not d['added'] and not d['removed']
 native=artifact['native_support_overrides'];native['base_native_support_revision']=native['source_revision'];native['source_revision']=revision;native['kind']='research-owned-texture-versions-plus-lazy-scalar-tls';native['sources']['ios/cpu/runtime.cpp']=sha(repo/'ios/cpu/runtime.cpp');native['objects']['memory.o']=sha(dest/'memory.o');native['commands']=[c for c in native['commands'] if not any(x.endswith('/ios/cpu/runtime.cpp') for x in c)]+[cmd];native['physical_device_qualified']=False;native['research_only']=True
 artifact['libraries']['libcpu-fixtures.a']=sha(dest/'libcpu-fixtures.a');artifact['experimental_support_override']={'kind':'lazy-scalar-tls','source_revision':revision,'base_artifact_sha256':sha(base/'artifact.json'),'archive_diff':'memory.o only'}
 (dest/'artifact.json').write_text(json.dumps(artifact,indent=2)+'\n');report['platforms'][platform]={'archive':sha(dest/'libcpu-fixtures.a'),'memory':sha(dest/'memory.o'),'baseline_artifact':sha(base/'artifact.json')};print(platform,'archive verified',flush=True)
(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
