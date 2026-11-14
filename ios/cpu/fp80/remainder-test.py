#!/usr/bin/env python3
"""Verify production and lowered FP80 remainders against exact finite arithmetic."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import struct
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
a = parser.parse_args()
p = a.output.resolve()
build = a.build.resolve()
source = Path(__file__).resolve().parent
if (
    len(p.parts) < 3
    or p.parts[1] != "Volumes"
    or not Path("/Volumes", p.parts[2]).is_mount()
):
    parser.error("Output must be on a mounted external disk")
p.mkdir(parents=True, exist_ok=True)
manifest = json.loads((build / "manifest.json").read_text())
sf = Path(manifest["source"]) / "source"
inc = [
    "-I" + str(source),
    "-I" + str(sf / "include"),
    "-I" + str(sf / "8086-SSE"),
    "-DSOFTFLOAT_FAST_INT64",
    "-DTHREAD_LOCAL=_Thread_local",
]
llvm = Path("/opt/homebrew/opt/llvm/bin")
lowerer = build / "fp80-lower"
lowerer_sha = hashlib.sha256(lowerer.read_bytes()).hexdigest()


def run(cmd, name):
    with (p / name).open("w") as f:
        subprocess.run(
            list(map(str, cmd)),
            stdout=f,
            stderr=subprocess.STDOUT,
            check=True,
            timeout=180,
            env=dict(os.environ, ROSETTA_ADVERTISE_AVX="1"),
        )


run(
    [
        "xcrun",
        "clang++",
        "-arch",
        "x86_64",
        "-std=c++17",
        "-O2",
        "-DNATIVE",
        source / "remainder-oracle.cpp",
        "-o",
        p / "native",
    ],
    "build-native.log",
)


def fp(e, s, neg=0):
    return struct.pack("<QH", s, e | (neg << 15))


J = 1 << 63
special = [
    fp(e, s, n)
    for e, s in [
        (0, 0),
        (0, 1),
        (0, J - 1),
        (0, J),
        (0, J + 1),
        (0, 2 * J - 1),
        (1, J),
        (1, J + 1),
        (16382, J),
        (16383, J),
        (16383, J + 1),
        (16384, J),
        (32766, 2 * J - 1),
        (32767, J),
        (32767, J + 1),
        (32767, J + (1 << 62) - 1),
        (32767, J + (1 << 62)),
        (32767, J + (1 << 62) + 1),
        (32767, 2 * J - 1),
        (1, 0),
        (1, 1),
        (16383, J - 1),
        (32767, 0),
        (32767, 1),
        (32767, (1 << 62)),
    ]
    for n in range(2)
]
pairs = [(a, b) for a in special for b in special]
# Many quotient ties, huge exponent distances, subnormal boundaries, and random full mantissas.
rng = random.Random(148231)
for _ in range(8000):
    ea = rng.choice([rng.randrange(32767), 0, 1, 2, 16382, 16383, 16384, 32765, 32766])
    eb = rng.choice([rng.randrange(32767), 0, 1, 2, 16382, 16383, 16384, 32765, 32766])
    pairs.append(
        (
            fp(ea, rng.getrandbits(63) | (J if ea else 0), rng.randrange(2)),
            fp(eb, rng.getrandbits(63) | (J if eb else 0), rng.randrange(2)),
        )
    )
for e in [0, 1, 2, 16300, 16382, 16383, 16384, 16400, 32764, 32765, 32766]:
    for sa in [1, J - 1, J, J + 1, J + (J >> 1), 2 * J - 2, 2 * J - 1]:
        for sb in [1, J - 1, J, J + 1, 2 * J - 1]:
            for delta in [-64, -63, -2, -1, 0, 1, 2, 63, 64, 65, 1000, 16000, 32000]:
                eb = e + delta
                if not 0 <= eb <= 32766:
                    continue
                if (e and sa < J) or (eb and sb < J):
                    continue
                for n in range(4):
                    pairs.append((fp(e, sa, n & 1), fp(eb, sb, n >> 1)))
with (p / "vectors.bin").open("wb") as f:
    for pc in [0, 1, 2, 3]:
        for rc in range(4):
            cw = 0x7F | (pc << 8) | (rc << 10)
            for op in range(2):
                for a, b in pairs:
                    f.write(struct.pack("<HBB", cw, op, 0) + a + b)
run([p / "native", p / "vectors.bin", p / "native.bin"], "native.log")
ir = "declare x86_fp80 @remainderl(x86_fp80,x86_fp80)\ndeclare x86_fp80 @fmodl(x86_fp80,x86_fp80)\n"
for name, expression in [
    ("remainder", "call x86_fp80 @remainderl(x86_fp80 %a,x86_fp80 %b)"),
    ("fmod", "call x86_fp80 @fmodl(x86_fp80 %a,x86_fp80 %b)"),
    ("frem", "frem x86_fp80 %a,%b"),
]:
    ir += f"define void @lowered_{name}(ptr %out,ptr %ap,ptr %bp){{%a=load x86_fp80,ptr %ap,align 1\n%b=load x86_fp80,ptr %bp,align 1\n%v={expression}\nstore x86_fp80 %v,ptr %out,align 1\nret void}}\n"
(p / "original.ll").write_text(ir)
run([lowerer, p / "original.ll", p / "lowered.ll"], "lower.log")
assert "x86_fp80" not in (p / "lowered.ll").read_text()
for platform, sdk, target in [
    ("mac", "macosx", "arm64-apple-macos15"),
    ("ios", "iphoneos", "arm64-apple-ios18"),
    ("simulator", "iphonesimulator", "arm64-apple-ios18-simulator"),
]:
    sdkpath = subprocess.check_output(
        ["xcrun", "--sdk", sdk, "--show-sdk-path"], text=True
    ).strip()
    run(
        [
            llvm / "clang",
            "-target",
            target,
            "-isysroot",
            sdkpath,
            "-O2",
            "-c",
            p / "lowered.ll",
            "-o",
            p / (platform + "-lowered.o"),
        ],
        platform + "-ir.log",
    )
    run(
        [
            "xcrun",
            "--sdk",
            sdk,
            "clang",
            "-target",
            target,
            "-isysroot",
            sdkpath,
            "-O2",
            *inc,
            "-DLOWERED_REMAINDERS",
            "-c",
            source / "remainder-fixture.c",
            "-o",
            p / (platform + "-fixture.o"),
        ],
        platform + "-fixture.log",
    )
results = {}
for mode in ["direct", "lowered"]:
    flags = ["-DLOWERED_REMAINDERS"] if mode == "lowered" else []
    run(
        [
            "xcrun",
            "clang",
            "-arch",
            "arm64",
            "-O2",
            *inc,
            *flags,
            "-c",
            source / "remainder-fixture.c",
            "-o",
            p / "fixture.o",
        ],
        "build-" + mode + ".log",
    )
    objects = [
        p / "fixture.o",
        *([p / "mac-lowered.o"] if mode == "lowered" else []),
        build / "mac/libdcp-fp80.a",
    ]
    run(
        [
            "xcrun",
            "clang++",
            "-arch",
            "arm64",
            "-std=c++17",
            "-O2",
            source / "remainder-oracle.cpp",
            *objects,
            "-o",
            p / "checker",
        ],
        "checker-" + mode + ".log",
    )
    with (
        (p / (mode + "-native-results.json")).open("w") as out,
        (p / (mode + "-native-mismatches.txt")).open("w") as err,
    ):
        compared = subprocess.run(
            [
                str(p / "checker"),
                str(p / "vectors.bin"),
                str(p / "native.bin"),
                str(p / "candidate.bin"),
            ],
            stdout=out,
            stderr=err,
            timeout=120,
        )
    run(["python3", source / "remainder-exact.py", p], mode + "-exact.log")
    native = json.loads((p / (mode + "-native-results.json")).read_text())
    exact = json.loads((p / "exact-results.json").read_text())
    assert (
        native["flag_failures_including_DE"] == 0
        and native["native_control_failures"] == 0
    )
    assert (
        native["bit_failures"] == sum(exact["native_failures"])
        and exact["candidate_failures"] == 0
        and exact["native_failures"][1] == 0
    )
    assert compared.returncode == int(native["bit_failures"] != 0)
    (p / (mode + "-exact-results.json")).write_text(json.dumps(exact, indent=2) + "\n")
    run(
        [
            "xcrun",
            "clang++",
            "-arch",
            "arm64",
            "-std=c++17",
            "-O2",
            source / "remainder-thread.cpp",
            *objects,
            "-o",
            p / "threads",
        ],
        "thread-build-" + mode + ".log",
    )
    run(
        [p / "threads", p / "vectors.bin", p / "candidate.bin"],
        mode + "-thread-results.json",
    )
    threads = json.loads((p / (mode + "-thread-results.json")).read_text())
    assert threads["failures"] == 0
    results[mode] = {
        "native_comparison": native,
        "exact_finite": exact,
        "thread_checks": threads,
    }
assert hashlib.sha256(lowerer.read_bytes()).hexdigest() == lowerer_sha
report = {
    "lowerer_sha256": lowerer_sha,
    "vectors_sha256": hashlib.sha256((p / "vectors.bin").read_bytes()).hexdigest(),
    "libraries": manifest["libraries"],
    "results": results,
    "limitation": "Rosetta FPREM1 discrepancies are retained; exact integer arithmetic defines finite expectations. Quotient flags, partial-step state and unmasked traps are outside this proof.",
}
(p / "results.json").write_text(json.dumps(report, indent=2) + "\n")
print(
    json.dumps(
        {
            mode: {
                "rows": r["native_comparison"]["checks"],
                "finite_exact_failures": r["exact_finite"]["candidate_failures"],
                "native_fprem1_discrepancies": r["native_comparison"]["bit_failures"],
                "thread_failures": r["thread_checks"]["failures"],
            }
            for mode, r in results.items()
        },
        indent=2,
    )
)
