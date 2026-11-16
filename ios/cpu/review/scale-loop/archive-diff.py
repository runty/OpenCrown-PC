#!/usr/bin/env python3
"""Compare content hashes of BSD ar members without unpacking either archive."""
import argparse,hashlib,json
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('baseline',type=Path);p.add_argument('candidate',type=Path);p.add_argument('--output',type=Path,required=True)
a=p.parse_args();out=a.output.resolve()
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount():p.error('output must be on a mounted external disk')
def members(path):
 result={}
 with path.open('rb') as f:
  if f.read(8)!=b'!<arch>\n':raise ValueError('not a regular ar archive')
  while h:=f.read(60):
   if len(h)!=60 or h[58:]!=b'`\n':raise ValueError('invalid member header')
   name=h[:16].decode().strip();size=int(h[48:58]);data=f.read(size)
   if len(data)!=size:raise ValueError('truncated member')
   if size&1 and f.read(1)!=b'\n':raise ValueError('invalid padding')
   if name.startswith('#1/'):
    n=int(name[3:]);name=data[:n].rstrip(b'\0').decode();data=data[n:]
   name=name.rstrip('/')
   if name.startswith('__.SYMDEF') or name in ('','/'):continue
   if name in result:raise ValueError('duplicate archive member '+name)
   result[name]=hashlib.sha256(data).hexdigest()
 return result
x=members(a.baseline);y=members(a.candidate)
changed=sorted(k for k in x.keys()&y.keys() if x[k]!=y[k]);added=sorted(y.keys()-x.keys());removed=sorted(x.keys()-y.keys())
r={'baseline_members':len(x),'candidate_members':len(y),'changed':changed,'added':added,'removed':removed,'changed_sha256':{k:{'baseline':x[k],'candidate':y[k]} for k in changed},'added_sha256':{k:y[k] for k in added}}
out.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps({k:r[k] for k in ('baseline_members','candidate_members','changed','added','removed')}))
