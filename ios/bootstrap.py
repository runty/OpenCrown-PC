#!/usr/bin/env python3
"""Build the mobile port from pinned sources without installing or launching apps."""

import argparse
import fcntl
import hashlib
import json
import os
import re
from pathlib import Path
import shlex
import shutil
import signal
import subprocess
import sys
import tarfile
from datetime import datetime, timezone

SOURCE = Path(__file__).resolve().parents[1]
REMILL = "66c488c9911fd9b368e4d0c9647d74239f8bba8b"
ARCHIVES = {
    "sdl": (
        "https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz",
        "9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3",
    ),
    "freetype": (
        "https://github.com/freetype/freetype/archive/refs/tags/VER-2-13-3.tar.gz",
        "bc5c898e4756d373e0d991bab053036c5eb2aa7c0d5c67e8662ddc6da40c4103",
    ),
}
PATCHES = [
    "remill-llvm23.patch",
    "remill-lifter-lifetime.patch",
    "remill-packed-simd.patch",
    "remill-pcmpistrm.patch",
    "remill-sse41-lanes.patch",
    "remill-avx-extract.patch",
    "remill-packed-shifts.patch",
    "remill-x87-control.patch",
    "remill-movemask.patch",
    "remill-startup-variable-blend.patch",
    "remill-startup-lane-insert.patch",
    "remill-startup-permute128.patch",
    "remill-startup-dword-shifts.patch",
    "remill-sse41-rounding.patch",
    "remill-confirmed-packed-float.patch",
    "remill-confirmed-packed-integer.patch",
    "remill-movbe-stores.patch",
    "remill-scalar-fp-memory.patch",
    "remill-sse-nan-results.patch",
]


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def external(path):
    path = path.expanduser().resolve()
    if (
        len(path.parts) < 3
        or path.parts[1] != "Volumes"
        or not Path("/Volumes", path.parts[2]).is_mount()
    ):
        raise SystemExit(f"Path must be on a mounted external disk: {path}")
    return path


def output(command):
    return subprocess.check_output([str(part) for part in command], text=True).strip()


def render_cache_options(directory, source):
    # CMake records the checkout's absolute path. A new pipeline pin may
    # use a different checkout in the same external build workspace.
    cache = directory / "CMakeCache.txt"
    if cache.is_file():
        prefix = "CMAKE_HOME_DIRECTORY:INTERNAL="
        for line in cache.read_text().splitlines():
            if line.startswith(prefix) and Path(line[len(prefix):]).resolve() != source.resolve():
                return ["--fresh"]
    return []


def main():
    if sys.version_info < (3, 12):
        raise SystemExit(
            "Python 3.12 or newer is required (safe tar extraction filter)"
        )
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root",
        type=Path,
        required=True,
        help="External build workspace, separate from the game dump",
    )
    parser.add_argument(
        "--dump",
        type=Path,
        required=True,
        help="Private decrypted CUSA10454 dump; never copied into source",
    )
    parser.add_argument(
        "--team", help="Apple development team for signed iPhone/iPad app"
    )
    parser.add_argument(
        "--backend-root", type=Path, help="Reuse pinned Remill sources and build cache"
    )
    parser.add_argument("--sdl-source", type=Path, help="Reuse SDL 3.4.18 source cache")
    parser.add_argument(
        "--font-source", type=Path, help="Reuse FreeType 2.13.3 source cache"
    )
    parser.add_argument("--font-output", type=Path, help="Reuse font-build.sh output")
    parser.add_argument(
        "--fp80-output", type=Path, help="Reuse SoftFloat/lowering build output"
    )
    parser.add_argument(
        "--llvm-prefix", type=Path, default=Path("/opt/homebrew/opt/llvm")
    )
    parser.add_argument(
        "--rounds",
        type=int,
        default=128,
        help="Trace discovery rounds; 0 skips discovery",
    )
    parser.add_argument("--prefetch", type=int, default=12)
    parser.add_argument(
        "--save-seed",
        type=Path,
        help="Quiescent disposable save tree copied into each discovery attempt",
    )
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument(
        "--translated-libc-bulk",
        action="store_true",
        help="Use original translated memcpy/memmove/memset for comparison",
    )
    parser.add_argument(
        "--prepare-only",
        action="store_true",
        help="Build dependencies, fixtures and seed module libraries; skip discovery and signed apps",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Validate configuration and print resolved paths without changing files",
    )
    args = parser.parse_args()
    if args.rounds < 0 or args.prefetch < 0 or not 1 <= args.jobs <= 32:
        parser.error("rounds/prefetch must be nonnegative and jobs between 1 and 32")
    if not args.team and not (args.prepare_only or args.check):
        parser.error("--team is required for signed app builds (or use --prepare-only)")
    root, dump = external(args.root), external(args.dump)
    if root == dump or root.is_relative_to(dump) or dump.is_relative_to(root):
        parser.error("Build workspace and dump must be separate directory trees")
    backend = external(args.backend_root or root / "cpu-backends")
    font_output = external(args.font_output or root / "font-probe")
    fp80_output = external(args.fp80_output or root / "fp80-probe")
    cached_sources = root / "macos-rendered/_deps"
    sdl = external(
        args.sdl_source
        or (
            cached_sources / "sdl3-src"
            if (cached_sources / "sdl3-src").exists()
            else root / "sources/sdl"
        )
    )
    font = external(
        args.font_source
        or (
            cached_sources / "freetype-src"
            if (cached_sources / "freetype-src").exists()
            else root / "sources/freetype"
        )
    )
    llvm = args.llvm_prefix.expanduser().resolve()
    for tool in [
        "cmake",
        "ninja",
        "git",
        "curl",
        "spirv-cross",
        "xcrun",
        "xcodebuild",
    ] + ([] if args.prepare_only else ["xcodegen"]):
        if not shutil.which(tool):
            raise SystemExit(
                f"Required tool is missing: {tool}; bootstrap does not install software"
            )
    version = output([llvm / "bin/llvm-config", "--version"])
    if version != "23.1.5":
        raise SystemExit(f"Expected LLVM 23.1.5, found {version} at {llvm}")
    sdks = {
        sdk: {
            "path": output(["xcrun", "--sdk", sdk, "--show-sdk-path"]),
            "version": output(["xcrun", "--sdk", sdk, "--show-sdk-version"]),
        }
        for sdk in ["macosx", "iphoneos", "iphonesimulator"]
    }
    expected = {
        "eboot.bin": "f9bff150c4c28507096e7e143a07492063ed89bf05ac6cb995a55a0946e3508a",
        "sce_module/libc.prx": "1e952f9486c966e8e3058c01902c8d9c7382acfd3030362db19dfd74681f925c",
    }
    for name, sha in expected.items():
        path = dump / name
        if not path.is_file() or digest(path) != sha:
            raise SystemExit(f"Unsupported or missing private executable: {path}")
    subprocess.run(
        ["df", "-h", str(Path("/Volumes", root.parts[2])), "/System/Volumes/Data"],
        check=True,
    )
    plan = {
        "root": str(root),
        "dump": str(dump),
        "backend": str(backend),
        "sdl_source": str(sdl),
        "font_source": str(font),
        "font_output": str(font_output),
        "fp80_output": str(fp80_output),
        "softfloat_archive_sha256": "21130ce885d35c1fe73fc1e1bf2244178167e05c6747cad5f450cc991714c746",
        "llvm": str(llvm),
        "sdks": sdks,
        "remill_revision": REMILL,
        "archives": ARCHIVES,
        "prepare_only": args.prepare_only,
        "native_libc_bulk": not args.translated_libc_bulk,
        "patches": {name: digest(SOURCE / "ios" / name) for name in PATCHES},
        "sdl_patch": digest(SOURCE / "ios/render/sdl-simulator-metal.patch"),
    }
    print(json.dumps(plan, indent=2), flush=True)
    if args.check:
        return
    # Catch pre-lock legacy discovery jobs too. Do not print process arguments:
    # lifters can carry private game instruction bytes on their command line.
    game = root / "ios-game"
    for line in output(["ps", "-axo", "command="]).splitlines():
        try:
            words = shlex.split(line)
        except ValueError:
            continue
        if (
            any(word.endswith(("/advance.py", "/build.py")) for word in words)
            and "--root" in words
        ):
            index = words.index("--root") + 1
            if index < len(words):
                active_root = Path(words[index]).resolve()
                active_backend = active_root.parent / "cpu-backends"
                for option in ["--backend-root", "--remill"]:
                    if option in words:
                        active_backend = Path(words[words.index(option) + 1]).resolve()
                if active_root == game or active_backend == backend:
                    raise SystemExit(
                        f"An existing build/discovery process uses this game or backend cache; let it finish or request its graceful stop"
                    )
    root.mkdir(parents=True, exist_ok=True)
    lock = (root / "bootstrap.lock").open("a")
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        raise SystemExit("Another bootstrap owns this workspace")
    backend.mkdir(parents=True, exist_ok=True)
    backend_lock = (backend / "bootstrap.lock").open("a")
    try:
        fcntl.flock(backend_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        raise SystemExit("Another bootstrap owns this backend cache")
    logs = root / "bootstrap-logs"
    logs.mkdir(exist_ok=True)
    env = dict(
        os.environ,
        DCP_BACKEND_ROOT=str(backend),
        DCP_FONT_SOURCE=str(font),
        DCP_FONT_OUTPUT=str(font_output),
        DCP_FP80_OUTPUT=str(fp80_output),
        DCP_LLVM_PREFIX=str(llvm),
        DCP_SDL_SOURCE=str(sdl),
    )
    dump_icon = dump / "sce_sys/icon0.png"
    if "DCP_APP_ICON" not in env and dump_icon.is_file():
        env["DCP_APP_ICON"] = str(dump_icon)
        env.setdefault("DCP_APP_ICON_ALLOW_UPSCALE", "1")
    stop_requested = False

    def stop(signum, frame):
        nonlocal stop_requested
        stop_requested = True

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)

    def check_stop():
        if stop_requested or (root / "bootstrap.stop").exists():
            print(
                "Bootstrap stopped between stages; current child finished and caches retained",
                flush=True,
            )
            raise SystemExit(0)

    def run(name, command, round_limit=False):
        check_stop()
        log = logs / (name + ".log")
        print(f"{name}: {log}", flush=True)
        with log.open("w") as stream:
            result = subprocess.run(
                [str(part) for part in command],
                stdout=stream,
                stderr=subprocess.STDOUT,
                env=env,
                start_new_session=True,
            )
        check_stop()
        if result.returncode:
            if round_limit and log.read_text(errors="replace").rstrip().endswith(
                "Trace discovery reached its round limit; rerun to resume cached work"
            ):
                print(
                    "Trace discovery budget reached; packaging partial coverage. Real game boot is not verified.",
                    flush=True,
                )
                return
            raise SystemExit(f"{name} failed ({result.returncode}); read {log}")

    def patch(tree, path):
        # reverse-check distinguishes an already applied patch from divergent source.
        if (
            subprocess.run(
                ["git", "-C", str(tree), "apply", "--reverse", "--check", str(path)],
                capture_output=True,
            ).returncode
            == 0
        ):
            return
        if subprocess.run(
            ["git", "-C", str(tree), "apply", "--check", str(path)], capture_output=True
        ).returncode:
            raise SystemExit(
                f"Patch neither cleanly applies nor reverse-checks: {path}; inspect {tree}, no reset performed"
            )
        run("patch-" + path.stem, ["git", "-C", tree, "apply", path])

    def archive_source(kind, target):
        if target.exists():
            return
        url, sha = ARCHIVES[kind]
        downloads = root / "downloads"
        downloads.mkdir(exist_ok=True)
        archive = downloads / (kind + ".tar.gz")
        if not archive.exists():
            temporary = archive.with_suffix(".partial")
            run(
                "download-" + kind,
                ["curl", "--fail", "--location", "--retry", "3", url, "-o", temporary],
            )
            if digest(temporary) != sha:
                raise SystemExit(f"Archive SHA mismatch: {temporary}")
            temporary.replace(archive)
        if digest(archive) != sha:
            raise SystemExit(f"Archive SHA mismatch: {archive}")
        unpack = downloads / (kind + "-unpack")
        unpack.mkdir(exist_ok=True)
        with tarfile.open(archive) as tar:
            tar.extractall(unpack, filter="data")
        children = list(unpack.iterdir())
        if len(children) != 1 or not children[0].is_dir():
            raise SystemExit(f"Unexpected archive layout: {archive}")
        target.parent.mkdir(parents=True, exist_ok=True)
        children[0].rename(target)
        unpack.rmdir()

    remill = backend / "remill"
    if not remill.exists():
        backend.mkdir(parents=True, exist_ok=True)
        run(
            "remill-clone",
            ["git", "clone", "https://github.com/lifting-bits/remill.git", remill],
        )
        run("remill-pin", ["git", "-C", remill, "checkout", "--detach", REMILL])
    if output(["git", "-C", remill, "rev-parse", "HEAD"]) != REMILL:
        raise SystemExit(
            f"Remill cache is not pinned to {REMILL}; refusing to reset it"
        )
    run(
        "patch-remill",
        [
            sys.executable,
            SOURCE / "ios/patch-set.py",
            remill,
            *[SOURCE / "ios" / name for name in PATCHES],
        ],
    )
    archive_source("sdl", sdl)
    archive_source("freetype", font)
    # Explicitly reused sources must at least match the release ABI/version.
    sdl_header = (sdl / "include/SDL3/SDL_version.h").read_text()
    if not all(
        re.search(
            r"#define\s+SDL_" + key + r"_VERSION\s+" + str(value) + r"\b", sdl_header
        )
        for key, value in [("MAJOR", 3), ("MINOR", 4), ("MICRO", 18)]
    ):
        raise SystemExit("SDL source is not release 3.4.18")
    ft_header = (font / "include/freetype/freetype.h").read_text()
    if not all(
        re.search(r"#define\s+FREETYPE_" + key + r"\s+" + str(value) + r"\b", ft_header)
        for key, value in [("MAJOR", 2), ("MINOR", 13), ("PATCH", 3)]
    ):
        raise SystemExit("FreeType source is not release 2.13.3")
    patch(sdl, SOURCE / "ios/render/sdl-simulator-metal.patch")
    common = ["-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release"]
    run(
        "remill-deps-configure",
        [
            "cmake",
            *common,
            "-S",
            remill / "dependencies",
            "-B",
            backend / "remill-deps",
            "-DUSE_EXTERNAL_LLVM=ON",
            f"-DCMAKE_PREFIX_PATH={llvm}",
            f"-DCMAKE_INSTALL_PREFIX={backend / 'remill-install'}",
        ],
    )
    run(
        "remill-deps-build",
        ["cmake", "--build", backend / "remill-deps", "--parallel", args.jobs],
    )
    run(
        "remill-configure",
        [
            "cmake",
            *common,
            "-S",
            remill,
            "-B",
            backend / "remill-build",
            f"-DCMAKE_PREFIX_PATH={llvm};{backend / 'remill-install'}",
            f"-DCMAKE_INSTALL_PREFIX={backend / 'remill-install'}",
        ],
    )
    run(
        "remill-build",
        ["cmake", "--build", backend / "remill-build", "--parallel", args.jobs],
    )
    run(
        "fp80",
        [
            sys.executable,
            SOURCE / "ios/cpu/fp80/build.py",
            "--output",
            fp80_output,
            "--llvm",
            llvm,
            "--jobs",
            args.jobs,
        ],
    )
    run(
        "fp80-oracle",
        [sys.executable, SOURCE / "ios/cpu/fp80/test.py", "--build", fp80_output],
    )
    run(
        "fp80-lowered",
        [
            sys.executable,
            SOURCE / "ios/cpu/fp80/lowered-test.py",
            "--build",
            fp80_output,
        ],
    )
    for fixture in ["classify", "remainder"]:
        run(
            "fp80-" + fixture,
            [
                sys.executable,
                SOURCE / f"ios/cpu/fp80/{fixture}-test.py",
                "--build",
                fp80_output,
                "--output",
                root / f"fp80-{fixture}-probe",
            ],
        )
    run(
        "fp80-control",
        [
            sys.executable,
            SOURCE / "ios/cpu/fp80/control-test.py",
            "--build",
            fp80_output,
            "--backend-root",
            backend,
            "--llvm",
            llvm,
        ],
    )
    run("font", ["bash", SOURCE / "ios/game/font-build.sh", font, font_output])
    run(
        "platform",
        ["bash", SOURCE / "ios/platform/build-check.sh", root / "platform-probe"],
    )
    run("storage", ["bash", SOURCE / "ios/storage/build.sh", root / "storage-probe"])
    for platform, sdk, minimum in [
        ("mac", "macosx", "15.0"),
        ("iphoneos", "iphoneos", "18.0"),
        ("simulator", "iphonesimulator", "18.0"),
    ]:
        directory = root / "ios-render" / platform
        renderer_source = SOURCE / "ios/render"
        run(
            "render-" + platform + "-configure",
            [
                "cmake",
                *render_cache_options(directory, renderer_source),
                *common,
                "-S",
                renderer_source,
                "-B",
                directory,
                f"-DDCP_SDL_SOURCE={sdl}",
                f"-DDCP_FREETYPE_SOURCE={font}",
                "-DCMAKE_OSX_ARCHITECTURES=arm64",
                f"-DCMAKE_OSX_SYSROOT={sdk}",
                f"-DCMAKE_OSX_DEPLOYMENT_TARGET={minimum}",
                *([] if platform == "mac" else ["-DCMAKE_SYSTEM_NAME=iOS"]),
            ],
        )
        run(
            "render-" + platform + "-build",
            ["cmake", "--build", directory, "--parallel", args.jobs],
        )
    run(
        "render-fixtures",
        [
            sys.executable,
            SOURCE / "ios/render/run-fixtures.py",
            "--build",
            root / "ios-render/mac",
            "--output",
            root / "ios-render/verified",
        ],
    )
    run(
        "cpu-fixtures",
        [
            sys.executable,
            SOURCE / "ios/cpu/build.py",
            "--remill",
            backend,
            "--llvm",
            llvm,
            "--output",
            root / "cpu-harness",
        ],
    )
    run(
        "memory-cache",
        [
            sys.executable,
            SOURCE / "ios/cpu/review/memory-cache.py",
            "--output",
            root / "memory-cache-review",
            "--backend-root",
            backend,
        ],
    )
    run(
        "libc-bulk",
        [
            sys.executable,
            SOURCE / "ios/game/libc-bulk-test.py",
            "--output",
            root / "libc-bulk-probe",
            "--backend-root",
            backend,
            "--llvm",
            llvm,
        ],
    )
    run(
        "tail-dispatch",
        [
            sys.executable,
            SOURCE / "ios/cpu/review/tail-dispatch.py",
            "--output",
            root / "tail-dispatch-probe",
            "--backend-root",
            backend,
            "--llvm",
            llvm,
        ],
    )
    run(
        "thread-cancel",
        [SOURCE / "ios/game/test-thread-cancel.sh", root / "thread-cancel-probe"],
    )
    run("ime", [SOURCE / "ios/game/test-ime.sh", root / "ime-probe"])
    for review in ["rounding", "confirmed-packed", "movbe"]:
        run(
            review,
            [
                sys.executable,
                SOURCE / f"ios/cpu/review/{review}.py",
                "--output",
                root / f"{review}-probe",
                "--backends",
                backend,
                "--llvm",
                llvm,
            ],
        )
    builder = [
        sys.executable,
        SOURCE / "ios/game/build.py",
        "--root",
        game,
        "--dump",
        dump,
        "--jobs",
        args.jobs,
    ]
    if args.translated_libc_bulk:
        builder.append("--translated-libc-bulk")
    # Include the observed startup and gameplay entries and every distinct case
    # of the supported dump’s character-selection, edit-result, food and
    # equipment-menu switches, plus the paired status getter/setter switches
    # reached in the Ruins interior. Equipment uses ELF 757ee4; status uses
    # ELF 71822c/718244. Each has six signed relative entries with unsigned
    # index <= 5; status preserves its zero-extended index through R15/stack.
    # The Harpy defeat/Continue flow reaches ELF 519d70's state dispatch:
    # a zero-extended byte index guarded unsigned <= 9 selects ten signed
    # offsets from ELF 75a610. Include every case, not only observed 519de5.
    # Victory also reaches this owner, then its nested result dispatches.
    # Tables 75a648/75a674/75a638 have unsigned index bounds 10/14/3;
    # the middle table uses a zero-extended EAX index after decrement.
    # Include all 20 distinct targets across these three tables.
    # Advancing past level-up reaches ELF 1cff0's three loop switches.
    # Their preloaded RBX/R13 bases (645080/645060/645040) are retained;
    # LEA-to-EAX subtracts 12 and zero extends before unsigned <=8/7/7
    # guards. Cover all 19 distinct targets, including observed 1d410.
    traces = game / "traces"
    required = (
        "300717e0", "30000000", "2054c690", "203d54a0", "2063b950",
        "203a1b56", "203a1b72", "203a1b8b", "203a1ba4",
        "203ae061", "203ae9f0", "203aea03", "203aea16",
        "202ab640", "202ab6b5", "202ab6fc", "202ab767", "202ab85e", "202ab8e7",
        "204cf906", "204cf937", "204cf95b", "204cf97c", "204cf99d", "204cf9be",
        "202e0ca7", "202e0cc4", "202e0cda", "202e0cf4", "202e0d0a", "202e0d20",
        "202e0e50", "202e0e5c", "202e0e68", "202e0e74", "202e0e80", "202e0e8c",
        "20519de5", "2051a0e5", "2051a17d", "2051a5ed", "2051a80f",
        "2051aaf8", "2051ae82", "2051af32", "2051b057", "2051b0d7",
        "2051a7e8", "2051b43f", "2051b5a6", "2051b5f8", "2051b6b5",
        "2051b779", "2051b81d", "2051b8a1", "2051b8ee", "2051b93b",
        "2051b95b", "2051b9bf", "2051ba23", "2051ba70", "2051baae",
        "2051bd81", "2051c06e", "2051c0a9", "2051c0ee", "2051c1d0",
        "2001d0dd", "2001d0eb", "2001d0f8", "2001d0ff", "2001d106",
        "2001d10e", "2001d120", "2001d2cd", "2001d2e0", "2001d2f0",
        "2001d300", "2001d375", "2001d3cb", "2001d3d9", "2001d3e6",
        "2001d3ed", "2001d3f4", "2001d3fc", "2001d410",
    )
    missing = [pc for pc in required if not (traces / (pc + ".ll")).is_file()]
    run("game-seed" if missing else "game-cache-check",
        [*builder, *[arg for pc in missing for arg in ("--lift", pc)]])
    provenance = json.loads((game / "trace-provenance.json").read_text())
    if not provenance["complete_current_backend"]:
        run("game-relift", [*builder, "--relift-stale"])
    for fixture in ["provenance-test.py", "unwind-test.py"]:
        run(
            fixture.removesuffix(".py"),
            [
                sys.executable,
                SOURCE / "ios/game" / fixture,
                *(
                    ["--output", root / "unwind-test"]
                    if fixture == "unwind-test.py"
                    else []
                ),
            ],
        )
    atlas = game / "unwind-atlas"
    run(
        "game-unwind-atlas",
        [
            sys.executable,
            SOURCE / "ios/game/unwind_atlas.py",
            "--root",
            game,
            "--output",
            atlas,
        ],
    )
    if (atlas / "missing-targets.txt").read_text().strip():
        run(
            "game-unwind-lift", [*builder, "--lift-file", atlas / "missing-targets.txt"]
        )
    noreturn_source = SOURCE / "ios/game/noreturn"
    noreturn_tools = root / "noreturn-tools"
    run(
        "noreturn-fixtures",
        ["bash", noreturn_source / "build-and-test.sh", SOURCE, noreturn_tools],
    )
    module_metadata = json.loads((game / "prepared-modules.json").read_text())[
        "modules"
    ]
    module_specs = game / "noreturn-modules.json"
    module_specs.write_text(
        json.dumps(
            [
                {
                    "name": m["name"],
                    "canonical_base": m["canonical"],
                    "elf": str(game / (m["name"] + ".elf")),
                    "image": str(game / (m["name"] + ".image")),
                    "metadata": str(game / (m["name"] + ".json")),
                }
                for m in module_metadata
            ],
            indent=2,
        )
        + "\n"
    )
    noreturn_map = game / "noreturn-derived.json"
    run(
        "noreturn-imports",
        [
            sys.executable,
            noreturn_source / "derive_noreturn.py",
            "--modules",
            module_specs,
            "--nid-oracle",
            noreturn_tools / "nid-oracle",
            "--output",
            noreturn_map,
        ],
    )
    map_bytes = noreturn_map.read_bytes()
    map_hash = hashlib.sha256(map_bytes).hexdigest()
    map_root = game / "noreturn-maps"
    map_root.mkdir(exist_ok=True)
    noreturn_map = map_root / (map_hash + ".json")
    if noreturn_map.exists() and noreturn_map.read_bytes() != map_bytes:
        raise SystemExit("Existing noreturn-map hash snapshot differs from its content")
    if not noreturn_map.exists():
        temporary_map = noreturn_map.with_suffix(".json.partial")
        temporary_map.write_bytes(map_bytes)
        temporary_map.replace(noreturn_map)
    noreturn_records = json.loads(map_bytes)["targets"]
    plan["noreturn_imports_sha256"] = hashlib.sha256(
        noreturn_map.read_bytes()
    ).hexdigest()
    switch_source = SOURCE / "ios/game/control-flow"
    run(
        "switch-atlas-fixtures",
        [
            sys.executable,
            switch_source / "test.py",
            "--output",
            root / "switch-atlas-fixtures",
            "--xed-root",
            backend / "remill-install",
            "--llvm-bin",
            llvm / "bin",
        ],
    )
    run(
        "conditional-switch-fixtures",
        [
            sys.executable,
            switch_source / "conditional-test.py",
            "--output",
            root / "conditional-switch-fixtures",
            "--xed-root",
            backend / "remill-install",
            "--llvm-bin",
            llvm / "bin",
        ],
    )
    for module in module_metadata:
        name = module["name"]
        switch_atlas = game / "switch-atlas" / name
        run(
            "game-switch-atlas-" + name,
            [
                sys.executable,
                switch_source / "catalog.py",
                "--image",
                game / (name + ".image"),
                "--elf",
                game / (name + ".elf"),
                "--atlas",
                atlas / "entries.json",
                "--module",
                name,
                "--base",
                hex(module["canonical"]),
                "--output",
                switch_atlas,
                "--xed-root",
                backend / "remill-install",
                "--trace-directory",
                game / "traces",
                "--noreturn-provenance",
                noreturn_map,
                *[
                    arg
                    for record in noreturn_records
                    if record["module"] == name
                    for arg in ["--noreturn", record["canonical"]]
                ],
            ],
        )
        conditional_atlas = game / "conditional-switch-atlas" / name
        run(
            "conditional-switch-atlas-" + name,
            [
                sys.executable,
                switch_source / "conditional.py",
                "--strict-output",
                switch_atlas,
                "--xed-root",
                backend / "remill-install",
                "--output",
                conditional_atlas,
            ],
        )
        targets = conditional_atlas / "missing-prefetch-with-conditional-targets.txt"
        if targets.read_text().strip():
            run(
                "game-switch-lift-" + name,
                [
                    *builder,
                    "--lift-file",
                    targets,
                ],
            )
    if not args.prepare_only and args.rounds:
        run(
            "game-discovery",
            [
                sys.executable,
                SOURCE / "ios/game/advance.py",
                "--root",
                game,
                "--dump",
                dump,
                "--rounds",
                args.rounds,
                "--prefetch",
                args.prefetch,
                "--jobs",
                args.jobs,
                *(["--save-seed", external(args.save_seed)] if args.save_seed else []),
                *(["--translated-libc-bulk"] if args.translated_libc_bulk else []),
            ],
            round_limit=True,
        )
        if (game / "advance.stop").exists():
            raise SystemExit(
                "Discovery stopped gracefully; remove ios-game/advance.stop to resume. No apps were built."
            )
    run("game-libraries", [*builder, "--ios", "--simulator"])
    run(
        "save-seed",
        [
            sys.executable,
            SOURCE / "ios/game/save-seed-test.py",
            "--output",
            root / "save-seed-probe",
        ],
    )
    run(
        "headless-config",
        [SOURCE / "ios/game/test-headless.sh", root / "headless-probe", game],
    )
    plan.update(
        {
            "completed_utc": datetime.now(timezone.utc).isoformat(),
            "source_revision": output(["git", "-C", SOURCE, "rev-parse", "HEAD"]),
            "source_dirty": bool(
                output(["git", "-C", SOURCE, "status", "--porcelain"])
            ),
            "lifter_sha256": digest(backend / "remill-build/bin/lift/remill-lift-23"),
            "runtime_bc_sha256": digest(
                backend / "remill-build/lib/Arch/X86/Runtime/amd64_avx.bc"
            ),
        }
    )
    plan["dependency_revisions"] = {
        str(path.parent.relative_to(backend)): output(
            ["git", "-C", path.parent, "rev-parse", "HEAD"]
        )
        for directory in [backend / "remill-deps", backend / "remill-build/_deps"]
        for path in directory.rglob(".git")
        if path.is_dir()
    }
    (root / "bootstrap-manifest.json").write_text(json.dumps(plan, indent=2) + "\n")
    if args.prepare_only:
        print(
            "Prepared dependencies, fixtures and seed game libraries. No signed app, installation or device verification was performed."
        )
        return
    for platform in ["simulator", "ios"]:
        env["DCP_BUILD_PLATFORM"] = platform
        run(
            "app-" + platform,
            [
                "bash",
                SOURCE / "ios/build-app.sh",
                root / ("mobile-app-" + platform),
                args.team,
                game,
            ],
        )
    print(
        "Built simulator and signed iPhone/iPad apps. Neither app was installed or launched; real game boot remains a separate verification step."
    )


if __name__ == "__main__":
    main()
