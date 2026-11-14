# Mobile name entry

The AOT adapter implements the game's existing IME dialog ABI, matching the
pinned desktop adapter's parameter offsets, status/result values and panel size.
Init validates the fixed parameter area and complete UTF-16 output capacity,
then copies the title and initial text into an owned request. It accepts limits
of 1–2,048 UTF-16 units; unsupported capacities fail explicitly.

UIKit polls requests on the main display loop and presents a native text field
above the SDL window. The game, input/audio adapters and Metal presentation are
paused while editing. Done copies a bounded completion to the adapter; Cancel
preserves the original name. Workers resume after dismissal. Backgrounding
retains the paused dialog until the app becomes active again. Stopping drains
workers, dismisses the modal and resets the adapter before guest memory release.
The frontend source compiles for iPhoneOS and iPhone Simulator; actual character
creation and keyboard ergonomics remain acceptance work.

UI callbacks never retain or write guest pointers. The guest's next status or
result poll commits the accepted text and terminator, then publishes FINISHED.
The checked pointer hook validates the complete span before each write. These
IME buffers are exclusively transferred by dialog protocol; memory mappings
remain registered until quiescent engine teardown. The hook must not park a
guest while holding the IME mutex. The runtime uses its non-parking checked
pointer operation, rather than a cancellation callback that could wait for UI
resume while blocking that UI's request lock.

Surrogate pairs count as two units and cannot be split at the length limit.
Malformed surrogate units become U+FFFD; embedded NUL terminates the result.
Cancel and Abort preserve the original buffer and return distinct end statuses.
Monotonic request IDs survive session reset, so a late completion from a previous
session cannot edit a new request. Headless checkpoint runs complete unchanged,
as on the desktop headless path. The adapter does not log entered names.

```sh
./ios/game/test-ime.sh /Volumes/office/Build/OpenCrown/ime-probe
```

All three native, ASan/UBSan and TSAN runs pass **495 checks**, including 100
concurrent UI completions per run. They cover misaligned guest fields and output,
capacity bounds, stale IDs, cancellation/abort, Unicode truncation/replacement,
canaries, explicit invalid-range rejection and the headless path. The fixture and
adapter compile for all three arm64 SDKs with warnings treated as errors. These
are service/ABI tests, not evidence that the real game has reached name entry.
