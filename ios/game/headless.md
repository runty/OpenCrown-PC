# Unattended scene checkpoints and input

Headless Mac runs default to the verified TITLE component active-update phase
followed by at least 120
additional flips and vblanks. `DCP_CHECKPOINT_SCENE=INN` chooses INN instead;
only observing that exact scene and both advancing counters can complete it.
`title_seen` remains an independent report field. Merely configuring INN does
not establish that it was reached. Interactive mobile play has no automatic
checkpoint stop and ignores these harness environment overrides.

| Variable | Default | Valid override |
| --- | --- | --- |
| `DCP_CHECKPOINT_SCENE` | `TITLE` | 1–15 uppercase letters, digits or underscores |
| `DCP_MAX_TRACE_ENTRIES` | 100,000,000 | 1–10,000,000,000 |
| `DCP_MAX_VBLANKS` | 7,200 | 1–216,000 |
| `DCP_INPUT_SCRIPT` | Built-in boot input | Path to a text file, maximum 1 MiB |

The input file uses exactly the desktop runner's parser and button/left-stick
mapping. A line is `frame buttons`; button names combine with `+` or `|`, and
`#` starts a comment. Each state persists until the next line. An empty or
comment-only file means explicit idle, without built-in button presses.
Malformed scripts fail before game execution, naming the line. A real input
producer takes precedence over scripted input. Script-only stick bits never
leak into the guest's button mask.

The existing [Mac smoke script](../../macos/headless-input.txt) is available
unchanged. Use a disposable save copy, with the game stopped while copying.
The supplied `DCP_SAVE_ROOT` is writable; do not point tests at live progress.
For example, after preparing a disposable Felix save and a frozen host build:

```sh
DCP_CHECKPOINT_SCENE=INN DCP_MAX_VBLANKS=6000 \
DCP_INPUT_SCRIPT="$PWD/macos/headless-input.txt" \
/Volumes/office/Build/OpenCrown/ios-host/opencrown-aot-host \
  --game-root /Volumes/phil/Games/PS4/DragonsCrownPro/CUSA10454 \
  --save-root /Volumes/office/Build/OpenCrown/ios-inn-test/saves \
  --output /Volumes/office/Build/OpenCrown/ios-inn-test/run \
  --seconds 120 --offscreen
```

The report includes requested scene, observed checkpoint flag and SHA-256 of the
exact input file bytes. The host additionally requires a drawn, nonuniform RGB
readback and no guest/render error. Inspect the image for a recognizable scene.
Increasing a budget or selecting a scene is not an acceptance override.

```sh
./ios/game/test-headless.sh /Volumes/office/Build/OpenCrown/headless-probe \
  /Volumes/office/Build/OpenCrown/ios-game
```

Verification passes 24 configuration checks, 80 pad/script ABI checks in native
and ASan/UBSan runs, and all three SDK compilations. Six actual-runtime negative
and bounded-run cases pass 24 checks: INN remains incomplete under a one-entry
trace budget, invalid scenes/scripts and oversize scripts stop before execution,
and valid/empty script hashes match independently calculated SHA-256 values.
The real-runtime lifecycle fixture still passes 27 sessions (maximum stop 10.2 ms
in this run). Storage fixtures still pass 137 core and 108 service checks across
four simulated relaunches. These tests do not establish real INN boot or save
reopening; those require continued executed-target coverage and actual images.

For repeated discovery into INN, `advance.py --save-seed PATH` copies a
quiescent disposable save into each fresh attempt. The seed must be on mounted
external storage, outside the attempt-save tree. Copies are bounded to 4,096
entries / 256 MiB and reject symlinks, special files, overlapping paths and
nonempty destinations. Each file is copied byte-faithfully and verified with
SHA-256; an adjacent `.seed.json` manifest records that attempt's inputs. Source
files are never written. Keep the source quiescent throughout copying; detected
changes reject the unused attempt. Failed copies are retained for inspection and
never launched. This is a test-seeding facility, not cloud synchronization.

`save-seed-test.py` passes 28 filesystem checks, including binary/empty files,
empty directories, exact independent hashes, source preservation, size/count
limits, overlap and symlink/FIFO rejection. A separate disposable copy of the
previously verified Felix snapshot also matches all three input files. No live
Mac or iCloud save was accessed by those copy tests.

Vblank waits use the pause-excluded monotonic clock and absolute 60 Hz
deadlines, matching the desktop timer's four-period long-stall reset. Work
between waits consumes that frame's budget instead of adding another full
sleep. Waiting polls pause/stop/per-worker cancellation at most every millisecond.
The deadline fixture verifies 1,006 cases and 60 actual waits with 5 ms of work
between them (about 1.002 s total); this is timer validation, not a game FPS claim.
The updated real runtime passes 27 lifecycle sessions (maximum stop 37.8 ms)
and all six bounded headless failure/configuration cases.

The default vblank budget is 7,200: the earlier 1,800-vblank limit could stop
fresh startup before the original Mac reference's TITLE transition. Explicit
small-budget negative tests remain unchanged. Longer startup still needs a
matching outer process timeout; a larger budget does not satisfy a checkpoint.

Scene names use the pinned function’s second SysV argument (RSI); RDI carries
the audio-selector object. Native Mac probe logs for the verified Felix reload
show TITLE, INN and TOWN in RSI. The AOT observer previously read RDI; that
diagnostic bug is corrected, and the report identifies
`scene_observer_name_argument="rsi"`. Earlier negative observer flags do not prove
a scene was absent, and no accepted TITLE/INN result was claimed from them.

Completion now preserves genuine worker failures that arrive after the checkpoint
flag is set, and reports checkpoint success only after error-free teardown. The
lifecycle fixture waits for observed translated progress during each resume,
rather than relying on a one-millisecond initialization window. All 27 sessions
pass, alongside six bounded headless cases and 82 service/input checks in both
native and ASan/UBSan runs. Physical device lifecycle remains separate.

## Component checkpoint versus requested audio

Original-byte, relocation and phase-dispatch review establishes that the older
`68840` hook selects a named audio cue. Its numeric wrapper can be inlined into
an AOT trace, bypassing that function-entry hook. Audio-cue requests therefore
no longer satisfy checkpoints. Empty old scene reports also do not prove scene
absence. Cue reports are explicitly diagnostic and may miss inlined requests.

The current TITLE observer checks entry to its phase dispatcher, both original
relocated object vtables, the delivered update phase and the object's stored
phase. Initialization loads the locale-specific STAGE/TITLE resource pair.
`current_scene` and `title_seen` now identify that component's active update;
they do not assert screen-visible pixels or successful audio playback.
The completed report labels `checkpoint_observer=component_active_update` and
`visual_checkpoint_verified=false`. Independent rendered-frame inspection is
required for visible TITLE acceptance. The INN observer is separately bound to the Inn interior component, whose
BRB subtree selects the character-selection background and an exact SysObj
class. It checks primary/control vtables, class-tag pointer, normal mode and
stored state0x81 at its transition-to-idle helper. That state follows completed
localized UI resource loading. Constructor state0 is rejected; no prior object
address is remembered, and stale RSI is deliberately ignored. Its report kind
is `component_ready_transition`, followed by the same advancing-frame gate.
Visible INN and actual character-save reopening remain separate acceptance work.
Checkpoint anchors are initialized before publishing the observation flag.

TOWN has a separate checked component-update marker: the original Town class
vtables/tag and delivered/stored state0xa4 must match at the secondary callback.
Constructor0, initial states and the Inn portal cannot satisfy it. This is
post-initialization component activity; rendered Town and gameplay remain
independent checks.

Once the requested component is observed, the checkpoint driver releases all
scripted buttons and sticks while retaining the connected pad. The game keeps
updating/rendering for its required 120 advancing vblanks/flips. This prevents
later script events from leaving the screen being verified. Interactive input
producers retain priority. Reports identify `input_policy=neutral_after_checkpoint`
for checkpoint runs; the source input file hash is still retained.
