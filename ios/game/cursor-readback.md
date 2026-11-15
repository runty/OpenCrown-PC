# Read-only mobile cursor feedback

Direct screen pointing reuses `linux-port/tools/dcp_run/touch_math.h`'s existing
Mac relative-touch loop. The supported game has a relative cursor; raw absolute
touchpad coordinates do not implement screen tapping. All cursor movement and
interaction still come from normal ScePadData input, not guest-field writes.

## Observation boundary

The supported eboot SHA-256 is
`42243a6c2ff783d26b801c5b5db7714a1367022137a3ec2268aea9252be6acd9`.
Original `Cursor_Update` at ELF `39ad80` assigns its cursor to R12 at `39ad9b`.
No assignment changes R12 before the pad-record call at `39b0f8`. The original
game computes the player slot through its own virtual getters, checks slot <=3,
reads the slot's pad id at player-array + slot * `23b0` + `2284`, then calls
`45850`. This avoids adding observer calls into guest virtual getters.

At translated entry `20045850`, the observer accepts only RSI=0 and original
return address `2039b0fd`. R12 must point to the supported cursor vtable
`20b10c08`. Checked guest reads sample `abc/ac0` before the original update
applies this frame's touch delta. Wrong call sites, other pad ids and other
vtable identities are ignored. Memory failures are counted, not made into
runtime failures. The existing supported-image check still applies.

`CursorReadback` rejects nonfinite/out-of-picture bit patterns using integer
comparisons, preserving host FP flags. A mutex publishes the position and
sequence coherently. A new engine starts with no sample. The mobile input
hook forwards a snapshot before reading pad zero. Diagnostics expose the
readback sequence, coordinates and read-error count so game execution can be
verified separately from fixture/model success.

## Verification

Controller information must not advance the virtual finger. The original
parser calls `scePadReadState` at ELF `5a2a18`, then
`scePadGetControllerInformation` at `5a2a4d`. Sharing the consuming read hook
between these calls can discard an edge step: actual game samples alternate
contact/no-contact at pad centre with zero delta, while the frontend repeatedly
re-contacts and its cursor stays still. A model with a second consuming read
reproduces the exact `(211,-293)` → `(211,380)` stall. Metadata now uses a
separate non-consuming connection query, with a last-valid-read fallback for
hosts without that query. Invalid destination buffers also reject before a
sample is consumed. The offline fixture checks query/read counts, connection
changes, bounds and fallback; the cursor model includes repeated metadata
queries between real samples. The headless test script runs the offline
fixture normally and under ASan/UBSan.

`ios/platform/build-check.sh` compiles/runs the independent relative-cursor
model and snapshot probe, then compiles/links both mobile SDK adapters.
The model includes first contact, id changes, lifts, the observation delay and
screen clamps. Native, ASan/UBSan and TSan probe runs pass; the normal 14-test
Mac regression suite also passes. Original ELF PT_LOAD/PT_SCE_RELRO bytes and
LLVM disassembly verify the observation boundary. A retained Hopper analysis
session already owns this target and is left untouched.

Clean `ce065bb` executes the observer in the actual native game: a 2,525-frame
cold Town reload collects 1,894 samples with zero read errors and preserves
the earned adventure save. In packaged iPhone Simulator, actual UIKit input
holds a free-screen target at `(246.273,-108)` game pixels. A read-only host
snapshot records `(246.213,-108.253)`, less than 0.27 pixels from that target,
with the virtual movement axes neutral.

The same packaged build loads Data 1, travels from Town to Ancient Temple
Ruins, clears the first encounter/crates through the virtual controls and
opens the first chest with a short screen tap. Rannie unlocks the chest and
the original Treasure ranks tutorial appears. Earlier attempts used a chest
coordinate captured before the camera settled; the settled target succeeds
without a code change. Public Pause → Stop Game returns to the launcher after
15,014 captured frames, with 13,149 cursor samples, zero read errors and empty
runtime/frontend/save errors. The isolated save retains exact `728f0ec…`
bytes. Private evidence is `cursor-investigation/touch-pointer-verification.json`.
The XCTest input body acknowledges and terminates the owned game; its separate
teardown stalls, so this is not an official XCTest suite-pass claim.

This qualifies actual Simulator cursor steering and chest interaction. Door
use, a complete touch-only fresh tutorial, physical controls and sustained
performance remain separate acceptance gates. Simulator audio counters are
clean in this run; they do not qualify audibility or physical audio behavior.
