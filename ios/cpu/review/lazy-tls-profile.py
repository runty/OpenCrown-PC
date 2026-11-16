#!/usr/bin/env python3
"""Summarize self samples at the Darwin TLV resolver in an Instruments export."""
import argparse,collections,json,xml.etree.ElementTree as ET
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('xml',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
root=ET.parse(a.xml).getroot();ids={e.get('id'):e for e in root.iter() if e.get('id')};parents=collections.Counter();total=0;rows=0
resolve=lambda x:ids[x.get('ref')] if x.get('ref') else x
for row in root.iter('row'):
 stack=row.find('tagged-backtrace');weight=row.find('weight')
 if stack is None or weight is None:continue
 frames=[resolve(x).get('name','?') for x in resolve(stack).findall('frame')]
 if frames and frames[0]=='_tlv_get_addr':
  amount=int(resolve(weight).text or 0);total+=amount;rows+=1
  parents[frames[1] if len(frames)>1 else '<no caller>']+=amount
report={'self_sample_rows':rows,'self_sample_weight_ns':total,'immediate_observed_parent_weight_ns':dict(parents.most_common()),
        'scope':'Instruments sample weights are not CPU durations; optimized or missing stack frames make parent names incomplete and cannot establish exact helper call sites.'}
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'rows':rows,'weight_ms':total/1e6,'top':parents.most_common(5)},indent=2))
