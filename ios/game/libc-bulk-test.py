#!/usr/bin/env python3
"""Verify production bulk-libc LLVM shims, exact byte semantics and native sanitizers."""

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--output", type=Path, required=True)
p.add_argument("--backend-root", type=Path, required=True)
p.add_argument("--llvm", type=Path, default=Path("/opt/homebrew/opt/llvm"))
a = p.parse_args()
root = a.output.resolve()
source = Path(__file__).resolve().parent
cpu = source.parent / "cpu"
if (
    len(root.parts) < 3
    or root.parts[1] != "Volumes"
    or not Path("/Volumes", root.parts[2]).is_mount()
):
    p.error("Output must be on a mounted external disk")
root.mkdir(parents=True, exist_ok=True)
generator = source / "native_libc_ir.py"
spec = importlib.util.spec_from_file_location("native_libc_ir", generator)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
for kind, pc in enumerate([0x30054A60, 0x30054A20, 0x300552E0]):
    (root / f"bulk-{kind}.ll").write_text(module.native_libc_ir(pc, kind))


def run(command, log):
    with (root / log).open("w") as stream:
        subprocess.run(
            [str(x) for x in command],
            stdout=stream,
            stderr=subprocess.STDOUT,
            check=True,
        )


includes = [
    "-I" + str(source),
    "-I" + str(cpu),
    "-I" + str(a.backend_root.resolve() / "remill/include"),
]
platforms = [
    ("mac", "arm64-apple-macos15", "macosx"),
    ("ios", "arm64-apple-ios18", "iphoneos"),
    ("simulator", "arm64-apple-ios18-simulator", "iphonesimulator"),
]
for name, target, sdk in platforms:
    directory = root / name
    directory.mkdir(exist_ok=True)
    sysroot = subprocess.check_output(
        ["xcrun", "--sdk", sdk, "--show-sdk-path"], text=True
    ).strip()
    for kind in range(3):
        run(
            [
                a.llvm / "bin/clang",
                "-target",
                target,
                "-isysroot",
                sysroot,
                "-O2",
                "-funwind-tables",
                "-c",
                root / f"bulk-{kind}.ll",
                "-o",
                directory / f"bulk-{kind}.o",
            ],
            f"{name}-shim-{kind}.log",
        )
    run(
        [
            "xcrun",
            "--sdk",
            sdk,
            "clang++",
            "-target",
            target,
            "-isysroot",
            sysroot,
            "-std=c++17",
            "-O2",
            *includes,
            "-c",
            source / "libc-bulk.cpp",
            "-o",
            directory / "libc-bulk.o",
        ],
        name + "-adapter.log",
    )
    run(
        [
            "xcrun",
            "libtool",
            "-static",
            "-o",
            directory / "libdcp-libc-bulk.a",
            directory / "libc-bulk.o",
            *[directory / f"bulk-{i}.o" for i in range(3)],
        ],
        name + "-archive.log",
    )
    print(name, "ABI/shims compiled", flush=True)
inputs = [
    cpu / "runtime.h",
    cpu / "runtime.cpp",
    source / "libc-bulk.h",
    source / "libc-bulk.cpp",
    source / "libc-bulk-fixture.cpp",
    generator,
]
manifest = {
    "sources": {
        str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs
    },
    "runs": {},
}
for name, flags in [
    ("native", []),
    ("asan-ubsan", ["-fsanitize=address,undefined"]),
    ("tsan", ["-fsanitize=thread"]),
]:
    binary = root / ("bulk-" + name)
    run(
        [
            "xcrun",
            "clang++",
            "-std=c++17",
            "-O1",
            "-g",
            "-pthread",
            "-fno-omit-frame-pointer",
            "-Wall",
            "-Wextra",
            "-Werror",
            *flags,
            *includes,
            cpu / "runtime.cpp",
            source / "libc-bulk.cpp",
            source / "libc-bulk-fixture.cpp",
            *[root / "mac" / f"bulk-{i}.o" for i in range(3)],
            "-o",
            binary,
        ],
        name + "-build.log",
    )
    env = dict(
        os.environ,
        ASAN_OPTIONS="abort_on_error=1",
        UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1",
        TSAN_OPTIONS="halt_on_error=1",
    )
    result = subprocess.run(
        [str(binary)], capture_output=True, text=True, env=env, timeout=180
    )
    (root / (name + "-stdout.log")).write_text(result.stdout)
    (root / (name + "-stderr.log")).write_text(result.stderr)
    manifest["runs"][name] = {
        "exit_code": result.returncode,
        "stdout": result.stdout,
        "stderr": result.stderr,
    }
    (root / "results.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(name, result.returncode, result.stdout.strip(), flush=True)
    if result.returncode:
        raise SystemExit(
            "Bulk " + name + " failed; inspect " + str(root / (name + "-stderr.log"))
        )
