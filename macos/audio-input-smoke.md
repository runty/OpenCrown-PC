# macOS audio and controller verification

The 2026-11-13 development build enables CoreAudio playback and verifies a
Nintendo Switch Pro Controller through gameplay and physical reconnect. Tests
ran on M5 Max, macOS 27.2.1, Xcode 27.2, SDL 3.4.18, with the x86-64 runner
under Rosetta 2 and the supported CUSA10454 patch 1.03 dump. All game tests used
explicit disposable copies of the saved Felix character and settings.

## Implementation

- Initialize SDL video/events on the process main thread before installing audio
  and gamepad sources, then start guest threads. Preserve the existing Steam
  video-driver selection before SDL initialization.
- Apply the PulseAudio/PipeWire/ALSA preference only on Linux. Let SDL select
  CoreAudio on Apple platforms; an explicit `SDL_AUDIO_DRIVER` remains usable.
- Request **256 device frames** on Apple, matching CRI's 256-frame guest audio
  batches. `SDL_AUDIO_DEVICE_SAMPLE_FRAMES` can explicitly override this.
  The reference device reports float stereo 48 kHz, 256 frames per device buffer,
  with a 1024-frame queue target. Previously it used 1024 device frames and a
  3072-frame queue target.
- Check audio resume failure and release owned SDL streams when the sink is
  destroyed. Report mix peak and clipped samples with `DCP_AUDIO_LOG=1`.
- Own each SDL gamepad handle with a matching open/close. Preserve surviving
  device order, remove disconnected handles and respond to remapping. Clear
  snapshots and queued rumble/LED requests when device membership changes.
  Log device IDs and slot mappings, including same-count replacements.
  `DCP_INPUT_LOG=1` additionally logs button down/up events.

## Live checks and the popping fix

The original development runner forced Linux audio drivers and silently fell
back to the paced null sink on macOS. Removing that preference enabled audible
music/voice, but the player reported intermittent background pops. A 440 Hz
reference tone through the same sink, device and queue was clean.

A capture taken **before SDL playback** contained abrupt short zero-filled
sample runs, with no clipping or non-finite samples. This isolated the gaps to
the game/mixer feed rather than proving a CoreAudio output defect. The working
explanation is that 1024-frame device pulls allow four 256-frame guest batches
to be submitted in a burst, outrunning the asynchronously prepared guest mix.
Matching the device period to the guest period removed the repeated gaps in
the measured route. The player reported the pops gone or much reduced.

Matched runs used the same disposable state, existing
`macos/headless-input.txt`, GPU renderer, 1280×720 window, VSync, Metal API
validation and a 2400-frame limit. Both exited **0**, reached title/inn and
captured exactly 30 seconds of pre-device float stereo samples:

| Measurement | Explicit 1024-frame device buffer | New default, no period override |
| --- | ---: | ---: |
| Device frames | 1024 | 256 |
| Queue target, frames | 3072 | 1024 |
| Short exact-zero runs after the first 5 seconds | 133 | 0 |

Here a short run is 16–512 consecutive frames with both stereo samples exactly
zero; longer scene-transition silences are counted separately. The old short
runs were 32, 64, 96 or 160 frames, aligned with the end of a 256-frame batch.
This metric alone does not classify every silence as a defect; the waveform
edges, clean-tone comparison and listening result support the diagnosis.
It does not establish long-session drift or every output-device combination.

The player loaded Felix, moved through town and into Ancient Temple Ruins,
used menu/combat buttons and confirmed physical controller reconnect worked.
The device log independently records a **1 → 0 → 1** gamepad transition, with
a new instance ID for the reconnected Nintendo Switch Pro Controller
(`057e:2009`). Keyboard/mouse play was verified in the
[preceding gameplay session](gameplay-smoke.md). Physical rumble strength and
additional controller models were not assessed.

## Reproduce and diagnose

Build and run the [rendered tests](README.md). The final full rendered suite passes all thirteen tests in 35.73 seconds.
An earlier lossy-network test exceeded its 1500 ms deadline while a live game
was running; it passed on an isolated retry and in the final full run. The two
additional SDL tests use synthetic data and virtual devices, without game files
or audible test tones.
They cover:

- Mono/stereo/8-channel downmix, clipping, silence, float capture contents and
  its 30-second size cap, driver override, Apple default device period and
  real queue-driven pacing through SDL's dummy audio device.
- Face/shoulder/stick/D-pad/menu buttons, sticks, triggers, touch coordinates,
  prompt-device switching, rumble values, detach/reconnect, same-count device
  replacement, stale feedback clearing and SDL handle reference ownership.

For a live diagnostic, set explicit test state and run:

```sh
ROSETTA_ADVERTISE_AVX=1 DCP_USER_DIR=/path/to/test-state/user DCP_SAVE_ROOT=/path/to/test-state/saves DCP_AUDIO_LOG=1 DCP_INPUT_LOG=1 /path/to/macos-rendered/dcp_run /path/to/CUSA10454/eboot.bin --render --window --vsync --renderer gpu --size 1280x720
```

Optional diagnostics (omit them for normal play):

- `DCP_AUDIO_TONE=1`: replace the game mix with a 440 Hz reference tone.
- `DCP_AUDIO_CAPTURE=/path/to/mix.wav`: collect up to 30 seconds of the actual
  pre-device mix as 32-bit float stereo 48 kHz WAV. It reserves about 11 MiB
  while recording and writes the file once full. An orderly sink destruction
  saves a partial capture; the runner's immediate process exit does not run
  destructors, so wait for the **capture saved** log before quitting. The file
  write itself can perturb timing; do not use this mode for pacing acceptance.
- `SDL_AUDIO_DEVICE_SAMPLE_FRAMES=1024`: reproduce the former device period.
- `DCP_AUDIO_QUEUE=<frames>`: existing queue-depth diagnostic override.

Raw game audio, saves and diagnostic logs stay outside the repository.

## Remaining acceptance

Long-session audio drift, output-device migration/disconnection, recovery after
focus changes, physical rumble/light-bar behavior, other controllers and display
changes still need coverage. No claim of audio correctness on every Mac or iOS
is made by this short reference-host test. Formal produced/dropped/presented
frame counts and presentation percentiles, a complete adventure, failed-save
recovery and packaged-app validation remain open in the
[milestone plan](../docs/macos-port-plan.md).
