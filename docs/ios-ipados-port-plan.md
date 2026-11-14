# OpenCrown on iPhone and iPad

The first release target is offline Dragon’s Crown Pro (CUSA10454, patch 1.03) on an iPhone 17 Pro Max and an iPad mini (A20 Pro). Build one arm64 iOS app on the Mac, sign it for both devices and launch it from each Home Screen. Acceptance requires a playable town and adventure loop on each device, including touch, a controller, audio and a save that survives termination. Keep the tested Mac build available for comparisons.

The Mac implementation at `784fe46` executes the game's x86-64 instructions through Rosetta. It supplies a working reference for loader results, gameplay, saves and the SDL Metal renderer. Eight game shaders already compile and link with the iPhoneOS 27.2 SDK. Device CPU execution, shader draws and sustained performance are still to be measured. [Mac verification](../macos/app-bundle.md) · [Gameplay fixture](../macos/gameplay-smoke.md) · [Earlier feasibility study](macos-port-plan.md#phase-2-feasibility-on-ios-and-ipados)

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

### Current execution order while devices are away

| Milestone | Evidence and remaining gate |
| --- | --- |
| P0 | Both physical devices cold-launch the signed shell and export diagnostics. Passed. |
| P1 | Both pass the original 146-check synthetic corpus. The expanded host corpus passes 1,906 checks across 85 routines plus independent SIMD/x87 oracle suites; actual libc startup returns zero on arm64 Mac. Real-module proof on both physical devices remains open. |
| P2 | Real modules load, relocate, execute and start workers on arm64. Continue executed-target discovery and fix explicitly reported services/instructions until TITLE and INN; then compare event order and save reopening. No successful game boot is claimed. |
| P3 | All three SDK shader targets and synthetic Metal replay/readback pass. Real animated loading frames now render on native arm64 Mac and both simulators. The static loading-text region matches the Mac reference exactly on native host/tablet, with at most one channel-value difference on phone. TITLE, INN and town images remain open. |
| P4 | Touch/controller/audio adapters, live diagnostics and cooperative pause/stop are integrated. Host concurrency/lifecycle checks pass. Actual gameplay, sound, ergonomics, reconnect, interruptions and mobile frame pacing remain acceptance work. |
| P5 | Files import and local transactional save/checkpoint recovery pass host fixtures, including simulated process loss. Build/sign scripts exist. Verify provider behavior, two real save/reloads per device and repeat installation. Disposable iCloud handoff/conflict work precedes any live-save writes. |
| P6 | Requires the physical devices, actual adventure completion and recorded sustained gameplay. Open. |

The immediate critical path is real-module startup, then live replay and a
simulator play/save loop. In parallel, preserve reproducible source/dependency
pins and build the signed universal app. When hardware returns, install the same
qualified build and close each phone/tablet gate separately. Devices being away
does not prevent host/simulator implementation, and does not waive the physical
acceptance criteria.

P0 → P1 → P2 is the critical path. A synthetic Metal draw, fixture capture and import prototype can proceed independently. Live rendering and controls need P2. File-free CI can build the shell and run synthetic CPU/render fixtures. Real-game acceptance runs privately on each device with the same signed build, recording commit, dump hashes, device/OS, input route, log and result. Track phone and tablet passes separately.

The first planning checkpoint is about one to two working weeks for P0 and the time-boxed P1 investigation. A correct CPU backend and a real on-device scene are the inputs for a gameplay estimate. If P1 fails, deliver the smallest failing fixture and backend comparison. If P2 runs slowly, profile guest dispatch, HLE and memory access before investing in presentation. The offline milestones cover locally provisioned installs. Online co-op and public distribution can be planned separately.
