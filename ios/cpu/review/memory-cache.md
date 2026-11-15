# Independent mapped-memory cache review

Each host thread keeps eight native region values and a strong owner for the
registry it last used. Registrations append under a mutex. A worker resolves
cache hits locally. Registry replacement requires quiescence, and a change of
registry identity clears the thread's cached entries before reuse. Synthetic
addresses are translated separately.

Checked data access locks the stripes covering the actual host bytes. A full 4 KiB
page can be cached once its entire range is proven to belong to the registry.
Lock bands span 256 KiB. A cross-band access takes its participating stripes
in ascending order. A multi-megabyte comparison can run concurrently with a
guest access whose stripes are disjoint.
Copies protect both spans. An original guest LOCK takes all stripes until
its matching release. Same-thread nested guest contexts retain the enclosing
lock. Mapping lookup and guest code that waits for lifecycle changes run
outside ordinary data locks.

The review identified registration/null/zero-length boundaries that the runtime
owner corrected before final testing:

- Reject null, absent-registry and overflowing region registrations; ignore
  malformed native registry entries during lookup.
- Reject native lookup through an absent registry, while retaining valid
  synthetic memory access.
- Validate zero-length spans, then skip `memcpy` so nullable native buffers do
  not violate its nonnull contract. Empty synthetic storage cannot authorize a
  null-plus-zero pointer expression.

Future in-place unmap, region resize/replacement or concurrent registry-pointer
replacement requires explicit invalidation/versioning and synchronization.
Identity checks cannot protect an unsynchronized external write to the same
`shared_ptr`. A returned `guestPointer` remains borrowed; its backing must stay
alive. The cache does not establish backing ownership or validate an allocator's
true allocation size. The current runtime retains mappings until workers join.

## Stress and sanitizers

```sh
python3 ios/cpu/review/memory-cache.py \
  --output /Volumes/office/Build/OpenCrown/memory-cache-review \
  --backend-root /Volumes/office/Build/OpenCrown/cpu-backends
```

The same fixture passed **267,095 checks** in each configuration:

| Configuration | Exit | Failures | Sanitizer diagnostics |
| --- | --- | --- | --- |
| Native arm64 | 0 | 0 | n/a |
| AddressSanitizer + UndefinedBehaviorSanitizer | 0 | 0 | none |
| ThreadSanitizer | 0 | 0 | none |

Coverage includes hundreds of disjoint mappings and cache churn; unaligned and
end-boundary reads/writes; zero-length and null buffers; huge address/size and
registration overflow; 512 quiescent registry replacements followed by old
backing deallocation; all scalar and exact ten-byte FP80 bridges; eight workers
sharing a registry while another worker appends mappings; simultaneous CAS32
and CAS64 counters with exact final totals; valid shared-context cache access;
fault recording; cancellation callbacks/flags and atomic-region cleanup.
The blocking-callback regression parks a scalar-read cancellation callback,
then requires another thread's checked read to finish before releasing it.
This verifies a paused guest cannot hold the registry lock while waiting.
The test always releases and joins its workers after bounded waits, so the old
lock ordering reports failure instead of hanging the sanitizer process.
The negative control compiled this same callback subset against committed
`cab3d2b` with `MEMORY_CACHE_BLOCKING_ONLY`: it failed the expected lock-progress
assertion after the bounded wait, then released/joined both workers normally.

Reports and complete build/stdout/stderr logs are in the selected external
output. `results.json` records the reviewed runtime/header/fixture hashes.
These tests do not authorize concurrent access to thread-owned fault strings,
dispatch counters or an atomic-region lock through a shared `Memory`; the
production runtime supplies a distinct execution context per guest worker.

## Controlled lookup benchmark

```sh
python3 ios/cpu/review/memory-cache-benchmark.py \
  --output /Volumes/office/Build/OpenCrown/memory-cache-benchmark \
  --backend-root /Volumes/office/Build/OpenCrown/cpu-backends \
  --baseline cab3d2b --runs 3
```

This compiles an isolated copy of the committed baseline and the current
runtime against the identical workload, alternating their run order. It checks
every phase's checksum and records per-thread CPU time to reduce interference
from other concurrent build work. No game archive or translated trace is used.

With 1,536 mappings, the development machine produced these three-run medians:

| Workload | Baseline CPU | Cached CPU | Result |
| --- | --- | --- | --- |
| 1M reads cycling eight mappings at the registry tail | 519.54 ms | 10.69 ms | 48.6× faster |
| 1M pointer lookups cycling the same eight mappings | 468.15 ms | 10.91 ms | 42.9× faster |
| 300K reads churning across all 1,536 mappings | 77.66 ms | 123.39 ms | 59% slower |

The hot workload deliberately exercises the cache's intended repeated-mapping
case and an expensive baseline scan position. The thrashing workload measures
the added cache/validation cost when the working set does not fit. This is a
measured tradeoff, not a universal speedup or a game-frame performance claim.
Changed startup worker scheduling or a different first missing trace makes two
real-game startup timings incomparable; those timings are not evidence here.

The 2026-11-14 narrow-atomic extension adds the observed libc CAS8 bridge and
its CAS16 counterpart through the same checked, serialized template. The latest
fixture passes 251,067 checks natively, under ASan/UBSan and under TSAN, including
misaligned byte/word success/failure, expected-value updates, adjacent canaries,
end-of-range rejection and cancellation. Eight native workers also produce exact
no-wrap totals of 160 byte increments and 32,000 word increments. Real Remill
LOCK CMPXCHG byte/word retry routines bring the CPU corpus to 84 routines and
1,884 checks; 83 use a native Rosetta oracle. These new bridges also compile for
both mobile SDKs. A speculative libc routine first exposed the missing link
symbol; that discovery does not establish that the game executed that libc path.
