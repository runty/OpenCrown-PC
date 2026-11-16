#!/usr/bin/env python3
"""Run the exact lifted x86 bytes on an authorized x86 Linux reference host."""
import argparse, io, subprocess, tarfile
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--host', required=True)
a = p.parse_args()
out = a.output.resolve()
if len(out.parts) < 3 or out.parts[1] != 'Volumes' or not Path('/Volumes', out.parts[2]).is_mount():
    p.error('output must be on a mounted external build disk')
folder = out/'sequences'
asm = '.text\n'
for op in ('add', 'sub', 'mul', 'div'):
    data = (folder/(op+'ss.bin')).read_bytes()
    asm += f'.globl seq_{op}\n.type seq_{op},@function\nseq_{op}:\n.byte ' + ','.join(hex(x) for x in data) + f'\n.size seq_{op},.-seq_{op}\n'
asm += '.section .note.GNU-stack,"",@progbits\n'
buf = io.BytesIO()
with tarfile.open(fileobj=buf, mode='w') as tar:
    for name, data in [('native.cpp', (Path(__file__).parent/'native.cpp').read_bytes()), ('sequences.s', asm.encode())]:
        info = tarfile.TarInfo(name); info.size = len(data); tar.addfile(info, io.BytesIO(data))
script = 'set -eu; d=$(mktemp -d); trap \'rm -rf "$d"\' EXIT; tar -xf - -C "$d"; c++ -std=c++17 -O2 "$d/native.cpp" "$d/sequences.s" -o "$d/test"; "$d/test"'
result = subprocess.run(['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=5', a.host, script], input=buf.getvalue(), capture_output=True, check=True)
(out/'amd-exact-byte.txt').write_bytes(result.stdout)
reference = result.stdout.decode().splitlines()
lifted = (out/'lifted-test.txt').read_text().splitlines()
if len(reference) != len(lifted):
    raise SystemExit(f'case count differs: {len(reference)} vs {len(lifted)}')
diffs = [(x, y) for x, y in zip(reference, lifted) if x != y]
print(f'exact-byte comparisons: {len(reference)}; mismatches: {len(diffs)}')
for x, y in diffs:
    print(f'AMD {x} | lifted {y}')
if diffs:
    raise SystemExit(1)
