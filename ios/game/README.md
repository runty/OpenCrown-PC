# Real-module AOT startup harness

This private build path lifts executed x86-64 routines from the supported
CUSA10454 patch 1.03 dump into signed arm64 code. It does not execute writable
memory, use Rosetta on iOS, or generate code on the device. Generated module
images, relocations, LLVM IR, object archives and game data remain outside this
repository. The builder rejects a different eboot or libc SHA-256.

Use the [bootstrap workflow](../BOOTSTRAP.md) to build the pinned Remill
backend with all eight local patches, SoftFloat helpers/FP80 lowerer, FreeType
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

The current headless boot checkpoint requires the observed real `TITLE` scene
entry followed by 120 advancing vblanks and flips. A returned startup report or
`game_code_executed=true` alone does not mean the game has booted. Full mobile
acceptance additionally requires INN/town, visible frames, real controls/audio,
lifecycle and save/reload on both physical devices, as defined in the port plan.

The SSE reciprocal implementation returns full-precision reciprocals within the
x86 estimate's permitted error bound, with explicit signed-zero, infinity, NaN
and denormal behavior. Tests compare native reciprocal estimates within that
bound; integer and bit-permutation fixtures compare exactly. This does not imply
complete x86 instruction, floating-point exception or MXCSR coverage.
