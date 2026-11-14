#!/usr/bin/env python3
"""Build private real-module traces, stopping on explicit missing coverage."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--root", type=Path, required=True)
parser.add_argument("--dump", type=Path, required=True)
parser.add_argument("--lift", action="append", default=[])
parser.add_argument("--ios", action="store_true")
args = parser.parse_args()
root = args.root.resolve()
if (
    len(root.parts) < 3
    or root.parts[1] != "Volumes"
    or not Path("/Volumes", root.parts[2]).is_mount()
):
    parser.error("Build root must be on a mounted external disk")
root.mkdir(parents=True, exist_ok=True)
source = Path(__file__).resolve().parents[2]
llvm = Path("/opt/homebrew/opt/llvm/bin")
backend = root.parent / "cpu-backends"
lifter = backend / "remill-build/bin/lift/remill-lift-23"


def run(command, log):
    with (root / log).open("w") as stream:
        subprocess.run(
            [str(x) for x in command],
            stdout=stream,
            stderr=subprocess.STDOUT,
            check=True,
        )


cache_hash = hashlib.sha256(Path(__file__).read_bytes())
cache_hash.update((source / "ios/game/module-tool.cpp").read_bytes())
cache_hash.update((source / "ios/game/offline-stubs.json").read_bytes())
for path in [args.dump / "eboot.bin", args.dump / "sce_module/libc.prx"]:
    cache_hash.update(hashlib.sha256(path.read_bytes()).digest())
for path in sorted((source / "linux-port/src").rglob("*")):
    if path.suffix in (".cpp", ".h", ".inc"):
        cache_hash.update(path.read_bytes())
cache_hash.update(str(args.dump.resolve()).encode())
cache_hash.update(str(root).encode())
cache_path = root / "prepared-modules.json"
cached = json.loads(cache_path.read_text()) if cache_path.exists() else {}
expected = [
    "f9bff150c4c28507096e7e143a07492063ed89bf05ac6cb995a55a0946e3508a",
    "1e952f9486c966e8e3058c01902c8d9c7382acfd3030362db19dfd74681f925c",
]
for path, wanted in zip(
    [args.dump / "eboot.bin", args.dump / "sce_module/libc.prx"], expected
):
    if hashlib.sha256(path.read_bytes()).hexdigest() != wanted:
        raise SystemExit("Unsupported executable hash: " + path.name)
required = [
    "modules.h",
    "modules.s",
    "eboot.image",
    "libc.image",
    "eboot.fixups",
    "libc.fixups",
]
if cached.get("key") == cache_hash.hexdigest() and all(
    (root / name).exists()
    and hashlib.sha256((root / name).read_bytes()).hexdigest()
    == cached.get("outputs", {}).get(name)
    for name in required
):
    modules = cached["modules"]
    strlen = cached["strlen"]
    for module in modules:
        module["image"] = (root / (module["name"] + ".image")).read_bytes()
else:
    run(
        [
            "xcrun",
            "clang++",
            "-std=c++17",
            "-O2",
            "-I" + str(source / "linux-port/src"),
            source / "ios/game/module-tool.cpp",
            source / "linux-port/src/loader/elf.cpp",
            source / "linux-port/src/loader/nid.cpp",
            "-o",
            root / "module-tool",
        ],
        "tool-build.log",
    )
    expected = [
        "f9bff150c4c28507096e7e143a07492063ed89bf05ac6cb995a55a0946e3508a",
        "1e952f9486c966e8e3058c01902c8d9c7382acfd3030362db19dfd74681f925c",
    ]
    modules = []
    for index, (name, path) in enumerate(
        [
            ("eboot", args.dump / "eboot.bin"),
            ("libc", args.dump / "sce_module/libc.prx"),
        ]
    ):
        if hashlib.sha256(path.read_bytes()).hexdigest() != expected[index]:
            raise SystemExit("Unsupported executable hash: " + name)
        run([root / "module-tool", path, root / (name + ".elf")], name + ".json")
        metadata = json.loads((root / (name + ".json")).read_text())
        image = bytearray(metadata["image_size"])
        elf = (root / (name + ".elf")).read_bytes()
        for segment in metadata["segments"]:
            start = segment["vaddr"]
            size = segment["filesz"]
            image[start : start + size] = elf[
                segment["offset"] : segment["offset"] + size
            ]
        metadata.update(
            name=name, image=image, canonical=0x20000000 + index * 0x10000000
        )
        modules.append(metadata)

    # Decode SDK identifiers with the same NID algorithm as the shared loader.
    import base64

    names = {}
    text = (source / "linux-port/src/loader/sce_names.inc").read_text()
    extra_names = set()
    for path in (source / "linux-port/src").rglob("*.cpp"):
        extra_names.update(re.findall(r"\b[A-Za-z_][A-Za-z_0-9]*\b", path.read_text()))
    for name in (
        re.findall(r'\{"[^\"]+", "([^\"]+)"\}', text)
        + list(extra_names)
        + [
            "_init_env",
            "strlen",
            "memcpy",
            "memmove",
            "memset",
            "memcmp",
            "strcmp",
            "malloc",
            "free",
        ]
    ):
        digest = hashlib.sha1(
            name.encode() + bytes.fromhex("518d64a635ded8c1e6b039b1c3e55230")
        ).digest()[:8][::-1]
        names[base64.b64encode(digest).decode().replace("/", "-").replace("=", "")] = (
            name
        )
    exports = {}
    for index, module in enumerate(modules):
        for symbol in module["symbols"]:
            if symbol["defined"]:
                exports[symbol["nid"].split("#")[0]] = (index, symbol["value"])
    imports = []
    for index, module in enumerate(modules):
        fixups = []
        for offset, kind, symbol, addend in module["relocations"]:
            target, value = 2, 0
            if kind == 0:
                continue
            if kind == 8:
                target, value = index, addend
            elif kind in (1, 6, 7):
                sym = module["symbols"][symbol]
                nid = sym["nid"].split("#")[0]
                if sym["defined"]:
                    target, value = index, sym["value"]
                elif nid in exports:
                    target, value = exports[nid]
                else:
                    name = names.get(nid, nid)
                    if name not in imports:
                        imports.append(name)
                    value = 0x70000000 + imports.index(name) * 16
                if kind == 1:
                    value += addend
                if names.get(nid) == "__stack_chk_guard":
                    target, value = 3, 0
            elif kind == 16:
                value = index + 1
            elif kind == 17:
                value = (module["symbols"][symbol]["value"] if symbol else 0) + addend
            else:
                raise SystemExit("Unsupported relocation " + str(kind))
            if offset + 8 > len(module["image"]):
                raise SystemExit("Invalid relocation offset")
            fixups.append(struct.pack("<QQQ", offset, value % 2**64, target))
        (root / (module["name"] + ".image")).write_bytes(module["image"])
        (root / (module["name"] + ".fixups")).write_bytes(b"".join(fixups))
    assembly = ".section __DATA,__const\n"
    header = """#pragma once
    struct Fixup { uint64_t offset,value,module; };
    struct ModuleData { uint64_t canonical; const uint8_t *begin,*end; const Fixup *fixups,*fixupsEnd; };
    """
    for module in modules:
        name = module["name"]
        for suffix, file in [("data", "image"), ("fixups", "fixups")]:
            assembly += f'.p2align 3\n.globl _{name}_{suffix}_begin\n_{name}_{suffix}_begin:\n.incbin "{root / (name + "." + file)}"\n.globl _{name}_{suffix}_end\n_{name}_{suffix}_end:\n'
            typ = "uint8_t" if suffix == "data" else "Fixup"
            header += f'extern "C" const {typ} {name}_{suffix}_begin[], {name}_{suffix}_end[];\n'
    header += (
        "static const ModuleData moduleData[] = {"
        + ",".join(
            "{"
            + str(m["canonical"])
            + ","
            + m["name"]
            + "_data_begin,"
            + m["name"]
            + "_data_end,"
            + m["name"]
            + "_fixups_begin,"
            + m["name"]
            + "_fixups_end}"
            for m in modules
        )
        + "};\n"
    )
    strlen = next(s["value"] for s in modules[1]["symbols"] if s["name"] == "strlen")
    header += (
        "static constexpr uint64_t libcStrlen="
        + str(0x30000000 + strlen)
        + ";\nstatic constexpr uint64_t ebootEntry="
        + str(0x20000000 + modules[0]["entry"])
        + ";\n"
    )
    header += (
        "static constexpr uint64_t procParam="
        + str(0x20000000 + modules[0]["proc_param"])
        + ";\n"
    )
    header += (
        "static const uint64_t tlsTemplates[][3] = {"
        + ",".join(
            "{"
            + str(m["canonical"] + m["tls_vaddr"])
            + ","
            + str(m["tls_filesz"])
            + ","
            + str(m["tls_memsz"])
            + "}"
            for m in modules
        )
        + "};\n"
    )
    header += (
        "static const char *defaultGameRoot="
        + json.dumps(str(args.dump.resolve()))
        + ";\n"
    )
    header += (
        "static const char *defaultSaveRoot=" + json.dumps(str(root / "saves")) + ";\n"
    )
    header += (
        "static const char *offlineStubs[] = {"
        + ",".join(
            json.dumps(x)
            for x in json.loads((source / "ios/game/offline-stubs.json").read_text())
        )
        + "};\n"
    )
    header += (
        "static const char *imports[] = {"
        + ",".join(json.dumps(x) for x in imports)
        + "};\n"
    )
    (root / "modules.h").write_text(header)
    (root / "modules.s").write_text(assembly)
    state = {
        "outputs": {
            name: hashlib.sha256((root / name).read_bytes()).hexdigest()
            for name in required
        },
        "key": cache_hash.hexdigest(),
        "strlen": strlen,
        "modules": [
            {
                key: value
                for key, value in module.items()
                if key not in ("image", "symbols", "relocations")
            }
            for module in modules
        ],
    }
    cache_path.write_text(json.dumps(state, indent=2) + "\n")

oracle = root / "strlen-reference"
reference = source / "ios/game/strlen-reference.cpp"
if not oracle.exists() or oracle.stat().st_mtime < reference.stat().st_mtime:
    (root / "strlen-reference.s").write_text(
        '.text\n.globl _real_strlen\n_real_strlen:\n.incbin "'
        + str(root / "libc.image")
        + '",'
        + str(strlen)
        + ",256\n"
    )
    run(
        [
            "xcrun",
            "clang++",
            "-arch",
            "x86_64",
            "-O2",
            reference,
            root / "strlen-reference.s",
            "-o",
            oracle,
        ],
        "strlen-reference-build.log",
    )
with (root / "strlen-reference-result.json").open("w") as stream:
    import os

    subprocess.run(
        [str(oracle)],
        stdout=stream,
        check=True,
        env=dict(os.environ, ROSETTA_ADVERTISE_AVX="1"),
    )
trace_dir = root / "traces"
trace_dir.mkdir(exist_ok=True)
for text in args.lift:
    pc = int(text, 16)
    module = next(
        m for m in modules if m["canonical"] <= pc < m["canonical"] + len(m["image"])
    )
    offset = pc - module["canonical"]
    begin = max(0, offset - 0x1000)
    end = min(len(module["image"]), offset + 0x3000)
    out = trace_dir / (hex(pc)[2:] + ".ll")
    run(
        [
            lifter,
            "--os",
            "linux",
            "--arch",
            "amd64_avx",
            "--address",
            str(module["canonical"] + begin),
            "--entry_address",
            str(pc),
            "--bytes",
            module["image"][begin:end].hex(),
            "--ir_out",
            out,
        ],
        hex(pc)[2:] + "-lift.log",
    )
    # Permit explicit runtime faults to unwind to the outer diagnostic boundary.
    # x86 unwind directives/attributes are replaced by the arm64 target compiler.
    ir = out.read_text().replace(" nounwind", "")
    for group in re.findall(r"^attributes #(\d+) = \{\s*\}$", ir, re.M):
        ir = re.sub(r"^attributes #" + group + r" = \{\s*\}\n", "", ir, flags=re.M)
        ir = re.sub(r" #" + group + r"\b", "", ir)
    ir = re.sub(r"^(define .*?) \{$", r"\1 uwtable {", ir, flags=re.M)
    out.write_text(ir)

# Observe every signed trace entry, including direct calls resolved by the linker.
for path in sorted(trace_dir.glob("*.ll")):
    ir = path.read_text()
    if "@dcp_trace_enter" not in ir:
        ir, count = re.subn(
            r"^(define ptr @sub_[^\n]+\{\n)([A-Za-z0-9_.]+:[^\n]*\n)?",
            lambda m: (
                m.group(0)
                + "  call void @dcp_trace_enter(ptr %state, i64 %program_counter)\n"
            ),
            ir,
            flags=re.M,
        )
        if not count:
            raise SystemExit("Cannot instrument " + path.name)
        ir += "\ndeclare void @dcp_trace_enter(ptr, i64)\n"
        path.write_text(ir)

definitions = set()
references = set()
for path in sorted(trace_dir.glob("*.ll")):
    ir = path.read_text()
    definitions.update(re.findall(r"^define ptr @(sub_[0-9a-f]+)\(", ir, re.M))
    references.update(re.findall(r"@(sub_[0-9a-f]+)\(", ir))
trace_hash = hashlib.sha256()
for path in sorted(trace_dir.glob("*.ll")):
    trace_hash.update(path.read_bytes())
traces = (
    '#pragma once\nstatic const char *traceHash="' + trace_hash.hexdigest() + '";\n'
)
traces += "\n".join(
    'extern "C" Memory *' + s + "(State &,uint64_t,Memory *);"
    for s in sorted(definitions, key=lambda value: int(value[4:], 16))
)
traces += (
    "\nstruct Trace { uint64_t pc; Lifted function; };\nstatic const Trace traces[] = {"
    + ",".join(
        "{" + str(int(s[4:], 16)) + ",&" + s + "}"
        for s in sorted(definitions, key=lambda value: int(value[4:], 16))
    )
    + "};\n"
)
(root / "traces.h").write_text(traces)
stubs = (
    '#include "runtime.h"\n'
    + "\n".join(
        'extern "C" Memory *'
        + s
        + "(State &state,uint64_t pc,Memory *memory){return dispatch(state,pc,memory);}"
        for s in sorted(references - definitions)
    )
    + "\n"
)
(root / "stubs.cpp").write_text(stubs)
compile_hash = hashlib.sha256()
for directory in [
    source / "ios",
    source / "linux-port/src",
    backend / "remill/include",
]:
    for path in sorted(directory.rglob("*")):
        if path.is_file() and path.suffix in (".cpp", ".h", ".mm", ".inc"):
            compile_hash.update(path.read_bytes())
compile_hash.update(subprocess.check_output(["xcrun", "clang++", "--version"]))
platforms = [("mac", "arm64-apple-macos15", "macosx")]
if args.ios:
    platforms.append(("ios", "arm64-apple-ios18", "iphoneos"))
for platform, target, sdk in platforms:
    directory = root / platform
    directory.mkdir(exist_ok=True)
    sdk_path = subprocess.check_output(
        ["xcrun", "--sdk", sdk, "--show-sdk-path"], text=True
    ).strip()
    objects = []
    freetype = root.parent / "font-probe" / ("freetype-" + platform) / "libfreetype.a"
    font_includes = [
        source / "linux-port/src",
        root.parent / "macos-rendered/_deps/freetype-src/include",
        freetype.parent / "include",
    ]
    for path in sorted(trace_dir.glob("*.ll")):
        obj = directory / (path.stem + ".o")
        if not obj.exists() or obj.stat().st_mtime < path.stat().st_mtime:
            run(
                [
                    llvm / "clang",
                    "-target",
                    target,
                    "-isysroot",
                    sdk_path,
                    "-O2",
                    "-funwind-tables",
                    "-c",
                    path,
                    "-o",
                    obj,
                ],
                platform + "-" + path.stem + "-compile.log",
            )
        objects.append(obj)
    for path, name in [
        (source / "ios/cpu/runtime.cpp", "memory"),
        (source / "ios/game/runtime.cpp", "runtime"),
        (source / "ios/game/gnm-services.cpp", "gnm"),
        (source / "ios/game/thread-services.cpp", "threads"),
        (source / "ios/game/file-services.cpp", "files"),
        (source / "ios/game/offline-services.cpp", "offline"),
        (source / "ios/game/font-services.cpp", "font"),
        (root / "stubs.cpp", "stubs"),
        (root / "modules.s", "modules"),
    ]:
        obj = directory / (name + ".o")
        command = [
            "xcrun",
            "--sdk",
            sdk,
            "clang++",
            "-target",
            target,
            "-isysroot",
            sdk_path,
            "-std=c++17",
            "-O2",
            "-I" + str(backend / "remill/include"),
            "-I" + str(source / "ios/cpu"),
            "-I" + str(root),
            *["-I" + str(include) for include in font_includes],
            "-c",
            path,
            "-o",
            obj,
        ]
        signature = hashlib.sha256(
            compile_hash.digest() + repr(command).encode() + path.read_bytes()
        )
        if name == "font":
            signature.update(freetype.read_bytes())
        if name == "runtime":
            signature.update(
                (root / "modules.h").read_bytes() + (root / "traces.h").read_bytes()
            )
        record = obj.with_suffix(".signature")
        if (
            not obj.exists()
            or not record.exists()
            or record.read_text() != signature.hexdigest()
        ):
            run(command, platform + "-" + name + "-build.log")
            record.write_text(signature.hexdigest())
        objects.append(obj)
    import shutil

    shutil.copy2(freetype, directory / "libfreetype.a")
    archive = directory / "libcpu-fixtures.a"
    archive.unlink(missing_ok=True)
    run([llvm / "llvm-ar", "rcs", archive, *objects], platform + "-archive.log")
    if platform == "mac":
        (root / "cli.cpp").write_text(
            '#include <cstdio>\nextern "C" const char *dcp_cpu_report();\nint main(){puts(dcp_cpu_report());}\n'
        )
        run(
            [
                "xcrun",
                "clang++",
                "-arch",
                "arm64",
                root / "cli.cpp",
                archive,
                freetype,
                "-o",
                root / "game-boot",
            ],
            "mac-link.log",
        )
print("Built", len(definitions), "real module traces")
