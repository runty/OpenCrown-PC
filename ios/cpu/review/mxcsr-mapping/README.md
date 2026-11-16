# Experimental masked MXCSR status mapping

This isolated prototype maps ARM FPSR invalid, divide-by-zero, overflow,
underflow and inexact sticky bits to x86 MXCSR bits, and maps those five bits
back on a simulated load. It preserves unrelated FPSR bits and retains the x86
denormal-operand bit in virtual state; **it does not detect new denormal
operands**. It is not connected to the translator, HLE runtime or any app.

```sh
python3 ios/cpu/review/mxcsr-mapping/run.py \
  --review /Volumes/office/Build/OpenCrown/mobile-adventure-live/sse-nan-candidate/review \
  --remill /Volumes/office/Build/OpenCrown/cpu-backends/remill \
  --output /Volumes/office/Build/OpenCrown/mobile-adventure-live/mxcsr-mapping-test
```

The test uses actual previously qualified lifted 32-bit scalar add/sub/mul/div
objects and an x86-64 Rosetta reference. In 24 masked cases with nearest rounding
and FTZ/DAZ off, 22 value/status results match. Two denormal-input cases retain
the correct value but miss x86 MXCSR bit 1; the strict status result remains
**failed**, and both mismatches are retained. Pure mapping checks 256 bit cases,
128 actual Mac FPSR load/store/clear checks, and 4,000 TSan checks across two
threads pass. Native/ASan+UBSan bit/register fixtures pass; both mobile SDKs
compile the register fixtures syntax-only.

An additional reference sequence performs invalid addition followed by division
by zero. The actual lifted sequence, separated by the experimental native fenv
boundary and a nested guest callback, stores `1f85`, matching Rosetta; a native
overflow flag is isolated. This does not prove general HLE, callback, or game
integration. The candidate still requires a compiler ordering guarantee for
FPSR reads/writes around translated arithmetic.

The unmasked-invalid trap reference is unavailable on this host: Rosetta reads
back `1f80` after the test requests `1f00`, forcing the invalid mask on, and the
unmasked trap probe exits without the expected signal. Its nonzero exit and raw
output are kept. Do not call this a trap pass or infer physical AMD behavior.
Denormal detection and priority, all rounding/control/FTZ/DAZ modes, packed
lanes, unmasked traps, native boundaries, status synchronization with actual
STMXCSR/LDMXCSR, compiler optimization, physical devices and gameplay remain
open. No candidate here should be installed or promoted on this evidence.
