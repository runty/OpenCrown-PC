# Packed-memory exploratory study

This is an offline research fixture, not a game runtime change. A phone profile's
selected hot sites include 32-byte VMOVUPS loads and stores that are lifted into
eight separately locked scalar accesses. The profile reports only selected sites,
so its sample counts are not a complete breakdown of game CPU time.

The read-only prototype groups a valid 32-byte read under one lock and falls back
to scalar accesses for invalid spans or a changed cancellation generation. The
isolated lifted-instruction comparison passed 10,000 random state/data cases and
four partial-fault cases, including native, ASan/UBSan and TSan runs (the lifted
LLVM objects themselves were not sanitizer-instrumented). A separate native x86
VMOVUPS check passed random, page-boundary and guard-page cases. The runtime
fixture also checks a stale cancellation cache and concurrent bulk writer/reader.

A tiny single-address Mac benchmark showed lower grouped-read overhead. It does
not establish an end-to-end or device speedup. Grouping can change concurrent
interleavings and cancellation/fault behavior needs further examination. Do not
ship or use this study as evidence of full semantic equivalence.

An earlier uncommitted vector-memory lowerer exists in the protected original
checkout. It was only inspected, not modified or incorporated. Review that
prior work and its results before implementing a general lowering pass.
Evidence from this study is under `/Volumes/office/Build/OpenCrown/mobile-adventure-live/packed-memory-audit/`.
