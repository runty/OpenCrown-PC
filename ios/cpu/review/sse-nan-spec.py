#!/usr/bin/env python3
"""Check captured x86 NaN bits against the documented operand policy.

Consumes the generated literal case header and unmodified x86 result stream.
It never replaces native expectations or normalizes a comparison failure.
"""
import argparse,json,re
from pathlib import Path

def check(root):
    report=json.loads((root/'review.json').read_text());forms=report['forms']
    header=(root/'cases.h').read_text();cases={}
    for bits in (32,64):
        literal=re.search(r'static const Case cases'+str(bits)+r'\[\]=(\{.*?\});',header).group(1)
        cases[bits]=json.loads(literal.replace('{','[').replace('}',']'))
        assert len(cases[bits])==report['vectors_per_form']
    result=(root/'native-results.bin').read_bytes()
    assert len(result)==len(forms)*report['vectors_per_form']*72
    counts={'nan_input_results':0,'invalid_without_nan_results':0,'minmax_nan_or_zero_controls':0}
    for fi,form in enumerate(forms):
        bits=form['bits'];width=bits//8;absolute=(1<<(bits-1))-1
        exponent=0x7f800000 if bits==32 else 0x7ff0000000000000
        quiet=1<<(bits-1-(8 if bits==32 else 11)-1)
        indefinite=(1<<(bits-1))|exponent|quiet
        lanes=form['vector_bits']//bits if form['op'].endswith(('ps','pd')) else 1
        operation=form['op'][:-2]
        for ci,(_,offset,source) in enumerate(cases[bits]):
            source=bytes(source)
            for lane in range(lanes):
                left=int.from_bytes(source[64+lane*width:64+(lane+1)*width],'little')
                right=int.from_bytes(source[offset+lane*width:offset+(lane+1)*width],'little')
                la,ra=left&absolute,right&absolute;ln,rn=la>exponent,ra>exponent
                wanted=None;category=None
                if operation in ('min','max'):
                    if ln or rn or (not la and not ra):
                        wanted=right;category='minmax_nan_or_zero_controls'
                elif ln or rn:
                    wanted=(left if ln else right)|quiet;category='nan_input_results'
                else:
                    opposite=bool((left^right)>>(bits-1))
                    invalid=(operation=='add' and la==ra==exponent and opposite or
                             operation=='sub' and la==ra==exponent and not opposite or
                             operation=='mul' and ((not la and ra==exponent) or (not ra and la==exponent)) or
                             operation=='div' and ((not la and not ra) or la==ra==exponent))
                    if invalid:wanted=indefinite;category='invalid_without_nan_results'
                if wanted is None:continue
                pos=(fi*report['vectors_per_form']+ci)*72+32+lane*width
                actual=int.from_bytes(result[pos:pos+width],'little')
                assert actual==wanted,(form['name'],ci,lane,hex(left),hex(right),hex(actual),hex(wanted))
                counts[category]+=1
    assert all(counts.values())
    return dict(success=True,checks=counts,source='AMD APM Vol1 Table4-5/4-8 and Intel SDM Vol1 Table4-7/4.8.3.7',
                oracle='x86-64 instruction binary executed through Rosetta on Apple silicon, not physical AMD/Intel hardware',
                policy='First NaN source, quieted; invalid arithmetic without a NaN source returns negative indefinite. Min/max retain second-source rule.',
                scope='Independent bit-policy cross-check of captured oracle. Exact native stream remains the differential expectation; this does not validate guest MXCSR exception status/traps.')

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('review',type=Path);args=p.parse_args()
    report=check(args.review);(args.review/'native-policy-check.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))

if __name__=='__main__':main()
