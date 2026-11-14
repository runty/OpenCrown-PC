# Independent mapped-memory cache review

The eight-entry per-`Memory` cache is safe under the runtime's current ownership
contract: registries only append; their vector entries/backing ranges are not
removed or changed while guest workers run; a `Memory` receives a replacement
registry only while quiescent. Its cached entries are **Region values**, so
vector reallocation does not invalidate them. A strong cached registry owner
prevents allocator-address reuse from defeating identity invalidation.

All cache lookup, update and registry traversal is serialized by `guestAtomics`.
The internal `checkedPointerLocked` does not acquire it again; public
`guestPointer` still acquires it. Bounds use subtraction after checking sizes
and lower bounds, avoiding end-address overflow. Synthetic bytes remain a
separate fast path and are never cached as native ranges.

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

The same fixture passed **251,052 checks** in each configuration:

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
