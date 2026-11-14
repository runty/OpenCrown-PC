#!/usr/bin/env python3
"""Build/run the independent memory-cache regression under native sanitizers."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument("--output", type=Path, required=True)
p.add_argument("--backend-root", type=Path, required=True)
a = p.parse_args()
r = a.output.resolve()
source = Path(__file__).resolve().parents[1]
if (
    len(r.parts) < 3
    or r.parts[1] != "Volumes"
    or not Path("/Volumes", r.parts[2]).is_mount()
):
    p.error("Output must be on a mounted external disk")
r.mkdir(parents=True, exist_ok=True)
manifest = {
    "sources": {
        str(path): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in [
            source / "runtime.cpp",
            source / "runtime.h",
            Path(__file__).with_suffix(".cpp"),
        ]
    },
    "runs": {},
}
for mode, flags in [
    ("native", []),
    ("asan-ubsan", ["-fsanitize=address,undefined"]),
    ("tsan", ["-fsanitize=thread"]),
]:
    binary = r / ("memory-cache-" + mode)
    command = [
        "xcrun",
        "clang++",
        "-std=c++17",
        "-O1",
        "-g",
        "-fno-omit-frame-pointer",
        "-pthread",
        "-Wall",
        "-Wextra",
        "-Werror",
        *flags,
        "-I" + str(source),
        "-I" + str(a.backend_root.resolve() / "remill/include"),
        source / "runtime.cpp",
        Path(__file__).with_suffix(".cpp"),
        "-o",
        binary,
    ]
    with (r / (mode + "-build.log")).open("w") as log:
        subprocess.run(
            [str(x) for x in command], stdout=log, stderr=subprocess.STDOUT, check=True
        )
    env = dict(
        os.environ,
        ASAN_OPTIONS="abort_on_error=1",
        UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1",
        TSAN_OPTIONS="halt_on_error=1",
    )
    result = subprocess.run(
        [str(binary)], capture_output=True, text=True, env=env, timeout=120
    )
    (r / (mode + "-stdout.log")).write_text(result.stdout)
    (r / (mode + "-stderr.log")).write_text(result.stderr)
    manifest["runs"][mode] = {
        "exit_code": result.returncode,
        "stdout": result.stdout,
        "stderr": result.stderr,
    }
    (r / "results.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(mode, result.returncode, result.stdout.strip(), flush=True)
    if result.returncode:
        raise SystemExit(
            "Memory-cache "
            + mode
            + " failed; inspect "
            + str(r / (mode + "-stderr.log"))
        )
