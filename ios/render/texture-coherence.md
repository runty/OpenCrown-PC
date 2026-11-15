# Texture comparison optimization: writer audit

The `ee59b0c` instrumentation preserves the existing full-content comparison,
snapshot, hash and upload behavior. It does not implement a version cache.
The first native Town run compares 187,525,429,248 logical bytes across 2,825
consumed frames. Of 44,075 comparisons, 44,074 match; zero snapshot retention
misses are counted. These are whole-session counts including startup, not
settled-Town-only counters, measured DRAM traffic, or a phone speedup. The
existing 192 MiB snapshot budget is not the main source of repeated work in
this run.

## Writers that a version cache must cover

| Path | Current behavior | Requirement before a version-only hit |
| --- | --- | --- |
| Translated scalar/vector/f80 stores and CAS in `ios/cpu/runtime.cpp` | Checked pointers and access locks; CAS writes only on success | Invalidate every destination page while holding the same locks as the store, including cross-page and alias accesses. |
| `guestWrite`, `guestMove`, `guestFill` | Bulk writes hold access locks; move can overlap | Invalidate the full destination span, including all pages and overlapping moves. Retain original guest LOCK exclusion. |
| Native libc bulk adapters | Delegate to the bulk memory operations | Keep invalidation in the common operation, rather than relying on each adapter. |
| Native CRILAYLA in `ios/game/crilayla-native.cpp` | Uses non-escaping `guestValidate`, decodes privately, publishes with `guestWrite` | Publication must invalidate the output when versions are added. Include translated fallback stores. |
| File service in `ios/game/file-services.cpp` | The game supplies checked copy callbacks: bounded host staging, `guestWrite` publication, checked input/string copies and stat outputs | Add version invalidation to the common memory writes. Legacy pointer-only callers must remain conservatively untracked. |
| Font services in `ios/game/font-services.cpp` | Guest input/output uses checked copies; original rasterization is private and clipped coverage rows use checked max publication | Invalidate checked output pages when versions are added. Native-owned opaque Font/token mappings still require exclusion or mutation/lifetime tracking. |
| IME services | Checked copies publish accepted text/results; host UI never accesses guest memory | Invalidate through common checked writes. Pointer-only callers remain untracked. |
| Offline services | Several service results still use borrowed pointers | Track complete output spans or conservatively exclude their escaped mappings from version-only hits. |
| Thread/service hooks and direct copies in `ios/game/runtime.cpp` | Several `memcpy`/`memset`/field writes use `guestPointer` | Audit each mutable escape, including initialization and worker paths. Validation alone cannot establish future immutability. |
| Capture/marker callbacks | Capture has separate validation, copy and equality hooks; its memory hook accepts a `writing` flag | Honor any actual writes or keep them conservatively untracked. Read validation must not poison otherwise trackable regions. |

`guestPointer` releases its access lock before returning the borrowed pointer.
The legacy pointer callback available to font/IME/offline services in
`ios/game/runtime.cpp` still ignores its `writing` argument; the capture
validation callback does too. The file adapter now uses separate checked copy
callbacks and does not escape guest pointers on the production path. Font and
IME guest input/output now use checked publication too; native-owned handle
ranges remain a separate concern. See [file memory](../game/file-memory.md)
and [font/IME publication](../game/font-memory.md).
Consequently, adding epochs only to translated stores would be incomplete.
The pointer flag by itself is also insufficient: a renderer could read a new
epoch before the native function actually writes its output.

## Conservative implementation sequence

1. Introduce a distinction between checked range validation, scoped native
   access and escaped mutable pointers. Unknown escapes must disable a
   version-only hit for the affected mapping. Preserve the existing content
   comparison path whenever tracking cannot prove that the snapshot is current.
2. Native file, font and IME guest output now publish through checked memory
   operations, with private font rasterization. Route remaining service copies
   through the same protocol, or mark escaped/native-owned ranges conservatively
   untracked. Do not assume a pointer's declared read-only use
   prevents a callee from writing it; audit the actual implementation.
3. Associate page versions with the underlying host mapping, not just a guest
   address. Synthetic/native aliases must share identity. Registration, session
   reset, free/reuse and version overflow must invalidate prior tokens. Version
   metadata must not outlive its registry owner.
4. Capture the bytes and their version token in one synchronized operation.
   Keep the token attached to that exact immutable snapshot. A successful
   full comparison may refresh that snapshot's token under the same locks.
   An unrelated pointer/length result must never establish a cache hit.
5. Validate all covered pages and the mapping identity while excluding writes,
   then reuse the existing GPU texture. Writers must publish invalidation before
   releasing the access lock; native write lifetimes must exclude this check.
   If any proof is missing, use the full-content path. Retain the existing
   first-snapshot-per-replayed-frame policy.

Measure metadata lookup/update cost before enabling the fast path. Scalar
memory locks are already a major sampled cost; a second global lock or page
table lookup on every store could erase the comparison savings. Keep the
unaccepted lock-region experiments separate so an improvement has one cause.

## Acceptance evidence

Extend the current texture snapshot and memory fixtures with last-byte writes,
cross-page writes, synthetic/native aliases, overlapping moves, successful and
failed CAS, f80/vector writes, raw native output, font surface updates, mapping
reuse, session reset, counter overflow and teardown. Force races between
snapshot/token capture and every writer class. Include a deliberately omitted
writer in a functional negative control; sanitizer success alone does not prove
the version protocol is complete. Compare uploads against the full-content
reference and retain current exact rendering/BC fixture checks.

Then compare interleaved native runs using the same frozen core, renderer,
save and input. Report comparison bytes, fallback rate, CPU time and frame
intervals separately. Phone qualification requires its own silent runs with
thermal/low-power state and real audio accounting, followed by sustained
Town/combat acceptance. Neither native counters nor short idle samples close
that gate. No normal player save or live cloud progress is needed.
