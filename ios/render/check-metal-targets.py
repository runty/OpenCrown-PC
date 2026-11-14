#!/usr/bin/env python3
"""Verify actual AIR platform metadata and embedded metallibs after a CMake build.

Example: python3 ios/render/check-metal-targets.py EXTERNAL_RENDER_BUILD
 --sdk iphonesimulator --minimum-os 18.0 --llvm-dis /path/to/llvm-dis
No shaders are rebuilt or modified by this check.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("build", type=Path)
parser.add_argument(
    "--sdk", choices=["macosx", "iphoneos", "iphonesimulator"], required=True
)
parser.add_argument("--minimum-os", required=True)
parser.add_argument("--llvm-dis", default=shutil.which("llvm-dis"))
args = parser.parse_args()
if not args.llvm_dis:
    parser.error("Pass --llvm-dis=<the pinned LLVM disassembler>")
if not re.fullmatch(r"\d+(?:\.\d+){0,2}", args.minimum_os):
    parser.error("--minimum-os must be numeric")
source = Path(__file__).resolve().parents[2] / "linux-port/src/render/shaders/generated"
headers = sorted(source.glob("*.spv.h"))
if len(headers) != 8:
    parser.error("Expected eight source shader headers")
shader_root = args.build / "shaders"
embedded = (shader_root / "metal_shaders.h").read_text()
archive = (args.build / "libopencrown-mobile-metal.a").read_bytes()
arrays = {}
for symbol, body in re.findall(
    r"static const unsigned char (\w+)\[\] = \{(.*?)\};", embedded, re.S
):
    arrays[symbol] = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", body))
if len(arrays) != 8:
    raise SystemExit("FAIL: generated header does not contain eight shader arrays")
expected_os = "macosx" if args.sdk == "macosx" else "ios"
expected_environment = "-simulator" if args.sdk == "iphonesimulator" else ""
expected_version = tuple(int(p) for p in args.minimum_os.split("."))
expected_version += (0,) * (3 - len(expected_version))
results = []
for header in headers:
    symbol = re.search(r"unsigned char (\w+)\[", header.read_text()).group(1)
    stem = header.name.removesuffix(".spv.h")
    air = shader_root / (stem + ".air")
    library = shader_root / (stem + ".metallib")
    ir = subprocess.check_output([args.llvm_dis, str(air), "-o", "-"], text=True)
    triple = re.search(r'target triple = "([^"]+)"', ir).group(1)
    target = re.fullmatch(
        r"air64(?:_v\d+)?-apple-(macosx|ios)(\d+)\.(\d+)\.(\d+)(-simulator)?", triple
    )
    if (
        not target
        or target.group(1) != expected_os
        or tuple(int(target.group(i)) for i in (2, 3, 4)) != expected_version
        or (target.group(5) or "") != expected_environment
    ):
        raise SystemExit(f"FAIL: {stem}: unexpected AIR target {triple} for {args.sdk}")
    data = library.read_bytes()
    if triple.encode() not in data:
        raise SystemExit(f"FAIL: {stem}: metallib lost expected AIR target metadata")
    if arrays.get(symbol) != data:
        raise SystemExit(
            f"FAIL: {stem}: generated array differs from compiled metallib"
        )
    if data not in archive:
        raise SystemExit(
            f"FAIL: {stem}: final static archive does not embed the verified library"
        )
    results.append(
        {
            "shader": stem,
            "air_target": triple,
            "metallib_sha256": hashlib.sha256(data).hexdigest(),
        }
    )
print(
    json.dumps(
        {
            "passed": True,
            "sdk": args.sdk,
            "minimum_os": args.minimum_os,
            "verified_libraries": len(results),
            "shaders": results,
        },
        indent=2,
    )
)
