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

`ios/platform/build-check.sh` compiles/runs the independent relative-cursor
model and snapshot probe, then compiles/links both mobile SDK adapters.
The model includes first contact, id changes, lifts, the observation delay and
screen clamps. Native, ASan/UBSan and TSan probe runs pass; the normal 14-test
Mac regression suite also passes. Original ELF PT_LOAD/PT_SCE_RELRO bytes and
LLVM disassembly verify the observation boundary. A retained Hopper analysis
session already owns this target and is left untouched.

Actual game readback, screen targeting and door/chest interaction remain
pending until the candidate is exercised. Fixture convergence does not count
as those runtime results, physical input or sustained performance.
