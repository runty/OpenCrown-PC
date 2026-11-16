# Experimental scale-loop study

This isolated study targets the pinned game routine at canonical `0x20619220`.
The original 57 bytes from `eboot.image` hash to
`4bc6f9fed2b1f9e325415296cc7bbadf3056c45cdec94da6998507d6f0bcda3f`.
Its scalar multiply instruction at `0x20619240` was identified in the prior
phone profile. `prototype.cpp` is a research implementation; it is **not**
installed, wired into the game, or qualified as a replacement. It calls the
same checked scalar memory bridges but omits the normal trace-entry wrapper.

The exact original bytes were executed on an AMD Ryzen 7 7840HS Zen 4 host;
the actual game-generated ARM object and the prototype were executed on Mac.
For 32,778 selected cases (including 16 operand classes, 0–31 elements and
four rounding modes), both matched the AMD result memory, selected GPRs,
defined arithmetic flags and XMM/YMM lanes. The fixture does not compare
MXCSR status or every register. One early prototype mismatched 3,849 cases
because it copied upper XMM lanes from the wrong source; the corrected version
matches this matrix. Captured AMD identity is not hardware attestation, and
Zen 4 is not the PS4 Jaguar.

A separate 14-case comparison checks the complete translated State and guest
memory against the actual ARM object on normal returns, two overlapping-buffer
cases, first/later out-of-bounds read/write, invalid return stack and five
cancellation points. All match, including an ASan/UBSan run instrumenting the
fixture, prototype and memory runtime; the original game object itself is not
sanitizer-instrumented. An earlier prototype left incorrect fault RIP and
parity; its negative output is retained. These finite cases do not establish
all fault timing, aliasing, concurrency, trace-observer, floating-environment,
MXCSR, extremely large count or game behavior.

An alternating six-pair Mac microbenchmark of 5,000 calls per sample used
synthetic checked memory. Median baseline/prototype nanoseconds per call were
30.87/28.27 (1 element), 72.85/63.74 (4), 216.73/212.18 (16), 816.05/803.59
(64), and 3092.68/3140.28 (256). Other host workloads were present. These are
small, variable local samples, not device, real-game or sustained performance;
they do not justify promotion. In particular, the earlier faster incomplete
prototype omitted correct fault-state bookkeeping and is not a valid baseline.

The source files are standalone fixtures. Compile `lifted.cpp` with the real
candidate `memory.o` and `20619220.o`; add `prototype.cpp` and define
`DCP_SCALE_PROTOTYPE` for the prototype. `compare-amd.py` extracts and verifies
the original code bytes, executes them on an authorized x86 Linux reference,
and compares the saved output matrices. `fullstate.cpp` and `bench.cpp` use the
same two real objects. Compiler/target flags and object paths, hashes, raw negative controls, machine
identity and outputs are retained under
`/Volumes/office/Build/OpenCrown/mobile-adventure-live/sse-nan-candidate/scale-loop-study`.

## Isolated batched-memory experiment

`batch-runtime.cpp` is a separate private implementation that includes the
runtime to access the existing stripe locks and snapshot invalidation. For a
validated span of up to 4,096 elements with an already-clear signal cache, it
locks both ranges as a unit and preserves ordered scalar operations. It falls
back to the original trace for a missing/stale signal cache, oversized or
invalid spans. It drops locks before any slow cancellation callback and then
reacquires them. `batch-wrapper.ll` and the renamed original object preserve
one normal trace-entry callback and retain the original as fallback. None of
this is wired into the shipping builder.

The first batch prototype called cancellation callbacks while holding a stripe;
a reentrant guest read caused the child process to be killed (signal 9). That
unsafe version and the negative result are retained, not used. The revised
fixture matched the AMD exact-byte matrix above (32,778 selected cases), and
19 full translated-State/memory cases covering selected faults, cancellation,
input/output overlap, return-slot overlap, zero-length and fallback. A
controlled signal-generation change verifies a reentrant callback runs after
lock release. A separate two-thread fixture confirms an existing guest LOCK
excludes the batch and that changed texture snapshots invalidate. ASan/UBSan
and TSan passes cover the respective fixtures and instrumented batch runtime;
the original translated object remains uninstrumented. Both iOS SDKs pass
syntax checks. `run-batch-host.sh` and `run-batch-sanitizers.sh` reproduce the
bounded host tests with explicitly supplied paths and reference host.

The registered-memory Mac microbenchmark with a clear signal cache measured
median original/batch times of 1,094.5/668.3 ns for 96 elements and
3,018.8/1,734.9 ns for 256 across six alternating pairs. This is an uncontended
host-only microbenchmark, not game or device performance. Whole-span locking
changes which concurrent interleavings are possible and may affect scheduling;
full guest atomic semantics, other instruction observers, full FP/MXCSR
behavior, long pauses/cancellation, varied aliases, contention and heavy combat
remain insufficiently qualified. The previous call-distribution audit suggests
this one loop is not enough evidence of material overall headroom. Do not
promote or install it based on these numbers.

A private instrumented Mac build booted to Town: 20,744 fast calls, no
fallbacks, 25,920 full-content texture checks and an unchanged disposable save.
A separately built iPhone Simulator QA app reached the first Ruins/GOBLIN
encounter in 7,068 frames, recording 82,432 fast calls, no fallbacks, 19,420,672
processed elements, 142,928 verified texture checks and zero proof failures.
Its disposable save hash remained `728f0ecb5c92c57a833b731b9b2943bc059d2abd2383a043a280e6c8cf937bac`;
tracked files and diagnostic input were preserved and the original Simulator
app was reinstalled. Audio was muted, and the result is not a device speed
measurement. Independent archive inventories verify that each private Mac or
Simulator archive differs from its respective original only in `memory.o` and
`20619220.o`, plus the added renamed original fallback object. The QA app's
build ID and manifest explicitly mark it experimental.

The initial Simulator attempt showed the Ruins visually but missed the GOBLIN
checkpoint after its timed walking input ended. It is retained as incomplete;
the retry used the longer ordinary-input script. The QA-only app diagnostics
patch is `batch-app-qa.patch` (`git apply --unidiff-zero`); the machine-specific
retry driver and its input file are `simulator-batch-ruins.py` and
`batch-ruins-extended-input.txt`. Inspect the driver before reuse: it installs
a test app on the dedicated Simulator, copies a disposable save and restores
the original app/input. Evidence and hashes are in
`/Volumes/office/Build/OpenCrown/mobile-adventure-live/sse-nan-candidate/scale-batch-study/result.json`
and the sibling `scale-batch-game`, `scale-batch-simulator`,
`simulator-batch-ruins` and `simulator-batch-ruins-retry` directories.
No physical device, normal save, or skipped manual save test was used.

The Mac audit was compiled before adding three read-only QA statistics getters
used by the Simulator frontend. Rebuilding from the source with those getters
removed produced a different full object hash, although the `__text` bytes and
1,382 normalized text-relocation rows matched the audited object. This is not
full-object identity; both hashes and the distinction are recorded in
`result.json`.

## Sixteen-element chunk and contention study

`batch-chunk-runtime.cpp` is a further isolated experiment. Instead of holding
both full-buffer stripes for the entire loop, it locks corresponding source and
destination chunks, defaulting to 16 elements, and releases at each boundary.
It retains the signal-cache gate, original fallback and unlock-before-slow-poll
behavior. This still changes allowed interleavings relative to the original
per-scalar locking; the size is a research choice, not a production setting.

`contention.cpp` runs a deliberately aggressive synthetic observer alongside
20,000 256-element worker calls. The observer repeatedly reads the destination
and takes the global guest atomic lock every 32 reads. In four alternating pairs
on a busy Mac, median observed reader p99 was approximately 1.0 µs for the
original versus 333.4 µs for the earlier whole-buffer batch. In a separate
four-pair run, the original/16-element chunk medians were about 1.0/4.6 µs;
32- and 64-element chunks had higher observed tails. The worker and observer
rates/sample counts differ greatly between variants; these are stressed host
observations, not statistically controlled phone latency, general fairness or
an audio-underrun prediction. The whole-buffer result is a negative reason not
to promote that version, and the shorter version still needs contention and
interleaving qualification.

The 16-element variant matches the selected 32,778-case AMD matrix and 19
full-State/memory cases from the previous audit. It also passes 18 cases at
chunk/256 KiB stripe boundaries with disjoint and overlapping buffers, and ten
reentrant signal-change cases at either side of chunk boundaries. Relevant
ASan/UBSan and TSan fixtures pass; the original translated object remains
uninstrumented. iPhoneOS and Simulator SDK syntax checks pass. An uncontended
registered-memory Mac microbenchmark measured median original/chunk calls of
856.4/579.5 ns for 96 elements and 2,271.7/1,525.0 ns for 256 elements across
six alternating pairs. This is not an overall-game or device speedup.

A private Mac Town audit used the chunk fast path 20,784 times, passed 24,469
full-content texture checks and retained the disposable save. A separate muted
Simulator QA build reached the first Ruins/GOBLIN encounter in 7,095 frames,
with 82,312 fast calls, no fallback, 141,516 verified texture checks, zero proof
failures and the unchanged Darrel save. Its original Simulator app, tracked
files and diagnostic input were restored. Archive member hashes show only
`memory.o` differs from the prior private batch archives. No physical device or
skipped manual save check was used.

Run `run-chunk-host.sh` and `run-chunk-sanitizers.sh` with the same arguments as
the prior batch drivers. `archive-diff.py` compares BSD ar member contents;
`simulator-chunk16-ruins.py` is the machine-specific QA driver and must be
inspected before reuse. Evidence is under the sibling `scale-contention-study`,
`scale-chunk-game`, `scale-chunk-simulator` and `simulator-chunk16-ruins`
directories; the summary is `scale-contention-study/result.json`. Full game
performance, long-run contention, FP/status, observer coverage, all races and
physical-device behavior remain open. This is not enabled in a shipping app.
