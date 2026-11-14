# Live mobile diagnostics

The mobile app writes a fresh `Documents/diagnostics-live.json` on launcher load,
at session startup and lifecycle changes, and about once per second while its
CADisplayLink is running. The heartbeat happens before Replay/capture readiness
checks, so a game that has not produced its first frame still reports CPU and
service progress. iOS may suspend the app in the background; the foreground
heartbeat is not a background execution service.

Each report identifies the build, a new session UUID and start time, snapshot
time and frontend phase. It separates guest trace steps, libc checks, worker
starts, submissions/flips/vblanks, current ASCII scene, TITLE observation and the
headless checkpoint. `title_seen=true` means the checked TITLE component entered its active-update
phase. It does not establish visible pixels or playback of its requested cue.
`checkpoint_complete` is the headless TITLE-plus-120-vblanks/flips acceptance
checkpoint; an interactive session deliberately does not finish that checkpoint.
A false checkpoint flag in play mode does not by itself indicate a failed boot.

`captured_game_frames` counts actual frames consumed from capture.
`drawable_presents` counts successful drawable-present calls. A renderer can
present repeatedly without receiving new game frames, so these numbers must not
be used interchangeably. Both have separate bounded rings of 3,600 main-thread
arrival/present-call intervals, with sample count and p50/p95/p99 in milliseconds.
Pause gaps reset the timing anchors. These are frontend observations, not GPU
completion timestamps or a lossless history of every submitted frame.

Audio diagnostics report actual submitted/rendered/underrun/clipped/flushed
frames, queue depth/target, measured callback size, device rate/buffer duration
and running/paused state. Reports also include the last platform error, thermal
state, Metal capability queries and this process's `task_info` physical footprint
and resident bytes. A failed memory query is labeled unavailable with its Mach
result instead of invented values.

Snapshots and JSON encoding use main-thread frontend ownership. Immutable NSData
is written atomically on a dedicated serial queue. The guest runs independently;
the main thread never waits for game completion. Save errors crossing guest/UI
threads have their own short lock. Final reports still use `diagnostics.json`,
and Share Diagnostics exports both the fresh live file and the last final file
when present. The serial writer flushes the newly requested live snapshot before
the share sheet is presented. Initial launcher diagnostics therefore work before
any game session has completed, while the previous session report remains useful.

`ios/game/game.h` exposes `dcp_game_snapshot(DcpGameStats *)`. It always initializes
its output and returns zero when idle (or passed null). A live copy is protected
by engine lifetime ownership; scene/error strings use a dedicated diagnostics
lock, separate from allocation, capture and guest-memory locks. All returned
values are owned copies. Counters may advance between field reads. The C API does
not export a raw Engine pointer. UI-read guest scalar fields are atomic, including
64-bit trace steps.

`ios/game/stats-fixture.cpp` exercises the public API against the private
real-module archive: four concurrent readers sample four paused startups,
request stop, observe destruction and begin another session. Idle fields,
terminated strings, snapshots while parked and wake-on-stop are checked. Native
and ThreadSanitizer runs pass; the latter reports zero races. App and runtime
sources also compile with strict warnings for both iPhoneOS and Simulator.
Physical-device heartbeat, memory and TITLE readback now pass on iPad mini
A20 Pro; sustained gameplay and the iPhone remain to verify.

Session memory peaks are the maximum of heartbeat samples, explicitly labelled
as sampled peaks rather than OS high-water marks. Final live exports retain
the completed guest report and final launcher status, including errors, until
a new session starts.

The completed guest report separately counts component-dispatch entries and
read errors, plus optional direct audio-cue observer calls/string-read failures.
The old audio selector may be inlined and is no longer used for checkpoints.
The report explicitly leaves visual verification to rendered-frame inspection.

## Explicit automated startup check

Launching with `--diagnostic-startup` (Simulator or physical device) automatically
starts one bounded checkpoint run and reads the caller-staged
`Documents/diagnostic-input.txt`. The runtime still validates and hashes that
input script. Each run creates an independent
`Documents/DiagnosticSessions/<session UUID>/ProbeSaves` and checkpoint folder;
ordinary player saves are never selected by this mode. It exports first
nonuniform and last rendered game PNGs in that same session folder and requests stop after 120 wall-clock
seconds by default (`DCP_DIAGNOSTIC_SECONDS` accepts only finite 1–600-second
values, and invalid values reject before guest startup). The timer belongs to that session and cannot stop a later session.
Reports label the disposable save scope and diagnostic mode. Normal Home Screen
launch requires Play and retains its ordinary local saves. Missing inputs,
guest errors and incomplete checkpoints remain explicit failures. This option
exists for repeated device tests without manual launch taps; it does not claim
audio/controller quality or sustained play.

Automated checkpoint mode deliberately uses the paced null-audio adapter and
scripted pad events. Zero platform PCM counters in that mode are expected and
do not qualify audible playback. Normal interactive Play connects real platform
audio and input.

`current_scene` is the legacy field name for the last checked component marker,
not continuous tracking of every world scene. TITLE means active update; INN
means the normal component completed UI resource initialization and entered its
transition-to-idle helper. Completed reports export the observation stage and
checkpoint method. INN does not reuse the audio selector or constructor state0.
Its visible interior and reopened character still require independent checks.

`--diagnostic-seeded-startup` is the explicit seeded variant. Stage a quiescent
copy at `Documents/DiagnosticSaveSeed` first. The app takes and validates a
checkpoint, then restores it into the new session's private ProbeSaves before
starting the guest. It does not select normal ProbeSaves or modify the seed.
A missing/invalid seed rejects startup. For an INN investigation, supply the
existing runtime-validated `DCP_CHECKPOINT_SCENE=INN`, trace/vblank budgets and
a longer diagnostic duration through the launch environment. Merely restoring
a snapshot does not establish that the game reopened its character.

The seeded frontend passes five Simulator cases: nonfinite/over-budget
durations, missing seed, a one-second seeded stop and normal launch with an
invalid diagnostic variable. Three real disposable source files restore
byte-for-byte; seed/source and ordinary save/checkpoint sentinels remain
unchanged. The normal app still waits for Play. Both SDKs compile, and the
iPhoneOS package passes strict signature/profile checks.
