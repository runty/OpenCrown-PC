# Experimental scale-loop study

This isolated study targets the pinned game routine at canonical `0x20619220`.
The original 57 bytes from `eboot.image` hash to
`4bc6f9fed2b1f9e325415296cc7bbadf3056c45cdec94da6998507d6f0bcda3f`.
Its scalar multiply instruction at `0x20619240` was identified in the prior
phone profile. `prototype.cpp` is a research implementation; it is **not**
installed, wired into the game, or qualified as a replacement. It calls the
same checked scalar memory bridges but omits the normal trace-entry wrapper.

The exact original bytes were executed on an AMD Ryzen 7 7840HS Zen 4 host;
the actual game-generated ARM object and the prototype were executed on Mac.
For 32,778 selected cases (including 16 operand classes, 0–31 elements and
four rounding modes), both matched the AMD result memory, selected GPRs,
defined arithmetic flags and XMM/YMM lanes. The fixture does not compare
MXCSR status or every register. One early prototype mismatched 3,849 cases
because it copied upper XMM lanes from the wrong source; the corrected version
matches this matrix. Captured AMD identity is not hardware attestation, and
Zen 4 is not the PS4 Jaguar.

A separate 14-case comparison checks the complete translated State and guest
memory against the actual ARM object on normal returns, two overlapping-buffer
cases, first/later out-of-bounds read/write, invalid return stack and five
cancellation points. All match, including an ASan/UBSan run instrumenting the
fixture, prototype and memory runtime; the original game object itself is not
sanitizer-instrumented. An earlier prototype left incorrect fault RIP and
parity; its negative output is retained. These finite cases do not establish
all fault timing, aliasing, concurrency, trace-observer, floating-environment,
MXCSR, extremely large count or game behavior.

An alternating six-pair Mac microbenchmark of 5,000 calls per sample used
synthetic checked memory. Median baseline/prototype nanoseconds per call were
30.87/28.27 (1 element), 72.85/63.74 (4), 216.73/212.18 (16), 816.05/803.59
(64), and 3092.68/3140.28 (256). Other host workloads were present. These are
small, variable local samples, not device, real-game or sustained performance;
they do not justify promotion. In particular, the earlier faster incomplete
prototype omitted correct fault-state bookkeeping and is not a valid baseline.

The source files are standalone fixtures. Compile `lifted.cpp` with the real
candidate `memory.o` and `20619220.o`; add `prototype.cpp` and define
`DCP_SCALE_PROTOTYPE` for the prototype. `compare-amd.py` extracts and verifies
the original code bytes, executes them on an authorized x86 Linux reference,
and compares the saved output matrices. `fullstate.cpp` and `bench.cpp` use the
same two real objects. Full commands, hashes, raw negative controls, machine
identity and outputs are retained under
`/Volumes/office/Build/OpenCrown/mobile-adventure-live/sse-nan-candidate/scale-loop-study`.
