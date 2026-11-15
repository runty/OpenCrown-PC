# Owned texture snapshot versions

The experimental AOT texture path is enabled with `DCP_TEXTURE_VERSIONS=1`.
The default remains the full-content path until device qualification. Set
`DCP_TEXTURE_VERSION_VERIFY=1` as well to compare every otherwise valid version
hit against the actual bytes. Verification mismatches increment a failure
counter and make the final game report fail; verified runs are correctness
audits, not performance samples.

`GuestSnapshot` owns immutable copied bytes and the proof for those exact
bytes. `matches` is a single-consumer operation; it never memoizes a result for
an unrelated host buffer. It validates the registry identity and resolved host
address before considering a proof. Unknown or changed proofs fall back to the
complete byte comparison. If bytes still match, the proof can be refreshed
under the same locks without recopying or rehashing them.

Only explicitly opted-in mappings can produce a version-only hit. The game
opts its committed DirectMemory allocations in with `addSnapshotRegion`.
Ordinary `addRegion` mappings, including native-owned font handles, thread
objects, module data and flexible allocations, remain untracked. An unknown
overlapping registration also excludes version reuse. Registry admission is
checked against the actual append-only entries, not just an address interval.
Use the registration APIs; backing storage remains alive until joined unload
and renderer shutdown, as required by the checked memory layer already.

All aliases use versions indexed by the resolved host page. Scalar/floating
stores, f80, successful CAS including 128-bit CAS, bulk write/move/fill and glyph
max publication invalidate destination pages under their existing locks.
Failed CAS does not invalidate. No second global lock is added to these paths.
An address bound and negative filter avoid map lookups for ordinary writes
outside watched pages. Metadata uses the same stripe order as guest access;
original guest LOCK still excludes snapshot copy/check and checked writes.

`guestPointer` marks every escaped page before returning the pointer, including
escapes before any snapshot is captured. Those pages cannot regain a version
proof during that session, because native code may retain the pointer. Range
validation uses `guestValidate` instead. Capture's read-only validator does not
escape a pointer; its native marker-write validation does. File/font/IME output
already uses checked publication. This allows conservative content fallback for
remaining service writers without assuming they have all been converted.

Copying the bytes, recording versions and checking/refreshing a proof happen
under the locks for the entire span. Registration invalidates previous proofs
even when an address and size are reused. A saturated page counter permanently
falls back instead of wrapping. Quiescent `configureGuestSnapshots` clears
session metadata and changes generation; generation zero is terminal after
overflow. It cannot silently revive an old proof. Old immutable bytes remain
owned by their snapshot, independent of metadata lifetime.

Configure before the new session's registrations. Existing mappings do not
become opted-in merely by enabling tracking.

The capture interface carries the owned object through `AotSnapshot`; hashing,
detiling and uploading use its immutable bytes. Snapshot budget accounting
includes both byte and proof-vector capacity. Metadata stays until the next
quiescent session reset. The first synchronized snapshot per replayed frame
policy remains unchanged. Content-only fixtures retain an owned-copy fallback.
The capture hook and texture stats interfaces changed: rebuild native capture,
backend/replay, frontend and all AOT fixture callers together. Do not mix old
archives or objects with these headers.

Reports separate `textures.version_hits` / `version_matched_bytes` from actual
content-comparison attempts. The guest's `texture_versions` reports the enabled
and verification modes, version-hit count, verified checks/bytes and proof
failures. These are logical spans, not measured DRAM traffic. In audit mode,
`verified_bytes` records additional full comparisons even though the renderer
classifies the successful proof as a version hit.

```sh
python3 ios/cpu/review/snapshots.py \
  --output /Volumes/office/Build/OpenCrown/snapshot-review \
  --backend-root /Volumes/office/Build/OpenCrown/cpu-backends
```

Native, ASan/UBSan and TSan cases cover all writer paths, last-byte/cross-page
changes, aliases, overlapping move, failed CAS, escaped pointers before/after
capture, native-owned overlaps, registry replacement, registration reuse,
session reset, original LOCK exclusion and concurrent publication/capture.
The concurrent check count varies with scheduling. A separate seven-count
limit forces page-counter overflow. A private source copy deliberately omits
scalar invalidation and must fail the functional test; it never enters a game.
The verification-mode test also deliberately violates writer admission and
requires the full-content audit to detect it. Renderer fixtures and real-game
audits/performance evidence are recorded in the progress ledger separately.
