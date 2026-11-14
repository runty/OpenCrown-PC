# Reproducing the iPhone/iPad build

`bootstrap.sh` builds dependencies, CPU and Metal fixtures, translated game
module libraries, an unsigned simulator app, and a development signed universal
iPhone/iPad app. It never installs or launches either app. Building an app does
not establish that the original game reaches a playable screen.

Use an Apple Silicon Mac with Python 3.12+, CMake, Ninja, Git, curl, SPIRV-Cross,
XcodeGen, Homebrew LLVM **23.1.5**, and an Xcode installation containing the
macOS, iPhoneOS and simulator SDKs and Metal compiler. These tools must already
be installed; the script does not install software or change Xcode selection.
The development team must already be available to Xcode. App signing uses
Xcode's automatic provisioning and may contact Apple.

Choose a mounted external build workspace separate from your decrypted dump.
The Office disk is the normal build location. Inspect available space first;
dependency and shader caches are retained and reused on every run.

```sh
./ios/bootstrap.sh \
  --root /Volumes/office/Build/OpenCrownMobile \
  --dump /Volumes/phil/Games/PS4/DragonsCrownPro/CUSA10454 \
  --team YOUR_TEAM_ID --rounds 128
```

Only the supported CUSA10454 executable pair is accepted. The builder verifies
SHA-256 of `eboot.bin` and `sce_module/libc.prx` before generating code. These
private module images and translated IR remain in the external workspace.
Original game archives are not included in the app or copied into this source
repository; the app's Files import supplies them separately.

Use `--check` to validate tools, SDKs, mounted paths and executable hashes,
and print the resolved configuration without creating a workspace. Use
`--prepare-only` to build dependencies, host fixtures and seed module libraries
without trace discovery, app generation or signing. A team is unnecessary in
these modes. `--prepare-only` still compiles code; `--check` is the quick check.

The configurable discovery budget counts host execution/build rounds. Each
missing trace is lifted together with up to `--prefetch 12` nearby uncompiled
direct callees or possible vtable/callback targets from executable module
segments. Relocation records are bounds checked and speculative destinations
require 16-byte alignment; executable segments also contain constants, so these
are candidates rather than proven functions. Executed unaligned demands are
always retained. Arbitrary code bytes are not scanned for guessed entries. Unsupported services,
instructions and runtime failures stop the build explicitly. Exhausting the
round budget is reported as **partial coverage**, then the current libraries
and apps are built. A later invocation reuses those traces and continues.
`--rounds 0` packages current coverage without discovery.

## Dependency pins and patches

| Dependency | Pin | Archive SHA-256 |
| --- | --- | --- |
| Remill | `66c488c9911fd9b368e4d0c9647d74239f8bba8b` | Git commit; clone from lifting-bits/remill |
| SDL | 3.4.18 | `9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3` |
| FreeType | 2.13.3 | `bc5c898e4756d373e0d991bab053036c5eb2aa7c0d5c67e8662ddc6da40c4103` |
| LLVM | 23.1.5 | Existing installation, validated with `llvm-config` |
| Berkeley SoftFloat | 3e | `21130ce885d35c1fe73fc1e1bf2244178167e05c6747cad5f450cc991714c746` |

SDL and FreeType archive URLs/hashes match `linux-port/CMakeLists.txt`.
Downloads come from the projects' GitHub release/tag endpoints and are verified
before extraction. Remill's pinned CMake files select XED, mbuild, glog, gflags,
GoogleTest, Sleigh and Ghidra; the completed manifest records the exact checked
out transitive revisions. Xcode SDK versions and paths are recorded rather than
downloaded or silently changed. Builds with another SDK are separate validation
configurations, not byte-identical artifacts.

The script applies these checked local Remill patches, in order:

1. `remill-llvm23.patch`
2. `remill-lifter-lifetime.patch`
3. `remill-packed-simd.patch`
4. `remill-pcmpistrm.patch`
5. `remill-sse41-lanes.patch`
6. `remill-avx-extract.patch`
7. `remill-packed-shifts.patch`
8. `remill-x87-control.patch`
9. `remill-movemask.patch`
10. `remill-startup-variable-blend.patch`
11. `remill-startup-lane-insert.patch`
12. `remill-startup-permute128.patch`
13. `remill-startup-dword-shifts.patch`

The FP80 stage downloads the checked official SoftFloat archive, builds helper
libraries for all three targets and the host LLVM lowerer, and runs direct,
lowered-IR and guest control-word checks. Extended arithmetic retains 80-bit
values through i80 storage; unsupported operations stop generation. See
[FP80 support](cpu/fp80/README.md) for coverage and exception-model limits.

Remill patches are replayed in order against pinned pristine source in a
scratch tree. Each cached destination must match pristine source, a known
intermediate or the final result before any file changes. This handles later
patches modifying earlier context while rejecting unrelated edits. No checkout
is reset. SDL's separate `render/sdl-simulator-metal.patch` uses clean apply or
reverse checks. Dependency, trace and shader caches are retained.

The SDL change enables the simulator's measured Common1 GPU family to exercise
our basic Metal drawing subset. It does not establish full SDL GPU feature
support on that simulated device. The physical iOS hardware gate remains
unchanged. See [renderer verification](render/README.md) for format fallbacks
and the distinction between fixture output and actual game frames.

## Reusing existing external caches

Source overrides reuse caller-supplied release trees. Their release versions
and patches are checked, but they are not claimed to be pristine archive
extractions. Omit the source overrides for a fresh SHA-verified download.

```sh
./ios/bootstrap.sh \
  --root /Volumes/office/Build/OpenCrownMobile \
  --dump /Volumes/phil/Games/PS4/DragonsCrownPro/CUSA10454 \
  --team YOUR_TEAM_ID --rounds 128 \
  --backend-root /Volumes/office/Build/OpenCrown/cpu-backends \
  --sdl-source /Volumes/office/Build/OpenCrown/macos-rendered/_deps/sdl3-src \
  --font-source /Volumes/office/Build/OpenCrown/macos-rendered/_deps/freetype-src \
  --font-output /Volumes/office/Build/OpenCrown/font-probe
```

Run one build per workspace/backend at a time. The bootstrap holds workspace
and backend locks and rejects an existing trace discovery/build process using the same
workspace or backend. Direct manual CMake jobs are the caller's responsibility.
To stop discovery, create `ROOT/ios-game/advance.stop`; the current child finishes
before exit. Remove that marker to resume. Avoid terminating an active compiler.
To stop the entire bootstrap between stages, create `ROOT/bootstrap.stop`, or
send SIGINT/SIGTERM to its Python parent. Its current child completes first;
remove the marker before resuming.

The game builder also accepts `--backend-root`, `--font-source`,
`--font-output`, `--fp80-output` and `--llvm-prefix` directly. Equivalent environment variables
`DCP_BACKEND_ROOT`, `DCP_FONT_SOURCE`, `DCP_FONT_OUTPUT`, `DCP_FP80_OUTPUT`,
`DCP_LLVM_PREFIX` pass
the same settings into `advance.py` child builds. `DCP_SDL_SOURCE` is passed to
the app builder. The default compatibility layout remains supported.

## Outputs and limits

Every stage prints its complete log path under `ROOT/bootstrap-logs`.
`bootstrap-manifest.json` records pins, patch hashes, SDKs, source revision,
dirty state, dependency revisions, and lifter/runtime-bitcode hashes after the
module-library stage. `ios-game/trace-provenance.json` distinguishes current,
stale and unknown cached IR; stale/unknown traces are relifted before packaging.
Individual provenance records contain input-image/slice hashes, raw/final IR
hashes and the FP80 lowerer/helper manifest hashes. No old trace is retrospectively assigned a new semantics version.

Host CPU fixtures compare translated routines with a Rosetta x86 oracle.
Host renderer fixtures verify saved image pixels, BC fallbacks and capture
consumption. Platform/storage checks compile the device and simulator adapters.
The script cross compiles arm64 libraries for macOS 15+, iOS 18+ and its
simulator. It packages third party notices with the apps.

App outputs are:

- `mobile-app-ios/DerivedData/Build/Products/Release-iphoneos/OpenCrown.app`
- `mobile-app-simulator/DerivedData/Build/Products/Release-iphonesimulator/OpenCrown.app`

Signing verifies the device bundle locally. Installation, physical controller
and audio checks, on-device gameplay, background/foreground handling and save
round trips require separate verification. The fixed bundle ID remains
`com.wadscorp.opencrown.mobile`; the app builder currently owns its product
configuration. CPU translation coverage remains incremental and specific to
the validated executable hashes.
