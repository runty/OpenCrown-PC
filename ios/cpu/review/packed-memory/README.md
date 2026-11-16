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

## Fixed game leaf against the current runtime

`leaf-run.py` checks the exact SHA-256 of the game trace, image and earlier
239-group lowering before using them as read-only inputs. It compiles the
original and lowered LLVM with the current runtime and runs the earlier DSP
fixture, adapted to supply a clear signal cache. Four- and eight-lane writes
and four-lane reads still use the scalar bridges; only eight-lane reads can
use the experimental grouped helper. The fixture checks aliases, special float
bits, canaries, State, return ABI and a throwing fault. Its outer-lock modes
are research controls, not proposed game behavior. Each fixture uses independent
buffers; equivalence does not establish behavior under concurrent guest writes.

Example (outputs must be on a mounted external disk):

```
python3 ios/cpu/review/packed-memory/leaf-run.py \
  --game /Volumes/office/Build/OpenCrown/ios-game \
  --backend /Volumes/office/Build/OpenCrown/cpu-backends \
  --output /Volumes/office/Build/OpenCrown/mobile-adventure-live/packed-memory-audit/leaf-current/repro
```

Native, ASan/UBSan and TSan builds each passed 36 cases and 212,078 checks,
with LLVM objects compiled under the relevant sanitizer flags. A separate
instrumented fixture reported 22,296 grouped eight-lane reads and no fallbacks.
In three native 2,000-call repetitions per worker, independent-buffer throughput
was 36,218 vs 54,299 calls/sec for one worker and 45,543 vs 89,764 for two,
original vs grouped. The benchmark does not represent the whole game, and
other Mac workloads make these measurements noisy. The older game experiment
on the previous global-lock runtime found no settled gameplay improvement.
Further correctness review and a matched whole-game comparison are required.
