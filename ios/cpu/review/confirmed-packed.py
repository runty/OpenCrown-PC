#!/usr/bin/env python3
"""Private-backend proof for the remaining confirmed packed SIMD forms.

Applies rounding plus the confirmed packed-float/integer patches to isolated
source copies, builds private
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
patches = [
    cpu.parent / name
    for name in [
        "remill-sse41-rounding.patch",
        "remill-confirmed-packed-float.patch",
        "remill-confirmed-packed-integer.patch",
    ]
]
semantics = out / "semantics"
semantics.mkdir(exist_ok=True)
scratch = out / "source"
files = ["SSE.cpp", "MMX.cpp", "AVX.cpp"]
shared = [
    *[remill / ("lib/Arch/X86/Semantics/" + name) for name in files],
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


for name in files:
    path = scratch / "lib/Arch/X86/Semantics" / name
    path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(remill / "lib/Arch/X86/Semantics" / name, path)
for patch in patches:
    check = subprocess.run(
        ["git", "apply", "--check", str(patch)], cwd=scratch, capture_output=True
    )
    if check.returncode:
        subprocess.run(
            ["git", "apply", "--reverse", "--check", str(patch)],
            cwd=scratch,
            check=True,
        )
    else:
        run(["git", "apply", patch], patch.stem + "-apply.log", cwd=scratch)
instructions = (remill / "lib/Arch/X86/Runtime/Instructions.cpp").read_text()
for name in files:
    instructions = instructions.replace(
        '#include "lib/Arch/X86/Semantics/' + name + '"',
        "#include " + json.dumps(str(scratch / "lib/Arch/X86/Semantics" / name)),
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
replace_argument(command, "-o", out / "confirmed-lift")
run(command, "private-lifter-link.log", cwd=build)


specs = []


def binary(group, name, mnemonic, reg):
    for alias, a, b in [
        ("reg", reg + "1", reg + "2"),
        ("first_alias", reg + "0", reg + "2"),
        ("second_alias", reg + "1", reg + "0"),
    ]:
        specs.append((group, name + "_" + alias, f"{mnemonic} {reg}0,{a},{b}"))


binary("horizontal", "hsub256", "vhsubps", "ymm")
for kind in ["lps", "hps", "lpd", "hpd"]:
    binary("floating_unpack", "unpack_" + kind, "vunpck" + kind, "ymm")
for name in ["vpmulld", "vpunpckhdq", "vpunpckhwd", "vpunpcklwd", "vpunpckldq"]:
    binary("integer", name, name, "xmm")
for alias, source in [("mem", "xmm1"), ("mem_first_alias", "xmm0")]:
    specs.append(
        (
            "integer",
            "vpunpckldq_" + alias,
            f"vpunpckldq xmm0,{source},xmmword ptr [rdi+64]",
        )
    )
specs.append(
    ("integer", "vpunpckldq_mem_unaligned", "vpunpckldq xmm0,xmm1,xmmword ptr [rdi+65]")
)
for alias, source in [("mem", "ymm1"), ("mem_first_alias", "ymm0")]:
    specs.append(
        ("minmax", "minps256_" + alias, f"vminps ymm0,{source},ymmword ptr [rdi+64]")
    )
specs.append(
    ("minmax", "minps256_mem_unaligned", "vminps ymm0,ymm1,ymmword ptr [rdi+65]")
)
binary("minmax", "maxps128", "vmaxps", "xmm")
binary("minmax", "maxps256", "vmaxps", "ymm")
for alias, source in [("reg", "xmm2"), ("source_alias", "xmm0")]:
    specs.append(("sqrt", "sqrtps128_" + alias, f"vsqrtps xmm0,{source}"))
# Every ordered special-value pair under all RC/DAZ/FTZ combinations. Status
# flags and architectural exception delivery are deliberately not claimed.
values = [
    0,
    0x80000000,
    1,
    0x80000001,
    0x3F800000,
    0xBF800000,
    0x3F800001,
    0xBF800001,
    0x00800000,
    0x80800000,
    0x007FFFFF,
    0x807FFFFF,
    0x7F7FFFFF,
    0xFF7FFFFF,
    0x7F800000,
    0xFF800000,
    0x7FC12345,
    0xFFC12345,
    0x7F812345,
    0xFF812345,
    0x7FFFFFFF,
    0xFFFFFFFF,
    0x55555555,
    0xAAAAAAAA,
]
seeds = []
for left in range(len(values)):
    for right in range(len(values)):
        for rc in range(4):
            for daz in range(2):
                for ftz in range(2):
                    first = [
                        values[left],
                        values[right],
                        values[(left + 3) % 24],
                        values[(right + 5) % 24],
                        values[(left + 7) % 24],
                        values[(right + 11) % 24],
                        values[(left + 13) % 24],
                        values[(right + 17) % 24],
                    ]
                    second = [
                        values[right],
                        values[left],
                        values[(right + 9) % 24],
                        values[(left + 15) % 24],
                        values[(right + 4) % 24],
                        values[(left + 8) % 24],
                        values[(right + 12) % 24],
                        values[(left + 16) % 24],
                    ]
                    dest = [values[(left + lane * 5) % 24] for lane in range(8)]
                    seed = bytearray(struct.pack("<24I", *(dest + first + second)))
                    seed += struct.pack(
                        "<I", 0x1F80 | (rc << 13) | (daz << 6) | (ftz << 15)
                    ) + bytes(12)
                    seeds.append(seed)
for index in range(64):
    state = 0x9E3779B9 * (index + 1)
    words = []
    for _ in range(24):
        state = (1664525 * state + 1013904223) & 0xFFFFFFFF
        words.append(state)
    seeds.append(struct.pack("<24I", *words) + struct.pack("<I", 0x1F80) + bytes(12))
(out / "seeds.h").write_text(
    "#include <cstdint>\nalignas(32) static const uint8_t seeds[][112]={"
    + ",".join("{" + ",".join(str(v) for v in seed) + "}" for seed in seeds)
    + "};\n"
)
assembly = ".intel_syntax noprefix\n.text\n"
objects, declarations, calls = [], [], []
for i, (group, name, instruction) in enumerate(specs):
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
    pc = 0x3000000 + i * 0x1000
    run(
        [
            out / "confirmed-lift",
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
    calls.append(f'{{"{group}","{name}",sub_{pc:x},{pc}}}')
    assembly += f".globl _native_{i}\n_native_{i}:\n" + code
(out / "native.s").write_text(assembly)
(out / "native.cpp").write_text(
    '#include <cstdio>\n#include "seeds.h"\n'
    + "\n".join(
        f'extern "C" void native_{i}(const void*,void*);' for i in range(len(specs))
    )
    + "\nusing Fn=void(*)(const void*,void*);\nFn calls[]={"
    + ",".join(f"native_{i}" for i in range(len(specs)))
    + "};\nint main(){for(auto fn:calls)for(auto &seed:seeds){uint8_t output[100];fn(seed,output);if(fwrite(output,1,100,stdout)!=100)return 1;}}\n"
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
expected = out / "native-results.bin"
assert expected.stat().st_size == len(specs) * len(seeds) * 100
(out / "expected.s").write_text(
    ".section __TEXT,__const\n.p2align 4\n.globl _packed_expected\n_packed_expected:\n.incbin "
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
extern "C" const unsigned char packed_expected[];
Memory *dispatch(State &,uint64_t,Memory*){throw std::runtime_error("unexpected dispatch");}
extern "C" Memory *__remill_sync_hyper_call(State &,Memory *m,uint32_t){m->fault="unsupported instruction";return m;}
extern "C" Memory *__remill_error(State &,uint64_t,Memory *m){m->fault="unsupported instruction";return m;}
"""
    + "\n".join(declarations)
    + """
struct Test{const char *group;const char *name;Lifted function;uint64_t pc;};
static const Test tests[]={"""
    + ",".join(calls)
    + """};
int main(){uint64_t index=0,checks=0,failures=0,group_checks=0,group_failures=0,group_routines=0;
for(auto &test:tests){unsigned seed_index=0;for(auto &seed:seeds){State state{};Memory memory;
state.x87.fxsave.mxcsr.flat=0x1f80;
state.gpr.rdi.qword=Memory::base+0x100;state.gpr.rsi.qword=Memory::base+0x300;state.gpr.rsp.qword=Memory::base+0xff00;
std::memcpy(memory.bytes.data()+0x100,seed,sizeof(seed));write64(memory,state.gpr.rsp.qword,0xfeedface);
test.function(state,test.pc,&memory);
for(unsigned word=0;word<25;++word){uint32_t a,r;std::memcpy(&a,memory.bytes.data()+0x300+word*4,4);
std::memcpy(&r,packed_expected+(index*"""
    + str(len(seeds))
    + """+seed_index)*100+word*4,4);
if(word==24){a&=~0x3fu;r&=~0x3fu;}++checks;++group_checks;
if(a!=r){++failures;++group_failures;if(failures<=100)std::printf("FAIL %s seed%u word%u actual=%08x reference=%08x\\n",test.name,seed_index,word,a,r);}}
if(!memory.fault.empty()){++failures;++group_failures;if(failures<=100)std::printf("FAULT %s %s\\n",test.name,memory.fault.c_str());}++seed_index;}
++index;++group_routines;
if(index==sizeof(tests)/sizeof(tests[0]) || std::strcmp(tests[index].group,test.group)){
std::printf("GROUP %s routines=%llu checks=%llu failures=%llu\\n",test.group,group_routines,group_checks,group_failures);std::fflush(stdout);group_checks=group_failures=group_routines=0;}}
std::printf("SUMMARY routines=%llu vectors=%zu checks=%llu failures=%llu\\n",index,sizeof(seeds)/sizeof(seeds[0]),checks,failures);return failures?1:0;}
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
    "oracle_sha256": hashlib.sha256(expected.read_bytes()).hexdigest(),
    "patch_sha256": {
        p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in patches
    },
    "limitations": "New FP value helpers do not model sticky SIMD exception flags or #XM delivery; native comparisons exclude only MXCSR status bits0..5, preserving control-bit checks",
}
(out / "review.json").write_text(json.dumps(report, indent=2) + "\n")
(out / "specs.json").write_text(json.dumps(specs, indent=2) + "\n")
print((out / "review-results.txt").read_text(), flush=True)
assert unchanged, "Shared backend changed during isolated test"
raise SystemExit(status)
