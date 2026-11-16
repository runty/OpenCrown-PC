# Experimental floating-environment boundary

This isolated prototype explores separating guest and native floating-point
flags across a service call. It is **not wired into the game runtime** and does
not repair guest MXCSR. A boundary saves the guest environment, holds native
exceptions, and restores the guest environment on exit. A nested callback
saves native state, restores guest state for the callback, records guest changes,
and then restores native state. Nested boundaries compose in the local fixture.

```sh
python3 ios/cpu/review/fenv-boundary/run.py \
  --review /Volumes/office/Build/OpenCrown/mobile-adventure-live/sse-nan-candidate/review \
  --remill /Volumes/office/Build/OpenCrown/cpu-backends/remill \
  --output /Volumes/office/Build/OpenCrown/mobile-adventure-live/fenv-boundary-test
```

Two concurrent threads perform 1,000 sequences each. Every sequence checks
rounding and sticky flags across a host boundary, nested callback and nested
boundary, a second callback and exception unwinding. Native, ASan/UBSan and
TSan each pass 22,000 checks. Both iPhoneOS and Simulator compile the fixture
with syntax-only checks; no physical device or simulator execution is implied.

A separate test invokes the real reviewed lifted `ADDSS` object. Its invalid
operation sets the host invalid flag; the prototype keeps a native overflow flag
out of the guest side and retains a callback's divide-by-zero flag. Guest MXCSR
still remains `1f80` rather than the reference `1f81`. This is deliberate:
separating fenv scopes alone does not map host flags into guest state.

Before considering integration, establish how each HLE service should expose
intentional rounding/status changes and guest callback behavior, including
thread cancellation and all exceptional exits. Design and verify MXCSR
load/store/clear and sticky mapping, denormal operand status, DAZ/FTZ, rounding,
exception priority, unmasked traps, SIMD lanes, host compiler reordering and
native code that may change FP state outside the service boundary. Validate on
both target devices and measure the added service/callback cost. This proof does
not qualify full CPU fidelity, gameplay, lifecycle, audio or performance.
