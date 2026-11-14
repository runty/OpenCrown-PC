#!/usr/bin/env python3
"""Discover executed missing traces on arm64 Mac and rebuild the signed-code inputs."""

import argparse
import os
import tempfile
import json
from pathlib import Path
import re
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument("--root", type=Path, required=True)
parser.add_argument("--dump", type=Path, required=True)
parser.add_argument("--rounds", type=int, default=128)
args = parser.parse_args()
if args.rounds < 1:
    parser.error("--rounds must be positive")
root = args.root.resolve()
builder = Path(__file__).with_name("build.py")
base = [sys.executable, str(builder), "--root", str(root), "--dump", str(args.dump)]
# The builder validates mounts and exact module hashes before writing output.
seed = []
if not list((root / "traces").glob("*.ll")):
    seed = ["--lift", "300717e0", "--lift", "30000000", "--lift", "2054c690"]
subprocess.run(base + seed, check=True)
for iteration in range(args.rounds):
    runs = root / "attempt-saves"
    runs.mkdir(exist_ok=True)
    save_root = tempfile.mkdtemp(prefix="boot-", dir=runs)
    result = subprocess.run(
        [str(root / "game-boot")],
        env=dict(os.environ, DCP_SAVE_ROOT=save_root),
        capture_output=True,
        text=True,
        timeout=30,
    )
    if result.returncode:
        raise SystemExit(
            f"Runtime process failed ({result.returncode}): {result.stderr}"
        )
    report = json.loads(result.stdout)
    (root / "boot-result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(
        json.dumps(
            {"iteration": iteration, "stage": report["stage"], "error": report["error"]}
        ),
        flush=True,
    )
    match = re.search(r"missing trace: ([a-f0-9]+)", report["error"])
    if not match:
        sys.exit(0 if not report["error"] else 1)
    pc = match[1]
    if (root / "traces" / (pc + ".ll")).exists():
        raise SystemExit("Runtime requested an already compiled trace " + pc)
    with (root / "advance-build.log").open("w") as log:
        subprocess.run(
            base + ["--lift", pc], stdout=log, stderr=subprocess.STDOUT, check=True
        )
raise SystemExit("Trace discovery reached its round limit; rerun to resume cached work")
