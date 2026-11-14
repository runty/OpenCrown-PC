#!/usr/bin/env python3
"""Compare synthetic control/rounding traces with the native x87/SSE oracle."""

import argparse
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument("--build", type=Path, required=True)
p.add_argument("--backend-root", type=Path, required=True)
p.add_argument("--lifter", type=Path)
p.add_argument("--llvm", type=Path, default=Path("/opt/homebrew/opt/llvm"))
p.add_argument("--expect-stock-failure", action="store_true")
a = p.parse_args()
r = a.build.resolve()
b = a.backend_root.resolve()
s = Path(__file__).resolve().parent
repo = s.parents[2]
if (
    len(r.parts) < 3
    or r.parts[1] != "Volumes"
    or not Path("/Volumes", r.parts[2]).is_mount()
):
    p.error("Build output must be on a mounted external disk")
lifter = a.lifter or b / "remill-build/bin/lift/remill-lift-23"


def run(command, log):
    with (r / log).open("w") as f:
        subprocess.run(
            [str(x) for x in command], stdout=f, stderr=subprocess.STDOUT, check=True
        )


run(
    [
        "xcrun",
        "clang++",
        "-arch",
        "x86_64",
        "-std=c++17",
        "-O2",
        s / "control-fixture.cpp",
        "-o",
        r / "control-native",
    ],
    "control-native-build.log",
)
run([r / "control-native"], "control-native-results.txt")
objects = []
for name, pc, code in [
    ("control", 4096, "d92ed93fc3"),
    ("independence", 8192, "0fae16d96e040fae1fd97f04dbe30fae5f08d97f0cc3"),
]:
    ir = r / (name + "-tested.ll")
    obj = r / (name + "-tested.o")
    objects.append(obj)
    run(
        [
            lifter,
            "--os",
            "linux",
            "--arch",
            "amd64_avx",
            "--bytes",
            code,
            "--address",
            pc,
            "--ir_out",
            ir,
        ],
        name + "-tested-lift.log",
    )
    run(
        [
            a.llvm / "bin/clang",
            "-target",
            "arm64-apple-macos15",
            "-O2",
            "-c",
            ir,
            "-o",
            obj,
        ],
        name + "-tested-compile.log",
    )
run(
    [
        "xcrun",
        "clang++",
        "-std=c++17",
        "-O2",
        "-DCONTROL_LIFTED",
        "-I" + str(repo / "ios/cpu"),
        "-I" + str(b / "remill/include"),
        s / "control-fixture.cpp",
        repo / "ios/cpu/runtime.cpp",
        *objects,
        r / "mac/libdcp-fp80.a",
        "-o",
        r / "control-tested",
    ],
    "control-tested-build.log",
)
result = subprocess.run([str(r / "control-tested")], capture_output=True, text=True)
(r / "control-tested-results.txt").write_text(result.stdout + result.stderr)
assert result.returncode == (1 if a.expect_stock_failure else 0), (
    result.stdout + result.stderr
)
print((r / "control-native-results.txt").read_text(), result.stdout)
