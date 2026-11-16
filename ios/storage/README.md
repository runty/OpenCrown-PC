# Mobile game import and local save checkpoints

`storage-core` implements filesystem transactions. `storage-ui` supplies a Files
folder picker and a cancellable progress sheet. They are independent of the CPU
runtime, rendering and the existing Mac app. The integrator must stop the guest
and **all** guest file writers before import, transaction recovery, checkpoint
or restore; pausing presentation alone is insufficient. Serialize storage
operations and do not run two imports/checkpoints against the same roots.

Build the static libraries and run the file-free regression suite:

```sh
./ios/storage/build.sh /Volumes/office/Build/OpenCrown/storage-probe
/Volumes/office/Build/OpenCrown/storage-probe/storage-validate /path/to/CUSA10454
```

Outputs are `mac/libstorage.a`, `ios/libstorage.a` and
`simulator/libstorage.a`. The core uses C++20 for the existing UTF table reader.
The UIKit archive additionally requires Foundation, UIKit and
UniformTypeIdentifiers at final app link. CommonCrypto's SHA-256 comes from the
system; no third-party crypto library is needed.

## App integration

Retain a `DCPStorageController` initialized with the app's Documents URL. Call
`recoverLocalStorageWithError:` before starting the guest. It recovers `Game`,
`ProbeSaves` restore transactions and abandoned `SaveCheckpoints` staging copies.
Set `completionHandler` and optionally `progressHandler`, then call
`presentGameFolderPickerFrom:` from a stopped-game launcher button on the main
thread. A nil completion error means the validated directory was installed at
`Documents/Game`. `NSUserCancelledError` identifies cancellation. The controller
holds the selected folder's security scope throughout its coordinated read and
copy. `cancelImport` sets a polled flag and cancels pending file coordination.
The existing installed game remains usable until promotion.

The production importer always requires this private dump revision:

| File | Bytes | Validation |
| --- | ---: | --- |
| `eboot.bin` | 22,283,899 | SHA-256 `f9bff150c4c28507096e7e143a07492063ed89bf05ac6cb995a55a0946e3508a` |
| `sce_module/libc.prx` | 1,069,730 | SHA-256 `1e952f9486c966e8e3058c01902c8d9c7382acfd3030362db19dfd74681f925c` |
| `DRACRO.CPK` | 5,264,082,928 | Exact size plus CPK/TOC structure and every entry's bounds |
| `DRACRO_US.CPK` | 871,756,328 | Exact size plus CPK/TOC structure and every entry's bounds |

An ordinary `sce_discmap.plt` is copied when present, bounded to 16 MiB. Required
names are resolved component by component with the same case fallback as the
runtime and normalized at the destination. Symlinks, special files, path
traversal, ambiguous fallback casing and unrecognized executable hashes reject.
CPK table packets are bounded to 64 MiB, and offset arithmetic checks overflow.
CPK payloads are not individually authenticated; table bounds do not prove every
asset's contents. Import copies only these runtime inputs, not an arbitrary tree
or unrelated dump-folder files.

The four mandatory files total 6,159,192,885 bytes. Preflight requires their
complete staging size, optional metadata and another 64 MiB free on local
storage. It never relies on streaming assets from iCloud or the Files provider
while the game runs. The existing installation remains on disk while the new
copy is staged. A full-size replacement therefore needs about another 6.16 GB
free, in addition to the already installed data. Copies use bounded 1 MiB heap
buffers, progress callbacks and cancellation between chunks. Files are fsynced
before validation and promotion.

`.<destination>.staging`, `.<destination>.transaction` and a generation marker
identify the transaction. A validated replacement exchanges directories using
Darwin's atomic `RENAME_SWAP`; a first installation uses atomic rename. Recovery
discards unpromoted partial staging copies. If promotion happened before an
interruption, it retains the former installation as `.<destination>.previous`.
Only the latest former installation is retained, and saves live outside Game.
This journal permits recovery at launch without rescanning 6 GB of archives.
The CLI validator can run the full lightweight executable/TOC checks separately.

## Save checkpoints

Use the C++ API only while writers are stopped:

```cpp
using namespace dcp::storage;
auto id = checkpoint_saves(documents / "ProbeSaves",
                           documents / "SaveCheckpoints");
validate_checkpoint(documents / "SaveCheckpoints" / id);
restore_checkpoint(documents / "SaveCheckpoints" / id,
                   documents / "ProbeSaves");
```

Checkpoints have random 32-hex directory IDs and a SHA-256 inventory of every
ordinary file. They preserve nested and empty directories, including save
metadata; no temporary/cache directory is silently excluded. The reserved
`.opencrown-generation` and `.opencrown-checkpoint` metadata files are not copied
as game save data. Symlinks and special files reject. The current bounds are
8,192 files and 2 GiB per complete save tree. Source and checkpoint paths must be
disjoint. Available-space checks apply to staging. A checkpoint is only made
visible after its full copy and inventory validate. Abandoned `.ID.pending`
copies can be removed with `recover_checkpoints`, which leaves unrelated files
and completed checkpoints alone.

Restore validates the complete checkpoint first, stages a fresh copy, validates
again and atomically exchanges the live directory. The former live tree remains
as `.ProbeSaves.previous`. An invalid checkpoint never replaces the live tree.
The direct `restore_checkpoint` API refuses an unresolved save-write journal.
For the launcher, `restore_user_checkpoint` first recovers any interrupted
write (retaining its partial tree), then applies the selected checkpoint. This
prevents a later launch from silently rolling back a newly selected restore.
If recovery fails, the restore fails and the journal and interrupted live tree
remain available for repair. Both APIs still require exclusive access and all
guest writers to be stopped.
Completed checkpoints are retained until the user/integrator deliberately prunes
them. An empty tree is a valid checkpoint; the launcher decides when that is
useful. The helpers do not infer whether game progress or a save slot is complete.
Create checkpoints at known quiescent boundaries and offer explicit recovery
after an interrupted save rather than automatically preferring a newer partial
slot.

These helpers access only the supplied roots. They do not configure automatic
cloud sync or inspect live Mac progress. The manual Files handoff below provides
explicit save copies. Cross-device cloud completion and conflict handling still
need provider/device acceptance.

## Manual save handoff with Files

With the game stopped, **Transfer Save with Files** offers **Export Save Copy**
and **Import Save Folder**. Export prepares a complete SHA-256 checkpoint under
`Documents/SaveTransfers`, then opens Apple's copy/export picker. Choose a
folder in Files, including iCloud Drive, to receive a separate copy. Local
progress is unchanged. The temporary source is removed after picker completion
or cancellation. A process killed while the export picker is open may retain
its staging copy. The destination folder keeps its inventory metadata.

Import accepts a folder containing `SAVEDATA00/SAVE0.DAT`, either a raw Mac save
root or a previously exported checkpoint. It holds the Files security scope
through a coordinated read. An existing inventory must validate, and the
staged copy must match it. Empty/missing slots, symlinks, special files,
overlapping roots and unrecovered save-write journals reject. The existing
2 GiB / 8,192-file limits and storage preflight apply. This validates filesystem
structure and advertised hashes, not the game's internal save format.

Only after staging the complete incoming tree does import checkpoint current
local progress and atomically replace `ProbeSaves`. The old save remains a
completed local checkpoint and a previous tree. Restore Save Checkpoint can
select the pre-import copy. Incoming checkpoints also remain available.
The picker remembers the last transferred folder only within this launcher
session; the next import still requires an explicit selection.
Game execution and other storage actions remain disabled throughout a transfer.
If file coordination returns without invoking its read callback, both the save
and game-data importers report an error instead of reporting an import that
never occurred. Provider errors and errors from the import itself are kept.
The integrator owns exclusive access and must stop/join all guest workers first.
`DCPSaveTransferController` also recovers abandoned local transactions before
reading or replacing saves.

Quit the game on every participating device before handoff, and wait for Files
to upload/download the complete folder. The Mac launcher can use a copied save
root directly because both runtimes use the same PS4 files. Keep independent
exports when offline progress diverges. This UI does not merge saves, overwrite
the existing `iCloud Drive/OpenCrown/saves` root automatically, keep a persistent
cloud-folder bookmark or declare that an upload has reached another device.

The C++ entry points are `stage_save_transfer(source, staging_root)` and
`import_save_folder(source, save_root, checkpoint_root)`. The latter returns
the pre-import checkpoint ID, or an empty string for a first import. It refuses
an active/interrupted write journal; call `SaveTransactions::recover()` with
writers stopped before invoking it. Neither helper prunes checkpoints.

## Verification

The arm64 Mac regression suite covers initial/replacement imports, case
normalization, exact hash failure, early and mid-copy cancellation, optional
metadata, symlink/special-file/traversal rejection, metadata size bounds,
pre/post-exchange journal recovery, valid and overflowing synthetic CPK TOCs,
whole-save copy/restore, previous-save retention, hash tampering, truncated
inventories, disjoint roots and abandoned checkpoint cleanup. The same sources
compile with `-Wall -Wextra -Werror` for arm64 iPhoneOS and iPhone Simulator.
The supplied matching real dump passes executable hashes, archive sizes and
archive TOC bounds through `storage-validate` without copying or changing it.
Files-provider UI, progress-sheet presentation and on-device interruption remain
unverified until the devices are available.
`run-picker-qa.py` builds a separate Simulator QA app and directly exercises
picker delegate edge cases; it does not operate a Files provider or normal
game save. Specify a fresh external output, the storage build output, and an
explicit Simulator UDID. The QA app uses a unique bundle ID.

## Save write sessions

`save-transactions.h` supplies `dcp::storage::SaveTransactions`, intended for
root-runtime typed callbacks. Construct it once with the local save and checkpoint
roots. Its `begin()`, `commit()` and `recover()` return bool and catch exceptions;
`error()` returns the latest diagnostic. Calls are serialized within the object.
All instances/direct storage operations for these paths still need exclusive
access. Checkpoints must remain on the same local filesystem as saves.

At startup, call `recover()` before any guest writer starts. Immediately before a
writable savedata mount, while all existing guest writers are quiescent, call
`begin()`. It captures a complete SHA-validated prewrite checkpoint and publishes
an atomic `.Saves.write-session` journal before returning success. Only then may
the guest write. At successful unmount, after file writers are quiescent again,
call `commit()`. It captures and validates the new complete tree before clearing
the journal. A failed callback leaves the journal for recovery; report `error()`
and do not silently acknowledge successful persistence. Nested begin and commit
without begin reject.

An interrupted session restores its prewrite checkpoint. Its former, potentially
partial tree is retained under `SaveCheckpoints/interrupted-ID` and reported by
`last_recovery_path()`. This archive is deliberately not labeled a validated game
save checkpoint: it is a preserved repair candidate. Recovery validates the
rollback checkpoint before changing live data. A persisted recovery generation
makes restart after directory exchange or after archiving idempotent, so another
termination during recovery cannot replace the interrupted copy with the restored
one. The generic `.Saves.previous` directory is moved into the interrupted archive
as part of this operation. Completed write sessions retain their new save on
subsequent startup.

The additional regression cases cover begin/write/recover, complete commit,
invalid/nested transitions, rollback-checkpoint corruption and restart after both
the recovery exchange and interrupted-tree archival. These are filesystem
simulations on the host; actual termination during the translated game's write
still requires an integrated runtime/device pass.

A separate interrupted-write/user-restore fixture verifies direct restore is
rejected while the journal is active; the user-facing restore then recovers
the interrupted copy, applies the chosen checkpoint, and retains it after a
subsequent simulated launch. A corrupt rollback checkpoint instead leaves the
interrupted data and journal untouched. The regression failed against the
previous direct restore implementation. It does not replace a device or
manual in-game save/relaunch test.

For the recovery change, the 159-check storage suite and 216-check integrated
save-service suite also pass as iPhone Simulator processes using disposable
files. The storage suite passes ASan/UBSan and TSan on macOS. iPhoneOS and
Simulator app builds link the launcher change. A separate Simulator QA bundle
then exercises the actual launcher's **Restore Save Checkpoint** button on a
synthetic interrupted session. XCTest selects the checkpoint and observes the
success message; a host-side check confirms the chosen bytes, prewrite copy,
interrupted archive and an additional simulated recovery. The test uses the
launcher source from `c0dee0d`, the existing isolated NaN-candidate core and
an independent app container; it does not run the game or touch a device.
`ui-qa/RestoreTests.mm` and `ui-qa-seed.cpp` are test-only. The seed helper
requires a marker file with `synthetic restore QA` followed by a newline, and
the seed operation also refuses existing save/checkpoint roots. The test
project, command logs and results are under
`/Volumes/office/Build/OpenCrown/mobile-adventure-live/save-restore-recovery/restore-ui-qa/`.
Physical-device, real interrupted-write and skipped manual save checks remain
open.

A second independent Simulator QA app covers the error path. Its synthetic
rollback checkpoint is corrupted after the write journal is created. XCTest
chooses a separate valid checkpoint and observes the checksum error; a
host-side check verifies the interrupted live bytes and journal are unchanged,
the chosen checkpoint remains valid, and no recovery archive or previous tree
was falsely created. `ui-qa-seed.cpp` provides `seed-corrupt` and
`verify-corrupt` modes for this fixture. Each XCTest method requires its own
pre-seeded app container; select one with `-only-testing` per invocation. Evidence:
`/Volumes/office/Build/OpenCrown/mobile-adventure-live/save-restore-recovery/restore-ui-failure-qa/`.

The successful-restore QA app subsequently ran the real game from a separate
disposable R diagnostic save. It reached an inspected Town frame after 2,186
captured frames and verified 25,276 texture-version hits against full contents
without a proof failure; the R save and earlier synthetic QA saves/checkpoints
were unchanged. The QA container used an APFS clone of the already installed
Simulator game files. This checks runtime/renderer integration with the fixed
launcher; it does not claim the synthetic restored save was game progress,
that the skipped manual in-game save test occurred, or that device performance
or audio was qualified. `ui-qa-game.py` records the exact diagnostic setup;
its build-ID and guest-boot assertions were added after the retained run and
were independently verified in `final.json`. Evidence:
`/Volumes/office/Build/OpenCrown/mobile-adventure-live/save-restore-recovery/real-game-qa/`.

The fix was also packaged against the audited `bb540ac` native support and
matching renderer, without the experimental NaN core. Both simulator and
signed iPhoneOS builds include compiled icons; their three CPU library hashes
match the audited artifacts, and their 50,464-entry translated corpus has the
same trace hash as the `851c06b` stable package. This is not an identical
`851c06b` app: it includes the later `bb540ac` support overrides. A separate
Simulator QA app reached an inspected Town frame in 2,341 frames with 26,112
full-content texture checks, zero proof failures and an unchanged disposable R
save. The game was muted; no physical device or in-game save test was used.
The earlier attempt to pair the current launcher with the original `851c06b`
renderer failed at link time because that renderer lacks `texture_stats_v2`;
the matching renderer was used without bypassing that ABI boundary. Evidence:
`/Volumes/office/Build/OpenCrown/mobile-adventure-live/save-restore-cache-core/`
and the retained `save-restore-stable-core/build-simulator.log`.

An end-to-end Simulator QA pass then used copies of two real saves in that
independent app. The fixture checkpointed R, began a write session on Darrel,
and changed one byte of the local copy to simulate interruption. XCTest used
the actual launcher to select R's checkpoint. Hash inventories confirm R was
restored, Darrel's complete prewrite tree retained, the modified interrupted
tree archived, and the journal cleared. A separate diagnostic session copied
that restored R tree and loaded it to an inspected Town frame, with 2,124
frames and 25,557 texture checks, zero proof failures, and the restored and
retained trees unchanged. This is a simulated interrupted write and a copied
restore, not an in-game save or a physical-device test. The fixture source is
`ui-qa-real-seed.cpp`; it requires a `real-save restore QA` marker and empty
save/checkpoint roots, and only reads its source trees. `ui-qa-game.py` accepts
`--restored` for the subsequent diagnostic copy. The QA project, logs and
results are under `save-restore-cache-core/real-restore-ui/` and
`save-restore-cache-core/real-restore-game/`.

The same non-NaN build also reached the first GOBLIN/Ruins checkpoint from a
separate disposable Darrel save and completed 720 scripted encounter frames.
It captured 7,162 frames, verified 142,568 texture hits against full contents
with no proof failure, retained the Darrel save hash, and left the previous
restored R save and its recovery copies unchanged. The final captured frame
shows Darrel at the chest. `ui-qa-game.py --combat` uses the retained
`ui-qa-ruins-input.txt` route in this separate QA app. This is not a full
adventure clear, device performance test, audio acceptance, or in-game save
test. Evidence: `save-restore-cache-core/simulator-ruins/`.

Save write sessions apply retention only **after a successful durable commit**:
keep the latest ten validated checkpoint directories, ordered by checkpoint
modification time with an ID tie-breaker. The just-committed checkpoint is always
kept. An active prewrite journal is never pruned during begin or recovery, and
interrupted repair archives are never automatically removed. Unknown names,
symlinks, special files and invalid checkpoints remain untouched. Low-level
`checkpoint_saves()` still has no pruning policy. `retention_warning()` reports a
cleanup failure separately; an already durable save commit remains successful
and its journal stays cleared even if an old backup cannot be removed.

`save-service-integration.cpp` joins the actual offline savedata adapter, file
adapter's exclusive I/O gate and SaveTransactions. It creates a writable mount,
writes through guest file calls, rejects unmount while a writable descriptor is
open, commits after close, reopens the committed bytes across simulated launches,
then interrupts another write without unmount. The next launch restores the
prewrite progress and retains the interrupted bytes; a following clean launch is
idempotent. `build.sh` runs this fixture as `save-service-integration` in addition
to the core suite. Current results are 149 core/retention/transfer checks and 108 integrated
service checks, including four simulated relaunches. Both return zero failures.
The retention suite makes fifteen commits, checks the exact latest-ten IDs after
each commit, protects a deliberately old active rollback, preserves unknown and
special entries, and verifies the generic checkpoint API retains more than ten.


## Manual handoff qualification

The dedicated iPhone Simulator exports a real edited save through the local
Files provider, then imports that copy over a different real combat save using
the remembered picker location. The imported `SAVE0.DAT` keeps its exact hash,
and both a completed checkpoint and the previous tree retain the replaced
save's exact hash. An invalid folder selection leaves local progress unchanged.
That earlier round trip uses the picker-granted export URL as an initial
directory. A later first-use test uses separate QA app containers and the
unchanged production transfer controller/storage code. Export to the local
provider's ordinary root succeeds. A new controller with no remembered directory
then browses into that exported folder and imports the exact adventure save
(`728f0ec…`) over the touch-earned save (`df59b068…`). The replaced save survives
exactly in both the previous tree and a completed checkpoint. Cancelling the
first picker also leaves local progress unchanged.

A damaged copy with one changed byte fails checksum validation before promotion;
all live, previous and checkpoint bytes remain unchanged. `734ceb9` corrects
the generic checksum error text to `File checksum mismatch`, since the same
validator checks saves as well as executable files. The current storage/service
suites retain 149 / 108 passing checks, including four simulated relaunches.

Top-level app-container rows still fail to navigate on both an existing and a
fresh iOS 27.2 Simulator. Apple's Files app reproduces the failure and provider
logs report a missing cached domain for `FILocalAppContainerNode`. Ordinary
provider folders work in the same environment. This qualifies first-use local
folder browsing, not app-container navigation or physical-device Files. Actual
iCloud propagation and offline divergence remain open. Private evidence is
`cursor-investigation/files-provider/verification.json`; QA saves never replace
the game app's normal save root or live cloud progress.
