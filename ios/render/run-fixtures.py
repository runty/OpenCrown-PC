#!/usr/bin/env python3
"""Run host Metal/AOT capture regressions and verify every saved RGB pixel."""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import zlib

parser = argparse.ArgumentParser()
parser.add_argument("--build", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
output = args.output.resolve()
if (
    len(output.parts) < 3
    or output.parts[1] != "Volumes"
    or not Path("/Volumes", output.parts[2]).is_mount()
):
    parser.error("Fixture output must be on a mounted external disk")
output.mkdir(parents=True, exist_ok=True)
for executable, parameters in [
    ("metal-fixture", [output / "fixture"]),
    ("capture-fixture", []),
    ("texture-fixture", []),
    ("bc-fixture", []),
    ("replay-fixture", [output / "replay.png"]),
]:
    with (output / (executable + ".log")).open("w") as log:
        subprocess.run(
            [str(args.build / executable), *map(str, parameters)],
            stdout=log,
            stderr=subprocess.STDOUT,
            check=True,
        )


def rgb(path):
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("Invalid PNG signature")
    position, compressed = 8, bytearray()
    width = height = 0
    while position < len(data):
        length = struct.unpack_from(">I", data, position)[0]
        kind = data[position + 4 : position + 8]
        payload = data[position + 8 : position + 8 + length]
        checksum = struct.unpack_from(">I", data, position + 8 + length)[0]
        if zlib.crc32(kind + payload) & 0xFFFFFFFF != checksum:
            raise ValueError("PNG chunk checksum mismatch")
        if kind == b"IHDR":
            width, height, depth, color, compression, filtering, interlace = (
                struct.unpack(">IIBBBBB", payload)
            )
            if (width, height, depth, color, compression, filtering, interlace) != (
                64,
                64,
                8,
                2,
                0,
                0,
                0,
            ):
                raise ValueError("Unexpected fixture image format")
        elif kind == b"IDAT":
            compressed.extend(payload)
        position += length + 12
    raw = zlib.decompress(compressed)
    if len(raw) != height * (width * 3 + 1):
        raise ValueError("Unexpected fixture image length")
    rows = []
    for y in range(height):
        row = raw[y * (width * 3 + 1) : (y + 1) * (width * 3 + 1)]
        if row[0] != 0:
            raise ValueError("Fixture writer must use unfiltered PNG rows")
        rows.append(row[1:])
    return rows


report = json.loads((output / "fixture.json").read_text())
images = {}
checks = 0
for filename, expected in [
    ("fixture-blend.png", lambda y: (127, 0, 128) if y < 32 else (255, 0, 0)),
    ("fixture-mask.png", lambda y: (64, 64, 192)),
    ("replay.png", lambda y: (64, 128, 191)),
]:
    path = output / filename
    for y, row in enumerate(rgb(path)):
        for x in range(64):
            for channel, value in enumerate(expected(y)):
                checks += 1
                if abs(row[x * 3 + channel] - value) > 1:
                    raise ValueError(
                        f"{filename}: pixel {x},{y} channel {channel} differs"
                    )
    images[filename] = hashlib.sha256(path.read_bytes()).hexdigest()
report.update(
    saved_rgb_channel_checks=checks,
    capture_to_backend_to_metal=True,
    full_texture_hash_regression=True,
    bc_native_and_fallback_checks=1504,
    png_sha256=images,
)
(output / "verification.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, indent=2))
