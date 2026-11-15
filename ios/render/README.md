# In-process mobile Metal replay

This target reuses the existing SDL 3 GPU Device and captured-frame Backend on
arm64 Mac, iPhoneOS and the arm64 iOS Simulator. The eight game shader inputs are
compiled into SDK-specific embedded metallibs. The iPhoneOS and simulator builds
use an explicit iOS 18.0 deployment minimum for both code and Metal libraries.
Physical-device rendering remains to verify.

## Build

Use the existing pinned SDL 3.4.18 and FreeType caches, with a mounted external
build root. The renderer itself contains no game bytes or runtime compiler.

```sh
cmake -G Ninja -S ios/render -B /Volumes/office/Build/OpenCrown/ios-render/mac -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DDCP_SDL_SOURCE=/Volumes/office/Build/OpenCrown/macos-rendered/_deps/sdl3-src -DDCP_FREETYPE_SOURCE=/Volumes/office/Build/OpenCrown/macos-rendered/_deps/freetype-src
cmake --build /Volumes/office/Build/OpenCrown/ios-render/mac --parallel 8
```

For a separate device build, use the same source/cache arguments plus
`-DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos
-DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=18.0
-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO` and the `iphoneos` output
folder. For Simulator replace the SDK with `iphonesimulator` and use the
`simulator` output folder. Keep the three build directories separate.

Each platform produces:

- `libopencrown-mobile-replay.a`, containing replay pump, Backend, capture,
  prompts, PNG and format/detile code.
- `libopencrown-mobile-metal.a`, containing the Metal device and BC fallback.
- `sdl/libSDL3.a` and `freetype/libfreetype.a`, required static dependencies.
- `ThirdPartyNotices/`, containing SDL, FreeType FTL and bcdec license texts.

Link the CMake `opencrown-mobile-replay` target to receive transitive SDL and
Apple framework dependencies. A manual app build must link all four archives
and SDL's platform frameworks. Supply `dcp::hle::vblank_count_public()` from
the runtime. Avoid linking another direct capture.cpp object when using the
replay archive. Compile capture/Backend callers with `DCP_AOT` and C++20.

## Connect the translated game

Initialize capture on the fully loaded, hash-verified eboot image before starting
workers:

```cpp
dcp::render::initialize_aot_capture(eboot, image_size,
    {context, checked_memory, copy_guest_memory});
```

`checked_memory(context,address,bytes,writing)` must validate the complete range
against the shared guest-memory registry. `copy_guest_memory(context,address,
destination,bytes)` must copy the complete span while holding the same memory
synchronization lock as translated reads/writes. Both callbacks must outlive the
capture session. Capture releases its state locks before entering those callbacks
to avoid inversion with guest atomic regions. Omitting the copy callback is only
for immutable or single-threaded synthetic fixtures, which use validated memcpy.
The live runtime must supply it. AOT capture does not use page-fault
tracking or executable x86 relay hooks. Before each original wrapper entry,
call `observe_aot_entry(eboot_offset, sysv_integer_args)` from trace entry
instrumentation. It recognizes the same twenty shader/resource/state/Execute
addresses used by the Mac capture hooks. Original guest instructions continue.

The GNM adapter must call `on_draw` after writing a draw packet NOP, call
`on_marker` for submitted marker NOP payloads, and call `on_flip` after a flip.
VideoOut buffer registration calls `set_display_buffers`. Capture copies vertex,
index and constant bytes into DrawRec objects and queues frames in submit order.
The fairy-cursor observer is excluded from AOT until its guest vfunc callbacks
can be dispatched through the translated execution engine.

On the SDL render thread, after initializing SDL video and creating its window
(or supplying nullptr for offscreen work):

```cpp
dcp::mobile::Replay replay;
replay.initialize(sdl_window_or_null, true, &error);
dcp::mobile::ReplayFrame frame;
auto result = replay.pump(0, &frame, &error);
if (result == dcp::mobile::PumpResult::Frame) {
    replay.write_png(output_path, &error); // optional offscreen/readback evidence
    replay.present(drawable_width, drawable_height); // windowed consumer
}
```

Empty and Error are separate pump outcomes. Frame metadata includes vblank, flip,
draw count and target dimensions. The caller owns the SDL window and must keep
it alive until replay destruction. The device owns its SDL GPU and nested replay
resources.

Before backgrounding, stop frame producers, call `replay.suspend(true)` to submit
pending work and wait for GPU idle, then stop the pump. Resume with
`replay.suspend(false)`. Texture objects survive. Calls that would produce GPU work
are disabled while suspended. These methods require one render-thread owner.
Stop/join workers and stop the consumer before `shutdown_aot_capture()` or guest
image reset. Texture descriptors retain live guest addresses, so keep allocations
alive while queued frames are consumed.

## Texture correctness

All draws using a texture in one frame share its first synchronized snapshot.
On a later frame, the renderer checks the complete guest span against the
bytes used for the upload. It retains up to 192 MiB of these snapshots and
prefers active textures. A changed or uncached span goes through synchronized
copy, SHA-256 and detiling. A write to the last byte participates in the check.
The renderer submits a new upload only when the content changes. Guest code may
write again immediately after the comparison or copy. Those writes become
visible at the next frame's lookup.

### Texture diagnostics

Live/final iOS reports and the native runner's final report expose a `textures`
object from `Replay::texture_stats()`. Counters belong to one renderer session;
the frontend retains the last values before teardown. They are updated only on
the render thread. The classic non-AOT backend returns unavailable/zero values.

`requests` includes every texture lookup, including render targets and invalid
entries. `same_frame_hits` counts retained textures already checked for that
frame. `comparisons` and `comparison_span_bytes` count complete spans requested
from the snapshot comparator; a failed comparison may stop early or reject an
invalid range. `comparison_hits` and `matched_span_bytes` count successful full
matches. These are logical byte counts, not measurements of DRAM traffic.

`copies`, `copied_bytes` and `hashed_bytes` count successful synchronized copies
and subsequent SHA input. `uploads` counts successful backend upload calls.
`budget_evictions` counts snapshots removed for retention space;
`retention_misses` counts snapshots the budget could not retain. Current/peak
snapshot bytes use vector capacity, matching the existing 192 MiB limit.

For session averages, use native `consumed_frames` or iOS
`captured_game_frames`. Texture totals cover startup too; do not divide them by
the separate settled-scene frame count. Deltas from live reports can isolate a
steady interval. The new accessors require rebuilding the backend/replay archives
and frontend together; an old archive fails linkage. Shader binaries and the
translated game corpus do not change.

When the SDL Metal device cannot sample BC1, BC2, BC3 or BC7, the mobile wrapper
decodes blocks to RGBA8 before uploading. Partial edge blocks and unaligned input
are handled. A forced-fallback option exercises that branch on a GPU that supports
BC formats. `Replay::decoded_uploads()` reports the number of decoded uploads.
Decoded storage costs four bytes per pixel, and the upload queue copies it into
its own staging buffer before the temporary CPU buffer is released.

The unmodified bcdec v0.985 header is vendored from revision
`80859ed3b7afb1c527a2a99d70c61457bea72d0c`; preserve its license alongside the
SDL/FreeType notices when packaging. [bcdec source](https://github.com/iOrange/bcdec/tree/80859ed3b7afb1c527a2a99d70c61457bea72d0c)

## Verification on 2026-11-13

```sh
python3 ios/render/run-fixtures.py --build /Volumes/office/Build/OpenCrown/ios-render/mac --output /Volumes/office/Build/OpenCrown/ios-render/verified
```

On the M5 Max/macOS 27.2.1, with Metal API validation enabled:

- The lower Device fixture passes 49,152 RGBA channel checks for blending,
  top-origin scissor, two-texture mask sampling, adjacent translucent draws, incomplete primitives and suspend/resume. It renders 120
  offscreen 64×64 frames and reads back the result.
- The AOT capture fixture passes wrapper state, shader-pool lookup, copied
  vertices, submitted marker ordering, display/flip metadata, invalid-memory
  rejection and reinitialization.
- The capture-to-Metal fixture produces a PNG through wrapper observation,
  DrawRec, marker, Frame queue, Backend decoder and actual Metal rendering.
- The texture-cache fixture changes byte 4096 between two draws in one frame.
  Both draws use that frame's first synchronized snapshot; the next frame detects
  the mutation and reuploads, and a third unchanged frame reuses the upload.
- A concurrent-writer fixture runs 200 frames with 128 repeated atlas draws per
  frame and exactly 200 texture snapshots. Its guest
  texture address deliberately has no native mapping, and its copy callback
  changes the source before returning. Every uploaded byte must match the owned
  snapshot. Native and ThreadSanitizer runs pass with no race reports; the latter
  checks 1,622,016 uploaded bytes in 99 uploads (counts vary with writer timing).
- Native and forced CPU BC1/2/3/7 paths pass 1,504 channel/partial-edge checks.
  The forced branch reports four decoded uploads; the native path reports zero.
- The Python verifier checks all 36,864 saved RGB channels and stores PNG hashes.

All fixtures return failure on an unmet check. Assertion-based fixtures retain
assertions in Release builds. `verification.json`, PNGs and logs stay in the
external output directory. Device and Simulator replay archives compile and
carry the correct IOS/IOSSIMULATOR Mach-O build platforms (minimum 18.0, SDK 27.2).
No physical device was used for this rendering work.

The existing x86-64 Mac dcp_run, gpu and texwatch targets rebuild with the shared
source edits. Existing gpu and texwatch CTests both pass. Their logs are
`/Volumes/office/Build/OpenCrown/ios-render-mac-regression-{build,tests}.log`.
Live game draw coverage, simulator/device presentation, display changes and
sustained performance remain integration work.


### UI atlas performance

AOT replay now checks each texture once per replayed frame. All repeated glyph
and sprite draws share the first owned synchronized snapshot; guest writes after
that copy become visible on the next frame. There is no unsynchronized detile or
partial-content shortcut. On a cache miss, CommonCrypto SHA-256 covers every byte
of the newly acquired snapshot. Descriptor
keys and the desktop write-protection path retain their existing behavior.

This removes the per-glyph full-atlas copy/hash that stalled the Create save data
prompt. The 128-draw concurrent fixture passes native and ThreadSanitizer, and
the renderer verifier still checks all36,864 saved RGB channels plus1,504 BC
channels/edges. Updated Mac, Simulator and iPhoneOS archives compile. Real-game
performance measurements are recorded in the progress ledger; fixture timings
alone do not establish gameplay frame rate.

The [texture writer audit](texture-coherence.md) records the measured remaining
comparison work and the native pointer writers that must be covered before
introducing a version-only cache hit.
