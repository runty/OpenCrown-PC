# Mac development build

The x86-64 headless runner builds on Apple silicon and runs the supported
Dragon's Crown Pro patch 1.03 dump under Rosetta. Build directories and test
state belong outside the source checkout and game dump.

```sh
./macos/build.sh --build-dir /Volumes/office/Build/OpenCrown/macos-headless --mode headless
ROSETTA_ADVERTISE_AVX=1 ctest --test-dir /Volumes/office/Build/OpenCrown/macos-headless --timeout 60 --output-on-failure
./macos/run-headless.sh --build-dir /Volumes/office/Build/OpenCrown/macos-headless \
  --dump /path/to/CUSA10454 --state-dir /path/to/disposable-test-state \
  --frames 3600 --input macos/headless-input.txt
```

The state directory contains `user/` and `saves/`. Reusing it on the next
launch tests reload. Supply a disposable directory. The runner writes only its
own test progress there. The script sets `ROSETTA_ADVERTISE_AVX=1` before
launch unless explicitly overridden, and the runner logs its effective CPU
features. This environment setting was verified on macOS 27.2.1.

For scene evidence at the pinned source baseline, set `DCP_PROBE='68840*'`
before the run command. The probe logs scene names passed to the guest
dispatcher. Synthetic breakpoint and single-step regression checks pass under
Rosetta before this diagnostic is used. Scene progress must be recorded alongside
flips, submissions, save accesses and the exit status.

The dependency-free mode remains available for early diagnostics.

```sh
./macos/build.sh --build-dir /Volumes/office/Build/OpenCrown/macos-probes --mode probes
ROSETTA_ADVERTISE_AVX=1 ctest --test-dir /Volumes/office/Build/OpenCrown/macos-probes --output-on-failure
```

Probe mode builds four format tools and format, loader, CPU compatibility, touch
and platform tests. Loader regressions cover synthetic relocation and an import
stub calling the host, occupied-map collisions, and 64 code mutation cycles.
Platform checks cover checked reads and writes, protection failures, socket
descriptor flags, SIGPIPE suppression and breakpoint/single-step contexts. The
headless suite additionally checks the guest monotonic timeout and module TLS
on multiple threads.

The CPU suite reports skipped native comparisons and synthetic patch groups
whose padding differs from real game bytes. Inspect its counts before
interpreting the exit code.

Visible Metal rendering builds with Python 3, Xcode's Metal compiler and
SPIRV-Cross installed. The reference tool is Homebrew spirv-cross 1.4.363.0.
CMake translates all eight checked-in SPIR-V shaders, compiles individual Metal
libraries and embeds them in the runner. Runtime launch uses those embedded
libraries and needs no compiler. SDL 3.4.18 and ImGui 1.91.9b archives are
SHA-256 pinned.

```sh
./macos/build.sh --build-dir /Volumes/office/Build/OpenCrown/macos-rendered --mode rendered
ROSETTA_ADVERTISE_AVX=1 ctest --test-dir /Volumes/office/Build/OpenCrown/macos-rendered --timeout 60 --output-on-failure
```

To launch a visible test, set explicit disposable state directories and use the
rendered runner. Copying the headless test save into this save root exercises the
same reload route. Supply the script for deterministic input, or omit
`--input` to use live input.

```sh
ROSETTA_ADVERTISE_AVX=1 DCP_USER_DIR=/path/to/test-state/user DCP_SAVE_ROOT=/path/to/test-state/saves /Volumes/office/Build/OpenCrown/macos-rendered/dcp_run /path/to/CUSA10454/eboot.bin --render --window --vsync --renderer gpu --size 1280x720 --frames 2100 --input macos/headless-input.txt
```

Optional `--shots 600=/path/to/opening.png,1100=/path/to/title.png,1800=/path/to/inn.png`
captures the 1920×1080 game target before window scaling and overlays. The visible
window still exits at the explicit `--frames` limit. Request shots before that
limit so the render thread can finish their readback. Logs identify the Metal
driver and swapchain size. `DCP_PROBE='68840*'` provides scene evidence and
`MTL_DEBUG_LAYER=1` enables Metal API validation for a diagnostic run.

Eleven tests pass on the reference Mac, including exact-pixel checks for Metal
uniforms, alpha blending, scissor orientation and two-texture mask sampling.
The visible smoke run reaches TITLE and INN, writes all three PNGs, and exits 0
at 2,100 frames. The spawn adapter also preserves redirected stdio and closes
unrelated descriptors in a child process.

Live testing now covers character creation, the full tutorial, town/guild/shop,
Ancient Temple Ruins combat through a Harpy boss attempt ending in game-over,
and two full process
quit/relaunch cycles recovering the same earned EXP and gold. The
[gameplay smoke record](gameplay-smoke.md) gives the route, controls, save hashes
and exact limits. A successful adventure clear, ImGui name/chat overlays, audio,
controller/hotplug, measured performance, the launcher workflow, Linux regression
execution and packaged-app tests remain open in the
[milestone plan](../docs/macos-port-plan.md).
