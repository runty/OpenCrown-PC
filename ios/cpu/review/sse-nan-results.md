# SSE/AVX binary arithmetic NaN result bits

This isolated candidate follows the first-source NaN policy documented in
[AMD APM volume 1, table 4-5](https://www.amd.com/content/dam/amd/en/documents/processor-tech-docs/programmer-references/24592.pdf)
and [Intel SDM volume 1, table 4-7](https://cdrdv2-public.intel.com/843820/325462-sdm-vol-1-2abcd-3abcd-4-1.pdf).
The selected NaN retains its sign/payload and is quieted. Invalid arithmetic
without a NaN source returns the negative indefinite NaN. These rules differ
from the ordinary ARM propagation used by the unchanged translated arithmetic.

The patch changes only the result selection of binary SSE/AVX add/sub/mul/div,
scalar and packed. Min/max remain unchanged and serve as controls. It adds no
memory reads and retains the preceding width correction. x87, horizontal
arithmetic, conversions, guest MXCSR status/traps and DAZ/FTZ are outside this
result-only correction and remain unqualified. No global FPCR change is made.

```sh
python3 ios/cpu/review/sse-nan-results.py \
  --backends /Volumes/office/Build/OpenCrown/cpu-backends \
  --width-backend /Volumes/office/Build/OpenCrown/mobile-adventure-live/scalar-fp-width-candidate/backend \
  --output /Volumes/office/Build/OpenCrown/mobile-adventure-live/sse-nan-candidate/review
python3 ios/cpu/review/sse-nan-spec.py \
  /Volumes/office/Build/OpenCrown/mobile-adventure-live/sse-nan-candidate/review
```

The first backend supplies read-only build commands/libraries. The width backend
supplies the actual 18-patch source and frozen baseline semantics. Private
sources, semantics and explicitly bound lifters are built separately; hashes
verify that shared inputs remain unchanged. The baseline is not retagged.

The x86-64 instruction binary executes under Rosetta on Apple M5 Max, **not
physical AMD or Intel hardware**. Its raw expectation stream is cross-checked
against an independent integer policy: 200,704 NaN-input results, 8,960 invalid
results without NaN inputs and 14,848 min/max NaN/zero controls pass. The native
stream is never normalized or replaced by this policy check.

The differential matrix contains 96 forms, 2,048 vectors each: all 16×16
operand-class pairs, four host rounding modes and two alignments. Scalar,
packed XMM/YMM, legacy/VEX, memory/register and min/max controls capture source,
destination, inactive lanes and arithmetic flags. Register VEX cases alias the
destination with the first source; this matrix does not independently qualify
all alias arrangements. Forty-eight valid native protected-end operands work,
and forty-eight one-byte overruns fault in handled child processes.

Baseline and fixed arm64 run both normally and under ASan/UBSan. Each evaluates
2,163,456 checks including 384 checked-memory boundary cases. The unchanged
baseline retains 14,656 exact result-word mismatches, all NaN values; both fixed
runs have zero failures and match all 14,155,776 captured bytes exactly. Only NaN
result words change baseline/fixed; access/source/flags/inactive lanes remain
exact. Existing strict failed width-review reports are retained unchanged.

This is bounded CPU result/access evidence. Full game regeneration, actual
runtime integration and performance cost remain pending. The candidate stays
on `codex/ios-sse-nan`; it is not integrated, installed, pinned or a full CPU/port
acceptance. Neither physical device is used by this review.
