# macOS launcher and local app bundle

The development app has a Metal launcher and sibling x86-64 game runner. Both
run under Rosetta 2 on Apple silicon. Game data stays in the player's external
PS4 dump; the app contains no game files. Tested on M5 Max, macOS 27.2.1 and
Xcode 27.2 with SDL 3.4.18. The current binaries target macOS 27.2; earlier
systems and Intel Macs have not been validated.

## Build and package

Use an external build directory, as in [the build instructions](README.md):

```sh
./macos/build.sh --build-dir /path/to/build --mode rendered
ROSETTA_ADVERTISE_AVX=1 ctest --test-dir /path/to/build --timeout 60 --output-on-failure
./macos/package.sh --build-dir /path/to/build --output /path/to/output/OpenCrown.app
```

The packager verifies x86-64 architecture and system-only dynamic dependencies,
strips debug symbols, embeds the original project crown icon, shader-bearing
runtime binaries, build metadata and third-party notices, then signs the nested
executables and bundle ad-hoc and verifies the signatures. It needs Python 3
and Apple build tools at **package time**. The resulting app needs no Python,
Homebrew, Xcode, source tree or shell environment at runtime. Rosetta and the
macOS frameworks remain system requirements.

`--replace` keeps an existing output as a timestamped backup before replacing it;
player state is never copied into the bundle. `Contents/Resources/build-info.json`
records source revision/dirty state, deployment target, binary hashes and linked
libraries. Game shaders and ImGui's Metal shaders are included in the runtime.
The bundle's minimum OS follows the actual Mach-O deployment targets.

Double-click the app, browse to the supported decrypted CUSA10454 patch 1.03
dump, choose the save folder and press **PLAY**. **Stop game** returns control
to the launcher; return to town to save before stopping. The launcher remains
available for another run. Its native folder picker and typed paths both work.
The current Mac launcher selects Metal; Linux retains its existing renderer
choices and OpenGL frontend.

## State and logs

| Data | Default on macOS |
| --- | --- |
| Settings and controls | `~/Library/Application Support/OpenCrown/{launcher.ini,input.ini}` |
| Online identity | `~/Library/Application Support/OpenCrown/identity.ini` |
| Saves | `~/Library/Application Support/OpenCrown/saves/SAVEDATA00` or the chosen folder |
| Launcher log | `~/Library/Logs/OpenCrown/launcher.log` |
| Game log | `~/Library/Logs/OpenCrown/last_run.log` |

`DCP_USER_DIR` overrides state and puts test logs in `<override>/logs`.
The launcher passes the selected save root as `DCP_SAVE_ROOT`; saves stay in
the PS4 format. Replacing or moving the app preserves these external files.
On Apple the shared default-directory helper ignores Linux's XDG defaults.
The launcher discovers its real executable location, preserving paths with
spaces/Unicode, and sets `ROSETTA_ADVERTISE_AVX=1` before starting the runner
unless explicitly overridden. The runner logs its effective CPU features.

For iCloud Drive, choose a dedicated folder such as
`~/Library/Mobile Documents/com~apple~CloudDocs/OpenCrown/saves` on the Save
Folder page. Existing progress should be copied there with a backup before
launching. The game reads/writes that folder directly; no nxnative save packing
or separate sync engine is involved. Remote sync completion and multi-Mac
conflict handling were not tested in this pass.

The Mac Update page links to the fork's releases; automatic replacement is
disabled. Startup/console update checks cannot fetch/install an upstream Linux
AppImage or Windows binary. Bug-report preview/redaction remains available and
identifies macOS correctly. No report was submitted during verification.

## Verification

All fourteen rendered CTest suites pass, including the Metal launcher selftest
and platform executable-path/spawn working-directory regressions. After the last
UI/logging adjustments the affected launcher/platform tests pass again. The
launcher selftest can save a real rendered PNG with
`DCP_LAUNCHER_SELFTEST_SHOT=/path/to/launcher.png`; Metal readback renders into
an offscreen texture rather than trying to download a framebuffer-only drawable.

The game-dependent bundle check uses only disposable state and a copied save:

```sh
python3 macos/test-package.py --app /path/to/OpenCrown.app --dump /path/to/CUSA10454 --save-copy /path/to/test-save/SAVEDATA00 --deny-build-dir /path/to/build
```

It retains logs, screenshots and `verification.json` beside the supplied app.
It copies the app into a path with spaces/Unicode, runs from `/tmp` with only
`/usr/bin:/bin` on PATH and makes the build directory inaccessible temporarily
(restoring its permissions in `finally`). It validates supported/missing dumps,
checks the disabled updater, draws the Metal UI, starts/stops the real runner,
quits/relaunches the launcher and repeats Play/Stop. The child produces hundreds
of real frames with AVX enabled, embedded Metal shaders and system fonts.
Settings persist across both full launcher processes. A renamed disposable
external-disk alias verifies Play disables when the dump disappears and recovers
when it returns; the actual shared external disk was not unmounted.

Native launch without a Terminal, the macOS folder picker, save-folder display
and physical mouse Play were checked separately. The player enabled 4K and
reported it looked good. The child log records a 3840×2160 game target presented
into a 1920×1080 window, CoreAudio's 256-frame period, the physical Switch Pro
Controller and reads from the selected iCloud folder. The player did not save
that test session; no new gameplay save is claimed from it.

The inspected UI captures show readable system fonts, correct artwork/textures,
Play/Stop state and missing-dump errors. Captures containing game artwork and
raw player state stay outside this repository.

## Remaining checks

This is a local developer bundle, not a notarized public release. Fresh-account
and clean-machine testing, actual disk detach/reattach, downloaded/quarantined
app launch, broader display/device changes, networking compatibility and long
soaks remain open. Earlier gameplay/save/audio verification is recorded in
[gameplay-smoke.md](gameplay-smoke.md) and [audio-input-smoke.md](audio-input-smoke.md).
The full [milestone plan](../docs/macos-port-plan.md) remains the acceptance list.
