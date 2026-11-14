#!/usr/bin/env python3
"""Build pinned SoftFloat helpers and a host LLVM FP80 lowering tool externally."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument(
    "--source", type=Path, help="Already verified official SoftFloat-3e source"
)
parser.add_argument("--llvm", type=Path, default=Path("/opt/homebrew/opt/llvm"))
parser.add_argument("--jobs", type=int, default=8)
args = parser.parse_args()
root = args.output.resolve()
source = Path(__file__).resolve().parent
if (
    len(root.parts) < 3
    or root.parts[1] != "Volumes"
    or not Path("/Volumes", root.parts[2]).is_mount()
):
    parser.error("Output must be on a mounted external disk")
if args.jobs < 1:
    parser.error("jobs must be positive")
root.mkdir(parents=True, exist_ok=True)
archive_sha = "21130ce885d35c1fe73fc1e1bf2244178167e05c6747cad5f450cc991714c746"
url = "https://www.jhauser.us/arithmetic/SoftFloat-3e.zip"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command, log):
    with (root / log).open("w") as stream:
        subprocess.run(
            [str(x) for x in command],
            stdout=stream,
            stderr=subprocess.STDOUT,
            check=True,
        )


if args.source:
    soft = args.source.resolve()
else:
    archive = root / "SoftFloat-3e.zip"
    if not archive.exists():
        temporary = root / "SoftFloat-3e.zip.partial"
        run(["curl", "--fail", "--location", url, "-o", temporary], "download.log")
        if sha(temporary) != archive_sha:
            raise SystemExit("SoftFloat archive checksum mismatch")
        temporary.replace(archive)
    if sha(archive) != archive_sha:
        raise SystemExit("SoftFloat archive checksum mismatch")
    soft = root / "SoftFloat-3e"
    if not soft.exists():
        with zipfile.ZipFile(archive) as zipped:
            for entry in zipped.infolist():
                if (
                    Path(entry.filename).is_absolute()
                    or ".." in Path(entry.filename).parts
                ):
                    raise SystemExit("Unsafe ZIP member")
            zipped.extractall(root)
if not (soft / "source/extF80_mul.c").exists():
    raise SystemExit("SoftFloat source is missing")
llvm = args.llvm.resolve()
if (
    subprocess.check_output([llvm / "bin/llvm-config", "--version"], text=True).strip()
    != "23.1.5"
):
    raise SystemExit("Expected LLVM23.1.5")
flags = shlex.split(
    subprocess.check_output(
        [
            llvm / "bin/llvm-config",
            "--cxxflags",
            "--ldflags",
            "--system-libs",
            "--libs",
            "core",
            "irreader",
            "support",
            "transformutils",
        ],
        text=True,
    )
)
run(
    [
        llvm / "bin/clang++",
        *flags,
        "-std=c++17",
        "-O2",
        source / "lower.cpp",
        "-o",
        root / "fp80-lower",
    ],
    "lower-build.log",
)
makefile = (soft / "build/Linux-x86_64-GCC/Makefile").read_text()
names = sorted(set(re.findall(r"\b(\w+)\$\(OBJ\)", makefile)))
inputs = []
for name in names:
    path = soft / "source/8086-SSE" / (name + ".c")
    if not path.exists():
        path = soft / "source" / (name + ".c")
    if not path.exists():
        raise SystemExit("Missing upstream object source: " + name)
    inputs.append(path)
inputs.append(source / "fp80.c")
inputs.append(source / "exceptions.c")
common = [
    "-std=c11",
    "-O2",
    "-DSOFTFLOAT_FAST_INT64",
    "-DSOFTFLOAT_ROUND_ODD",
    "-DINLINE_LEVEL=5",
    "-DSOFTFLOAT_FAST_DIV32TO16",
    "-DSOFTFLOAT_FAST_DIV64TO32",
    "-DTHREAD_LOCAL=_Thread_local",
    "-I" + str(source),
    "-I" + str(soft / "source/8086-SSE"),
    "-I" + str(soft / "source/include"),
]
header_hash = hashlib.sha256()
for p in (
    sorted(source.glob("*.h"))
    + sorted((soft / "source/include").glob("*.h"))
    + sorted((soft / "source/8086-SSE").glob("*.h"))
):
    header_hash.update(p.read_bytes())
platforms = [
    ("mac", "arm64-apple-macos15", "macosx"),
    ("ios", "arm64-apple-ios18", "iphoneos"),
    ("simulator", "arm64-apple-ios18-simulator", "iphonesimulator"),
]
for platform, target, sdk in platforms:
    directory = root / platform
    directory.mkdir(exist_ok=True)
    sysroot = subprocess.check_output(
        ["xcrun", "--sdk", sdk, "--show-sdk-path"], text=True
    ).strip()

    def compile(path):
        obj = directory / (path.stem + ".o")
        command = [
            "xcrun",
            "--sdk",
            sdk,
            "clang",
            "-target",
            target,
            "-isysroot",
            sysroot,
            *common,
            "-c",
            path,
            "-o",
            obj,
        ]
        signature = hashlib.sha256(
            header_hash.digest() + path.read_bytes() + repr(command).encode()
        ).hexdigest()
        stamp = obj.with_suffix(".sha256")
        if not obj.exists() or not stamp.exists() or stamp.read_text() != signature:
            run(command, platform + "-" + path.stem + ".log")
            stamp.write_text(signature)
        return obj

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        objects = list(pool.map(compile, inputs))
    run(
        ["xcrun", "libtool", "-static", "-o", directory / "libdcp-fp80.a", *objects],
        platform + "-archive.log",
    )
    print(platform, "built", len(objects), "objects", flush=True)
notices = root / "ThirdPartyNotices"
notices.mkdir(exist_ok=True)
shutil.copy2(soft / "COPYING.txt", notices / "SoftFloat-3e-LICENSE.txt")
(root / "manifest.json").write_text(
    json.dumps(
        {
            "url": url,
            "archive_sha256": archive_sha,
            "caller_supplied_source": bool(args.source),
            "source": str(soft),
            "llvm_version": "23.1.5",
            "specialization": "8086-SSE",
            "thread_local": True,
            "lower_sha256": sha(root / "fp80-lower"),
            "helper_sha256": sha(source / "fp80.c"),
            "lower_source_sha256": sha(source / "lower.cpp"),
            "build_script_sha256": sha(Path(__file__)),
            "headers": {
                str(p.relative_to(soft)) if p.is_relative_to(soft) else p.name: sha(p)
                for p in sorted(source.glob("*.h"))
                + sorted((soft / "source/include").glob("*.h"))
                + sorted((soft / "source/8086-SSE").glob("*.h"))
            },
            "compile_flags": common,
            "libraries": {
                platform: sha(root / platform / "libdcp-fp80.a")
                for platform, _, _ in platforms
            },
            "sources": {
                str(p.relative_to(soft)) if p.is_relative_to(soft) else p.name: sha(p)
                for p in inputs
            },
        },
        indent=2,
    )
    + "\n"
)
