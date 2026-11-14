# Real-module AOT startup harness

This private build path lifts executed x86-64 routines from the supported
CUSA10454 patch 1.03 dump into signed arm64 code. It does not execute writable
memory, use Rosetta on iOS, or generate code on the device. Generated module
images, relocations, LLVM IR, object archives and game data remain outside this
repository. The builder rejects a different eboot or libc SHA-256.

Use the [bootstrap workflow](../BOOTSTRAP.md) to build the pinned Remill
backend with all sixteen local patches, SoftFloat helpers/FP80 lowerer, FreeType
and platform dependencies. Rebuild both `remill-lift-23` and `amd64_avx` after
semantics changes and relift the complete corpus. Cache roots are configurable
with builder options and their corresponding `DCP_*` variables. Exact extended
precision lowering and its limits are described in [FP80 support](../cpu/fp80/README.md).

```sh
python3 ios/game/advance.py --root /Volumes/office/Build/OpenCrown/ios-game \
  --dump /Volumes/phil/Games/PS4/DragonsCrownPro/CUSA10454 --rounds 1024
python3 ios/game/build.py --root /Volumes/office/Build/OpenCrown/ios-game \
  --dump /Volumes/phil/Games/PS4/DragonsCrownPro/CUSA10454 --ios --simulator
./ios/build-probe.sh /Volumes/office/Build/OpenCrown/ios-probe TEAM_ID \
  /Volumes/office/Build/OpenCrown/ios-game
DCP_BUILD_PLATFORM=simulator ./ios/build-probe.sh \
  /Volumes/office/Build/OpenCrown/ios-simulator TEAM_ID \
  /Volumes/office/Build/OpenCrown/ios-game
```

Run one builder per output tree. `advance.py` builds and runs the native arm64
host harness, then lifts the first unresolved executed target and repeats.
Prefetch fills the configured batch with nearby direct calls and possible
vtable/callback targets from bounds-checked relocations. Speculative pointers
must be 16-byte aligned; executed unaligned demands remain valid. Executable
segments also contain constants, so prefetch does not establish reachability.
Privileged I/O port instructions fail explicitly if executed.
Other errors stop discovery for investigation. It uses a fresh disposable save
directory for each attempt. Logs and the latest structured `boot-result.json`
are in that output tree. The default per-attempt timeout is 120 seconds;
`--timeout` changes it. Missing targets are reported using canonical module
addresses: eboot starts at `0x20000000`, libc at `0x30000000`.

The UIKit shell reads assets from `Documents/Game` and writes disposable local
saves to `Documents/ProbeSaves`. It runs the guest on a background queue and
exports `Documents/diagnostics.json`. A simulator may override roots through
`SIMCTL_CHILD_DCP_GAME_ROOT` and `SIMCTL_CHILD_DCP_SAVE_ROOT`; copying assets into
its sandbox avoids external-volume permission prompts. Physical devices always
use their sandbox. Test progress remains separate from the player's Mac and
iCloud saves.

Real libc `strlen` is checked against 64 input cases before libc startup and
eboot entry execute. The runtime shares registered native memory across guest
workers, with separate CPU states, guest stacks and TLS. Typed HLE adapters cover
files, savedata, threads, synchronization, fonts, scripted pad input, paced null
audio and headless GNM labels. Unknown executed imports, missing traces, bad
memory ranges and unsupported instructions fail explicitly.

The current headless boot checkpoint requires the checked `TITLE` component active-update
phase followed by 120 advancing vblanks and flips. A returned startup report or
`game_code_executed=true` alone does not mean the game has booted. Full mobile
acceptance additionally requires INN/town, visible frames, real controls/audio,
lifecycle and save/reload on both physical devices, as defined in the port plan.

The three pinned bulk libc exports use [checked native adapters](libc-bulk.md)
by default. Builder provenance marks these replacements explicitly; actual
strlen and libc startup remain translated. `--translated-libc-bulk` on the
builder, discovery or bootstrap restores original entry IR for comparisons.

The SSE reciprocal implementation returns full-precision reciprocals within the
x86 estimate's permitted error bound, with explicit signed-zero, infinity, NaN
and denormal behavior. Tests compare native reciprocal estimates within that
bound; integer and bit-permutation fixtures compare exactly. This does not imply
complete x86 instruction, floating-point exception or MXCSR coverage.

Native [UIKit name entry](ime.md) is connected to the checked IME adapter.
`--jobs 8` bounds independent trace lifters and compilers; archive inputs use an LLVM
response file so large trace sets do not exceed macOS's process argument limit.
Objects are published only after successful compilation and archived in stable
trace order. Run a single builder per output tree.

Successful lifted traces publish entry instrumentation and matching provenance
individually before aggregate compilation. A failed batch leaves completed peer
traces reusable, while invalid entry points remain unpublished. Address spellings
are normalized before parallel scheduling. Re-running a failed batch remains
explicit; unsupported instructions are never replaced with successful stubs.

Headless runs can select an [INN checkpoint and desktop-format input script](headless.md).
Reports retain the requested scene, observation flag and exact script SHA-256.
The default remains TITLE plus 120 advancing flips/vblanks; interactive mobile
play ignores headless overrides.


## DirectMemory address layout

The original resource converter stores an unsigned 32-bit difference between a
name slot and its resource header; the consumer zero-extends it before adding
the header address. Original initialization maps the header arena before its
two data arenas. Unrestricted independent native mappings do not preserve that
order. The mobile DirectMemory service now suballocates increasing addresses
from one reserved span, keeping these differences representable without
changing guest pointers or weakening mapped-range checks.

The preferred span is 4 GiB, with a 3 GiB reservation attempt if that fails.
Only allocated pages become read/write and only those ranges enter the guest
registry. No existing mapping is replaced and no unbounded address-hint search
is used. Exhaustion fails explicitly. The original initializer alone requires
about 1.4–1.6 GiB; a native Town run uses 2.04 GiB across 57 mappings. Lifetime
usage through combat still needs qualification; these are not lifetime bounds.
All backing storage remains owned until workers stop and renderer unload is
acknowledged.

```sh
./ios/game/test-direct-memory.sh /Volumes/office/Build/OpenCrown/direct-memory-verification
```

The fixture checks the original offset representation (including the extra
4 GiB produced by reversed real regions), concurrent nonoverlapping allocations,
alignment, invalid/exhausted requests and preserved contents. Native and
ThreadSanitizer runs pass 563 checks; all three SDK targets compile. Actual
runtime lifecycle and bounded headless checks are separate. Failure-only
`fault_context` now records checked resource operands for the previously
observed string-pointer fault. That old intermittent failure has not recurred
in the latest same-core comparison, so its exact causal operands remain open.


### Adventure coverage diagnostics

`DCP_CHECKPOINT_SCENE=GOBLIN` selects the checked plain-Goblin component update
marker. The shared callback checks exact independent class/control/tag pointers,
normal mode0 and matching delivered/stored state0xa4; the Town check remains
separate. After observing the marker the checkpoint driver releases scripted
input and requires120 advancing flips and vblanks. This proves component/frame
advancement only. Inspect rendered enemies and test attack/damage/adventure
results independently before calling combat playable. Constructors, asset opens,
music requests and tutorial-mode Goblins do not satisfy this marker.

For transition inspection, `DCP_CHECKPOINT_SETTLE_FRAMES=600` keeps input neutral
for at least600 advancing flips and vblanks after the requested marker. The
default remains120; accepted values are120–3600. Existing trace, vblank and
frontend wall-clock budgets still apply. Reports include the requested count
and observation flip/vblank anchors. This
diagnostic option does not affect interactive Play or certify the resulting
pixels. Compare later settled images before treating a transition capture as
a persistent rendering defect.
