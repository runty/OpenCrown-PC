# OpenCrown on iPhone and iPad

The first release target is offline Dragon’s Crown Pro (CUSA10454, patch 1.03) on an iPhone 17 Pro Max and an iPad mini (A20 Pro). Build one arm64 iOS app on the Mac, sign it for both devices and launch it from each Home Screen. Acceptance requires a playable town and adventure loop on each device, including touch, a controller, audio and a save that survives termination. Keep the tested Mac build available for comparisons.

The Mac implementation at `784fe46` executes the game's x86-64 instructions through Rosetta. It supplies a working reference for loader results, gameplay, saves and the SDL Metal renderer. Eight game shaders already compile and link with the iPhoneOS 27.2 SDK. The iPad executes the real modules and renders TITLE, INN and Town. The matching Simulator save-data prompt sustains about59.5fps after a renderer fix. Two scripted 3,000-frame Town walks on the physical iPad rendered at 59.40 and 59.38 fps. The first Ancient Temple Ruins encounter runs on both physical devices. Physical long-session and boss-clear qualification remain open; the native AOT harness now clears the Harpy, completes appraisal, returns to Town and explicitly saves level-2 progress. [Mac verification](../macos/app-bundle.md) · [Gameplay fixture](../macos/gameplay-smoke.md) · [Earlier feasibility study](macos-port-plan.md#phase-2-feasibility-on-ios-and-ipados)

## Execution design and first decision

The selected implementation is Remill AOT with signed arm64 traces, explicit
guest memory/dispatch and typed HLE. The original decision criteria below remain
the acceptance bar; expanded host tests do not substitute for real-routine
execution and gameplay on both devices. See [current evidence](../ios/progress.md)
and [reproducible build](../ios/BOOTSTRAP.md).

Start with ahead-of-time translation (AOT). At build time on the Mac, lift reachable x86-64 code from the player's matching eboot and libc into C or LLVM IR, compile it to arm64 and link it into the signed app. Record hashes of both inputs so the runtime can reject mismatched game data. Generated game-specific code, maps, archives, binaries and progress stay in the player's private build tree. A new executable revision needs a matching rebuild. The mobile app loads assets and guest data, starts worker threads and renders within one process. [Apple code signing](https://developer.apple.com/documentation/xcode/using-the-latest-code-signature-format) · [Rosetta on Mac](https://developer.apple.com/documentation/apple-silicon/about-the-rosetta-translation-environment)

Before choosing a translator, run a small bake-off. Inspect available lifters for x86-64 long mode, AVX/SSE, flags, indirect control flow, reproducible output and ability to compile offline for arm64 iOS. Compare the most promising candidate against an interpreter on the same microprograms and real hot routines. The interpreter also provides a possible route for unresolved blocks if profiling supports it. Define a common guest state and memory interface before mixing the two. Stop after five active development days for a feasibility report if neither can execute the fixtures correctly. Budget the next stage from measured coverage and timings.

| Existing component | Mobile adaptation |
| --- | --- |
| SELF/ELF reader, relocations, NID binder and HLE | Reuse parsing and service semantics. Represent guest addresses explicitly, including code pointers, data imports and thread-local addresses. Replace generated x86 stubs and direct SysV calls with dispatch to signed functions and typed bridges for arguments, stack, varargs and returns. |
| CPU compatibility patches and texture write tracking | Inventory each patch by purpose. Implement required guest behavior in the translator or interpreter. Track guest and host writes to texture memory explicitly. Compare uploads against a full-content reference, including page overlap and free/reuse. |
| SDL GPU, input, audio and archive formats | Cross-build the portable pieces for arm64. Add iPhoneOS shader variants and device format fallbacks. Reuse action mappings, input fixtures and PS4 saves. |
| Launcher, process and filesystem | Supply an in-process mobile entry point, Files import and lifecycle callbacks. Replace child process start/stop, desktop paths and process-wide exits with start/pause/resume/shutdown. Keep macOS behavior covered by its existing tests. |

## Milestones and exit criteria

1. **P0 — Baseline and device shell.** Pin the Mac code, supported dump hashes and input script. Capture reference events for libc initialization, TITLE, INN and the Felix save (level 1, 351 EXP, 47 gold). Record both device OS builds, available storage and actual page size. Build one minimal provisioned arm64 app, install it with Xcode or devicectl and cold-launch it from each Home Screen with the debugger detached. **Pass** when each device reports its build ID and writes an exportable diagnostic log. Allow 1–2 active days for this and fixture preparation.

2. **P1 — CPU proof on both devices.** Exercise direct and indirect calls, jump tables, integer flags, SIMD, atomics, a guest-to-HLE call, a callback into the guest and independent TLS on two threads. Compare exact register/memory results with an x86-64 reference. Then translate and exercise at least one real routine from each module using pinned inputs. Record unsupported operations, reached targets, throughput, memory and backend revision per device. **Pass** when both physical devices match reference outputs in a signed standalone launch. Use the five-day decision point above to select AOT, interpreter or a bounded hybrid, or report the specific blocker and revised estimate.

3. **P2 — Real headless boot.** Use the selected backend to load both modules, relocate guest memory, bind imports and run libc startup and the main thread. Handle indirect calls through a guest-address map and report the module and guest PC for missing targets. Audit worker entry points, memory ordering, page assumptions and any writes into guest code. Reuse the Mac input script and a disposable slot. **Pass** when each device reaches TITLE and INN on two cold launches, reopens the save and reports advancing submissions/vblanks with zero unresolved *executed* targets. Compare event order with the Mac and measure CPU time and peak resident memory. This is the point to forecast playable delivery.

4. **P3 — Visible Metal frames.** A synthetic draw can run alongside P1. Build the eight game shaders and ImGui shaders for the device SDK. Check SDL bindings, samplers, swizzles, blending, drawable format and BC1/2/3/7 support per device. Exercise a decode-to-RGBA fallback for unsupported formats and measure its memory cost. Replay a fixed fixture before connecting live guest frames. **Pass** when both devices present recognizable live TITLE, INN and town frames. Compare offscreen reference pixels at fixed size, record tolerances for converted formats, and check landscape safe areas and iPad window resizing.

5. **P4 — Mobile play and lifecycle.** Support touch-only movement, simultaneous direction/action presses, menus, pointer actions and name entry. Verify the physical controller through play and reconnect. Measure the actual audio period, underruns and recovery after interruption or output change. Pause workers and GPU submissions on backgrounding, then resume audio, input and display. **Pass** when each device can create a character, finish the tutorial, enter town, fight, return to town, save, terminate and reload progress twice. Record a successful adventure clear separately. Run the sequence with a disposable slot and include background/foreground and termination during a save. [UIKit lifecycle](https://developer.apple.com/documentation/uikit/managing-your-app-s-life-cycle) · [Metal background transitions](https://developer.apple.com/documentation/metal/preparing-your-metal-app-to-run-in-the-background)

6. **P5 — Import, saves and repeatable installation.** Introduce a basic sandbox import by P2. Finish it with a Files folder picker, coordinated copy, free-space check, progress/cancel, validation before promotion, and recovery after interruption. The two CPK archives alone total about 6.14 GB. Test partial copies, wrong patch and offline relaunch. Keep local save checkpoints and recover the last complete slot after a failed write. For the chosen iCloud Drive OpenCrown/saves folder, test with disposable copies first. Verify Mac → phone → iPad → Mac handoff, offline divergence, backup/restore and conflict handling before enabling writes to live progress. Add a pinned build/install command to the private pipeline using its existing device selection and external build root. **Pass** when both devices accept a repeat install, reopen offline and retain progress. Record cloud handoff as a separate result. [Files directory access](https://developer.apple.com/documentation/uikit/providing-access-to-directories)

7. **P6 — Playable acceptance.** Run a 30-minute town/combat loop per device at a recorded resolution and refresh rate. Collect p50/p95 frame intervals, game simulation speed, audio gaps, peak memory, thermal state, dropped frames and any OS termination logs. Aim initially for 60 Hz (16.7 ms per frame). If sustained rendering requires a 30 fps preset, verify real-time gameplay and audio at that preset and publish the per-device result. **Pass** when both devices finish the loop at the declared preset, complete an adventure, save and cold-reload, with no crash, corrupted slot or sustained slowdown. Re-run the Mac regression suite after changes to shared code.

## Delivery and estimates

### Current execution order — 2026-11-15

| Milestone | Evidence and remaining gate |
| --- | --- |
| P0 | Both physical devices cold-launch the signed shell and export diagnostics. Passed. |
| P1 | iPad passes the expanded 1,906-check / 85-routine probe, then executes actual libc startup and the game. The iPhone passed the original synthetic corpus and now runs the real game and libc through the first Ruins fight. |
| P2 | The iPad reaches TITLE and copied-Felix INN with checked component/frame advancement and no error. The current build cold-reopens the same working save twice without reseeding; both images show Felix LV1. The physical iPhone also runs through TITLE and INN to combat. |
| P3 | Visible TITLE passes on iPad and both simulators; visible INN and Felix pass on iPad and native host. Native and physical iPad Town pass extended advancing-frame gates with clean inspected images. The physical iPhone also presents Town, the outbound gate and Ancient Temple Ruins. |
| P4 | Real interactive audio/input adapters run. The user reports severe iPad save-dialog stalls; the matching Simulator reproduction is reduced from roughly two-second replay stalls to about 10 ms and 59.46 fps. Clean fix passes a 180-second physical iPad prompt run at about10 ms replay work with zero audio underruns/clipping and a complete image. Simulator touch also loads Felix, walks in Town and opens Stats with the retained values. The signed iPad build completed consecutive 3,000-frame walks at 59.40 and 59.38 rendered fps with p95 intervals of 16.82 and 16.84 ms. The first goblin fight measured 59.40 fps on iPad and 54.28 fps on iPhone with real audio. A native fresh Fighter completes the tutorial and later opens Equipment with the six-case switch fix; packaged Simulator also opens that menu. The native route also defeats the Harpy, appraises/obtains both spoils, returns to Town and explicitly saves new level-2 progress. Simulator screen pointing, chest opening and tablet portrait/landscape targets now pass with non-consuming controller metadata. A fresh touch-only Fighter also completes name editing, simultaneous controls, all tutorial lessons, door/chest taps and Orc, reaches Town and writes a new save that cold-reopens three times, including two settled visible-Town runs. The player now confirms touch/menu, controller input, overlay hiding/return and reconnect on the physical iPhone, with a disposable R save and muted output. A Home Screen observation also shows guest/audio paused without errors. The player skips the additional explicit save/foreground check, which remains unqualified. The full fresh-character sequence, iPad controls, audio/output interruptions and sustained gameplay remain open. |
| P5 | Manual Files save import/export now stages SHA-validated copies and backs up local progress before replacement. The storage core passes 149 checks and the savedata integration passes 108. Simulator Files export preserves the exact native combat save, and a host-simulated three-hop handoff retains that hash and the replaced Feli backup. A local-provider Simulator export/import round trip also restores the exact edited-save hash over a different combat save while retaining that replaced save as a checkpoint and previous copy. That first test uses the remembered export location. A later separate-container QA test browses an ordinary local Files folder on first use with no remembered URL, imports the exact adventure save over the touch-earned save, and preserves both backups. Cancel and deliberately damaged-checksum selection leave local/checkpoint bytes unchanged. App-container rows fail in both new and existing Simulator environments, including Apple Files; hardware Files and remote iCloud remain unqualified. Files game import and transactional save recovery pass host fixtures. Repeated iPad installs preserve assets and save/checkpoint files. Two existing-save character reloads pass on the physical iPad. The iPad Simulator changed Felix’s armor, saved on leaving the Inn and preserved the visible change and exact new save hash across two cold launches. A native combat-written save also reopens twice in Simulator with the exact new hash and visible Town. Simulator termination with an active prewrite journal recovers the previous save and visible armor color. A later Simulator kill after SAVE0.DAT changed also restores the prior save, retaining the exact interrupted bytes. Native tutorial-earned and later guild-progress saves each retain exact hashes through two native and two packaged Simulator cold reloads. The latter uses explicit Report Adventures saving. The new Harpy-clear/appraised-spoils save also retains its exact hash through two native and two packaged Simulator visible-Town cold processes without reseeding. Physical interruption, earned adventure progress on hardware, provider behavior and disposable iCloud handoff/conflicts still need qualification. |
| P6 | A longer `851c06b` Town walk falls to 37.62 fps over its final minute in serious thermal state, with one 256-frame audio underrun; it is stopped after 416 observed Town seconds and is not a 30-minute pass. Lock-region/table experiments have native CPU proxy gains but no proven sustained phone benefit; stable pin remains `851c06b`. Current iPhone `851c06b` runs a short first-fight / walking-Town sample at 51.63 / 51.43 fps in fair thermal state. Two shorter idle-Town loads of the new Simulator-earned save reach 59.35 / 59.05 fps; these differ in input/character and do not qualify sustained 60. An attached 25-second Town profile identifies scalar memory locking and texture comparisons for investigation, and reaches serious thermal state. Repeated short iPad walks are recorded in the [device progress ledger](../ios/progress.md). The first fight also runs on the physical phone at 54.28 fps. The 30-minute Town/combat qualification, sustained real-time audio and simulation checks and adventure clear remain open. |

The mobile screen-pointer adapter at `ce065bb` now passes actual packaged
Simulator steering and first Ruins chest interaction. UIKit virtual controls
reach the encounter, then a short screen tap makes Rannie unlock/open the
chest and displays the original treasure tutorial. The guided 15,014-frame run
stops through the public launcher flow with no runtime/frontend/save error,
13,149 cursor samples and zero read errors. The disposable save remains exact.
The later fresh touch-only Simulator tutorial below closes the door/tutorial
gate there; physical qualification remains open. Fixture/model convergence
alone is not used as runtime acceptance.

A tablet follow-up found that controller-information queries consumed a second
pointer step and could strand long moves at touchpad edges. Runtime `7bc7603`,
packaged as `280b856`, makes metadata non-consuming. Four actual tablet Simulator
targets across portrait/landscape are within 0.4 pixels; its touch-only Ruins
route also opens the first chest and stops cleanly after 41,403 frames with
unchanged save bytes. This is input/cleanup evidence, not sustained timing.

The fresh touch-only Simulator route now also passes edited name, simultaneous
movement/action, potion/food, door/chest and Orc, then writes new Town progress.
Three cold processes preserve that new hash; two show settled Town. The private
UI helper completes teardown and exits 0 with one passing test. The guided
route records a Simulator audio restart error, remains muted at the user's
request and does not qualify sustained timing or audible recovery.

The `4741c15` opt-in texture version path now passes native and packaged
Simulator audits against every otherwise skipped full comparison, with zero
proof failures. Nine interleaved native R/Town walks show 33.08 → 30.07 seconds
of median checkpoint CPU time (9.10% less than the prior build), and replay
work 2.551 → 0.393 ms. Native FPS remains capped near 59.4. Versions remain
off by default and stable pin stays `851c06b`: next compare off/on on the
available phone using the same package, thermal state and disposable save,
then qualify sustained Town/combat. The iPad remains excluded. These native
proxy/audit results do not close the physical P6 gate. See the
[snapshot protocol](../ios/cpu/review/snapshots.md) and progress ledger for
writer coverage, conservative fallback, package provenance and exact evidence.

`bb540ac` also makes mixed capture/statistics cache ABIs fail at link time.
Current capture/backend/replay callers link and execute with rebuilt archives;
all three reject both pre-version and pre-guard caches. Both SDK packages and
native/packaged Simulator Town audits pass with these guards. After the player
unlocks the phone, the guarded package installs while preserving all 64
existing non-asset files byte-for-byte. Four interleaved same-package R/Town
walks, all nominal with Low Power Mode off, measure median 54.46 fps off versus
59.40 on and 37.14% less checkpoint CPU. Real muted 256-frame audio callbacks
report zero underruns/clipping/errors. An independent phone full-content audit
checks 25,286 hits / 114.88 GB of logical spans with zero mismatches. These are
short-run results: the ongoing long Town run reaches serious thermal state
and later drops below 60, so sustained acceptance remains open. The earlier
installed `110904c` experiment changes lock geometry and is not the baseline.

The same guarded cache now passes native and packaged Simulator Town → world
map → Ruins first-encounter audits, with zero proof failures across new uploads
and cache evictions. The [adventure-save route](../ios/host/routes/ruins-from-adventure-save.txt)
adds late Cross confirmations for the observed Inn menu; the earlier route
could begin movement before Leave tavern became ready and later open Options.
This route requires the qualified adventure save, not the fresh R save whose
Guild introduction is still pending. Simulator success does not qualify device
combat, a complete adventure or real platform-audio recovery.

Continue without user input in this order:

1. The [fresh touch-only route](../ios/touch-playthrough.md) now passes in packaged iPhone Simulator: edited name, simultaneous movement/action, potion/food, door/chest, Orc and new Town save. Three cold processes retain its exact hash; two qualify settled visible Town. First-use ordinary local Files folder browsing now passes in a separate-container QA harness; app-container rows and remote provider handoff/conflicts remain open. The player has released the iPhone for testing and explicitly excludes the physical iPad. Use only the iPhone for current device work, with `DCP_MUTE_AUDIO=1`; this keeps real audio callbacks/accounting while zeroing output buffers. Retain native full Ruins/Harpy/reward/save and tablet target/rotation evidence separately. The player confirms iPhone touch/menu and controller/reconnect/overlay behavior; the additional explicit save/foreground check is skipped at the player’s request and remains unqualified. Repeat the full fresh/adventure route, map/potion use and sustained timing on hardware. Keep the iPad unused until the player releases it. Never use normal player saves or live cloud progress for experiments.
2. Retain the 50,292-entry save-dialog baseline and use the 50,464-entry character-selection/edit/food/equipment/status/results/reward build with a matched save/input route to compare Town changes. The signed build uses the native CRILAYLA decoder for 91 of 94 observed calls. Texture counters at `ee59b0c` show about 66.38 MB of successful unchanged comparisons per consumed native frame across startup plus Town, with no retention misses; the packaged Simulator independently confirms the repeated work. `fe8f18a` publishes native file output through checked memory copies. `e9369f1` adds checked font/IME guest output, exact original-renderer glyph comparisons and functional original-LOCK proof, with real native/Simulator font setup. Town invokes no dynamic glyphs; that path remains fixture evidence. Snapshot-bound versions at `4741c15` now implement the [writer audit](../ios/render/texture-coherence.md), with conservative full-content fallback for opaque native-owned objects and escaped/unknown mappings, exact snapshot/mapping identity, all tracked stores and concurrency coverage. ABI guards at `bb540ac` prevent mixed old caches from linking. Next audit scene transitions/combat, then compare the same guarded package off/on on the phone before default enablement or pin promotion. Preserve bounds checks, cancellation and original guest LOCK behavior. Treat lock geometry experiments separately; native proxy gains and short device runs do not prove sustained 60 fps.
3. The iPad has twice cold-reopened its existing diagnostic working save with Felix visible and no reseeding. The iPad Simulator has also saved and twice cold-reloaded a new armor change. The native tutorial-earned Darrel save reopens twice in packaged iPhone Simulator. The new native adventure save now survives two native and two packaged Simulator visible-Town cold processes with exact hashes. The current iPhone also cold-reopens the new Simulator touch-earned R save twice with exact hashes and recognizable Town. Fresh-character/earned-progress creation on hardware remains open. Never use normal player roots or live cloud progress for these experiments.
4. Investigate the dedicated Simulator's AVAudioEngine startup RPC timeout, also reproduced in a minimal app without game code. Qualify current real input/audio/lifecycle and performance on the available iPhone. Do not use the physical iPad during this window; retain its earlier results separately until the player releases it. Simulator success does not substitute for either device.
5. Finish disposable cloud-provider handoff/conflict cases and sustained gameplay measurements before enabling live progress writes or declaring the port complete.


P0 → P1 → P2 is the critical path. A synthetic Metal draw, fixture capture and import prototype can proceed independently. Live rendering and controls need P2. File-free CI can build the shell and run synthetic CPU/render fixtures. Real-game acceptance runs privately on each device with the same signed build, recording commit, dump hashes, device/OS, input route, log and result. Track phone and tablet passes separately.

The first planning checkpoint is about one to two working weeks for P0 and the time-boxed P1 investigation. A correct CPU backend and a real on-device scene are the inputs for a gameplay estimate. If P1 fails, deliver the smallest failing fixture and backend comparison. If P2 runs slowly, profile guest dispatch, HLE and memory access before investing in presentation. The offline milestones cover locally provisioned installs. Online co-op and public distribution can be planned separately.
