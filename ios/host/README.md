# Native AOT game rendering runner

This macOS arm64 command line runner executes the same translated module archive
as the mobile app. SDL/Metal and `Replay` run on the main thread while the game
runs in `std::async`. It does not use Rosetta to execute the game and does not
install or launch anything on physical devices or in Simulator.

Build against a **frozen** game archive and a compatible renderer cache. Do not
link an archive while trace discovery is replacing it. CMake 4.3+ consumes SDL's
exported static target, including its actual framework dependencies.

```sh
./ios/host/build.sh \
  /Volumes/office/Build/OpenCrown/ios-host \
  /Volumes/office/Build/OpenCrown/ios-game-app \
  /Volumes/office/Build/OpenCrown/ios-render/mac

/Volumes/office/Build/OpenCrown/ios-host/opencrown-aot-host \
  --game-root /Volumes/phil/Games/PS4/DragonsCrownPro/CUSA10454 \
  --save-root /Volumes/office/Build/OpenCrown/ios-host/disposable-saves \
  --output /Volumes/office/Build/OpenCrown/ios-host/run \
  --seconds 120 --offscreen
```

All build/cache/runtime paths must be on mounted external volumes. Save and
output directories must be separate from the dump. The supplied save directory
can be modified by the game; use a disposable directory for boot tests. The
game archive must match the dump. The runner never copies assets into source.

Omit `--offscreen` for a resizable SDL window. Closing it requests a cooperative
game stop. SIGINT and SIGTERM do the same. The runner forces checkpoint mode,
which enables the existing automatic Options/Cross input script and stops after
the runtime's TITLE checkpoint. This runner provides no new live controller or
Mac audio support.

`--seconds` defaults to 120 and requests cancellation when its wall-clock budget
expires. Main-thread pumping and unload servicing continue while cancellation
finishes. If the guest remains stuck for another 15 seconds, the runner writes
a failed report and exits the process without freeing memory underneath it.
It does not wait indefinitely in a `std::async` destructor.

Rendering starts only after `capture_enabled()` publishes capture readiness.
When the guest invokes `will_unload`, it waits for the main thread to consume
the queued frames, drain GPU work, destroy Replay and acknowledge completion.
Only then may the guest release capture and backing memory. The main loop never
waits synchronously for the guest while an unload request needs servicing.

The first readable frame is saved as `frame-000001.png`. `--every 60` also saves
every sixtieth frame; `--every 0` disables periodic images. `report.json` contains
the complete guest report, consumed/rendered frame counts, last display size,
draw/flip information, BC fallback count, elapsed time, cancellation/timeout
status and unload acknowledgement. The same JSON is printed on stdout.

Exit 0 requires `guest.booted=true`, at least one readable rendered frame, no
guest/render error and no timeout. Exit 1 reports missing coverage, an incomplete
checkpoint or a rendering/runtime failure. Exit 2 indicates invalid arguments,
setup/report-writing failure or an expired cancellation grace period. A game
that executed instructions but produced no frames is explicitly a failed
rendering verification.

## Verification performed

The frozen 545-trace archive in `ios-game-app` executed real libc/game code and
returned a missing-trace diagnostic for `205ba8f0`. It reported libc start 0,
64 libc checks, 17 guest workers and 15,032 trace entries, with zero flips/frames.
The runner returned exit 1 in 7.14 seconds, acknowledged unload, and recorded
no rendering error or timeout. This establishes cleanup and diagnostic handling;
it does **not** establish visible real-game frames.

A separate 1 ms budget test requested stop and acknowledged unload in 48 ms,
returning exit 1 with `timeout=true`, without reaching the grace deadline.
Evidence is under `/Volumes/office/Build/OpenCrown/ios-host/run-frozen` and
`run-timeout` on the development machine.

An optional, separate synthetic executable tests the harder ordering case:
the producer queues a 64×64 frame and immediately requests unload before the
main thread necessarily initializes Replay. The main thread must initialize,
render/read back the frame and acknowledge before the producer unmaps it.

```sh
cmake -S ios/host -B /Volumes/office/Build/OpenCrown/ios-host \
  -DDCP_BUILD_HOST_FIXTURE=ON
cmake --build /Volumes/office/Build/OpenCrown/ios-host \
  --target opencrown-aot-host-fixture
# Run the fixture executable with the same required paths shown above.
```

The synthetic test passed with one draw/frame, 64×64 output and acknowledged
unload in 49 ms. PNG SHA-256 was
`62b403fecaf4bc0574ae469023bd57260d0d59798db1d004fd5b112aae8aa014`, matching
the existing Replay fixture. Its guest JSON includes `fixture=true`; it is never
linked into `opencrown-aot-host` and is not game-boot evidence.

The current cached SDL archive was built with a macOS 27.2 deployment version,
so linking it into this runner's macOS 15 target emits deployment warnings.
These measurements were on macOS 27.2. Rebuild SDL/renderer with the intended
deployment target before claiming macOS 15 compatibility. This runner does not
silently rebuild or modify caller-supplied caches. Third party licenses remain
in the supplied game/renderer `ThirdPartyNotices` directories.
