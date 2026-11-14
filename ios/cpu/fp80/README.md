# Exact 80-bit x87 arithmetic for arm64 AOT

LLVM's arm64 backend cannot implement x86 double-extended arithmetic directly.
It can also silently turn an unanchored `x86_fp80` load/store copy into a 32-bit
copy. `fp80-lower` therefore replaces arithmetic with pointer-only C helpers,
then represents **all executable FP80 values as i80 bits**, including constants,
loads/stores, bitcasts, PHIs, selects and freeze. It never substitutes `double`
or the Apple arm64 `long double` type for 80-bit values.

The runtime uses unmodified Berkeley SoftFloat Release 3e with the `8086-SSE`
specialization, little-endian configuration and `_Thread_local` mode/flag state.
SoftFloat sources remain in the external build cache; none are vendored here.
The official archive is:

- URL: `https://www.jhauser.us/arithmetic/SoftFloat-3e.zip`
- SHA-256: `21130ce885d35c1fe73fc1e1bf2244178167e05c6747cad5f450cc991714c746`
- License: Berkeley's three-clause license, copied into build output
  `ThirdPartyNotices/SoftFloat-3e-LICENSE.txt`.

See the [official interface documentation](https://www.jhauser.us/arithmetic/SoftFloat-3/doc/SoftFloat.html)
for supported formats, rounding modes and noncanonical encoding boundaries.

## Build and integration

```sh
python3 ios/cpu/fp80/build.py \
  --output /Volumes/office/Build/OpenCrown/fp80-probe

/Volumes/office/Build/OpenCrown/fp80-probe/fp80-lower \
  PRIVATE_INPUT.ll PRIVATE_LOWERED_OUTPUT.ll --remill-control
```

Use LLVM **23.1.5** and Xcode with macOS/iPhoneOS/simulator SDKs. `--llvm` chooses
the installed LLVM prefix. A previously verified source extraction may be
supplied with `--source`; the manifest marks it caller-supplied and hashes its
compiled inputs. Fresh downloads verify the pinned archive hash. Outputs must
be on mounted external volumes. Existing objects are reused by source/header
and command hash; caches are never deleted. Deterministic `libtool -D` archives
keep unchanged helper manifests stable across repeated builds, avoiding spurious
whole-corpus provenance invalidation.

The host-only lowerer links Homebrew LLVM. Mobile helper archives contain
portable C and SoftFloat, with no LLVM or host dynamic-library dependency:

- `mac/libdcp-fp80.a` (arm64 macOS 15+)
- `ios/libdcp-fp80.a` (arm64 iOS 18+)
- `simulator/libdcp-fp80.a` (arm64 iOS 18+ Simulator)

Link the matching archive **after** the core runtime archive, and package its
license notice. The archive also supplies `__remill_fpu_exception_clear`,
`__remill_fpu_exception_test` and `__remill_fpu_exception_raise`; it deliberately
does not duplicate the core memory or rounding bridges.

Production Remill traces require `--remill-control`. The lowerer derives the
control-word byte offset from that IR's `State`/`FPU` type layout, loads the
guest x87 control word before each helper and sets per-thread rounding/precision.
It rejects missing or ambiguous layouts. Helpers do not change host fenv
rounding, so host FPCR continues to implement SSE rounding independently.
Generic synthetic functions omit the flag and explicitly set helper control
with `dcp_fp80_set_control`, or use default host-fenv rounding/full precision.

Input pointers carry 10 little-endian bytes: 64 significand bits followed by
16 sign/exponent bits. Helper output buffers require 16 bytes; padding bytes
10..15 are zeroed. No FP80 value crosses a native C calling convention.

Builder provenance must include the lowerer binary/helper-source hashes and
SoftFloat manifest/archive pin, plus hashes of raw and lowered IR. A lowerer,
helper, semantics-bitcode or relevant patch change requires relifting/relowering
the affected cached traces. Successful arm64 compilation alone is insufficient
evidence that storage precision was preserved.

## Operation coverage

The lowerer implements scalar addition, subtraction, multiplication, division,
square root, signed/unsigned integers up to 64 bits, f32/f64 conversions, all
16 LLVM comparison predicates, negate, abs and integer-rounding intrinsics.
Original bit storage remains exact. Unknown transcendental functions,
constrained intrinsics, FP80 function argument/return ABIs, vector operations
and aggregate/global FP80 storage fail explicitly before arm64 code generation.
IEEE remainder (`remainderl`), truncating remainder (`fmodl` and LLVM `frem`)
use exact extended-format SoftFloat arithmetic; quotient rounding is independent
of the guest precision/rounding control. FMA and other unsupported operations
are rejected rather than approximated.

The integer-only lowering of `llvm.is.fpclass.f80` currently accepts only mask
144 (positive/negative subnormal). It checks the raw exponent, explicit integer
bit and significand without canonicalization or floating environment changes.
Other masks remain explicit errors: LLVM’s x86 handling of pseudo-denormals and
unsupported encodings does not permit generalizing from this one predicate.

Remill represents FSCALE as
`value * exp2l(llvm.trunc.f80(exponent))`. The lowerer recognizes that specific
form and replaces the whole operation with direct extended-format scaling.
The helper normalizes the significand, adjusts the exponent and uses SoftFloat's
extended round/pack routine. It never materializes an intermediate power of two
that could overflow while the final result remains finite. Infinite exponents
follow the x87 result table, including signed zero/infinity and invalid cases.
Ordinary NaNs, infinities, subnormals and signed zeros are preserved according
to the specialization; pseudo-denormals are canonicalized and unsupported x87
encodings raise invalid and yield the x87 indefinite NaN.

## Control-word semantics patch

`../../remill-x87-control.patch` corrects the AOT Remill semantics used here:

- FLDCW/FNSTCW preserve the guest precision bits; FNSTCW stores guest RC.
- FLDCW/FNINIT leave host SSE rounding alone; FP80 helpers read guest CW.
- FNINIT resets the live FXSAVE x87 fields and virtual status flags, preserving
  MXCSR. Upstream legacy FSAVE writes alias MXCSR through its state union.

Apply this patch only during a coordinated backend refresh. Rebuild semantics
bitcode, relift the corpus, and record the new bitcode/patch hashes. It is intended
for this AOT helper backend; an independent native-x87 Remill runtime would need
its own native control synchronization.

Exception bridges map x87 bit positions explicitly to Darwin `FE_*` constants,
combining native flags with SoftFloat thread-local flags. Selected-mask clears
retain unrelated flags. Full architectural separation of x87 and SSE sticky
exception state, unmasked guest exception delivery, and complete x87 stack/tag
fault behavior remain outside this numerical proof. FNINIT's shared intrinsic
clear still clears the combined native/soft flag model. This work does not claim
complete x87 environment emulation.

## Tests

```sh
python3 ios/cpu/fp80/test.py --build /Volumes/office/Build/OpenCrown/fp80-probe
python3 ios/cpu/fp80/lowered-test.py --build /Volumes/office/Build/OpenCrown/fp80-probe
python3 ios/cpu/fp80/classify-test.py --build /Volumes/office/Build/OpenCrown/fp80-probe \
  --output /Volumes/office/Build/OpenCrown/fp80-classify-probe
python3 ios/cpu/fp80/remainder-test.py --build /Volumes/office/Build/OpenCrown/fp80-probe \
  --output /Volumes/office/Build/OpenCrown/fp80-remainder-probe
python3 ios/cpu/fp80/control-test.py \
  --build /Volumes/office/Build/OpenCrown/fp80-probe \
  --backend-root /Volumes/office/Build/OpenCrown/cpu-backends
```

The control test expects the patched backend. `--expect-stock-failure` checks
the known pre-patch failure; `--lifter` selects a scratch lifter for comparison.
The native oracle runs actual x87 instructions through Rosetta. Helper and
lowered-IR comparisons inspect all ten bytes, with no FP64 tolerance. Arithmetic
tests cover all four RC modes, 24/53/64-bit significand precision, boundary and
random inputs, NaN/Inf/zero/subnormal cases and FSCALE exponent extremes.
Unsigned integer conversion forces full native precision in its oracle because
the native compiler's synthesized unsigned cast includes arithmetic that would
otherwise introduce a control-precision artifact absent from LLVM `uitofp`.

Measured results on the development Mac:

- Direct helper oracle: **153,228 checks, 0 failures, 8 concurrent threads**.
- LLVM lowering → arm64 execution: **153,228 checks, 0 failures, 8 threads**;
  also cross compiled for iPhoneOS and Simulator. Unknown `sinl` was rejected.
- Exception masks/partial clearing/native+soft merge: **2,088 checks, 0 failures,
  8 concurrent threads** (`exception-fixture.cpp`).
- Native control/F[N]STCW and x87/SSE independence: **16/16 passed**; stock lifted
  backend failed 12/16; scratch patched backend passed 16/16.
- Actual private libc trace `30082b40`: **19,800 checks, 0 failures** after
  lowering. Inputs included random/special doubles and scale exponents up to
  INT32 limits, across three PC and four RC modes. Native actual
  FILD/FLD/FSCALE/FSTP result bits were the oracle; the real routine was executed,
  rather than replaced. `real-libc-fixture.cpp` contains only its test driver.

The private trace and generated oracle data remain under external
`/Volumes/office/Build/OpenCrown/fp80-probe`; they are not redistributed here.
These are native arm64 host execution and cross-compilation results. No physical
iPhone/iPad or Simulator execution was performed for this component.

Additional classification proof covers 1,321,120 raw inputs with 5,325,954
native LLVM checks and 42,275,840 lowered arm64 checks, including environment
preservation across eight rounding configurations. Unsupported masks are tested
to fail. All three SDK targets compile the lowered IR.

Remainder tests exercise 554,624 rows each through the production helper and
actual lowered IR, plus concurrent flag/control preservation. An independent
integer/rational oracle verifies 494,592 finite rows with zero differences.
Native Rosetta FPREM agrees for every truncating remainder; its FPREM1 differs
on 2,896 rows, while the exact oracle agrees with SoftFloat. Several Rosetta
answers violate the half-modulus bound, so the implementation retains the exact
result. This is a documented Rosetta discrepancy, not physical x86 validation.
Masked flags match across all rows. Partial-step FPREM status, unmasked traps,
and Remill’s simplified quotient-condition bits remain separate limitations.
