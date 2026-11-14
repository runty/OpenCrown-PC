# Real-module arm64 bootstrap

This experimental headless runtime lifts the supported CUSA10454 patch 1.03
modules into signed arm64 code. It reuses the existing SELF/ELF parser and NID
scheme, relocates private module images into checked identity-backed memory,
and dispatches translated calls and native HLE services. The bootstrap exposes
its actual checkpoint and any missing trace or service in the probe's JSON.

Build outputs, unwrapped modules, images, IR, objects and game assets belong on
the external build disk. The builder checks the two supported module hashes.
The generated module cache checks its inputs and output hashes on reuse.

## Dependencies and build

Use the Remill revision and dependency build from ../progress.md. Apply these
patches from this fork to that pinned checkout before rebuilding the lifter and
amd64_avx semantics runtime:

- ../remill-llvm23.patch
- ../remill-lifter-lifetime.patch
- ../remill-packed-simd.patch
- ../remill-pcmpistrm.patch

The lifetime patch fixes a deleted LLVM name reference. The semantics patches
add packed shuffle/blend/saturation and string mask operations, with an empty
range fix in the shared string comparison implementation. The synthetic CPU
suite compares their tested cases against native x86-64 execution under Rosetta.

FreeType uses the same cached 2.13.3 source as the Mac build:

```sh
ios/game/font-build.sh /path/to/freetype-src /Volumes/YOUR_DISK/Build/OpenCrown/font-probe
```

The current builder expects sibling cpu-backends, font-probe and macos-rendered
cache directories under the OpenCrown build root, including the Mac build's
cached FreeType source. Run the discovery harness on the arm64 Mac:

```sh
python3 ios/game/advance.py --root /Volumes/YOUR_DISK/Build/OpenCrown/ios-game --dump /path/to/CUSA10454 --rounds 512
```

It runs actual module code and adds only executed missing traces. It stops on
an instruction/service gap or a process failure. Every attempt gets disposable
save storage under attempt-saves; its outputs remain available for diagnosis.
Rerun after implementing the reported gap to reuse cached traces and objects.
Missing call targets are explicit failures. Known offline stubs are restricted
to API names observed on the reference Mac route.

Build the device library from an established trace corpus:

```sh
python3 ios/game/build.py --root /Volumes/YOUR_DISK/Build/OpenCrown/ios-game --dump /path/to/CUSA10454 --ios
./ios/build-probe.sh /Volumes/YOUR_DISK/Build/OpenCrown/ios-probe TEAM_ID /Volumes/YOUR_DISK/Build/OpenCrown/ios-game
```

Copy the user's dump into the probe's Documents/Game directory through devicectl.
The app sets DCP_GAME_ROOT to that folder and DCP_SAVE_ROOT to Documents/ProbeSaves.
On Mac the builder's selected dump and external test-save folder are defaults.
The UI starts the runtime on a background queue and displays/exports the JSON.
Inspect stage, error, libc_start_result, libc_strlen_checks, guest_workers,
submits, flips, label_writes and vblanks. An entered game function is an early
startup checkpoint. The booted flag requires a TITLE observation followed by
120 more vblanks and flips. Device validation uses the same generated trace hash.

## Host services and lifetime

The runtime includes checked scalar memory, memory barriers and compare/exchange,
thread-local guest TLS and errno, module imports, sparse aligned allocation,
native guest workers, conditions/events, file I/O, savedata, headless pad/audio,
FreeType fonts and CPU-side GNM label completion. Headless audio uses a paced
null sink. Live input, audible output and visible Metal frames remain later
milestones.

Workers share registered memory and immutable trace maps. Callback execution
uses a separate guest CPU/stack while retaining that thread's guest TLS.
The runtime propagates the first failure, interrupts memory polling and service
waits, releases tracked mutex ownership and joins workers before freeing images,
font data or file handles. If workers fail to join within the timeout, their
context stays alive and the report records that failure.

The isolated adapter probes cover file errors and files larger than 4 GiB,
savedata mount/write gating, thread cancellation and synchronization, GNM packet
labels and font metrics/rasterization. Full game acceptance still depends on
real runtime and physical-device results in ../progress.md.
