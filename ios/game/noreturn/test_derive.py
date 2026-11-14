#!/usr/bin/env python3
import argparse
import copy
import importlib.util
import json
from pathlib import Path
import struct

spec = importlib.util.spec_from_file_location(
    "derive_noreturn", Path(__file__).with_name("derive_noreturn.py")
)
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)
parser = argparse.ArgumentParser()
parser.add_argument("--nid-oracle", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
identities = tool.read_oracle(args.nid_oracle)
nid = next(iter(identities))
passed = 0


def make(name, changes=None):
    changes = changes or {}
    shift = changes.get("shift", 0)
    text_rva, data_rva = 0x1000 + shift, 0x2000 + shift
    stub, slot = text_rva + 0x40, data_rva + 0x10
    raw = bytearray(0x4200)
    raw[:16] = b"\x7fELF\x02\x01\x01" + bytes(9)
    struct.pack_into(
        "<HHIQQQIHHHHHH",
        raw,
        16,
        0xFE10,
        changes.get("machine", 62),
        1,
        text_rva,
        64,
        0,
        0,
        64,
        56,
        5 if changes.get("overlap") else 4,
        0,
        0,
        0,
    )
    ph = [
        (1, 5, 0x1000, text_rva, 0, 0x100, 0x100, 0x1000),
        (0x61000010, 4, 0x2000, data_rva, 0, 0x100, 0x100, 0x1000),
        (2, 4, 0x3000, 0, 0, 7 * 16, 7 * 16, 8),
        (0x61000000, 4, 0x4000, 0, 0, 0x200, 0x200, 8),
    ]
    if changes.get("overlap"):
        ph.append(ph[0])
    for i, p in enumerate(ph):
        struct.pack_into("<IIQQQQQQ", raw, 64 + i * 56, *p)
    raw[0x1000:0x1100] = b"\x90" * 0x100
    jump = slot - (stub + 6) + changes.get("bad_displacement", 0)
    struct.pack_into(
        "<2siBI Bi",
        raw,
        0x1040,
        changes.get("opcode", b"\xff\x25"),
        jump,
        0x68,
        changes.get("ordinal", 0),
        0xE9,
        changes.get("resolver", text_rva) - (stub + 16),
    )
    struct.pack_into("<Q", raw, 0x2010, changes.get("got", stub + 6))
    reloc_count = 2 if changes.get("duplicate_reloc") else 1
    tags = [
        (tool.STR, 0),
        (tool.STRSZ, 64),
        (tool.SYM, 0x100),
        (tool.SYMSZ, 48),
        (tool.JMP, 0x180),
        (tool.JMPSZ, 24 * reloc_count),
        (0, 0),
    ]
    if changes.get("missing_tag"):
        tags[0] = (0x1234, 0)
    for i, (tag, value) in enumerate(tags):
        struct.pack_into("<qQ", raw, 0x3000 + i * 16, tag, value)
    raw[0x4000 : 0x4000 + len(nid) + 6] = b"\0" + nid.encode() + b"#A#B\0"
    defined = int(changes.get("defined", 0))
    struct.pack_into(
        "<IBBHQQ", raw, 0x4118, 1, changes.get("symbol_info", 0x12), 0, defined, 0, 0
    )
    kind = changes.get("reloc_type", 7)
    for i in range(reloc_count):
        struct.pack_into("<QQq", raw, 0x4180 + i * 24, slot, (1 << 32) | kind, 0)
    segments = [
        dict(offset=p[2], vaddr=p[3], filesz=p[5], memsz=p[6], flags=p[1])
        for p in ph
        if p[0] in (1, 0x61000010)
    ]
    image_size = (max(p["vaddr"] + p["memsz"] for p in segments) + 0x3FFF) & ~0x3FFF
    image = bytearray(image_size)
    for p in segments:
        image[p["vaddr"] : p["vaddr"] + p["filesz"]] = raw[
            p["offset"] : p["offset"] + p["filesz"]
        ]
    metadata = dict(
        image_size=image_size,
        segments=segments,
        symbols=[
            dict(nid="", name="", value=0, defined=0),
            dict(nid=nid + "#A#B", name="", value=0, defined=defined),
        ],
        relocations=[[slot, kind, 1, 0] for i in range(reloc_count)],
    )
    if changes.get("image_mismatch"):
        image[text_rva] ^= 1
    if changes.get("metadata_mismatch"):
        metadata["symbols"][1]["nid"] = "unknown#A#B"
    if changes.get("segment_mismatch"):
        metadata["segments"][0]["flags"] = 4
    if changes.get("truncated"):
        raw = raw[:0x4100]
    folder = args.output / name
    folder.mkdir(exist_ok=True)
    (folder / "module.elf").write_bytes(raw)
    (folder / "module.image").write_bytes(image)
    (folder / "module.json").write_text(json.dumps(metadata))
    return dict(
        name=name,
        elf=str(folder / "module.elf"),
        image=str(folder / "module.image"),
        metadata=str(folder / "module.json"),
        canonical_base=changes.get("base", "0x50000000"),
    )


def check(condition, message):
    global passed
    if not condition:
        raise AssertionError(message)
    passed += 1


valid = make("valid")
r = tool.derive([valid], identities)
check(r["noreturn_targets"] == ["0x50001040"], "wrong valid target")
check(
    r["targets"][0]["got_slot_rva"] == "0x2010"
    and r["targets"][0]["symbol"] == "__stack_chk_fail",
    "missing relocation/symbol provenance",
)
check(r["lifter_arguments"] == ["--noreturn", "0x50001040"], "wrong lifter flags")
shift = tool.derive([make("shifted", {"shift": 0x5000})], identities)
check(
    shift["noreturn_targets"] == ["0x50006040"], "derivation assumed a fixed game RVA"
)
for name, changes in [
    ("wrong-image", {"image_mismatch": True}),
    ("wrong-metadata", {"metadata_mismatch": True}),
    ("wrong-segments", {"segment_mismatch": True}),
    ("wrong-machine", {"machine": 183}),
    ("wrong-reloc", {"reloc_type": 6}),
    ("defined-symbol", {"defined": 1}),
    ("nonfunction-symbol", {"symbol_info": 0x11}),
    ("missing-tag", {"missing_tag": True}),
    ("duplicate-reloc", {"duplicate_reloc": True}),
    ("zero-got", {"got": 0}),
    ("data-thunk", {"got": 0x2016}),
    ("wrong-jmp", {"opcode": b"\x90\x90"}),
    ("wrong-displacement", {"bad_displacement": 8}),
    ("wrong-ordinal", {"ordinal": 2}),
    ("nonexec-resolver", {"resolver": 0x2010}),
    ("ambiguous-mapping", {"overlap": True}),
    ("truncated-elf", {"truncated": True}),
    ("canonical-overflow", {"base": hex((1 << 64) - 1)}),
]:
    rejected = False
    try:
        tool.derive([make(name, changes)], identities)
    except (ValueError, struct.error):
        rejected = True
    check(rejected, name + " was not rejected")
rejected = False
try:
    tool.derive([valid, copy.deepcopy(valid)], identities)
except ValueError:
    rejected = True
check(rejected, "ambiguous canonical target accepted")
print(json.dumps({"passed": passed, "failed": 0, "synthetic_only": True}))
