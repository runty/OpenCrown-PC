# Unattended scene checkpoints and input

Headless Mac runs default to the real TITLE scene followed by at least 120
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
