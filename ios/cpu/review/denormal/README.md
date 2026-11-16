# Experimental scalar denormal-operand status

This prototype classifies whether a **masked, 32-bit scalar** ADD/SUB/MUL/DIV
should set x86 MXCSR denormal-operand bit 1. It considers DAZ, NaN priority and
the sampled denormal-divided-by-zero priority. It does not change arithmetic
operands/results, implement DAZ or FTZ, handle packed/64-bit operations or
unmasked traps, or alter the game runtime. See [Intel exception priority](https://cdrdv2-public.intel.com/843820/325462-sdm-vol-1-2abcd-3abcd-4-1.pdf)
and [AMD APM volume 1](https://www.amd.com/content/dam/amd/en/documents/processor-tech-docs/programmer-references/24592.pdf).

An independent AMD Ryzen 7 7840HS Linux x86-64 host ran `broad-native.cpp`:
4,096 cases cover four scalar operations, 16 representative bit patterns in
each operand and all four DAZ/FTZ combinations with exceptions masked. The
same x86 binary sources under Rosetta on M5 Max produce identical value and
MXCSR outputs. The integer classifier agrees on bit 1 for these 4,096 sampled
cases in normal and ASan/UBSan runs. It is not exhaustive over all FP inputs.

The classifier plus the experimental FPSR mapping produces correct result and
status for the 24 prior masked, nearest-rounding, DAZ/FTZ-off lifted cases
against the physical AMD capture, in normal and ASan/UBSan runs. The two
previously missing denormal flags are included. iPhoneOS and Simulator compile
the fixture syntax-only. This manually updates virtual state around an actual
lifted operation; it does **not** execute patched STMXCSR/LDMXCSR, implement
DAZ/FTZ result behavior, or prove game/device performance.

The physical AMD host's separately run unmasked invalid probe catches SIGFPE
in a child process (`native_child_exit=77`). Rosetta forced the invalid mask on
and could not serve as that oracle; its failure remains in the earlier review.
This is one hardware trap case on Zen 4, not a full Jaguar equivalence or a
working translated trap implementation.

Capture the three source outputs and system identity on an authorized physical
AMD Linux machine, using a temporary working directory:

```sh
c++ -std=c++17 -O1 broad-native.cpp -o broad && ./broad > amd-broad.txt
c++ -std=c++17 -O1 ../mxcsr-mapping/native.cpp -o matrix && ./matrix > amd-matrix.txt
c++ -std=c++17 -O1 ../mxcsr-mapping/trap-native.cpp -o trap && ./trap > amd-trap.txt
c++ -std=c++17 -O1 ../mxcsr-mapping/boundary-native.cpp -o boundary && ./boundary > amd-boundary.txt
```

Then run `run.py --help` for the required local review, source and captured
fixture paths. The script compares captures byte-for-byte to local Rosetta and
hashes inputs; a captured file and identity text are not hardware attestation.
The current private evidence is under `mobile-adventure-live/sse-nan-candidate/`
`mxcsr-mapping/denormal-qualified-v2/` and sibling AMD capture files. Full
exception priority, DAZ/FTZ semantics, signaling traps, 64-bit/packed lanes,
compiler ordering, real guest register instructions, HLE integration, gameplay,
performance and device acceptance remain open. Do not promote this prototype.
