# Trace counter sharding study

The game currently increments one shared atomic counter at every translated
trace entry. This study asks whether giving workers separate cache-line-aligned
counters reduces contention. `runtime-study.patch` and `counter.h` are an
isolated experiment, not production changes. A session has 128 monotonic slots;
additional acquisitions share a fallback counter. Nested callbacks retain the
current token and restore the previous one. Final totals after joined workers
include every slot and the fallback. A live total is a sum of concurrent atomic
loads, not a linearizable global snapshot. Slot reuse, more complex session
lifetimes, cancellation, and diagnostic semantics would need broader review
before integration.

In normal headless mode the patch retains the exact shared trace-budget path.
For private measurement only, compiling `DCP_QA_TRACE_SHARDS` and setting
`DCP_QA_TRACE_SHARDS=1` enables sharding in checkpoint mode and deliberately
**bypasses the exact headless trace budget**; the host's bounded duration and
external timeout still apply. This override must not be shipped. Interactive
play selects sharding in the research patch, but actual interactive input and
lifecycle have not been qualified with it.

`test.cpp` passes native, ASan/UBSan and TSan with 3.2 million concurrent
increments, a live monotonic-reader check, slot exhaustion and a nested token.
An actual native game audit with an independent per-entry shadow counter
reached inspected Town, matched all 100,129,367 trace entries, verified 25,630
texture hits against full contents, and preserved the disposable save. In a
separate headless limit check, both baseline and patched runtime stopped at
entry 101 for a budget of 100; the QA override was off. This does not cover all
possible nested guest callbacks or worker/session lifetimes. Both mobile SDKs
pass syntax checks for the normal and QA forms.

A synthetic Mac counter benchmark with four workers measured median 11.98 ms
for one shared atomic versus 0.92 ms for sharded atomics; at 16 workers it
measured 57.16 versus 1.12 ms. The one-worker result slightly favored the
shared atomic. These are highly contended fixed-count microbenchmarks, not
whole-game timings or a device prediction.

Four alternating native Town runs per variant use the same original 50,464
translated entries, renderer and input; their private archives differ only in
`runtime.o`. The QA sharding override is on in the candidate and off in the
baseline. With texture versions enabled but verification disabled for timing,
all runs reach Town and retain the disposable save. Baseline checkpoint CPU
ranges 13.85–14.44 s, median 14.120 s; sharded ranges 12.84–15.40 s, median
14.219 s (0.70% higher). The Mac was running other work, the samples overlap,
and this small host proxy neither proves nor rules out an iPhone benefit. No
prototype was installed or promoted.

`build-study.py NEW_EXTERNAL_OUTPUT` reproduces the private archives and hosts
from the audited `bb540ac` core, requiring the existing recorded local build
tree. It verifies the runtime preimage and confirms the archives differ only
in `runtime.o`. The test binaries and game runs can be repeated with `test.cpp`,
`bench.cpp`, and `run-game.py`; inspect their local paths and QA mode first.
The independent rebuild reproduced both research runtime object and host hashes.
Evidence is under `/Volumes/office/Build/OpenCrown/mobile-adventure-live/trace-counter-study/`
and `trace-counter-study-repro/`. No physical device or skipped manual save
test was used.
