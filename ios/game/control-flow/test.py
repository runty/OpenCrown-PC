#!/usr/bin/env python3
"""Owned x86-64 synthetic fixtures for conservative switch-atlas acceptance/rejection."""

from pathlib import Path
import argparse, subprocess, json, struct, hashlib, sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--output", type=Path, required=True)
p.add_argument("--xed-root", type=Path, required=True)
p.add_argument("--llvm-bin", type=Path, required=True)
p.add_argument("--cxx", default="clang++")
a = p.parse_args()
out = a.output.resolve()
if (
    len(out.parts) < 3
    or out.parts[1] != "Volumes"
    or not Path("/Volumes", out.parts[2]).is_mount()
):
    p.error("Test output must be on a mounted external disk")
out.mkdir(parents=True, exist_ok=True)
source = Path(__file__).resolve().parent


def run(cmd, name):
    with (out / name).open("w") as f:
        subprocess.run(
            list(map(str, cmd)), stdout=f, stderr=subprocess.STDOUT, check=True
        )


def function(name, pre, body="", badcase="", tableoverride=None):
    start = f"fixture_{name}"
    labels = [start + "_case" + str(i) for i in range(3)]
    default = start + "_default"
    table = start + "_table"
    lines = ([".Lnormal_target:"] if name == "normal" else []) + [
        f".global {start}",
        f".global {start}_end",
        f"{start}:",
    ]
    pre = (
        pre.replace("@DEFAULT", default)
        .replace("@TABLE", table)
        .replace("@LOAD", start + "_load")
    )
    lines.extend(pre.splitlines())
    lines.extend(
        [
            f"{start}_load:",
            "movsxd rdx, dword ptr [rcx+rax*4]",
            "add rdx, rcx",
            "jmp rdx",
        ]
    )
    for i, label in enumerate(labels):
        lines.extend(
            [
                label + ":",
                badcase if i == 0 and badcase else f"mov eax, {0x11223344 + i}",
                "ret",
            ]
        )
    lines.extend([default + ":", "xor eax, eax", "ret", start + "_end:"])
    entries = tableoverride or labels
    data = [".balign 4", table + ":"] + [f".long {entry}-{table}" for entry in entries]
    return lines, data


fixtures = {
    "narrow8": ("movzx eax,bl\ncmp al,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]", True, ""),
    "narrow8_copy": (
        "movzx eax,bl\ncmp bl,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        True,
        "",
    ),
    "narrow8_schedule": (
        "add bl,0x7d\nmovzx eax,bl\ncmp al,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]\nmov r14b,1",
        True,
        "",
    ),
    "narrow16_copy": (
        "movzx eax,si\ncmp si,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        True,
        "",
    ),
    "narrow8_regcopy32": (
        "movzx ecx,bl\nmov eax,ecx\ncmp al,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        True,
        "",
    ),
    "narrow8_regcopy64": (
        "movzx ecx,bl\nmov rax,rcx\ncmp al,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        True,
        "",
    ),
    "narrow8_memory": (
        "movzx eax,byte ptr [rdi]\ncmp al,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        True,
        "",
    ),
    "bad_upper8": (
        "mov eax,0x100\nmov al,bl\ncmp al,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        False,
        "",
    ),
    "bad_upper16": (
        "mov eax,0x10000\nmov ax,si\ncmp ax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        False,
        "",
    ),
    "stale_movzx8": (
        "movzx eax,bl\nadd eax,0x100\ncmp al,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        False,
        "",
    ),
    "stale_movzx16": (
        "movzx eax,si\nmov eax,edi\ncmp ax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        False,
        "",
    ),
    "clobber_narrow_source": (
        "movzx eax,bl\ninc bl\ncmp bl,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        False,
        "",
    ),
    "narrow_partial_dest": (
        "movzx ax,bl\ncmp al,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        False,
        "",
    ),
    "narrow_source_too_wide": (
        "movzx eax,si\ncmp al,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        False,
        "",
    ),
    "narrow_highbyte": (
        "movzx eax,bh\ncmp ah,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        False,
        "",
    ),
    "narrow_ambiguous16": ("cmp ax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]", False, ""),
    "narrow_call_clobber": (
        "movzx eax,bl\ncall .Lnormal_target\ncmp al,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        False,
        "",
    ),
    "narrow_bypass": (
        "test edi,edi\njnz fixture_narrow_bypass_cmp\nmovzx eax,bl\nfixture_narrow_bypass_cmp:\ncmp al,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        False,
        "",
    ),
    "copy_bypass": (
        "mov eax,0x100\ntest edi,edi\njnz fixture_copy_bypass_guard\nmov eax,esi\nfixture_copy_bypass_guard:\ncmp esi,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        False,
        "",
    ),
    "normal": ("mov eax, edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]", True, ""),
    "copy": ("mov eax, esi\ncmp esi,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]", True, ""),
    "intervening": (
        "mov eax, edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]\nmov rdi,qword ptr [r12+0x78]",
        True,
        "",
    ),
    "wide": ("cmp rax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]", True, ""),
    "clobber_index": (
        "mov eax,edi\ncmp eax,2\nja @DEFAULT\nmov eax,99\nlea rcx,[rip+@TABLE]",
        False,
        "",
    ),
    "clobber_base": (
        "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]\nxor ecx,ecx",
        False,
        "",
    ),
    "ambiguous_upper": ("cmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]", False, ""),
    "clobber_copy": (
        "mov eax,esi\nmov esi,2\ncmp esi,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        False,
        "",
    ),
    "alternate_entry": (
        "test edx,edx\njnz @LOAD\nmov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        False,
        "",
    ),
    "unresolved_cfg": (
        "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        False,
        "jmp r8",
    ),
    "overlap": ("mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]", False, ""),
    "count_cap": (
        "mov eax,edi\ncmp eax,4096\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        False,
        "",
    ),
    "external_tail": (
        "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        False,
        "jmp .Lnormal_target",
    ),
    "guard16": ("movzx eax,si\ncmp ax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]", True, ""),
}
text = [".intel_syntax noprefix", ".text", ".org 0x100"]
tables = []
for name, (pre, accepted, badcase) in fixtures.items():
    override = (
        [f"fixture_{name}_case0", f"fixture_{name}_case0+1", f"fixture_{name}_case2"]
        if name == "overlap"
        else None
    )
    code, data = function(name, pre, badcase=badcase, tableoverride=override)
    text += code
    tables += data
specials = ["table_bypass32", "table_bypass_copy"]
for name in specials:
    fn = "fixture_" + name
    guard = "eax" if name.endswith("32") else "esi"
    initial = "movabs rax,0x100000000" if name.endswith("32") else "mov eax,99"
    text += [
        f".global {fn}",
        f".global {fn}_end",
        fn + ":",
        initial,
        "mov ecx,edi",
        "cmp rcx,1",
        f"ja {fn}_default",
        f"lea rdx,[rip+{fn}_table0]",
        "movsxd r8,dword ptr [rdx+rcx*4]",
        "add r8,rdx",
        "jmp r8",
        fn + "_define:",
        "mov eax,esi",
        fn + "_cmp:",
        f"cmp {guard},2",
        f"ja {fn}_default",
        f"lea rcx,[rip+{fn}_table1]",
        "movsxd rdx,dword ptr [rcx+rax*4]",
        "add rdx,rcx",
        "jmp rdx",
    ]
    for i in range(3):
        text += [fn + f"_case{i}:", f"mov eax,{i + 1}", "ret"]
    text += [fn + "_default:", "xor eax,eax", "ret", fn + "_end:"]
    tables += [
        ".balign 4",
        fn + "_table0:",
        f".long {fn}_cmp-{fn}_table0",
        f".long {fn}_define-{fn}_table0",
        ".balign 4",
        fn + "_table1:",
    ] + [f".long {fn}_case{i}-{fn}_table1" for i in range(3)]

text += [
    ".global fixture_nested",
    ".global fixture_nested_end",
    "fixture_nested:",
    "mov eax,edi",
    "cmp eax,1",
    "ja fixture_nested_default",
    "lea rcx,[rip+fixture_nested_table]",
    "movsxd rdx,dword ptr [rcx+rax*4]",
    "add rdx,rcx",
    "jmp rdx",
    "fixture_nested_case0:",
    "mov eax,esi",
    "cmp eax,1",
    "ja fixture_nested_default",
    "lea rcx,[rip+fixture_nested_table2]",
    "movsxd rdx,dword ptr [rcx+rax*4]",
    "add rdx,rcx",
    "jmp rdx",
    "fixture_nested_case1:",
    "mov eax,5",
    "ret",
    "fixture_nested_case2:",
    "mov eax,6",
    "ret",
    "fixture_nested_case3:",
    "mov eax,7",
    "ret",
    "fixture_nested_default:",
    "xor eax,eax",
    "ret",
    "fixture_nested_end:",
]
tables += [
    ".balign 4",
    "fixture_nested_table:",
    ".long fixture_nested_case0-fixture_nested_table",
    ".long fixture_nested_case1-fixture_nested_table",
    "fixture_nested_table2:",
    ".long fixture_nested_case2-fixture_nested_table2",
    ".long fixture_nested_case3-fixture_nested_table2",
]
(out / "fixture.s").write_text("\n".join(text + tables) + "\n")
run(
    [
        a.cxx,
        "-target",
        "x86_64-unknown-linux-gnu",
        "-c",
        out / "fixture.s",
        "-o",
        out / "fixture.o",
    ],
    "assemble.log",
)
run(
    [a.llvm_bin / "llvm-readobj", "--relocations", out / "fixture.o"], "relocations.txt"
)
assert "R_X86_64" not in (out / "relocations.txt").read_text()
run(
    [
        a.llvm_bin / "llvm-objcopy",
        "-O",
        "binary",
        "--only-section=.text",
        out / "fixture.o",
        out / "fixture.image",
    ],
    "objcopy.log",
)
run([a.llvm_bin / "llvm-nm", "--format=posix", out / "fixture.o"], "symbols.txt")
symbols = {}
for line in (out / "symbols.txt").read_text().splitlines():
    fields = line.split()
    if len(fields) >= 3:
        symbols[fields[0]] = int(fields[2], 16)
image = (out / "fixture.image").read_bytes()
header = bytearray(4096)
ident = bytes([127]) + b"ELF" + bytes([2, 1, 1]) + bytes(9)
struct.pack_into(
    "<16sHHIQQQIHHHHHH", header, 0, ident, 2, 62, 1, 0, 64, 0, 0, 64, 56, 1, 0, 0, 0
)
struct.pack_into(
    "<IIQQQQQQ", header, 64, 1, 5, 4096, 0, 0, len(image), len(image), 4096
)
(out / "fixture.elf").write_bytes(header + image)
base = 0x400000
rows = []
for name in list(fixtures) + specials + ["nested"]:
    rows.append(
        {
            "module": "fixture",
            "canonical_start": base + symbols["fixture_" + name],
            "canonical_end": base + symbols["fixture_" + name + "_end"],
            "fde_va": None,
        }
    )
(out / "entries.json").write_text(json.dumps(rows, indent=2) + "\n")
run(
    [
        sys.executable,
        source / "catalog.py",
        "--image",
        out / "fixture.image",
        "--elf",
        out / "fixture.elf",
        "--atlas",
        out / "entries.json",
        "--module",
        "fixture",
        "--base",
        hex(base),
        "--passes",
        "3",
        "--xed-root",
        a.xed_root,
        "--cxx",
        a.cxx,
        "--output",
        out / "catalog",
    ],
    "catalog-test.log",
)
accepted = json.loads((out / "catalog/accepted-tables.json").read_text())
actual = {s["owner"] for s in accepted}
expected = {symbols["fixture_" + n] for n, (_, ok, _) in fixtures.items() if ok} | {
    symbols["fixture_nested"]
}
assert actual == expected, {"actual": actual, "expected": expected}
assert len(accepted) == len(expected) + 1
partial = json.loads((out / "catalog/incomplete-owner-tables.json").read_text())
partial_owners = {t["owner"] for t in partial}
assert {
    symbols["fixture_external_tail"],
    symbols["fixture_unresolved_cfg"],
} <= partial_owners
assert all(
    not t["owner_cfg"]["complete"]
    and t["owner_cfg"]["decode_or_reachability_failures"] == 0
    and t["owner_cfg"]["instruction_overlaps"] == 0
    for t in partial
)
assert all(
    len(t["owner_cfg"]["unresolved_indirect_pcs"])
    == t["owner_cfg"]["unresolved_indirect_branches"]
    and len(t["owner_cfg"]["external_direct_edges"])
    == t["owner_cfg"]["external_direct_branches"]
    for t in partial
)
manifest = json.loads((out / "catalog/provenance.json").read_text())
assert len(manifest["passes"]) >= 2

# Read-only artifact boundaries fail explicitly before scanner execution.
cli = [
    sys.executable,
    source / "catalog.py",
    "--image",
    out / "fixture.image",
    "--elf",
    out / "fixture.elf",
    "--atlas",
    out / "entries.json",
    "--module",
    "fixture",
    "--base",
    hex(base),
    "--passes",
    "1",
    "--xed-root",
    a.xed_root,
    "--cxx",
    a.cxx,
]


def rejection(name, replace, expect):
    command = list(map(str, cli))
    for key, value in replace.items():
        command[command.index(key) + 1] = str(value)
    command += ["--output", str(out / name)]
    result = subprocess.run(
        command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
    )
    (out / (name + ".log")).write_text(result.stdout)
    assert result.returncode and expect in result.stdout, (
        name,
        result.returncode,
        result.stdout,
    )


changed = bytearray(image)
changed[-1] ^= 1
(out / "mismatch.image").write_bytes(changed)
rejection(
    "reject-image-mismatch",
    {"--image": out / "mismatch.image"},
    "Prepared image differs",
)
overlap = list(rows) + [dict(rows[0])]
(out / "overlap-entries.json").write_text(json.dumps(overlap))
rejection(
    "reject-atlas-overlap",
    {"--atlas": out / "overlap-entries.json"},
    "Atlas owners overlap",
)
bad = bytearray(header + image)
struct.pack_into("<Q", bad, 64 + 32, len(image) + 4096)
(out / "bad-bounds.elf").write_bytes(bad)
rejection(
    "reject-elf-bounds", {"--elf": out / "bad-bounds.elf"}, "Invalid ELF load bounds"
)

results = {
    "owned_functions": len(rows),
    "accepted_owners": len(actual),
    "accepted_tables": len(accepted),
    "rejected_owners": len(rows) - len(actual),
    "checks": len(rows) + 9,
    "incomplete_owner_candidates": len(partial_owners),
    "failures": 0,
    "nested_passes": len(manifest["passes"]),
    "accepted_names": sorted(n for n in fixtures if fixtures[n][1]) + ["nested"],
    "rejected_names": sorted(n for n in fixtures if not fixtures[n][1]) + specials,
    "outputs": {
        f.name: hashlib.sha256(f.read_bytes()).hexdigest()
        for f in [
            out / "fixture.s",
            out / "fixture.image",
            out / "fixture.elf",
            out / "entries.json",
        ]
    },
}
(out / "results.json").write_text(json.dumps(results, indent=2) + "\n")
print(json.dumps(results, indent=2))
