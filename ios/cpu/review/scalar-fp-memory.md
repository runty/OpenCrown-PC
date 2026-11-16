# Scalar floating-point memory widths

The phone scalar diagnostic observes the game's `VMULSS` at ELF `0x619240`
issuing four checked F32 reads, although its source is m32. The unused reads
remain in the compiled execution path. The selector instantiates `MV128`.
`ios/remill-scalar-fp-memory.patch` narrows nine memory selectors:
legacy/VEX MULSS/MULSD/DIVSS/DIVSD, and VMAXSS. Packed and register forms,
arithmetic expressions, lane merge and destination types are unchanged.
ADD/SUB and the other inspected scalar selectors are already narrow.

[Intel's instruction reference](https://cdrdv2-public.intel.com/782151/253667-sdm-vol-2b.pdf)
specifies m32/m64 operands. Accessing bytes beyond those operands is a
correctness issue at an allocation/page boundary as well as extra bridge and
synchronization work.

```sh
python3 ios/cpu/review/scalar-fp-memory.py \
  --backends /Volumes/office/Build/OpenCrown/cpu-backends \
  --output /Volumes/office/Build/OpenCrown/scalar-fp-memory-review
```

Use a matching **unpatched** baseline backend. The test copies BINARY/SSE
sources, applies the patch and builds private semantics and an explicitly bound
lifter. Baseline lifting also explicitly selects its own matching bitcode.
Shared source, semantics and lifter hashes must remain unchanged. A subsequent
`--reuse-backend` validates those hashes and reverses the patch on a scratch copy
to prove that the cached private sources exactly derive from the baseline.
Never use this scratch overlay as a writable shared CMake build directory.

The native x86 oracle executes 24 instruction forms with 256 vectors per form:
four rounding modes, aligned/unaligned operands, finite values, zeros,
subnormals, infinities, quiet/signaling NaNs, different source/destination
patterns and arithmetic-flag seeds. It captures both YMM sources/destinations
and flags. Register, packed and already-narrow controls remain in the matrix.
Twenty real native protected-page end probes succeed; twenty one-byte-overrun
probes fault in child processes with signal handlers, avoiding crash dialogs.

Both baseline and fixed arm64 run the same fixtures under native execution and
ASan/UBSan, with explicit game-style fault unwinding. Typed FP-read wrappers
count each access. Each configuration checks 67,904 conditions, including 160
recorded/throwing boundary probes. The baseline reproduces 18 oversized-access
negative controls; the fixed version eliminates them. Access-count,
boundary, input-mutation, flag and inactive-lane checks have no regressions.
All 442,368 captured result bytes match baseline/fixed exactly.

**The strict native oracle is not passing.** Both baseline and fixed retain
160 identical NaN-selection/payload differences from the native x86 oracle.
Every differing word is a corresponding NaN result; other words and result
classes match. The full failure reports and nonzero exit remain intact.
`width-delta-qualification.json` separately records the width/boundary and
unchanged-result proof; it is not an overall CPU-fidelity or port acceptance.
The NaN differences need a separate architectural review and resolution.
Do not normalize them away or present this as a fully passing native oracle.

The game builder fingerprints BINARY.cpp as well as the existing semantics
sources and runtime bitcode. Missing/changed binary-source identity rejects
old generation provenance, including non-FP80 traces; a new corpus must be
actually regenerated rather than retagged. This change remains an experiment
until real game, package, hardware and sustained acceptance checks pass.
