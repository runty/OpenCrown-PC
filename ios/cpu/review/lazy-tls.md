# Lazy scalar-thread fallback study

This branch is an experiment, not an app change or a claim of faster phone
play. In the captured `bb540ac` phone binary, disassembly of
`__remill_read_memory_64` resolves `scalarThread` before selecting the already
bound `Memory::scalarThread` pointer. The proposal moves only the lifted
unbound fallback into a noinline helper. In a new iPhoneOS object, the bound
path branches around that helper. Non-lifted host hooks still select the
calling thread's TLS, and unbound lifted calls still use TLS. The existing
contract that a bound `Memory` belongs to its guest worker is unchanged.

The old phone Time Profiler export contains 1,733 samples weighted at 1 ms
whose leaf is `_tlv_get_addr`, 4.23% of its overall sample weight. These are
sample weights, not CPU durations, and frames can be omitted by unwinding;
they do not establish that every sample comes from this particular lookup or
predict a device speedup. `lazy-tls-profile.py` re-reads the retained XML.

`lazy-tls-test.py` inserts a counter only into a private test runtime. Native,
ASan/UBSan and TSan confirm six unbound fallbacks, no fallback for bound lifted
or host calls, and eight independent bound workers. The existing memory suite
passes 267,130 checks per mode; snapshot tests pass native, ASan/UBSan, TSan,
wrap and writer-negative controls. Both mobile SDK runtime objects compile.
This is not exhaustive cross-thread or lifecycle qualification.

Six alternating pairs of a tiny bound native scalar benchmark have median
2M-read CPU 8.103 ms baseline / 8.022 ms candidate and 2M-write CPU 9.335 /
8.299 ms. This is synthetic, with no phone claim. A candidate native Town audit
passes 25,297 full-content texture checks with unchanged disposable save.
In four alternating uninstrumented Town runs, process CPU medians are 13.825 s
baseline / 14.176 s candidate, with different trace counts and competing host
load. The archives have 50,479 members and differ only in `memory.o`; the
baseline object is byte-identical to the audited reference. These few runs do
not establish a whole-game gain or regression. No mobile build was installed.

Evidence and retained inputs: `/Volumes/office/Build/OpenCrown/mobile-adventure-live/lazy-scalar-tls/`.
`lazy-tls-game.py` requires already-built private matched binaries and the prior
disposable reference run. The iPad is with the player; no device was contacted.

## Actual routine and game-path audit

`lazy-tls-leaf.cpp` adapts the prior fixed DSP leaf fixture but binds the memory
context on its owning worker. Both linked entry names contain the original,
inline-flags-lowered game IR; no vector grouping is applied. In six alternating
process pairs, each with three inner repetitions per thread count, original
runtime / candidate median throughput is 37,092 / 39,770 calls/sec with one
worker and 50,458 / 53,881 with two independent-buffer workers. Ranges overlap
and other Mac work runs concurrently. Each process passes its 36 within-run
alias/state cases and canary checks; these are not a cross-process state oracle,
a device test, or evidence of a whole-game gain.

A separately instrumented, deliberately slow native Town run records
2,980,616,903 bound and zero unbound lifted scalar selections over the full
session, including startup. It passes 25,260 full-content texture checks,
with all three disposable-save files unchanged. The atomic counters alter
performance and scheduling, so that run is not a timing sample. The generator
`lazy-tls-instrument.py` reproduces the private counter source byte-for-byte.
The uninstrumented Town results above still show no established gain.

## Private mobile package and Simulator gameplay

`lazy-tls-package.py` builds private iPhoneOS and Simulator archives from the
audited texture-cache reference, verifies every reference library hash, and
records the experimental support revision and exact compile command. Each
50,479-member archive differs from its reference only in `memory.o`; generated
traces and the reference archives remain untouched. Source at `2152b65` built
icon-bearing Simulator and signed iPhoneOS apps. The app manifests explicitly
mark the support override as research-only and not physically qualified.

With a separate muted Simulator QA app and disposable saves, Town reaches
2,176 frames / 24,243 full-content texture checks and the first Ruins encounter
reaches 9,919 frames / 174,647 checks. Both have zero reported proof failures,
preserve their disposable save bytes and existing protected QA inventories,
and their final frames were inspected. These are correctness checks, not
performance or full-adventure qualification. Neither mobile package was
installed on a physical device; the iPad player session remains untouched.
Evidence: `/Volumes/office/Build/OpenCrown/mobile-adventure-live/lazy-scalar-tls-mobile/`.

## Simulator background and muted-audio audit

A separate QA app uses the exact candidate Simulator CPU archive and the
existing `ios/platform/review/background-counter-qa.patch` to keep the app
runnable briefly while independently sampling guest counters. It is not a
release build. After entering Town, backgrounding stops muted platform audio;
15 background samples spanning 7.887 seconds report the guest paused, with
steps, flips and vblanks unchanged after settling. Returning to the same
session advances frames and resumes muted audio. The run finishes with 2,748
frames, 41,720 full-content texture checks, no proof failures or reported
underruns/clipping, unchanged disposable save and protected QA inventory, and
restored input. The final frame was inspected. The sampler's background task
changes normal OS suspension behavior: this does not qualify real device
suspension, audible quality, route changes or performance.

The first private QA package failed to link because `Source` and `source`
collided on the case-insensitive filesystem; its log is retained. A fresh
`background-qa2` package separates `patch-source` from `Source`, applies the
unchanged QA patch, and passes. Provenance and results are under
`/Volumes/office/Build/OpenCrown/mobile-adventure-live/lazy-scalar-tls-mobile/background-run/`.
No physical device was contacted, and this remains an unpromoted experiment.

## Native guided route to Harpy

A private guided host with input-only and read-only cursor helpers was linked
against the candidate memory object. Its 50,479-member archive differs from
the earlier guided reference only in `memory.o`. The retained route opens the
first chest and progresses through snakes, water/lizards and the visible Harpy;
three additional attack segments run after the original route. Captured
screenshots show the boss still alive afterward. The 21,277-frame run reports
528,556 full-content texture checks and zero proof failures; all disposable
save files remain unchanged, no guest/host error is reported and unload is
acknowledged. The harness intentionally stops on SIGINT with exit 1 because its
`HARPY` scene checkpoint is unmet (the original scene identifier remains
`GOBLIN`). This is not a boss clear, full adventure, device test, or performance
measurement. The private helpers are not release code, and the TLS candidate
remains unpromoted. Evidence:
`/Volumes/office/Build/OpenCrown/mobile-adventure-live/lazy-scalar-tls-guided/`.

## Extended guided Harpy and results

A second private native run used the same candidate binary and a fresh copy of
the disposable save. After the recorded route, the QA driver used input-only
attack and Continue commands, adjusted between steps after inspecting captured
frames; actual commands and screenshots are preserved. The Harpy was defeated
and the Stats screen shows score 8,074 and 925 EXP. The run ended on that
screen; it did not exercise rewards or an adventure save. It rendered 41,908
frames and verified 998,111 full-content texture checks with zero proof
failures. Host and guest errors were empty, unload was acknowledged, and both
the copied and source saves stayed byte-identical. The intentional SIGINT exit
is 1 because the configured HARPY checkpoint remained unmet while the scene
identifier stayed GOBLIN; this is not a generic checkpoint pass. Evidence is
`/Volumes/office/Build/OpenCrown/mobile-adventure-live/lazy-scalar-tls-harpy-extended/`.
No physical device was contacted. The candidate remains unpromoted: this is
correctness coverage, not evidence of an iOS performance gain.

## Recorded reference replay

`lazy-tls-replay.py` replays the 55 captured pad scripts and the two recorded
touch points from the extended candidate fight using a fresh disposable save.
The guided reference binary completed all 55 commands with identical script
contents, while actual starting vblanks differed by -1 to +10. It produced
40,719 rendered frames and 998,831 full-content texture checks with zero proof
failures, empty host/guest errors, clean unload and unchanged seed and working
saves. The final captured frame still shows the Harpy alive, whereas the
candidate reached Stats. The input-driven runs do not follow identical game
states, so they are stability coverage, not a differential correctness or speed
measurement. Intentional stop exited 1 with the same unmet HARPY checkpoint.
Evidence: `/Volumes/office/Build/OpenCrown/mobile-adventure-live/lazy-tls-harpy-reference-replay/`.
The executable replay driver is retained to reproduce this evidence; its
post-run script-equality and host-hash assertions were added after this run and
the script equality and host hash were independently verified on the recorded
files. No devices or normal saves were accessed.
