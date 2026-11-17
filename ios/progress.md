# Device port progress

## 2026-11-17 — Wyvern clear, scepter save and iPhone combat

On the Mac QA host using core `be8ce7c`, the player-style disposable route
reported a level-4 Guild request, visited the Castle, selected Julius and used
Report adventures. The game displayed Save completed; SAVE0.DAT changed from
`1618ee9e2bff1b5f21ef5121a4e3c3153ef0e5d6218fecde54f233459db19c2d` to
`cb7ac2636df46f46dd051aca38dfb0c434e54bcf3b1316fb7cebd67648868776`.
A fresh native process reopened both Darrel and Julius with the objective to
retrieve the royal scepter. It entered Old Capital, observed the formerly
missing `2024dec4` at vblank 19,664, and continued into the Wyvern encounter.
The game visibly reported the Wyvern defeated and the scepter obtained, then
returned to Town with the objective to go to the Castle. The game wrote new
SAVE0.DAT `82d24f52ac07522bedb3dc0ae137d49fe37cab64489fb225c3c49df0ee2ebf1b`.
The host rendered 41,323 frames / 41,770 vblanks and verified 973,719 texture
hits with zero proof failures, empty host/guest errors, protected source seed
unchanged and acknowledged unload. Its intentional stop exits 1 because the
unrelated HARPY checkpoint remains unmet.

With the user's iPhone available, signed `be8ce7c` was installed after a full
non-game-data backup; all 124 pre-existing files matched before and after
installation, before launch. Two distinct disposable diagnostic sessions were
staged with complete byte-for-byte readback. The phone cold-reopened the
post-Wyvern save and displayed the Castle objective (2,180 frames, 27,193
full-content texture checks, zero proof failures). With the pre-Wyvern save it
reached recognizable Old Capital with Darrel and Julius (10,511 frames,
196,808 verified hits, zero failures). An additional run using the same save
and input but with verification disabled reached Old Capital (11,358 frames,
216,967 version hits). Over its 1,799 measured intervals it averaged 56.60 fps,
p50 16.67 ms / p95 33.29 ms; thermal state at stop was fair. This single
short run is not sustained or heavy-boss performance qualification, and scene
and timing variability prevent a controlled verification-overhead comparison.
All three real audio callback runs were muted, reported zero underrun/clipped
frames, and left the diagnostic save inventories unchanged. Afterward all 122
protected pre-existing files still matched, excluding the two expected global
diagnostic reports; the normal launcher is restored muted. CoreDevice's 207 MB
install cache was cleaned. No iPad or live cloud save was accessed. Full story,
physical touch/gameplay and sustained performance, audible audio, and iCloud
provider/conflict qualification remain open. Evidence:
`/Volumes/office/Build/OpenCrown/mobile-adventure-live/capital-resume-castle/`,
`capital-castle-save-reopen/`, and `/Volumes/office/Build/OpenCrown/capital-state-phone/`.

## 2026-11-16 — New combat dispatch and level-4 save reopen

Another muted replay stopped at `2024dec4` (Old Capital combat). Original ELF
owner 24de00..24e2e1 uses a zero-extended selector, an unsigned <= 3 guard and
four signed relative targets in a file-backed table at 70b6f0. The reproducible
`ios/game/capital-state-switch-audit.py` checks the guard, table bytes, unwind
owner and instruction boundaries. All four targets build into the 50,508-entry
Mac/iPhoneOS/Simulator archives at `be8ce7c`; the exact new case has not yet
been observed executing in gameplay. The signed, icon-bearing iOS build is
verified but uninstalled. A separate uninstrumented Simulator Guild regression
passes at 5,126 frames / 73,972 texture checks with protected files unchanged;
it does not exercise the new combat case.

A subsequent native route diverged and reached different adventure results
without encountering the new case. Results show level 4 and 310 maximum HP;
after item appraisal and return to Town, the game wrote a new disposable save
(`SAVE0.DAT` SHA-256 `1618ee9e2bff1b5f21ef5121a4e3c3153ef0e5d6218fecde54f233459db19c2d`).
A fresh native process reopened a copy and showed Darrel in Town with 310/310 HP,
2,298 rendered frames, 29,498 full-content texture checks, zero proof failures,
empty errors and acknowledged unload. Both seed and working copies remained
unchanged during reopen. A separate muted Simulator app at `be8ce7c` reopened
the exact same save and showed Darrel in Town at 310/310 HP: 2,152 frames and
25,400 verified texture checks, with protected files and seed unchanged. Neither
reopen proves the prior dragon run's royal-scepter reward survived or qualifies
device, iCloud or full-story behavior. The test
host intentionally returns 1 because its unrelated HARPY checkpoint is unmet.
Evidence: `/Volumes/office/Build/OpenCrown/mobile-adventure-live/capital-reward-replay/`,
`capital-state-replay/`, `capital-level4-reopen/`, and
`/Volumes/office/Build/OpenCrown/capital-state-package/`. No physical device or
live save was touched.

## 2026-11-16 — Old Capital dragon and royal scepter

A second silent native replay with the `34b1c02` core reproduced the post-Guild
route and re-executed `2045e6d0` at vblank 33,599. It continued into a cavern
and a multi-dragon fight, used the game's continue option after Darrel fell,
and returned to the Inn's results: Darrel reached level 4 and the game displayed
that the royal scepter was reclaimed. Captures retain the fight, continue,
results, scepter and item-appraisal screens. The run reached 59,024 vblanks /
58,296 rendered frames, 1,334,643 full-content texture checks with zero proof
failures, empty host/guest errors and acknowledged unload. The QA driver's
900-second post-route deadline stopped it during appraisal; the runner exits 1
with its unrelated HARPY checkpoint unmet. The working and source saves stayed
unchanged, so this does not establish that the new progress was saved or
reopened. A fresh replay uses longer driver and host budgets. This is not full
story, physical device, audio, cloud or sustained-performance qualification.
Evidence: `/Volumes/office/Build/OpenCrown/mobile-adventure-live/capital-deep-replay/`.
No physical device or live save was touched.

## 2026-11-16 — Deeper Old Capital combat

A private native replay found missing `2045e6d0` during Old Capital combat.
`ios/game/capital-switch-audit.py` checks its four-slot bounded table against
the original ELF, prepared image, unwind owner and instruction boundaries;
all four targets are included in the 50,504-entry Mac, iPhoneOS and Simulator
archives. A silent disposable-save replay reached the Tommit story scene and a
further fight. Its read-only QA observer recorded `2045e6d0` at vblank 33,594,
then continued to vblank 39,869: 39,188 rendered frames, 824,073 full-content
texture checks, zero proof failures, empty errors and acknowledged unload.
The intentional stop returns 1 because the unrelated HARPY checkpoint remains
unmet. Its source and working saves remained unchanged, so this does not
establish persistence of the newly acquired quest or skill.

A separate uninstrumented Simulator regression reached Guild dialogue with
5,200 frames / 71,310 checks, observed the expected offline trophy call and
preserved its protected files. The signed, icon-bearing iOS build `34b1c02`
was verified but not installed. Neither test qualifies the full story, device
performance, audio or iCloud behavior. Evidence:
`/Volumes/office/Build/OpenCrown/mobile-adventure-live/capital-switch-replay/`
and `/Volumes/office/Build/OpenCrown/capital-switch-package/`. No physical device
or normal/cloud save was touched.

## 2026-11-16 — Guild quests, Castle and Old Capital

The guided Julius party defeats Harpy, appraises and obtains three items,
levels Darrel to 3 and returns to Town. Approaching the Guild then fails at
missing `20515a4d`. Original ELF owner 515980..515b9d uses a zero-extended
bit mask, a shifted/decremented EDX guarded unsigned <= 7, and eight signed
relative slots at 75a3b0. Five distinct destinations are verified against the
read-only ELF, instruction boundaries and unwind owner by
`ios/game/guild-switch-audit.py`; all five are required by bootstrap and
compiled into each 50,500-entry Mac, iPhoneOS and Simulator archive. This is
a table/target audit, not a complete CFG proof.

The interrupted disposable game wrote SAVE0.DAT and param.bin; its new
SAVE0.DAT hash is `c5e06eeea48aa237c843883a4acbb4dc6dfef36c1d50fc38544539760ea7cef9`.
A fresh native process reopens a copy with 306 HP visible. Its read-only
observer sees `20515a4d` twice before the next stop: missing
`sceNpTrophyUnlockTrophy`. The existing Mac loader treats this unresolved
import as a return-zero no-op; mobile's explicit offline allowlist now matches
that behavior. It does not unlock or persist platform trophies.

A second fresh native process reopens the same seed, observes the repaired
Guild case 360 times and the reference trophy stub, completes the Guild reward,
accepts a quest, buys Cyclone Masher, enters the Castle audience and reaches
Old Capital combat with Julius. It renders 27,645 frames / 27,711 vblanks,
with 539,926 verified texture hits, zero proof failures, empty errors and clean
unload. It stops intentionally with the unrelated HARPY checkpoint unmet and
exit 1. Both working and source save copies retain their seed hashes in this
run: quest/skill persistence is not established, and no Old Capital clear is
claimed. These private hosts use input/readback observers, not release code.

An uninstrumented iPhone Simulator app at `0517001` reopens the same seed and
reaches Guild dialogue while recording the trophy call: 5,143 frames / 74,563
verified hits, no reported error and unchanged QA saves. A separate QA-only
counter build observes the previously missing Guild case 59 times and the
trophy call with 5,183 frames / 73,118 checks. The checked-in
`ios/storage/ui-qa-guild.py` repeats this from an explicitly supplied disposable
seed and QA bundle; its independent repeat passes with 5,150 frames / 75,267
checks, 59 Guild-case hits and exact seed/protected-save inventories. None of
these runs qualifies physical audio, performance, trophy integration, save
persistence after skill purchase or device behavior. Signed icon-bearing
iPhoneOS build `0517001` verifies and is not installed. Evidence:
`/Volumes/office/Build/OpenCrown/guild-switch-audit/`,
`/Volumes/office/Build/OpenCrown/mobile-adventure-live/ally-post-harpy-guided/`,
`/Volumes/office/Build/OpenCrown/mobile-adventure-live/guild-trophy-cold-reopen/`,
`/Volumes/office/Build/OpenCrown/mobile-adventure-live/ally-simulator-study/guild-regression-repro/`
and `/Volumes/office/Build/OpenCrown/guild-hle-package/`. No physical device or
normal/cloud save was touched.

## 2026-11-16 — Ally switch and both continuations execute

The longer private guided ally run stopped at missing `200f8f13`. In the
supported original ELF, the owner spans f8bb0..f9054. It sign-extends a
16-bit selector, subtracts 0x4a in EAX, guards unsigned <= 0x6a, and dispatches
through 107 signed relative slots at 6e6984. The original read-only table and
owner bytes match the unique file-backed ELF mapping; all 30 distinct targets
are decoded instruction boundaries inside the unwind owner. The audit records
all target bytes and does not claim a complete owner CFG or individual effects.
`ios/game/ally-switch-audit.py` reproduces the target inventory. Bootstrap
requires all 30 destinations, including the observed case. All 30 and the prior
`2026dfc0` continuation are present in the 50,495-entry Mac, iPhoneOS and
Simulator archives, with their manifest hashes verified.

A separate native guided QA host uses the fixed core and read-only counters,
with no guest-field mutation. From a fresh disposable Julius save, it observes
`200f8f13` once at vblank 9,369 and `2026dfc0` 693 times, first at 17,297,
then shows the Harpy defeated with Julius and Darrel visible. It renders
19,190 frames / 20,058 vblanks and reports 550,405 full-content texture checks
with zero proof failures, empty host/guest errors, unchanged save and clean
unload. It intentionally exits 1 on SIGINT because the HARPY scene checkpoint
remains unmet (the scene identifier stays GOBLIN); this is not a generic
checkpoint pass. The counter/readback host is test-only.

A separate uninstrumented iPhone Simulator QA app at `4a3fb16` reaches the
first Ruins checkpoint with the same ally save: 6,680 frames, 154,150
full-content checks, zero failures/errors and unchanged protected QA saves.
It does not establish that the two target PCs ran on Simulator. The earlier
original-build retry passed once and another remained in the Inn's save menu
until timeout; neither reproduces the original intermittent Simulator failure.
An icon-bearing signed iPhoneOS app at `4a3fb16` builds and verifies without
increased-memory entitlements. It is uninstalled and needs device gameplay,
performance and audio qualification before promotion. Evidence:
`/Volumes/office/Build/OpenCrown/ally-switch-audit-final/`,
`/Volumes/office/Build/OpenCrown/mobile-adventure-live/ally-switch-guided-run/`,
`/Volumes/office/Build/OpenCrown/mobile-adventure-live/ally-simulator-study/switch-combat/`
and `/Volumes/office/Build/OpenCrown/ally-switch-package/`. No physical device
or normal/cloud save was accessed.

## 2026-11-16 — Ally combat exposes a missing continuation

A private guided native session starts from the completed-adventure disposable
save, reports at the Adventurers Guild, enters Canaan Temple, revives Julius,
selects him at the Inn and takes the party into the first Ruins fight. Captures
show the ally in Town and combat, including spell effects. The run renders
25,049 frames, checks 595,246 cached textures against full content with zero
proof failures, reports no host/guest error and unloads cleanly. The game
changes the disposable SAVE0.DAT and param.bin during the story/party flow.
A separate cold native process reopens a clone of the resulting save and shows
Julius still in the party, with 2,300 frames and 30,516 full-content checks.
Its save stays byte-identical and the original seed remains unchanged. On
reopen, the visible objective again says Go to Adventurers Guild; this does
not establish that the whole quest sequence persisted. Neither intentional
SIGINT stop satisfies the configured HARPY checkpoint; exit 1 is expected.

A separate iPhone Simulator QA app with that game-written save reaches the
first fight but stops at missing trace `2026dfc0` after 6,847 frames / 7,112
vblanks. The original supported executable has `VUCOMISS` at ELF 26dfbc and
`JBE +73` at 26dfc0, yielding 26dfc2 or 26e00b from incoming CF/ZF. The
bootstrap now includes that observed continuation. A fresh 50,465-entry core
compiles for Mac, iPhoneOS and Simulator. A separate uninstrumented Simulator
QA package reaches the same fight checkpoint with Julius visible, no runtime
error, unchanged disposable save and 157,529 full-content texture checks; it
ends at 7,138 vblanks. A private read-only counter build runs two longer fight
variants to 9,431 / 10,016 vblanks and 238,673 / 265,423 checks with no
proof failures, but observes **zero** entries to the continuation. Its test
assertions correctly fail: reaching the checkpoint does not prove the original
failed branch executed. One additional attempted test was rejected before
game startup because its 6,000-frame settle limit exceeded the allowed 3,600.

The test-truncated actual lifted entry is exercised for all four incoming
CF/ZF combinations by `ios/game/control-flow/ally-branch.py`; the selected
target and preserved flags match JBE semantics. This tests only the first
branch, not the full function or actual game execution. The continuation is
therefore provisional until an on-path run observes it and passes. Evidence:
`/Volumes/office/Build/OpenCrown/mobile-adventure-live/ally-simulator-study/`,
`/Volumes/office/Build/OpenCrown/mobile-adventure-live/next-scene-guided/`,
`/Volumes/office/Build/OpenCrown/mobile-adventure-live/ally-progress-cold-reopen/`
and `/Volumes/office/Build/OpenCrown/ally-continuation-fixture-repro/`.
No physical device or normal/cloud save was accessed.

## 2026-11-16 — Simulator Files-picker cancellation and damaged copy

The isolated real-picker QA exports a synthetic save into its own unique
local container, changes the current synthetic save, cancels an import, then
attempts a same-size checksum-damaged copy. Both errors are displayed and the
current save remains `after`; repairing the export and importing succeeds,
with current/exported `before`, incoming/previous checkpoints and marker
verified. XCTest passes on the dedicated iPhone Simulator. An initial bad
fixture had a different size and was correctly rejected as incomplete rather
than checksum-invalid; its failed expectation is retained. No physical
device, iCloud or real save was used. Actual Files-provider/network and
interruption behavior remain open. Evidence: `mobile-adventure-live/
files-picker-failures-simulator-qa3/`.

## 2026-11-16 — Real Simulator Files-picker local handoff

A uniquely named Simulator QA app exercises the production save-transfer
controller through the real Files picker: export a synthetic save into that
app’s own local container, change the synthetic current save, then import
and verify the exported copy. XCTest passes; separate filesystem checks
confirm both checkpoint contents, current/exported/source bytes and an
unrelated marker. The runner refuses an export without selecting its unique
QA container. Earlier local-root assumptions failed and are retained as
negative evidence; no cloud or real save was involved. This does not qualify
iCloud, physical Files UI, interruption, or game-generated saves. The player
is using the iPad and was not interrupted. Evidence:
`/Volumes/office/Build/OpenCrown/mobile-adventure-live/files-picker-simulator-final-qa4/`.

## 2026-11-16 — Simulator local save-transfer round trip

The `--scenario transfer` runner builds a uniquely named Simulator QA app
with the real transfer controller and local file coordinator. Using only
synthetic data, it imports an incoming save, validates and restores the old
checkpoint to a separate folder, then rejects an invalid import while checking
current save, checkpoint bytes and an unrelated marker. All 22 checks pass;
a separate rerun of the existing picker scenario passes 32. No production
code changed. This does not exercise a Files picker/provider, iCloud, game
saves, actual device interruption or physical devices. Evidence:
`/Volumes/office/Build/OpenCrown/mobile-adventure-live/save-transfer-preservation-final-qa/`.

## 2026-11-16 — Verify launcher fixes with the real Simulator game

Clean source `3b546d0`, with the coordinated-read, picker and pending-
presentation fixes, was linked into a separate icon-bearing Simulator QA
app against the audited `bb540ac` game/runtime/renderer artifacts. A muted
Town run reaches 2,128 frames with 23,631 full-content texture checks; a
separate disposable combat run reaches the first Ruins encounter in 9,917
frames with 171,052 texture checks. Both report no errors or proof failures;
protected QA save/checkpoint inventories and both disposable save hashes
remain unchanged. Captured Town and Ruins frames were inspected. These runs
do not exercise Files-provider UI, an in-game save, iCloud, real audio,
sustained device performance or physical devices. Evidence:
`/Volumes/office/Build/OpenCrown/mobile-adventure-live/launcher-fixes-game-qa/`.

## 2026-11-16 — Physical AMD reference for denormal status and trap

Isolated `codex/ios-denormal-audit` at `6997438` adds a bounded classifier for
masked 32-bit scalar ADD/SUB/MUL/DIV denormal-operand status. A real AMD Ryzen
7 7840HS Linux x86-64 host and M5 Max Rosetta produce identical values and
MXCSR in 4,096 sampled operand-class / DAZ / FTZ cases. The classifier predicts
status bit 1 in all 4,096 cases, including NaN and divide-by-zero priority,
with native and ASan/UBSan runs. This is sampled scalar status classification,
not exhaustive FP or DAZ/FTZ result emulation.

Combined manually with the experimental FPSR mapping, actual lifted operations
match the AMD reference on 24 masked, nearest-rounding, DAZ/FTZ-off cases,
including the two earlier denormal mismatches. Normal and ASan/UBSan runs pass;
both mobile SDKs compile the fixture syntax-only. This does not use actual
STMXCSR/LDMXCSR, integrate the HLE boundary, or qualify an app/device.

The same AMD host catches unmasked invalid as SIGFPE in a child process; the
Mac Rosetta process forces the invalid mask on, so the earlier Rosetta trap
probe remains failed. The physical host is Zen 4, not the PS4's Jaguar: one
trap case does not establish full equivalence or a translated trap solution.
Captures, source hashes and compiler/system identity are recorded in
`mobile-adventure-live/sse-nan-candidate/mxcsr-mapping/denormal-qualified-v2/`
and adjacent files. The [fixture](https://github.com/runty/OpenCrown-PC/blob/6997438/ios/cpu/review/denormal/README.md)
does not authenticate arbitrary supplied captured files.

The prototype remains isolated and uninstalled. DAZ/FTZ result behavior,
64-bit and packed lanes, unmasked trap translation, compiler ordering, actual
register-instruction integration, game performance and device acceptance are
open. No physical iPad, skipped manual save, or normal/cloud progress was used.
Stable pin and P4–P6 gates stay unchanged.

## 2026-11-16 — Prototype masked FPSR-to-MXCSR status mapping

Isolated `codex/ios-mxcsr-mapping` at `cbac3c2` maps five ARM FPSR sticky bits
to the corresponding x86 MXCSR bits and back for a simulated status load.
It preserves unrelated FPSR bits and the virtual x86 denormal bit but cannot
detect a new denormal operand. This helper is not connected to STMXCSR/LDMXCSR,
HLE or any app; exception traps, control modes and compiler ordering are not
implemented or qualified.

A 24-case masked scalar matrix uses the actual reviewed lifted operations and
an x86-64 Rosetta reference. Result and status match in 22; two denormal-input
cases have the correct value but miss MXCSR bit 1. The strict status result
remains failed. Pure mapping (256 checks), actual Mac FPSR load/clear (128)
and two-thread TSan (4,000) pass. Native and ASan/UBSan bit/register tests pass;
both mobile SDKs compile register tests syntax-only. A separate translated
invalid-add / native-boundary / guest-callback divide-by-zero sequence stores
`1f85`, matching Rosetta, with native overflow isolated. This is not actual
STMXCSR/LDMXCSR, general HLE or game integration.

An unmasked invalid-trap reference cannot be obtained here: Rosetta reads back
`1f80` after loading `1f00`, forcing the mask on, and the trap probe exits
without the expected signal. Its nonzero exit is retained; no trap pass or
physical AMD behavior is inferred. [Fixture and limits](https://github.com/runty/OpenCrown-PC/blob/cbac3c2/ios/cpu/review/mxcsr-mapping/README.md)
and private evidence `sse-nan-candidate/mxcsr-mapping/qualified/` record the
inputs and raw failures. Denormal priority, DAZ/FTZ, unmasked traps, compiler
reordering, packed lanes, actual game integration, performance and physical
validation remain open. Stable pin, device restrictions, skipped manual save
and all P4–P6 gates remain unchanged.

## 2026-11-16 — Exercise experimental fenv boundaries in real runtime

Isolated `codex/ios-fenv-runtime` at `9108301` places the prototype behind
`DCP_FENV_BOUNDARY_EXPERIMENT` in the real HLE service and nested worker
callback paths. A second compile-time flag injects a host exception and a
single translated `scePthreadOnce` callback for tests only. Neither flag is
used by a shipped app. No MXCSR status mapping or trap fix is implemented.

A private Mac archive differs from the verified NaN candidate in only its
instrumented `runtime.o`; all 50,479 members were independently hashed. The
real game reaches Town in 2,224 frames with 25,186 full-content texture checks /
114,386,884,608 logical bytes and zero mismatch. Its disposable R save remains
exact and the final Town image was inspected. During startup and the checkpoint,
2,312,840 service entries and one deliberately injected callback report zero
restore failures. The callback's guest flags were 0 and the native side held
invalid flag 1; guest entry and native return matched their expected states.
The callback runs the original translated `xor eax,eax; ret` at `0x20007bc0`;
it contains no floating-point work. This is one synthetic callback path, not
qualification of all real callback semantics or guest flag mapping.

The private archive also passes 20 parked-stop and five resume-cycle sessions,
plus configured cancellation and concurrent-report rejection, with 27 unload
handoffs and maximum observed stop 51.109 ms. The fixture uses an empty asset
root, not full gameplay. Both iPhoneOS and Simulator compile experimental
runtime syntax-only. Default-off Mac object text and relocations match the
reference. A host-only scope microbenchmark gives 17.02 ns median net per scope;
it excludes actual HLE work/instrumentation and is not device/game performance.
All details and limitations are in the [experiment notes](https://github.com/runty/OpenCrown-PC/blob/9108301/ios/game/fenv-boundary-experiment.md)
and `mobile-adventure-live/sse-nan-candidate/fenv-runtime-callback-v2/`.
No physical device or normal/cloud save was touched. Intentional HLE FP changes,
direct native helpers, DAZ/FTZ, exceptions/traps, performance, device testing
and the P4–P6 gates remain open; no integration or pin promotion is claimed.

## 2026-11-16 — Prototype guest/native floating-environment isolation

Isolated `codex/ios-fenv-boundary` at `ac1395c` tests a proposed native-boundary
scope and nested guest callback scope. It saves/restores separate floating-point
environments and collects callback changes in the guest side. This is a local
prototype, **not connected to the game runtime and not an MXCSR fix**.

Two host threads each run 1,000 sequences covering saved rounding and sticky
flags, native contamination, nested boundary/callback and exception unwinding.
Native, ASan/UBSan and TSan each pass 22,000 checks. The fixture compiles for
both iPhoneOS and Simulator with syntax-only checks; neither target execution
nor physical-device validation is implied. A separate test uses the reviewed
lifted ADDSS object: host invalid survives a native boundary, native overflow
is excluded, and a callback's divide-by-zero survives on the guest side.
Guest MXCSR remains `1f80`, demonstrating that no status mapping has been fixed.

Before integration, resolve intended HLE/callback rounding behavior, cancellation,
MXCSR status load/store/clear and mapping, denormal operand flags, DAZ/FTZ,
exception priorities, unmasked traps, packed lanes, compiler behavior and native
work outside the proposed boundary. Measure boundary cost and test both devices.
Proof: `mobile-adventure-live/sse-nan-candidate/fenv-boundary/qualified/` and
[prototype instructions](https://github.com/runty/OpenCrown-PC/blob/ac1395c/ios/cpu/review/fenv-boundary/README.md).
Stable pin, device restrictions, skipped manual save and full acceptance gates
remain unchanged.

## 2026-11-16 — Reproduce masked SIMD status and host contamination

A reproducible [isolated probe](https://github.com/runty/OpenCrown-PC/tree/codex/ios-mxcsr-evidence/ios/cpu/review/mxcsr)
at `0ee0285` executes the actual reviewed scalar add/sub/mul/div traces against
an x86-64 Rosetta reference on M5 Max. Eleven operand cases × two initial sticky
states use masked exceptions, nearest rounding and FTZ/DAZ off. Result bits
match in all 22 cases, while guest MXCSR differs in 13. The cases exercise
invalid, divide-by-zero, overflow, underflow and inexact status. Clearing host
exceptions immediately before the operation yields corresponding host flags
in these 22 cases, including the seeded guest invalid bit.

A separate negative control pre-raises host invalid and then performs exact
`1 + 2`: the reference guest status remains clean, while the host flag remains
set. Blindly copying host flags at STMXCSR would therefore leak exceptions from
native work. This is a bounded result, not a fix or proof that host-flag mapping
is generally safe. Denormal-operand status, DAZ/FTZ, exception priority, unmasked
traps, packed lanes, HLE callbacks, concurrency and gameplay use remain open.
The reference is Rosetta, not physical x86 hardware. `run.py` retains all raw
outputs and hashes; proof is under `sse-nan-candidate/mxcsr-probe/repro-matrix-formatted/`.

No runtime code, installed app, physical device or live save changed. The main
candidate remains isolated and the original skipped manual save is untested.
Host indexing and another workload still make a performance comparison noisy.

## 2026-11-16 — Simulator lifecycle passes; masked invalid status remains wrong

Packaged isolated candidate `a3e07c2` reaches Town on the dedicated iPhone
Simulator with a fresh disposable R copy. Launching Settings backgrounds the
running game; the guest reports paused at captured frame 1,700, and remains
at 1,700 across a five-second hold. Returning to OpenCrown resumes the same
session and reaches frame 1,939. The run completes 5,237 frames with 118,612
full texture checks / 464,702,838,784 logical bytes and zero mismatches. The
save hash stays `df59b0681ff021bf4609361f0abb8d6b50f895a9902db701d09cda91bccb1fb4`;
existing Simulator data and input are preserved. Simulator audio was muted and
reported paused throughout, so no audio-interruption recovery is established.
Physical-device lifecycle and sustained performance remain open.

A separate bounded CPU probe executes masked `ADDSS` on positive and negative
infinity. An x86-64 binary under Rosetta produces `ffc00000` and MXCSR `1f81`;
the actual translated candidate produces the same result but MXCSR `1f80`,
missing the sticky invalid-operation flag. Source object hashes and outputs are
retained in `mobile-adventure-live/sse-nan-candidate/mxcsr-probe/result.json`.
A real libc trace at canonical `0x30071f70` reads MXCSR status; static presence
does not prove gameplay executes it. This reproduces one specific flag gap,
not unmasked-trap behavior or every floating-point exception. No fix or full
CPU qualification is claimed; the candidate remains isolated and uninstalled.

The host was running unrelated Steam, indexing and another game workload during
this period, so no matched performance benchmark was attempted. Private lifecycle
proof is in `mobile-adventure-live/sse-nan-candidate/simulator-lifecycle/`.
The physical iPad, skipped manual save and normal/cloud progress remain untouched.
Stable pin `851c06b`, default-off versions and remaining P4–P6 gates stay open.

## 2026-11-16 — Isolated NaN-result candidate reaches Town and Ruins

Candidate `a3e07c2` on `codex/ios-sse-nan` corrects masked binary SSE/AVX
NaN result selection, with bounded [CPU review](https://github.com/runty/OpenCrown-PC/blob/a3e07c2fe69fcf2558a39748e3c2e693aebc6e4d/ios/cpu/review/sse-nan-results.md).
Guest MXCSR exception flags/traps, DAZ/FTZ, x87, horizontal operations and full
CPU fidelity remain open. It has not been integrated into this branch or pinned.

A new private backend frozen to the reviewed semantics regenerated all 50,464
entrypoints. Every recorded IR hash and both module images verify; 6,005 IR
files differ from the width-only candidate, while all seven reviewed native
CRILAYLA routines remain byte-identical. Mac, iPhoneOS and Simulator archives
finish with full generation/current provenance, matching library hashes, native
bulk libc and native CRILAYLA. The signed iPhoneOS package includes a compiled
icon and verifies its signature and renderer/archive hashes; it was not installed.

A native audit boots the real game into Town with the disposable R save: 2,225
frames, 25,653 full-content version checks / 118,014,629,888 logical bytes,
zero mismatches, 91 native decoder and 3 fallback calls, unchanged save and
clean shutdown. SDK compilation overlapped this run, so it is not a performance
sample. Its final Town image was inspected.

The packaged iPhone Simulator reaches Town (1,977 frames; 23,367 checks /
105,089,804,288 logical bytes) and separately travels through Town and world map
to the first Ruins encounter (7,120 frames; 140,866 checks / 540,681,011,200
logical bytes). Both have zero texture mismatches, unchanged disposable save
hashes and preserved existing Simulator data/input. Both final images were
inspected. The first Town attempt stopped before install because the simulator
was shut down; the retained retry passed after boot. These are correctness
checks, not device speed or full-adventure acceptance.

A static scan finds 17 libc traces calling rounding helpers; none match the
3,200 reported recent-entry contexts in an earlier Town profile. The limited
profile and name-based scan cannot prove those routines were not executed or
that exception handling is correct. Private evidence is under
`mobile-adventure-live/sse-nan-candidate/`. No physical iPad was contacted,
no manual save test was performed, and normal/cloud progress remains untouched.
Stable pin `851c06b`, default-off texture versions, sustained headroom and all
remaining lifecycle, audio, device, cloud and adventure gates stay open.

## 2026-11-15 — Narrow scalar FP operands; retain width proof and separate phone timing

Isolated `6970470` on `codex/ios-scalar-fp-width` adds nine selector corrections:
legacy/VEX MULSS/MULSD/DIVSS/DIVSD and VMAXSS read their actual m32/m64 operands.
Register/packed forms, arithmetic expressions, destination and merge types are
unchanged; ADD/SUB were already narrow. The bootstrap applies the new patch,
and game generation now fingerprints BINARY.cpp. The provenance fixture passes
22 checks, including changed/missing binary-source rejection. See the
[width review](https://github.com/runty/OpenCrown-PC/blob/697047062d02dbf27bc6324689ae4d7e76be18c8/ios/cpu/review/scalar-fp-memory.md).

Native x86 covers 24 forms × 256 vectors, four rounding modes, aligned and
unaligned sources, finite/zero/subnormal/infinity/NaN values and lane/flag
controls. Twenty valid protected-end operands work and twenty one-byte overruns
fault. Each baseline/fixed arm64 configuration, native and ASan/UBSan, evaluates
67,904 checks including 160 recorded/throwing boundary cases. All 18 baseline
excess-read negative controls disappear in the fixed version. Access, boundary,
input mutation, flags and inactive lanes have zero regressions; all 442,368
captured result bytes per mode remain baseline/fixed identical.

**Strict native-oracle qualification remains failed:** all four configurations
retain the same 160 NaN-selection/payload differences. The reports and nonzero
exits are retained. Width-delta proof is a separate verdict, not a full CPU
fidelity pass. Resolving the existing NaN policy requires a separate review.

A private backend and explicitly bound lifter actually regenerate all 50,464
frozen entry roots. Module bytes and the reviewed native CRILAYLA routines stay
exact; 4,488 IR files change. The game's `0x20619220` routine now has one checked
F32 read instead of four at VMULSS `0x20619240`. All Mac/iPhoneOS/Simulator archives
complete with current and actual-generation provenance, trace hash
`3ec0775bcb6316520ae607e26f2063c2a609bd8e50703d4429cad58f1076f1a1`.
Existing shared backends, frozen corpus, shader caches and nxnative are untouched.
Both SDK apps include icons and matching archive identities; physical signing
verifies without added memory entitlements. Renderer bytes, native bulk/codec
features, FreeType/FP80 helpers and inline flag lowering match the reference.

Native Town completes 2,226 frames/600 checkpoint intervals and 24,387 full
texture-version checks over 112,645,425,152 logical bytes with zero mismatches.
Packaged Simulator completes 2,133 frames/600 intervals, 24,898 checks over
114,520,973,312 logical bytes, zero mismatches, preserved existing state/input
and an inspected recognizable Town image. These audits are not speed tests.

Four uninstrumented physical iPhone walks use reference/candidate/candidate/reference,
with opt-in texture versions on, identical input/save bytes, nominal thermal
state, Low Power Mode off and real muted 48 kHz/256-frame audio. Median FPS is
59.424 reference versus 59.450 candidate; median checkpoint CPU is 26.916902
versus 27.265752 seconds (**1.30% higher**, normalized CPU/wall 1.36% higher).
The small, variable series establishes no headroom gain or sustained-performance
claim. A separate phone audit checks 25,851 hits / 118,792,048,640 logical bytes
with zero mismatches. All phone runs finish with clean errors, zero audio
underruns/clipping and exact disposable R save bytes. The width code remains
isolated; correctness of narrower access does not imply measured speedup.

Candidate installation preserves all 113 pre-existing non-asset files before
first launch. Restoration of the original `bb540ac` preserves all 122 then-existing
files before first launch. The original input, protected manual root and normal
muted launcher are retained; final comparison preserves all 122 prior files
except the two expected global reports, with 124 files now backed up. The new disposable R root is separate from the
skipped manual save check. No physical iPad or normal/cloud saves are used.

Private proof: `mobile-adventure-live/scalar-fp-width-candidate/`, including the
strict failed review, separate width delta, private-backend construction,
actual generation/IR change maps, SDK package identities, native/Simulator Town,
phone comparison/audit and preservation. The width code stays on its isolated
branch. Stable pin `851c06b`, default-off texture versions and full P4–P6 gates
remain unchanged; no sustained 60 fps or full-port completion is claimed.

## 2026-11-15 — Phone scalar diagnostics identify an oversized scalar FP read

Opt-in diagnostic `b238069` stays on `codex/ios-scalar-profile`. Normal builds
omit it: CPU/game runtime text and relocations match the guarded-cache reference
across Mac, iPhoneOS and Simulator. Native/ASan+UBSan/TSan verify exact counts,
worker/nested-state lifetime, original LOCK accounting and joined-only reporting;
the existing memory suite passes 267,130 checks per mode with the diagnostic
compiled in. Both SDK packages retain icons, full 50,464-entry provenance,
paired cache hashes and verified physical signing without memory entitlements.
No translated entry or existing backend is changed by this diagnostic.

The native and physical iPhone each complete 600 Town intervals with exact R
bytes and clean shutdown/errors. The phone records 661,691,324 lifted scalar
reads and 433,958,196 writes after the first active Town update. Its top 20
**most-recent-entry contexts** account for 48.91% of those accesses; these are
not exact active-caller attribution after a translated return. Sampled live guest
RIP uses scoped bindings restored across native guest callbacks. The xorshift64
sampler observes 1,072,140 acquisition latencies; 42 exceed 10 microseconds,
maximum 81.5 microseconds. Acquisition includes overhead and preemption, not
only lock contention. No total-wait extrapolation or CPU percentage is inferred.
Page/RIP limits omit 6,897/1,279 sampled attribution records; total scalar and
acquisition counters remain complete, with zero omitted threads/entry records
or allocation failures. This instrumented run is excluded from FPS/headroom
qualification. Real muted 48 kHz/256-frame audio has zero underruns/clipping;
sampled peak footprint is 2,383,023,984 bytes, not an OS high-water mark.

A concrete instruction-width problem is observed at canonical `0x20619240`:
original bytes `c5 fa 59 16` decode as `vmulss (%rsi), %xmm0, %xmm2`.
[Intel specifies a 32-bit memory source](https://cdrdv2-public.intel.com/782151/253667-sdm-vol-2b.pdf).
The current Remill selector instead instantiates `MULSS<..., MV128>` and its
actual game IR issues four checked F32 reads at offsets 0/4/8/12; only the first
value participates in the multiplication. Phone samples at this read and the
corresponding scalar store are 19,101/4,794, consistent with this hot expansion.
This is ordinary LLVM disassembly, source/IR and runtime-counter evidence,
not REA decompilation. REA accepts the derived ELF identity but cannot acquire
Hopper because another REA run owns it; that owner is left untouched and this
chat's binding is closed. The original SELF carrier is unsupported by REA.

Next audit and correct scalar SS/SD memory selectors in an isolated backend.
ADD/SUB are already narrow in the current source; do not change working forms
or replace all memory operands indiscriminately. Preserve register merge/upper
lanes/FP semantics, prove exact operand access with native x86 and guarded-end
negative controls, rebuild a fully provenanced corpus in a new tree, and then
measure the uninstrumented phone. No width fix or resulting speedup is claimed
yet. Existing frozen traces/backend and `~/work/nxnative` remain untouched.

The diagnostic install preserves all 106 existing non-asset files before first
launch. It uses a new disposable R UUID and restores original input. Restoring
`bb540ac` preserves all 111 then-present files before first launch and reopens
the normal launcher muted. The manual save root, normal/cloud progress and
physical iPad remain untouched; the manual Inn/save/reload test stays skipped.
Private proof: `mobile-adventure-live/scalar-profile-candidate/`, including
fixture/memory/default-code/package provenance, both native attempts, phone
result/analysis, preservation, `scalar-width-finding.json`, original-byte LLVM
assembly and the separate unsuccessful REA record. Stable pin `851c06b`,
default-off texture versions and outstanding full-port acceptance gates remain.

## 2026-11-15 — Exact renderer-entry filtering does not establish phone headroom

Isolated `b5d4fe5` on `codex/ios-trace-hooks` derives the native installation
metadata, exact inline AOT entry filter and direct dispatch from one shared
20-hook list. Addresses, handlers, displaced instruction lengths, branch flags,
installation order and comments remain identical. Ordinary traces avoid SysV
argument materialization and observer calls; all original scene/cursor,
cancellation, budget and LOCK behavior remains. All 15,482,921 checked offsets
match the original hook set, including high-bit/truncation negatives.

Six fresh renderer fixtures retain exact PNGs; the shared Mac build passes all
14 CTest suites in 22.29 seconds. A real native Town walk verifies 43,588 hits /
184,376,729,600 logical bytes with zero mismatches, exact R bytes and clean
unload/errors. Both SDK apps have compiled icons, paired renderer/core metadata,
complete backend provenance and verified device signing without additional
memory entitlements. All 50,472 non-overridden archive members per SDK remain
byte-identical to the frozen baseline, including all 50,464 translated entries.

Six interleaved native walks show median CPU 32.143833 / 31.016121 seconds
(reference/filter), 3.51% less for the prototype, with overlapping run ranges.
The physical iPhone series does not support promotion: four nominal cache-on
R/Town walks in reference/filter/filter/reference order use 29.651809 /
33.254249 / 28.177188 / 28.685302 CPU seconds. Median reference/filter is
29.168556 / 30.715719 seconds (5.30% more for the filter in this small series).
Rendered fps is 59.3991 / 59.0101 / 59.4488 / 59.3029. All sampled thermal states
are nominal, Low Power Mode is off, real muted 48 kHz/256-frame audio has zero
underruns/clipping, errors are empty and R bytes remain exact. Neither series
establishes a sustained or heavy-action gain. The prototype stays isolated;
`bb540ac` is restored, stable pin `851c06b` and default-off versions unchanged.

The packaged Simulator deliberately suppresses early walking and observes the
Ruins/GOBLIN after vblank 8,800: 9,649 captured frames, 217,762 full-verified
hits / 774,088,222,720 logical bytes, zero mismatches, exact Darrel bytes,
empty errors and prior-state/input preservation. The inspected final image
shows the Ruins, score 460 and the first chest. This checkpoint uses null audio
and is not a device, full-adventure or performance qualification.

Candidate phone installation preserves all 97 existing non-asset files before
first launch; restoration preserves all 104 then-present files exactly. Tests
use a new disposable R UUID, restore original input and reopen the normal
launcher muted. Existing normal/cloud/manual-session progress and the physical
iPad remain untouched; the manual save check remains explicitly skipped.
Private proof: `mobile-adventure-live/trace-hooks-candidate/`, including hook
preservation/equivalence, renderer/core/package provenance, full archive-member
comparison, native/phone summaries, preservation and inspected images. Next
investigate measured scalar synchronization or trace bookkeeping costs rather
than assuming that fewer observer calls provide actual device headroom.

## 2026-11-15 — Fresh cache-enabled phone profile identifies remaining overhead

An attached Time Profiler capture of the restored `bb540ac` iPhone app records
25.968692 seconds and 40,927 target-process samples. The trace explicitly
identifies the iPhone 17 Pro Max / iOS 27.2.1 (24B156), PID 53564, Time Profiler
and time-limit completion. The PID is independently listed alive immediately
before attachment. Eleven surrounding heartbeats show running Town, nominal
thermal state and muted real 48 kHz/256-frame audio. Versions are enabled,
verification disabled and original 256 KiB geometry retained. This instrumented
capture is not an FPS qualification or a timing comparison.

| Inclusive sample group | Weight share |
| --- | ---: |
| Lock / wake / wait paths | 44.3448% |
| Guest scalar memory helpers | 40.1275% |
| Trace-entry work | 12.8448% |
| Thread-local lookup | 4.2344% |
| Snapshot matching | 0.2175% |
| Full-content comparison | 0.0880% |

These are statistical sample weights, not measured CPU-duration percentages;
groups overlap and must not be added. Renderer entry observation is 6.6338%
inclusive / 0.6597% self, within trace-entry work. The profile confirms full
texture comparisons are no longer a major hot path; it does not establish how
much valid rendering work can be removed. System-PC names are reused from the
earlier capture only when binary UUIDs match; current symbols are retained.

Next investigate trace bookkeeping and repeated renderer observer lookup before
another lock-geometry experiment. `dcp_trace_enter` currently materializes six
register arguments and calls the 20-entry observer lookup for every game trace;
an exact hook-entry filter may avoid irrelevant calls. Any prototype must keep
all real hooks, scene/cursor observers, cancellation, bounds checks and original
LOCK semantics, and demonstrate a phone benefit before promotion. No such new
optimization is claimed here.

The first capture attempt pauses before TITLE while unlocked and is excluded;
its local reader also races an in-place diagnostic-file transfer, then gains a
retry. A second short diagnostic completes cleanly but Instruments cannot find
its target; no profile data is claimed. A longer timer-driven Town session
allows the verified PID attachment above. It stops cleanly with exact R bytes,
empty runtime/frontend/save/platform errors, zero audio underruns/clipping,
original input restored and silent normal launcher reopened. PROFILE is an
intentionally unobserved timer target, not a completed component checkpoint.
Private proof: `texture-cache-abi-candidate/phone-cache-profile-3/`, including
record intent/liveness/exit, trace, TOC, samples, attribution and diagnostic
reports. Earlier excluded attempts are retained separately. The physical iPad
and skipped manual save test remain untouched; the full port goal stays active.

## 2026-11-15 — Native lock-band gain does not reproduce on the iPhone

The separate `48bc60b` 64 KiB experiment retains original LOCK behavior and
passes memory/snapshot sanitizers, both SDK packages and native/Simulator
scene/combat coherence audits. Six interleaved cache-on native Town walks show
14.305% less median checkpoint CPU than the same guarded 256 KiB build.

The physical iPhone result is different. After cooling back to nominal, four
matched R/Town walks run 256/64/64/256 KiB with versions enabled throughout.
Every sampled thermal state remains nominal, Low Power Mode is off, saves
remain exact, errors are empty and real muted 256-frame audio has no underruns
or clipping. Median checkpoint CPU is 30.819987 / 32.229454 seconds and rendered
fps is 59.4508 / 59.1590. The candidate median uses 4.573% more CPU in this small
series; no convincing additional phone headroom is established.

The experiment stays isolated on `codex/ios-snapshot-lock-bands`. The phone is
restored to `bb540ac` with original 256 KiB geometry; stable pin stays `851c06b`
and texture versions stay off by default. The candidate install retains all
81 existing non-asset files exactly, and restoration retains all 88 then-present
files exactly. Each diagnostic restores input and the silent normal launcher.
Do not promote the narrower geometry based on its native proxy. Further tuning
should use a new cache-enabled phone profile; the completed long Town failure
below remains open. Private proof: `snapshot-lock-band-candidate/` phone summary
and preservation reports, plus the matched reference runs under
`texture-cache-abi-candidate/phone-comparison/geometry-baseline-{before,after}/`.

## 2026-11-15 — Native coherence audit reaches the Harpy; input fallback is exercised

A private native `bb540ac` harness advances through the first chest (Treasure
rank E / +98 gold), spiders, water/bugs, lizard combat, one death/respawn and
the visible Harpy boss / first 95-damage strike. Across 19,982 consumed frames,
all 497,273 version hits / 1,742,735,489,024 logical bytes pass full-content
verification with zero mismatches. Guest/host errors are empty, unload is
acknowledged and the original `728f0ec…` save remains exact. It stops
intentionally before the host wall budget; no boss/adventure-clear, sustained
timing or physical-device qualification is claimed.

The level component remains GOBLIN even while the Harpy and boss health bar
are visible. HARPY was therefore an unmet diagnostic target, not a valid
automatic boss checkpoint. Exit 1 / incomplete checkpoint is retained rather
than rewritten as a passing game-run result. Visual boss arrival and the
independent coherence counters are the evidence here.

Private support replacements supply ordinary pad/tap input and a read-only
snapshot of the same AOT cursor mailbox used by mobile input. The first pointer
harness incorrectly queried the separate native relay cursor storage, which
remains empty under AOT; that attempt is stopped and excluded. The corrected
harness uses the production TouchLoop and public pause API. It changes no
guest fields or translated functions; runtime/offline/host source/object hashes
and commands are recorded separately. This diagnostic harness is not packaged
into either device app and is not a performance sample.

The adventure-save route also gains late walking for slower Ruins arrival.
A separate `48bc60b` Simulator test deliberately removes the early encounter
walk and requires GOBLIN observation at vblank 8,800 or later. It captures 9,637
frames and verifies 219,540 hits / 780,911,747,072 bytes with zero mismatches,
exact Darrel bytes, clean errors and preserved prior state/input. This proves
the fallback actually executes, rather than merely passing via early movement.
It is an input fixture, not a promotion of the experimental CPU geometry.

Private evidence: `texture-cache-abi-candidate/scene-guided/` provenance and
`adventure-aot-pointer/observation.json`, command log and inspected step images;
`snapshot-lock-band-candidate/simulator-ruins-fallback/` and its explicit
checkpoint-threshold assertion. Physical iPad remains excluded; the player’s
manual save check remains skipped.

## 2026-11-15 — Guarded cache reaches the iPhone and the Ruins

The player unlocks the only authorized physical iPhone. Clean `bb540ac`
installs and all 64 pre-existing non-asset files remain byte-identical before
first launch. Normal roots, the skipped interactive save session and live
cloud progress are retained. Tests use a new disposable R UUID, restore the
original input after each run and reopen the normal launcher silently.

Four interleaved 1,200-settled-interval Town walks use this same signed package,
frozen corpus, R save and committed input route: off/on/on/off. Every sampled
thermal state is nominal and Low Power Mode is off. Median results:

| Metric | Versions off | Versions on |
| --- | ---: | ---: |
| Checkpoint CPU | 45.637113 s | 28.686214 s |
| Rendered fps | 54.4643 | 59.3998 |
| Frame p95 | 33.3087 ms | 16.8196 ms |
| Replay work p50 | 4.49327 ms | 0.396583 ms |

That is 37.14% less checkpoint CPU and 9.06% higher rendered fps in these short
matched walks. The two enabled runs are 59.35/59.45 fps, versus 55.33/53.60
off. All retain exact `df59b068…` bytes and report empty guest/frontend/save/
platform errors, zero audio underruns/clipping and real muted 48 kHz/256-frame
callbacks. This is a device result, distinct from the earlier native 9.10%
proxy; neither series establishes sustained 60 or heavy-action headroom.

A separate physical full-content audit covers 25,286 hits / 114,880,665,600
logical bytes with zero proof failures, exact R bytes and the same clean audio
accounting. Auditing does the extra full comparisons and is excluded from the
timing comparison. A longer Town run completes 2,015.13 observed Town seconds /
1,933.08 walking seconds and 110,697 frames before clean scheduled shutdown.
It fails sustained performance: rolling approximately minute-long windows have
median 55.14 fps, minimum 43.22 and final 52.99; final p95 is 33.34 ms in serious
thermal state. One observed burst adds 12,800 underrun frames (266.7 ms) at
1,122.35 Town seconds; clipping remains zero. This is muted real audio
accounting, not an audible-output check. Sampled peak footprint is
2,362,724,184 bytes (heartbeat samples, not an OS high-water mark). Guest/
frontend/save/platform errors remain empty, exact R bytes survive, input is
restored and the silent launcher is reopened. The retained `result.json`
explicitly records `performance_pass=false`; the original test correctly rejects
the underruns. Neither sustained 60 nor clean audio passes. Stable pin/default
remain `851c06b`/versions off pending qualification.

Native `bb540ac` also reaches the Ruins first goblin encounter from the
qualified Darrel adventure save: 7,272 consumed frames, 720 settled intervals,
143,773 audited hits / 553,213,425,664 logical bytes, zero mismatches, 145
uploads, four budget evictions, clean unload/errors and exact `728f0ec…` bytes.
The final image shows score 430 and GO after that first encounter. This is not
a complete adventure-clear pass on the new cache.

The first packaged Simulator route stays in Inn Options and is excluded from
combat acceptance. An independent INN checkpoint shows Leave tavern selected;
the route's last confirmation was too early. Late Cross pulses at 1,680/1,860/
2,040/2,220 allow that menu to become ready before Town movement. The corrected
route captures 7,027 frames, reaches GOBLIN, completes 720 settled intervals,
audits 143,347 hits / 545,942,306,816 bytes with zero failures and has 145
uploads/seven budget evictions. Exact Darrel bytes survive, errors are empty,
and existing Simulator state is preserved except global reports, input restored
and silent launcher reopened. The inspected final image shows the live Ruins
encounter with an enemy still present; no encounter-clear claim there. Simulator
checkpoint audio uses the paced null sink, not physical audio qualification.

The committed adventure-save route records the extra confirmations. The fresh
R save is separately observed in the first-town Guild introduction, so its
attempt is intentionally stopped and excluded from adventure acceptance. The
physical iPad remains excluded and the manual save check remains skipped.
Evidence: `mobile-adventure-live/texture-cache-abi-candidate/` under
`phone-comparison/`, `phone-install-preservation.json`,
`ruins-adventure-save-audit/`, `simulator-inn-route/`,
`simulator-ruins-confirmed/` and the retained unsuccessful-route observations.

## 2026-11-15 — Mixed capture/statistics cache ABIs are rejected before launch

Implementation `bb540ac` gives the changed by-value hook type/initializer and
statistics getters explicit V2 symbols. Previously an older renderer could
satisfy the getter name while returning the smaller old record. Current
capture/backend/replay probes link and execute with the rebuilt cache, and all
three fail against both a pre-version cache and the pre-guard version cache
for the expected missing V2 symbol. Old archives remain unchanged. Every AOT
caller is updated and rebuilt; no snapshot or game logic changes here.

All six rebuilt renderer fixtures retain their exact image hashes and checks.
Seven native support objects and compatible renderer archives are rebuilt for
Mac/iPhoneOS/Simulator with the unchanged complete 50,464-entry corpus and
explicit source/command/override provenance. Both clean SDK packages build with
icons; physical signature verifies without extra memory entitlements, while
Simulator signing is disabled.

The real native Town walk consumes 2,825 frames, completes 1,200 settled
intervals and audits 44,026 hits / 187,377,842,176 logical bytes with zero proof
failures, clean unload/errors and exact `df59b068…` R bytes. Packaged iPhone
Simulator independently captures 2,125 frames, completes 600 settled vblanks
and audits 24,862 hits / 115,733,399,552 bytes with zero failures and empty
runtime/frontend/save/platform errors. Both final images are inspected as
recognizable Town. Existing Simulator state is preserved except global reports,
input is restored and the silent launcher reopened. These are correctness
audits, not a repeat of the earlier unverified nine-run performance comparison.

The connected iPhone is locked (`passcodeRequired=true`), so an unlock request
is pending. Its old paused `110904c` session is left unchanged; no app install
or launch occurs. All 64 non-asset files are backed up read-only (201 MiB) before
any future update. Next, once unlocked, install this same guarded package and
compare versions off/on silently with a new disposable R UUID, recorded thermal
state and real audio accounting. The earlier native 9.10% CPU result remains
proxy evidence; no phone speedup or sustained pass is claimed. The stable pin
stays `851c06b`, the physical iPad remains excluded, and the player-skipped
manual save check stays deferred.

Private proof: `mobile-adventure-live/texture-cache-abi-candidate/`, including
`abi-tests{,-preversions}/verification.json`, renderer fixtures,
`core-verification.json`, `package-verification.json`, native/Simulator Town
reports and the read-only phone backup/lock-state records.

## 2026-11-15 — Versioned texture snapshots reduce native Town CPU cost

Implementation `4741c15` adds immutable owned snapshots with versions bound to
their exact bytes, registry and resolved host span. Only opted-in DirectMemory
mappings can use version-only reuse. Ordinary/native-owned mappings and every
page exposed through a mutable pointer use the full-content fallback. All
checked scalar/floating/f80/CAS/bulk/glyph writes invalidate watched pages under
the existing locks. Registration, aliases, reset and counter saturation are
covered; no second global write lock or partial hash is introduced. The feature
remains opt-in with `DCP_TEXTURE_VERSIONS=1`; stable pin stays `851c06b` pending
device qualification. See [protocol and tests](cpu/review/snapshots.md).

The snapshot fixture passes natively, under ASan/UBSan and under TSan, including
concurrent capture/publication (check counts vary with scheduling), original
LOCK exclusion, native/synthetic aliases, all writers, pre/post-capture escapes,
native-owned overlaps, mapping/registry identity and session changes. A separate
seven-count version limit proves saturation falls back; a private omitted-store
negative control is detected and never enters a game. Verification mode also
detects a deliberately unreported fixture write. The existing memory fixture
passes all 267,130 checks per configuration. All six renderer fixtures are
rebuilt for the new capture ABI and retain exact PNG hashes, 49,152 pixel
channel checks, 36,864 saved RGB checks and 1,504 BC checks. A fresh shared Mac
build passes all 14 CTest suites (44.14 seconds); CPU oracle is 1,206/1,206
byte-exact, zero unplaceable/failed, with 37 original patch-site byte mismatches
reported separately.

The first real native R/Town walk audits every version hit against full bytes:
44,141 checks covering 187,856,517,120 logical bytes, zero proof failures.
Only eight ordinary comparisons remain; zero retention misses. It consumes
2,823 frames, completes 1,200 settled vblanks, preserves exact `df59b068…` save
bytes, unloads cleanly and has no guest/host error. The inspected image shows
Town after walking. Audit mode still performs the extra full comparisons and
is excluded from performance claims.

Nine interleaved unverified native walks compare three runs each of the prior
`e9369f1` build, new build with versions off, and new build with versions on.
All use the same frozen corpus/current shaders, R save, committed walking route
and 1,200 settled intervals, stay in Town without scene changes, preserve exact
save bytes and finish with clean errors/unloads. Median checkpoint CPU time is
33.080844 s prior / 33.286678 s off / 30.070929 s on: **9.10% less than prior**
and **9.66% less than off**. Median replay work is 2.551 → 0.392834 ms (84.60%
lower), p95 2.760417 → 0.510709 ms. Native pacing stays capped near 59.4 fps.
This is measured native headroom, not a phone speedup, sustained/thermal result
or combat qualification.

Both SDK apps build from clean `4741c15` with icons, unchanged complete
50,464-entry translation provenance, seven explicit native support overrides
and rebuilt compatible backend/replay/capture archives. Physical signing
verifies without extra memory entitlements; Simulator signing is disabled.
The packaged iPhone Simulator independently audits 24,892 hits / 115,376,619,520
logical bytes with zero failures, captures 2,138 frames and completes a
600-vblank visible-Town checkpoint with empty runtime/frontend/save/platform
errors. Exact R bytes survive; every old state file except the two global
reports is preserved, input is restored and the silent launcher reopened.

Private proof is `mobile-adventure-live/texture-version-candidate/`: snapshot
and memory fixtures, renderer fixtures/provenance, `macos-ctest.log`,
`core-verification.json`, `package-verification.json`, `town-runs/verified-walk/`,
`town-comparison.json` and `simulator-town/`. Neither physical device is used
or updated in this stage. Next is a silent same-package iPhone off/on comparison
with thermal state recorded, followed by sustained Town/combat checks before
default enablement or pin promotion. Keep the iPad excluded and the skipped
manual save check deferred; normal/live-cloud saves remain untouched.

## 2026-11-15 — Font and IME guest output uses checked publication

Implementation `e9369f1` routes font input, initialization, handles, layouts,
surface descriptors and results through checked copies. The original font
renderer rasterizes privately; clipped bitmap rows publish with checked
`max(old, coverage)`, preserving higher existing coverage and pitch padding.
This avoids full-surface staging per glyph. `guestMaxBytes` shares access locks
and original guest LOCK exclusion with other native writes. Font/IME callback
groups must be complete; legacy pointer users remain supported. IME name/result
writes now use checked publication too, preserving dialog state on rejection.
See [the protocol and fixture command](game/font-memory.md).

The font fixture compares exact surface, metrics and result bytes with the
original renderer across ten glyph/surface cases. All 985 checks pass natively,
with ASan/UBSan and with TSan. The deliberately unlocked blend fails the
functional original-LOCK assertion as expected; it is fixture-only. The cached
FreeType library itself is not rebuilt with sanitizers. The memory fixture
passes 267,130 checks per configuration, including max-coverage bounds,
page/lock-band crossings and native/synthetic aliases. IME passes 1,002 checks
per configuration, including 200 concurrent completions across legacy/checked
paths, failure retry and complete panel validation. Both SDK probes compile.

Six native support objects are rebuilt for Mac/iPhoneOS/Simulator, preserving
the frozen 50,464-entry corpus and original archive hashes. Source/native header
hashes and commands are recorded separately; packages carry explicit native
override metadata. Both SDK apps build with icons and full translation
provenance. The iPhoneOS signature verifies without extra memory entitlements;
Simulator signing is disabled. Stable pipeline pin stays `851c06b`.

The real native game reaches inspected Town, consumes 2,828 frames and completes
a 1,200-vblank checkpoint with clean unload and empty guest/host errors. Font
diagnostics confirm checked setup: 6,758,400 read bytes and 416 written bytes.
Native CRILAYLA still executes 91 fast calls / three fallbacks. The packaged
iPhone Simulator independently reaches inspected Town, captures 2,144 frames
and completes a 600-vblank checkpoint with the same font counters and empty
runtime/frontend/save/platform errors. Both retain exact disposable R
`df59b068…` save bytes. Existing Simulator state survives installation/run except
the two global reports, with input restored and silent launcher reopened.

Both Town runs record zero dynamic glyph calls: its text uses a baked atlas.
They qualify setup/integration, not dynamic glyph execution in the game or a
new IME keyboard UI pass. No speedup or sustained hardware result is claimed.
Opaque native-owned Font/token objects and other service pointer escapes still
need conservative exclusion or mutation tracking. Next work is snapshot-bound
version metadata with full-content fallback for untracked writes, preserving
original LOCK behavior and mapping/snapshot identity. Private evidence:
`mobile-adventure-live/font-memory-candidate/` (`font-tests-final/`, `ime/`,
`memory-tests/`, `core-verification.json`, `package-verification.json`,
`native-town/` and `simulator-town/`). Neither physical device is used; the iPad
stays excluded and the player-skipped manual save test remains deferred.

## 2026-11-15 — Native file output uses checked memory publication

Implementation `fe8f18a` supplies non-escaping validation and synchronized
copy callbacks to the file adapter. File reads publish through `guestWrite`;
file input, paths and stat results also use checked copies. Transfers stage
at most 256 KiB while retaining 64 MiB validation windows, descriptor locking
and save-service admission. Guest locks are not held across blocking file I/O.
Native CRILAYLA range checks use `guestValidate` rather than borrowing a mutable
pointer; its output still publishes through `guestWrite`. The original CPU
lock geometry and full-content texture checks remain unchanged. See
[the protocol and regression command](game/file-memory.md).

The new real-memory/file fixture passes 114 checks natively, with ASan/UBSan
and with TSan. It covers chunk/64 MiB boundaries, EOF tails, positioned I/O,
invalid/rejected access, errno/stat output, callback configuration, reset and
original guest LOCK exclusion. A deliberately unlocked publication callback
correctly fails the functional assertion; negative code is fixture-only and
never enters a game archive. The existing memory fixture passes 267,095 checks
in all three configurations. Save integration passes 216 checks across both
callback paths and eight relaunches, including interrupted-write recovery;
quiescence passes 30 checks across both paths. Storage retains its 149 checks.

Four native support objects are rebuilt for Mac/iPhoneOS/Simulator in separate
archives, with source/header/command hashes recorded. The frozen 50,464-entry
translated corpus, trace hash and original archives remain unchanged. Both
SDK packages compile with icons and complete translation provenance. The
iPhoneOS signature verifies without extra memory entitlements; Simulator
signing is disabled. Native override provenance is explicit in each package.

The real native game reaches inspected R/Town, consumes 2,822 frames, completes
its 1,200-vblank checkpoint and unloads without guest/host error. Final
`file_memory` diagnostics prove the new path executes: 277,512,802 bytes copied
into guest memory and 79 bytes read from it (including path/stat copies).
Native CRILAYLA still executes 91 fast calls / three fallbacks. The packaged
iPhone Simulator independently reaches inspected R/Town, captures 2,151 frames
and completes a 600-vblank checkpoint with 277,394,038 checked output bytes,
empty runtime/frontend/save/platform errors and counters retained after unload.
Both preserve exact disposable R bytes `df59b068…`. Simulator installation/run
preserves every pre-existing state file except the two global reports, restores
the original input and returns to the silent launcher.

Private proof is `mobile-adventure-live/native-memory-candidate/`, including
`file-tests-final/verification.json`, `memory-tests/`,
`final-service-verification.json`, `core-verification.json`,
`package-verification.json`, `native-town/` and `simulator-town/`.
This closes the file writer escape on the production path, not the remaining
font/IME/offline/service escapes or texture version tracking. No speedup or
sustained hardware qualification is claimed. Neither physical device is used,
the stable pin remains `851c06b`, and the player-skipped manual save test stays
deferred. Next work is native font/service publication and conservative version
tracking with the full-content fallback retained.

## 2026-11-15 — Texture scan counters identify repeated unchanged comparisons

Implementation `ee59b0c` adds render-thread texture request, comparison, copy,
hash, upload and snapshot-budget counters to native and iOS diagnostic reports.
The counters survive renderer teardown. They preserve the current full-content
checks and do not change the CPU memory-lock geometry. Both SDK app packages
build with compiled icons and the unchanged 50,464-entry frozen core, complete
current/generation backend provenance and native libc/CRILAYLA paths. The
iPhoneOS signature verifies without increased-memory or extended-VA
entitlements; Simulator signing is disabled. Neither physical device is used
for these checks, and the stable pipeline pin stays `851c06b`.

The real native R/Town run consumes 2,825 frames and completes its 1,200-vblank
checkpoint with clean unload and empty guest/host errors. It compares
187,525,429,248 logical bytes: 44,074 of 44,075 comparisons match. Successful
matches cover about 66.38 MB per consumed frame across the whole session,
including startup. Copies/hashes total 276,334,592 bytes, with 60 uploads,
six budget evictions and zero retention misses. This points to redundant
comparisons rather than the snapshot budget on this route. Instrumented timing
is 57.58 fps; there is no performance improvement or zero-overhead claim.

The packaged iPhone Simulator app independently reaches visible R/Town,
captures 2,146 frames and completes a 600-vblank checkpoint. Its counters
survive teardown: 25,528 of 25,529 comparisons match, spanning
118,000,379,904 matched logical bytes; copies/hashes again total 276,334,592
bytes, with 60 uploads and zero retention misses. The final image is inspected,
runtime/frontend/save/platform errors are empty, and the disposable R save
remains exact `df59b068…`. Every pre-existing non-asset file survives installation
and the run exactly, except the two intentionally replaced global reports;
the original scripted input is restored. This is renderer integration evidence,
not hardware timing or the skipped manual save test.

All six renderer fixtures pass, retaining exact image hashes, 49,152 pixel
channel checks, 36,864 saved RGB checks, 1,504 BC checks and suspend/resume.
A fresh shared Mac build passes all 14 CTest suites. Its CPU oracle reports
1,206/1,206 byte-exact re-encodings, zero unplaceable/failed sites; it separately
reports 37 original patch-site byte mismatches. Rebuilding the original cached
backend/replay objects with their original commands reproduces their hashes
exactly before adding the counters.

The [writer audit](render/texture-coherence.md) finds that file reads, font/IME
outputs and runtime hooks still use borrowed mutable guest pointers. A cache
that tracks only translated stores would miss those writers. Next work must
give native writes an explicit lifetime/publication protocol, associate versions
with the exact snapshot and mapping, and retain full comparisons whenever
tracking is incomplete. No cache shortcut or lock experiment is promoted.
Private proof is `mobile-adventure-live/texture-metrics-candidate/`, including
`native-town/`, `simulator-town/`, `render-fixtures/`,
`baseline-object-check/verification.json`, `package-verification.json` and
`macos-ctest.log`. Physical input confirmation remains valid; the player-skipped
manual save check remains deferred and the physical iPad stays excluded.

## 2026-11-15 — Physical controls pass; sustained iPhone pacing remains below target

The player confirms actual iPhone touch movement/menu use, controller movement/
menu use, touch controls hiding/returning, and disconnect/reconnect with continued
input. This is interactive `110904c`, with no scripted pad input, resuming only
the disposable R save. The player also confirms muted system volume and no
XCTest automation prompt. App output remains explicitly muted. A later Home
Screen capture and live report show guest workers paused and the real audio
stream stopped/paused with empty runtime/frontend/save/platform errors. That
image is excluded from visual overlay proof; overlay behavior is player-confirmed.
The player declines the additional Report adventures / Stop Game check, so
that check is skipped and is not counted as physical save qualification. The physical iPad remains excluded from this work window.

An earlier `851c06b` sustained Town attempt exposes a real acceptance failure.
The corrected route reaches Town and walks there for 416 observed seconds
before an intentional early stop. Its final 61-second window averages
**37.62 fps** in serious thermal state, with one **256-frame audio underrun**
and zero clipping; output is muted throughout. There is no 30-minute pass.
The collector is interrupted deliberately and restores the input file/launcher;
this is not a clean-unload qualification. Its first attempt stays at the Inn
and is excluded. The corrected route remains in Town, with empty live frontend/
save/platform errors. Private proof is
`phone-input-peek/town-soak-confirm-851c06b/intentional-stop.json`.

Twelve interleaved native Town runs compare 64/128/256/512 KiB memory lock
regions, retaining the original 64 locks and identical frozen 50,464-entry
corpus/current mobile renderer. Every run preserves exact R bytes and completes
without runtime/render error. The 64 KiB variant uses 29.09 versus 33.24 seconds
of median checkpoint CPU time (12.48% lower) while rendered pacing stays around
59 fps. This is native proxy evidence, with considerable variation, not a
phone speedup. An initial run against an older renderer cache is excluded.
A separate native combat macro ends in Inn options; it provides no accepted
combat comparison and preserves the prior save exactly.

Experimental branch `codex/ios-lock-band-candidate` / `110904c` changes only
that region size and adds a low-power-mode diagnostic. Both SDK packages build,
with icons, complete unchanged translation provenance and valid physical signing
without extra memory entitlements. The memory fixture passes 267,095 checks
natively, under ASan/UBSan and under TSan. On the phone, consecutive same-save/
same-input walks measure **50.74 fps** for `851c06b` in fair thermal state and
**38.99 fps** for `110904c` in serious thermal state. That is not a controlled
temperature comparison. After two nominal readings, the experimental app repeats
at **57.59 fps**, ending fair; low power mode is false. All three runs preserve
R's exact hash and have empty runtime/frontend/save/platform errors, zero audio
underruns/clipping and muted real callbacks. The experiment remains unaccepted;
the stable pipeline pin stays `851c06b`. The phone temporarily carries `110904c`
for the player controls/save check.

A separate host-only prototype expands the memory-lock table while preserving
ascending acquisition, checked access and full original LOCK exclusion. Its
267,095-check fixture and mixed scalar/bulk stress pass natively and under both
sanitizer configurations. Stress alone does not detect a deliberately omitted
upper-lock-word regression. A separate functional exclusion test does: zero of
256 writers finish inside the original LOCK for the correct prototype, versus
192 premature writes for the broken control. The broken code never enters a
game archive. Nine native runs compare the legacy table, the generalized table
at the old size, and 256 locks; the larger table alone shows no convincing gain.
Six further runs combine 256 locks with 64 KiB regions: median checkpoint CPU
31.71 → 28.09 seconds (11.39% lower), with similar capped pacing and exact save
hashes. These separate series do not establish an additional benefit over the
simpler experiment. No expanded table is installed or promoted.

Private evidence is under `cursor-investigation/{lock-band-review,stripe-count-review,phone-input-peek}/`.
Hardware fresh-character/adventure creation, audio/output recovery, physical
Files/remote sync and sustained Town/combat acceptance remain open. All normal
player saves, live cloud progress and the physical iPad stay untouched.

## 2026-11-15 — First-use local Files transfer and portable silent testing

Separate private provider/consumer QA apps exercise the unchanged production
save-transfer controller and storage code from the `280b856` candidate. Export
through the system Files picker to an ordinary local root succeeds. A new
controller with no remembered URL browses into that exported folder and imports
the exact adventure save (`728f0ec…`) over the touch-earned save (`df59b068…`).
The previous tree and a completed checkpoint retain the replaced bytes exactly.
Cancelling the initial picker preserves local progress. A damaged export with a
one-byte checksum mismatch rejects before promotion; all local/previous/checkpoint
files remain byte-identical. Private evidence is
`cursor-investigation/files-provider/verification.json`.

App-container rows still do not navigate on both an existing and a fresh
owned iOS 27.2 Simulator; Apple's own Files app reproduces the failure. Provider
logs report a missing cached domain for `FILocalAppContainerNode`. Ordinary
provider folders work in that same environment. This closes first-use local
folder browsing, not app-container navigation, physical Files or remote iCloud
handoff/conflicts. Earlier failed helper timing/navigation attempts are excluded;
the final negative-case and original cancellation helpers exit with one passing
test each. Normal game save roots and live cloud progress remain untouched.

`734ceb9` changes the shared validator's misleading executable-hash message to
`File checksum mismatch`; valid export and damaged-save fixtures confirm both
paths, with the existing 149 storage / 108 integrated savedata checks passing.
`851c06b` adds `DCP_MUTE_AUDIO=1` on devices as well as Simulator. The real render
callback consumes/accounts PCM, then zeroes output buffers; diagnostics expose
`audio.output_muted` without changing the audio-stats ABI. The legacy Simulator
mute name remains accepted. The input/audio probe (400,000 concurrent frames),
pointer/concurrency probes and both SDK compile/link checks pass. Both clean SDK
app packages retain the frozen 50,464-entry core, complete backend provenance
and compiled icons; physical signing verifies without extra memory entitlements.
Simulator bundle signing is disabled and is not a device-signature qualification.

The player makes the physical iPhone available and explicitly excludes the iPad.
Only the iPhone is updated; all 24 pre-update non-asset files are backed up and
preserved exactly across the initial update. Physical XCTest times out while
enabling automation mode before test methods execute, so it provides no touch
qualification. App-level muted output avoids relying on the private native volume
helper; phone system media volume is not claimed to be zero. Real device timing
and save checks are recorded separately below after completion.

## 2026-11-15 — Current iPhone combat, cold saves and performance profile

Only the explicitly released physical iPhone 17 Pro Max is used. Signed
`851c06b` retains the same frozen game core. The first fixed-timing boot remains
on the outbound map and does not qualify combat. Its retry reaches the first
Ruins goblin encounter before newly appended late-confirm pulses are needed;
these are not established as a fix for the timing variation. The final image
shows Felix, a goblin, the chest and readable Ruins background. It captures
6,848 frames, completes the 600-vblank settle and unloads without runtime,
frontend, save or platform error. Real 256-frame / 48 kHz callbacks consume
5,836,800 frames with `audio.output_muted=true`, zero underruns and zero clipping.
The settled short fight is **51.63 fps**, p95 **33.33 ms**, in fair thermal state.
Native CRILAYLA executes 122 fast calls / three translated fallbacks in this run.

A cold process reuses that same isolated diagnostic save without reseeding,
walks in recognizable Town and captures 4,591 frames. Its 3,000-vblank settle
is **51.43 fps**, p95 **33.39 ms**, also fair thermal state. The save remains
exact `400fed80…`. Another cold Town run retains that hash, captures 5,202 frames
and ends in serious thermal state. An attached Time Profiler records 25.07
seconds before ordinary diagnostic completion exits its target; that run is
excluded from uninstrumented FPS acceptance. The first 23 seconds have 50,199
weighted samples: 41.84% in lock/wake/wait functions, 9.21% in full texture
comparison, and 6.50% directly in trace-entry bookkeeping. Symbol resolution
confirms unfair locks/ulocks and `memcmp`; most lock paths lead through scalar
memory bridges, while comparisons lead through the texture snapshot hook.
These are statistical sample weights, not CPU durations or proof of one
specific optimization. Remaining work should target those measured costs and
preserve coherent texture reads, guest atomics and cancellation.

The exact Simulator touch-earned `R` save (`df59b068…`) is then staged once
in a new diagnostic UUID, leaving all earlier sessions intact. Two new physical
processes reopen that same working root without reseeding. Their final images
show R at 300 HP in settled Town; both preserve the exact save hash, capture
2,804 / 2,855 frames and finish with empty runtime/frontend/save/platform errors
and zero audio underruns/clipping. Output remains muted. Their neutral-input,
1,200-vblank idle-Town samples are **59.35 / 59.05 fps**, at nominal / fair thermal
state. The shorter idle route differs from the walking/combat route, so it does
not establish that cooling alone explains the timing difference. This qualifies
physical consumption/persistence of a new Simulator-earned save, not creation
or touch input on hardware.

Private proof is `cursor-investigation/phone-input-peek/`, including
`qualified-runs.json`, per-run reports/images/save hashes and
`town-cpu-{toc.xml,summary.json,symbols.json,attribution.json}`. A profiling
configuration above the existing 3,600-frame bound rejects before game execution;
that attempt and a name-based attach failure are excluded. Physical XCTest still
fails enabling automation before test methods run. The optional LLDB probe also
fails to attach and is ended; it supplies no input/power-state evidence. The
launcher is restored with mute enabled. The physical iPad, normal player saves
and live cloud progress remain untouched. Full hardware fresh/adventure/input,
interruptions, physical Files, remote sync and sustained Town/combat gates remain
open; the port is not declared complete.

## 2026-11-15 — Fresh touch-only tutorial, door/chest and earned-save reloads

Clean packaged `280b856a08eb7447102c8ace8285ff03fbbd2baf`, with the unchanged
50,464-entry frozen core, completes a fresh Fighter tutorial through actual
UIKit touches in the owned iPhone Simulator. The game keyboard edits the name
to `R`; this does not exercise the native IME adapter. All movement/basic/aerial/
guard/Power Smash/crossbow lessons pass, followed by potion selection/use
(three → two), food (300 → 330 HP), a direct tap on the locked door and another
on the chest. Rannie unlocks both. Ordinary controls defeat the Orc and the
character returns through the Inn to recognizable Town at 300 HP / 100 gold.
Leaving the Inn writes new save `df59b068…`, from empty-save `625651e2…`.
No guest scene, cursor or save fields are patched. See the repeatable
[touch acceptance route](touch-playthrough.md).

A held two-finger Move-right/Square read-only snapshot has raw buttons `0x8000`,
left X/Y `239/127` and all four physical pads disconnected. The private helper
now synthesizes multi-touch with the app's actual interface orientation; the
earlier hardcoded-portrait attempt remains excluded. A private shim only
suppresses Mac-forwarded controllers and is not shipped. An earlier attempt
ends during potion work with SIGBUS; its crash report says the mapped backing
vnode was force-unmounted, and another Simulator game process fails at the same
time. The owned Simulator restarts without replacing saves. Its empty file is
unchanged; the successful new process recreates the character in that same root.

The completed run captures 45,844 frames / 34,132 cursor samples with zero
cursor read errors and empty runtime/frontend/save errors. Repeated audio
restarts also record AVFAudio `-10851` and 5,632 underrun frames. This guided run
is not audio, thermal or sustained performance qualification. At the user's
request the existing Simulator mute setting is enabled during this run and
retained on all later launches. Real stream consumption/accounting continues.

Three cold processes reopen the same new working save without reseeding. All
show `R` at LV1 / 100 gold and retain exact SHA-256
`df59b0681ff021bf4609361f0abb8d6b50f895a9902db701d09cda91bccb1fb4`.
The first qualifies visible Inn; its early Town capture is a transition image
and is excluded from visual Town acceptance. The next two wait for settled,
recognizable Town, capture 6,076 / 6,962 frames, stop through Pause → Stop Game
and retain empty runtime/frontend/save/platform errors. Both remain muted and
count zero underruns/clipping, without an audibility claim. The input helper
terminates the owned game and finishes teardown: xcodebuild exits 0 with one
test, zero failures and `TEST SUCCEEDED`. The private helper now has a real
host window and returns Home after termination. This successful teardown does
not retroactively qualify earlier interrupted suites.

The private proof is `cursor-investigation/touch-tutorial-driver/`, including
`touch-tutorial-final.json`, `cold-{1,2,3}-final.json`, screenshots, input proofs,
command journal and immutable earned-save copy. Physical devices, normal
player saves and live cloud progress remain untouched. Physical fresh-character/
adventure/save, controller/reconnect, audio recovery, 30-minute performance/
thermal and first-use/remote-provider handoff gates remain open.

## 2026-11-15 — Full-screen pointing survives touchpad edges and rotation

Runtime `7bc7603362bbf4ae75cee96689dd1385a1265442`, packaged from clean
`280b856a08eb7447102c8ace8285ff03fbbd2baf`, fixes a tablet follow-up failure:
small screen moves worked, but distant targets repeatedly re-contacted without
moving. The original pad parser calls ReadState, then GetControllerInformation.
Both previously consumed the pointer loop; the metadata call could discard the
edge step before the next real read. Actual guest records alternated contact /
no-contact at pad centre with zero delta. An independent model reproduces the
exact `(211,-293)` → `(211,380)` stall. Metadata now uses a separate connection
peek; hosts without it use the last valid read. Invalid output buffers reject
before consuming input. No guest scene/cursor/save fields are patched.

Normal/ASan/UBSan offline-service tests cover read/query counts, connection
changes, bounds, fallback and existing audio/save behavior. The headless script
now includes those fixtures, with 42 configuration checks and 141 scripted-input
checks in each normal/sanitized run. The cursor probe passes repeated metadata
queries, long moves/re-contacts, four gains and normal/cancellation cases; its
TSan run also passes. Both SDK adapters compile/link. All three frozen cores
retain 50,464 entries, complete current/generation backend provenance and native
CRILAYLA/libc paths. Both app bundles include compiled icons; device signature
verifies without memory entitlements. The icon packer also avoids an empty-array
expansion rejected by macOS system Bash when no upscale flag is needed.

Actual UIKit input in the iPad mini (A17 Pro) Simulator reaches three portrait
targets and, after rotation back to landscape, the opposite corner. Each held
read-only snapshot is within 0.4 pixels of its target. The same run loads the
existing Darrel LV2 save, uses Town Map/Gate, enters Ancient Temple Ruins,
clears the first encounter/crates and opens the first chest with a short
screen tap. Rannie unlocks it and Treasure ranks appears; Cross dismisses the
tutorial. Public Pause → Stop Game returns to launcher after 41,403 captured
frames / 40,198 cursor samples with zero read errors and empty runtime/frontend/
save errors. The working save retains exact `728f0ec…` bytes. Mac-forwarded
controllers are suppressed only in the private UI test. A synthetic multi-event
combo is not counted as simultaneous-control qualification. The input body
acknowledges and terminates the owned game; XCTest teardown stalls separately
and is interrupted, without an official suite-pass claim.

A separate native cold process reaches visible Town at 303 HP, renders 1,915
frames through 2,525 vblanks and unloads with empty errors and exact save bytes.
The first attempt reaches Town but exhausts the default trace budget; a
five-billion-entry harness limit allows the 900-vblank settle to finish on the
same working save, without reseeding. These guided/offscreen runs do not qualify
physical performance or sustained play. Simulator audio counts zero underruns /
clipping, without an audibility or physical-audio claim. Touch-only door/full
fresh tutorial, physical controls/audio, sustained thermal and remote-provider
gates remain open. Both physical devices and normal/live-cloud saves stay
untouched. Private proof is `cursor-investigation/input-peek-verification.json`
and `input-peek-target-verification.json`.

## 2026-11-15 — Screen pointing and actual touch-only chest opening

Runtime `ce065bbb9e58337d64eedc3e580becf94fb26ba0` connects the mobile
screen pointer to the existing Mac relative-touch loop. A checked read-only
observer samples the original cursor at its normal player-zero pad-record
call. A short screen tap steers to its target, then emits the ordinary
touchpad interaction; dragging moves without a click. Raw touchpad/Map input,
pause, cancellation and reset cancel queued pointer actions. Other fingers
retain virtual controls. No guest scene/cursor/save fields are written.

The independent cursor/input model passes normal, ASan/UBSan and TSan runs,
including four sensitivities, bounded targets, tap/drag and simultaneous
direction/action, Map priority, cancellation and coherent snapshots. Both
SDK adapters compile/link, the normal 14-test Mac suite passes, and clean
iPhoneOS/Simulator apps package with compiled icons. Device signature verifies
without memory entitlements. All three frozen 50,464-entry cores retain complete
current/generation backend provenance and native CRILAYLA/libc fast paths.
An actual native cold Town reload captures 2,525 frames / 1,894 cursor samples,
with no read error, clean unload and the exact earned adventure-save hash.

Actual UIKit touches in the dedicated iPhone Simulator load Darrel LV2 / 248
gold, use Town Map to reach the Gate, enter Ancient Temple Ruins and clear the
first goblin encounter/crates with virtual controls. A held screen target has
less than 0.27 pixels of measured cursor error. After the camera settles, a
short screen tap on the first chest makes Rannie unlock and open it; the
original Treasure ranks tutorial appears. Cross dismisses that tutorial.
Public Pause → Stop Game returns to launcher after 15,014 captured frames,
with 13,149 cursor samples, zero read errors and empty runtime/frontend/save
errors. The isolated save remains byte-identical (`728f0ec…`). Earlier taps
aimed at a pre-settle screenshot and missed the moving chest; no source change
was needed. The private test shim only suppresses Mac-forwarded controllers.
The input body acknowledges all commands and terminates the owned game, while
XCTest teardown still stalls separately; no official suite-pass is claimed.

This qualifies Simulator screen targeting and chest interaction, not the
touch-only door/full fresh tutorial or physical input. The real Simulator
audio adapter reports zero underrun frames and clipping in this guided run;
audibility, interruption/output recovery and physical audio remain open.
Lifecycle pauses and read-only debugger snapshots exclude a sustained timing
claim. Both physical devices and normal/live-cloud saves remain untouched.

## 2026-11-15 — Touch Town Map and destination travel

Source `d1da89a` changes the development overlay's Pad control to Map. The
original game requires a touchpad click with its first contact on the left
half; a bare click opens the character menu. The Mac adapter already supplies
the matching `(300,471)` contact. Mobile now publishes that left-half contact
first while Map is held and releases it with the touch. Menu retains the
ordinary Options input. No guest scene or cursor fields are changed.

Both SDK adapter compile/link checks and core input/audio probes pass; both
SDK apps package from clean source with the existing frozen 50,464-entry core.
Device signing verifies without memory entitlements. The dedicated iPhone
Simulator uses actual XCTest touch events and a separate disposable copy of
the adventure save; Mac-forwarded gamepads are suppressed only in that private
UI test. Touch loads Data 1 at LV2 / 248 gold, enters Town, opens Map, selects
Adventurers Guild with the virtual D-pad and confirms travel into the guild.
The post-adventure dialogue advances. A second Map opening selects Canaan
Temple and confirms travel into that facility. The public Pause → Stop Game
flow returns to the launcher with 18,457 captured frames and empty runtime,
frontend and save errors. Its isolated save still has exact `728f0ec…` bytes.
The input test body acknowledges all commands; XCTest teardown is tracked
separately and is not an official suite-pass claim. Touch-only finger-pointer
door/chest use and physical map/control qualification remain open. No physical
device or normal/live-cloud save is touched.

## 2026-11-15 — MOVBE executes, spoils complete, adventure saved

Clean source `4d90913` runs the complete native Ancient Temple Ruins route,
collects the quest bones and defeats the Harpy. The Adventure Results screen
shows completion bonus 2,500, total score 7,873, two C-rank treasures and
1,075 gold minus a 50-gold Continue charge. Stats levels Darrel to 2 with
303 maximum HP and 902 EXP. A private read-only marker at the unique original
MOVBE byte-swap operation records `20491ec3` executing at vblank 48,299.
The game advances past the former fault to Appraise spoils. Both items are
appraised for 370 / 407 gold and obtained; inventory count is two and gold
is 248. The first is visibly named Crushing Axe Blade.

The game returns to visible Town. Ordinary input enters Dragon's Haven Inn,
chooses Report adventures, Data 1 and Yes, then shows Save completed. The new
SAVE0.DAT SHA-256 is
`728f0ecb5c92c57a833b731b9b2943bc059d2abd2383a043a280e6c8cf937bac`.
The complete guided run renders 52,139 frames / 52,432 vblanks. Its
1,699.077-second wall time includes public-API lifecycle pauses and is not
sustained performance evidence. After the explicit save, an owned SIGINT
requests shutdown: unload is acknowledged, host/guest errors are empty and
shutdown grace does not expire. The harness's UNREACHED checkpoint is
intentionally unmet, so its generic checkpoint-success boolean is false;
this manual-stop result is not being counted as a checkpoint pass.

The normal frozen core, without private input or execution observers, is used
for separate native cold reloads. Packaged Simulator cold reloads use the clean
SDK app. Working saves are seeded once per environment; no reseeding occurs
between cold processes. Native runs 1 and 3 show visible Town at 303 HP,
rendering 1,924 / 2,525 frames; both retain the exact new hash and pass their
checkpoints with empty host/guest errors and clean unload. Run 2 retains the
hash and passes its component checkpoint but ends with the loading overlay;
it is retained as checkpoint-only evidence, not a visual Town pass. Run 3
uses a 900-frame settle interval. Two packaged Simulator cold processes show
visible Town at 303 HP, render 2,022 / 1,855 frames and retain the exact hash
with empty runtime/frontend/save errors. Both resume the same isolated save
session without reseeding. All Simulator reloads use the paced null sink. Physical fresh-character,
adventure, controls/audio, sustained/thermal and cloud-provider gates remain
open; neither physical device is installed or launched during this work.

## 2026-11-15 — Ruins combat, results and reward coverage

Continuing from the earned guild-progress save, native Darrel passes the first
encounter, pit vipers, spiders, bats, the waterfall corridor and lizardmen,
collects the quest bones and reaches the Harpy boss. Ordinary item controls
consume Heal Potion (S): HP changes from 85 to 175 and quantity from 3 to 2.
A later use also increases HP by 90 and reduces the quantity again. This
qualifies native ordinary potion use; touch-only and physical use remain open.
Short right-stick pulses plus L1 also open a Ruins chest.

The first interior discovery stops at missing `202e0e5c`. Original ELF
`2e0c40` uses paired six-case signed-relative tables at `71822c` / `718244`,
with unsigned index <= 5. Source `ff04004` requires all twelve destinations;
six getters were already present and six setters are new. A separate bounded
replay logs actual execution of `202e0e5c` at vblank 14,267 and continues to
28,329 vblanks / 28,327 rendered frames in 480 seconds, with empty host/guest
errors, acknowledged unload and unchanged guild-save bytes. The individual
status effects have not been named from this evidence.

The guided adventure then renders 40,701 frames through the Harpy fight,
death, automatic revival and the Continue prompt. It stops after Continue at
missing `20519de5`. Original ELF `519d70` loads a zero-extended state byte,
guards it unsigned <= 9, then dispatches through ten signed-relative entries
at `75a610`. All ten destination instructions and executable mappings are
verified against the supported ELF. Source `cc12a9c` requires the complete
switch. Its frozen 50,425-entry core builds for Mac, iPhoneOS and Simulator;
all ten archive symbols and library hashes verify. Both icon-bearing SDK
apps package from clean source; iPhoneOS signing verifies without increased
memory or extended-VA entitlements. A second native run visibly defeats the
Harpy. Read-only probes record
`20519de5` at vblank 39,918 and `2051a17d` at 40,099, so the first repaired
state target is now executed. Victory also reaches this owner: the evidence
does not identify it as an exclusively Continue-specific routine. The run
then stops at missing nested target `2051c06e`, before rewards/return/save.
It renders 39,516 frames, unloads cleanly and retains the prior guild-save
hash. All four indirect dispatches in ELF `519d70..51d520` are now inspected.
The three nested tables at `75a648`, `75a674` and `75a638` have unsigned bounds
10, 14 and 3 and twenty distinct destination instructions. These are added
together in a frozen 50,445-entry core, which builds for all three targets.
The next native run executes formerly missing `2051c06e` at vblank 42,552,
then thirteen more nested result targets. Adventure Results visibly shows a
2,500-point completion bonus, 8,438 total score, 1,161 gold and two C-rank
treasures. The subsequent Stats screen levels Darrel to 2 (303 maximum HP,
896 EXP). Advancing past Stats stops at `2001d410`, before the next reward
screen/return/save. The run renders 43,966 frames and unloads cleanly with
empty host error; its prior guild-save hash remains unchanged. Rewards and
new adventure-save persistence remain open despite the verified results and
level-up screens.

Original ELF `1cff0..1d5c0` contains that new missing target inside three
loop switches. Preloaded RBX/R13 table bases `645080` / `645060` / `645040`
are retained through their loops and ordinary preserved-register calls.
LEA-to-EAX subtracts 12 and zero extends before unsigned bounds 8 / 7 / 7.
All nineteen distinct target instructions are verified and required by
bootstrap. The frozen 50,464-entry core builds on all three targets. Its next
native run executes `2001d410` and six other reward targets at vblank 43,072.
Normal paid continuation also resurrects the character; Adventure Results
records the 50-gold charge, leaving 1,060 gold. The run then stops at an
unsupported instruction at `20491ec3`, before the next reward screen. It
renders 42,318 frames and unloads cleanly, preserving the original guild-save
hash. This closes the observed reward-target coverage gap, while reward-flow
completion, return and a new adventure save remain open.

The original instruction is `movbe qword ptr [rcx],rdx`, inside ELF
`491c20..492080`. Remill already implements byte swaps for the load forms but
omits the store registrations. A patch adds all three memory-store widths
without changing the shared semantics or arithmetic flags. The isolated
[review](cpu/review/movbe.md) tests 24 synthetic routines on 64 vectors against
Rosetta and independent expected bytes/registers/flags. Native arm64 and
ASan/UBSan each pass 21,552 checks, including 48 checked-memory boundary checks;
the shared backend remains unchanged during that proof. The patch is now in
the ordered bootstrap and its review is an acceptance stage. All 50,464 translations are now relifted on the updated backend. The frozen
Mac, iPhoneOS and Simulator cores report complete current and generation
provenance, and retain native CRILAYLA/libc fast paths. Clean `4d90913` packages
both icon-bearing SDK apps; iPhoneOS signing verifies without increased-memory
or extended-VA entitlements. The next run actually executes the corrected
instruction and completes rewards, Town return and an explicit new save as
recorded below.

The guided run pauses through the public lifecycle API between input batches.
It modifies no guest scene, health, cursor or save fields. Its 2,926-second
wall time includes those pauses and is not sustained-play or performance
evidence. The collected input history is only a replay candidate until checked.
Both physical devices remain released; no install or launch is attempted there.

Packaged `cc12a9c` cold-reopens the same isolated guild-progress save twice
without reseeding. The 300-frame settle checks render 1,871 / 1,886 frames,
show Darrel in visible Town, retain the exact `04855cc…` hash and stop with
empty runtime/frontend/save errors. Earlier 120-frame checks stopped while
the loading logo remained visible; those are retained as checkpoint-only
evidence and do not count as visual Town acceptance. Simulator audio is the
paced null sink. The long unattended Ruins replay diverges from its guided
source and is continued with ordinary live input; it is not an unattended
acceptance route.

## 2026-11-15 — Fresh tutorial clear and earned-save reloads

A fresh Fighter, Darrel, completes the introductory tutorial in the native
arm64 AOT harness: movement, attacks, guard, Power Smash, crossbow encounter,
item lesson, apple consumption, locked door, chest and tutorial Orc boss. He
then reaches visible Town with the Adventurers Guild objective. The bounded
1,800-second run renders 106,758 frames and ends with empty host/guest errors,
acknowledged unload and no shutdown-grace expiry. This is a tutorial clear,
not a successful Ancient Temple Ruins adventure.

Short right-stick pulses aim the finger cursor and ordinary L1 selects the
door/chest. Earlier long holds drove the cursor to the screen boundary. A
private read-only observer confirms the original input conversion and cursor
coordinates; it does not write scene, cursor, health or save fields. The
successful run uses ordinary pad input with live timing adjustments. Its
collected input history is not yet a repeatable unattended acceptance route.
The touch contact experiment did not establish absolute screen pointing.
Touch-only door/chest interaction and potion consumption remain open.

The game writes Darrel's new progress with SAVE0.DAT SHA-256
`5c03436b3c67ae0ce543ac4c206412fe8c881795c93e1a2647c038e731a8a81d`.
Two separate native cold processes reopen the same working save without
reseeding, show Darrel in Town and retain that exact hash. A copy is then
placed once in a new isolated iPhone Simulator diagnostic save session.
Two cold launches of packaged build `34e4a0b` resume that same session without
reseeding, reach checked and visible Town, render 1,890 / 1,842 frames and
retain the exact newly earned save hash. Original Simulator player saves and
seeds are untouched. The Simulator reload checks use paced null audio.

Real platform audio on this dedicated Simulator aborts before the first frame
with an AURemoteIO Cleanup RPC timeout while obtaining AVAudioEngine's main
mixer. A separate minimal UIKit/AVAudioEngine app reproduces the same abort
without the game runtime. The Simulator audio-startup failure remains under
investigation; null-audio save checks do not qualify audio recovery or output.
After standalone audio configuration tests, three unchanged baseline probe
launches complete. Two cold game launches then reach Town with real platform
audio callbacks and retain the exact Darrel save hash. They render 1,809 /
2,020 frames; the first counts 1,280 underrun frames and the second zero, with
no clipping. Simulator output is muted after consuming the real ring. The
intermittent earlier timeout is retained; its cause is not established and
this does not qualify audible output or interruption recovery.

Continuing Darrel's normal quest reaches the guild, repairs the sword at
Morgan's shop and accepts the entry exam. Opening the equipment menu then
stops at missing trace `204cf906`, after 34,354 frames, with all workers
unloaded and the prior tutorial save intact. The original function at ELF
`4cf860` guards the six-character index with unsigned `<= 5`, then loads
signed relative targets from table `757ee4`. All six destination instructions
are verified, and bootstrap now requires their translation. The new coverage
builds as a frozen 50,409-entry core for Mac, iPhoneOS and Simulator. Both SDK
apps package cleanly from `3276ecb`; device signature and entitlement checks
pass without increased-memory or extended-VA entitlements. All six symbols
and the archive hashes verify on each target. Headless checks pass: input
141 native / 141 ASan-UBSan, configuration 42, vblank timing 1,006, and actual
runtime rejection/budget checks 32 across eight cases.

Darrel now opens the Equipment screen after repairing his sword in the native
run. An independently edited Feli save also opens Equipment in the packaged
iPhone Simulator. That bounded test captures 6,829 frames / 6,896 flips, stops
with empty runtime/frontend/save errors and retains its exact `9069666…` save.
A compressed quest replay missed a navigation trigger and is not qualified;
the native check uses live guidance and the Simulator uses a focused menu route.

Continuing Darrel accepts the entry exam and unlocks the Gate. Merely visiting
and leaving the Inn did not write these quest changes. The Inn's Report
Adventures command saves Data 1 explicitly: its displayed gold changes from
100 to 0 after the sword repair, and the game reports Save completed. The new
SAVE0.DAT SHA-256 is
`04855ccd81cf2b90ea19a4ecf4f8495c74df2e353912f2b5e3d776d9ed84a3de`.
The 1,200-second native run finishes with 70,634 frames, empty host/guest
errors, acknowledged unload and no grace expiry. Two native cold launches
then reopen the same working folder without reseeding: each renders 1,923
frames, passes the Town checkpoint and retains that exact new hash. After one
copy into a new isolated Simulator save session, two packaged cold launches
render 1,841 / 1,859 frames, reach visible Town and retain the same hash without
reseeding between them. Their new narration concerns proving the character's
mettle, rather than the earlier arrival introduction. Simulator reloads use
null audio. The icon-bearing SDK apps retain the same qualified source/core.

The game's Game Options 3 screen identifies the default touchpad cursor mode
as Relative position. Absolute screen pointing remains unqualified; do not
infer it from right-stick cursor success. Town-map touchpad input also needs
separate qualification.

Both physical devices remain released for the other chat. Full fresh-character
play on each device, an adventure clear, touch/controller acceptance, audio
interruptions/routes, sustained thermal play and cloud handoff remain open.

## 2026-11-14 — Manual save copies through Files

The stopped-game launcher now offers Transfer Save with Files. Export creates
an independent SHA-256-inventoried folder, then uses the system copy/export
picker. Import stages and validates the selected folder before checkpointing
and replacing local progress. Existing inventories must match the copied
bytes. Missing/empty slots, symlinks, overlapping roots and pending write
journals reject. Storage actions and game execution remain mutually exclusive.
The action sheet dismisses before picker presentation. Picker callbacks are
checked against their active picker, and an in-progress local transaction
finishes before releasing storage ownership. The last selected save folder is
only a session-local picker hint, without a persistent cloud bookmark.

The storage suite passes 149 checks and the integrated savedata service passes
108 checks across four simulated relaunches. Device and Simulator SDK builds
compile. A dedicated Simulator exported the native combat-written save through
Files. Its exact SAVE0.DAT hash and full checkpoint inventory survive that copy.
The exported folder also passes a host-simulated three-hop handoff. Importing
it over the independently edited Feli save preserves Feli in a backup. These
host hops do not represent phone/iPad/cloud-provider execution.

The dedicated Simulator also exports the Feli save through the local Files
provider, then imports it over the different native combat save. The picker
starts at the remembered export location. Import restores Feli's exact
`9069666…` hash and retains the replaced `affc24e…` hash in both a completed
checkpoint and the previous tree, with no active journal. An earlier invalid
folder selection reports an error and leaves local progress unchanged.
Top-level app-container rows did not navigate in the automated Simulator test;
first-use provider/container browsing remains unqualified. The successful
round trip used the picker-granted export URL as its initial directory.

A fresh native Connor replay again passes crossbow combat, the compound item
lesson and food consumption at 330/300 HP. It renders 58,645 frames in about
1,004 seconds and stops cleanly at the locked-door lesson, with empty host and
guest errors and all runtime workers unloaded. The cursor-movement objective becomes checked during scripted right-stick
input, but opening the door remains unverified. Full
tutorial completion, ordinary potion use and new-character persistence after
Town remain open. Both physical devices stay released for the other chat.
First-use provider browsing, remote iCloud handoff/conflicts and sustained
hardware play still need qualification.

## 2026-11-14 — Item lesson, food switch and scripted right stick

A live-guided x86-64 Mac reference used disposable local user/save folders
with the Mac-only story, tutorial and warning skip offers disabled. The
reference and arm64 AOT runs both passed movement, ground/aerial attacks,
guard, Power Smash and the crossbow encounter. A compound controller sequence
advanced the item lesson in both runtimes. An enlarged HUD capture identifies
the selected item as the Buckler. Potion consumption and ordinary directional
item selection still need separate qualification.

The AOT run broke the food box and picked up its apple, then stopped at
`202ab640` when eating began. Source `7f3e2f7` compiles all six verified
destinations of the bounded food switch at ELF `710570`. The 50,403-entry
core packages for iOS and Simulator. A fresh native run consumed the apple,
raised Abby's HP from 300 to 330 and reached the locked-door/Rannie lesson.
Its 1,400-second diagnostic ended there with an empty runtime error.

The headless script now accepts `RS_LEFT`, `RS_RIGHT`, `RS_UP` and
`RS_DOWN` alongside the existing left-stick controls. Both desktop and AOT
pad adapters convert those flags into axes and clear the flags from guest
buttons. The expanded input fixture passes 141 checks in native and
ASan/UBSan runs and compiles for the iOS SDKs. All 14 x86-64 desktop
regressions pass. Real-game scripted cursor movement is the next check.

The iPad Simulator also created a new level-one Fighter, Gerald, and reached
crossbow pickup using the food candidate. The fixed replay's pickup positions
required adjustment. Food consumption in Simulator remains open. Its
ordinary saves, checkpoints and diagnostic seed retain their exact hashes,
and the original diagnostic input was restored after stopping that test.
Both physical devices remain released. Tutorial completion, a successful
adventure clear, sustained hardware acceptance and cloud handoff remain open.

## 2026-11-14 — Character-edit switch and post-write recovery

The iPad Simulator's original game keyboard shortened the disposable name from
Felix to Feli. Leaving the Inn saved it. A cold process reopened Feli with the
exact new SAVE0.DAT SHA-256
`9069666229e360dd969bbe0fe1953589f2f8ecfe55f2d0107e5556dfe669d05d`.
This exercises the game's keyboard through the touch overlay. The native
UIKit IME was not invoked on this route.

A subsequent armor edit stopped at missing target `203aea16`. Source
`5ad93eb` adds every destination of its original four-entry relative switch
at ELF `74dce0`. An unsigned index guard, signed relative loads and target
instructions were verified before translation. The frozen core has 50,397
entries and packages for both iOS and Simulator. Reinstall retained the edited
name and its save hash.

With that build, another armor edit reached an actual savedata write. The
watcher stopped and killed the Simulator app after SAVE0.DAT changed to
`af7dce1f69b43594f912734beaec402910441c3333d21018d87e0e14fafba1f4`
while its save journal remained active. A cold launch recovered the exact
prior `9069666…` save, cleared the journal and displayed Feli with Burnished
Bronze. The retained interrupted file matches the captured `af7dce1…` hash.
This qualifies interruption after guest bytes changed and before transaction
commit, alongside the earlier prewrite case. Physical interruption remains open.

The fresh-Fighter native replay also passed crossbow pickup, firing and the
goblin encounter. It stopped at potion selection/use until its 1,400-second
deadline. No missing trace was reported there. The cause is unresolved.
A disposable x86-64 Mac reference replay is comparing the same control route.
Both physical devices remain released.

## 2026-11-14 — Fresh Fighter creation and changed-save cold reloads

Build `7f7298f` compiles all four verified destinations of the character-selection
switch. The 11 signed table entries resolve to four unique targets. Instruction
and table bytes were checked against the extracted ELF before compilation.
The frozen core contains 50,393 entries and packages for iOS and Simulator.

Touch input in the iPhone Simulator created a Fighter in an empty slot and
entered the introductory tutorial. The native arm64 harness independently
created Willie from empty save data and passed movement, ground attacks,
aerial attacks, guard and Power Smash. Tutorial completion remains open.

On the iPad Simulator, touch input changed Felix's armor from Sterling Silver
to Burnished Bronze. Leaving the Inn wrote SAVE0.DAT with SHA-256
`91dfab154867eb3dd3e8ec4653b777466f22e8cb6bd9c340166164f88d095890`,
different from the original seed. Two cold launches resumed the same working
folder without seeding. Both reopened the character editor with Burnished
Bronze visible and the exact saved hash. Three completed checkpoints existed
and the write journal was absent after the save. Normal player saves and the
seed remained unchanged. These runs used a test-only Simulator shim to hide
the Mac-forwarded controller and exercise the shipping touch overlay.

The arm64 combat route also passed the spoils exit and RTC call, reached the
Town scene checkpoint after GOBLIN and wrote a changed SAVE0.DAT
(`affc24e12f718663174ec55860521458eb1a1fcf7b766c6146c03755c50a5e36`).
Its final captured image was still black during transition. A separate iPad
Simulator working folder received a copy of that native save. Two cold launches
retained its exact hash, reopened Felix and reached visible Town. The save was
produced on the Mac runtime. A successful adventure clear and the complete
combat-to-save sequence on physical devices remain open.

A separate clone of the armor-edit save also passed Simulator recovery.
Felix was changed from Burnished Bronze to Divine Gold. Leaving the Inn
published the real save journal. An external watcher stopped and killed
only that Simulator game process while the journal existed, before guest
data changed. Relaunch cleared the journal, kept an interrupted checkpoint
and restored the exact prior SAVE0.DAT hash. The editor visibly showed
Burnished Bronze. This qualifies the prewrite interruption boundary.
Termination after partial replacement and physical interruption remain open.
Both physical devices are released for another chat.

## 2026-11-14 — iPhone Town, existing-save reload and Simulator lifecycle

On iPhone 17 Pro Max, the 3,000-frame Felix Town walk with platform audio
reached 57.87 fps. Median and p95 replay intervals were 16.68 and 18.45 ms.
The replay covered all 2,999 guest flip intervals. Two separate cold launches
reopened the same working save at the Inn. The Felix slot retained its original
save hash. The simulator touch overlay loaded that slot, exited the Inn and
walked along the street. Pressing Home paused guest execution and audio;
reactivating resumed both in the same session. The on-screen Pause, Resume
and Stop Game controls returned to the launcher with no guest or save error.

The arm64 host reached the adventure spoils screen with 123 gold. Exit from
that screen called a missing routine at `2063b950`. The bootstrap includes
that observed target and the Town movement target `203d54a0`. Qualification
of the resulting save requires an on-device return and cold reload.

## 2026-11-14 — First Ancient Temple Ruins fight on both physical devices

The same Felix slot and scripted pad route reached TITLE, INN, Town, the
magic gate and the first goblin encounter on the physical iPad mini (A20 Pro)
and iPhone 17 Pro Max. A gate capture on the arm64 host shows the stage
selection by name. Device images show the Ruins, sword attacks, the caged or
freed fairy and score gains. The phone showed 365 points and the iPad 245.
The iPad recorded 215 active enemy-component updates and a visible damage
number. These short windows cover the first room.

| Device and signed build | Replay intervals | Rendered fps | Median | p95 | p95 host replay work |
| --- | ---: | ---: | ---: | ---: | ---: |
| iPad mini, `bb1d95a` | 600 | 59.40 | 16.67 ms | 16.90 ms | 3.14 ms |
| iPhone 17 Pro Max, `6b0302b` | 599 | 54.28 | 16.70 ms | 33.13 ms | 7.60 ms |

Neither device skipped a guest flip between its first and last measured
replays. Real 48 kHz platform output ran on both, with 256-frame callbacks
and zero counted underruns or clipping across the complete runs. A separate
3,000-frame Town walk on iPad with real audio measured 59.42 fps and 16.83 ms
p95. The iPhone Simulator also reached the encounter. The external arm64 route
continued through Roland, snakes, spiders, adventurer bones and lizardmen.
The tested input routes and launch settings are in [diagnostics](diagnostics.md).

## 2026-11-14 — Town frame budget on iPad mini

Signed build `c8b7c9f` completed two consecutive 3,000-frame walks on the
physical A20 Pro iPad mini. The starting save, boot input and scene-relative
walk match the earlier 45.3–45.4 fps runs. All 3,000 flips were replayed in
each window. The final captures show Felix in the street with the HUD.

| Run | Thermal state | Rendered fps | Median interval | p95 interval | p95 replay work | Process CPU per walk |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| First | nominal → fair | 59.40 | 16.67 ms | 16.82 ms | 2.82 ms | 69.30 s |
| Immediate repeat | fair → fair | 59.38 | 16.67 ms | 16.84 ms | 2.74 ms | 67.33 s |

The AOT texture snapshot budget increased from 64 to 192 MiB. An intermediate
build reached 52.32 fps with this change. The renderer checks the entire
texture and combines adjacent sprite primitives with identical state. Following
an on-device CPU profile, guest threads now back off after repeated yields,
cache whole-page range checks and synchronize large atlas comparisons in
256 KiB bands. Each translated unit can fold the runtime's pure flag helpers.
With the other changes in place, the walk used 84.98 process CPU seconds
before flag folding. CPU time sums all threads. Per-frame replay work measures
host processing through submission and excludes asynchronous GPU execution.

An earlier trial under serious thermal pressure measured 42.67 fps. The final
build peaked at 2.33 GB across these short walks. Both used scripted input
and paced null audio. Each finished with zero runtime, frontend or save
errors. The seed and ordinary save files retained their pre-run hashes and
the device returned to the normal launcher. Combat, long-session thermal
behavior, physical iPhone performance and interactive audio remain open.

Native Metal regressions passed 49,152 channel checks, with another 36,864
channels independently checked from PNGs. Checked guest memory passed
267,095 assertions per native, ASan/UBSan and TSan build. An active-Town
pause/resume/stop completed on the native host, which rendered 58.79 fps
on the same walk. All 14 x86-64 Mac regression tests passed.

## 2026-11-14 — Physical iPad Town walks at 45 fps

Signed build `34e793f` ran the same 3,000-frame Felix walk twice on the
iPad mini (A20 Pro), iPadOS 27.2.1, with the 50,340-entry core and a fresh
disposable copy of the same save each time. Both runs reached TITLE, INN and
Town. Neither left Town during its measurement window. The final images show
Felix, the street and the HUD. Counting completed Metal replay intervals gives
the following results.

| iPad walk | Seconds | Rendered fps | Guest fps | Median interval | 95th percentile | Skipped flips inside window |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| First | 66.00 | 45.42 | 45.44 | 17.19 ms | 34.16 ms | 1 |
| Repeat | 66.12 | 45.34 | 45.38 | 17.28 ms | 34.46 ms | 2 |

The game called the CRILAYLA decoder 94 times on each route. The native
implementation handled 91 in-place calls and decoded 283,831,168 bytes.
Three calls used the original translated routine. In isolation, the revised
decoder shortened the host boot-to-checkpoint run from 327 to 220 seconds,
while settled Town still ran at about 5 fps. Differential tests compared
four input/output layouts with the original routine, using eight game
records and 113 generated cases in native, ASan/UBSan and TSan configurations.

The larger frame-rate gain came from reducing contention and repeated work.
Guest memory checks use per-thread mapping caches and ordered page locks,
with full synchronization for guest atomic instructions. Metal reuses a
previous texture only after checking its complete contents. The intermediate
iPad walk measured 27.29 fps. Cheaper scalar memory checks, fast polling for
clear runtime signals and eliminating redundant presents brought it above
45 fps. The final host run measured 57.17 rendered fps on the same walk.
An earlier input script returned to the Inn and was excluded.

Both iPad runs ended in fair thermal state, at sampled peak footprints of
2.17 and 2.18 GB. They reported zero guest, frontend and save errors.
The previous 60 and 70 diagnostic files, respectively, retained their hashes,
as did the seed and ordinary player files. The normal launcher was restored
after each run. The checked-memory suite passed 267,089 checks per
native, ASan/UBSan and TSan build. All 27 host runtime lifecycle sessions
passed. In an additional active-Town test, the first resumed flip took 34 ms
and stop/join took 29 ms.

These scripted walks use paced null audio. The next gates are actual combat,
new progress saved and reopened, real controls and audio, a sustained
Town/combat loop, and qualification on the physical iPhone. The earlier
iPhone Simulator build completed a 1,450-frame Town walk. Follow the
[walking fixture instructions](diagnostics.md#measuring-town-frame-timing)
to reproduce the comparison.

## 2026-11-14 — Clean physical Town; settled performance remains too slow

Clean frontend `368caf2` with the frozen50,340-entry core passes the iPad's
extended Town gate:6,456 rendered frames /225,962,278 entries /303.27seconds,
exactly600 advancing flips and vblanks after observation, no guest/frontend
error. The downloaded final PNG shows clean Town and Felix, without the earlier
logo overlay. This qualifies this later scene, not the cause of the earlier
capture difference. All28 prior save/checkpoint/image files retain their hashes;
only two new session images appear. Normal launcher mode is verified afterward.

The longer window exposes unacceptable settled speed:592 rendered frames over
115.69seconds after the first Town heartbeat, about5.12fps. Native Town also
advances roughly120frames per22–26seconds. The clean native600-frame gate passes
5,993 rendered frames /208,525,509entries /315.31seconds. The prompt's earlier
59.46fps result does not extend to Town. Profiling translated game execution
is now the critical path, alongside remaining adventure coverage. No playable
performance claim is made.

Interactive iPhone Simulator touch reaches the character menu, navigates
Equipment→Armory→Stats, and shows the exact retained Felix values:LV1,EXP351,
NEXT850,HP300,STR16,CON14,DEX12,INT4,MGR7,LUC10,skill point1;47gold appears in
the character menu. Final image is inspected. Its600-second diagnostic run
stops cleanly after8,289frames /437,557,255entries. This is reopening existing
progress, not saving newly earned progress. An apparent brief menu disappearance
does not establish a release-cancel input defect; original held-mask/edge audit
and settled navigation support no input workaround.


## 2026-11-14 — Physical Town gate and clean live touch movement

The clean `1d0adf7`/50,327-entry app passes the physical iPad Town component gate
from the retained working save, without reseeding:6,131 rendered frames /
168,965,536 entries /218.88seconds, no guest/frontend error. Its57 ordered maps
use2,190,737,408bytes of4GiB, sampled peak dirty memory is1,974,112,016bytes,
and thermal state is fair. All26 retained files survive the install unchanged;
the ten prior working-save files also remain unchanged after the run. Normal
launcher mode is restored. The final PNG shows Town/Felix beneath an unexpected
large logo overlay. Clean physical Town rendering remains open; this capture
alone does not distinguish a transition from retained composition.

An independent interactive iPhone Simulator run with clean `e5b266d` and the
50,334-entry core reaches a clean live Town view with full quest dialogue.
Holding the real MOVE overlay right for fourseconds visibly walks Felix and
scrolls the camera. Continuing right exposes executed target203d54a0; a bounded
six-entry table batch now brings all SDK archives to50,340 entries. This is
a movement result, not combat or sustained gameplay acceptance. The fixed-time
native exploratory route instead selects Inn Options and is stopped with no
runtime error; its input intent does not prove an adventure was reached.

`DCP_CHECKPOINT_SETTLE_FRAMES` can extend neutral post-marker advancement from
the unchanged120-frame default up to3600, with minimum120. Existing trace,
vblank and wall-clock budgets remain. Reports include observation anchors and
the requested interval. All37 configuration checks,32 actual-runtime negative
checks/eightcases,95 input checks native/ASan+UBSan, all SDK builds and27 runtime
lifecycle sessions pass (maximum stop69.73ms). A600-frame Town capture is in
progress to inspect the later scene. No rendering change is inferred yet.


## 2026-11-14 — Simulator Town and interactive navigation

Clean `1d0adf7` with the frozen 50,327-entry ordered core also passes visible
Town in iPhone Simulator: 5,411 rendered frames /151,366,149 trace entries,
checked advancement, no guest/frontend error, and an inspected Town/Felix image.
This is the simulator, not the physical iPhone. The final minimum-3-GiB adapter
also passes all27 lifecycle sessions with maximum stop44.94ms.

An isolated interactive simulator session uses the real touch overlay to load
DATA1/Felix, reach the Inn, pause through the native sheet, resume and confirm
Leave tavern. It records7,500 frames /290,264,330 entries before an explicit
missing trace at204db441. The independent native exploratory adventure script
stops at the same target, with Town visible in its final image. Movement,
combat and a completed adventure are not qualified by these runs. The next
bounded seven-case table batch compiles for allthreeSDKs, bringing the core to
50,334 entries; its adventure attempt is in progress.

A checked GOBLIN component observer is available for later coverage tests. It
requires independent original primary/control vtables, class tag, normal mode0,
and both delivered/stored post-initialization state0xa4 at the shared callback.
It preserves the separate Town predicate and rejects tutorial variants. This
marker means only that a plain Goblin component receives an active update;
it does not prove stage identity, enemy visibility, an attack, damage or victory.
No mobile Goblin checkpoint has passed yet. Generated original-code evidence
and game-specific traces remain private.


## 2026-11-14 — Physical save-dialog fix, cold reloads and native Town

Clean `4fd1796` with the qualified 50,292-entry core passes a 180.16-second
interactive run on iPad mini A20 Pro: 9,064 rendered frames, complete inspected
Create save data Yes/No image, replay median/p95/p99 9.68/10.13/10.17 ms, and
zero audio underruns/clipping. The final frame-arrival window has 16.67 ms median
and 33.34 ms p95. This qualifies the stalled prompt's improvement on hardware,
not sustained gameplay or a hands-on controller test.

The same physical working-save UUID is then reopened twice by new processes,
without restoring the seed: 5,479 / 5,421 frames and 127,649,949 /
126,660,272 translated entries in about147.36 /138.73 seconds. Both INN gates
pass with no error; both final images show Felix LV1 at Dragon's Haven Inn.
All ten preexisting save/checkpoint/image files keep their exact hashes. This
qualifies actual character reopening from the retained working folder, separately
from creating and saving new progress. Normal player and live Mac/iCloud saves
are untouched. The app returns to normal launcher mode after testing.

The iPad Simulator also reopens the same working copy after process replacement,
with no reseeding and unchanged source/normal roots. It passes both INN gates
(4,762 /4,613 frames); the second image shows Felix but some incomplete labels,
so general animation/text rendering remains open. Both physical reload images
are complete.

Three bounded original table batches bring all SDK archives to 50,327 entries.
The native arm64/Metal host now passes visible Town: Felix outside the Inn,
5,862 frames /145,518,001 trace entries in191.74 seconds, no errors and unload
acknowledged. A same-coverage unrestricted-mapping baseline also passes Town
(5,662 frames /188.55 seconds); extra table entries establish that progress.
The new ordered DirectMemory adapter independently preserves the original
unsigned relative-address contract, using a 4 GiB reservation and about2.04 GiB
in that Town run. The earlier intermittent string fault's exact failing resource
operands remain unresolved; no causal claim follows from two healthy runs.

DirectMemory fixtures pass 563 checks native/ThreadSanitizer and all SDK
compilations. Bounded configuration/runtime negatives pass24/24, input services
95 native/ASan+UBSan, and actual runtime lifecycle passes all27 sessions with
maximum stop38.42 ms. The original mapping trio needs502 MiB; initial explicit
maps need1,424–1,616 MiB, but later caller-sized maps make lifetime usage an open
qualification item. A 3 GiB fallback replaces an insufficient2 GiB fallback.
Physical Town/new memory-adapter validation and adventure/combat remain next.

## 2026-11-14 — Save-dialog rendering stalls reproduced and reduced

An isolated interactive iPhone Simulator run reproduces the player's Create
save data prompt with real platform audio. A native sample attributes nearly
all main-thread samples to texture lookup: AOT copied and byte-hashed the same
full atlas for every glyph/sprite draw. The same frontend and frozen50,292-entry
game archive were used for three180-second runs; archived inputs retain hashes.

Per-frame snapshot reuse removes repeated glyph copies. Accelerated full
SHA-256 then replaces the large byte-serial FNV content hash. Final prompt
replay-pump median/p95/p99 are9.98/10.33/10.69ms in the rolling window, versus
baseline p99 about1,920ms. The final69.09-second interval renders at59.46fps.
The final SHA variant's PNG was inspected and shows the complete Yes/No prompt.
This is a Simulator result, not a physical-iPad performance qualification.
The intermediate cache-only variant's final PNG has incomplete prompt text;
no pixel-equivalence or general gameplay-rendering claim follows from these
snapshots. Further scene/animation verification remains required.

Live SHA-run audio samples at84/111seconds have zero underruns; shutdown's final
counter includes768 underrun frames. Native and ThreadSanitizer concurrent
fixtures pass200frames ×128atlas draws with exactly200owned snapshots and no
reported race. The byte4096 regression verifies next-frame invalidation and
unchanged reuse. All36,864 saved RGB and1,504BC checks pass; allthreeSDK
renderer archives compile. Normal player saves and live Mac/iCloud data are
not used by the comparison.

Frontend diagnostics now time replay/present/heartbeat work. The explicit timed
interactive test mode keeps normal input/audio but isolated UUID saves. Resume
mode reuses an existing diagnostic working save without seeding it again and
stores new images separately. Six earlier Simulator configuration/resume cases
pass; actual character reopening from the same working folder is still pending.

## 2026-11-14 — Physical iPad INN and copied Felix verified

Clean frontend `0e15ee5` with the frozen 50,292-entry core passes the seeded
INN checkpoint on iPad mini A20 Pro. The isolated session records 123,268,154
translated entries, 3,467 rendered frames, one checked INN-ready observation,
no guest/frontend error and 144.97 seconds elapsed. The downloaded final Metal
PNG was inspected: Dragon's Haven Inn and Felix level1 are visible. Sampled
peak dirty footprint is about1.80GiB, thermal state nominal. This test uses
scripted input and paced null audio, separately from normal interactive Play.

Its three game save files retain their exact hashes after replacing the process
and relaunching the normal launcher. They remain in the diagnostic UUID folder;
this only proves retained files, not reopening that same working save or saving
new progress. A diagnostic resume mode is being tested for that separate check.
The seed and live Mac/iCloud saves are untouched. The normal player's save roots
are no longer assumed empty after the subsequent hands-on session.

The player reports extremely slow response at the fresh Create save data dialog.
Live diagnostics confirm serious interactive frame stalls with real audio active.
That performance issue takes priority over additional device gameplay testing.
The iPhone remains away and has not been contacted.

A 50,304-entry native Town attempt reaches the checked Town component, then
fails on missing `2045f21f` while the final image still shows loading. This is
not visible Town acceptance. An earlier run reported an invalid string pointer
at `2057cbef`; failure-only checked argument/caller/module addresses are now
included as `fault_context` to investigate recurrence. The later attempt does
not reproduce that fault; no cause or resolution is claimed.

## 2026-11-14 — Real INN and the copied character load on the native host

The frozen 50,292-entry core in `ios-game-50292` reaches Dragon's Haven Inn
from the quiescent Felix copy. Native Metal run `ios-host-50292/run-inn` passes
with 3,650 rendered frames / 114,571,041 trace entries in 168.21 seconds, one
checked INN resource-ready observation, acknowledged unload and no guest/host
error. Final PNG was inspected: the Inn interior and Felix at level1 are
visible in the party list. This qualifies native character reopening; fresh
progress/save/relaunch and physical INN are still pending.

The checkpoint driver now releases scripted buttons/sticks after observing its
requested component, retaining connection and continued real frame advancement.
The 95-check input/service suite passes native/ASan+UBSan, configuration/runtime
negatives remain24/24, and all27 lifecycle sessions pass (max stop65.3ms).
A fresh TITLE regression passes3,035 frames /77,075,990 entries in93.29seconds;
its inspected final image is the real title splash/PressX screen held by neutral
input, rather than automatically pressing into the menu. Interactive Play is
unchanged. The earlier physical title-menu result remains qualified.

TOWN component observation is connected from original class/vtable/state
evidence, separately from the Inn portal and constructor. A copied-Felix Town
run is underway. Its visibility/movement/combat have not passed. All three
SDK archives compile; the same seeded INN test is being packaged for the iPad.
Live Mac/iCloud progress is untouched.

## 2026-11-14 — Seeded device-test support and 50,285-entry core

The explicit seeded diagnostic variant restores a quiescent staged save into
a new UUID session through the existing validated checkpoint transactions.
Five Simulator cases pass: invalid durations, missing seed, bounded seeded stop
and normal launch. All three original disposable files retain their hashes,
as do the source and normal save/checkpoint sentinels. No character-reopening
claim follows from this frontend copy test. Both SDK builds pass; iPhoneOS
signature/profile checks pass. The previously qualified physical app is retained.

Private original-byte/provenance-window analysis supplies 296 direct-branch
continuations plus 15 closure targets. Thirty adversarial checks pass; the
existing-boundary/same-owner/direct-window rule reaches zero missing targets.
It is a compile-only overapproximation, not overall coverage or reachability.
A separate four-case backward-guarded table batch brings all three archives
to 50,285 entries. The 49,970-entry copied-Felix run reached 102,018,652 entries
and another loading frame before that table miss. INN visibility and character
reopening remain pending; the TITLE passes below remain the qualified baseline.

## 2026-11-14 — TITLE also passes both simulators; INN marker connected

The matching 49,492-entry core now passes in the iPhone and tablet simulators:
2,974 / 2,749 rendered frames, about 96 / 94 seconds, complete checkpoints
and no guest/frontend/save errors. Both final PNGs were inspected and show
the real title menu. These are simulator results, not physical-iPhone results.
The actual iPad result below remains the qualified device baseline.

A 239-destination batch covers the next long owner's direct continuations;
49,731 entries now compile for all three SDKs. The new INN observer checks
its original primary/control vtables, class tag, normal mode and stored0x81
at the resource-ready transition helper. Constructor state0, an audio cue or
just a resource open cannot satisfy it. It records initialization completion,
not screen visibility, and uses no remembered object address. Readback and
actual character reopening remain separate. A copied-Felix Metal run is
underway; INN has not yet passed. Headless/configuration/service tests remain
24 / 87 / 24 checks, native/sanitizer service runs pass, and 27 lifecycle
sessions pass (max stop 127.7 ms under concurrent simulator startup).

## 2026-11-14 — Visible TITLE passes on the physical iPad

Clean frontend `062a77d99e76a5e233cccf0d2ba08f31d753ee1b` with the frozen
49,492-entry `ios-game-component-20261114` core passes on iPad mini A20 Pro
(`iPad19,1`, Apple A20 Pro GPU). The automatic disposable session executes
89,813,167 trace entries, renders 3,189 game frames, makes 3,541 drawable-present
calls and completes the verified TITLE-component checkpoint with no guest,
frontend, platform or save error. Downloaded final Metal readback was inspected
and shows the real Dragon's Crown main menu. Total session is about 95 seconds.

Sampled peak dirty footprint is 1,813,237,448 bytes (1.69 GiB); thermal state
remains nominal. Captured-frame median is 16.67 ms with p95 about 50 ms across
startup; this is not a sustained gameplay performance claim. Default signing
works without increased-memory entitlements. Three game-generated files remain
in the diagnostic session's save tree, while ordinary ProbeSaves contains no
files. The app is relaunched normally after the check, leaving Play available.
This checkpoint uses paced null audio and scripted pad input; audible output,
physical input/reconnect and gameplay remain unqualified. The iPhone is away
and has not been contacted. Evidence is `ipad-title-verification.json`, final
diagnostics and first/final PNGs in the private external workspace.

The matching native arm64 Metal host also passes: 3,385 rendered frames,
85,649,897 trace entries, no error and a visually inspected title-menu frame.
A copied-Felix INN run gets beyond TITLE but reports missing `2002ef6d`; INN
component mapping and save reopening remain work in progress. The full expanded
Simulator core is compiling. Live Mac/iCloud saves and the installed Mac app
remain untouched.

## 2026-11-14 — Isolated automatic startup checks and component evidence

The 49,492-entry Mac/iPhoneOS core now compiles. Its preceding frozen Metal
run renders 3,612 frames / 40,261 draws, acknowledges unload and has no host
error, then stops at an unsupported Toolkit2 clear-presence call. Loading
content is visible, but the last inspected image is black; no visual TITLE
pass is claimed. The offline clear-presence adapter now mirrors the reference
call's unused request/future arguments and idempotent already-absent state.
The service suite passes 87 checks native/ASan+UBSan, configuration passes 24,
and six bounded negative runtime cases pass 24 checks. All 27 actual-runtime
lifecycle sessions pass (max stop 58.6 ms in this concurrent run).

Original-byte and relocation review found that the old named scene hook is
an audio-cue selector, and Remill can inline its numeric wrapper. It no longer
satisfies checkpoints. The new TITLE observer verifies the actual component's
phase-dispatch entry, original primary/control vtables and active update phase;
its initializer loads STAGE/TITLE locale resources. This is a component-state
marker, explicitly separate from visual TITLE. Runtime exports that distinction
and keeps visual verification false. The new real checkpoint run is pending.
INN's component identity remains to establish.

The explicit `--diagnostic-startup` app option starts a bounded test using its
own UUID save/checkpoint folder and a staged, validated input script. It writes
first nonuniform/final game images into the same session folder. A 27,507-entry
Simulator core tests the new frontend: 376 real rendered frames, isolated PNGs,
and two unchanged normal-save/checkpoint sentinel files. Missing input rejects
with zero frames and no stale session image; normal launch still waits for Play.
These frontend tests do not qualify the newer core.

The physical iPad still has the clean `80228bb` / 48,367-entry game candidate
at Ready to play; its automatic diagnostic replacement is being prepared.
No physical game-frame result is claimed yet. Its separate expanded CPU probe
passes 1,906 checks. Live Mac/iCloud saves remain untouched.

## 2026-11-14 — Expanded CPU suite verified on the returned iPad

The player reconnected the iPad mini A20 Pro. Clean source `cc0d767` replaces
its earlier probe without uninstalling the data container. Fresh diagnostics
identify `iPad19,1` and pass all 1,906 checks across 85 authored translated
routines / 84 native-reference routines, with two workers and zero failures.
This is synthetic CPU execution, explicitly not a game-startup result. The
iPhone remains on its earlier probe and was not contacted.

The native core now links 48,366 entry points after a separate compile-only
prefetch of original direct branch destinations in a long unwind owner. A
matching real iPhoneOS archive now compiles for hardware startup/render/memory testing.
Generic full-width table-base copies and independent post-ADD cleanup now pass
118 checks / 109 owned functions. A separate proved32-bit index copy after
CMP passes 50 checks / 23 owned functions; conditional isolation/baseline
comparison passes 34 checks. A new executed branch is being added before
packaging.
TITLE/INN, physical game frames and actual save reopening remain unverified.
The desktop app and live Mac/iCloud progress are untouched.

## 2026-11-14 — Startup coverage, bounded dispatch and real frame checks

The native corpus now links 47,980 entry points after scheduled-comparison,
termination-import and guarded/conditional prefetch work. The scanner regression
passes 74 checks across 65 owned functions, conditional isolation passes 34
checks across 11 functions, and import derivation passes 23 synthetic checks.
Derived stack-failure thunk declarations bind checked ELF/GOT/PLT bytes to the
typed fatal service, with content-addressed policy snapshots in bootstrap.
Declarations mean noreturn, not nounwind; host diagnostic unwinding remains valid.
The service/input suite now passes 82 checks native/ASan+UBSan and all SDK compiles.

A review found the scene observer reading the scene-manager object in RDI rather
than the scene name in RSI. Native Mac probe logs confirm RSI; the correction and
report marker are now in the real runtime. Completion also retains late worker
errors instead of suppressing everything after its checkpoint flag. The lifecycle
fixture exposed an initialization-timing assumption; it now waits for observed
translated progress on resume, and all 27 sessions pass. The current corrected
runtime still has no accepted TITLE/INN checkpoint.

An independently decoded 8 KiB owner explains a large remaining cluster: 125
relative-table slots / 98 distinct cases, plus a default and shared loop head.
Its 1,869 decoded instructions have no errors, overlaps or unresolved branches.
Those destinations and two reviewed factory groups add 126 missing traces; the
next fresh boot is running. Generic base-copy/post-ADD recognition is being
prepared separately. Latest frozen Metal evidence remains loading content;
no physical device, live Mac save or iCloud progress was touched.

Bootstrap now reproduces the generic internal-switch scan for both original
modules. The owned XED scanner/validator checks immutable table bytes, full-index
bounds, copy/zero-extension dominance, alternate entries and instruction overlap.
Review found and fixed three direct/table-edge proof-bypass counterexamples
before integration. Its 36-function matrix passes 45 checks: 13 accepted owners /
14 tables, 23 rejected owners, and four explicitly incomplete candidate owners.
The latter keep unresolved/external PCs in separate metadata and never count as
complete CFGs. Bootstrap may precompile those local candidates; actual dispatch
and checkpoint acceptance remain unchanged. Fresh scans identify 3,573 missing
prefetch destinations; a 3,952-target union with reviewed private tables is now
compiling. The current 41,688-entry runtime still reports loading-stage targets,
including the same `204dd973` in fresh and disposable Felix-seeded runs. Neither
TITLE nor INN/save reopening has passed.

Packaging now retains the actual version/build substitutions instead of letting
the shell execute their names. Built bundles report version 0.2/build 1, and
post-build verification records signature/profile memory-capability status.
The ordinary device app signs with the cached profile; an explicit
`DCP_EXTENDED_MEMORY=1` request currently fails because Xcode reports No Accounts
and the wildcard profile lacks those capabilities. The unsigned simulator
variant compiles with both entitlements requested. Diagnostics now include
remaining dirty-memory allowance and physical memory. Neither simulator memory
nor a successful signature proves hardware memory availability.

The post-unwind private table review expands static coverage to 41,580 entries.
A frozen 36,289-entry Metal run draws 2,080 frames / 32,709 draws, without a
frontend error, before an internal case at `2006bab3` (now compiled). Loading
content is still visible; TITLE/INN remain unverified. Original 1,800-vblank tests
were too short for fresh startup, so the default now permits 7,200 while explicit
negative budgets remain tested. Further startup branches vary with worker
scheduling and are discovered under longer actual pad-input scripts. Partial
mobile object caches are retained pending a useful runtime checkpoint.

The mobile vblank timer now uses absolute, pause-excluded monotonic deadlines
instead of appending a full 16.7 ms sleep to each frame's computation. The owned
deadline/timing fixture passes 1,006 checks, with 60 waits plus 5 ms inter-frame
work taking 1.002 s. Real-runtime lifecycle repeats all 27 sessions, maximum
stop 37.8 ms; six bounded headless cases and input/configuration fixtures still
pass. This corrects a pacing error, without claiming game or device FPS.

The validated sectionless-ELF unwind reader now identifies 32,282 function
entries in the pinned modules, exactly matching independent LLVM unwind output.
Twenty-nine owned synthetic ELF/CFI cases cover malformed metadata and valid
zero-address/null-pointer distinctions. Static indexing is not an execution
claim. The expanded corpus has 35,976 entries; all used dependency records are
current, while unchanged non-FP80 traces retain their original generation
backend records explicitly. Full linked runtime verification is continuing.

The cold libc corpus exposed mask-144 FP80 classification and extended-format
remainder operations. Integer-only classification passes 5,325,954 native LLVM
and 42,275,840 lowered arm64 checks. Production/lowered remainder each pass
554,624 rows, flags and concurrent state checks; an independent exact finite
oracle has zero differences. Rosetta FPREM1 differs on 2,896 rows and is recorded
separately, not copied into the implementation. Other classification masks and
unknown transcendental operations remain explicit failures.

Six indexed libc functions also require the Remill CAS128 reference-operand
ABI. The checked serialized bridge now passes full-width success/failure,
expected/desired preservation, bounds, cancellation, canaries and an eight-worker
counter carrying across 64 bits. Memory fixtures pass 251,076 checks in each
native/ASan+UBSan/TSAN run. A translated LOCK CMPXCHG16B fixture matches the native
oracle, bringing the CPU corpus to 1,906 checks / 85 routines (84 native-oracle
routines), with all three SDKs compiled. The six private functions are compiled;
this does not claim they have executed in the game.

Clean frontend `7c43b4e` with the 27,507-entry artifact now renders the real
loading image on both simulators. Phone/tablet replay 455/376 frames and
7,290/5,939 draws, report nonuniform snapshots, pass libc start/64 strlen checks,
and stop at the already covered `20428a47`, with no frontend error. Readback
shows the fairy and loading text during its initial fade. Simulator-only audio
muting keeps these unattended runs quiet while preserving PCM consumption and
counters; it is not audible-quality acceptance. Both complete app builds pass,
and the iPhoneOS signature verifies. Evidence is `ios-app-frames-simulator/evidence`.
No actual device was contacted. TITLE/INN remain open.

Further libc prefetch exposed a missing CAS8 bridge at link time. Checked CAS8
and CAS16 now share the serialized template used by CAS32/64. The expanded
memory suite passes 251,067 checks under native/ASan/UBSan/TSAN; translated
byte/word LOCK CMPXCHG routines bring the CPU corpus to 1,884 checks / 84 routines
with 83 native-oracle routines. Mobile SDK compilation passes. Main discovery
has passed 31,000 entries and exhausted ordinary direct-call/relocation
prefetch; additional function-pointer origins are under review. No wider lift
window is justified by the sampled gaps: all seven nonaligned demands reviewed
are switch cases already inside the original slices.

The first recognizable AOT game image now passes native Mac readback: the
27,507-entry frozen artifact renders the real animated fairy and NOW LOADING
text. The host replays 580 frames / 8,276 draw calls, sees nonuniform RGB content,
and acknowledges ordered unload in 20.06 seconds before missing `20428a47`.
Actual libc start remains 0 with 64 strlen checks. This is the same AOT path
used by mobile, with native SDL/Metal replay; Rosetta does not execute this run.
TITLE/INN have not been reached, so the host correctly returns failure despite
visible loading content. Evidence is `ios-host-frames/run/frame-final.png` and
`report.json` in the external workspace. Device/simulator builds of this frozen
artifact are in progress. Physical devices remain away.

A separate conditional catalog supplies 25 narrow-guard tables / 291 exact case
and default PCs, with 6,856 CFG instructions checked. Each records its unproven
high-index-zero condition. These are optional static case precompilations, not
proven dynamic bounds or executed paths; runtime dispatch is unchanged and any
other actual target still fails explicitly. The original strict 111-table
catalog remains intact. An end-to-end seeded discovery attempt also verifies
all three disposable Felix files and its adjacent SHA manifest, then stops at
its configured round limit without claiming boot acceptance.

Discovery can now seed each attempt from a quiescent disposable save copy,
with bounded regular-file inventory, independent copy hashes and per-attempt
manifests. Twenty-eight filesystem checks pass, and the earlier disposable
Felix snapshot copies with all three files verified. This prepares repeatable
INN/save work; it does not establish that the AOT game has reopened that save.
Live Mac/iCloud progress remains untouched.

Headless scene selection and scripted input are now connected and verified.
The default stays TITLE plus 120 advancing flips/vblanks; INN is an explicit
alternative. Configured names and positive finite budgets are validated, and
input uses the desktop parser with an exact file SHA-256 in the report. Empty
scripts mean idle. Tests pass 24 config and 80 pad/script checks (also under
ASan/UBSan), all three SDK compilations, and six actual-runtime bounded/negative
cases with 24 checks. None reports boot acceptance. A repeat of the actual
runtime lifecycle passes 27 sessions with maximum stop 10.2 ms, and storage
still passes 137 core / 108 service checks over four simulated relaunches.
See [unattended checkpoints](game/headless.md).

The independent static table catalog validates 111 guarded tables, 5,117 entries
and 1,395 unique targets including defaults, with 29,386 case-CFG instructions
checked. 1,330 targets were prefetched in a parallel batch. Ambiguous low-byte
guards and unresolved indirect exits remain rejected; static inventory does not
claim execution. The current corpus has 22,597 entries, current provenance and
three explicitly marked bulk libc adapters. Startup discovery continues; TITLE,
INN and nonuniform game images remain unverified.

Clean frontend `79fc148` now builds for both iPhoneOS and Simulator; the signed
iPhoneOS bundle verifies. The 13,307-entry artifact is linked into both. Phone
and tablet simulator runs each pass real libc startup and 64 strlen checks,
start 24 workers, and replay 106/98 captured frames before missing `2006d309`.
That target is already covered in ongoing discovery. The exported artifact
identity matches the packaged manifest. Both runs explicitly report no
nonuniform snapshot, with no frontend error. Their viewed control overlay is
intact. These are simulated iPhone 17 Pro Max/iPad mini A17 Pro results; no
physical installation is claimed. Evidence: `ios-app-stage-simulator/evidence`
in the external workspace. Recognizable game images are still outstanding.

Trace lifting now also uses bounded independent processes. Duplicate address
spellings are normalized before tasks start. Each successful trace is instrumented
and its provenance committed individually, so a failed peer does not erase that
evidence. A separate invalid-entry batch test confirms this behavior; invalid
code is not published. A successful batch verifies current provenance across
13,557 entries, and subsequent discovery has passed 16,000 entries. This larger
static corpus does not establish that every prefetched function executed.

UIKit name entry now uses a checked IME request/completion adapter. UI callbacks
copy owned UTF-16 text; only the guest poll commits it to validated game memory.
Cancel/abort preserve the original buffer, stale session IDs are rejected, and
Unicode truncation preserves surrogate pairs. Native, ASan/UBSan and TSAN each
pass 495 checks with 100 concurrent completions, plus all three SDK compilations.
The latest frontend compiles for both iOS SDKs. Real character-creation keyboard
acceptance remains open. See [name entry](game/ime.md).

Four guarded switch families were independently resolved and their missing
unaligned targets translated in one batch. Independent trace compilation is now
bounded by `--jobs`; object publication is atomic. LLVM response-file archive
inputs avoid the process argument limit hit by the larger frozen corpus. A
13,307-entry frozen Mac run captures 251 drawn frames, still uniformly black,
before the next missing target. Discovery and device/simulator cross compilation
continue; no recognizable image or TITLE result is claimed.

The goal remains active while the reference devices are away. The sixteen-patch
backend adds observed ROUND, packed float and packed integer forms. Rounding
passes 13,824,000 masked native-oracle comparisons, 2,560,000 suppression
invariance checks and two fault-before-write cases. Another 10,208,000 packed
value/control checks pass. Rosetta forces MXCSR exception masks; unmasked ROUND
checks are separate AOT properties. Packed float sticky exception flags and
architectural guest #XM delivery remain outside this coverage. See
[review boundaries](cpu/review/confirmed-simd.md).

A real worker exhausted its native stack through a non-tail Remill jump bridge.
The production bridges and game dispatcher now require tail branches. A lifted
regression executes 1,100,003 entries per route on a 128 KiB requested pthread
stack, with zero entry-SP growth and 127 checks passing. Disassembly also verifies
the real game dispatcher's service B and trace BR. See
[tail dispatch](cpu/review/tail-dispatch.md).

The game then exercised per-worker cancellation, which previously stopped the
entire runtime. Cancellation now exits only the selected worker with the PS4
canceled result 1, wakes parked execution, preserves TLS cleanup and retires its
notification context after successful joins. Native, ASan/UBSan and TSAN variants
each pass 885 adapter checks. A repeat of the actual-runtime lifecycle fixture
passes 27 sessions, with maximum measured stop latency 30.2 ms in this run.
These are startup and cleanup results, not physical-device gameplay acceptance.
See [cancellation scope](game/thread-cancel.md).

Discovery has progressed beyond 12,000 trace entries with current backend
provenance. A frozen 5,435-entry archive produces 197 captured/drawn frames and
985 submissions before the next missing target. Its actual PNG readbacks remain
uniform black; TITLE, INN and recognizable real-game images have **not passed**.
The native Mac reference renders the create-save prompt at vblank 200. The AOT
runtime still needs additional initialization paths before that comparison can
establish matching content. Live discovery continues in the external cache.

The host now requires nonuniform RGB content, drawn frames and the runtime's
TITLE checkpoint for success. It saves a final image before ordered unload.
The simulator preserves the first nonuniform game snapshot rather than freezing
its initial black frame. Uniform-render fixtures still pass and explicitly skip
the real-game content requirement. Game library manifests bind the three copied
archives to their platform, trace hash and backend; packaging checks their SHA-256
values, and exported app diagnostics retain the artifact identity. Both iOS SDKs
compile the latest app source. A fresh complete app link is still pending the
next useful frozen corpus. The devices retain the older probe.

## 2026-11-13 — Real modules and mobile host integration in progress

The active goal is to complete the iOS port as far as possible while the physical
devices are away. The last hardware-installed build remains the earlier CPU
probe. All new verification in this section uses the arm64 Mac or iOS Simulator;
it does not establish playable performance or acceptance on either physical device.

The real-module AOT runtime executes the supported libc and eboot, applies
relocations, handles independent guest stacks/TLS and starts the game's workers.
Real libc startup returns zero and its actual strlen passes 64 cases against a
native x86 reference. Execution is progressing through game initialization with
explicit missing-target diagnostics. TITLE/INN boot and live game frames have
not yet passed. The two complete CPK archives were previously copied into each
device's probe sandbox, but the new game runtime has not been verified there.

The expanded CPU corpus now passes 1,840 checks across 82 routines and two workers
on the arm64 host; 81 routines use a native Rosetta x86 oracle. This includes
concurrent 32/64-bit CAS, exact/bounded 80-bit memory bridges, string comparisons/masks, packed saturation, lane insertion,
blending, shuffles, reciprocal/rsqrt corner cases and MXCSR rounding-mode
round-trips. An independent 35-form full-YMM review passes 6,720 checks, including
legacy upper-lane preservation, VEX clearing, memory operands and 256-bit lane
locality. It found and fixed legacy BLENDPD selector names and the RCPPS signed-zero
boundary at magnitude 2^126. Complete x86 and floating-point-environment coverage
is still outside these tests.

The packed-shift/VPBLENDVB differential suite passes 46,720 full-YMM checks
across 73 routines and 80 vectors. VEXTRACTF128 fixtures cover register/memory
forms, immediate lane selection and upper-register clearing. Software x87
uses SoftFloat 3e and an LLVM pass that replaces executable FP80 values with
exact i80 storage. Direct and lowered-IR suites each pass 153,228 native-oracle
checks; the real libc FSCALE routine passes 19,800 vectors. Unknown extended
operations fail explicitly. A frozen 2,355-trace candidate builds for all three targets. Its real-module
simulator runs reach libc start and 18 workers on iPhone 17 Pro Max and iPad mini
(A17 Pro) simulated models, then stop at VMOVMSKPS, matching the arm64 host.
The new movemask patch passes 38,400 full-register native-oracle checks; the
nine-patch full refresh passed. Four further statically observed SIMD groups
(variable blends, lane insert/extract, 128-bit lane permutations and dword
shifts) pass 1,199,616 differential checks across 213 routines; their thirteen-
patch full refresh now covers 3,033 current-provenance traces, with discovery
continuing. Static presence is separate from execution. These are simulated models, not the
physical iPad mini A20 Pro.
See [FP80 boundaries and evidence](cpu/fp80/README.md).

Startup has reached one vblank and over four million trace entries. It now
stops at VROUNDSS at `20639719`, with zero flips and no actual game frames.
Three verified bulk libc exports use checked native copy/fill adapters, marked
explicitly in provenance; a comparison flag restores original translated code.
Both modes reach the same rounding gap with actual strlen/startup still passing.
The production-wrapper ABI suite passes 822,659 checks in native, ASan/UBSan and
TSAN runs, plus all three SDK compilations. See [bulk libc](game/libc-bulk.md).

`game/build.py` produces Mac, iPhoneOS and simulator libraries. `advance.py`
discovers executed targets in batches, stops explicitly on other errors and
supports graceful stopping. Prefetch includes aligned possible callbacks from
bounds-checked relocations; four unaligned constant pointers were quarantined
without removing executed demands. Generated traces now carry input/slice/backend/IR
SHA provenance. A full relift replaced the earlier unknown-provenance host
corpus; later generated entries use the same checked backend. All private images,
IR, code archives and game assets remain in the external build tree.

A sampled startup showed range scanning and nested lock acquisition dominating
CPU use. A bounded eight-range cache retains the existing synchronization and
removes redundant recursive locking. Independent native, ASan/UBSan and TSAN
runs each pass 251,055 checks, including concurrent append, registry replacement,
bounds, cancellation and CAS32/64. A controlled lookup benchmark shows strong
hot-set gains but a 59% deliberate cache-thrash regression; neither is a measured
game-frame result. See [memory review and limits](cpu/review/memory-cache.md).

The new UIKit game launcher links the CPU runtime, in-process SDL Metal replay,
touch/GameController input, AVAudioEngine output, Files import and local save
transactions. Its iPhoneOS Release build and signature verify; the simulator
build links and launches. This is a development app, with a separate bundle ID
`com.wadscorp.opencrown.mobile`. No new physical install is claimed. The earlier
probe remains available through `build-probe.sh`.

Rendering reuses the existing draw-capture and Backend interfaces with signed
trace-entry observers instead of executable x86 patches. Guest memory snapshots
are synchronized before hashing/detiling. Renderer fixtures pass 32,768 RGBA
checks, 36,864 saved-image channel checks and 1,504 native/CPU-decoded BC checks.
The concurrent snapshot fixture passes 200 frames under ThreadSanitizer. Mac GPU
and texwatch tests pass after the guarded shared-source changes. These are
synthetic rendering results, not live game scenes.

Simulator integration exposed two distinct startup problems. Its reported GPU
families are Apple1/Common1, so a narrowly marked simulator-only SDL patch permits
the renderer's basic draw subset; physical iOS keeps SDL's original hardware
check. CMake's SDK selection also misidentified normalized iPhoneSimulator paths
and embedded device shaders. Case-insensitive selection plus explicit AIR
targets now pass all 24 Mac/device/simulator artifact checks. The assembled
simulator app initializes Metal and presents while the guest starts; no captured
game frames have yet been observed. The 2,355-trace app presents 478/506 blank
drawables on phone/tablet simulators before the CPU gap; these are explicitly
zero captured game frames. Pause and touch controls appear above the Metal view.
The one-second live heartbeat reports startup progress before the first frame,
and retains the final guest error/status for Share after shutdown. Presentation
counts alone do not prove that the game has rendered.

Host input/audio tests cover four pad slots, multitouch serialization, disconnect
state, clipping/downmix and 400,000 concurrent ring-buffer frames. The sink requests
48 kHz stereo and 256-frame I/O periods, and records observed periods/underruns.
The iPad simulator also exposed portrait overlap between Menu/Pad and the right
stick buttons. Compact layouts now move Menu/Pad above the control clusters
and keep R3 separate; a new rendered UI snapshot verifies the adjustment.
Actual device sound, touch ergonomics, physical controller reconnect and audio
interruption recovery remain unverified. Real-runtime lifecycle tests pass 27
sessions, including parked stop, repeated pause/resume, concurrent-entry rejection,
pre-start cancellation and ordered renderer teardown; maximum measured stop
latency is 3.85 ms in that run.

Storage passes 137 core/retention checks and 108 integrated savedata/file-service
checks across four simulated relaunches. Writable mounts take a validated
pre-write checkpoint; completed unmounts checkpoint the new tree. Interrupted
writes roll back on relaunch while retaining the partial tree. An exclusive file
I/O gate prevents snapshots racing open/write/close. The latest ten validated
completed checkpoints are retained; interrupted copies remain. Import validates
the exact executables, archive sizes and TOC bounds before atomic promotion.
These results do not establish physical-device Files-provider behavior or cloud
handoff. Live iCloud saves remain untouched.

See [bootstrap](BOOTSTRAP.md), [real-module harness](game/README.md),
[renderer](render/README.md), [platform adapters](platform/README.md),
[storage](storage/README.md), and the [acceptance plan](../docs/ios-ipados-port-plan.md).

## 2026-11-13 — Translated routines execute on both devices

Implementation 31ffb1e adds the checked guest runtime and eight Remill AOT
synthetic routines to the signed arm64 app. Both the iPhone 17 Pro Max and
iPad mini (A20 Pro) execute the same corpus with 146 checks passing, zero
failures and two workers. Each passes again after a terminated-process relaunch,
and its persistent launch count advances from 3 to 4. All runs use devicectl
without LLDB. The app's recursive signature verifies.

The arm64 Mac harness reports exactly the same results and corpus hash:
8447df7e24d3edc8c43647f2beb892df881aa3afeb314eb74ecc9de2c56a81f7.
Seven routines compare with actual native x86-64 execution under Rosetta.
The eighth uses seeded per-worker guest FS/TLS memory and expected arithmetic.
Each worker executes eight input vectors per routine, plus nine memory/dispatch
checks. Arithmetic flags compare CF/PF/ZF/SF/OF where defined and selected by
the fixture. Coverage includes branching, rotation, unaligned reads/writes,
direct/indirect guest calls, stack/return state, AVX XOR and guest TLS isolation.
Invalid reads/writes, final valid qword, unresolved targets and a translated
out-of-range memory access are checked. Runtime bridges record faults without
unwinding across nounwind LLVM intrinsics.

Build with ios/cpu/build.py, then pass its output directory as the third
argument to ios/build-probe.sh, as documented in README.md. The build runs the
native oracle, generates the expected vectors and dispatch table, tests the
arm64 Mac library and produces a static iPhoneOS library. Remill's example
lifter leaves some callee bodies external, so the builder explicitly lifts
those entry points and verifies them through real guest call/return execution.
The generated tables, IR, objects, oracle results and device JSON stay external.

Private evidence is in cpu-harness/ and ios-probe/ under the OpenCrown build
root. mac-results.json, verification.json and cpu-results/cpu-relaunch device
JSON files identify source revision and corpus. ios-cpu-app-build.log records
the provisioned Release build. The probe displays and exports its CPU report.

P1 remains open for real eboot/libc routines, atomics, guest-to-HLE calls and
callbacks, broader SIMD/flags coverage, performance and interpreter comparison.
These eight routines are synthetic. The complete game has yet to boot on a device.

## 2026-11-13 — P0 hardware shell and initial P1 lift

The signed arm64 shell is installed on both reference devices as OpenCrown Probe
(bundle ID com.wadscorp.opencrown.probe), from clean source commit 4f8caea.
Xcode Release build and signature verification pass. devicectl launches without
LLDB succeed, then repeat installation and terminated-process relaunch increase
the persistent launch count from 1 to 2 on each device. Retrieved JSON files
contain the expected build ID and machine. The player confirms Home Screen
launch, displayed diagnostics and the share sheet on both devices.

| Device | OS | Page size | Metal device | BC capability query |
| --- | --- | --- | --- | --- |
| iPhone 17 Pro Max, iPhone18,2 | 27.2.1, 24B156 | 16384 | Apple A19 Pro GPU | true |
| iPad mini (A20 Pro), iPad19,1 | 27.2.1, 24B8156 | 16384 | Apple A20 Pro GPU | true |

P0 is complete for the device shell and existing reference inputs. A private
manifest pins the matching eboot/libc hashes, input script, Felix payload hash
and installed Mac reference log containing TITLE at vblank 677 and INN at 1322.
The fixtures use the already verified Mac runtime at 784fe46. The diagnostic
screen explicitly reports guest_execution=false. GPU capability queries still
need actual compressed-texture draws in P3.

### P1 backend work

Built Remill at 66c488c9911fd9b368e4d0c9647d74239f8bba8b with Homebrew LLVM 23.1.5.
Its upstream dependency superbuild completed using external LLVM. The main build
initially failed because LLVM 23 removed convert_from_fp16/convert_to_fp16.
The adjacent remill-llvm23.patch handles half-precision memory loads/stores with
raw i16/half bitcasts for LLVM 23+, retaining the older code on earlier LLVM.
The patched full build succeeds. Half-value runtime regression coverage is pending.
[Remill source](https://github.com/lifting-bits/remill/tree/66c488c9911fd9b368e4d0c9647d74239f8bba8b)

The first synthetic fixture, mov rax,rdi / add rax,rsi / ret, lifts to LLVM IR
and compiles into an arm64 iOS Mach-O object. Reproduce with probe-lifter.sh.
Its undefined-symbol list exposes the runtime bridges still needed for flags,
guest return, stack reads and FS/GS/stack initialization. The object has yet to
be linked into the mobile probe and executed against reference vectors. Remill
also logs a redundant return-terminator diagnostic for this trace, so inspect
control flow while implementing that harness. Translation output alone does
not complete the P1 CPU proof.

The locally available source-recomp translator uses i386 registers and 32-bit
guest addresses. Its current CPU runtime requires substantial changes for this
x86-64 guest. Remill is the first candidate under experiment. Interpreter
comparison, SIMD/flags/atomics/callbacks/TLS, two-thread device tests, and real
routines from both game modules remain in P1. Backend selection is still open.

### Reproduce the host lifter build

Use a mounted external build root. In a fresh Remill checkout at the pin above,
apply ios/remill-llvm23.patch from this fork with git apply, then follow:

```sh
cmake -G Ninja -S remill/dependencies -B remill-deps -DUSE_EXTERNAL_LLVM=ON -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/llvm -DCMAKE_INSTALL_PREFIX="$PWD/remill-install"
cmake --build remill-deps --parallel 8
cmake -G Ninja -S remill -B remill-build -DCMAKE_PREFIX_PATH="$PWD/remill-install" -DCMAKE_BUILD_TYPE=Release
cmake --build remill-build --parallel 8
/path/to/OpenCrown-PC/ios/probe-lifter.sh "$PWD/remill-build/bin/lift/remill-lift-23" /opt/homebrew/opt/llvm /path/on/external/disk/fixtures
```

Private evidence stays under the external OpenCrown build root in ios-probe/
and ios-cpu-fixtures/, with dependency source/build caches in cpu-backends/.
The device JSON, provisioning files and generated objects stay outside Git.
The next implementation task is a checked guest-memory/runtime harness that
executes lifted fixtures on the Mac reference and both signed device apps.
