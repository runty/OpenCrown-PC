# Checked font and IME publication

The game supplies complete memory-copy callbacks to both adapters. Font input,
handles, layouts, surface descriptors and glyph results use checked guest reads
or writes. FreeType owns its font data. For glyph rendering, the original
renderer rasterizes with a private descriptor whose coverage pointer is null;
the adapter then publishes the resulting bitmap through checked max-coverage
rows. Clipping, pitch padding, existing higher coverage values and the original
metrics/result layout are preserved. This avoids copying an entire surface
for each glyph.

`guestMaxBytes` validates its destination and performs `max(old, input)` under
the usual access locks. Input must be private immutable host memory. Original
guest LOCK excludes each row publication. The font adapter mutex keeps the
face/bitmap alive and excludes other font calls/reset. Synchronization is per
row, not an atomic transaction over a whole glyph and all results. A rejected
callback throws rather than reporting a successful glyph.

The font callback group is validate/read/write/max; the IME group is
validate/read/write. Partial groups are rejected. Pointer-only users retain
the legacy path and must remain conservatively untracked by a future version
cache. Callers/adapters must be rebuilt together after these C++ hook changes.
No translated program routines or desktop font implementation are changed.

Final reports retain `font_memory` configuration and successful logical
read/write/blend byte counts plus glyph calls before reset. Ordinary Town uses
its baked atlas: it executes checked font setup, but records zero dynamic
glyph calls. Dynamic glyph evidence comes from the fixture, not that Town image
or a claimed on-device rendering pass.

Native-owned opaque Font/token objects remain registered until joined unload
and are still mutated natively. A future cache must exclude these ranges or
explicitly track their lifetime/mutations. Other service escapes remain in the
[writer audit](../render/texture-coherence.md). Full-content texture comparisons
remain enabled; this change does not establish a frame-rate gain.

```sh
python3 ios/game/test-font-memory.py \
  --output /Volumes/office/Build/OpenCrown/font-memory-review \
  --backend-root /Volumes/office/Build/OpenCrown/cpu-backends \
  --font-source /Volumes/office/Build/OpenCrown/macos-rendered/_deps/freetype-src \
  --font-build /Volumes/office/Build/OpenCrown/font-probe \
  --font /System/Library/Fonts/Helvetica.ttc
```

Supply a font containing the Latin and Greek fixture glyphs. The fixture opens
an owned copy and compares the checked path with the original renderer across
ten cases: fractional/negative coordinates, clipped/offscreen glyphs, missing
glyph, unsupported bpp, null buffer and zero-width clip. It checks exact
surface/metrics/result bytes, guards, pitch padding, failures, configuration
and original LOCK exclusion. A deliberately unlocked fixture blend must fail
the functional assertion and never enters a game. Native, ASan/UBSan and TSan
runs instrument the adapters and memory runtime; they link the existing
FreeType archive, not an instrumented FreeType rebuild. Both SDKs also compile.
`ios/game/test-ime.sh` covers legacy/checked IME behavior and failed-publication
retry. Revision-specific real-game UI acceptance remains in the progress ledger.
