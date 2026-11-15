#!/usr/bin/env python3
"""Private-backend MOVBE load/store differential test; shared backend is read-only.

Applies ios/remill-movbe-stores.patch to an isolated DATAXFER copy, builds private
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
patch=cpu.parent/'remill-movbe-stores.patch'
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
replace_argument(command,'-o',out/'movbe-lift')
run(command,'private-lifter-link.log',cwd=build)

# Low/high-register forms, unaligned and scaled addressing, in-place stores,
# and address/destination aliasing. All payload/flag expectations are also
# computed independently of the native oracle.
specs=[]
for family,offset in [('low',0),('low',3),('high',9)]:
    for bits in [16,32,64]:
        for direction in ['load','store']:
            specs.append(dict(name=f'{direction}{bits}_{family}_{offset}',bits=bits,
                              direction=direction,family=family,offset=offset))
for bits in [16,32,64]:
    specs.append(dict(name=f'inplace{bits}',bits=bits,direction='store',family='low',offset=0,inplace=True))
    specs.append(dict(name=f'alias_load{bits}',bits=bits,direction='load',family='low',offset=0,alias=True))
flag_mask=0x8d5
sentinel=0xfedcba9876543210
base=0x100000
input_address=base+0x100
patterns=[0,0xffffffffffffffff,0x0123456789abcdef,0x8000000000000001,
          0x00000000ffffffff,0xffffffff00000000,0x0102030405060708,0xaa55aa55aa55aa55]
seeds=[]
for i in range(64):
    seed=bytearray(((j*37+i*13+19)&255) for j in range(64))
    struct.pack_into('<Q',seed,32,patterns[i%len(patterns)])
    flags=sum(((i>>j)&1)<<bit for j,bit in enumerate([0,2,4,6,7,11]))
    struct.pack_into('<Q',seed,40,0x202|flags)
    seeds.append(bytes(seed))
(out/'seeds.h').write_text('#include <cstdint>\nstatic const uint8_t seeds[][64]={' +
    ','.join('{'+','.join(map(str,s))+'}' for s in seeds)+'};\n')


def swapped(value,bits):
    return int.from_bytes((value&((1<<bits)-1)).to_bytes(bits//8,'little'),'big')


def expected_output(spec,seed):
    payload=bytearray(seed);bits=spec['bits'];width=bits//8;offset=spec['offset']
    source=int.from_bytes(seed[32:40],'little')
    low,high=(source,sentinel) if spec['family']=='low' else (sentinel,source)
    if spec.get('inplace'):
        low=int.from_bytes(seed[offset:offset+width],'little')
        # The fixture uses the ordinary full 64-bit load preceding the
        # observed 64-bit MOVBE, and width-specific loads for smaller forms.
    address=input_address
    if spec['direction']=='store':
        value=high if spec['family']=='high' else low
        payload[offset:offset+width]=(value&((1<<bits)-1)).to_bytes(width,'big')
    else:
        value=int.from_bytes(seed[offset:offset+width],'big')
        if spec.get('alias'):
            address=((input_address&~0xffff)|value) if bits==16 else value
        elif spec['family']=='high':
            high=(high&~0xffff)|value if bits==16 else value
        else:
            low=(low&~0xffff)|value if bits==16 else value
    flags=int.from_bytes(seed[40:48],'little')&flag_mask
    return struct.pack('<5Q',low,high,flags,address,input_address)+payload


def operand(spec):
    bits=spec['bits'];width={16:'word',32:'dword',64:'qword'}[bits]
    address='[r10+r9*2+3]' if spec['family']=='high' else ('[rcx+3]' if spec['offset']==3 else '[rcx]')
    if spec.get('alias'):reg={16:'cx',32:'ecx',64:'rcx'}[bits]
    elif spec['family']=='high':reg={16:'r11w',32:'r11d',64:'r11'}[bits]
    else:reg={16:'dx',32:'edx',64:'rdx'}[bits]
    return f'movbe {width} ptr {address},{reg}' if spec['direction']=='store' else f'movbe {reg},{width} ptr {address}'


assembly='.intel_syntax noprefix\n.text\n'
objects={mode:[] for mode in ['native','asan-ubsan']}
declarations=[];calls=[]


def compile_trace(name,code,pc):
    (out/f'{name}.s').write_text('.intel_syntax noprefix\n.text\n'+code)
    run([llvm/'clang','-target','x86_64-unknown-linux-gnu','-c',out/f'{name}.s','-o',out/f'{name}.elf.o'],name+'-assemble.log')
    run([llvm/'llvm-objcopy','-O','binary','--only-section=.text',out/f'{name}.elf.o',out/f'{name}.bin'],name+'-extract.log')
    run([out/'movbe-lift','--os','linux','--arch','amd64_avx','--address',str(pc),
         '--bytes',(out/f'{name}.bin').read_bytes().hex(),'--ir_out',out/f'{name}.ll'],name+'-lift.log')
    for mode in objects:
        flags=[] if mode=='native' else ['-fsanitize=address,undefined','-g']
        obj=out/f'{name}-{mode}.o'
        run([llvm/'clang','-target','arm64-apple-macos15','-O1',*flags,'-c',out/f'{name}.ll','-o',obj],name+'-'+mode+'-compile.log')
        objects[mode].append(obj)
    declarations.append(f'extern "C" Memory *sub_{pc:x}(State&,uint64_t,Memory*);')
    return f'{{"{name}",sub_{pc:x},{pc}}}'


for i,spec in enumerate(specs):
    low,high=('mov rdx,qword ptr [rdi+32]','movabs r11,0xfedcba9876543210') if spec['family']=='low' else ('movabs rdx,0xfedcba9876543210','mov r11,qword ptr [rdi+32]')
    pre=''
    if spec.get('inplace'):
        bits=spec['bits']
        pre={16:'movzx edx,word ptr [rcx]\n',32:'mov edx,dword ptr [rcx]\n',64:'mov rdx,qword ptr [rcx]\n'}[bits]
    code=('mov rcx,rdi\nmov r10,rdi\nmov r9,3\n'+low+'\n'+high+'\n'+pre+
          'mov rax,qword ptr [rdi+40]\npush rax\npopfq\n'+operand(spec)+
          '\npushfq\npop r8\nmov qword ptr [rsi],rdx\nmov qword ptr [rsi+8],r11\n'
          'mov qword ptr [rsi+16],r8\nmov qword ptr [rsi+24],rcx\nmov qword ptr [rsi+32],r10\nret\n')
    pc=0xc00000+i*0x1000;calls.append(compile_trace(spec['name'],code,pc))
    assembly+=f'.globl _native_{i}\n_native_{i}:\n'+code
(out/'native.s').write_text(assembly)
(out/'native.cpp').write_text('''#include <cstdio>
#include <cstdint>
#include <cstring>
#include "seeds.h"
'''+ '\n'.join(f'extern "C" void native_{i}(const void*,void*);' for i in range(len(specs)))+'''
using Fn=void(*)(const void*,void*);
Fn calls[]={'''+','.join(f'native_{i}' for i in range(len(specs)))+'''};
unsigned aliases[]={'''+','.join(str(s['bits'] if s.get('alias') else 0) for s in specs)+'''};
int main(){for(unsigned i=0;i<sizeof(calls)/sizeof(*calls);++i)for(auto &seed:seeds){
uint8_t payload[64];std::memcpy(payload,seed,64);uint64_t output[13]={};
calls[i](payload,output);const uint64_t pointer=reinterpret_cast<uint64_t>(payload);
if(output[4]!=pointer)return 2;
if(!aliases[i]){if(output[3]!=pointer)return 3;output[3]=0x100100;}
else if(aliases[i]==16){if((output[3]&~uint64_t(0xffff))!=(pointer&~uint64_t(0xffff)))return 4;
output[3]=(0x100100&~uint64_t(0xffff))|(output[3]&0xffff);}
output[4]=0x100100;output[2]&=0x8d5;std::memcpy(output+5,payload,64);
if(std::fwrite(output,1,sizeof(output),stdout)!=sizeof(output))return 5;
}}\n''')
run(['xcrun','clang++','-arch','x86_64',out/'native.cpp',out/'native.s','-o',out/'native'],'native-build.log')
run([out/'native'],'native-results.bin',env=dict(os.environ,ROSETTA_ADVERTISE_AVX='1'))
expected=(out/'native-results.bin').read_bytes()
independent=b''.join(expected_output(s,v) for s in specs for v in seeds)
assert expected==independent,'Native MOVBE differs from independent byte/register/flag expectations'
(out/'expected.h').write_text('static const unsigned char expected[]={'+','.join(map(str,expected))+'};\n')
# Raw forms exercise checked memory at the final valid word, one byte beyond,
# below the guest span, and an overflowing address; native faults are not invoked.
raw=[]
for bits in [16,32,64]:
    for direction in ['load','store']:
        spec=dict(bits=bits,direction=direction,family='low',offset=0)
        name=f'raw_{direction}{bits}';pc=0xd00000+len(raw)*0x1000
        entry=compile_trace(name,operand(spec)+'\nret\n',pc)
        raw.append((entry,bits//8,direction=='store'))
program='''#include "runtime.h"
#include "seeds.h"
#include "expected.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
Memory *dispatch(State &,uint64_t,Memory*){throw std::runtime_error("unexpected dispatch");}
extern "C" Memory *__remill_sync_hyper_call(State &,Memory *m,uint32_t){m->fault="unsupported instruction";return m;}
extern "C" Memory *__remill_error(State &,uint64_t,Memory *m){m->fault="unsupported instruction";return m;}
'''+ '\n'.join(declarations)+'''
struct Test{const char *name;Lifted function;uint64_t pc;};
static const Test tests[]={'''+','.join(calls)+'''};
static const Test raw_tests[]={'''+','.join(x[0] for x in raw)+'''};
static const unsigned widths[]={'''+','.join(str(x[1]) for x in raw)+'''};
static const bool stores[]={'''+','.join('true' if x[2] else 'false' for x in raw)+'''};
int main(){unsigned index=0,checks=0,failures=0;
for(auto &test:tests){unsigned seed_index=0;for(auto &seed:seeds){State state{};Memory memory;
state.gpr.rdi.qword=Memory::base+0x100;state.gpr.rsi.qword=Memory::base+0x300;
state.gpr.rsp.qword=Memory::base+0xff00;std::memcpy(memory.bytes.data()+0x100,seed,64);
write64(memory,state.gpr.rsp.qword,0xfeedface);test.function(state,test.pc,&memory);
uint64_t flags=read64(memory,Memory::base+0x310)&0x8d5;write64(memory,Memory::base+0x310,flags);
std::memcpy(memory.bytes.data()+0x328,memory.bytes.data()+0x100,64);
for(unsigned word=0;word<13;++word){uint64_t a,r;std::memcpy(&a,memory.bytes.data()+0x300+word*8,8);
std::memcpy(&r,expected+(index*'''+str(len(seeds))+'''+seed_index)*104+word*8,8);++checks;
if(a!=r){++failures;std::printf("FAIL %s seed%u word%u actual=%016llx reference=%016llx\\n",test.name,seed_index,word,(unsigned long long)a,(unsigned long long)r);}}
++checks;if(!memory.fault.empty()){++failures;std::printf("FAULT %s %s\\n",test.name,memory.fault.c_str());}++seed_index;}++index;}
unsigned boundary_checks=0;
for(unsigned i=0;i<std::size(raw_tests);++i)for(unsigned kind=0;kind<4;++kind){State state{};Memory memory;
std::fill(memory.bytes.begin(),memory.bytes.end(),0xa5);state.gpr.rsp.qword=Memory::base+0xff00;
write64(memory,state.gpr.rsp.qword,0xfeedface);state.gpr.rdx.qword=0x0123456789abcdef;
const auto width=widths[i];const uint64_t address=kind==0?Memory::base+memory.bytes.size()-width:
kind==1?Memory::base+memory.bytes.size()-width+1:kind==2?Memory::base-1:UINT64_MAX-width/2;
state.gpr.rcx.qword=address;const auto before=memory.bytes;
raw_tests[i].function(state,raw_tests[i].pc,&memory);++checks;++boundary_checks;
if(memory.fault.empty()!=(kind==0)){++failures;std::printf("FAULT POLICY %s kind%u %s\\n",raw_tests[i].name,kind,memory.fault.c_str());}
auto wanted=before;if(kind==0&&stores[i])for(unsigned b=0;b<width;++b)
wanted[wanted.size()-width+b]=uint8_t(0x0123456789abcdefULL>>(8*(width-1-b)));
++checks;++boundary_checks;if(memory.bytes!=wanted){++failures;std::printf("BOUNDARY BYTES %s kind%u\\n",raw_tests[i].name,kind);}}
std::printf("SUMMARY routines=%u vectors='''+str(len(seeds))+''' checks=%u boundary_checks=%u failures=%u\\n",index,checks,boundary_checks,failures);return failures?1:0;}
'''
(out/'review.cpp').write_text(program)
report={'routines':len(specs),'vectors':len(seeds),'raw_forms':len(raw),'runs':{},
        'oracle_sha256':hashlib.sha256(expected).hexdigest(),
        'patched_dataxfer_sha256':hashlib.sha256(data.read_bytes()).hexdigest(),
        'reference':'Intel SDM MOVBE (16/32/64 memory/register forms; arithmetic flags unchanged)',
        'reference_url':'https://cdrdv2-public.intel.com/671110/325383-sdm-vol-2abcd.pdf'}
for mode in objects:
    flags=[] if mode=='native' else ['-fsanitize=address,undefined','-g','-fno-omit-frame-pointer']
    binary=out/('review-'+mode)
    run([llvm/'clang++','-target','arm64-apple-macos15','-std=c++20','-O1',*flags,
         '-I'+str(cpu),'-I'+str(remill/'include'),'-I'+str(out),out/'review.cpp',
         cpu/'runtime.cpp',*objects[mode],'-o',binary],mode+'-review-build.log')
    run([binary],mode+'-results.txt',env=dict(os.environ,ASAN_OPTIONS='abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
    report['runs'][mode]=(out/(mode+'-results.txt')).read_text().strip();print(mode,report['runs'][mode],flush=True)
report['shared_backend_inputs_unchanged']=all(hashlib.sha256(p.read_bytes()).hexdigest()==hashes[str(p)] for p in shared)
(out/'review.json').write_text(json.dumps(report,indent=2)+'\n')
assert report['shared_backend_inputs_unchanged'],'Shared backend changed during isolated test'
