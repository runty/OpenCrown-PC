# Import-derived termination boundaries

This source contains no game addresses or literal game NIDs. It derives a private
static-analysis target map from original ELF bytes, module-tool JSON and the original
loader image. The only semantic policy is the ABI name `__stack_chk_fail`.

Build its name-to-NID helper using the checkout's actual `loader/nid.cpp`, then
run synthetic structural tests:

```sh
./build-and-test.sh CHECKOUT EXTERNAL_TOOL_OUTPUT
python3 derive_noreturn.py --modules PRIVATE_MODULE_SPEC.json \
  --nid-oracle EXTERNAL_TOOL_OUTPUT/nid-oracle --output PRIVATE_DERIVED.json
```

A private module specification is a JSON array containing `name`, `elf`, `image`,
`metadata` and `canonical_base` for every supplied module. Paths and canonical
bases come from the build, not from this source. Supplying all modules permits
rejection of a matching definition that would change binder precedence.

Validation independently parses ELF64 little-endian x86-64 program headers and
SCE dynamic string/symbol/RELA/JMPREL tables. Module-tool symbol, relocation and
segment metadata must agree with those raw tables. Reconstructed segment bytes,
zero gaps and BSS must match the supplied original image exactly. The supported
loader model rounds its image to the PS4's 16 KiB guest page boundary.

For each reviewed external function identity, require an undefined function
symbol and an unambiguous zero-addend JUMP_SLOT relocation. Read its original GOT
word. That word must identify the lazy branch at stub+6. Validate the complete
file-backed executable stub: FF25 RIP-relative indirect jump back to precisely
the relocation's GOT slot, the matching pushed relocation ordinal, and a direct
branch to a file-backed executable resolver. Emit the computed canonical stub
address plus byte/relocation/symbol/file-offset/hash provenance. Do not round
addresses to an arbitrary function alignment.

Unsupported opcode shapes, defined symbols, duplicate/ambiguous slots or
canonical targets, missing/duplicate dynamic tags, malformed bounds, mismatched
JSON/images and address overflow reject before output is written. A non-lazy or
differently shaped PLT needs a separately reviewed decoder path; this prototype
does not guess or silently scan for a likely address.

Output includes `noreturn_targets`, `lifter_arguments`, and detailed `targets`.
Its provenance remains private build output. Bootstrap derives the map from all
prepared modules, keeps a content-addressed snapshot under `noreturn-maps/`, and
passes matching per-module declarations to the switch scanner. Scanner provenance
binds the snapshot and original ELF/image hashes; conditional scans verify the
same policy snapshot. `lifter_arguments` is the legacy output key for the
scanner's `--noreturn` flags, not an instruction to change Remill IR. This policy
controls only static traversal and prefetch: existing translated IR is not
relabeled or regenerated when its used semantics have not changed.
The ELF import tables do not themselves encode a noreturn attribute. The semantic
registry and the runtime's typed fatal binding must agree. Do not infer arbitrary
noreturn calls just because an unknown HLE currently throws.

The 23 synthetic checks cover valid and relocated layouts, emitted arguments and
provenance; wrong image/metadata/segment identity; unsupported machine,
relocation/symbol kind and defined identity; missing tags; duplicate relocations;
invalid GOT targets, opcodes, displacements and ordinals; non-executable targets;
ambiguous mappings, truncated files, canonical overflow and duplicate modules.
The real pinned modules independently pass this same parser, producing the two
private import-thunk addresses recorded in the review's derived JSON.

The runtime has an explicit typed `__stack_chk_fail` handler that throws a fatal
game diagnostic and returns no success value. Its two behavioral checks run in
the native/ASan headless service suite and compile for all SDKs. This is a
**noreturn** declaration; it does not imply **nounwind**. The host exception may
unwind translated/native frames to the coordinator for cleanup and diagnostics.
The original external kernel implementation is absent from the supplied modules;
semantic identity comes from the ABI policy and matching fatal binding.
