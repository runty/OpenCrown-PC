#!/usr/bin/env python3
"""Private-backend MOVMSKPS/PD differential test; shared backend is read-only.

Applies ios/remill-movemask.patch to an isolated DATAXFER copy, builds private
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
patch=cpu.parent/'remill-movemask.patch'
semantics=out/'semantics'
semantics.mkdir(exist_ok=True)
scratch=out/'source'
data=scratch/'lib/Arch/X86/Semantics/DATAXFER.cpp'
data.parent.mkdir(parents=True,exist_ok=True)
shared=[remill/'lib/Arch/X86/Semantics/DATAXFER.cpp',
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


shutil.copy2(shared[0],data)
check=subprocess.run(['git','apply','--check',str(patch)],cwd=scratch,capture_output=True)
if check.returncode:
    # Allow this regression test after the patch is integrated upstream locally.
    subprocess.run(['git','apply','--reverse','--check',str(patch)],cwd=scratch,check=True)
else:
    run(['git','apply',patch],'apply-patch.log',cwd=scratch)
instructions=(remill/'lib/Arch/X86/Runtime/Instructions.cpp').read_text()
instructions=instructions.replace('#include "lib/Arch/X86/Semantics/DATAXFER.cpp"',
                                  '#include '+json.dumps(str(data)))
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
replace_argument(command,'-o',out/'movemask-lift')
run(command,'private-lifter-link.log',cwd=build)

specs=[('legacy_ps','movmskps ecx,xmm3',32,4,'ecx'),
       ('legacy_pd','movmskpd ecx,xmm3',64,2,'ecx')]
for kind,bits,width128,width256 in [('ps',32,4,8),('pd',64,2,4)]:
    for width,lanes in [(128,width128),(256,width256)]:
        for dest in ['ecx','r11d']:
            specs.append((f'vex_{kind}{width}_{dest}',
                          f'vmovmsk{kind} {dest},{"xmm" if width==128 else "ymm"}3',bits,lanes,dest))
seeds=[]
special32=[0,0x80000000,0x3f800000,0xbf800000,0x7f800000,0xff800000,
           0x7fc00001,0xffc00001,0x7f800001,0xff800001,1,0x80000001,
           0x7fffffff,0xffffffff,0x00800000,0x80800000]
special64=[0,0x8000000000000000,0x3ff0000000000000,0xbff0000000000000,
           0x7ff0000000000000,0xfff0000000000000,0x7ff8000000000001,0xfff8000000000001,
           0x7ff0000000000001,0xfff0000000000001,1,0x8000000000000001,
           0x7fffffffffffffff,0xffffffffffffffff,0x0010000000000000,0x8010000000000000]
for mask in range(256):
    seeds.append(b''.join(struct.pack('<I',(special32[(mask+lane)%16]&0x7fffffff)|((mask>>lane&1)<<31)) for lane in range(8)))
for rotation in range(4):
    for mask in range(16):
        seeds.append(b''.join(struct.pack('<Q',(special64[(mask+lane+rotation*4)%16]&0x7fffffffffffffff)|((mask>>lane&1)<<63)) for lane in range(4)))
(out/'seeds.h').write_text('#include <cstdint>\nalignas(32) static const uint8_t seeds[][32]={' +
                         ','.join('{'+','.join(str(v) for v in seed)+'}' for seed in seeds)+'};\n')
assembly='.intel_syntax noprefix\n.text\n'
objects,declarations,calls=[],[],[]
for i,(name,instruction,_,_,_) in enumerate(specs):
    code=('vmovdqu ymm3,ymmword ptr [rdi]\nmovabs rcx,0xfedcba9876543210\n'
          'movabs r11,0xabcdef0198765432\n'+instruction+
          '\nmov qword ptr [rsi],rcx\nmov qword ptr [rsi+8],r11\n'
          'vmovdqu ymmword ptr [rsi+16],ymm3\nvzeroupper\nret\n')
    (out/f'{name}.s').write_text('.intel_syntax noprefix\n.text\n'+code)
    run([llvm/'clang','-target','x86_64-unknown-linux-gnu','-c',out/f'{name}.s',
         '-o',out/f'{name}.elf.o'],name+'-assemble.log')
    run([llvm/'llvm-objcopy','-O','binary','--only-section=.text',out/f'{name}.elf.o',
         out/f'{name}.bin'],name+'-extract.log')
    pc=0xc00000+i*0x1000
    run([out/'movemask-lift','--os','linux','--arch','amd64_avx','--address',str(pc),
         '--bytes',(out/f'{name}.bin').read_bytes().hex(),'--ir_out',out/f'{name}.ll'],name+'-lift.log')
    run([llvm/'clang','-target','arm64-apple-macos15','-O2','-c',out/f'{name}.ll',
         '-o',out/f'{name}.o'],name+'-compile.log')
    objects.append(out/f'{name}.o')
    declarations.append(f'extern "C" Memory *sub_{pc:x}(State&,uint64_t,Memory*);')
    calls.append(f'{{"{name}",sub_{pc:x},{pc}}}')
    assembly+=f'.globl _native_{i}\n_native_{i}:\n'+code
(out/'native.s').write_text(assembly)
(out/'native.cpp').write_text('#include <cstdio>\n#include "seeds.h"\n'+
    '\n'.join(f'extern "C" void native_{i}(const void*,void*);' for i in range(len(specs)))+
    '\nusing Fn=void(*)(const void*,void*);\nFn calls[]={'+','.join(f'native_{i}' for i in range(len(specs)))+
    '};\nint main(){for(auto fn:calls)for(auto &seed:seeds){uint8_t output[48];fn(seed,output);if(fwrite(output,1,48,stdout)!=48)return 1;}}\n')
run(['xcrun','clang++','-arch','x86_64',out/'native.cpp',out/'native.s','-o',out/'native'],'native-build.log')
run([out/'native'],'native-results.bin',env=dict(os.environ,ROSETTA_ADVERTISE_AVX='1'))
expected=(out/'native-results.bin').read_bytes()
assert len(expected)==len(specs)*len(seeds)*48
# Verify native output layout and all possible sign masks independently, including
# full-width zero-extension and both preserved source halves.
for i,(_,_,bits,lanes,dest) in enumerate(specs):
    for j,seed in enumerate(seeds):
        start=(i*len(seeds)+j)*48
        actual=expected[start:start+48]
        mask=sum(((int.from_bytes(seed[lane*(bits//8):(lane+1)*(bits//8)],'little')>>(bits-1))&1)<<lane for lane in range(lanes))
        assert struct.unpack('<QQ',actual[:16])==((mask,0xabcdef0198765432) if dest=='ecx' else (0xfedcba9876543210,mask))
        assert actual[16:]==seed
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
struct Test{const char *name;Lifted function;uint64_t pc;};
static const Test tests[]={'''+','.join(calls)+'''};
int main(){unsigned index=0,checks=0,failures=0;
for(auto &test:tests){unsigned seed_index=0;for(auto &seed:seeds){State state{};Memory memory;
state.gpr.rdi.qword=Memory::base+0x100;state.gpr.rsi.qword=Memory::base+0x300;state.gpr.rsp.qword=Memory::base+0xff00;
std::memcpy(memory.bytes.data()+0x100,seed,32);write64(memory,state.gpr.rsp.qword,0xfeedface);
test.function(state,test.pc,&memory);
for(unsigned word=0;word<12;++word){uint32_t a,r;std::memcpy(&a,memory.bytes.data()+0x300+word*4,4);
std::memcpy(&r,expected+(index*'''+str(len(seeds))+'''+seed_index)*48+word*4,4);++checks;
if(a!=r){++failures;std::printf("FAIL %s seed%u word%u actual=%08x reference=%08x\\n",test.name,seed_index,word,a,r);}}
if(!memory.fault.empty()){++failures;std::printf("FAULT %s %s\\n",test.name,memory.fault.c_str());}++seed_index;}++index;}
std::printf("SUMMARY routines=%u checks=%u failures=%u\\n",index,checks,failures);return failures?1:0;}
'''
(out/'review.cpp').write_text(program)
run(['xcrun','clang++','-arch','arm64','-std=c++20','-O2','-I'+str(cpu),'-I'+str(remill/'include'),
     '-I'+str(out),out/'review.cpp',cpu/'runtime.cpp',*objects,'-o',out/'review'],'review-build.log')
with (out/'review-results.txt').open('w') as log:
    status=subprocess.run([str(out/'review')],stdout=log,stderr=subprocess.STDOUT).returncode
unchanged=all(hashlib.sha256(p.read_bytes()).hexdigest()==hashes[str(p)] for p in shared)
report={'routines':len(specs),'vectors':len(seeds),'dword_checks':len(specs)*len(seeds)*12,
        'status':status,'shared_backend_inputs_unchanged':unchanged,
        'oracle_sha256':hashlib.sha256(expected).hexdigest(),
        'patched_dataxfer_sha256':hashlib.sha256(data.read_bytes()).hexdigest()}
(out/'review.json').write_text(json.dumps(report,indent=2)+'\n')
print((out/'review-results.txt').read_text())
assert unchanged,'Shared backend changed during isolated test'
raise SystemExit(status)
