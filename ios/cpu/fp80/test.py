#!/usr/bin/env python3
"""Generate synthetic x87 full-storage oracle vectors and compare arm64 SoftFloat."""

import argparse
from pathlib import Path
import random
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--build", type=Path, required=True)
args = parser.parse_args()
root = args.build.resolve()
source = Path(__file__).resolve().parent
if (
    len(root.parts) < 3
    or root.parts[1] != "Volumes"
    or not Path("/Volumes", root.parts[2]).is_mount()
):
    parser.error("Build output must be on a mounted external disk")


def run(command, log):
    with (root / log).open("w") as stream:
        subprocess.run(
            [str(x) for x in command],
            stdout=stream,
            stderr=subprocess.STDOUT,
            check=True,
        )


run(
    [
        "xcrun",
        "clang++",
        "-arch",
        "x86_64",
        "-std=c++17",
        "-O2",
        "-DNATIVE_ORACLE",
        source / "oracle.cpp",
        "-o",
        root / "oracle",
    ],
    "oracle-build.log",
)
run(
    [
        "xcrun",
        "clang++",
        "-arch",
        "arm64",
        "-std=c++17",
        "-O2",
        source / "oracle.cpp",
        root / "mac/libdcp-fp80.a",
        "-o",
        root / "checker",
    ],
    "checker-build.log",
)


def fp(sign, exponent, significand):
    return significand.to_bytes(8, "little") + ((sign << 15) | exponent).to_bytes(
        2, "little"
    )


zero = fp(0, 0, 0)
special = [
    zero,
    fp(1, 0, 0),
    fp(0, 16383, 1 << 63),
    fp(1, 16383, 1 << 63),
    fp(0, 0, 1),
    fp(0, 0, (1 << 63) - 1),
    fp(0, 1, 1 << 63),
    fp(0, 32766, (1 << 64) - 1),
    fp(0, 32767, 1 << 63),
    fp(1, 32767, 1 << 63),
    fp(0, 32767, (3 << 62) | 123),
    fp(1, 32767, (3 << 62) | 123),
    fp(0, 32767, (1 << 63) | 123),
]
rng = random.Random(8180)
values = special + [
    fp(
        rng.randrange(2),
        rng.choice([0, 1, 16320, 16382, 16383, 16384, 16400, 32765, 32766]),
        rng.getrandbits(63) | (1 << 63),
    )
    for _ in range(80)
]
pairs = [(a, b) for a in special for b in special] + [
    (rng.choice(values), rng.choice(values)) for _ in range(256)
]
# FSCALE's exponent range must not be materialized as an intermediate 2^N.
for n in [16383, 16384, 32767, 32768, -16383, -16384, -32767, -32768, 65536, -65536]:
    exp = abs(n).bit_length() - 1
    value = fp(n < 0, 16383 + exp, abs(n) << (63 - exp))
    pairs.extend([(a, value) for a in special])
rows = []
for precision in [0, 2, 3]:
    for rounding in range(4):
        cw = 0x7F | (precision << 8) | (rounding << 10)
        for op in range(5):
            for a, b in pairs:
                rows.append((op, cw, a, b))
        for op in range(20, 36):
            for a, b in pairs:
                rows.append((op, cw, a, b))
        for op in [5, 10, 11, 12, 14, 15, 16, 17, 18, 19]:
            for a in values:
                rows.append((op, cw, a, zero))
        for op in [6, 7, 8, 9]:
            for i in [
                0,
                1,
                2**32 - 1,
                2**63,
                2**63 - 1,
                2**64 - 1,
                *[rng.getrandbits(64) for _ in range(40)],
            ]:
                rows.append((op, cw, i.to_bytes(8, "little") + b"\0\0", zero))
input = root / "oracle-input.txt"
input.write_text("".join(f"{op} {cw} {a.hex()} {b.hex()}\n" for op, cw, a, b in rows))
run([root / "oracle", input], "oracle-results.txt")
run([root / "checker", root / "oracle-results.txt"], "checker-results.txt")
print((root / "checker-results.txt").read_text())
