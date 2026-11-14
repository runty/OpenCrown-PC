#!/usr/bin/env python3
"""Lift owned control-flow routines and prove bounded native tail dispatch."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--output", type=Path, required=True)
p.add_argument("--backend-root", type=Path, required=True)
p.add_argument("--llvm", type=Path, default=Path("/opt/homebrew/opt/llvm"))
a = p.parse_args()
r = a.output.resolve()
source = Path(__file__).resolve().parents[1]
backend = a.backend_root.resolve()
llvm = a.llvm.resolve() / "bin"
if (
    len(r.parts) < 3
    or r.parts[1] != "Volumes"
    or not Path("/Volumes", r.parts[2]).is_mount()
):
    p.error("Output must be on a mounted external disk")
r.mkdir(parents=True, exist_ok=True)
lifter = backend / "remill-build/bin/lift/remill-lift-23"
runtime_bc = backend / "remill-build/lib/Arch/X86/Runtime/amd64_avx.bc"
backend_sha = {
    str(path): hashlib.sha256(path.read_bytes()).hexdigest()
    for path in [lifter, runtime_bc]
}


def run(command, log):
    with (r / log).open("w") as f:
        subprocess.run(
            [str(x) for x in command], stdout=f, stderr=subprocess.STDOUT, check=True
        )


assembly = {
    0x1000: "jmp rsi",
    0x2000: "jmp rdi",
    0x3000: "mov rax,rdi\ncall helper\nxor rax,rsi\nret\n.p2align 4\nhelper: imul rax,rax,3\nret",
    0x4000: "mov rax,rdi\ncall rdx\nadd rax,rsi\nret\n.p2align 4\nhelper: ror rax,13\nxor rax,0x1234\nret",
}
traces = []
for pc, body in assembly.items():
    prefix = f"{pc:x}"
    asm = r / (prefix + ".s")
    asm.write_text(".intel_syntax noprefix\n.text\n" + body + "\n")
    run(
        [
            llvm / "clang",
            "-target",
            "x86_64-unknown-linux-gnu",
            "-c",
            asm,
            "-o",
            r / (prefix + ".elf.o"),
        ],
        prefix + "-assemble.log",
    )
    run(
        [
            llvm / "llvm-objcopy",
            "-O",
            "binary",
            "--only-section=.text",
            r / (prefix + ".elf.o"),
            r / (prefix + ".bin"),
        ],
        prefix + "-extract.log",
    )
    code = (r / (prefix + ".bin")).read_bytes().hex()
    for entry in [pc, pc + 16] if pc in [0x3000, 0x4000] else [pc]:
        ir = r / f"{entry:x}.ll"
        traces.append(ir)
        run(
            [
                backend / "remill-build/bin/lift/remill-lift-23",
                "--os",
                "linux",
                "--arch",
                "amd64_avx",
                "--address",
                pc,
                "--entry_address",
                entry,
                "--bytes",
                code,
                "--ir_out",
                ir,
            ],
            f"{entry:x}-lift.log",
        )
        text = ir.read_text().replace(" nounwind", "")
        for group in re.findall(r"^attributes #(\d+) = \{\s*\}$", text, re.M):
            text = re.sub(
                r"^attributes #" + group + r" = \{\s*\}\n", "", text, flags=re.M
            )
            text = re.sub(r" #" + group + r"\b", "", text)
        text, instrumented = re.subn(
            r"^(define ptr @sub_[0-9a-f]+\(ptr noalias %state, i64 %program_counter, ptr noalias %memory\)) \{",
            r"\1 uwtable {\n  call void @dcp_trace_enter(ptr %state, i64 %program_counter)",
            text,
            flags=re.M,
        )
        assert instrumented == 1, "Expected exactly one owned trace entry per IR module"
        text += "\ndeclare void @dcp_trace_enter(ptr, i64)\n"
        ir.write_text(text)
platforms = [
    ("mac", "arm64-apple-macos15", "macosx"),
    ("ios", "arm64-apple-ios18", "iphoneos"),
    ("simulator", "arm64-apple-ios18-simulator", "iphonesimulator"),
]
for platform, target, sdk in platforms:
    d = r / platform
    d.mkdir(exist_ok=True)
    sdkPath = subprocess.check_output(
        ["xcrun", "--sdk", sdk, "--show-sdk-path"], text=True
    ).strip()
    objects = []
    for ir in traces:
        obj = d / (ir.stem + ".o")
        objects.append(obj)
        run(
            [
                llvm / "clang",
                "-target",
                target,
                "-isysroot",
                sdkPath,
                "-O2",
                "-funwind-tables",
                "-c",
                ir,
                "-o",
                obj,
            ],
            platform + "-" + ir.stem + ".log",
        )
    for path, name in [
        (source / "runtime.cpp", "runtime"),
        (Path(__file__).with_suffix(".cpp"), "fixture"),
    ]:
        obj = d / (name + ".o")
        objects.append(obj)
        run(
            [
                "xcrun",
                "--sdk",
                sdk,
                "clang++",
                "-target",
                target,
                "-isysroot",
                sdkPath,
                "-std=c++17",
                "-O2",
                "-g",
                "-fno-omit-frame-pointer",
                "-I" + str(source),
                "-I" + str(backend / "remill/include"),
                "-c",
                path,
                "-o",
                obj,
            ],
            platform + "-" + name + ".log",
        )
    run(
        [
            "xcrun",
            "libtool",
            "-static",
            "-o",
            d / "libtail-dispatch-fixture.a",
            *objects,
        ],
        platform + "-archive.log",
    )
    print(platform, "compiled", flush=True)
    if platform == "mac":
        run(
            ["xcrun", "clang++", "-pthread", *objects, "-o", r / "tail-dispatch"],
            "mac-link.log",
        )
# Inspect relocation-bearing object disassembly, distinguishing B from BL.
nativeBranches = {}
for symbol in ["___remill_jump", "___remill_missing_block", "___remill_function_call"]:
    text = subprocess.check_output(
        [
            llvm / "llvm-objdump",
            "-dr",
            "--disassemble-symbols=" + symbol,
            r / "mac/runtime.o",
        ],
        text=True,
    )
    (r / (symbol + ".asm")).write_text(text)
    lines = text.splitlines()
    tail = False
    for i, line in enumerate(lines):
        if "ARM64_RELOC_BRANCH26" in line and "__Z8dispatchR5StateyP6Memory" in line:
            tail |= bool(i and re.search(r"\bb\s+0x", lines[i - 1]))
    assert tail, "No native B dispatch in " + symbol
    nativeBranches[symbol] = "B dispatch verified"
text = subprocess.check_output(
    [
        llvm / "llvm-objdump",
        "-dr",
        "--disassemble-symbols=__Z8dispatchR5StateyP6Memory",
        r / "mac/fixture.o",
    ],
    text=True,
)
(r / "dispatch.asm").write_text(text)
assert re.search(r"\bbr\s+x\d+", text), "No indirect BR trace target"
serviceTail = False
lines = text.splitlines()
for i, line in enumerate(lines):
    if "ARM64_RELOC_BRANCH26" in line and "tail_service" in line:
        serviceTail |= bool(i and re.search(r"\bb\s+0x", lines[i - 1]))
assert serviceTail, "No direct B service target"
result = subprocess.run(
    [str(r / "tail-dispatch")], capture_output=True, text=True, timeout=60
)
(r / "stdout.log").write_text(result.stdout)
(r / "stderr.log").write_text(result.stderr)
inputs = [
    source / "runtime.cpp",
    source / "runtime.h",
    Path(__file__).with_suffix(".cpp"),
    Path(__file__),
]
for path, digest in backend_sha.items():
    assert hashlib.sha256(Path(path).read_bytes()).hexdigest() == digest, (
        "Backend changed during fixture generation"
    )
report = {
    "sources": {
        str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs
    },
    "lifter_sha256": hashlib.sha256(
        (backend / "remill-build/bin/lift/remill-lift-23").read_bytes()
    ).hexdigest(),
    "native_tail_branches": nativeBranches,
    "indirect_trace_BR": True,
    "service_B": True,
    "backend_sha256": backend_sha,
    "ir_sha256": {
        str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in traces
    },
    "exit_code": result.returncode,
    "stdout": result.stdout,
    "stderr": result.stderr,
}
(r / "results.json").write_text(json.dumps(report, indent=2) + "\n")
print(result.stdout)
print(result.stderr)
if result.returncode:
    raise SystemExit(result.returncode)
