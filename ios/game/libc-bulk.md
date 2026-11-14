# Typed native bulk libc adapter

`libc-bulk.cpp` implements the fixed guest ABI for three metadata-verified
exports from the pinned libc: memcpy `30054a60`, memmove `30054a20`, and memset
`300552e0`. The builder owns export verification, wrapper selection and
provenance. Actual libc startup, strlen and unrelated routines remain translated.

The C entry point is:

```cpp
Memory *dcp_libc_bulk(State &, uint64_t kind, Memory *);
```

Kinds are 0/copy, 1/move and 2/fill. Guest RDI is the original destination,
RSI is source or integer fill value, and RDX is the byte count. A completed
return changes only RAX, RIP and RSP: RAX receives the destination, RIP receives
the checked saved return address, and RSP advances eight bytes. Every other
byte in the CPU State remains unchanged.

The adapter uses `guestMove`/`guestFill` from the core runtime. They check
cancellation, take the existing memory lock once, validate the complete spans
before writing and call native memmove/memset. Fill uses the low byte of RSI.
Zero count touches neither source nor destination, allowing null/unmapped
arguments, while still performing a valid guest return. Copy uses memmove's
deterministic overlap behavior; overlapping memcpy lies outside the standard
memcpy contract and is not claimed to match a particular PS4 implementation.

The return-slot span is validated before writes. Its value is read **after**
the bulk operation, so overwriting the saved return slot affects RET correctly.
Invalid spans/stack or cancellation before the operation leave bytes and
return State untouched. Cancellation arriving after a committed bulk operation
can stop before RET: the bytes remain committed, while return State/counters
remain untouched. Cancellation is cooperative, not a rollback transaction.

`dcp_libc_bulk_stats()` returns four atomic fields: `copy_calls`, `move_calls`,
`fill_calls`, and `bytes`. They count completed guest returns, including
zero-length calls. A concurrent snapshot is diagnostic and is not a transaction
across all fields; final totals after worker join are exact. Call
`dcp_libc_bulk_reset()` only at a quiescent session boundary.

## Builder integration and comparison

The builder verifies the three export names, executable ranges and exact
addresses against the pinned libc input, then generates signed wrappers. Their
provenance says `native-libc-bulk`, records helper/generator hashes and includes
the original module image hash. Original translated entry IR is retained under
`translated-libc-bulk/` in the private build tree. Other routines remain AOT.

`build.py`, `advance.py` and `bootstrap.py` accept `--translated-libc-bulk` to
restore and execute original entry IR for comparisons. A later default build
restores the native wrappers. Runtime reports expose the compiled mode and
completed call/byte counters. Native byte operations preserve the existing
memory synchronization and range checks.

On one host startup pair at 3,383 entrypoints, both modes completed the actual
64-case strlen proof, returned zero from libc startup, reached one vblank and
stopped at the same unsupported VROUNDSS (`20639719`). Native mode reported
15,129 copy, 126 move and 4,786 fill returns over 1,432,320,205 bytes, taking
2.57 seconds; translated mode took 7.32 seconds. These are single startup
observations with concurrent worker scheduling, not sustained gameplay or frame
performance. No flip or actual game frame had occurred.

## Independent ABI and byte verification

```sh
python3 ios/game/libc-bulk-test.py \
  --output /Volumes/office/Build/OpenCrown/libc-bulk-probe \
  --backend-root /Volumes/office/Build/OpenCrown/cpu-backends
```

The runner imports the **production** `native_libc_ir.py` generator. Each actual
opaque-pointer LLVM wrapper calls `dcp_trace_enter(state, program_counter)`,
then `dcp_libc_bulk(state, immediateKind, memory)` and returns its Memory pointer.
The native test enters through these compiled wrappers, including failing calls
that must unwind through them. It does not replace the wrappers with C++ calls.

The adapter and wrappers cross compiled for arm64 macOS, iPhoneOS and Simulator.
The identical host suite passed **822,659 checks with zero failures** under
native execution, AddressSanitizer + UndefinedBehaviorSanitizer, and
ThreadSanitizer, with no sanitizer diagnostics. It verified 94,980 copy returns,
94,980 move returns, 15,683 fill returns and 690,888,880 bytes in each run.

Coverage includes every source/destination alignment modulo 64, size edges,
both overlap directions, a 16 MiB copy, complete output/guard comparison,
low-byte fill values, exact preservation of other State bytes, correct RAX and
checked RET behavior, return-slot overlap, zero/null/unmapped arguments,
source/destination/return-stack bounds, invalid kinds, cancellation before and
after commit, per-session totals/reset, and eight independent threaded contexts.
Expected copy bytes come from a separate source snapshot, rather than executing
another copy into the same overlapping buffer.

`results.json` records exact source/generator hashes and each sanitizer's
stdout/stderr; the output directory also contains the generated IR, three SDK
archives and complete compiler logs. This is a byte-semantics and ABI proof;
it is not an actual PS4 numerical oracle or physical-device execution result.
