#!/usr/bin/env python3
"""SSE binary NaN result bits: Rosetta x86-64 differential and guarded memory.

Private semantics/lifter builds read the shared backend without modifying it.
"""
import argparse, hashlib, json, os, re, shlex, shutil, struct, subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
p.add_argument('--backends',type=Path,required=True)
p.add_argument('--reuse-backend',action='store_true')
p.add_argument('--width-backend',type=Path,required=True)
p.add_argument('--llvm',type=Path,default=Path('/opt/homebrew/opt/llvm'))
args=p.parse_args();out=args.output.resolve()
if len(out.parts)<3 or out.parts[1]!='Volumes' or not Path('/Volumes',out.parts[2]).is_mount():
    p.error('Output must be on a mounted external build disk')
out.mkdir(parents=True,exist_ok=True)
remill=args.backends/'remill';build=args.backends/'remill-build';llvm=args.llvm/'bin'
cpu=Path(__file__).resolve().parents[2];patch=cpu.parent/'remill-mxcsr-status-experiment.patch'
width_source=args.width_backend/'remill';width_bc=args.width_backend/'remill-build/lib/Arch/X86/Runtime/amd64_avx.bc'
semantics=out/'semantics';semantics.mkdir(exist_ok=True)
scratch=out/'source';data=scratch/'lib/Arch/X86/Semantics'
data.mkdir(parents=True,exist_ok=True)
shared=[remill/'lib/Arch/X86/Semantics'/n for n in ('BINARY.cpp','SSE.cpp')]+[width_source/'lib/Arch/X86/Semantics'/n for n in ('BINARY.cpp','SSE.cpp')]+[width_bc,build/'lib/Arch/X86/Runtime/amd64_avx.bc',build/'bin/lift/remill-lift-23']
hashes={str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in shared}

def run(command,log,cwd=None,env=None):
    with (out/log).open('wb') as stream:
        subprocess.run(list(map(str,command)),stdout=stream,stderr=subprocess.STDOUT,
                       cwd=cwd,env=env,check=True)


def ninja_command(target):
    text=subprocess.check_output(['ninja','-C',str(build),'-t','commands',target],text=True)
    return shlex.split(text.splitlines()[-1])


def replace_argument(command,flag,value):
    command[command.index(flag)+1]=str(value)


if args.reuse_backend:
    meta=json.loads((out/'backend-preparation.json').read_text())
    assert meta['shared_hashes']==hashes
    assert meta['private_semantics_sha256']==hashlib.sha256((semantics/'amd64_avx.bc').read_bytes()).hexdigest()
    assert meta['private_lifter_sha256']==hashlib.sha256((out/'scalar-fp-lift').read_bytes()).hexdigest()
    check=out/'cache-source-check/lib/Arch/X86/Semantics';check.mkdir(parents=True,exist_ok=True)
    for name,digest in meta['source_files'].items():
        assert hashlib.sha256((data/name).read_bytes()).hexdigest()==digest
        shutil.copy2(data/name,check/name)
    run(['git','apply','--reverse',patch],'cache-patch-check.log',cwd=out/'cache-source-check')
    for name in meta['source_files']:
        assert (check/name).read_bytes()==(width_source/'lib/Arch/X86/Semantics'/name).read_bytes()
    print('Verified private backend cache reused',flush=True)
else:
    for filename in ('BINARY.cpp','SSE.cpp'):
        shutil.copy2(width_source/'lib/Arch/X86/Semantics'/filename,data/filename)
    check=subprocess.run(['git','apply','--check',str(patch)],cwd=scratch,capture_output=True)
    if check.returncode:
        subprocess.run(['git','apply','--reverse','--check',str(patch)],cwd=scratch,check=True)
    else:run(['git','apply',patch],'apply-patch.log',cwd=scratch)
    instructions=(remill/'lib/Arch/X86/Runtime/Instructions.cpp').read_text()
    for filename in ('BINARY.cpp','SSE.cpp'):
        instructions=instructions.replace('#include "lib/Arch/X86/Semantics/'+filename+'"',
                                          '#include '+json.dumps(str(scratch/'lib/Arch/X86/Semantics'/filename)))
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
    replace_argument(command,'-o',out/'scalar-fp-lift')
    run(command,'private-lifter-link.log',cwd=build)


    (out/'backend-preparation.json').write_text(json.dumps(dict(shared_hashes=hashes,
        private_semantics_sha256=hashlib.sha256((semantics/'amd64_avx.bc').read_bytes()).hexdigest(),
        private_lifter_sha256=hashlib.sha256((out/'scalar-fp-lift').read_bytes()).hexdigest(),
        source_files={n:hashlib.sha256((data/n).read_bytes()).hexdigest() for n in ('BINARY.cpp','SSE.cpp')},
        shared_backend_unchanged=all(hashlib.sha256(x.read_bytes()).hexdigest()==hashes[str(x)] for x in shared)),indent=2)+'\n')
    print('Private experimental MXCSR semantics and lifter built; shared backend unchanged',flush=True)
