# Mobile input, audio and lifecycle adapters

These adapters supply host services for the translated game. They contain no game
assets, execute no translated code and require no microphone, Bluetooth or
background-audio entitlement. Deployment target is iOS 18; the iOS 27 SDK's current
interruption notifications are used on iOS 27, with the older API on iOS 18–26.

## Integration

Compile `core.cpp` as C++17 and `platform.mm` as Objective-C++17 with ARC and
blocks. Include `platform.h` from C, C++ or Objective-C++. Link Foundation, objc,
UIKit, GameController, AVFoundation and AudioToolbox. `build-check.sh` demonstrates
both device and Simulator compiler/linker flags without altering an Xcode project.

Call `dcp_platform_initialize(hooks)` on the main thread once UIApplication exists.
The hook runs on the main thread when effective active state changes. Use it to
pause/resume translated workers, stop frame submission and finish GPU work. The
adapter pauses audio and clears touch actions itself. The hook must not block
waiting for work that synchronously needs the main thread. Audio callbacks never
invoke the hook. Initial inactive state does not emit a redundant inactive hook;
initialize the host paused, then observe the first active notification.

Attach controls using `dcp_platform_attach_touch_overlay((__bridge void *)view)`
to the game content view. The UIView is borrowed; its overlay follows its bounds
and safe area. Pass null to detach. Attach launcher buttons to a separate view or
above this overlay, which consumes touches within the game view. Call
`dcp_platform_set_paused(1/0)` for a launcher/pause screen. An explicit resume also
clears the interruption latch when the system did not recommend auto-resumption.
Use `dcp_platform_shutdown()` on the main thread before destroying the host.
Stop and join guest service callers before shutdown/reinitialization.

From the guest input service, call `dcp_platform_input_read(slot, &pad)` and copy
fields into guest ScePadData. The struct is a host value type, not the guest ABI:
do not memcpy it into guest memory. Button masks match ScePadData. Axes have centre
128 and Y increases downward; triggers are 0–255. Touch coordinates are 1920×943
logical points (maximum 1919,942). Slot 0 combines the virtual pad and first
extended physical controller; three more physical slots are supported. Virtual
slot 0 remains connected with no physical controller. Controllers are enumerated
and connect/disconnect notifications rebuild state, so disconnect clears held
buttons. Remaining controllers compact into available slots. No special Bluetooth
permission or custom pairing flow is used.

Touch controls provide two sticks, D-pad, four face buttons, shoulders, triggers,
stick clicks, Menu and Map. Menu sends the ordinary Options button. Map sends a
touchpad click with a left-half contact, matching the game's Town Map gesture;
a touchpad click without a contact opens the character menu. The Map contact
is published first if another screen contact is held, and releases with the
button. Connecting a supported extended gamepad hides those controls and
releases held touches. The screen still accepts game touchpad
input, including on controllers without a touchpad. The separate Pause button
remains available. Disconnecting the last supported gamepad restores the
on-screen controls immediately. Multiple captured touches combine movement/actions.
The first unclaimed screen finger steers the game cursor toward its position
over the centred 16:9 picture, using the Mac adapter's relative-touch loop.
A short tap interacts once the cursor arrives; dragging moves it without a
click. A second unclaimed finger is ignored while that gesture is captured;
other fingers can still hold movement or action controls. The runtime supplies
read-only pad-zero cursor feedback with `dcp_platform_input_cursor`; sequence
zero means no sample. Map/raw touchpad input, pause, cancellation and controller
layout changes cancel a queued screen interaction. Physical controller touchpad
points remain native unless a screen pointer gesture is active.

Controller-information queries use `dcp_platform_input_connected`, which never
advances that loop. Only a valid pad-state read consumes a virtual finger or
interaction step. Using another consuming read for metadata can discard an
edge step and strand distant targets; the offline-service and cursor probes
cover this regression.
If presentation crops or uses another viewport, replace that transform or
supply your own raw pad via `dcp_platform_input_set_touch`. The custom setter
replaces the same virtual pad as the overlay; use one frontend at a time.
`build-check.sh` also runs the pointer/cursor probe: convergence at four game
sensitivities, tap/drag, combined input, Map priority, cancellation/pause/reset,
nonfinite/boundary rejection and coherent concurrent cursor snapshots.
The controls are a functional development overlay and still need visual/device UX
tuning, accessibility actions, and controller/touch preference UI.

Call `dcp_platform_audio_start()` to request audio; it returns success only when
the engine starts. A call while inactive records the request and starts on resume.
Submit already mixed float32 interleaved stereo at 48000 Hz using
`dcp_platform_audio_submit(samples, frames, timeout_ms)`. The sink serializes
producers, sanitizes nonfinite samples, clips to [-1,1], queues at most 256-frame
chunks, and blocks the producer near the queue target. It returns the accepted
frame count. Handle short writes; do not busy-spin when inactive/stopped. There
is no implicit mono/8-channel downmix in this API: do that in the game AudioSink.
`audio_stop()` cancels the request, aborts waiting submissions and flushes audio.
Start/stop are synchronous; do not call them inside a real-time render callback.

The AVAudioSourceNode render block only zero-fills and reads a bounded SPSC ring.
It takes no locks, allocates no memory and does no logging. The main mixer converts
48 kHz stereo to the route's format. The session requests 256/48000-second buffers,
matching the Mac fix for intermittent pops. A preferred duration is not a hardware
guarantee: the queue targets at least 1024 source frames and three observed render
buffers. It primes for a complete callback before first playback, and audio
indices are reset only after callbacks and producers are quiescent. A route change
or media-services reset rebuilds the engine and discards stale queued samples.
Audio honors the silent switch and mixes with other apps (Ambient category).

Inspect `dcp_platform_audio_stats` for actual device rate, duration, observed render
size, queued/submitted/rendered/flushed frames, clipping and underruns. Initial
priming silence is not an underrun. Persistent underruns indicate the guest mix is
late; repeated clipped samples indicate gain/downmix problems. Error strings are
available through `dcp_platform_copy_error`. Stats are cumulative for the process.
No physical audio quality claim follows from successful compilation.

## Offline validation

Run from the repository root:

```sh
ios/platform/build-check.sh /Volumes/office/Build/OpenCrown/platform-probe
```

It runs a native Mac core probe, then builds static adapters and links a small
executable for arm64 iPhoneOS and arm64 iPhoneSimulator with iOS 18 deployment.
The probe checks input merge/disconnect/pause, axis/touch bounds, NaN/clipping,
ring capacity/reset and 400000 frames across concurrent producer/consumer threads
with multiple ring wraps. Outputs stay on a mounted external build disk.

This validates C++ behavior and SDK compile/link compatibility. It does not launch
the UIKit host or verify device sound, touch layout, Bluetooth reconnection,
interruption/route recovery, battery use or frame pacing. Those require a later
interactive pass. GameController exposes no distinct finger-presence flag on the
legacy DualShock/DualSense touchpad axes: a finger exactly at centre (0,0) is treated
as absent. Rumble, motion sensors, keyboard/mouse and background audio are not
implemented. The host still owns scene-specific policy, guest memory validation,
AudioSink/PadState conversion, saving and Metal presentation.

The native core probe also passed AddressSanitizer and UndefinedBehaviorSanitizer
on the development Mac. Hardware-dependent behavior remains unverified.

For quiet device or Simulator verification, `DCP_MUTE_AUDIO=1` silences output
after consuming the real PCM ring. Callback, underrun and clipping counters
continue. App diagnostics report the source node's captured output mode as
`audio.output_muted`; it remains available after stopping for the final report.
This does not change system volume or qualify audible quality. The setting is
read when the audio graph starts or restarts. The older `DCP_SIM_MUTE_AUDIO=1`
alias and `audio.simulator_muted` field remain supported only in Simulator.
