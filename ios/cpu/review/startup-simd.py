#!/usr/bin/env python3
"""Private-backend proof for four artifact-confirmed startup SIMD gaps.

Applies four ios/remill-startup-*.patch files to isolated source copies, builds private
semantics and a private lifter, and compares actual arm64 lifted execution to
native Rosetta. No device operations or shared backend builds are performed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import struct
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--backends', type=Path, required=True)
parser.add_argument('--llvm', type=Path, default=Path('/opt/homebrew/opt/llvm'))
args = parser.parse_args()
out = args.output.resolve()
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount():
    parser.error('Output must be on a mounted external build disk')
out.mkdir(parents=True,exist_ok=True)
remill=args.backends/'remill'
build=args.backends/'remill-build'
llvm=args.llvm/'bin'
cpu=Path(__file__).resolve().parents[1]
patches=[cpu.parent/('remill-startup-'+name+'.patch') for name in
         ['variable-blend','lane-insert','permute128','dword-shifts']]
semantics=out/'semantics'
semantics.mkdir(exist_ok=True)
scratch=out/'source'
files=['SSE.cpp','MMX.cpp','AVX.cpp']
shared=[*[remill/('lib/Arch/X86/Semantics/'+name) for name in files],
        build/'lib/Arch/X86/Runtime/amd64_avx.bc',build/'bin/lift/remill-lift-23']
hashes={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in shared}


def run(command,log,cwd=None,env=None):
    with (out/log).open('wb') as stream:
        subprocess.run(list(map(str,command)),stdout=stream,stderr=subprocess.STDOUT,
                       cwd=cwd,env=env,check=True)


def ninja_command(target):
    text=subprocess.check_output(['ninja','-C',str(build),'-t','commands',target],text=True)
    return shlex.split(text.splitlines()[-1])


def replace_argument(command,flag,value):
    command[command.index(flag)+1]=str(value)


for name in files:
    path=scratch/'lib/Arch/X86/Semantics'/name
    path.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(remill/'lib/Arch/X86/Semantics'/name,path)
for patch in patches:
    check=subprocess.run(['git','apply','--check',str(patch)],cwd=scratch,capture_output=True)
    if check.returncode:
        subprocess.run(['git','apply','--reverse','--check',str(patch)],cwd=scratch,check=True)
    else:
        run(['git','apply',patch],patch.stem+'-apply.log',cwd=scratch)
instructions=(remill/'lib/Arch/X86/Runtime/Instructions.cpp').read_text()
for name in files:
    instructions=instructions.replace('#include "lib/Arch/X86/Semantics/'+name+'"',
        '#include '+json.dumps(str(scratch/'lib/Arch/X86/Semantics'/name)))
(out/'Instructions.cpp').write_text(instructions)
command=ninja_command('lib/Arch/X86/Runtime/amd64_avx_Instructions.cpp.bc')
command=command[command.index('&&')+1:]
replace_argument(command,'-c',out/'Instructions.cpp')
replace_argument(command,'-o',out/'Instructions.bc')
run(command,'private-semantics-build.log',cwd=out)
run([llvm/'llvm-link',out/'Instructions.bc',
     *[build/f'lib/Arch/X86/Runtime/amd64_avx_{part}.cpp.bc'
       for part in ('BasicBlock','Intrinsics','HyperCall')],
     '-o',semantics/'amd64_avx.bc'],'private-semantics-link.log')

# The public CLI has no semantics-directory argument. Compile only a private
# driver whose LoadArchSemantics call prefers our isolated directory; link it
# against read-only existing Remill/LLVM libraries.
lift=(remill/'bin/lift/Lift.cpp').read_text()
old='remill::LoadArchSemantics(arch.get())'
assert old in lift
lift=lift.replace(old,'remill::LoadArchSemantics(arch.get(), {'+json.dumps(str(semantics))+'})')
(out/'Lift.cpp').write_text(lift)
object_target='bin/lift/CMakeFiles/remill-lift-23.dir/Lift.cpp.o'
command=ninja_command(object_target)
if Path(command[0]).name=='ccache': command=command[1:]
replace_argument(command,'-c',out/'Lift.cpp')
replace_argument(command,'-o',out/'Lift.o')
for flag in ('-MF','-MT'):
    if flag in command: replace_argument(command,flag,out/('Lift.o.d' if flag=='-MF' else 'Lift.o'))
run(command,'private-lifter-build.log',cwd=build)
command=ninja_command('bin/lift/remill-lift-23')
command=command[command.index('&&')+1:]
command=command[:command.index('&&')]
command=[str(out/'Lift.o') if x==object_target else x for x in command]
replace_argument(command,'-o',out/'startup-lift')
run(command,'private-lifter-link.log',cwd=build)

# Each output contains complete YMM0+YMM1, the full R11 GPR and a sixteen-byte
# memory guard. This catches VEX upper-zeroing, source destruction, GPR truncation
# and out-of-width EXTRACTPS stores independently of selected payload values.
specs=[]
for kind in ['ps','pd']:
    for width,reg,mem in [(128,'xmm','xmmword'),(256,'ymm','ymmword')]:
        specs.extend([
            ('blend',f'blendv{kind}{width}_reg',f'vblendv{kind} {reg}0,{reg}1,{reg}2,{reg}3'),
            ('blend',f'blendv{kind}{width}_mem',f'vblendv{kind} {reg}0,{reg}1,{mem} ptr [rdi+64],{reg}3'),
            ('blend',f'blendv{kind}{width}_mask_alias',f'vblendv{kind} {reg}0,{reg}1,{reg}2,{reg}0'),
            ('blend',f'blendv{kind}{width}_source_alias',f'vblendv{kind} {reg}0,{reg}0,{reg}2,{reg}3'),
        ])
for mnemonic,controls,mem in [
        ('vpinsrb',list(range(16))+[16,127,128,255],'byte'),
        ('vpinsrd',[0,1,2,3,4,15,128,255],'dword')]:
    for control in controls:
        specs.extend([
            ('lanes',f'{mnemonic}_reg_{control}',f'{mnemonic} xmm0,xmm1,r11d,{control}'),
            ('lanes',f'{mnemonic}_mem_{control}',f'{mnemonic} xmm0,xmm1,{mem} ptr [rdi+129],{control}'),
        ])
    specs.append(('lanes',mnemonic+'_source_alias',f'{mnemonic} xmm0,xmm0,r11d,2'))
for control in [0,1,2,3,4,15,128,255]:
    specs.extend([
        ('lanes',f'extractps_reg_{control}',f'vextractps r11d,xmm1,{control}'),
        ('lanes',f'extractps_mem_{control}',f'vextractps dword ptr [rsi+76],xmm1,{control}'),
    ])
controls=[(high<<4)|low for high in range(4) for low in range(4)]
controls += [(high<<4)|8 for high in range(4)]+[0x80|low for low in range(4)]+[0x88,0x44,0x77,0xcc,0xff]
for control in controls:
    specs.extend([
        ('permute',f'perm2f128_reg_{control}',f'vperm2f128 ymm0,ymm1,ymm2,{control}'),
        ('permute',f'perm2f128_mem_{control}',f'vperm2f128 ymm0,ymm1,ymmword ptr [rdi+64],{control}'),
    ])
specs.extend([
    ('permute','perm2f128_first_alias','vperm2f128 ymm0,ymm0,ymm2,33'),
    ('permute','perm2f128_second_alias','vperm2f128 ymm0,ymm1,ymm0,50'),
])
for mnemonic in ['pslld','psrld','psrad']:
    for count in [0,1,31,32,255]:
        specs.extend([
            ('shifts',f'{mnemonic}_legacy_imm{count}',f'{mnemonic} xmm0,{count}'),
            ('shifts',f'{mnemonic}_vex128_imm{count}',f'v{mnemonic} xmm0,xmm1,{count}'),
            ('shifts',f'{mnemonic}_vex256_imm{count}',f'v{mnemonic} ymm0,ymm1,{count}'),
        ])
    specs.extend([
        ('shifts',f'{mnemonic}_legacy_count_reg',f'{mnemonic} xmm0,xmm2'),
        ('shifts',f'{mnemonic}_legacy_count_mem',f'{mnemonic} xmm0,xmmword ptr [rdi+64]'),
        ('shifts',f'{mnemonic}_vex128_count_reg',f'v{mnemonic} xmm0,xmm1,xmm2'),
        ('shifts',f'{mnemonic}_vex128_count_mem',f'v{mnemonic} xmm0,xmm1,xmmword ptr [rdi+64]'),
        ('shifts',f'{mnemonic}_vex256_count_reg',f'v{mnemonic} ymm0,ymm1,xmm2'),
        ('shifts',f'{mnemonic}_vex256_count_mem',f'v{mnemonic} ymm0,ymm1,xmmword ptr [rdi+64]'),
    ])
values=[0,1,0x7fffffff,0x80000000,0xffffffff,0x3f800000,0xbf800000,0x7f800000,
        0xff800000,0x7fc00001,0xffc00001,0x7f800001,0xff800001,0x00800000,0x80800000,0x13579bdf]
counts=[0,1,30,31,32,255,256,1<<32,1<<63,(1<<64)-1]
seeds=[]
for index in range(256):
    seed=bytearray()
    for reg in range(3):
        for lane in range(8):
            seed+=struct.pack('<I',values[(index+lane+reg*5)%len(values)])
    seed[64:72]=struct.pack('<Q',counts[index%len(counts)])
    seed[72:80]=struct.pack('<Q',0xfedcba9876543210^index)
    for lane in range(8):
        seed+=struct.pack('<I',(values[(index*3+lane)%16]&0x7fffffff)|((index>>lane&1)<<31))
    seed+=struct.pack('<Q',0xfedcba9881fe0000+index*0x010203)
    seed+=bytes([0x5a]*8)
    assert len(seed)==144
    seeds.append(seed)
(out/'seeds.h').write_text('#include <cstdint>\nalignas(32) static const uint8_t seeds[][144]={'+
    ','.join('{'+','.join(str(v) for v in seed)+'}' for seed in seeds)+'};\n')
assembly='.intel_syntax noprefix\n.text\n'
objects,declarations,calls=[],[],[]
for i,(group,name,instruction) in enumerate(specs):
    code=('vmovdqu ymm0,ymmword ptr [rdi]\nvmovdqu ymm1,ymmword ptr [rdi+32]\n'
          'vmovdqu ymm2,ymmword ptr [rdi+64]\nvmovdqu ymm3,ymmword ptr [rdi+96]\n'
          'mov r11,qword ptr [rdi+128]\nmovabs rax,0xa5a5a5a5a5a5a5a5\n'
          'mov qword ptr [rsi+72],rax\nmov qword ptr [rsi+80],rax\n'+instruction+
          '\nvmovdqu ymmword ptr [rsi],ymm0\nvmovdqu ymmword ptr [rsi+32],ymm1\n'
          'mov qword ptr [rsi+64],r11\nvzeroupper\nret\n')
    (out/f'{name}.s').write_text('.intel_syntax noprefix\n.text\n'+code)
    run([llvm/'clang','-target','x86_64-unknown-linux-gnu','-c',out/f'{name}.s','-o',out/f'{name}.elf.o'],name+'-assemble.log')
    run([llvm/'llvm-objcopy','-O','binary','--only-section=.text',out/f'{name}.elf.o',out/f'{name}.bin'],name+'-extract.log')
    pc=0xd00000+i*0x1000
    run([out/'startup-lift','--os','linux','--arch','amd64_avx','--address',str(pc),
         '--bytes',(out/f'{name}.bin').read_bytes().hex(),'--ir_out',out/f'{name}.ll'],name+'-lift.log')
    run([llvm/'clang','-target','arm64-apple-macos15','-O2','-c',out/f'{name}.ll','-o',out/f'{name}.o'],name+'-compile.log')
    objects.append(out/f'{name}.o')
    declarations.append(f'extern "C" Memory *sub_{pc:x}(State&,uint64_t,Memory*);')
    calls.append(f'{{"{group}","{name}",sub_{pc:x},{pc}}}')
    assembly+=f'.globl _native_{i}\n_native_{i}:\n'+code
    if i+1==len(specs) or specs[i+1][0]!=group:print(f'Prepared {group} lifted routines',flush=True)
(out/'native.s').write_text(assembly)
(out/'native.cpp').write_text('#include <cstdio>\n#include "seeds.h"\n'+
    '\n'.join(f'extern "C" void native_{i}(const void*,void*);' for i in range(len(specs)))+
    '\nusing Fn=void(*)(const void*,void*);\nFn calls[]={'+','.join(f'native_{i}' for i in range(len(specs)))+
    '};\nint main(){for(auto fn:calls)for(auto &seed:seeds){uint8_t output[88];fn(seed,output);if(fwrite(output,1,88,stdout)!=88)return 1;}}\n')
run(['xcrun','clang++','-arch','x86_64',out/'native.cpp',out/'native.s','-o',out/'native'],'native-build.log')
run([out/'native'],'native-results.bin',env=dict(os.environ,ROSETTA_ADVERTISE_AVX='1'))
expected=(out/'native-results.bin').read_bytes()
assert len(expected)==len(specs)*len(seeds)*88
(out/'expected.h').write_text('static const unsigned char expected[]={'+','.join(str(v) for v in expected)+'};\n')
program='''#include "runtime.h"
#include "seeds.h"
#include "expected.h"
#include <cstdio>
#include <cstring>
Memory *dispatch(State &,uint64_t,Memory*){throw std::runtime_error("unexpected dispatch");}
extern "C" Memory *__remill_sync_hyper_call(State &,Memory *m,uint32_t){m->fault="unsupported instruction";return m;}
extern "C" Memory *__remill_error(State &,uint64_t,Memory *m){m->fault="unsupported instruction";return m;}
'''+ '\n'.join(declarations)+'''
struct Test{const char *group;const char *name;Lifted function;uint64_t pc;};
static const Test tests[]={'''+','.join(calls)+'''};
int main(){unsigned index=0,checks=0,failures=0;unsigned group_checks=0,group_failures=0,group_routines=0;
for(auto &test:tests){unsigned seed_index=0;for(auto &seed:seeds){State state{};Memory memory;
state.gpr.rdi.qword=Memory::base+0x100;state.gpr.rsi.qword=Memory::base+0x300;state.gpr.rsp.qword=Memory::base+0xff00;
std::memcpy(memory.bytes.data()+0x100,seed,sizeof(seed));write64(memory,state.gpr.rsp.qword,0xfeedface);
test.function(state,test.pc,&memory);
for(unsigned word=0;word<22;++word){uint32_t a,r;std::memcpy(&a,memory.bytes.data()+0x300+word*4,4);
std::memcpy(&r,expected+(index*'''+str(len(seeds))+'''+seed_index)*88+word*4,4);++checks;++group_checks;
if(a!=r){++failures;++group_failures;std::printf("FAIL %s seed%u word%u actual=%08x reference=%08x\\n",test.name,seed_index,word,a,r);}}
if(!memory.fault.empty()){++failures;++group_failures;std::printf("FAULT %s %s\\n",test.name,memory.fault.c_str());}++seed_index;}
++index;++group_routines;
if(index==sizeof(tests)/sizeof(tests[0]) || std::strcmp(tests[index].group,test.group)){
std::printf("GROUP %s routines=%u checks=%u failures=%u\\n",test.group,group_routines,group_checks,group_failures);std::fflush(stdout);group_routines=group_checks=group_failures=0;}}
std::printf("SUMMARY routines=%u checks=%u failures=%u\\n",index,checks,failures);return failures?1:0;}
'''
(out/'review.cpp').write_text(program)
run(['xcrun','clang++','-arch','arm64','-std=c++20','-O2','-I'+str(cpu),'-I'+str(remill/'include'),
     '-I'+str(out),out/'review.cpp',cpu/'runtime.cpp',*objects,'-o',out/'review'],'review-build.log')
with (out/'review-results.txt').open('w') as log:
    status=subprocess.run([str(out/'review')],stdout=log,stderr=subprocess.STDOUT).returncode
unchanged=all(hashlib.sha256(p.read_bytes()).hexdigest()==hashes[str(p)] for p in shared)
report={'routines':len(specs),'vectors':len(seeds),'dword_checks':len(specs)*len(seeds)*22,
        'status':status,'shared_backend_inputs_unchanged':unchanged,
        'oracle_sha256':hashlib.sha256(expected).hexdigest(),
        'patch_sha256':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in patches}}
(out/'review.json').write_text(json.dumps(report,indent=2)+'\n')
(out/'specs.json').write_text(json.dumps(specs,indent=2)+'\n')
print((out/'review-results.txt').read_text(),flush=True)
assert unchanged,'Shared backend changed during isolated test'
raise SystemExit(status)
