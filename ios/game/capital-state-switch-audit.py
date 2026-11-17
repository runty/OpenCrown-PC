#!/usr/bin/env python3
"""Audit the observed deeper combat-state switch against private prepared game inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent / 'control-flow'))
from common import Elf

p = argparse.ArgumentParser()
p.add_argument('--game-root', type=Path, required=True)
p.add_argument('--atlas', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
root, out = a.game_root.resolve(), a.output.resolve()
if len(out.parts) < 3 or out.parts[1] != 'Volumes' or not Path('/Volumes', out.parts[2]).is_mount():
    p.error('output must be on a mounted external disk')
if out == root or root in out.parents or out in root.parents:
    p.error('output and game root must be separate')
out.mkdir(parents=True, exist_ok=False)
image = (root / 'eboot.image').read_bytes()
elf = Elf(root / 'eboot.elf')
start, end, table, count = 0x24de00, 0x24e2e1, 0x70b6f0, 4
owner = image[start:end]
raw_table = image[table:table + count * 4]
if len(owner) != end - start or len(raw_table) != count * 4:
    raise SystemExit("owner or table is outside the prepared image")
owner_offset, owner_flags = elf.file_match(start, owner, readonly=True)
table_offset, table_flags = elf.file_match(table, raw_table, readonly=True)
rows = json.loads(a.atlas.read_text())
if sum(r.get('module') == 'eboot' and r.get('start') == start and r.get('end') == end for r in rows) != 1:
    raise SystemExit('unwind atlas does not identify the expected owner')
cmd = ['/opt/homebrew/opt/llvm/bin/llvm-objdump', '-d', '--start-address=' + hex(start),
       '--stop-address=' + hex(end), str(root / 'eboot.elf')]
r = subprocess.run(cmd, check=True, capture_output=True, text=True)
(out / 'disassembly.txt').write_text(r.stdout)
(out / 'disassembly-stderr.txt').write_text(r.stderr)
instructions = {}
for line in r.stdout.splitlines():
    m = re.match(r'^\s*([0-9a-f]+):\s+(?:(?:[0-9a-f]{2})\s+)+\s*(.*)$', line)
    if m:
        instructions[int(m.group(1), 16)] = m.group(2).strip()
anchors = {0x24de88: 'movzbl', 0x24de90: 'cmpq', 0x24de94: 'sete',
           0x24de97: 'cmpq', 0x24de9b: 'ja',
           0x24dea1: 'leaq', 0x24dea8: 'leaq', 0x24deaf: 'vmovss',
           0x24deb4: 'leaq', 0x24debb: 'movslq', 0x24debf: 'addq',
           0x24dec2: 'jmpq'}
for pc, mnemonic in anchors.items():
    if not instructions.get(pc, '').startswith(mnemonic):
        raise SystemExit(f'unexpected dispatch instruction at {pc:x}')
if not all(x in instructions[pc] for pc, x in [
        (0x24de88, '0x104(%r15), %edx'), (0x24de90, '$0x1, %rax'),
        (0x24de94, '%cl'), (0x24de97, '$0x3, %rdx'),
        (0x24de9b, '0x24e2c2'), (0x24dea1, '0x70b4a0'),
        (0x24dea8, '0xa8(%r15), %r14'), (0x24deaf, '(%rsi,%rcx,4), %xmm0'),
        (0x24deb4, '0x70b6f0'), (0x24debb, '(%rsi,%rdx,4)'),
        (0x24debf, '%rsi, %rdx'), (0x24dec2, '*%rdx')]):
    raise SystemExit('unexpected switch guard or address calculation')

targets = [table + struct.unpack_from('<i', raw_table, i * 4)[0] for i in range(count)]
unique = sorted(set(targets))
if len(unique) != 4 or 0x24dec4 not in unique or any(t not in instructions or not start <= t < end for t in unique):
    raise SystemExit('unexpected target set or instruction boundary')
canonical = [0x20000000 + t for t in unique]
(out / 'targets.txt').write_text(''.join(f'{t:x}\n' for t in canonical))
report = {'elf_sha256': hashlib.sha256(elf.data).hexdigest(),
          'image_sha256': hashlib.sha256(image).hexdigest(),
          'atlas_sha256': hashlib.sha256(a.atlas.read_bytes()).hexdigest(),
          'owner': [hex(start), hex(end)], 'owner_file_offset': owner_offset,
          'owner_flags': owner_flags, 'table': hex(table), 'table_file_offset': table_offset,
          'table_flags': table_flags, 'table_sha256': hashlib.sha256(raw_table).hexdigest(),
          'slots': count, 'distinct_targets': len(unique),
          'targets': [f'{x:x}' for x in canonical],
          'missing': [f'{x:x}' for x in canonical if not (root / 'traces' / f'{x:x}.ll').is_file()],
          'scope': 'File-backed switch target and instruction-boundary audit; not gameplay reachability or a whole-function CFG proof'}
(out / 'verification.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({k: report[k] for k in ('slots', 'distinct_targets', 'missing', 'scope')}))
