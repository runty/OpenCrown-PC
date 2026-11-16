# Experimental MXCSR instruction sequence

This is an isolated research fixture. Its Remill patch is deliberately absent
from the normal bootstrap and must not be used for an app build. The fixture
rejects DAZ, FTZ, unmasked exceptions and reserved MXCSR bits before writing
the guest register. It does not implement guest traps or other instructions.

On 2026-11-16 a private semantics/lifter build generated actual lifted x86
`LDMXCSR`, scalar arithmetic, and `STMXCSR` sequences. Against the existing
24-case physical AMD Zen 4 reference (which uses an equivalent arithmetic and
status sequence, not these exact assembled bytes), the O2 lifted fixture
matched 23 result/status pairs. The `add_snan` case with initially clear flags
returned the correct NaN bits but omitted the invalid sticky flag. This is a
failure, not qualification. A first fixture revision accidentally changed two
2.0 inputs when renaming a PC; that fixture defect was corrected before the
23/24 comparison. The exact-byte native differential, unsupported-control
unwind checks, sanitizer runs and device tests remain to be done. The AMD
reference is not evidence of identical PS4 Jaguar behavior.

Outputs are under `mobile-adventure-live/sse-nan-candidate/mxcsr-lift-private`
on the Office build volume; the reference is `mxcsr-mapping/amd-matrix-24.txt`.
No production integration or performance conclusion follows from this test.

A follow-up experiment explicitly records invalid status for a signaling NaN
operand in these four masked scalar operations. Its actual lifted instruction
bytes were also executed on the physical AMD Zen 4 host: all 40 cases matched
result bits and status, including signaling NaNs in either operand. An
ASan/UBSan build matched the same 40 results. Four unsupported guest controls
(DAZ, FTZ, unmasked invalid, and a reserved bit) were rejected by actual lifted
`LDMXCSR` with guest MXCSR and host FPSR unchanged. The earlier 23/24 result
remains the negative control. These checks do not qualify other instructions,
all operand combinations or rounding modes, guest trap behavior, integration
with native callbacks, game behavior, performance, or physical iOS devices.
The comparison script accepts a reference host; its output is not hardware
attestation. Detailed v2 results are in `mxcsr-lift-private-v2` on Office.
