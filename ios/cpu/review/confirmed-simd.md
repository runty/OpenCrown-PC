# Observed rounding and packed instruction review

The last three backend patches cover forms found at executable instruction
boundaries in the pinned game: SSE4.1 ROUND scalar/packed legacy/VEX forms;
YMM horizontal subtract and floating unpacks; and selected integer unpack and
multiply forms, packed min/max and square root. Static presence does not claim
that every form has executed during startup. Indirect tables are not exhaustively
traversed, and an undecoded boundary remains unclassified.

Run the two independent owned-code suites against the checked backend:

```sh
python3 ios/cpu/review/rounding.py --output /Volumes/office/Build/OpenCrown/rounding-probe --backends /Volumes/office/Build/OpenCrown/cpu-backends
python3 ios/cpu/review/confirmed-packed.py --output /Volumes/office/Build/OpenCrown/confirmed-packed-probe --backends /Volumes/office/Build/OpenCrown/cpu-backends
```

Rounding uses integer IEEE bit operations to preserve signed zero, NaN handling,
MXCSR control bits, lane merging/clearing and new IE/PE state. The 360-routine,
1,536-vector suite passes 13,824,000 exact masked-native dword checks, 2,560,000
suppression invariance checks and two unmasked fault-before-destination-write
cases. Suppression invariance compares AOT behavior under different masks.
Rosetta forces MXCSR masks on load, so unmasked checks are AOT properties,
not native exception-delivery comparisons. Unsupported unmasked faults stop
explicitly before writing the destination; architectural guest #XM delivery is
not implemented.

The packed review covers 44 routines and 9,280 vectors, passing 10,208,000 dword
checks. It verifies source and memory forms, complete YMM lanes, legacy upper
preservation, VEX zeroing and RC/DAZ/FTZ/control behavior. Newly covered packed
floating helpers do not model sticky exception flags or #XM delivery; those
native comparisons exclude only MXCSR status bits 0–5 and still compare control
bits. This limitation is not a claim of complete floating-point-environment
emulation.

Each external review JSON records backend, generated-code and oracle identities
and verifies that shared backend inputs did not change during its run. Owned
synthetic routines and tests are public; original game bytes and static inventory
evidence stay in the external private build tree.
