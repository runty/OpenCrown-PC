#!/usr/bin/env python3
"""Verify save seeding without reading or writing any live save directory."""

import argparse
import hashlib
import json
from pathlib import Path
import os
import tempfile
from save_seed import copy_seed

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
output = args.output.resolve()
if (
    len(output.parts) < 3
    or output.parts[1] != "Volumes"
    or not Path("/Volumes", output.parts[2]).is_mount()
):
    parser.error("Output must be on a mounted external disk")
output.mkdir(parents=True, exist_ok=True)
root = Path(tempfile.mkdtemp(prefix="fixture-", dir=output))
checks = 0


def require(okay):
    global checks
    checks += 1
    if not okay:
        raise AssertionError(f"Save seed check {checks} failed")


def rejected(source, destination, **limits):
    try:
        copy_seed(source, destination, **limits)
    except ValueError:
        require(True)
    else:
        require(False)


source = root / "source"
(source / "SAVEDATA00/sce_sys").mkdir(parents=True)
(source / "empty").mkdir()
payloads = {
    "SAVEDATA00/SAVE0.DAT": bytes(range(256)) * 257,
    "SAVEDATA00/sce_sys/param.bin": b"fixture\x00metadata\xff",
    "SAVEDATA00/sce_sys/empty.bin": b"",
}
for name, value in payloads.items():
    (source / name).write_bytes(value)
destination = root / "attempt"
destination.mkdir()
manifest = copy_seed(source, destination)
require(set(manifest) == set(payloads))
for name, value in payloads.items():
    require((destination / name).read_bytes() == value)
    require((source / name).read_bytes() == value)
    require(
        manifest[name]
        == {"size": len(value), "sha256": hashlib.sha256(value).hexdigest()}
    )
require((destination / "empty").is_dir())
for label, limits in [
    ("byte-limit", {"max_bytes": 16}),
    ("entry-limit", {"max_entries": 1}),
]:
    dest = root / label
    dest.mkdir()
    rejected(source, dest, **limits)
    require(not list(dest.iterdir()))
nonempty = root / "nonempty"
nonempty.mkdir()
(nonempty / "keep").write_bytes(b"canary")
rejected(source, nonempty)
require((nonempty / "keep").read_bytes() == b"canary")
overlap = source / "attempt"
overlap.mkdir()
rejected(source, overlap)
require(not list(overlap.iterdir()))
for label, special in [("root-link", "root"), ("entry-link", "file"), ("fifo", "fifo")]:
    seed = root / label
    if special == "root":
        seed.symlink_to(source, target_is_directory=True)
    else:
        seed.mkdir()
        if special == "file":
            (seed / "link").symlink_to(source / "SAVEDATA00/SAVE0.DAT")
        else:
            os.mkfifo(seed / "pipe")
    dest = root / (label + "-attempt")
    dest.mkdir()
    rejected(seed, dest)
    require(not list(dest.iterdir()))
real_destination = root / "real-destination"
real_destination.mkdir()
link_destination = root / "destination-link"
link_destination.symlink_to(real_destination, target_is_directory=True)
rejected(source, link_destination)
require(not list(real_destination.iterdir()))
empty_source = root / "empty-source"
empty_source.mkdir()
empty_destination = root / "empty-destination"
empty_destination.mkdir()
require(copy_seed(empty_source, empty_destination) == {})
result = {
    "checks": checks,
    "failed": 0,
    "fixture": str(root),
    "live_saves_accessed": False,
}
(output / "results.json").write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps(result))
