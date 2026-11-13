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

Rendered builds still need Metal shader integration and renderer/launcher host
adapters. Linux regression execution and packaged-app tests remain open in the
[milestone plan](../docs/macos-port-plan.md).
