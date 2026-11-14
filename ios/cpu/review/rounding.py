#!/usr/bin/env python3
"""Private-backend SSE4.1 rounding differential test; shared backend is read-only.

Applies ios/remill-sse41-rounding.patch to an isolated SSE copy, builds private
semantics and a private lifter, and compares actual arm64 lifted execution to
native Rosetta. Masked cases compare exactly; Rosetta exception-mask forcing
requires separate AOT suppression/fault property tests. No device operations or
shared backend builds are performed.
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
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--backends", type=Path, required=True)
parser.add_argument("--llvm", type=Path, default=Path("/opt/homebrew/opt/llvm"))
args = parser.parse_args()
out = args.output.resolve()
if (
    len(out.parts) < 3
    or out.parts[1] != "Volumes"
    or not Path("/Volumes", out.parts[2]).is_mount()
):
    parser.error("Output must be on a mounted external build disk")
out.mkdir(parents=True, exist_ok=True)
remill = args.backends / "remill"
build = args.backends / "remill-build"
llvm = args.llvm / "bin"
cpu = Path(__file__).resolve().parents[1]
patch = cpu.parent / "remill-sse41-rounding.patch"
semantics = out / "semantics"
semantics.mkdir(exist_ok=True)
scratch = out / "source"
data = scratch / "lib/Arch/X86/Semantics/SSE.cpp"
data.parent.mkdir(parents=True, exist_ok=True)
shared = [
    remill / "lib/Arch/X86/Semantics/SSE.cpp",
    build / "lib/Arch/X86/Runtime/amd64_avx.bc",
    build / "bin/lift/remill-lift-23",
]
hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in shared}


def run(command, log, cwd=None, env=None):
    with (out / log).open("wb") as stream:
        subprocess.run(
            list(map(str, command)),
            stdout=stream,
            stderr=subprocess.STDOUT,
            cwd=cwd,
            env=env,
            check=True,
        )


def ninja_command(target):
    text = subprocess.check_output(
        ["ninja", "-C", str(build), "-t", "commands", target], text=True
    )
    return shlex.split(text.splitlines()[-1])


def replace_argument(command, flag, value):
    command[command.index(flag) + 1] = str(value)


shutil.copy2(shared[0], data)
check = subprocess.run(
    ["git", "apply", "--check", str(patch)], cwd=scratch, capture_output=True
)
if check.returncode:
    # Allow this regression test after the patch is integrated upstream locally.
    subprocess.run(
        ["git", "apply", "--reverse", "--check", str(patch)], cwd=scratch, check=True
    )
else:
    run(["git", "apply", patch], "apply-patch.log", cwd=scratch)
instructions = (remill / "lib/Arch/X86/Runtime/Instructions.cpp").read_text()
instructions = instructions.replace(
    '#include "lib/Arch/X86/Semantics/SSE.cpp"', "#include " + json.dumps(str(data))
)
(out / "Instructions.cpp").write_text(instructions)
command = ninja_command("lib/Arch/X86/Runtime/amd64_avx_Instructions.cpp.bc")
command = command[command.index("&&") + 1 :]
replace_argument(command, "-c", out / "Instructions.cpp")
replace_argument(command, "-o", out / "Instructions.bc")
run(command, "private-semantics-build.log", cwd=out)
run(
    [
        llvm / "llvm-link",
        out / "Instructions.bc",
        *[
            build / f"lib/Arch/X86/Runtime/amd64_avx_{part}.cpp.bc"
            for part in ("BasicBlock", "Intrinsics", "HyperCall")
        ],
        "-o",
        semantics / "amd64_avx.bc",
    ],
    "private-semantics-link.log",
)
# The public CLI has no semantics-directory argument. Compile only a private
# driver whose LoadArchSemantics call prefers our isolated directory; link it
# against read-only existing Remill/LLVM libraries.
lift = (remill / "bin/lift/Lift.cpp").read_text()
old = "remill::LoadArchSemantics(arch.get())"
assert old in lift
lift = lift.replace(
    old, "remill::LoadArchSemantics(arch.get(), {" + json.dumps(str(semantics)) + "})"
)
(out / "Lift.cpp").write_text(lift)
object_target = "bin/lift/CMakeFiles/remill-lift-23.dir/Lift.cpp.o"
command = ninja_command(object_target)
if Path(command[0]).name == "ccache":
    command = command[1:]
replace_argument(command, "-c", out / "Lift.cpp")
replace_argument(command, "-o", out / "Lift.o")
for flag in ("-MF", "-MT"):
    if flag in command:
        replace_argument(
            command, flag, out / ("Lift.o.d" if flag == "-MF" else "Lift.o")
        )
run(command, "private-lifter-build.log", cwd=build)
command = ninja_command("bin/lift/remill-lift-23")
command = command[command.index("&&") + 1 :]
command = command[: command.index("&&")]
command = [str(out / "Lift.o") if x == object_target else x for x in command]
replace_argument(command, "-o", out / "rounding-lift")
run(command, "private-lifter-link.log", cwd=build)

specs = []
for control in list(range(16)) + [0x89, 0xFF]:
    for kind in ["ps", "pd"]:
        for encoding, reg in [("legacy", "xmm"), ("vex128", "xmm"), ("vex256", "ymm")]:
            mnemonic = ("" if encoding == "legacy" else "v") + "round" + kind
            mem = "xmmword" if reg == "xmm" else "ymmword"
            for source_kind, source_operand in [
                ("reg", reg + "2"),
                ("mem", mem + " ptr [rdi+64]"),
            ]:
                specs.append(
                    (
                        f"{mnemonic}_{encoding}_{source_kind}_{control}",
                        f"{mnemonic} {reg}0,{source_operand},{control}",
                        control,
                    )
                )
    for kind, mem in [("ss", "dword"), ("sd", "qword")]:
        for encoding in ["legacy", "vex128"]:
            mnemonic = ("" if encoding == "legacy" else "v") + "round" + kind
            for source_kind, source_operand in [
                ("reg", "xmm2"),
                ("mem", mem + " ptr [rdi+64]"),
            ]:
                operands = (
                    "xmm0," + ("xmm1," if encoding == "vex128" else "") + source_operand
                )
                specs.append(
                    (
                        f"{mnemonic}_{encoding}_{source_kind}_{control}",
                        f"{mnemonic} {operands},{control}",
                        control,
                    )
                )
values32 = [
    0,
    0x80000000,
    0x3E800000,
    0xBE800000,
    0x3EFFFFFF,
    0xBEFFFFFF,
    0x3F000000,
    0xBF000000,
    0x3F000001,
    0xBF000001,
    0x3F3FFFFF,
    0xBF3FFFFF,
    0x3F800000,
    0xBF800000,
    0x3F800001,
    0xBF800001,
    0x3FC00000,
    0xBFC00000,
    0x40200000,
    0xC0200000,
    0x40600000,
    0xC0600000,
    0x4AFFFFFF,
    0xCAFFFFFF,
    0x4B000000,
    0xCB000000,
    0x4B000001,
    0xCB000001,
    0x7F7FFFFF,
    0xFF7FFFFF,
    1,
    0x80000001,
    0x007FFFFF,
    0x807FFFFF,
    0x00800000,
    0x80800000,
    0x7F800000,
    0xFF800000,
    0x7FC12345,
    0xFFC12345,
    0x7F812345,
    0xFF812345,
    0x7FFFFFFF,
    0xFFFFFFFF,
    0x100,
    0x80000100,
    0x3F7FFFFF,
    0xBF7FFFFF,
]
values64 = [
    0,
    0x8000000000000000,
    0x3FD0000000000000,
    0xBFD0000000000000,
    0x3FDFFFFFFFFFFFFF,
    0xBFDFFFFFFFFFFFFF,
    0x3FE0000000000000,
    0xBFE0000000000000,
    0x3FE0000000000001,
    0xBFE0000000000001,
    0x3FE7FFFFFFFFFFFF,
    0xBFE7FFFFFFFFFFFF,
    0x3FF0000000000000,
    0xBFF0000000000000,
    0x3FF0000000000001,
    0xBFF0000000000001,
    0x3FF8000000000000,
    0xBFF8000000000000,
    0x4004000000000000,
    0xC004000000000000,
    0x400C000000000000,
    0xC00C000000000000,
    0x432FFFFFFFFFFFFF,
    0xC32FFFFFFFFFFFFF,
    0x4330000000000000,
    0xC330000000000000,
    0x4330000000000001,
    0xC330000000000001,
    0x7FEFFFFFFFFFFFFF,
    0xFFEFFFFFFFFFFFFF,
    1,
    0x8000000000000001,
    0x000FFFFFFFFFFFFF,
    0x800FFFFFFFFFFFFF,
    0x0010000000000000,
    0x8010000000000000,
    0x7FF0000000000000,
    0xFFF0000000000000,
    0x7FF8000000012345,
    0xFFF8000000012345,
    0x7FF0000000012345,
    0xFFF0000000012345,
    0x7FFFFFFFFFFFFFFF,
    0xFFFFFFFFFFFFFFFF,
    0x100,
    0x8000000000000100,
    0x3FEFFFFFFFFFFFFF,
    0xBFEFFFFFFFFFFFFF,
]
seeds = []
for bits, values, fmt in [(32, values32, "<I"), (64, values64, "<Q")]:
    for index in range(len(values)):
        for rc in range(4):
            for daz in range(2):
                for ftz in range(2):
                    seed = bytearray()
                    # Different payloads in each register and lane expose scalar
                    # merge, inactive sNaNs and VEX destination/source confusion.
                    for reg in range(3):
                        for lane in range(256 // bits):
                            seed += struct.pack(
                                fmt, values[(index + lane + reg * 7) % len(values)]
                            )
                    # Rotate low source separately so every scalar edge is tested.
                    seed[64 : 64 + bits // 8] = struct.pack(fmt, values[index])
                    sticky = 0x3F if (index + rc + daz + ftz) & 1 else 0
                    csr = 0x1F80 | (rc << 13) | (daz << 6) | (ftz << 15) | sticky
                    seed += struct.pack("<I", csr) + bytes(12)
                    assert len(seed) == 112
                    seeds.append(seed)
(out / "seeds.h").write_text(
    "#include <cstdint>\nalignas(32) static const uint8_t seeds[][112]={"
    + ",".join("{" + ",".join(str(v) for v in seed) + "}" for seed in seeds)
    + "};\n"
)
assembly = ".intel_syntax noprefix\n.text\n"
objects, declarations, calls = [], [], []
for i, (name, instruction, control) in enumerate(specs):
    code = (
        "sub rsp,16\nstmxcsr dword ptr [rsp]\nldmxcsr dword ptr [rdi+96]\n"
        "vmovdqu ymm0,ymmword ptr [rdi]\nvmovdqu ymm1,ymmword ptr [rdi+32]\n"
        "vmovdqu ymm2,ymmword ptr [rdi+64]\n"
        + instruction
        + "\nvmovdqu ymmword ptr [rsi],ymm0\nvmovdqu ymmword ptr [rsi+32],ymm1\n"
        "vmovdqu ymmword ptr [rsi+64],ymm2\nstmxcsr dword ptr [rsi+96]\n"
        "ldmxcsr dword ptr [rsp]\nadd rsp,16\nvzeroupper\nret\n"
    )
    (out / f"{name}.s").write_text(".intel_syntax noprefix\n.text\n" + code)
    run(
        [
            llvm / "clang",
            "-target",
            "x86_64-unknown-linux-gnu",
            "-c",
            out / f"{name}.s",
            "-o",
            out / f"{name}.elf.o",
        ],
        name + "-assemble.log",
    )
    run(
        [
            llvm / "llvm-objcopy",
            "-O",
            "binary",
            "--only-section=.text",
            out / f"{name}.elf.o",
            out / f"{name}.bin",
        ],
        name + "-extract.log",
    )
    pc = 0x2000000 + i * 0x1000
    run(
        [
            out / "rounding-lift",
            "--os",
            "linux",
            "--arch",
            "amd64_avx",
            "--address",
            str(pc),
            "--bytes",
            (out / f"{name}.bin").read_bytes().hex(),
            "--ir_out",
            out / f"{name}.ll",
        ],
        name + "-lift.log",
    )
    run(
        [
            llvm / "clang",
            "-target",
            "arm64-apple-macos15",
            "-O2",
            "-c",
            out / f"{name}.ll",
            "-o",
            out / f"{name}.o",
        ],
        name + "-compile.log",
    )
    objects.append(out / f"{name}.o")
    declarations.append(f'extern "C" Memory *sub_{pc:x}(State&,uint64_t,Memory*);')
    calls.append(f'{{"{name}",sub_{pc:x},{pc},{control}}}')
    assembly += f".globl _native_{i}\n_native_{i}:\n" + code
    if (i + 1) % 20 == 0:
        print(
            f"Prepared rounding control{control}: {i + 1}/{len(specs)} traces",
            flush=True,
        )
(out / "native.s").write_text(assembly)
(out / "native.cpp").write_text(
    '#include <cstdio>\n#include <cstring>\n#include "seeds.h"\n'
    + "\n".join(
        f'extern "C" void native_{i}(const void*,void*);' for i in range(len(specs))
    )
    + "\nusing Fn=void(*)(const void*,void*);\nstruct Test{Fn function;unsigned control;};\nTest calls[]={"
    + ",".join(f"{{native_{i},{control}}}" for i, (_, _, control) in enumerate(specs))
    + """};
int main(){for(auto test:calls){unsigned index=0;for(auto &seed:seeds){alignas(32) uint8_t input[112],output[100];
std::memcpy(input,seed,sizeof(input));
test.function(input,output);if(fwrite(output,1,100,stdout)!=100)return 1;++index;}}}
"""
)
run(
    [
        "xcrun",
        "clang++",
        "-arch",
        "x86_64",
        out / "native.cpp",
        out / "native.s",
        "-o",
        out / "native",
    ],
    "native-build.log",
)
run(
    [out / "native"],
    "native-results.bin",
    env=dict(os.environ, ROSETTA_ADVERTISE_AVX="1"),
)
(out / "csr-probe.cpp").write_text(
    '#include <cstdio>\nint main(){unsigned old;asm volatile("stmxcsr %0":"=m"(old));unsigned inputs[]={0x1f80,0x0f80,0x1f00,0};for(unsigned input:inputs){unsigned stored;asm volatile("ldmxcsr %1; stmxcsr %0; ldmxcsr %2":"=m"(stored):"m"(input),"m"(old));printf("%u %u\\n",input,stored);}}\n'
)
run(
    [
        "xcrun",
        "clang++",
        "-arch",
        "x86_64",
        "-std=c++17",
        out / "csr-probe.cpp",
        "-o",
        out / "csr-probe",
    ],
    "csr-probe-build.log",
)
run(
    [out / "csr-probe"],
    "csr-probe.txt",
    env=dict(os.environ, ROSETTA_ADVERTISE_AVX="1"),
)
csr_observations = [
    {"requested": int(a), "stored": int(b)}
    for a, b in (
        line.split() for line in (out / "csr-probe.txt").read_text().splitlines()
    )
]
expected = out / "native-results.bin"
assert expected.stat().st_size == len(specs) * len(seeds) * 100
# Embed oracle bytes without a huge C++ initializer or compile-time AST.
(out / "expected.s").write_text(
    ".section __TEXT,__const\n.p2align 4\n.globl _round_expected\n_round_expected:\n.incbin "
    + json.dumps(str(expected))
    + "\n"
)
run(
    [
        "xcrun",
        "clang",
        "-arch",
        "arm64",
        "-c",
        out / "expected.s",
        "-o",
        out / "expected.o",
    ],
    "oracle-embed.log",
)
program = (
    """#include "runtime.h"
#include "seeds.h"
#include <cstdio>
#include <cstring>
#include <csetjmp>
#include <cstdlib>
extern "C" const unsigned char round_expected[];
static thread_local std::jmp_buf *fault_jump;
Memory *dispatch(State &,uint64_t,Memory*){throw std::runtime_error("unexpected dispatch");}
extern "C" Memory *__remill_sync_hyper_call(State &,Memory *m,uint32_t){m->fault="unsupported instruction";return m;}
extern "C" Memory *__remill_error(State &,uint64_t,Memory *m){m->fault="unmasked rounding exception";if(fault_jump)std::longjmp(*fault_jump,1);std::abort();}
"""
    + "\n".join(declarations)
    + """
struct Test{const char *name;Lifted function;uint64_t pc;unsigned control;};
static const Test tests[]={"""
    + ",".join(calls)
    + """};
static void setup(State &state,Memory &memory,const uint8_t *seed,uint32_t clear_mask=0){
state.x87.fxsave.mxcsr.flat=0x1f80;
state.gpr.rdi.qword=Memory::base+0x100;state.gpr.rsi.qword=Memory::base+0x300;state.gpr.rsp.qword=Memory::base+0xff00;
std::memcpy(memory.bytes.data()+0x100,seed,112);
uint32_t csr;std::memcpy(&csr,seed+96,4);csr&=~clear_mask;std::memcpy(memory.bytes.data()+0x160,&csr,4);
write64(memory,state.gpr.rsp.qword,0xfeedface);
}
int main(){uint64_t index=0,checks=0,failures=0,suppression_checks=0,fault_checks=0;
for(auto &test:tests){unsigned seed_index=0;for(auto &seed:seeds){State state{};Memory memory;setup(state,memory,seed);
try{test.function(state,test.pc,&memory);}catch(const std::exception &e){++failures;if(failures<=100)std::printf("FAULT %s %s\\n",test.name,e.what());}
for(unsigned word=0;word<25;++word){uint32_t a,r;std::memcpy(&a,memory.bytes.data()+0x300+word*4,4);
std::memcpy(&r,round_expected+(index*"""
    + str(len(seeds))
    + """+seed_index)*100+word*4,4);++checks;
if(a!=r){++failures;if(failures<=100)std::printf("FAIL %s seed%u word%u actual=%08x reference=%08x\\n",test.name,seed_index,word,a,r);}}
++seed_index;}++index;}
// Rosetta forces exception masks on LDMXCSR, so masked native comparisons above
// remain exact. Separately prove the architectural suppression invariant: PM
// unmasking changes only its stored control bit, never data, flags or faulting.
index=0;
for(auto &test:tests){if(test.control&8){for(unsigned seed_index=0;seed_index<sizeof(seeds)/sizeof(seeds[0]);seed_index+=3){
State state{};Memory memory;setup(state,memory,seeds[seed_index],0x1000);
try{test.function(state,test.pc,&memory);}catch(const std::exception &e){++failures;if(failures<=100)std::printf("SUPPRESSION FAULT %s %s\\n",test.name,e.what());}
for(unsigned word=0;word<25;++word){uint32_t a,r;std::memcpy(&a,memory.bytes.data()+0x300+word*4,4);
std::memcpy(&r,round_expected+(index*"""
    + str(len(seeds))
    + """+seed_index)*100+word*4,4);if(word==24)r&=~0x1000u;++suppression_checks;
if(a!=r){++failures;if(failures<=100)std::printf("SUPPRESSION FAIL %s seed%u word%u actual=%08x reference=%08x\\n",test.name,seed_index,word,a,r);}}
}}++index;}
// Unmasked new exceptions must report a fault before writing the destination.
for(auto &test:tests){bool precision=!std::strcmp(test.name,"vroundss_vex128_reg_0");
bool invalid=!std::strcmp(test.name,"vroundss_vex128_reg_9");if(!precision && !invalid)continue;
unsigned seed_index=(precision?16:40)*16;uint32_t clear_mask=precision?0x1000:0x80;
// This intrinsic is noreturn/nounwind: do not throw C++ through lifted IR.
// Heap state remains defined after the nonlocal jump; no guest RAII scope is live.
auto owner=std::make_unique<State>();auto storage=std::make_unique<Memory>();
auto &state=*owner;auto &memory=*storage;setup(state,memory,seeds[seed_index],clear_mask);
std::jmp_buf jump;fault_jump=&jump;if(!setjmp(jump))test.function(state,test.pc,&memory);fault_jump=nullptr;
bool trapped=memory.fault=="unmasked rounding exception";
uint32_t initial;std::memcpy(&initial,seeds[seed_index]+96,4);uint32_t expected_csr=(initial&~clear_mask)|(precision?0x20:1);
++fault_checks;if(!trapped ||
state.x87.fxsave.mxcsr.flat!=expected_csr || std::memcmp(&state.vec[0].ymm,seeds[seed_index],32)){
++failures;std::printf("FAIL fault-before-write %s csr=%08x expected=%08x trapped=%d\\n",test.name,state.x87.fxsave.mxcsr.flat,expected_csr,trapped);}}
if(fault_checks!=2)++failures;
std::printf("SUMMARY routines=%llu vectors=%zu native_checks=%llu suppression_checks=%llu fault_cases=%llu failures=%llu\\n",index,sizeof(seeds)/sizeof(seeds[0]),checks,suppression_checks,fault_checks,failures);return failures?1:0;}
"""
)
(out / "review.cpp").write_text(program)
run(
    [
        "xcrun",
        "clang++",
        "-arch",
        "arm64",
        "-std=c++20",
        "-O2",
        "-I" + str(cpu),
        "-I" + str(remill / "include"),
        "-I" + str(out),
        out / "review.cpp",
        cpu / "runtime.cpp",
        out / "expected.o",
        *objects,
        "-o",
        out / "review",
    ],
    "review-build.log",
)
with (out / "review-results.txt").open("w") as log:
    status = subprocess.run(
        [str(out / "review")], stdout=log, stderr=subprocess.STDOUT
    ).returncode
unchanged = all(
    hashlib.sha256(p.read_bytes()).hexdigest() == hashes[str(p)] for p in shared
)
report = {
    "routines": len(specs),
    "vectors": len(seeds),
    "dword_checks": len(specs) * len(seeds) * 25,
    "status": status,
    "shared_backend_inputs_unchanged": unchanged,
    "suppression_invariance_checks": sum(1 for _, _, control in specs if control & 8)
    * ((len(seeds) + 2) // 3)
    * 25,
    "unmasked_fault_before_write_cases": 2,
    "fault_capture": "setjmp intercept respects Remill noreturn/nounwind intrinsic; architectural guest #XM delivery is not implemented",
    "native_mxcsr_probe": csr_observations,
    "limitation": "Rosetta forces MXCSR exception masks; unmasked suppression and fault delivery are AOT property tests, not native trap-delivery comparisons",
    "oracle_sha256": hashlib.sha256(expected.read_bytes()).hexdigest(),
    "patched_sse_sha256": hashlib.sha256(data.read_bytes()).hexdigest(),
}
(out / "review.json").write_text(json.dumps(report, indent=2) + "\n")
print((out / "review-results.txt").read_text(), flush=True)
assert unchanged, "Shared backend changed during isolated test"
raise SystemExit(status)
