#!/usr/bin/env python3
"""Decode sampled guest RIPs from a retained diagnostic; no device access."""
import argparse,collections,hashlib,json,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--report',type=Path,required=True);p.add_argument('--elf',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
out=a.output.resolve()
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount():p.error('output must be on a mounted external disk')
j=json.loads(a.report.read_text());g=j['guest'];profile=g['scalar_profile'];mods=g['scalar_profile_modules'];rows=[]
for sample in profile['sampled_guest_rip_by_total_acquire_time']:
 pc=sample['guest_rip'];module=next((m for m in mods if m['actual']<=pc<m['actual']+m['size']),None)
 if not module or module['canonical']!=0x20000000:continue
 canonical=module['canonical']+pc-module['actual'];offset=canonical-module['canonical']
 cmd=['/opt/homebrew/opt/llvm/bin/llvm-objdump','-d',f'--start-address={offset}',f'--stop-address={offset+15}',str(a.elf)]
 d=subprocess.run(cmd,text=True,capture_output=True,check=True)
 line=next((x for x in d.stdout.splitlines() if re.match(r'^\s*'+format(offset,'x')+r':\s',x)),None)
 if not line:rows.append(dict(pc=canonical,decode=None,samples=sample['wait']['samples']));continue
 match=re.match(r'^\s*[0-9a-f]+:\s+((?:[0-9a-f]{2}\s+)+)(.*)$',line)
 if not match:raise RuntimeError('unexpected disassembly: '+line)
 raw=bytes.fromhex(match.group(1));ins=match.group(2).strip();mnemonic=ins.split()[0];operands=ins[len(mnemonic):].strip()
 # AT&T syntax: last operand is destination. Only classify unambiguous YMM moves.
 kind='other'
 if mnemonic in ('vmovups','vmovaps','vmovdqu','vmovdqa') and '%ymm' in operands and '(' in operands:
  kind='ymm_load' if operands.startswith('(') or re.match(r'^[^,]*\([^)]*\),',operands) else 'ymm_store'
 rows.append(dict(pc=canonical,bytes=raw.hex(),instruction=ins,kind=kind,samples=sample['wait']['samples'],acquire_ns=sample['wait']['acquire_ns']))
counts=collections.Counter();sites=collections.Counter()
for r in rows:
 if r.get('kind'):counts[r['kind']]+=r['samples'];sites[r['kind']]+=1
result=dict(report_sha256=hashlib.sha256(a.report.read_bytes()).hexdigest(),elf_sha256=hashlib.sha256(a.elf.read_bytes()).hexdigest(),sampled_total=profile['wait']['samples'],dropped_sites=profile['dropped_instruction_sites'],selection='Only retained sites sorted by aggregate acquisition time; sampled counts are not complete traffic or CPU time',classification_samples=dict(counts),classification_sites=dict(sites),rows=rows)
out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(result,indent=2)+'\n')
print('decoded',sum(r.get('decode',True) is not None for r in rows),'sites; sample counts',dict(counts))
for r in rows:
 if r.get('kind','').startswith('ymm'):print(hex(r['pc']),r['instruction'],'samples',r['samples'])
