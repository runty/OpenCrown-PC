#!/usr/bin/env python3
"""Compile the checked-in SPIR-V into embedded macOS Metal libraries."""
import argparse
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--source", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--spirv-cross", required=True)
args = parser.parse_args()
args.output.parent.mkdir(parents=True, exist_ok=True)
headers = sorted(args.source.glob("*.spv.h"))
if len(headers) != 8:
    raise SystemExit("Expected all eight pinned SPIR-V shader headers")
output = ["// Generated Metal libraries. Rebuild through CMake.", "#pragma once"]
for header in headers:
    text = header.read_text()
    symbol = re.search(r"unsigned char (\w+)\[", text).group(1)
    # Array names stay common to both host builds; the selected format differs.
    binary = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", text))
    stem = args.output.parent / header.name.removesuffix(".spv.h")
    spirv = stem.with_suffix(".spv")
    metal = stem.with_suffix(".metal")
    air = stem.with_suffix(".air")
    library = stem.with_suffix(".metallib")
    spirv.write_bytes(binary)
    subprocess.run([args.spirv_cross, str(spirv), "--msl", "--output", str(metal)], check=True)
    subprocess.run(["xcrun", "--sdk", "macosx", "metal", "-c", str(metal), "-o", str(air)], check=True)
    subprocess.run(["xcrun", "--sdk", "macosx", "metallib", str(air), "-o", str(library)], check=True)
    data = library.read_bytes()
    output.append(f"static const unsigned char {symbol}[] = {{")
    for offset in range(0, len(data), 16):
        output.append("    " + ", ".join(f"0x{byte:02x}" for byte in data[offset:offset + 16]) + ",")
    output.append("};")
temporary = args.output.with_suffix(".tmp")
temporary.write_text("\n".join(output) + "\n")
temporary.replace(args.output)
