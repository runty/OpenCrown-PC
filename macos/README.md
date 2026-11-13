# Mac development build

The first implementation supports dependency-free x86-64 loader and format
probes on Apple silicon through Rosetta. Use an external build directory and
keep the game dump separate.

```sh
./macos/build.sh --build-dir /Volumes/office/Build/OpenCrown/macos-probes
ROSETTA_ADVERTISE_AVX=1 ctest --test-dir /Volumes/office/Build/OpenCrown/macos-probes --output-on-failure
```

The build contains the four format tools plus format, loader, CPU compatibility
and touch tests. The loader test covers a synthetic relocated module and import
stub calling the host, a collision with an occupied mapping, and 64 cycles of
modifying and executing protected machine code.

The CPU suite uses synthetic instructions. Its output reports skipped comparison
cases and patch groups whose padding differs from the real game. Inspect those
counts before interpreting a successful exit. Set the Rosetta environment before
starting a test process, and record it with the OS build.

The build script also accepts `--mode headless` and `--mode rendered` for
development of the complete runtime. These modes currently encounter Linux-only
host APIs. The first full headless attempt reaches the texture tracker's Linux
signal context access. Networking, clocks and runner fault diagnostics also need
Darwin adapters before a real game can start.

The [milestone plan](../docs/macos-port-plan.md) tracks the remaining work.
