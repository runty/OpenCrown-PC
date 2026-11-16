# Masked SSE exception-status probe

This probe preserves and compares the observed MXCSR gap; it does not fix it.
It uses the four actual `raw_*ss` objects from a completed, passing fixed run of
[`sse-nan-results.py`](../sse-nan-results.py), plus that review's runtime object.
The x86-64 reference executes through Rosetta on Apple silicon, not on a
physical AMD/Intel CPU. Object and input hashes are written to the result.

```sh
python3 ios/cpu/review/mxcsr/run.py \
  --review /Volumes/office/Build/OpenCrown/mobile-adventure-live/sse-nan-candidate/review \
  --remill /Volumes/office/Build/OpenCrown/cpu-backends/remill \
  --output /Volumes/office/Build/OpenCrown/mobile-adventure-live/mxcsr-matrix
```

For add, subtract, multiply and divide, eleven operand cases are each run with
zero or pre-existing invalid sticky state: 22 cases total. Exception masks are
set, rounding is nearest, and FTZ/DAZ are off. Native and lifted results match
in all 22 cases. Guest MXCSR differs in 13; those include invalid, divide-by-zero,
overflow, underflow and inexact status. On this host, clearing host exceptions
before each operation and mapping the observed five host flags plus the seeded
guest invalid flag happens to match all 22 reference statuses. This is limited
observation, not a correctness proof for that strategy.

A negative control pre-raises host invalid before an exact guest `1 + 2`: the
reference guest has no invalid status, while the host flag remains set. A naive
`fetestexcept` at STMXCSR could therefore leak exceptions from native services
or other host work. Proper isolation must account for guest/native transitions,
nested callbacks, thread-local state, clear/load/store semantics and host
floating-point compiler behavior. It must also independently cover the x86
**denormal operand** flag, DAZ/FTZ, exception priority, unmasked traps and packed
lanes. None is qualified here. Preserve the original failed results; don't
convert this probe into a passing CPU/port verdict.
