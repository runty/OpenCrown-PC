#!/usr/bin/env python3
"""Rewrite only the separately lifted, verified VMOVUPS example for QA."""
import re,sys
from pathlib import Path
if len(sys.argv)!=3:raise SystemExit('Usage: rewrite-one.py INPUT_LL OUTPUT_DIRECTORY')
s=Path(sys.argv[1]).read_text();out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
assert s.count('define ptr @sub_206373d0(')==1
pattern=r'^  (%\d+) = call float @__remill_read_memory_f32\(ptr noundef (%\d+), i64 noundef (%\d+)\) #\d+$'
calls=list(re.finditer(pattern,s,re.M));assert len(calls)==8 and len({m.group(2) for m in calls})==1
base=calls[0].group(3);mem=calls[0].group(2)
for i,m in enumerate(calls[1:],1):
 assert re.search(r'^  '+re.escape(m.group(3))+r' = add i64 '+re.escape(base)+r', '+str(i*4)+r'$',s,re.M)
# Preserve numeric SSA definitions and insert named temporaries for vector slots.
new=s
for i,m in reversed(list(enumerate(calls))):
 target=m.group(1);prefix=(f'  call void @dcp_read256(ptr {mem}, i64 {base}, ptr %dcp_tmp)\n' if i==0 else '')
 prefix+=f'  %dcp_slot_{i} = getelementptr i8, ptr %dcp_tmp, i64 {i*4}\n'
 new=new[:m.start()]+prefix+f'  {target} = load float, ptr %dcp_slot_{i}, align 1'+new[m.end():]
new=new.replace('define ptr @sub_206373d0(', 'define ptr @sub_dcp_grouped(' ,1)
new=new.replace('  %R12 = getelementptr', '  %dcp_tmp = alloca [8 x float], align 4\n  %R12 = getelementptr',1)
new+='\ndeclare void @dcp_read256(ptr, i64, ptr)\n'
def prepare(x):
 x=x.replace(' nounwind','')
 for group in re.findall(r'^attributes #(\d+) = \{\s*\}$',x,re.M):
  x=re.sub(r'^attributes #'+group+r' = \{\s*\}\n','',x,flags=re.M);x=re.sub(r' #'+group+r'\b','',x)
 return re.sub(r'^(define .*?) \{$',r'\1 uwtable {',x,flags=re.M)
(out/'original.ll').write_text(prepare(s));(out/'grouped.ll').write_text(prepare(new))
print('rewrote eight scalar reads in one isolated VMOVUPS lift')
