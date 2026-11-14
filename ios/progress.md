# Device port progress

## 2026-11-13 — Translated routines execute on both devices

Implementation 31ffb1e adds the checked guest runtime and eight Remill AOT
synthetic routines to the signed arm64 app. Both the iPhone 17 Pro Max and
iPad mini (A20 Pro) execute the same corpus with 146 checks passing, zero
failures and two workers. Each passes again after a terminated-process relaunch,
and its persistent launch count advances from 3 to 4. All runs use devicectl
without LLDB. The app's recursive signature verifies.

The arm64 Mac harness reports exactly the same results and corpus hash:
8447df7e24d3edc8c43647f2beb892df881aa3afeb314eb74ecc9de2c56a81f7.
Seven routines compare with actual native x86-64 execution under Rosetta.
The eighth uses seeded per-worker guest FS/TLS memory and expected arithmetic.
Each worker executes eight input vectors per routine, plus nine memory/dispatch
checks. Arithmetic flags compare CF/PF/ZF/SF/OF where defined and selected by
the fixture. Coverage includes branching, rotation, unaligned reads/writes,
direct/indirect guest calls, stack/return state, AVX XOR and guest TLS isolation.
Invalid reads/writes, final valid qword, unresolved targets and a translated
out-of-range memory access are checked. Runtime bridges record faults without
unwinding across nounwind LLVM intrinsics.

Build with ios/cpu/build.py, then pass its output directory as the third
argument to ios/build-probe.sh, as documented in README.md. The build runs the
native oracle, generates the expected vectors and dispatch table, tests the
arm64 Mac library and produces a static iPhoneOS library. Remill's example
lifter leaves some callee bodies external, so the builder explicitly lifts
those entry points and verifies them through real guest call/return execution.
The generated tables, IR, objects, oracle results and device JSON stay external.

Private evidence is in cpu-harness/ and ios-probe/ under the OpenCrown build
root. mac-results.json, verification.json and cpu-results/cpu-relaunch device
JSON files identify source revision and corpus. ios-cpu-app-build.log records
the provisioned Release build. The probe displays and exports its CPU report.

P1 remains open for real eboot/libc routines, atomics, guest-to-HLE calls and
callbacks, broader SIMD/flags coverage, performance and interpreter comparison.
These eight routines are synthetic. The complete game has yet to boot on a device.

## 2026-11-13 — P0 hardware shell and initial P1 lift

The signed arm64 shell is installed on both reference devices as OpenCrown Probe
(bundle ID com.wadscorp.opencrown.probe), from clean source commit 4f8caea.
Xcode Release build and signature verification pass. devicectl launches without
LLDB succeed, then repeat installation and terminated-process relaunch increase
the persistent launch count from 1 to 2 on each device. Retrieved JSON files
contain the expected build ID and machine. The player confirms Home Screen
launch, displayed diagnostics and the share sheet on both devices.

| Device | OS | Page size | Metal device | BC capability query |
| --- | --- | --- | --- | --- |
| iPhone 17 Pro Max, iPhone18,2 | 27.2.1, 24B156 | 16384 | Apple A19 Pro GPU | true |
| iPad mini (A20 Pro), iPad19,1 | 27.2.1, 24B8156 | 16384 | Apple A20 Pro GPU | true |

P0 is complete for the device shell and existing reference inputs. A private
manifest pins the matching eboot/libc hashes, input script, Felix payload hash
and installed Mac reference log containing TITLE at vblank 677 and INN at 1322.
The fixtures use the already verified Mac runtime at 784fe46. The diagnostic
screen explicitly reports guest_execution=false. GPU capability queries still
need actual compressed-texture draws in P3.

### P1 backend work

Built Remill at 66c488c9911fd9b368e4d0c9647d74239f8bba8b with Homebrew LLVM 23.1.5.
Its upstream dependency superbuild completed using external LLVM. The main build
initially failed because LLVM 23 removed convert_from_fp16/convert_to_fp16.
The adjacent remill-llvm23.patch handles half-precision memory loads/stores with
raw i16/half bitcasts for LLVM 23+, retaining the older code on earlier LLVM.
The patched full build succeeds. Half-value runtime regression coverage is pending.
[Remill source](https://github.com/lifting-bits/remill/tree/66c488c9911fd9b368e4d0c9647d74239f8bba8b)

The first synthetic fixture, mov rax,rdi / add rax,rsi / ret, lifts to LLVM IR
and compiles into an arm64 iOS Mach-O object. Reproduce with probe-lifter.sh.
Its undefined-symbol list exposes the runtime bridges still needed for flags,
guest return, stack reads and FS/GS/stack initialization. The object has yet to
be linked into the mobile probe and executed against reference vectors. Remill
also logs a redundant return-terminator diagnostic for this trace, so inspect
control flow while implementing that harness. Translation output alone does
not complete the P1 CPU proof.

The locally available source-recomp translator uses i386 registers and 32-bit
guest addresses. Its current CPU runtime requires substantial changes for this
x86-64 guest. Remill is the first candidate under experiment. Interpreter
comparison, SIMD/flags/atomics/callbacks/TLS, two-thread device tests, and real
routines from both game modules remain in P1. Backend selection is still open.

### Reproduce the host lifter build

Use a mounted external build root. In a fresh Remill checkout at the pin above,
apply ios/remill-llvm23.patch from this fork with git apply, then follow:

```sh
cmake -G Ninja -S remill/dependencies -B remill-deps -DUSE_EXTERNAL_LLVM=ON -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/llvm -DCMAKE_INSTALL_PREFIX="$PWD/remill-install"
cmake --build remill-deps --parallel 8
cmake -G Ninja -S remill -B remill-build -DCMAKE_PREFIX_PATH="$PWD/remill-install" -DCMAKE_BUILD_TYPE=Release
cmake --build remill-build --parallel 8
/path/to/OpenCrown-PC/ios/probe-lifter.sh "$PWD/remill-build/bin/lift/remill-lift-23" /opt/homebrew/opt/llvm /path/on/external/disk/fixtures
```

Private evidence stays under the external OpenCrown build root in ios-probe/
and ios-cpu-fixtures/, with dependency source/build caches in cpu-backends/.
The device JSON, provisioning files and generated objects stay outside Git.
The next implementation task is a checked guest-memory/runtime harness that
executes lifted fixtures on the Mac reference and both signed device apps.
