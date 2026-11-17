#!/usr/bin/env python3
"""Check the first branch of a lifted, supported ally-combat continuation.

This redirects only the test copy immediately after JBE to report the selected
PC. It does not validate the subsequent function body or prove gameplay reached
this entry. Inputs are private generated files; output stays on external storage.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--trace', type=Path, required=True)
parser.add_argument('--image', type=Path, required=True)
parser.add_argument('--backend', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
out = args.output.resolve()
if len(out.parts) < 3 or out.parts[1] != 'Volumes' or not Path('/Volumes', out.parts[2]).is_mount():
    parser.error('output must be on a mounted external disk')
out.mkdir(parents=True, exist_ok=False)
source = Path(__file__).resolve().parents[3]
image = args.image.read_bytes()
if image[0x26dfbc:0x26dfc2] != bytes.fromhex('c5f82ec87649'):
    raise SystemExit('unsupported executable: expected VUCOMISS followed by JBE +73')
trace = args.trace.read_text()
if 'define ptr @sub_2026dfc0(' not in trace:
    raise SystemExit('wrong translated entry')
old = '  br i1 %18, label %19, label %26\n\n19:'
new = '''  br i1 %18, label %probe_taken, label %probe_not_taken

probe_taken:
  store i64 %5, ptr %PC, align 8
  ret ptr %memory

probe_not_taken:
  store i64 %6, ptr %PC, align 8
  ret ptr %memory

19:'''
if trace.count(old) != 1:
    raise SystemExit('unexpected lifted branch structure')
(out / 'branch.ll').write_text(trace.replace(old, new))
llvm = '/opt/homebrew/opt/llvm/bin/clang'
subprocess.run([llvm, '-target', 'arm64-apple-macos15', '-O2', '-x', 'ir', '-c',
                str(out / 'branch.ll'), '-o', str(out / 'branch.o')], check=True)
subprocess.run(['xcrun', 'clang++', '-std=c++17', '-O2',
                '-I' + str(source / 'ios/cpu'),
                '-I' + str(args.backend.resolve() / 'remill/include'),
                str(Path(__file__).with_suffix('.cpp')), str(out / 'branch.o'),
                '-o', str(out / 'branch-test')], check=True)
result = subprocess.run([str(out / 'branch-test')], check=True, capture_output=True, text=True)
(out / 'results.txt').write_text(result.stdout)
(out / 'provenance.json').write_text(json.dumps({
    'trace_sha256': hashlib.sha256(args.trace.read_bytes()).hexdigest(),
    'image_sha256': hashlib.sha256(image).hexdigest(),
    'scope': 'Four incoming CF/ZF combinations through test-truncated lifted JBE; not a full-function or gameplay test'
}, indent=2) + '\n')
print(result.stdout, end='')
