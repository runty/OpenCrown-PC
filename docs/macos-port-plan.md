# OpenCrown macOS port plan

Build a playable macOS version of Dragon's Crown Pro from the public OpenCrown
sources, running the player's PS4 executable under Rosetta 2 and drawing through
SDL3's Metal backend. Prove game execution before committing to graphics and app
packaging work.

The 2026-11-13 Darwin implementation builds the complete headless runner.
The supported dump starts under Rosetta, creates and reloads a disposable save,
and reaches TITLE and INN using scripted input. See the [Mac build instructions](../macos/README.md).
Metal game frames and the packaged app remain open. The milestone tracker below
is the execution checklist. A milestone is
complete only when its acceptance checks have evidence attached to its issue.

## Baseline and scope

- Upstream: [Nezara/OpenCrown-PC](https://github.com/Nezara/OpenCrown-PC).
- Fork: [runty/OpenCrown-PC](https://github.com/runty/OpenCrown-PC).
- Source baseline: [`88e7e0118614e22743c9691a55588f1045cab873`](https://github.com/Nezara/OpenCrown-PC/commit/88e7e0118614e22743c9691a55588f1045cab873),
  public PC/Linux version 1.0.10, committed 2026-11-11.
- Development branch: `codex/macos-port`; keep `main` as the upstream baseline.
- Initial platform: Apple silicon with working Rosetta 2. Validate first on the
  reference Mac's current macOS and Xcode; publish an older deployment minimum
  only after testing it. Intel Mac support is a later compatibility target.
- Game: decrypted Dragon's Crown Pro, `CUSA10454`, supported patch 1.03, with its
  matching `sce_module/libc.prx` and game archives. `APP_VER` can still read 01.00
  after merging an update; use the executable signatures in
  `linux-port/tools/common/game_version.h` as the version gate.
- Alpha scope: offline play, working graphics/audio/input, persistent saves,
  launcher and a self-contained app runtime. Track Windows/Linux co-op and public
  binary distribution independently of the local offline alpha.
- Deferred: ARM-native game execution, a separate GPU process, Android/Vita work,
  new gameplay features, MetalFX, and automatic installation of updates.
  Upstream's experimental Customize feature has unpublished source, so it cannot
  be promised as part of this public-source port.

On the reference M5 Max, macOS 27.2.1 and Xcode 27.2, the pinned validators
accepted both SELF modules, four supported-version signatures and both CPK
table bounds. Payload and gameplay comparisons remain open. A translated process
reported 4 KiB pages, compared with 16 KiB in a native arm64 process. An adapted
synthetic loader fixture called a host shim successfully. A translated Metal
probe submitted a command buffer. All eight shaders compiled and five RGBA8
offscreen pipelines were created using cached SDL 3.4.16. Real guest startup,
draws, presentation and a repeat on pinned SDL 3.4.18 are outstanding.

## Architecture decisions

1. **Extend the Linux source tree with Darwin support.** Preserve its Linux build
   entry points and Windows tree. Put OS differences behind a small host layer
   in `linux-port/src/platform/`, with Linux and Darwin implementations. Add Mac
   build/package entry points under `macos/` and extend the root `.gitignore`
   whitelist when those files are introduced. Avoid a third copied engine tree.
2. **Start with one x86-64 runner process.** Build the runner and every linked
   dependency for x86-64. Rosetta executes host and loaded PS4 code; SDL submits
   graphics to Metal from the same process. Do not assume an ARM library can be
   linked into it. A native launcher is optional after the alpha works.
3. **Keep capture and replay.** Reuse `render/capture.cpp`, `backend.cpp`, texture
   detiling and the `Device` interface. Supply MSL/metallib shaders to the existing
   SDL GPU device rather than designing a new PS4 GPU translator.
4. **Separate guest and host rules.** Guest page alignment, structures, error
   numbers and calling convention remain PS4-compatible. Host allocation,
   signals, clocks, sockets, paths and assembly symbols follow Darwin. Query the
   actual host page size rather than substituting 16 KiB everywhere.
5. **Use disposable test saves.** Tests set both `DCP_USER_DIR` and
   `DCP_SAVE_ROOT` to a run-specific directory. Import real progress only through
   a backed-up copy. Cloud sync is a separate opt-in decision before enabling
   it; never point an initial run at existing cloud progress.

## Milestone schedule and dependencies

Effort below is active developer time for one experienced developer with the
reference dump and Mac available. It is a planning range, not a delivery date.
External test partners, hardware availability and distribution permission can
add waiting time. Re-estimate after M3 and M5.

| Milestone | Outcome | Depends on | Effort |
| --- | --- | --- | --- |
| M0 | Reproducible source and dump baseline | None | 0.5–1 day |
| M1 | Darwin build and host compatibility layer | Pinned M0 inputs for probes; Linux comparison for sign-off | 1–2 days |
| M2 | Safe executable memory and hooks under Rosetta | M1 | 2–4 days |
| M3 | Headless game reaches and advances scenes | M2 | 1–3 days |
| M4 | Correct frames through SDL GPU and Metal | M3 | 2–4 days |
| M5 | Responsive offline game with reliable saves | M4 | 2–3 days |
| M6 | Mac launcher and persistent settings | M5 | 1–2 days |
| M7 | Co-op verified against PC/Linux | M5 for protocol/play; M6 for UI; M8 for packaged TLS check | 1–3 days |
| M8 | Relocatable local app and pipeline integration | M6 | 1–2 days |
| M9 | Verified local offline alpha | M8 | 2–3 days |

The local offline path runs from the pinned M0 inputs through M1–M6, M8 and M9.
Start host probes while the Linux comparison is being prepared. M7 can proceed
after M5 and finish when a compatible peer and the packaged app are available.
The ranges for all ten milestones sum to 13.5–27 active days before contingency.
Use 4–7 working weeks as a provisional Mac budget. Re-estimate after a repeatable
M3 scene transition and an M5 playable save/reload. Track peer availability and
public distribution waiting time separately.

## M0 Reproducible baseline

**Work**

- Record the upstream commit, compiler/SDK, OS, machine and Rosetta availability.
  Keep personal machine paths and dump inventories in private local notes.
- Verify supported executable signatures, SELF segment flags, required matching
  modules and CPK table bounds using upstream's dump-check rules. Hash the
  executables for local reproducibility; never commit executable bytes or assets.
- Capture a repeatable Linux route at the source pin with a disposable save,
  scripted inputs, scene identifiers and reference images. Mac CPU, loader and
  shader probes can begin as soon as their own inputs are pinned.
- Record default and advertised CPUID, XCR0 and instruction results per tested OS.
  On macOS 27.2.1 a 256-bit AVX comparison passed with default CPUID reporting
  AVX=0. Starting with `ROSETTA_ADVERTISE_AVX=1` exposed AVX, OSXSAVE, F16C
  and MOVBE. The actual runner rejects the default CPU report after its dry-load
  path. Limit compatibility claims to verified builds. Apple describes narrower
  game support starting with macOS 28. [Rosetta availability](https://support.apple.com/en-us/102527)
- Inventory current tests and establish their Linux results at the pinned commit
  in an available Linux environment. Record lack of a Linux comparison runner as
  a remaining validation requirement rather than claiming a passing baseline.
- Record source/public-binary differences, especially Customize. Preserve the
  upstream attribution and third-party notices. The root currently has no project
  license file: settle redistribution terms before offering downloadable modified
  builds; this does not prevent recording the plan and local compatibility work.
- Keep dumps outside the checkout and outputs outside dump folders. Use the
  Office build root while mounted; retain dependency and shader caches.

**Acceptance**

- [ ] A baseline report identifies exact inputs, supported dump and test results.
- [ ] Logs identify both modules, version and CPK table bounds. Record any
  additional payload verification separately.
- [ ] A pinned Linux route has reproducible input, save state and comparison
  evidence. CPU capability results include the OS build and launch environment.
- [ ] Repository tracking contains only source/docs and allowed notices.

**Deliverable:** baseline report and commands; Linux comparison evidence.

## M1 Darwin build and host compatibility

**Work**

- Teach `linux-port/CMakeLists.txt` to accept Darwin and select platform sources;
  retain Linux behavior and reject accidental ARM builds of the game runner.
- Add `macos/build.sh` with explicit build-root, configuration, headless/rendered
  selection and x86-64 architecture. Use Apple Clang/libc++; avoid Linux-only
  compiler/linker assumptions and `nproc`. Build all fetched dependencies for the
  same architecture; pin download hashes and record versions.
- Start with `DCP_RENDER=OFF`. This still links networking, FreeType and the
  texture tracker, so their Darwin compatibility cannot be deferred to M7.
- Implement host socket creation/accept/close-on-exec and SIGPIPE behavior for
  `SOCK_CLOEXEC`, `accept4`, `MSG_NOSIGNAL` users in `src/net/`. Use Darwin secure
  randomness for `identity.cpp` instead of Linux `getrandom`.
- Replace GNU-only threading features. In particular, preserve monotonic timeout
  semantics when replacing `pthread_condattr_setclock`; do not feed a monotonic
  absolute deadline to a realtime condition variable. Replace
  `pthread_tryjoin_np` with explicit completion state and an ordinary join.
- Build, assemble, link and execute the synthetic loader independently of the
  full dependency build. Apple Clang can parse the trampoline before its Mach-O
  assembler rejects the ELF call. Check `mincore` types and Darwin register
  access as well as the existing socket, clock and mapping changes.
- Port the assembly trampoline in `loader/hle.cpp`: Mach-O C symbol spelling and
  calls instead of ELF `@PLT`. Verify stack alignment and guest callback ABI.
- Introduce platform seams for safe memory reads, fault context access and memory
  allocation. Complete their runtime semantics in M2 rather than hiding failures
  behind success-returning stubs.

**Acceptance**

- [ ] The isolated loader target links and executes under Rosetta. The CLI logs
  its pre-launch CPU environment and reports actionable unsupported features.
- [ ] Headless runner and existing CLI/test targets compile with Apple Clang.
- [ ] `file`/`lipo` confirm x86-64 runner and dependencies; no mixed architecture.
- [ ] Applicable format, HLE, socket, lobby, update and input-math tests pass under
  Rosetta; any execution-dependent tests remaining for M2 are listed explicitly.
- [ ] Linux configure/build/tests retain their baseline behavior.

**Deliverable:** repeatable headless build and a short host compatibility API.

## M2 Executable memory and fault handling

This is the first major technical decision gate. Focus files:
`loader/{elf,hle,cpu_compat}.cpp`, `hle/kernel/pure.cpp`,
`render/{capture,texwatch,backend}.cpp`, `tools/dcp_run/main.cpp` and patch helpers
in `hle/{skip_offers,confirm_button,own_camera}.cpp`.

**Work**

- Replace `MAP_FIXED_NOREPLACE` with non-clobbering Darwin reservation logic;
  verify returned addresses and handle collisions. Never approximate it with
  unrestricted `MAP_FIXED`. Audit Mach-O's reserved low address range: the guest
  can relocate if 0x400000 is unavailable. Check address-dependent patches.
- Preserve direct-memory allocations below the 40-bit GPU address limit and
  relay allocations within signed rel32 reach of hook sites. Test collisions,
  overflow, alignment and failed reservations with synthetic mappings.
- Separate allocate/write/protect/execute operations. Test generated x86-64
  thunks and loaded-image code under Rosetta, then patch-and-rerun behavior to
  detect stale translations. Determine signing/entitlements from observed policy
  requirements; do not treat an ARM `MAP_JIT` recipe as a universal x86 solution.
- Port Darwin signal contexts and fault classification, including possible
  SIGBUS versus SIGSEGV delivery. Retain alternate stacks and ensure unrelated
  faults reach the crash reporter. Keep optional single-step probes disabled
  until independently proven; production hooks must not depend on them.
- Replace `process_vm_readv` in both the runner and renderer with a checked Darwin
  mechanism. Preserve range validation and allocation lifetime checks; plain
  `memcpy` is not a safe equivalent for a speculative guest pointer.
- Probe the actual translated CPU feature set, including AVX/F16C and the
  MOVBE/BMI1/LZCNT replacement path. Existing CPUID checks and patch tables need
  runtime validation, not an assumption based on hardware name.

**Acceptance**

- [ ] Synthetic code executes, calls a host shim and returns correct results.
- [ ] Patched code executes its new behavior repeatedly, without stale results.
- [ ] Mapping failures/collisions are safe; direct memory and relays meet limits.
- [ ] Test a relocated image and concurrent per-thread guest TLS/callbacks. List
  required CPU patches, expected and installed sites, executed comparisons and
  skips. Explain every missing required site or skipped comparison.
- [ ] Re-run mutation after first execution in the signing configuration used by
  the local app. Repeat for any different signing configuration offered later.
- [ ] An intentional unrelated fault produces a useful guest-relative report.

**Decision:** proceed if these pass. For a blocker, record a minimal reproduction,
affected OS/CPU and a bounded alternative before changing architecture. A native
GPU subprocess cannot fix guest CPU or executable-memory failures.

**Deliverable:** Rosetta memory/ABI probe, test results and execution policy notes.

## M3 Headless game execution

**Work**

- Run `dcp_run <dump>/eboot.bin --dry`; verify both modules, relocations, imports,
  data bindings and TLS registration. Unknown NIDs are not automatically failures;
  distinguish expected unimplemented imports from calls required to progress.
- Enter libc initialization and game entry on the dedicated game thread. Audit
  stack sizes, guest callback types, TLS isolation, mutex/condition/event behavior,
  file structures and errno translation when a real guest call exposes a mismatch.
- Use a disposable save root and scripted input to reach the title, create/load a
  character, enter the inn/town and start an adventure. Record actual observed
  checkpoints instead of treating old loader README frame numbers as contracts.
- Exercise bounded `--frames` runs and clean exits, including a no-save first run
  and a second run reading its newly created save.

**Acceptance**

- [ ] Identify the mapped eboot and matching libc, relocation and binding
  categories, TLS registration, and unsupported relocations. Record libc startup
  and guest entry from the real run. An exit status alone cannot establish these.
- [ ] Observe advancing flips, active submissions and an input-driven scene
  transition corresponding to the Linux route during a 10-minute run. Record
  scene evidence independently of the vblank limit used for a bounded exit.
- [ ] Scripted input changes scenes; no fatal fault, unresolved required import
  or sustained stalled fence appears.
- [ ] Fresh save creation and a second-process reload work in the test directory.
- [ ] Before/after dump inventory confirms the dump remains unchanged.

**Decision:** re-estimate the rest of the port using observed runtime behavior.
Do not call a successful dry load or an idle process a working game.

**Deliverable:** reproducible headless smoke script, logs and scene checkpoints.

## M4 Metal frames

**Work**

- Enable rendered builds using pinned SDL3 and Dear ImGui. Keep SDL video/window
  events and presentation on the process main thread; run guest work separately.
- Select SDL's Metal driver and advertise MSL/metallib availability. Update
  `render/gpu_device.cpp`, `tools/dcp_run/render_loop.cpp` and shader loading,
  retaining SPIR-V for Linux and existing Windows behavior.
- Generate all eight current shader assets: main vertex/fragment, present
  vertex/fragment, FSR vertex, EASU, RCAS and downscale fragment. Prefer an offline,
  pinned SPIR-V-to-MSL toolchain; validate resource reflection and embed results
  so the runtime needs no shader compiler installation.
- Validate SDL Metal texture/sampler/uniform bindings and vertex layouts,
  std140-to-MSL padding, clip depth, winding, scissor origin and image orientation.
  Check BC1/2/3/7 texture sampling support on the actual GPU and add a tested
  conversion path only where necessary.
- Make texture write tracking work on the actual protection granularity, with
  concurrent writes, host writes before file reads, unmaps and address reuse.
  Review the existing handler's locks/allocations: avoid deadlock if a fault
  interrupts a thread holding tracker state. If temporarily falling back to
  dirty marking or hashing, prove it detects complete texture updates and measure
  its cost; a head-only hash is not sufficient evidence of correctness.
- Bound concurrent fault tests with timeouts. Cover overlapping textures, writes
  outside the first 512 bytes, host reads into watched pages and freed or reused
  addresses. Compare output against a complete-content reference.
- Exercise clear/load behavior, alpha/blend equations, self-sampling snapshots,
  texture invalidation, transfers, fences, screenshot readback and GPU overlays.
- Compare stable screenshots against a matching Linux/Windows run and the same
  save/input checkpoints. Use known synthetic draws for precise pixel checks and
  tolerate documented backend rounding differences in game images.
- OpenGL may help diagnosis with a supported Mac core context, but is not a
  prerequisite or a substitute for the Metal acceptance checks.

**Acceptance**

- [ ] Force `--renderer gpu`, verify the selected Metal driver and use pinned
  SDL 3.4.18. Load each shader with its reflected resource counts and generated
  entry point (`main0` for the probes). Replace the current hardcoded `main`
  selection for Metal. Check ImGui's own metallib entry points separately.
- [ ] Render synthetic pixels and verify each requested screenshot exists,
  decodes and matches its checkpoint. Exercise a visible swapchain, overlays and
  display scaling in addition to hidden-window readback.
- [ ] Bounded texture tracking tests and a full-content reference agree after
  overlapping writes, host writes, unmap and address reuse.
- [ ] Title, text/font atlas, inn, town and combat render correctly; screenshots
  have correct orientation, masks, colors and transparency.
- [ ] Name-entry/chat overlays and readback work through SDL GPU.
- [ ] No Metal validation error or black/partially stale frame in the smoke route.

**Deliverable:** reproducible shader generation, image comparisons and first-frame
evidence from the actual game.

## M5 Offline play, audio, input and saves

**Work**

- Validate SDL audio initialization order on macOS, mixing, music/voice/effects,
  device changes and recovery after focus changes. Check long-session audio drift.
- Exercise keyboard/mouse, controller mapping/hotplug, fairy pointer, name entry,
  confirmation layout, rumble where supported, and pause/menu behavior.
- Check Retina backing sizes separately from logical window sizes, borderless
  fullscreen, resize, display changes, VSync, FSR/sharpening and 1080p versus 4K.
- Use a Mac application-support default for state and a Mac logs directory;
  preserve `DCP_USER_DIR` and `DCP_SAVE_ROOT` overrides. Keep settings user-editable.
- Test new saves, copied PC saves, quit/relaunch and failed/unwritable save paths.
  Compare unchanged save copies byte-for-byte; gameplay changes the save, so a
  successful save/load round trip also needs in-game progress verification.

**Acceptance**

- [ ] Complete at least one adventure and return to town during a 60-minute run.
- [ ] Save progress, quit fully and recover the same character/progress twice.
- [ ] Inject short and interrupted writes in disposable save tests, including
  truncated PARAM/ICON sidecars; verify recovery and reload known character
  progress in a fresh process. Record slot and sidecar results independently.
- [ ] Keyboard/mouse and one controller work, including disconnect/reconnect.
- [ ] No persistent audio drift, visual corruption, input loss or unexplained
  memory growth; record cold and warm behavior separately.
- [ ] Count produced, queue-dropped, replayed and presented frames. Measure
  p50/p95 presentation intervals and missed-frame rate on the same scene,
  resolution and refresh rate, cold and warm. Capture discards the oldest queued
  frame when three are pending; the F1 averages cannot expose those losses.
  Target 60 fps on the reference Mac; diagnose sustained misses rather than
  declaring the average frame rate sufficient.

Use the measured save/reload session and frame timings to update the remaining Mac estimate.

**Deliverable:** playable developer build, session checklist and performance report.

## M6 Mac launcher and settings

**Work**

- Adapt `tools/launcher/main.cpp` and shared path helpers. Replace `/proc/self/exe`
  with bundle/executable discovery, `xdg-open` with Mac opening behavior, and GNU
  spawn helpers with supported Darwin equivalents. Preserve argument boundaries
  for spaces and Unicode without shell interpolation.
- Choose the smallest working launcher graphics path: existing ImGui/OpenGL with
  a supported Mac core context, or the available ImGui SDL GPU backend using
  Metal. Keep runtime state and runner discovery independent of the working dir.
- Validate SDL native folder selection, dump/version diagnostics, missing external
  disk handling, settings persistence, save-folder selection and log access.
- Make unsupported update behavior explicit. Do not let Mac users download or
  install an upstream AppImage/Windows binary. Initially offer the fork's release
  page; automatic app replacement is deferred.
- Keep bug-report preview/redaction, Mac OS identification and opt-in submission
  behavior. Do not send test bug reports to the upstream service.

**Acceptance**

- [ ] Browse, validate, Play, stop and relaunch work without a Terminal session.
- [ ] Paths with spaces/Unicode and a missing/reconnected dump disk work correctly.
- [ ] From Finder, the launcher sets the measured CPU environment before starting
  the sibling runner. Log effective features and exercise required operations on
  the guest and worker threads. Confirm fonts and controller prompts in the
  launcher and in-game overlays on a clean account.
- [ ] Settings survive relaunch and reach the runner; bad inputs have useful errors.
- [ ] Updates cannot select an incompatible platform asset; no report is sent
  during automated tests.

**Deliverable:** functional Mac launcher and documented state/log locations.

## M7 Networking and cross-platform co-op

**Work**

- Run existing loopback socket, RUDP, lobby, HTTP/TLS and updater tests using
  Darwin adapters from M1. Preserve protocol framing, byte order, timeouts,
  retry/error behavior, identity storage and TLS certificate validation.
- Exercise disconnect/reconnect, NAT traversal/STUN and best-effort UPnP; failure
  to map a port must not break offline play. Validate firewall-denial diagnostics.
- Use a local mock lobby first. Coordinate an intentional live session with a
  compatible Windows/Linux client, testing Mac host and guest, friend joins,
  chat, separate-camera negotiation, adventure transitions and returning to town.
- Test across two distinct networks when available; same-LAN success is not proof
  of Internet NAT traversal. Record platform/version pairs and network conditions.

**Acceptance**

- [ ] Run the loopback protocol suite. From the packaged process, record the
  trust source used and verify a valid TLS chain and hostname. Exercise rejection
  of an untrusted chain and a wrong hostname. The default mock uses plaintext.
- [ ] On LAN, test Mac hosting and joining a named PC client version, including
  a 30-minute adventure and clean disconnect. Record platforms and builds.
- [ ] Friend join, chat and matching-version camera behavior are verified.
- [ ] Internet test passes, or the alpha explicitly lists Internet co-op as
  unverified and M7 remains open; never label an untested scenario complete.

**Deliverable:** compatibility matrix, reproducible mock tests and co-op evidence.

## M8 App bundle and console pipeline

**Work**

- Add a reproducible `macos/package.sh` producing `OpenCrown.app`, with the compiled
  launcher as its executable, runner, required libraries, shaders, original app
  icon and third-party notices. Game files remain external and read-only.
- Set bundle metadata and relative library paths. Verify recursive dependencies
  with `otool` and architecture checks; the app must not depend on Homebrew, the
  source checkout, build outputs, Xcode or a configured shell environment.
- Ad-hoc sign local bundles with only required entitlements and verify them.
  Establish separately what downloaded/quarantined builds need; notarization
  requires appropriate signing credentials and is not implied by ad-hoc success.
- In `runty/console-pipeline`, add `ps4/opencrown-macos.sh` following existing
  wrapper conventions: pinned fork commit, dump preflight, mount checks, Office
  build-root preference, resumable build/package/install and named per-step logs.
  Keep Office dependency/shader caches; no destructive tidy or source reset.
- Install to `/Applications/OpenCrown.app` only during the implementation install
  pass. Link the runbook from `ps4/README.md`; update `GAMES.md` only with measured
  installed/working status. Do not add mobile install obligations for this port.

**Acceptance**

- [ ] Finder launch works after copying the app to another directory and hiding
  the build tree; own dump disk is the only external game dependency.
- [ ] A fresh user can open the copied app from Finder, locate the external
  dump, start the sibling runner with working CPU detection, and reload a save
  after relaunch. Verify fonts, libraries and shaders without developer tools.
- [ ] Signature verification passes; state survives app replacement.
- [ ] Pipeline wrapper reproduces the pinned bundle and fails clearly for a
  missing disk, unsupported dump, missing Rosetta or failed step.

**Deliverable:** relocatable local app, pipeline wrapper and installation runbook.

## M9 Alpha verification and release

**Work**

- Run the offline rows of the validation matrix below, including a second Apple silicon machine or
  macOS version if available. Separate tested minimum OS from build deployment
  settings; list untested hardware rather than promising support.
- Repeat a clean dependency build and a warm rebuild. Record exact source/tool
  versions, commands, checksums, build times, startup times and cache locations.
- Perform a two-hour gameplay soak with scene transitions, combat, save/reload,
  audio and controller reconnect; sample memory at comparable checkpoints.
- Verify update/reinstall preserves settings, identity and saves. Exercise disk
  disappearance and failed save writes without corrupting existing progress.
- Resolve redistribution terms from M0 before publishing a downloadable modified
  build. If unresolved, deliver local build instructions and keep binary release
  outstanding. Preserve notices/attribution and exclude game assets from packages.
- Prepare an alpha tag, checksums, release notes, known issues, supported inputs
  and a minimal bug-report template. Publish only when the release checks pass.

**Acceptance**

- [ ] Milestone evidence links identify the exact tested commit and machine/OS.
- [ ] No unresolved crash, save-corruption bug or blocker on the documented route.
- [ ] Two-hour soak and copied-app clean-environment launch pass.
- [ ] Linux regression build/tests pass; Windows checks cover any shared changes.
- [ ] Co-op, compatibility and distribution limitations are accurately stated.
- [ ] Local offline alpha has evidence of a visible Metal scene, the playable
  route, character save/reload, and fresh-account bundle launch.
- [ ] Record M7 status separately for LAN and Internet. For a public binary also
  verify distribution terms and the downloaded, signed artifact's launch on a
  clean Mac. Local alpha sign-off can proceed while these claims are open.

**Deliverable:** tested alpha, release evidence and a prioritized remaining backlog.

## Validation matrix

| Area | Required evidence | Scope |
| --- | --- | --- |
| Build | Clean and incremental builds; architecture/library inventory | x86-64 runner on Apple silicon |
| Loader | Synthetic memory tests, `--dry`, CPU compatibility, TLS/callbacks | Supported eboot and matching libc |
| Faults | Concurrent texture writes, reuse/unmap, unrelated crash | Actual Darwin/Rosetta behavior |
| Game | First start, title, inn, town, adventure, return, reload | Disposable fresh and copied saves |
| Graphics | Synthetic draws, stable captures, Metal validation | 1080p first; 4K/Retina checks next |
| Audio/input | Music/voice/effects, keyboard, controller, hotplug | One wired/Bluetooth pad as available |
| Display | Resize, fullscreen, focus, display scale, sleep/wake | Record failures and supported behavior |
| Saves | Progress round trip, backups, unwritable destination | Real saves only as backed-up copies |
| Network (M7) | Packaged TLS and mock tests, cross-platform host/guest, distinct networks | Separate LAN and Internet claims |
| App | Moved bundle, clean user environment, reinstall | No dependency on development tree |
| Regression | Existing Linux tests and relevant Windows checks | Same pinned baseline and input fixtures |

Logs, captures and save fixtures remain in external build output directories;
publish only appropriate summaries, synthetic fixtures and permitted screenshots.
Never commit game dumps, production identities, personal paths or account secrets.

## Risks and decision rules

| Risk | Early signal | Response |
| --- | --- | --- |
| Rosetta CPU/code-memory incompatibility | M2 synthetic execution or invalidation fails | Minimal reproducer; decide a bounded workaround before continuing |
| Guest mapping or relay address constraints | Failed low/near reservations or truncation | Correct allocator and test collisions; recheck relocation assumptions |
| Fault-tracker deadlock or stale textures | Concurrent-write test hangs; partial updates missing | Fix handler/state design or prove a correct bounded fallback |
| Darwin timing mismatch | Headless scene/fence stalls | Check condition clocks, TLS and callbacks before blaming rendering |
| Metal shader/layout differences | Text, masks or alpha diverge | Synthetic draws, reflection checks and GPU capture |
| Source versus release feature mismatch | Binary-only Customize behavior expected | Keep public-source scope explicit; no unsupported parity claim |
| Upstream drift | New release changes hooks/protocol | Complete one pinned baseline, then rebase and rerun acceptance checks |
| Missing PC partner or older Mac | Co-op/support claim lacks evidence | Leave the corresponding validation open; narrow alpha claims |
| No explicit project license | Redistribution terms remain unclear | Continue scoped local work; resolve before downloadable binaries |

If M2 consumes more than four active days without its core probes passing,
record findings and re-estimate before graphics work. If M4 succeeds but M5 cannot
hold 60 fps, profile capture, detiling, uploads, synchronization and guest execution
separately. Consider a native GPU host only if measurements justify its IPC and
shared-memory cost; it is a separate architectural milestone, not a default fix.

## Tracking and evidence

Each milestone has a GitHub milestone and a tracking issue containing its work and
acceptance checklist. Keep these issues open until verification is complete.
Future implementation changes should reference the relevant issue and include
validation results. Evidence records use: commit, build command, OS/SDK/machine,
dump identifier/hash kept locally, save fixture, scenario, observed result and
remaining failures. Elapsed time alone never completes a milestone.

| Milestone | Tracking issue |
| --- | --- |
| [M0 Reproducible baseline](https://github.com/runty/OpenCrown-PC/milestone/1) | [#1](https://github.com/runty/OpenCrown-PC/issues/1) |
| [M1 Darwin build and host compatibility](https://github.com/runty/OpenCrown-PC/milestone/2) | [#2](https://github.com/runty/OpenCrown-PC/issues/2) |
| [M2 Executable memory and fault handling](https://github.com/runty/OpenCrown-PC/milestone/3) | [#3](https://github.com/runty/OpenCrown-PC/issues/3) |
| [M3 Headless game execution](https://github.com/runty/OpenCrown-PC/milestone/4) | [#4](https://github.com/runty/OpenCrown-PC/issues/4) |
| [M4 Metal frames](https://github.com/runty/OpenCrown-PC/milestone/5) | [#5](https://github.com/runty/OpenCrown-PC/issues/5) |
| [M5 Offline play, audio, input and saves](https://github.com/runty/OpenCrown-PC/milestone/6) | [#6](https://github.com/runty/OpenCrown-PC/issues/6) |
| [M6 Mac launcher and settings](https://github.com/runty/OpenCrown-PC/milestone/7) | [#7](https://github.com/runty/OpenCrown-PC/issues/7) |
| [M7 Networking and cross-platform co-op](https://github.com/runty/OpenCrown-PC/milestone/8) | [#8](https://github.com/runty/OpenCrown-PC/issues/8) |
| [M8 App bundle and console pipeline](https://github.com/runty/OpenCrown-PC/milestone/9) | [#9](https://github.com/runty/OpenCrown-PC/issues/9) |
| [M9 Alpha verification and release](https://github.com/runty/OpenCrown-PC/milestone/10) | [#10](https://github.com/runty/OpenCrown-PC/issues/10) |

## Phase 2 feasibility on iOS and iPadOS

The Mac runner executes x86-64 guest instructions and machine-code hooks through
Rosetta. Apple documents Rosetta for Mac apps. A physical iPhone or iPad needs an
arm64 application with its own way to execute the PS4 guest. The existing Switch
pipeline handles a different guest ISA. Its signing and deployment workflow may
help once an iOS app exists. Ordinary device signing supplies no general-purpose
facility for generating executable pages. Plan for a signed build that launches
without a debugger or special runtime-code entitlement. [Rosetta](https://developer.apple.com/documentation/apple-silicon/about-the-rosetta-translation-environment) · [Apple platform security](https://support.apple.com/en-ie/guide/security-pdf/sec15bfe098e/1/web/1)

During the Mac phase, keep guest addresses, CPU callbacks, TLS and runtime code
patches behind explicit execution interfaces as those sites are touched. Keep
capture/replay, shader sources and binding metadata, archive readers, portable
high-level OS services and deterministic save/input fixtures usable from other
hosts. Test with Mac fixtures before introducing a second implementation.

| Phase 2 step | Work and evidence needed to continue |
| --- | --- |
| P2.0 · Establish a CPU path | Evaluate build-time x86-64→arm64 translation on the Mac, linked and signed into a device build. An interpreter with guest instructions stored as data is a fallback to benchmark. Inventory indirect targets, callbacks, TLS, SIMD/flags and runtime patches for both eboot and libc. On a physical device run a synthetic guest that calls a host service, returns correctly and produces the reference results on two threads. |
| P2.1 · Execute a real scene | With the selected backend, reproduce the M3 route and compare guest events, results and timings to the Mac baseline. Measure CPU time and peak memory on device. Stop and re-estimate if guest coverage, address semantics or frame budget fails here. |
| P2.2 · Render and play on device | Rebuild shaders for the iPhoneOS SDK. All eight pinned shaders already compiled and linked into individual iPhoneOS 27.2 metallibs in an offline probe. Verify actual SDL Metal draws, bindings, pixel comparisons and texture-format support on each target. Check touch/controller input, audio interruption, frame pacing, memory pressure and sustained thermals on iPhone and iPad. |
| P2.3 · Package and recover | Use an in-process mobile entry point, sandboxed save paths and a document-picker/import flow for game data. Test suspend, resume, termination and offline save/reload. Verify signed installs and repeat launch without a debugger on both device classes. Assess public distribution separately if pursued. |

A phase 2 gameplay forecast depends on P2.0 and P2.1. Its effort is outside the
Mac estimate. The initial target is a locally provisioned device build using the
player's own inputs. Public distribution adds a separate rights and review
decision. [App Review Guidelines](https://developer.apple.com/app-store/review/guidelines/)

## Technical references

- [Pinned Linux build definition](https://github.com/Nezara/OpenCrown-PC/blob/88e7e0118614e22743c9691a55588f1045cab873/linux-port/CMakeLists.txt).
- [Pinned loader](https://github.com/Nezara/OpenCrown-PC/tree/88e7e0118614e22743c9691a55588f1045cab873/linux-port/src/loader).
- [Pinned render design](https://github.com/Nezara/OpenCrown-PC/blob/88e7e0118614e22743c9691a55588f1045cab873/linux-port/src/render/README.md).
- [Pinned dump validation](https://github.com/Nezara/OpenCrown-PC/blob/88e7e0118614e22743c9691a55588f1045cab873/linux-port/tools/common/dump_check.h)
  and [version validation](https://github.com/Nezara/OpenCrown-PC/blob/88e7e0118614e22743c9691a55588f1045cab873/linux-port/tools/common/game_version.h).
- [Apple Rosetta translation environment](https://developer.apple.com/documentation/apple-silicon/about-the-rosetta-translation-environment).
- [SDL GPU backend and shader overview](https://wiki.libsdl.org/SDL3/CategoryGPU).
- [SDL Metal shader binding requirements](https://wiki.libsdl.org/SDL3/SDL_CreateGPUShader).

The file inventory above comes from the pinned source. The architecture choices,
effort ranges, acceptance thresholds and alternatives are the proposed port plan;
they are not claims that macOS execution has already been validated.
