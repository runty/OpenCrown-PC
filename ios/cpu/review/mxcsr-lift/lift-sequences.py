#!/usr/bin/env python3
"""Lift identical MXCSR load/arithmetic/store sequences with a private backend."""
import argparse
import re
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--backend', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
out = a.output.resolve()
if len(out.parts) < 3 or out.parts[1] != 'Volumes' or not Path('/Volumes', out.parts[2]).is_mount():
    p.error('output must be on a mounted external build disk')
out.mkdir(parents=True, exist_ok=True)
llvm = Path('/opt/homebrew/opt/llvm/bin')
for i, op in enumerate(('addss', 'subss', 'mulss', 'divss')):
    pc = 0x50000000 + i * 0x10000
    asm = out / (op + '.s')
    elf = out / (op + '.elf.o')
    raw = out / (op + '.bin')
    ir = out / (op + '.ll')
    asm.write_text('.intel_syntax noprefix\n.text\nldmxcsr [rdi]\nmovd xmm2, [rdi+4]\n' + op + ' xmm2, [rdi+8]\nstmxcsr [rsi]\nmovd [rsi+4], xmm2\nret\n')
    def run(args):
        subprocess.run(list(map(str, args)), check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    run([llvm/'clang', '-target', 'x86_64-unknown-linux-gnu', '-c', asm, '-o', elf])
    run([llvm/'llvm-objcopy', '-O', 'binary', '--only-section=.text', elf, raw])
    run([a.backend/'scalar-fp-lift', '--os', 'linux', '--arch', 'amd64_avx', '--address', str(pc), '--bytes', raw.read_bytes().hex(), '--ir_out', ir])
    lifted = ir.read_text().replace(' nounwind', '')
    for group in re.findall(r'^attributes #(\d+) = \{\s*\}$', lifted, re.M):
        lifted = re.sub(r'^attributes #' + group + r' = \{\s*\}\n', '', lifted, flags=re.M)
        lifted = re.sub(r' #' + group + r'\b', '', lifted)
    lifted = re.sub(r'^(define .*?) \{$', r'\1 uwtable {', lifted, flags=re.M)
    ir.write_text(lifted)
    for optimization in ('O1', 'O2', 'asan'):
        flags = ['-O1', '-fsanitize=address,undefined', '-g'] if optimization == 'asan' else ['-'+optimization]
        run([llvm/'clang', '-target', 'arm64-apple-macos15', *flags, '-funwind-tables', '-c', ir, '-o', out/(op+'-'+optimization+'.o')])
    print(op, raw.read_bytes().hex(), flush=True)
