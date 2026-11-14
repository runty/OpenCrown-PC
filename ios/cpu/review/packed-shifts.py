#!/usr/bin/env python3
"""Full-YMM native Rosetta differential tests for packed-word shifts and VPBLENDVB.

Never builds or changes shared Remill bitcode. Use --prepare-only while the
coordinated backend build is pending, then rerun without it after that build.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--backends', type=Path, required=True)
parser.add_argument('--llvm', type=Path, default=Path('/opt/homebrew/opt/llvm'))
parser.add_argument('--prepare-only', action='store_true')
args = parser.parse_args()
out = args.output.resolve()
if len(out.parts) < 3 or out.parts[1] != 'Volumes' or not Path('/Volumes', out.parts[2]).is_mount():
    parser.error('Output must be on a mounted external build disk')
out.mkdir(parents=True, exist_ok=True)
llvm = args.llvm / 'bin'
remill = args.backends / 'remill'
lifter = args.backends / 'remill-build/bin/lift/remill-lift-23'
cpu = Path(__file__).resolve().parents[1]


def run(command, log, env=None):
    with (out / log).open('wb') as stream:
        subprocess.run(list(map(str, command)), stdout=stream, stderr=subprocess.STDOUT,
                       env=env, check=True)


specs = []
for mnemonic in ('psllw', 'psrlw', 'psraw'):
    for count in (0, 1, 15, 16, 255):
        specs.extend([
            (f'{mnemonic}_legacy_imm{count}', f'{mnemonic} xmm0,{count}'),
            (f'{mnemonic}_vex128_imm{count}', f'v{mnemonic} xmm0,xmm1,{count}'),
            (f'{mnemonic}_vex256_imm{count}', f'v{mnemonic} ymm0,ymm1,{count}'),
        ])
    specs.extend([
        (f'{mnemonic}_legacy_count_reg', f'{mnemonic} xmm0,xmm2'),
        (f'{mnemonic}_legacy_count_mem', f'{mnemonic} xmm0,xmmword ptr [rdi+64]'),
        (f'{mnemonic}_vex128_count_reg', f'v{mnemonic} xmm0,xmm1,xmm2'),
        (f'{mnemonic}_vex128_count_mem', f'v{mnemonic} xmm0,xmm1,xmmword ptr [rdi+64]'),
        (f'{mnemonic}_vex256_count_reg', f'v{mnemonic} ymm0,ymm1,xmm2'),
        (f'{mnemonic}_vex256_count_mem', f'v{mnemonic} ymm0,ymm1,xmmword ptr [rdi+64]'),
    ])
specs.extend([
    ('blend_vex128_reg', 'vpblendvb xmm0,xmm1,xmm2,xmm3'),
    ('blend_vex128_mem', 'vpblendvb xmm0,xmm1,xmmword ptr [rdi+64],xmm3'),
    ('blend_vex256_reg', 'vpblendvb ymm0,ymm1,ymm2,ymm3'),
    ('blend_vex256_mem', 'vpblendvb ymm0,ymm1,ymmword ptr [rdi+64],ymm3'),
    ('blend_vex128_mask_alias', 'vpblendvb xmm0,xmm1,xmm2,xmm0'),
    ('blend_vex256_mask_alias', 'vpblendvb ymm0,ymm1,ymm2,ymm0'),
    ('blend_vex128_source_alias', 'vpblendvb xmm0,xmm0,xmm2,xmm3'),
    ('blend_vex256_source_alias', 'vpblendvb ymm0,ymm0,ymm2,ymm3'),
    ('game_sign_mask128', 'vpsllw xmm0,xmm1,15\nvpsraw xmm0,xmm0,15\nvpblendvb xmm0,xmm1,xmm2,xmm0'),
    ('game_sign_mask256', 'vpsllw ymm0,ymm1,15\nvpsraw ymm0,ymm0,15\nvpblendvb ymm0,ymm1,ymm2,ymm0'),
])
assembly = '.intel_syntax noprefix\n.text\n'
for i, (name, instruction) in enumerate(specs):
    code = ('vmovdqu ymm0,ymmword ptr [rdi]\n'
            'vmovdqu ymm1,ymmword ptr [rdi+32]\n'
            'vmovdqu ymm2,ymmword ptr [rdi+64]\n'
            'vmovdqu ymm3,ymmword ptr [rdi+96]\n' + instruction +
            '\nvmovdqu ymmword ptr [rsi],ymm0\nvzeroupper\nret\n')
    (out / f'{name}.s').write_text('.intel_syntax noprefix\n.text\n' + code)
    run([llvm/'clang', '-target', 'x86_64-unknown-linux-gnu', '-c', out/f'{name}.s',
         '-o', out/f'{name}.elf.o'], name+'-assemble.log')
    run([llvm/'llvm-objcopy', '-O', 'binary', '--only-section=.text', out/f'{name}.elf.o',
         out/f'{name}.bin'], name+'-extract.log')
    assembly += f'.globl _native_{i}\n_native_{i}:\n' + code
(out/'native.s').write_text(assembly)

# Distinct signed words and upper-half sentinels; count uses the full low64 bits
# while upper64 bits of XMM2 deliberately remain nonzero and must be ignored.
words = [0,1,0x7fff,0x8000,0x8001,0xffff,0x00ff,0xff00,
         0x1357,0x2468,0xaaaa,0x5555,0x1234,0x9876,2,0xfffe]
counts = [0,1,15,16,255,256,65536,1<<32,1<<63,(1<<64)-1]
mask_bytes = [0,1,0x7f,0x80,0x81,0xff,0xfe,0x7e]
seeds = []
for count in counts:
    for variant in range(8):
        seed = bytearray()
        for register in range(2):
            for word in range(16):
                seed += struct.pack('<H', words[(word+variant+register*5)%16])
        state = 0x9e3779b9 * (variant+1)
        for _ in range(32):
            state = (1664525*state+1013904223)&0xffffffff
            seed.append(state >> 24)
        seed[64:72] = struct.pack('<Q', count)
        seed[72:80] = struct.pack('<Q', 0xfedcba9876543210 ^ variant)
        seed.extend(mask_bytes[(byte+variant)%8] for byte in range(32))
        assert len(seed) == 128
        seeds.append(seed)
(out/'seeds.h').write_text('#include <cstdint>\nalignas(32) static const uint8_t seeds[][128]={' +
    ','.join('{'+','.join(str(v) for v in seed)+'}' for seed in seeds) + '};\n')
declarations = '\n'.join(f'extern "C" void native_{i}(const void*,void*);' for i in range(len(specs)))
(out/'native.cpp').write_text('#include <cstdio>\n#include "seeds.h"\n' + declarations +
    '\nusing Fn=void(*)(const void*,void*);\nFn calls[]={' +
    ','.join(f'native_{i}' for i in range(len(specs))) +
    '};\nint main(){for(auto fn:calls)for(auto &seed:seeds){uint8_t output[32];fn(seed,output);if(fwrite(output,1,32,stdout)!=32)return 1;}}\n')
run(['xcrun','clang++','-arch','x86_64',out/'native.cpp',out/'native.s','-o',out/'native'], 'native-build.log')
run([out/'native'], 'native-results.bin', dict(os.environ, ROSETTA_ADVERTISE_AVX='1'))
expected = (out/'native-results.bin').read_bytes()
assert len(expected) == len(specs)*len(seeds)*32
report = {'routines':len(specs), 'vectors':len(seeds), 'ymm_dword_checks':len(specs)*len(seeds)*8,
          'oracle_sha256':hashlib.sha256(expected).hexdigest(),
          'mmx_source_sha256':hashlib.sha256((remill/'lib/Arch/X86/Semantics/MMX.cpp').read_bytes()).hexdigest()}
(out/'specs.json').write_text(json.dumps(specs, indent=2)+'\n')
if args.prepare_only:
    report['status']='native oracle prepared; shared backend not tested'
    (out/'review.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))
    raise SystemExit(0)
(out/'expected.h').write_text('static const unsigned char expected[]={' + ','.join(str(v) for v in expected) + '};\n')
objects, declarations, calls = [], [], []
for i, (name, _) in enumerate(specs):
    pc = 0xa00000+i*0x1000
    run([lifter,'--os','linux','--arch','amd64_avx','--address',str(pc),'--bytes',
         (out/f'{name}.bin').read_bytes().hex(),'--ir_out',out/f'{name}.ll'], name+'-lift.log')
    run([llvm/'clang','-target','arm64-apple-macos15','-O2','-c',out/f'{name}.ll',
         '-o',out/f'{name}.o'], name+'-compile.log')
    objects.append(out/f'{name}.o')
    declarations.append(f'extern "C" Memory *sub_{pc:x}(State&,uint64_t,Memory*);')
    calls.append(f'{{"{name}",sub_{pc:x},{pc}}}')
program = '''#include "runtime.h"
#include "seeds.h"
#include "expected.h"
#include <cstdio>
#include <cstring>
Memory *dispatch(State &,uint64_t,Memory*){throw std::runtime_error("unexpected dispatch");}
extern "C" Memory *__remill_sync_hyper_call(State &,Memory *memory,uint32_t){memory->fault="unsupported instruction";return memory;}
extern "C" Memory *__remill_error(State &,uint64_t,Memory *memory){memory->fault="unsupported instruction";return memory;}
''' + '\n'.join(declarations) + '''
struct Test{const char *name;Lifted function;uint64_t pc;};
static const Test tests[]={''' + ','.join(calls) + '''};
int main(){unsigned test_index=0,failures=0,checks=0;
for(auto &test:tests){unsigned seed_index=0;for(auto &seed:seeds){
State state{};Memory memory;state.gpr.rdi.qword=Memory::base+0x100;state.gpr.rsi.qword=Memory::base+0x300;state.gpr.rsp.qword=Memory::base+0xff00;
std::memcpy(memory.bytes.data()+0x100,seed,sizeof(seed));write64(memory,state.gpr.rsp.qword,0xfeedface);
test.function(state,test.pc,&memory);
for(unsigned lane=0;lane<8;++lane){uint32_t actual,reference;
std::memcpy(&actual,memory.bytes.data()+0x300+lane*4,4);
std::memcpy(&reference,expected+(test_index*''' + str(len(seeds)) + '''+seed_index)*32+lane*4,4);
++checks;if(actual!=reference){++failures;std::printf("FAIL %s seed%u lane%u actual=%08x reference=%08x\\n",test.name,seed_index,lane,actual,reference);}}
if(!memory.fault.empty()){++failures;std::printf("FAULT %s %s\\n",test.name,memory.fault.c_str());}++seed_index;}++test_index;}
std::printf("SUMMARY routines=%u checks=%u failures=%u\\n",test_index,checks,failures);return failures?1:0;}
'''
(out/'review.cpp').write_text(program)
run(['xcrun','clang++','-arch','arm64','-std=c++20','-O2','-I'+str(cpu),
     '-I'+str(remill/'include'),'-I'+str(out),out/'review.cpp',cpu/'runtime.cpp',
     *objects,'-o',out/'review'], 'review-build.log')
with (out/'review-results.txt').open('w') as log:
    status = subprocess.run([str(out/'review')],stdout=log,stderr=subprocess.STDOUT).returncode
report['status']=status
(out/'review.json').write_text(json.dumps(report, indent=2)+'\n')
print((out/'review-results.txt').read_text())
raise SystemExit(status)
