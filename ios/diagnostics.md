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
headless checkpoint. `title_seen=true` means the real scene observer saw TITLE.
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
Physical-device heartbeat and sustained gameplay measurements remain to verify.

Session memory peaks are the maximum of heartbeat samples, explicitly labelled
as sampled peaks rather than OS high-water marks. Final live exports retain
the completed guest report and final launcher status, including errors, until
a new session starts.
