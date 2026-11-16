#!/usr/bin/env python3
"""Execute the exact dump bytes on an authorized x86 Linux reference host."""
import argparse,hashlib,io,subprocess,tarfile
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--host',required=True);p.add_argument('--image',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
a=p.parse_args();out=a.output.resolve()
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount():p.error('output must be on a mounted external disk')
source=Path(__file__).resolve().parent
with a.image.open('rb') as f:f.seek(0x619220);b=f.read(57)
if hashlib.sha256(b).hexdigest()!='4bc6f9fed2b1f9e325415296cc7bbadf3056c45cdec94da6998507d6f0bcda3f':raise SystemExit('unexpected game bytes')
buf=io.BytesIO()
with tarfile.open(fileobj=buf,mode='w') as tar:
 for n,data in [(n,(source/n).read_bytes()) for n in ('native.cpp','cases.h','native.S')]+[('original-bytes.inc',('.byte '+','.join(hex(x) for x in b)+'\n').encode())]:
  info=tarfile.TarInfo(n);info.size=len(data);tar.addfile(info,io.BytesIO(data))
script='set -eu; d=$(mktemp -d); trap \'rm -rf "$d"\' EXIT; tar -xf - -C "$d"; c++ -std=c++17 -O2 -mavx "$d/native.cpp" "$d/native.S" -o "$d/test"; "$d/test"'
r=subprocess.run(['ssh','-o','BatchMode=yes','-o','ConnectTimeout=5',a.host,script],input=buf.getvalue(),capture_output=True,check=True);(out/'amd-matrix.txt').write_bytes(r.stdout)
reference=r.stdout.decode().splitlines()
failed=False
for name in ('lifted','prototype'):
 data=(out/(name+'-matrix.txt')).read_text().splitlines()
 if len(data)!=len(reference):raise SystemExit(f'{name}: different case count')
 diff=[(x,y) for x,y in zip(reference,data) if x!=y]
 print(f'{name}: {len(data)} cases, {len(diff)} mismatches')
 for x,y in diff[:5]:print('AMD',x,'\n'+name,y)
 failed |= bool(diff)
if failed:raise SystemExit(1)
