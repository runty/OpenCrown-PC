#!/usr/bin/env python3
"""Discover missing traces, prefetch nearby direct callees, and resume cached builds."""

import argparse
import json
import os
from pathlib import Path
import re
import signal
import struct
import subprocess
import sys
import tempfile
from save_seed import copy_seed, inventory

parser = argparse.ArgumentParser()
parser.add_argument("--root", type=Path, required=True)
parser.add_argument("--dump", type=Path, required=True)
parser.add_argument("--rounds", type=int, default=128)
parser.add_argument("--timeout", type=float, default=120)
parser.add_argument("--jobs", type=int, default=8)
parser.add_argument(
    "--save-seed",
    type=Path,
    help="Read-only quiescent disposable save tree copied into each attempt",
)
parser.add_argument("--translated-libc-bulk", action="store_true")
parser.add_argument(
    "--prefetch",
    type=int,
    default=12,
    help="Additional nearest executable direct-call/relocation targets per build",
)
parser.add_argument(
    "--stop-file",
    type=Path,
    help="Stop between child processes when this file exists (default ROOT/advance.stop)",
)
args = parser.parse_args()
if not 1 <= args.jobs <= 32:
    parser.error("--jobs must be between 1 and 32")
if args.timeout <= 0:
    parser.error("--timeout must be positive")
if args.rounds < 1:
    parser.error("--rounds must be positive")
if args.prefetch < 0:
    parser.error("--prefetch must be nonnegative")
root = args.root.resolve()
stop_file = args.stop_file.resolve() if args.stop_file else root / "advance.stop"
stop_requested = False
save_seed = args.save_seed
if save_seed is not None:
    # Restrict this private build workflow to mounted external storage.
    if (
        len(save_seed.resolve().parts) < 3
        or save_seed.resolve().parts[1] != "Volumes"
        or not Path("/Volumes", save_seed.resolve().parts[2]).is_mount()
    ):
        parser.error("Save seed must be on a mounted external disk")
    inventory(save_seed)
    attempts = root / "attempt-saves"
    if (
        save_seed.resolve() == attempts
        or attempts in save_seed.resolve().parents
        or save_seed.resolve() in attempts.parents
    ):
        parser.error("Save seed must be outside the attempt-save tree")


def request_stop(signum, frame):
    global stop_requested
    stop_requested = True


signal.signal(signal.SIGINT, request_stop)
signal.signal(signal.SIGTERM, request_stop)


def stopping():
    if stop_requested or stop_file.exists():
        print(
            json.dumps(
                {
                    "stopped": True,
                    "stop_file": str(stop_file),
                    "note": "Current child finished; caches retained",
                }
            ),
            flush=True,
        )
        return True
    return False


builder = Path(__file__).with_name("build.py")
base = [
    sys.executable,
    str(builder),
    "--root",
    str(root),
    "--dump",
    str(args.dump),
    "--jobs",
    str(args.jobs),
]
if args.translated_libc_bulk:
    base.append("--translated-libc-bulk")


def candidates(missing):
    metadata = json.loads((root / "prepared-modules.json").read_text())
    ranges = []
    for module in metadata["modules"]:
        for segment in module["segments"]:
            if segment["flags"] & 1:
                start = module["canonical"] + segment["vaddr"]
                ranges.append((start, start + segment["filesz"]))

    def executable(pc):
        return any(start <= pc < end for start, end in ranges)

    if not executable(missing):
        raise SystemExit(
            f"Missing trace {missing:x} is outside executable module bytes"
        )
    compiled = {int(path.stem, 16) for path in (root / "traces").glob("*.ll")}
    if missing in compiled:
        raise SystemExit(f"Runtime requested an already compiled trace {missing:x}")
    stub_file = root / "stubs.cpp"
    referenced = {
        int(value, 16)
        for value in re.findall(r"\bsub_([0-9a-f]+)\(", stub_file.read_text())
    }
    # Vtables and callbacks supply possible entries absent from direct calls.
    # Executable segments also contain constants: restrict speculative targets
    # to the supported modules' 16-byte function alignment. Executed unaligned
    # targets remain valid demands and are never filtered.
    for module in metadata["modules"]:
        fixups = (root / (module["name"] + ".fixups")).read_bytes()
        if len(fixups) % 24:
            raise SystemExit("Truncated relocation records for " + module["name"])
        for offset, value, target in struct.iter_unpack("<QQQ", fixups):
            if offset + 8 > module["image_size"]:
                raise SystemExit("Relocation destination outside module image")
            if target < len(metadata["modules"]):
                pc = metadata["modules"][target]["canonical"] + value
                if pc % 16 == 0 and executable(pc):
                    referenced.add(pc)
    nearby = sorted(
        (pc for pc in referenced - compiled if pc != missing and executable(pc)),
        key=lambda pc: (abs(pc - missing), pc),
    )
    return [missing] + nearby[: args.prefetch]


if stopping():
    sys.exit(0)
seed = []
if not list((root / "traces").glob("*.ll")):
    seed = ["--lift", "300717e0", "--lift", "30000000", "--lift", "2054c690"]
# Children use independent sessions so Ctrl-C cannot interrupt an active compiler.
# SIGINT/SIGTERM request a stop after the current child exits.
subprocess.run(base + seed, check=True, start_new_session=True)
for iteration in range(args.rounds):
    if stopping():
        sys.exit(0)
    runs = root / "attempt-saves"
    runs.mkdir(exist_ok=True)
    save_root = tempfile.mkdtemp(prefix="boot-", dir=runs)
    if save_seed is not None:
        seed_manifest = copy_seed(save_seed, save_root)
        Path(save_root + ".seed.json").write_text(
            json.dumps(seed_manifest, indent=2) + "\n"
        )
    try:
        result = subprocess.run(
            [str(root / "game-boot")],
            env=dict(os.environ, DCP_SAVE_ROOT=save_root, DCP_GAME_MODE="checkpoint"),
            capture_output=True,
            text=True,
            timeout=args.timeout,
            start_new_session=True,
        )
    except subprocess.TimeoutExpired:
        if stopping():
            sys.exit(0)
        raise SystemExit(
            f"Runtime exceeded {args.timeout} seconds; disposable saves retained at {save_root}"
        )
    if stopping():
        sys.exit(0)
    if result.returncode:
        raise SystemExit(
            f"Runtime process failed ({result.returncode}): {result.stderr}"
        )
    report = json.loads(result.stdout)
    if save_seed is not None:
        report["save_seed_manifest"] = save_root + ".seed.json"
    temporary = root / "boot-result.json.partial"
    temporary.write_text(json.dumps(report, indent=2) + "\n")
    temporary.replace(root / "boot-result.json")
    print(
        json.dumps(
            {"iteration": iteration, "stage": report["stage"], "error": report["error"]}
        ),
        flush=True,
    )
    match = re.search(r"missing trace: ([a-f0-9]+)", report["error"])
    if not match:
        sys.exit(0 if not report["error"] else 1)
    batch = candidates(int(match[1], 16))
    command = list(base)
    for pc in batch:
        command.extend(["--lift", f"{pc:x}"])
    print(json.dumps({"prefetch_batch": [f"{pc:x}" for pc in batch]}), flush=True)
    if stopping():
        sys.exit(0)
    with (root / "advance-build.log").open("w") as log:
        subprocess.run(
            command,
            stdout=log,
            stderr=subprocess.STDOUT,
            check=True,
            start_new_session=True,
        )
raise SystemExit("Trace discovery reached its round limit; rerun to resume cached work")
