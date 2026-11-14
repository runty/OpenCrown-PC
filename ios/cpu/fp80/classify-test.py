#!/usr/bin/env python3
"""Compare actual lowered mask144 IR with owned native x86 LLVM oracles."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
root = args.output.resolve()
if (
    len(root.parts) < 3
    or root.parts[1] != "Volumes"
    or not Path("/Volumes", root.parts[2]).is_mount()
):
    parser.error("Output must be on a mounted external disk")
root.mkdir(parents=True, exist_ok=True)
source = Path(__file__).resolve().parent
llvm = Path("/opt/homebrew/opt/llvm/bin")
lowerer = args.build.resolve() / "fp80-lower"
lowerer_sha = hashlib.sha256(lowerer.read_bytes()).hexdigest()


def run(command, name):
    with (root / (name + ".log")).open("w") as output:
        subprocess.run(
            list(map(str, command)),
            stdout=output,
            stderr=subprocess.STDOUT,
            check=True,
            env=dict(os.environ, ROSETTA_ADVERTISE_AVX="1"),
            timeout=120,
        )


ir = "declare i1 @llvm.is.fpclass.f80(x86_fp80, i32 immarg)\n"
for name, attr in [("oracle144", ""), ("oracle144_strict", "strictfp")]:
    ir += f"""define i32 @{name}(ptr %pointer) {attr} {{
  %value = load x86_fp80, ptr %pointer, align 1
  %class = call i1 @llvm.is.fpclass.f80(x86_fp80 %value, i32 144)
  %result = zext i1 %class to i32
  ret i32 %result
}}
"""
original = root / "original.ll"
original.write_text(ir)
run(
    [
        llvm / "clang",
        "-target",
        "x86_64-apple-macos15",
        "-O2",
        "-c",
        original,
        "-o",
        root / "oracle.o",
    ],
    "native-ir",
)
run(
    [
        llvm / "clang",
        "-target",
        "x86_64-apple-macos15",
        "-O2",
        "-c",
        source / "classify-predicate.c",
        "-o",
        root / "predicate.o",
    ],
    "predicate",
)
run(
    [
        "xcrun",
        "clang++",
        "-arch",
        "x86_64",
        "-std=c++17",
        "-O2",
        source / "classify-native.cpp",
        root / "oracle.o",
        root / "predicate.o",
        "-o",
        root / "native",
    ],
    "native-build",
)
run([root / "native", root / "vectors.bin"], "native-results")
native = json.loads((root / "native-results.log").read_text())
assert native["failed"] == 0
lowered = root / "lowered.ll"
run([lowerer, original, lowered], "lowering")
text = lowered.read_text()
assert (
    "x86_fp80" not in text and "dcp_fp80_" not in text and "llvm.is.fpclass" not in text
)
for platform, target, sdk in [
    ("mac", "arm64-apple-macos15", "macosx"),
    ("ios", "arm64-apple-ios18", "iphoneos"),
    ("simulator", "arm64-apple-ios18-simulator", "iphonesimulator"),
]:
    sdk_path = subprocess.check_output(
        ["xcrun", "--sdk", sdk, "--show-sdk-path"], text=True
    ).strip()
    run(
        [
            llvm / "clang",
            "-target",
            target,
            "-isysroot",
            sdk_path,
            "-O2",
            "-c",
            lowered,
            "-o",
            root / (platform + ".o"),
        ],
        platform + "-compile",
    )
run(
    [
        "xcrun",
        "clang++",
        "-arch",
        "arm64",
        "-std=c++17",
        "-O2",
        source / "classify-arm.cpp",
        root / "mac.o",
        "-o",
        root / "arm",
    ],
    "arm-build",
)
run([root / "arm", root / "vectors.bin"], "arm-results")
arm = json.loads((root / "arm-results.log").read_text())
assert arm["failed"] == 0 and arm["cases"] == native["input_cases"] * 8
negative = root / "unproved-mask.ll"
negative.write_text(ir.replace("i32 144)", "i32 3)"))
result = subprocess.run(
    [str(lowerer), str(negative), str(root / "rejected.ll")],
    capture_output=True,
    text=True,
)
assert result.returncode and "Unsupported FP80" in result.stderr
assert hashlib.sha256(lowerer.read_bytes()).hexdigest() == lowerer_sha
report = {
    "native": native,
    "lowered_arm": arm,
    "lowerer_sha256": lowerer_sha,
    "lowered_ir_sha256": hashlib.sha256(lowered.read_bytes()).hexdigest(),
    "native_vectors_sha256": hashlib.sha256(
        (root / "vectors.bin").read_bytes()
    ).hexdigest(),
    "unproved_mask_rejected": True,
    "sdk_targets": ["mac", "ios", "simulator"],
}
(root / "results.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, indent=2))
