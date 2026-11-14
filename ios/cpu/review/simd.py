#!/usr/bin/env python3
"""Independent full-YMM differential checks for the OpenCrown Remill SSE additions."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--output", type=Path, required=True)
parser.add_argument(
    "--backends",
    type=Path,
    default=Path("/Volumes/office/Build/OpenCrown/cpu-backends"),
)
parser.add_argument("--llvm", type=Path, default=Path("/opt/homebrew/opt/llvm"))
args = parser.parse_args()
out = args.output.resolve()
if (
    len(out.parts) < 3
    or out.parts[1] != "Volumes"
    or not Path("/Volumes", out.parts[2]).is_mount()
):
    parser.error("External output directory required")
out.mkdir(parents=True, exist_ok=True)
source = Path(__file__).resolve().parents[1]
llvm = args.llvm / "bin"
lifter = args.backends / "remill-build/bin/lift/remill-lift-23"
remill = args.backends / "remill"


def run(command, log, env=None):
    with (out / log).open("w") as stream:
        subprocess.run(
            list(map(str, command)),
            stdout=stream,
            stderr=subprocess.STDOUT,
            env=env,
            check=True,
        )


specs = [
    ("insert_legacy_reg", "insertps xmm0,xmm1,0xe1", False),
    ("insert_legacy_mem", "insertps xmm0,dword ptr [rdi+64],0xd1", False),
    ("insert_vex_reg", "vinsertps xmm0,xmm1,xmm2,0x90", False),
    ("insert_vex_mem", "vinsertps xmm0,xmm1,dword ptr [rdi+64],0xc0", False),
    ("blendps_legacy_reg", "blendps xmm0,xmm1,0xf5", False),
    ("blendps_legacy_mem", "blendps xmm0,xmmword ptr [rdi+64],0xa", False),
    ("blendps_vex128", "vblendps xmm0,xmm1,xmm2,0x5", False),
    ("blendps_vex256", "vblendps ymm0,ymm1,ymm2,0xa5", False),
    ("blendps_mem256", "vblendps ymm0,ymm1,ymmword ptr [rdi+64],0xa5", False),
    ("blendpd_legacy_reg", "blendpd xmm0,xmm1,0xf2", False),
    ("blendpd_legacy_mem", "blendpd xmm0,xmmword ptr [rdi+64],0x1", False),
    ("blendpd_vex128", "vblendpd xmm0,xmm1,xmm2,0x1", False),
    ("blendpd_vex256", "vblendpd ymm0,ymm1,ymm2,0x6", False),
    ("blendpd_mem256", "vblendpd ymm0,ymm1,ymmword ptr [rdi+64],0x9", False),
    ("permilps_128", "vpermilps xmm0,xmm1,0x1b", False),
    ("permilps_256", "vpermilps ymm0,ymm1,0x1b", False),
    ("permilps_mem256", "vpermilps ymm0,ymmword ptr [rdi+64],0x4e", False),
    ("permilpd_128", "vpermilpd xmm0,xmm1,0x2", False),
    ("permilpd_256", "vpermilpd ymm0,ymm1,0x6", False),
    ("permilpd_mem256", "vpermilpd ymm0,ymmword ptr [rdi+64],0x9", False),
    ("shufps_legacy", "shufps xmm0,xmm1,0x1b", False),
    ("shufps_legacy_mem", "shufps xmm0,xmmword ptr [rdi+64],0xe4", False),
    ("shufps_vex128", "vshufps xmm0,xmm1,xmm2,0x1b", False),
    ("shufps_vex256", "vshufps ymm0,ymm1,ymm2,0x1b", False),
    ("shufps_mem256", "vshufps ymm0,ymm1,ymmword ptr [rdi+64],0xe4", False),
]
for mnemonic in ("rcpps", "rsqrtps"):
    specs.extend(
        [
            (mnemonic + "_legacy", f"{mnemonic} xmm0,xmm1", True),
            (mnemonic + "_legacy_mem", f"{mnemonic} xmm0,xmmword ptr [rdi+64]", True),
            (mnemonic + "_vex128", f"v{mnemonic} xmm0,xmm1", True),
            (mnemonic + "_vex256", f"v{mnemonic} ymm0,ymm1", True),
            (mnemonic + "_mem256", f"v{mnemonic} ymm0,ymmword ptr [rdi+64]", True),
        ]
    )

assembly = ".intel_syntax noprefix\n.text\n"
objects = []
decl = []
calls = []
native_calls = []
for i, (name, instruction, approximate) in enumerate(specs):
    code = (
        "vmovdqu ymm0,ymmword ptr [rdi]\nvmovdqu ymm1,ymmword ptr [rdi+32]\nvmovdqu ymm2,ymmword ptr [rdi+64]\n"
        + instruction
        + "\nvmovdqu ymmword ptr [rsi],ymm0\nvzeroupper\nret\n"
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
    pc = 0x900000 + i * 0x1000
    run(
        [
            lifter,
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
    decl.append(f'extern "C" Memory *sub_{pc:x}(State&,uint64_t,Memory*);')
    calls.append(
        f'{{"{name}",sub_{pc:x},{pc},{str(approximate).lower()},{8 if "256" in name else 4}}}'
    )
    assembly += f".globl _native_{i}\n_native_{i}:\n" + code
    native_calls.append(f'extern "C" void native_{i}(const void*,void*);')
(out / "native.s").write_text(assembly)
# Byte-pattern vectors distinguish both 128-bit groups, all selectors, and upper preservation.
seeds = []
special = [
    0,
    0x80000000,
    1,
    0x80000001,
    0x7F800000,
    0xFF800000,
    0x7FC00001,
    0xFF800001,
    0x00800000,
    0x80800000,
    0x7E7FFFFF,
    0x7E800000,
    0x7E800001,
    0xFE800000,
    0x3F800000,
    0x40800000,
    0x7F7FFFFF,
    0xFF7FFFFF,
    0x3F000000,
    0xBF000000,
    0x3EAAAAAB,
    0x40000000,
    0x007FFFFF,
    0x807FFFFF,
]
for seed in range(24):
    if seed < 3:
        seeds.append(special[seed * 8 :] + special[: seed * 8])
    else:
        state = seed * 0x9E3779B9
        values = []
        for j in range(24):
            state = (1664525 * state + 1013904223) & 0xFFFFFFFF
            values.append(state)
        seeds.append(values)
header = (
    "#include <cstdint>\nstatic const uint32_t seeds[][24]={"
    + ",".join("{" + ",".join(hex(v) for v in values) + "}" for values in seeds)
    + "};\n"
)
(out / "seeds.h").write_text(header)
native = (
    '#include <cstdio>\n#include "seeds.h"\n'
    + "\n".join(native_calls)
    + "\nusing Fn=void(*)(const void*,void*);\nFn calls[]={"
    + ",".join("native_" + str(i) for i in range(len(specs)))
    + "};\nint main(){for(auto fn:calls)for(auto &seed:seeds){uint32_t output[8];fn(seed,output);if(fwrite(output,1,32,stdout)!=32)return 1;}}\n"
)
(out / "native.cpp").write_text(native)
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
run([out / "native"], "native-results.bin", dict(os.environ, ROSETTA_ADVERTISE_AVX="1"))
expected = (out / "native-results.bin").read_bytes()
(out / "expected.h").write_text(
    "static const unsigned char expected[]={"
    + ",".join(str(v) for v in expected)
    + "};\n"
)
program = (
    """#include "runtime.h"
#include "seeds.h"
#include "expected.h"
#include <cmath>
#include <cstdio>
#include <cstring>
Memory *dispatch(State &,uint64_t,Memory*){throw std::runtime_error("unexpected dispatch");}
extern "C" Memory *__remill_sync_hyper_call(State &,Memory *memory,uint32_t){memory->fault="unsupported instruction";return memory;}
extern "C" Memory *__remill_error(State &,uint64_t,Memory *memory){memory->fault="unsupported instruction";return memory;}
"""
    + "\n".join(decl)
    + """
struct Test{const char *name;Lifted function;uint64_t pc;bool approximate;unsigned active;};
static const Test tests[]={"""
    + ",".join(calls)
    + """};
int main(){unsigned test_index=0,failures=0,checks=0;
for(auto &test:tests){unsigned seed_index=0;for(auto &seed:seeds){
State state{};Memory memory;state.gpr.rdi.qword=Memory::base+0x100;state.gpr.rsi.qword=Memory::base+0x200;state.gpr.rsp.qword=Memory::base+0xff00;
std::memcpy(memory.bytes.data()+0x100,seed,sizeof(seed));write64(memory,state.gpr.rsp.qword,0xfeedface);
test.function(state,test.pc,&memory);
for(unsigned lane=0;lane<8;++lane){uint32_t actual,reference;std::memcpy(&actual,memory.bytes.data()+0x200+lane*4,4);std::memcpy(&reference,expected+(test_index*24+seed_index)*32+lane*4,4);
bool equal=actual==reference;
if(!equal && test.approximate && lane<test.active){float a,r;std::memcpy(&a,&actual,4);std::memcpy(&r,&reference,4);if(std::isnan(r))equal=std::isnan(a);else if(std::isfinite(r) && r!=0)equal=std::isfinite(a) && std::fabs((a-r)/r)<=0.000367f;}
++checks;if(!equal){++failures;std::printf("FAIL %s seed%u lane%u actual=%08x reference=%08x\\n",test.name,seed_index,lane,actual,reference);}}
if(!memory.fault.empty()){++failures;std::printf("FAULT %s %s\\n",test.name,memory.fault.c_str());}++seed_index;}++test_index;}
std::printf("SUMMARY routines=%u checks=%u failures=%u\\n",test_index,checks,failures);return failures?1:0;}
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
        "-I" + str(source),
        "-I" + str(remill / "include"),
        "-I" + str(out),
        out / "review.cpp",
        source / "runtime.cpp",
        *objects,
        "-o",
        out / "review",
    ],
    "review-build.log",
)
# Preserve nonzero differential results rather than hiding the useful mismatch output.
with (out / "review-results.txt").open("w") as log:
    status = subprocess.run(
        [str(out / "review")], stdout=log, stderr=subprocess.STDOUT
    ).returncode
report = {
    "routines": len(specs),
    "vectors": len(seeds),
    "status": status,
    "sse_source_sha256": hashlib.sha256(
        (remill / "lib/Arch/X86/Semantics/SSE.cpp").read_bytes()
    ).hexdigest(),
}
(out / "review.json").write_text(json.dumps(report, indent=2) + "\n")
print((out / "review-results.txt").read_text())
raise SystemExit(status)
