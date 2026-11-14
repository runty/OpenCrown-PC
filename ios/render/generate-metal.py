#!/usr/bin/env python3
"""Generate an SDK-specific embedded metallib header from the eight source shaders."""

import argparse
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--source", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--spirv-cross", required=True)
parser.add_argument(
    "--sdk", choices=["macosx", "iphoneos", "iphonesimulator"], required=True
)
parser.add_argument("--minimum-os", required=True)
args = parser.parse_args()
if not re.fullmatch(r"\d+(?:\.\d+){0,2}", args.minimum_os):
    parser.error("--minimum-os must be a numeric deployment version")
# SDK selection controls headers; the explicit AIR target controls the Metal
# library platform. A simulator is a distinct target, not an iPhoneOS library.
target = {
    "macosx": f"air64-apple-macosx{args.minimum_os}",
    "iphoneos": f"air64-apple-ios{args.minimum_os}",
    "iphonesimulator": f"air64-apple-ios{args.minimum_os}-simulator",
}[args.sdk]
sysroot = subprocess.check_output(
    ["xcrun", "--sdk", args.sdk, "--show-sdk-path"], text=True
).strip()
headers = sorted(args.source.glob("*.spv.h"))
if len(headers) != 8:
    parser.error("Expected exactly eight source shader headers")
args.output.parent.mkdir(parents=True, exist_ok=True)
result = [f"// Generated for {args.sdk} ({target})", "#pragma once"]
for header in headers:
    text = header.read_text()
    symbol = re.search(r"unsigned char (\w+)\[", text).group(1)
    spirv = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", text))
    stem = args.output.parent / header.name.removesuffix(".spv.h")
    source, metal, air, library = [
        stem.with_suffix(suffix) for suffix in (".spv", ".metal", ".air", ".metallib")
    ]
    source.write_bytes(spirv)
    cross = [args.spirv_cross, str(source), "--msl", "--output", str(metal)]
    if args.sdk != "macosx":
        cross.append("--msl-ios")
    subprocess.run(cross, check=True)
    subprocess.run(
        [
            "xcrun",
            "--sdk",
            args.sdk,
            "metal",
            "-target",
            target,
            "-isysroot",
            sysroot,
            "-c",
            str(metal),
            "-o",
            str(air),
        ],
        check=True,
    )
    subprocess.run(
        ["xcrun", "--sdk", args.sdk, "metallib", str(air), "-o", str(library)],
        check=True,
    )
    result.append("static const unsigned char " + symbol + "[] = {")
    data = library.read_bytes()
    result.extend(
        "    "
        + ", ".join(f"0x{value:02x}" for value in data[offset : offset + 16])
        + ","
        for offset in range(0, len(data), 16)
    )
    result.append("};")
temporary = args.output.with_suffix(".tmp")
temporary.write_text("\n".join(result) + "\n")
temporary.replace(args.output)
