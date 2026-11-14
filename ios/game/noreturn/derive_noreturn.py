#!/usr/bin/env python3
"""Derive reviewed external noreturn import thunks from original SCE ELF bytes."""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

SEMANTIC_NORETURN = frozenset({"__stack_chk_fail"})
PT_LOAD, PT_RELRO, PT_DYNAMIC, PT_DYNLIB = 1, 0x61000010, 2, 0x61000000
STR, STRSZ, SYM, SYMSZ = 0x61000035, 0x61000037, 0x61000039, 0x6100003F
JMP, JMPSZ, RELA, RELASZ = 0x61000029, 0x6100002D, 0x6100002F, 0x61000031
UINT64 = (1 << 64) - 1


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def read_oracle(path):
    # Compile nid-oracle.cpp with this checkout's real loader/nid.cpp first.
    output = subprocess.check_output([str(path)], text=True)
    identities = {}
    for line in output.splitlines():
        fields = line.split()
        require(len(fields) == 2, "invalid NID oracle record")
        name, nid = fields
        require(
            name in SEMANTIC_NORETURN
            and nid not in identities
            and name not in identities.values(),
            "unsupported or ambiguous NID semantic identity",
        )
        require(
            len(nid) == 11 and all(c.isalnum() or c in "+-" for c in nid),
            "invalid NID oracle value",
        )
        identities[nid] = name
    require(set(identities.values()) == SEMANTIC_NORETURN, "incomplete NID oracle")
    return identities


class Module:
    def __init__(self, spec, identities):
        self.spec = spec
        self.name = spec["name"]
        self.base = (
            int(spec["canonical_base"], 0)
            if isinstance(spec["canonical_base"], str)
            else spec["canonical_base"]
        )
        require(
            isinstance(self.base, int) and 0 <= self.base <= UINT64,
            "invalid canonical base",
        )
        self.elf = Path(spec["elf"]).read_bytes()
        self.image = Path(spec["image"]).read_bytes()
        self.metadata = json.loads(Path(spec["metadata"]).read_text())
        data = self.elf
        require(
            len(data) >= 64 and data[:6] == b"\x7fELF\x02\x01",
            "unsupported or truncated ELF identity",
        )
        require(self.unpack("<H", 18)[0] == 62, "ELF is not x86-64")
        phoff = self.unpack("<Q", 32)[0]
        phent, count = self.unpack("<HH", 54)
        require(
            phent >= 56
            and count <= 1024
            and phoff <= len(data)
            and count * phent <= len(data) - phoff,
            "invalid program header bounds",
        )
        self.ph = [self.unpack("<IIQQQQQQ", phoff + i * phent) for i in range(count)]
        self.loads = [p for p in self.ph if p[0] in (PT_LOAD, PT_RELRO)]
        require(self.loads, "no loadable image segments")
        segments = []
        image_size = 0
        for p in self.loads:
            require(
                p[5] <= p[6] and p[2] <= len(data) and p[5] <= len(data) - p[2],
                "segment outside ELF",
            )
            require(
                p[3] <= UINT64 - p[6] and p[3] + p[6] <= UINT64 - 0x3FFF,
                "segment address overflow",
            )
            image_size = max(image_size, (p[3] + p[6] + 0x3FFF) & ~0x3FFF)
            segments.append(
                dict(offset=p[2], vaddr=p[3], filesz=p[5], memsz=p[6], flags=p[1])
            )
        require(
            self.metadata.get("segments") == segments,
            "module-tool segment metadata does not match ELF",
        )
        require(
            image_size == self.metadata.get("image_size") == len(self.image),
            "module image size mismatch",
        )
        # Reconstruct the loader image, including zero-filled BSS and gaps.
        expected = bytearray(len(self.image))
        for p in self.loads:
            expected[p[3] : p[3] + p[5]] = data[p[2] : p[2] + p[5]]
        require(
            expected == self.image,
            "module image bytes do not match original ELF segments/BSS",
        )
        dynamic = [p for p in self.ph if p[0] == PT_DYNAMIC]
        dynlib = [p for p in self.ph if p[0] == PT_DYNLIB]
        require(
            len(dynamic) == len(dynlib) == 1,
            "missing or ambiguous SCE dynamic segments",
        )
        dynamic, dynlib = dynamic[0], dynlib[0]
        require(
            dynamic[5] % 16 == 0
            and dynamic[2] <= len(data)
            and dynamic[5] <= len(data) - dynamic[2],
            "invalid dynamic table bounds",
        )
        self.dynbase = dynlib[2]
        require(
            self.dynbase <= len(data) and dynlib[5] <= len(data) - self.dynbase,
            "dynlib segment outside ELF",
        )
        self.dynsize = dynlib[5]
        self.tags = {}
        for at in range(dynamic[2], dynamic[2] + dynamic[5], 16):
            tag, value = self.unpack("<qQ", at)
            if not tag:
                break
            self.tags.setdefault(tag, []).append(value)
        self.strtab, self.strsize = self.table(STR, STRSZ)
        symtab, symsize = self.table(SYM, SYMSZ)
        require(symsize % 24 == 0, "invalid symbol table length")
        self.symbols = []
        for i, at in enumerate(range(symtab, symtab + symsize, 24)):
            name, info, other, section, value, size = self.unpack("<IBBHQQ", at)
            require(name < self.strsize, "symbol string outside string table")
            end = data.find(b"\0", self.strtab + name, self.strtab + self.strsize)
            require(end >= 0, "unterminated symbol name")
            nid = data[self.strtab + name : end].decode("ascii")
            self.symbols.append(
                dict(
                    index=i,
                    nid=nid,
                    defined=bool(section),
                    value=value,
                    info=info,
                    file_offset=at,
                    name_file_offset=self.strtab + name,
                    raw=data[at : at + 24].hex(),
                )
            )
        meta_symbols = self.metadata.get("symbols", [])
        require(
            len(meta_symbols) == len(self.symbols), "module-tool symbol count mismatch"
        )
        for meta, symbol in zip(meta_symbols, self.symbols):
            require(
                meta.get("nid") == symbol["nid"]
                and bool(meta.get("defined")) == symbol["defined"]
                and meta.get("value") == symbol["value"],
                "module-tool symbol metadata mismatch",
            )
        self.relocations = []
        for tag, size_tag, is_jump in [(RELA, RELASZ, False), (JMP, JMPSZ, True)]:
            if tag not in self.tags and size_tag not in self.tags:
                require(not is_jump, "missing jump relocation table")
                continue
            table, size = self.table(tag, size_tag)
            require(size % 24 == 0, "invalid relocation table length")
            for ordinal, at in enumerate(range(table, table + size, 24)):
                slot, info, addend = self.unpack("<QQq", at)
                sym = info >> 32
                require(sym < len(self.symbols), "relocation symbol out of range")
                self.relocations.append(
                    dict(
                        slot=slot,
                        type=info & 0xFFFFFFFF,
                        symbol=sym,
                        addend=addend,
                        jump=is_jump,
                        ordinal=ordinal,
                        file_offset=at,
                        raw=data[at : at + 24].hex(),
                    )
                )
        require(
            self.metadata.get("relocations")
            == [
                [r["slot"], r["type"], r["symbol"], r["addend"]]
                for r in self.relocations
            ],
            "module-tool relocation metadata mismatch",
        )
        self.identities = identities

    def unpack(self, fmt, offset):
        size = struct.calcsize(fmt)
        require(
            0 <= offset <= len(self.elf) and size <= len(self.elf) - offset,
            "ELF field out of bounds",
        )
        return struct.unpack_from(fmt, self.elf, offset)

    def table(self, offset_tag, size_tag):
        require(
            len(self.tags.get(offset_tag, [])) == len(self.tags.get(size_tag, [])) == 1,
            "missing or ambiguous SCE dynamic table tag",
        )
        offset, size = self.tags[offset_tag][0], self.tags[size_tag][0]
        require(
            offset <= self.dynsize and size <= self.dynsize - offset,
            "SCE table outside dynlib segment",
        )
        return self.dynbase + offset, size

    def offset(self, address, count, executable=False):
        matches = [
            p
            for p in self.loads
            if (not executable or p[1] & 1)
            and p[3] <= address
            and count <= p[5]
            and address - p[3] <= p[5] - count
        ]
        require(
            len(matches) == 1,
            "address has no unique file-backed executable/data mapping",
        )
        return matches[0][2] + address - matches[0][3]

    def derive(self):
        result = []
        for reloc in self.relocations:
            symbol = self.symbols[reloc["symbol"]]
            name = self.identities.get(symbol["nid"].split("#")[0])
            if name not in SEMANTIC_NORETURN:
                continue
            require(
                not symbol["defined"],
                "known semantic symbol is defined rather than an HLE import",
            )
            require(
                reloc["jump"]
                and reloc["type"] == 7
                and reloc["addend"] == 0
                and symbol["info"] & 15 == 2,
                "unsupported noreturn import relocation/symbol type",
            )
            require(
                sum(r["slot"] == reloc["slot"] for r in self.relocations) == 1,
                "ambiguous relocation ownership for GOT slot",
            )
            slot_file = self.offset(reloc["slot"], 8)
            initial = self.unpack("<Q", slot_file)[0]
            require(initial >= 6, "invalid lazy GOT target")
            stub = initial - 6
            stub_file = self.offset(stub, 16, executable=True)
            raw = self.elf[stub_file : stub_file + 16]
            require(raw[:2] == b"\xff\x25", "unsupported PLT indirect jump opcode")
            slot = stub + 6 + struct.unpack_from("<i", raw, 2)[0]
            require(
                slot == reloc["slot"],
                "PLT RIP-relative target does not match relocation GOT slot",
            )
            require(
                raw[6] == 0x68
                and struct.unpack_from("<I", raw, 7)[0] == reloc["ordinal"],
                "invalid lazy resolver relocation ordinal",
            )
            require(raw[11] == 0xE9, "unsupported lazy resolver branch")
            resolver = stub + 16 + struct.unpack_from("<i", raw, 12)[0]
            self.offset(resolver, 1, executable=True)
            require(stub <= UINT64 - self.base, "canonical noreturn address overflow")
            result.append(
                dict(
                    module=self.name,
                    symbol=name,
                    nid=symbol["nid"],
                    canonical=hex(self.base + stub),
                    rva=hex(stub),
                    semantic_policy="ABI __stack_chk_fail terminates; external original implementation not present",
                    stub_file_offset=hex(stub_file),
                    stub_bytes=raw.hex(),
                    got_slot_rva=hex(slot),
                    got_slot_file_offset=hex(slot_file),
                    got_initial_value=hex(initial),
                    lazy_resolver_rva=hex(resolver),
                    jump_relocation_ordinal=reloc["ordinal"],
                    relocation_file_offset=hex(reloc["file_offset"]),
                    relocation_bytes=reloc["raw"],
                    symbol_index=symbol["index"],
                    symbol_file_offset=hex(symbol["file_offset"]),
                    symbol_bytes=symbol["raw"],
                    nid_file_offset=hex(symbol["name_file_offset"]),
                    elf_sha256=digest(self.elf),
                    image_sha256=digest(self.image),
                    metadata_sha256=digest(Path(self.spec["metadata"]).read_bytes()),
                )
            )
        return result


def derive(specs, identities):
    modules = [Module(spec, identities) for spec in specs]
    # A defined matching export could change binding precedence. Refuse to infer
    # HLE semantics when any supplied module supplies such a definition.
    for module in modules:
        for symbol in module.symbols:
            require(
                not (symbol["defined"] and symbol["nid"].split("#")[0] in identities),
                "supplied module defines a reviewed noreturn import identity",
            )
    result = [record for module in modules for record in module.derive()]
    require(
        len({r["canonical"] for r in result}) == len(result),
        "ambiguous canonical noreturn target",
    )
    return dict(
        schema=1,
        semantic_names=sorted(SEMANTIC_NORETURN),
        targets=result,
        noreturn_targets=[r["canonical"] for r in result],
        lifter_arguments=[
            item for r in result for item in ("--noreturn", r["canonical"])
        ],
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--modules",
        type=Path,
        required=True,
        help="Private JSON module specifications: name,elf,image,metadata,canonical_base",
    )
    parser.add_argument("--nid-oracle", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    require(
        len(output.parts) >= 3
        and output.parts[1] == "Volumes"
        and Path("/Volumes", output.parts[2]).is_mount(),
        "output must be on a mounted external disk",
    )
    source_hash = digest(Path(__file__).read_bytes())
    specs = json.loads(args.modules.read_text())
    require(isinstance(specs, list) and specs, "empty module specification")
    inputs = [args.modules.resolve(), args.nid_oracle.resolve()] + [
        Path(spec[key]).resolve()
        for spec in specs
        for key in ("elf", "image", "metadata")
    ]
    require(
        output not in inputs and output != Path(__file__).resolve(),
        "output overlaps an input",
    )
    input_hashes = {str(path): digest(path.read_bytes()) for path in inputs}

    result = derive(specs, read_oracle(args.nid_oracle))
    require(
        input_hashes == {str(path): digest(path.read_bytes()) for path in inputs},
        "input changed during noreturn derivation",
    )
    result["input_sha256"] = input_hashes
    require(
        source_hash == digest(Path(__file__).read_bytes()),
        "source changed during derivation",
    )
    result["source_sha256"] = source_hash
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".partial")
    temporary.write_text(json.dumps(result, indent=2) + "\n")
    temporary.replace(output)
    print(
        json.dumps(
            {
                "noreturn_targets": result["noreturn_targets"],
                "lifter_arguments": result["lifter_arguments"],
            }
        )
    )


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, OSError, struct.error) as error:
        raise SystemExit("derive-noreturn: " + str(error))
