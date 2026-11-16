#!/usr/bin/env python3
"""Check diagnostic accounting, nested-state lifetime and joined publication."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--output', type=Path, required=True)
p.add_argument('--backend-root', type=Path, required=True)
a = p.parse_args()
r = a.output.resolve()
if len(r.parts) < 3 or r.parts[1] != 'Volumes' or not Path('/Volumes', r.parts[2]).is_mount():
    p.error('Output must be on a mounted external disk')
r.mkdir(parents=True, exist_ok=True)
s = Path(__file__).resolve().parents[1]
report = {'sources': {str(x.relative_to(s)): hashlib.sha256(x.read_bytes()).hexdigest()
    for x in [s/'runtime.cpp', s/'scalar-profile.h', s/'scalar-profile.inc', Path(__file__).with_suffix('.cpp')]}, 'runs': {}}
for mode, flags in [('native', []), ('asan-ubsan', ['-fsanitize=address,undefined']), ('tsan', ['-fsanitize=thread'])]:
    binary = r/mode
    command = ['xcrun', 'clang++', '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
        '-DDCP_SCALAR_PROFILE=1', *flags, '-I'+str(s), '-I'+str(a.backend_root/'remill/include'),
        str(s/'runtime.cpp'), str(Path(__file__).with_suffix('.cpp')), '-o', str(binary)]
    with (r/(mode+'-build.log')).open('w') as log:
        subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT)
    result = subprocess.run([binary], capture_output=True, text=True, timeout=90)
    (r/(mode+'-stdout.log')).write_text(result.stdout)
    (r/(mode+'-stderr.log')).write_text(result.stderr)
    assert result.returncode == 0 and not result.stderr, (mode, result.returncode, result.stderr)
    first, reset = map(json.loads, result.stdout.splitlines())
    assert first['available'] and first['threads'] == 5
    assert first['reads'] == 43137 and first['writes'] == 41088
    assert first['native_reads'] == first['native_writes'] == 7
    assert first['bytes'] == (43137+41088+14)*8
    assert first['atomic_held'] == 256
    assert not any(first[k] for k in ['omitted_threads', 'dropped_records', 'dropped_pages', 'dropped_instruction_sites'])
    entries = {x['pc']: x for x in first['recent_trace_entries']}
    assert len(entries) == 6
    assert entries[0x20001000]['reads'] == 8321 and entries[0x20001000]['writes'] == 8320
    assert entries[0x20003000]['reads'] == 2048 and not entries[0x20003000]['writes']
    ips = {x['guest_rip'] for x in first['sampled_guest_rip_by_total_acquire_time']}
    assert ips == {0x20001008, 0x20003008, *(0x20002008+i*0x100 for i in range(4))}
    assert first['wait']['samples'] >= 75
    assert reset['threads'] == 1 and reset['reads'] == reset['writes'] == 1
    assert not reset['wait']['samples'] and len(reset['recent_trace_entries']) == 1
    report['runs'][mode] = {'exit': result.returncode, 'reads': first['reads'], 'writes': first['writes'],
        'samples': first['wait']['samples'], 'nested_state': True, 'epoch_reset': True, 'command': command}
    (r/'verification.json').write_text(json.dumps(report, indent=2)+'\n')
    print(mode, 'pass', flush=True)
