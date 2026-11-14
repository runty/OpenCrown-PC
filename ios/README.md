# iPhone and iPad port

Start with the [reproducible bootstrap](BOOTSTRAP.md) to build the dependency
stack, CPU/rendering checks, real-module libraries and mobile apps. The script
does not install or launch apps. The [game runtime](game/README.md),
[renderer](render/README.md), [platform](platform/README.md) and
[storage](storage/README.md) documents describe individual components.

The following records the original diagnostics probe and initial CPU harness;
current fixture counts and port status are in [progress.md](progress.md).

## Original device probe

P0 builds a small UIKit arm64 app, with a Metal device capability query and an
exportable diagnostics file. It reports OS build, device model, host page size,
available storage, build revision and persistent launch count. The game runtime
will be connected after the CPU backend proof in P1/P2.

Build using Xcode, XcodeGen and a configured development team. Outputs stay on a
mounted external disk. The deployment target is provisionally iOS 18.0, with
physical verification currently limited to the reference devices' 27.2.1 builds.

```sh
./ios/build-probe.sh /Volumes/office/Build/OpenCrown/ios-probe TEAM_ID
xcrun devicectl device install app --device DEVICE_ID /Volumes/office/Build/OpenCrown/ios-probe/DerivedData/Build/Products/Release-iphoneos/OpenCrownProbe.app
xcrun devicectl device process launch --device DEVICE_ID com.wadscorp.opencrown.probe
xcrun devicectl device copy from --device DEVICE_ID --domain-type appDataContainer --domain-identifier com.wadscorp.opencrown.probe --source Documents/diagnostics.json --destination /path/to/diagnostics.json
```

The app is named OpenCrown Probe and has its own bundle ID. Its navigation-bar
share button exports the same JSON, and Files exposes its Documents folder.
A launch without LLDB checks standalone execution. Manual Home Screen launch
and share-sheet delivery are additional checks. The launch counter should
increase after terminating/relaunching and survive a repeat install.

See the [implementation plan](../docs/ios-ipados-port-plan.md) and
[progress record](progress.md).

## Translated CPU fixtures

The optional harness lifts eight synthetic x86-64 routines through the pinned
Remill build and compiles their complete CPU-state entry points into a static
arm64 library. Seven routines use native Rosetta x86-64 results as their oracle.
Guest FS/TLS uses a distinct seeded guest-memory region on each worker.

```sh
python3 ios/cpu/build.py --remill /Volumes/office/Build/OpenCrown/cpu-backends --llvm /opt/homebrew/opt/llvm --output /Volumes/office/Build/OpenCrown/cpu-harness
./ios/build-probe.sh /Volumes/office/Build/OpenCrown/ios-probe TEAM_ID /Volumes/office/Build/OpenCrown/cpu-harness
```

Build the dependency as recorded in progress.md first. The harness builder runs
the native x86-64 oracle, builds and runs an arm64 Mac executable, and builds the
iPhoneOS static library. The device app executes that library on launch and
includes its results and corpus hash in diagnostics.json. Link errors expose
unimplemented intrinsic bridges instead of silently substituting behavior.

Coverage includes arithmetic and selected flags, conditional branches, rotation,
unaligned load/store, direct/indirect translated calls, AVX bitwise operations,
guest FS/TLS on two workers, checked memory boundaries and unresolved targets.
The memory bridges record errors without unwinding through Remill's nounwind
intrinsics. These fixtures precede the real-module and broader P1 CPU tests.


## Isolated interactive diagnostics

`--diagnostic-interactive-startup` starts timed normal game execution with real
platform audio/input, but keeps saves/checkpoints and captured PNGs in a fresh
`Documents/DiagnosticSessions/<UUID>` folder. It supplies no automatic controller
input. `DCP_DIAGNOSTIC_SECONDS` must be finite and in1..600 (default120). Unlike
`--diagnostic-startup`, it does not use the scripted checkpoint driver or null
audio. Normal Home Screen launch still waits for Play and uses local player saves.

Any diagnostic startup may set `DCP_DIAGNOSTIC_RESUME_UUID` to an existing test
UUID to reopen that same working save/checkpoint folder without restoring a seed.
Invalid/missing sessions and combining resume with seeded startup reject before
game execution. New PNGs use the new run's UUID folder so previous evidence is
retained. Normal launch ignores this test variable. Reports distinguish run ID,
save-session ID and resumed/seeded/interactive modes. A retained file or a frontend
resume test does not itself prove an actual character was reopened.

`main_thread_work` reports bounded rolling milliseconds for replay pump,
drawable presentation and diagnostic heartbeats. Pump samples include empty
queue polls. These durations complement frame-arrival/present intervals; neither
software counter alone proves a screen was rendered correctly.
