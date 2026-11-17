#!/usr/bin/env python3
"""Audit the post-fight Inn dispatch against the original private game image."""
import argparse, hashlib, json, re, struct, subprocess, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent / 'control-flow'))
from common import Elf
p=argparse.ArgumentParser()
p.add_argument('--game-root',type=Path,required=True)
p.add_argument('--atlas',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args(); root=a.game_root.resolve(); out=a.output.resolve()
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount(): p.error('output must be on a mounted external disk')
if out==root or root in out.parents or out in root.parents: p.error('output and game root must be separate')
out.mkdir(parents=True,exist_ok=False)
image=(root/'eboot.image').read_bytes(); elf=Elf(root/'eboot.elf')
start,end,table,count=0x78620,0x78a6a,0x6e2e60,7
owner=image[start:end]; raw=image[table:table+count*4]
if len(owner)!=end-start or len(raw)!=count*4: raise SystemExit('owner or table outside image')
off,flags=elf.file_match(start,owner,readonly=True); toff,tflags=elf.file_match(table,raw,readonly=True)
rows=json.loads(a.atlas.read_text())
if sum(r.get('module')=='eboot' and r.get('start')==start and r.get('end')==end for r in rows)!=1: raise SystemExit('owner mismatch')
r=subprocess.run(['/opt/homebrew/opt/llvm/bin/llvm-objdump','-d','--start-address='+hex(start),'--stop-address='+hex(end),str(root/'eboot.elf')],check=True,capture_output=True,text=True)
(out/'disassembly.txt').write_text(r.stdout); (out/'disassembly-stderr.txt').write_text(r.stderr)
ins={}
for line in r.stdout.splitlines():
 m=re.match(r'^\s*([0-9a-f]+):\s+(?:(?:[0-9a-f]{2})\s+)+\s*(.*)$',line)
 if m: ins[int(m.group(1),16)]=m.group(2).strip()
checks={0x78897:('leaq','0x6e2e60'),0x788a0:('leaq','0x2(%rbx), %rcx'),0x788a4:('movl','%ecx, %eax'),0x788a6:('cmpl','$0x6, %ecx'),0x788a9:('ja','0x788c0'),0x788ab:('movslq','(%r14,%rax,4), %rcx'),0x788af:('movl','$0x1, %eax'),0x788b4:('addq','%r14, %rcx'),0x788b7:('jmpq','*%rcx')}
for pc,(op,arg) in checks.items():
 if not ins.get(pc,'').startswith(op) or arg not in ins[pc]: raise SystemExit(f'unexpected dispatch at {pc:x}')
if [x for x in sorted(ins) if 0x788a0<=x<=0x788b7] != sorted(x for x in checks if x!=0x78897): raise SystemExit('unexpected dispatch instruction')
targets=[table+x for x in struct.unpack('<7i',raw)]
expected=[0x78908,0x788b9,0x788c4,0x788ce,0x788d6,0x788de,0x788e9]
if targets!=expected or any(t not in ins or not start<=t<end for t in targets): raise SystemExit('unexpected table targets')
canonical=[0x20000000+t for t in targets]
(out/'targets.txt').write_text(''.join(f'{t:x}\n' for t in canonical))
report={'elf_sha256':hashlib.sha256(elf.data).hexdigest(),'image_sha256':hashlib.sha256(image).hexdigest(),'atlas_sha256':hashlib.sha256(a.atlas.read_bytes()).hexdigest(),'owner':[hex(start),hex(end)],'owner_file_offset':off,'owner_flags':flags,'table':hex(table),'table_file_offset':toff,'table_flags':tflags,'table_sha256':hashlib.sha256(raw).hexdigest(),'targets':[f'{t:x}' for t in canonical],'scope':'Bounded table and instruction-boundary audit, not a full control-flow or gameplay proof'}
(out/'verification.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
