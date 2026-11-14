#!/usr/bin/env python3
"""Build private AOT fixtures and a native x86-64 oracle, then test on arm64 Mac."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--remill', type=Path, required=True)
parser.add_argument('--llvm', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
output = args.output.resolve()
if len(output.parts) < 3 or output.parts[1] != 'Volumes' or not Path('/Volumes', output.parts[2]).is_mount():
    parser.error('Output must be on a mounted external disk')
output.mkdir(parents=True, exist_ok=True)
source = Path(__file__).resolve().parent
fixtures = json.loads((source / 'fixtures.json').read_text())
vectors = [(0, 0), (1, 2), (2**64-1, 1), (2**63-1, 1), (2**63, 2**64-1),
           (0x123456789abcdef0, 0xfedcba9876543210), (5, 63), (0xff0000ff, 31)]

def run(command, log, env=None):
    with (output / log).open('w') as stream:
        subprocess.run([str(x) for x in command], stdout=stream, stderr=subprocess.STDOUT, check=True, env=env)

llvm = args.llvm / 'bin'
lifter = args.remill / 'remill-build/bin/lift/remill-lift-23'
include = args.remill / 'remill/include'
assembly = '.intel_syntax noprefix\n.text\n'
for index, fixture in enumerate(fixtures):
    name = fixture['name']
    pc = 0x1000 + index * 0x1000
    fixture['pc'] = pc
    text = '.intel_syntax noprefix\n.text\n' + fixture['assembly'] + '\n'
    (output / f'{name}.s').write_text(text)
    run([llvm/'clang', '-target', 'x86_64-unknown-linux-gnu', '-c', output/f'{name}.s', '-o', output/f'{name}.elf.o'], f'{name}-assemble.log')
    run([llvm/'llvm-objcopy', '-O', 'binary', '--only-section=.text', output/f'{name}.elf.o', output/f'{name}.bin'], f'{name}-extract.log')
    code = (output/f'{name}.bin').read_bytes().hex()
    run([lifter, '--os', 'linux', '--arch', 'amd64_avx', '--address', str(pc), '--bytes', code,
         '--ir_out', output/f'{name}.ll'], f'{name}-lift.log')
    if name in ('call', 'indirect'):
        run([lifter, '--os', 'linux', '--arch', 'amd64_avx', '--address', str(pc), '--entry_address', str(pc+16), '--bytes', code, '--ir_out', output/f'{name}-callee.ll'], f'{name}-callee-lift.log')
    # Keep labels local to each native oracle function.
    native = fixture['assembly']
    for label in re.findall(r'^(\w+):', native, re.M):
        native = re.sub(r'\b' + label + r'\b', name + '_' + label, native)
    assembly += f'.p2align 4\n.globl _native_{name}\n_native_{name}:\n{native}\n'
    assembly += f'.globl _oracle_{name}\n_oracle_{name}:\ncall _native_{name}\npushfq\npop rdx\nret\n'
(output/'oracle.s').write_text(assembly)
declarations = '\n'.join('extern "C" Result oracle_'+f['name']+'(uint64_t,uint64_t);' for f in fixtures if not f.get('tls'))
code = '#include <cstdio>\n#include <cstdint>\nstruct Result { uint64_t value, flags; };\n'+declarations+'\nextern "C" void native_indirect();\nint main(){\n'
for fixture in fixtures:
    if fixture.get('tls'):
        continue
    for a, b in vectors:
        arg_a = '(uint64_t)memory' if fixture.get('memory') else f'{a}ULL'
        arg_b = '(uint64_t)&native_indirect+16' if fixture.get('indirect') else f'{b}ULL'
        code += '{ uint64_t memory[2]={' + str(a) + 'ULL,0}; auto r=oracle_'+fixture['name']+'('+arg_a+','+arg_b+'); '
        code += 'printf("'+fixture['name']+' %llu %llu\\n",(unsigned long long)r.value,(unsigned long long)(r.flags & 0x8c5)); }\n'
code += '}\n'
(output/'oracle.cpp').write_text(code)
run(['xcrun', 'clang++', '-arch', 'x86_64', '-O2', output/'oracle.cpp', output/'oracle.s', '-o', output/'oracle'], 'oracle-build.log')
env = dict(os.environ, ROSETTA_ADVERTISE_AVX='1')
run([output/'oracle'], 'oracle-results.txt', env)
results = iter((output/'oracle-results.txt').read_text().splitlines())
entries = []
cases = []
for fixture in fixtures:
    symbols = re.findall(r'^define ptr @sub_([0-9a-f]+)\(', (output/(fixture['name']+'.ll')).read_text(), re.M)
    entries.extend((int(s,16), 'sub_'+s) for s in symbols)
    for a, b in vectors:
        value, flags = (a+b) % 2**64, 0
        if not fixture.get('tls'):
            name, value, flags = next(results).split()
            assert name == fixture['name']
        booleans = [fixture.get(k, False) for k in ('memory', 'indirect', 'tls', 'flags')]
        booleans.append(fixture['name'] == 'indirect')
        cases.append('{"'+fixture['name']+'",'+','.join(str(x)+'ULL' for x in (fixture['pc'],a,b,int(value),int(flags)))+','+','.join(str(x).lower() for x in booleans)+'}')
for name in ('call', 'indirect'):
    entries.extend((int(s,16), 'sub_'+s) for s in re.findall(r'^define ptr @sub_([0-9a-f]+)\(', (output/(name+'-callee.ll')).read_text(), re.M))
corpus_hash = hashlib.sha256((source/'fixtures.json').read_bytes() + (output/'oracle-results.txt').read_bytes()).hexdigest()
header = 'static const char *corpus_hash = \"' + corpus_hash + '\";\n' + '\n'.join('extern "C" Memory *'+symbol+'(State &, uint64_t, Memory *);' for pc,symbol in entries)
header += '\nstruct Entry { uint64_t pc; Lifted function; };\nstatic const Entry entries[] = {'+','.join('{'+str(pc)+',&'+symbol+'}' for pc,symbol in entries)+'};\n'
header += 'struct Case { const char *name; uint64_t pc,a,b,result,flags; bool memory,indirect,tls,checkFlags,calls; };\nstatic const Case cases[] = {'+','.join(cases)+'};\n'
(output/'fixtures.h').write_text(header)
for platform, target, sdk in [('mac', 'arm64-apple-macos15', 'macosx'), ('ios', 'arm64-apple-ios18', 'iphoneos')]:
    directory = output/platform
    directory.mkdir(exist_ok=True)
    sdk_path = subprocess.check_output(['xcrun','--sdk',sdk,'--show-sdk-path'],text=True).strip()
    objects = []
    for fixture in fixtures + [{'name': 'call-callee'}, {'name': 'indirect-callee'}]:
        obj = directory/(fixture['name']+'.o')
        run([llvm/'clang', '-target', target, '-isysroot', sdk_path, '-O2', '-c', output/(fixture['name']+'.ll'), '-o', obj], platform+'-'+fixture['name']+'-compile.log')
        objects.append(obj)
    for name in ('runtime','harness'):
        obj = directory/(name+'.o')
        command = ['xcrun','--sdk',sdk,'clang++','-target',target,'-isysroot',sdk_path,'-std=c++17','-O2','-I'+str(include),'-I'+str(output),'-c',source/(name+'.cpp'),'-o',obj]
        run(command, platform+'-'+name+'-build.log')
        objects.append(obj)
    run([llvm/'llvm-ar','rcs',directory/'libcpu-fixtures.a',*objects], platform+'-archive.log')
    if platform == 'mac':
        (output/'cli.cpp').write_text('#include <cstdio>\n#include <cstring>\nextern "C" const char *dcp_cpu_report();\nint main(){auto r=dcp_cpu_report();puts(r);return strstr(r,"\\\"failed\\\":0")==nullptr;}\n')
        run(['xcrun','clang++','-arch','arm64',output/'cli.cpp',directory/'libcpu-fixtures.a','-o',output/'cpu-fixtures'], 'mac-link.log')
        run([output/'cpu-fixtures'], 'mac-results.json')
print((output/'mac-results.json').read_text())
