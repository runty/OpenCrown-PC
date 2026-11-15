# Device port progress

## 2026-11-15 — Ruins status coverage and Harpy Continue dispatch

Continuing from the earned guild-progress save, native Darrel passes the first
encounter, pit vipers, spiders, bats, the waterfall corridor and lizardmen,
collects the quest bones and reaches the Harpy boss. Ordinary item controls
consume Heal Potion (S): HP changes from 85 to 175 and quantity from 3 to 2.
A later use also increases HP by 90 and reduces the quantity again. This
qualifies native ordinary potion use; touch-only and physical use remain open.
Short right-stick pulses plus L1 also open a Ruins chest.

The first interior discovery stops at missing `202e0e5c`. Original ELF
`2e0c40` uses paired six-case signed-relative tables at `71822c` / `718244`,
with unsigned index <= 5. Source `ff04004` requires all twelve destinations;
six getters were already present and six setters are new. A separate bounded
replay logs actual execution of `202e0e5c` at vblank 14,267 and continues to
28,329 vblanks / 28,327 rendered frames in 480 seconds, with empty host/guest
errors, acknowledged unload and unchanged guild-save bytes. The individual
status effects have not been named from this evidence.

The guided adventure then renders 40,701 frames through the Harpy fight,
death, automatic revival and the Continue prompt. It stops after Continue at
missing `20519de5`. Original ELF `519d70` loads a zero-extended state byte,
guards it unsigned <= 9, then dispatches through ten signed-relative entries
at `75a610`. All ten destination instructions and executable mappings are
verified against the supported ELF. Source `cc12a9c` requires the complete
switch. Its frozen 50,425-entry core builds for Mac, iPhoneOS and Simulator;
all ten archive symbols and library hashes verify. Both icon-bearing SDK
apps package from clean source; iPhoneOS signing verifies without increased
memory or extended-VA entitlements. A second native run visibly defeats the Harpy. Read-only probes record
`20519de5` at vblank 39,918 and `2051a17d` at 40,099, so the first repaired
state target is now executed. Victory also reaches this owner: the evidence
does not identify it as an exclusively Continue-specific routine. The run
then stops at missing nested target `2051c06e`, before rewards/return/save.
It renders 39,516 frames, unloads cleanly and retains the prior guild-save
hash. All four indirect dispatches in ELF `519d70..51d520` are now inspected.
The three nested tables at `75a648`, `75a674` and `75a638` have unsigned bounds
10, 14 and 3 and twenty distinct destination instructions. These are added
together in a frozen 50,445-entry core, which builds for all three targets.
Executing the new result targets and returning with saved progress remain open.
Neither a full adventure completion nor a newly earned adventure save is
claimed from the boss defeat alone.

The guided run pauses through the public lifecycle API between input batches.
It modifies no guest scene, health, cursor or save fields. Its 2,926-second
wall time includes those pauses and is not sustained-play or performance
evidence. The collected input history is only a replay candidate until checked.
Both physical devices remain released; no install or launch is attempted there.

Packaged `cc12a9c` cold-reopens the same isolated guild-progress save twice
without reseeding. The 300-frame settle checks render 1,871 / 1,886 frames,
show Darrel in visible Town, retain the exact `04855cc…` hash and stop with
empty runtime/frontend/save errors. Earlier 120-frame checks stopped while
the loading logo remained visible; those are retained as checkpoint-only
evidence and do not count as visual Town acceptance. Simulator audio is the
paced null sink. The long unattended Ruins replay diverges from its guided
source and is continued with ordinary live input; it is not an unattended
acceptance route.

## 2026-11-15 — Fresh tutorial clear and earned-save reloads

A fresh Fighter, Darrel, completes the introductory tutorial in the native
arm64 AOT harness: movement, attacks, guard, Power Smash, crossbow encounter,
item lesson, apple consumption, locked door, chest and tutorial Orc boss. He
then reaches visible Town with the Adventurers Guild objective. The bounded
1,800-second run renders 106,758 frames and ends with empty host/guest errors,
acknowledged unload and no shutdown-grace expiry. This is a tutorial clear,
not a successful Ancient Temple Ruins adventure.

Short right-stick pulses aim the finger cursor and ordinary L1 selects the
door/chest. Earlier long holds drove the cursor to the screen boundary. A
private read-only observer confirms the original input conversion and cursor
coordinates; it does not write scene, cursor, health or save fields. The
successful run uses ordinary pad input with live timing adjustments. Its
collected input history is not yet a repeatable unattended acceptance route.
The touch contact experiment did not establish absolute screen pointing.
Touch-only door/chest interaction and potion consumption remain open.

The game writes Darrel's new progress with SAVE0.DAT SHA-256
`5c03436b3c67ae0ce543ac4c206412fe8c881795c93e1a2647c038e731a8a81d`.
Two separate native cold processes reopen the same working save without
reseeding, show Darrel in Town and retain that exact hash. A copy is then
placed once in a new isolated iPhone Simulator diagnostic save session.
Two cold launches of packaged build `34e4a0b` resume that same session without
reseeding, reach checked and visible Town, render 1,890 / 1,842 frames and
retain the exact newly earned save hash. Original Simulator player saves and
seeds are untouched. The Simulator reload checks use paced null audio.

Real platform audio on this dedicated Simulator aborts before the first frame
with an AURemoteIO Cleanup RPC timeout while obtaining AVAudioEngine's main
mixer. A separate minimal UIKit/AVAudioEngine app reproduces the same abort
without the game runtime. The Simulator audio-startup failure remains under
investigation; null-audio save checks do not qualify audio recovery or output.
After standalone audio configuration tests, three unchanged baseline probe
launches complete. Two cold game launches then reach Town with real platform
audio callbacks and retain the exact Darrel save hash. They render 1,809 /
2,020 frames; the first counts 1,280 underrun frames and the second zero, with
no clipping. Simulator output is muted after consuming the real ring. The
intermittent earlier timeout is retained; its cause is not established and
this does not qualify audible output or interruption recovery.

Continuing Darrel's normal quest reaches the guild, repairs the sword at
Morgan's shop and accepts the entry exam. Opening the equipment menu then
stops at missing trace `204cf906`, after 34,354 frames, with all workers
unloaded and the prior tutorial save intact. The original function at ELF
`4cf860` guards the six-character index with unsigned `<= 5`, then loads
signed relative targets from table `757ee4`. All six destination instructions
are verified, and bootstrap now requires their translation. The new coverage
builds as a frozen 50,409-entry core for Mac, iPhoneOS and Simulator. Both SDK
apps package cleanly from `3276ecb`; device signature and entitlement checks
pass without increased-memory or extended-VA entitlements. All six symbols
and the archive hashes verify on each target. Headless checks pass: input
141 native / 141 ASan-UBSan, configuration 42, vblank timing 1,006, and actual
runtime rejection/budget checks 32 across eight cases.

Darrel now opens the Equipment screen after repairing his sword in the native
run. An independently edited Feli save also opens Equipment in the packaged
iPhone Simulator. That bounded test captures 6,829 frames / 6,896 flips, stops
with empty runtime/frontend/save errors and retains its exact `9069666…` save.
A compressed quest replay missed a navigation trigger and is not qualified;
the native check uses live guidance and the Simulator uses a focused menu route.

Continuing Darrel accepts the entry exam and unlocks the Gate. Merely visiting
and leaving the Inn did not write these quest changes. The Inn's Report
Adventures command saves Data 1 explicitly: its displayed gold changes from
100 to 0 after the sword repair, and the game reports Save completed. The new
SAVE0.DAT SHA-256 is
`04855ccd81cf2b90ea19a4ecf4f8495c74df2e353912f2b5e3d776d9ed84a3de`.
The 1,200-second native run finishes with 70,634 frames, empty host/guest
errors, acknowledged unload and no grace expiry. Two native cold launches
then reopen the same working folder without reseeding: each renders 1,923
frames, passes the Town checkpoint and retains that exact new hash. After one
copy into a new isolated Simulator save session, two packaged cold launches
render 1,841 / 1,859 frames, reach visible Town and retain the same hash without
reseeding between them. Their new narration concerns proving the character's
mettle, rather than the earlier arrival introduction. Simulator reloads use
null audio. The icon-bearing SDK apps retain the same qualified source/core.

The game's Game Options 3 screen identifies the default touchpad cursor mode
as Relative position. Absolute screen pointing remains unqualified; do not
infer it from right-stick cursor success. Town-map touchpad input also needs
separate qualification.

Both physical devices remain released for the other chat. Full fresh-character
play on each device, an adventure clear, touch/controller acceptance, audio
interruptions/routes, sustained thermal play and cloud handoff remain open.

## 2026-11-14 — Manual save copies through Files

The stopped-game launcher now offers Transfer Save with Files. Export creates
an independent SHA-256-inventoried folder, then uses the system copy/export
picker. Import stages and validates the selected folder before checkpointing
and replacing local progress. Existing inventories must match the copied
bytes. Missing/empty slots, symlinks, overlapping roots and pending write
journals reject. Storage actions and game execution remain mutually exclusive.
The action sheet dismisses before picker presentation. Picker callbacks are
checked against their active picker, and an in-progress local transaction
finishes before releasing storage ownership. The last selected save folder is
only a session-local picker hint, without a persistent cloud bookmark.

The storage suite passes 149 checks and the integrated savedata service passes
108 checks across four simulated relaunches. Device and Simulator SDK builds
compile. A dedicated Simulator exported the native combat-written save through
Files. Its exact SAVE0.DAT hash and full checkpoint inventory survive that copy.
The exported folder also passes a host-simulated three-hop handoff. Importing
it over the independently edited Feli save preserves Feli in a backup. These
host hops do not represent phone/iPad/cloud-provider execution.

The dedicated Simulator also exports the Feli save through the local Files
provider, then imports it over the different native combat save. The picker
starts at the remembered export location. Import restores Feli's exact
`9069666…` hash and retains the replaced `affc24e…` hash in both a completed
checkpoint and the previous tree, with no active journal. An earlier invalid
folder selection reports an error and leaves local progress unchanged.
Top-level app-container rows did not navigate in the automated Simulator test;
first-use provider/container browsing remains unqualified. The successful
round trip used the picker-granted export URL as its initial directory.

A fresh native Connor replay again passes crossbow combat, the compound item
lesson and food consumption at 330/300 HP. It renders 58,645 frames in about
1,004 seconds and stops cleanly at the locked-door lesson, with empty host and
guest errors and all runtime workers unloaded. The cursor-movement objective becomes checked during scripted right-stick
input, but opening the door remains unverified. Full
tutorial completion, ordinary potion use and new-character persistence after
Town remain open. Both physical devices stay released for the other chat.
First-use provider browsing, remote iCloud handoff/conflicts and sustained
hardware play still need qualification.

## 2026-11-14 — Item lesson, food switch and scripted right stick

A live-guided x86-64 Mac reference used disposable local user/save folders
with the Mac-only story, tutorial and warning skip offers disabled. The
reference and arm64 AOT runs both passed movement, ground/aerial attacks,
guard, Power Smash and the crossbow encounter. A compound controller sequence
advanced the item lesson in both runtimes. An enlarged HUD capture identifies
the selected item as the Buckler. Potion consumption and ordinary directional
item selection still need separate qualification.

The AOT run broke the food box and picked up its apple, then stopped at
`202ab640` when eating began. Source `7f3e2f7` compiles all six verified
destinations of the bounded food switch at ELF `710570`. The 50,403-entry
core packages for iOS and Simulator. A fresh native run consumed the apple,
raised Abby's HP from 300 to 330 and reached the locked-door/Rannie lesson.
Its 1,400-second diagnostic ended there with an empty runtime error.

The headless script now accepts `RS_LEFT`, `RS_RIGHT`, `RS_UP` and
`RS_DOWN` alongside the existing left-stick controls. Both desktop and AOT
pad adapters convert those flags into axes and clear the flags from guest
buttons. The expanded input fixture passes 141 checks in native and
ASan/UBSan runs and compiles for the iOS SDKs. All 14 x86-64 desktop
regressions pass. Real-game scripted cursor movement is the next check.

The iPad Simulator also created a new level-one Fighter, Gerald, and reached
crossbow pickup using the food candidate. The fixed replay's pickup positions
required adjustment. Food consumption in Simulator remains open. Its
ordinary saves, checkpoints and diagnostic seed retain their exact hashes,
and the original diagnostic input was restored after stopping that test.
Both physical devices remain released. Tutorial completion, a successful
adventure clear, sustained hardware acceptance and cloud handoff remain open.

## 2026-11-14 — Character-edit switch and post-write recovery

The iPad Simulator's original game keyboard shortened the disposable name from
Felix to Feli. Leaving the Inn saved it. A cold process reopened Feli with the
exact new SAVE0.DAT SHA-256
`9069666229e360dd969bbe0fe1953589f2f8ecfe55f2d0107e5556dfe669d05d`.
This exercises the game's keyboard through the touch overlay. The native
UIKit IME was not invoked on this route.

A subsequent armor edit stopped at missing target `203aea16`. Source
`5ad93eb` adds every destination of its original four-entry relative switch
at ELF `74dce0`. An unsigned index guard, signed relative loads and target
instructions were verified before translation. The frozen core has 50,397
entries and packages for both iOS and Simulator. Reinstall retained the edited
name and its save hash.

With that build, another armor edit reached an actual savedata write. The
watcher stopped and killed the Simulator app after SAVE0.DAT changed to
`af7dce1f69b43594f912734beaec402910441c3333d21018d87e0e14fafba1f4`
while its save journal remained active. A cold launch recovered the exact
prior `9069666…` save, cleared the journal and displayed Feli with Burnished
Bronze. The retained interrupted file matches the captured `af7dce1…` hash.
This qualifies interruption after guest bytes changed and before transaction
commit, alongside the earlier prewrite case. Physical interruption remains open.

The fresh-Fighter native replay also passed crossbow pickup, firing and the
goblin encounter. It stopped at potion selection/use until its 1,400-second
deadline. No missing trace was reported there. The cause is unresolved.
A disposable x86-64 Mac reference replay is comparing the same control route.
Both physical devices remain released.

## 2026-11-14 — Fresh Fighter creation and changed-save cold reloads

Build `7f7298f` compiles all four verified destinations of the character-selection
switch. The 11 signed table entries resolve to four unique targets. Instruction
and table bytes were checked against the extracted ELF before compilation.
The frozen core contains 50,393 entries and packages for iOS and Simulator.

Touch input in the iPhone Simulator created a Fighter in an empty slot and
entered the introductory tutorial. The native arm64 harness independently
created Willie from empty save data and passed movement, ground attacks,
aerial attacks, guard and Power Smash. Tutorial completion remains open.

On the iPad Simulator, touch input changed Felix's armor from Sterling Silver
to Burnished Bronze. Leaving the Inn wrote SAVE0.DAT with SHA-256
`91dfab154867eb3dd3e8ec4653b777466f22e8cb6bd9c340166164f88d095890`,
different from the original seed. Two cold launches resumed the same working
folder without seeding. Both reopened the character editor with Burnished
Bronze visible and the exact saved hash. Three completed checkpoints existed
and the write journal was absent after the save. Normal player saves and the
seed remained unchanged. These runs used a test-only Simulator shim to hide
the Mac-forwarded controller and exercise the shipping touch overlay.

The arm64 combat route also passed the spoils exit and RTC call, reached the
Town scene checkpoint after GOBLIN and wrote a changed SAVE0.DAT
(`affc24e12f718663174ec55860521458eb1a1fcf7b766c6146c03755c50a5e36`).
Its final captured image was still black during transition. A separate iPad
Simulator working folder received a copy of that native save. Two cold launches
retained its exact hash, reopened Felix and reached visible Town. The save was
produced on the Mac runtime. A successful adventure clear and the complete
combat-to-save sequence on physical devices remain open.

A separate clone of the armor-edit save also passed Simulator recovery.
Felix was changed from Burnished Bronze to Divine Gold. Leaving the Inn
published the real save journal. An external watcher stopped and killed
only that Simulator game process while the journal existed, before guest
data changed. Relaunch cleared the journal, kept an interrupted checkpoint
and restored the exact prior SAVE0.DAT hash. The editor visibly showed
Burnished Bronze. This qualifies the prewrite interruption boundary.
Termination after partial replacement and physical interruption remain open.
Both physical devices are released for another chat.

## 2026-11-14 — iPhone Town, existing-save reload and Simulator lifecycle

On iPhone 17 Pro Max, the 3,000-frame Felix Town walk with platform audio
reached 57.87 fps. Median and p95 replay intervals were 16.68 and 18.45 ms.
The replay covered all 2,999 guest flip intervals. Two separate cold launches
reopened the same working save at the Inn. The Felix slot retained its original
save hash. The simulator touch overlay loaded that slot, exited the Inn and
walked along the street. Pressing Home paused guest execution and audio;
reactivating resumed both in the same session. The on-screen Pause, Resume
and Stop Game controls returned to the launcher with no guest or save error.

The arm64 host reached the adventure spoils screen with 123 gold. Exit from
that screen called a missing routine at `2063b950`. The bootstrap includes
that observed target and the Town movement target `203d54a0`. Qualification
of the resulting save requires an on-device return and cold reload.

## 2026-11-14 — First Ancient Temple Ruins fight on both physical devices

The same Felix slot and scripted pad route reached TITLE, INN, Town, the
magic gate and the first goblin encounter on the physical iPad mini (A20 Pro)
and iPhone 17 Pro Max. A gate capture on the arm64 host shows the stage
selection by name. Device images show the Ruins, sword attacks, the caged or
freed fairy and score gains. The phone showed 365 points and the iPad 245.
The iPad recorded 215 active enemy-component updates and a visible damage
number. These short windows cover the first room.

| Device and signed build | Replay intervals | Rendered fps | Median | p95 | p95 host replay work |
| --- | ---: | ---: | ---: | ---: | ---: |
| iPad mini, `bb1d95a` | 600 | 59.40 | 16.67 ms | 16.90 ms | 3.14 ms |
| iPhone 17 Pro Max, `6b0302b` | 599 | 54.28 | 16.70 ms | 33.13 ms | 7.60 ms |

Neither device skipped a guest flip between its first and last measured
replays. Real 48 kHz platform output ran on both, with 256-frame callbacks
and zero counted underruns or clipping across the complete runs. A separate
3,000-frame Town walk on iPad with real audio measured 59.42 fps and 16.83 ms
p95. The iPhone Simulator also reached the encounter. The external arm64 route
continued through Roland, snakes, spiders, adventurer bones and lizardmen.
The tested input routes and launch settings are in [diagnostics](diagnostics.md).

## 2026-11-14 — Town frame budget on iPad mini

Signed build `c8b7c9f` completed two consecutive 3,000-frame walks on the
physical A20 Pro iPad mini. The starting save, boot input and scene-relative
walk match the earlier 45.3–45.4 fps runs. All 3,000 flips were replayed in
each window. The final captures show Felix in the street with the HUD.

| Run | Thermal state | Rendered fps | Median interval | p95 interval | p95 replay work | Process CPU per walk |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| First | nominal → fair | 59.40 | 16.67 ms | 16.82 ms | 2.82 ms | 69.30 s |
| Immediate repeat | fair → fair | 59.38 | 16.67 ms | 16.84 ms | 2.74 ms | 67.33 s |

The AOT texture snapshot budget increased from 64 to 192 MiB. An intermediate
build reached 52.32 fps with this change. The renderer checks the entire
texture and combines adjacent sprite primitives with identical state. Following
an on-device CPU profile, guest threads now back off after repeated yields,
cache whole-page range checks and synchronize large atlas comparisons in
256 KiB bands. Each translated unit can fold the runtime's pure flag helpers.
With the other changes in place, the walk used 84.98 process CPU seconds
before flag folding. CPU time sums all threads. Per-frame replay work measures
host processing through submission and excludes asynchronous GPU execution.

An earlier trial under serious thermal pressure measured 42.67 fps. The final
build peaked at 2.33 GB across these short walks. Both used scripted input
and paced null audio. Each finished with zero runtime, frontend or save
errors. The seed and ordinary save files retained their pre-run hashes and
the device returned to the normal launcher. Combat, long-session thermal
behavior, physical iPhone performance and interactive audio remain open.

Native Metal regressions passed 49,152 channel checks, with another 36,864
channels independently checked from PNGs. Checked guest memory passed
267,095 assertions per native, ASan/UBSan and TSan build. An active-Town
pause/resume/stop completed on the native host, which rendered 58.79 fps
on the same walk. All 14 x86-64 Mac regression tests passed.

## 2026-11-14 — Physical iPad Town walks at 45 fps

Signed build `34e793f` ran the same 3,000-frame Felix walk twice on the
iPad mini (A20 Pro), iPadOS 27.2.1, with the 50,340-entry core and a fresh
disposable copy of the same save each time. Both runs reached TITLE, INN and
Town. Neither left Town during its measurement window. The final images show
Felix, the street and the HUD. Counting completed Metal replay intervals gives
the following results.

| iPad walk | Seconds | Rendered fps | Guest fps | Median interval | 95th percentile | Skipped flips inside window |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| First | 66.00 | 45.42 | 45.44 | 17.19 ms | 34.16 ms | 1 |
| Repeat | 66.12 | 45.34 | 45.38 | 17.28 ms | 34.46 ms | 2 |

The game called the CRILAYLA decoder 94 times on each route. The native
implementation handled 91 in-place calls and decoded 283,831,168 bytes.
Three calls used the original translated routine. In isolation, the revised
decoder shortened the host boot-to-checkpoint run from 327 to 220 seconds,
while settled Town still ran at about 5 fps. Differential tests compared
four input/output layouts with the original routine, using eight game
records and 113 generated cases in native, ASan/UBSan and TSan configurations.

The larger frame-rate gain came from reducing contention and repeated work.
Guest memory checks use per-thread mapping caches and ordered page locks,
with full synchronization for guest atomic instructions. Metal reuses a
previous texture only after checking its complete contents. The intermediate
iPad walk measured 27.29 fps. Cheaper scalar memory checks, fast polling for
clear runtime signals and eliminating redundant presents brought it above
45 fps. The final host run measured 57.17 rendered fps on the same walk.
An earlier input script returned to the Inn and was excluded.

Both iPad runs ended in fair thermal state, at sampled peak footprints of
2.17 and 2.18 GB. They reported zero guest, frontend and save errors.
The previous 60 and 70 diagnostic files, respectively, retained their hashes,
as did the seed and ordinary player files. The normal launcher was restored
after each run. The checked-memory suite passed 267,089 checks per
native, ASan/UBSan and TSan build. All 27 host runtime lifecycle sessions
passed. In an additional active-Town test, the first resumed flip took 34 ms
and stop/join took 29 ms.

These scripted walks use paced null audio. The next gates are actual combat,
new progress saved and reopened, real controls and audio, a sustained
Town/combat loop, and qualification on the physical iPhone. The earlier
iPhone Simulator build completed a 1,450-frame Town walk. Follow the
[walking fixture instructions](diagnostics.md#measuring-town-frame-timing)
to reproduce the comparison.

## 2026-11-14 — Clean physical Town; settled performance remains too slow

Clean frontend `368caf2` with the frozen50,340-entry core passes the iPad's
extended Town gate:6,456 rendered frames /225,962,278 entries /303.27seconds,
exactly600 advancing flips and vblanks after observation, no guest/frontend
error. The downloaded final PNG shows clean Town and Felix, without the earlier
logo overlay. This qualifies this later scene, not the cause of the earlier
capture difference. All28 prior save/checkpoint/image files retain their hashes;
only two new session images appear. Normal launcher mode is verified afterward.

The longer window exposes unacceptable settled speed:592 rendered frames over
115.69seconds after the first Town heartbeat, about5.12fps. Native Town also
advances roughly120frames per22–26seconds. The clean native600-frame gate passes
5,993 rendered frames /208,525,509entries /315.31seconds. The prompt's earlier
59.46fps result does not extend to Town. Profiling translated game execution
is now the critical path, alongside remaining adventure coverage. No playable
performance claim is made.

Interactive iPhone Simulator touch reaches the character menu, navigates
Equipment→Armory→Stats, and shows the exact retained Felix values:LV1,EXP351,
NEXT850,HP300,STR16,CON14,DEX12,INT4,MGR7,LUC10,skill point1;47gold appears in
the character menu. Final image is inspected. Its600-second diagnostic run
stops cleanly after8,289frames /437,557,255entries. This is reopening existing
progress, not saving newly earned progress. An apparent brief menu disappearance
does not establish a release-cancel input defect; original held-mask/edge audit
and settled navigation support no input workaround.


## 2026-11-14 — Physical Town gate and clean live touch movement

The clean `1d0adf7`/50,327-entry app passes the physical iPad Town component gate
from the retained working save, without reseeding:6,131 rendered frames /
168,965,536 entries /218.88seconds, no guest/frontend error. Its57 ordered maps
use2,190,737,408bytes of4GiB, sampled peak dirty memory is1,974,112,016bytes,
and thermal state is fair. All26 retained files survive the install unchanged;
the ten prior working-save files also remain unchanged after the run. Normal
launcher mode is restored. The final PNG shows Town/Felix beneath an unexpected
large logo overlay. Clean physical Town rendering remains open; this capture
alone does not distinguish a transition from retained composition.

An independent interactive iPhone Simulator run with clean `e5b266d` and the
50,334-entry core reaches a clean live Town view with full quest dialogue.
Holding the real MOVE overlay right for fourseconds visibly walks Felix and
scrolls the camera. Continuing right exposes executed target203d54a0; a bounded
six-entry table batch now brings all SDK archives to50,340 entries. This is
a movement result, not combat or sustained gameplay acceptance. The fixed-time
native exploratory route instead selects Inn Options and is stopped with no
runtime error; its input intent does not prove an adventure was reached.

`DCP_CHECKPOINT_SETTLE_FRAMES` can extend neutral post-marker advancement from
the unchanged120-frame default up to3600, with minimum120. Existing trace,
vblank and wall-clock budgets remain. Reports include observation anchors and
the requested interval. All37 configuration checks,32 actual-runtime negative
checks/eightcases,95 input checks native/ASan+UBSan, all SDK builds and27 runtime
lifecycle sessions pass (maximum stop69.73ms). A600-frame Town capture is in
progress to inspect the later scene. No rendering change is inferred yet.


## 2026-11-14 — Simulator Town and interactive navigation

Clean `1d0adf7` with the frozen 50,327-entry ordered core also passes visible
Town in iPhone Simulator: 5,411 rendered frames /151,366,149 trace entries,
checked advancement, no guest/frontend error, and an inspected Town/Felix image.
This is the simulator, not the physical iPhone. The final minimum-3-GiB adapter
also passes all27 lifecycle sessions with maximum stop44.94ms.

An isolated interactive simulator session uses the real touch overlay to load
DATA1/Felix, reach the Inn, pause through the native sheet, resume and confirm
Leave tavern. It records7,500 frames /290,264,330 entries before an explicit
missing trace at204db441. The independent native exploratory adventure script
stops at the same target, with Town visible in its final image. Movement,
combat and a completed adventure are not qualified by these runs. The next
bounded seven-case table batch compiles for allthreeSDKs, bringing the core to
50,334 entries; its adventure attempt is in progress.

A checked GOBLIN component observer is available for later coverage tests. It
requires independent original primary/control vtables, class tag, normal mode0,
and both delivered/stored post-initialization state0xa4 at the shared callback.
It preserves the separate Town predicate and rejects tutorial variants. This
marker means only that a plain Goblin component receives an active update;
it does not prove stage identity, enemy visibility, an attack, damage or victory.
No mobile Goblin checkpoint has passed yet. Generated original-code evidence
and game-specific traces remain private.


## 2026-11-14 — Physical save-dialog fix, cold reloads and native Town

Clean `4fd1796` with the qualified 50,292-entry core passes a 180.16-second
interactive run on iPad mini A20 Pro: 9,064 rendered frames, complete inspected
Create save data Yes/No image, replay median/p95/p99 9.68/10.13/10.17 ms, and
zero audio underruns/clipping. The final frame-arrival window has 16.67 ms median
and 33.34 ms p95. This qualifies the stalled prompt's improvement on hardware,
not sustained gameplay or a hands-on controller test.

The same physical working-save UUID is then reopened twice by new processes,
without restoring the seed: 5,479 / 5,421 frames and 127,649,949 /
126,660,272 translated entries in about147.36 /138.73 seconds. Both INN gates
pass with no error; both final images show Felix LV1 at Dragon's Haven Inn.
All ten preexisting save/checkpoint/image files keep their exact hashes. This
qualifies actual character reopening from the retained working folder, separately
from creating and saving new progress. Normal player and live Mac/iCloud saves
are untouched. The app returns to normal launcher mode after testing.

The iPad Simulator also reopens the same working copy after process replacement,
with no reseeding and unchanged source/normal roots. It passes both INN gates
(4,762 /4,613 frames); the second image shows Felix but some incomplete labels,
so general animation/text rendering remains open. Both physical reload images
are complete.

Three bounded original table batches bring all SDK archives to 50,327 entries.
The native arm64/Metal host now passes visible Town: Felix outside the Inn,
5,862 frames /145,518,001 trace entries in191.74 seconds, no errors and unload
acknowledged. A same-coverage unrestricted-mapping baseline also passes Town
(5,662 frames /188.55 seconds); extra table entries establish that progress.
The new ordered DirectMemory adapter independently preserves the original
unsigned relative-address contract, using a 4 GiB reservation and about2.04 GiB
in that Town run. The earlier intermittent string fault's exact failing resource
operands remain unresolved; no causal claim follows from two healthy runs.

DirectMemory fixtures pass 563 checks native/ThreadSanitizer and all SDK
compilations. Bounded configuration/runtime negatives pass24/24, input services
95 native/ASan+UBSan, and actual runtime lifecycle passes all27 sessions with
maximum stop38.42 ms. The original mapping trio needs502 MiB; initial explicit
maps need1,424–1,616 MiB, but later caller-sized maps make lifetime usage an open
qualification item. A 3 GiB fallback replaces an insufficient2 GiB fallback.
Physical Town/new memory-adapter validation and adventure/combat remain next.

## 2026-11-14 — Save-dialog rendering stalls reproduced and reduced

An isolated interactive iPhone Simulator run reproduces the player's Create
save data prompt with real platform audio. A native sample attributes nearly
all main-thread samples to texture lookup: AOT copied and byte-hashed the same
full atlas for every glyph/sprite draw. The same frontend and frozen50,292-entry
game archive were used for three180-second runs; archived inputs retain hashes.

Per-frame snapshot reuse removes repeated glyph copies. Accelerated full
SHA-256 then replaces the large byte-serial FNV content hash. Final prompt
replay-pump median/p95/p99 are9.98/10.33/10.69ms in the rolling window, versus
baseline p99 about1,920ms. The final69.09-second interval renders at59.46fps.
The final SHA variant's PNG was inspected and shows the complete Yes/No prompt.
This is a Simulator result, not a physical-iPad performance qualification.
The intermediate cache-only variant's final PNG has incomplete prompt text;
no pixel-equivalence or general gameplay-rendering claim follows from these
snapshots. Further scene/animation verification remains required.

Live SHA-run audio samples at84/111seconds have zero underruns; shutdown's final
counter includes768 underrun frames. Native and ThreadSanitizer concurrent
fixtures pass200frames ×128atlas draws with exactly200owned snapshots and no
reported race. The byte4096 regression verifies next-frame invalidation and
unchanged reuse. All36,864 saved RGB and1,504BC checks pass; allthreeSDK
renderer archives compile. Normal player saves and live Mac/iCloud data are
not used by the comparison.

Frontend diagnostics now time replay/present/heartbeat work. The explicit timed
interactive test mode keeps normal input/audio but isolated UUID saves. Resume
mode reuses an existing diagnostic working save without seeding it again and
stores new images separately. Six earlier Simulator configuration/resume cases
pass; actual character reopening from the same working folder is still pending.

## 2026-11-14 — Physical iPad INN and copied Felix verified

Clean frontend `0e15ee5` with the frozen 50,292-entry core passes the seeded
INN checkpoint on iPad mini A20 Pro. The isolated session records 123,268,154
translated entries, 3,467 rendered frames, one checked INN-ready observation,
no guest/frontend error and 144.97 seconds elapsed. The downloaded final Metal
PNG was inspected: Dragon's Haven Inn and Felix level1 are visible. Sampled
peak dirty footprint is about1.80GiB, thermal state nominal. This test uses
scripted input and paced null audio, separately from normal interactive Play.

Its three game save files retain their exact hashes after replacing the process
and relaunching the normal launcher. They remain in the diagnostic UUID folder;
this only proves retained files, not reopening that same working save or saving
new progress. A diagnostic resume mode is being tested for that separate check.
The seed and live Mac/iCloud saves are untouched. The normal player's save roots
are no longer assumed empty after the subsequent hands-on session.

The player reports extremely slow response at the fresh Create save data dialog.
Live diagnostics confirm serious interactive frame stalls with real audio active.
That performance issue takes priority over additional device gameplay testing.
The iPhone remains away and has not been contacted.

A 50,304-entry native Town attempt reaches the checked Town component, then
fails on missing `2045f21f` while the final image still shows loading. This is
not visible Town acceptance. An earlier run reported an invalid string pointer
at `2057cbef`; failure-only checked argument/caller/module addresses are now
included as `fault_context` to investigate recurrence. The later attempt does
not reproduce that fault; no cause or resolution is claimed.

## 2026-11-14 — Real INN and the copied character load on the native host

The frozen 50,292-entry core in `ios-game-50292` reaches Dragon's Haven Inn
from the quiescent Felix copy. Native Metal run `ios-host-50292/run-inn` passes
with 3,650 rendered frames / 114,571,041 trace entries in 168.21 seconds, one
checked INN resource-ready observation, acknowledged unload and no guest/host
error. Final PNG was inspected: the Inn interior and Felix at level1 are
visible in the party list. This qualifies native character reopening; fresh
progress/save/relaunch and physical INN are still pending.

The checkpoint driver now releases scripted buttons/sticks after observing its
requested component, retaining connection and continued real frame advancement.
The 95-check input/service suite passes native/ASan+UBSan, configuration/runtime
negatives remain24/24, and all27 lifecycle sessions pass (max stop65.3ms).
A fresh TITLE regression passes3,035 frames /77,075,990 entries in93.29seconds;
its inspected final image is the real title splash/PressX screen held by neutral
input, rather than automatically pressing into the menu. Interactive Play is
unchanged. The earlier physical title-menu result remains qualified.

TOWN component observation is connected from original class/vtable/state
evidence, separately from the Inn portal and constructor. A copied-Felix Town
run is underway. Its visibility/movement/combat have not passed. All three
SDK archives compile; the same seeded INN test is being packaged for the iPad.
Live Mac/iCloud progress is untouched.

## 2026-11-14 — Seeded device-test support and 50,285-entry core

The explicit seeded diagnostic variant restores a quiescent staged save into
a new UUID session through the existing validated checkpoint transactions.
Five Simulator cases pass: invalid durations, missing seed, bounded seeded stop
and normal launch. All three original disposable files retain their hashes,
as do the source and normal save/checkpoint sentinels. No character-reopening
claim follows from this frontend copy test. Both SDK builds pass; iPhoneOS
signature/profile checks pass. The previously qualified physical app is retained.

Private original-byte/provenance-window analysis supplies 296 direct-branch
continuations plus 15 closure targets. Thirty adversarial checks pass; the
existing-boundary/same-owner/direct-window rule reaches zero missing targets.
It is a compile-only overapproximation, not overall coverage or reachability.
A separate four-case backward-guarded table batch brings all three archives
to 50,285 entries. The 49,970-entry copied-Felix run reached 102,018,652 entries
and another loading frame before that table miss. INN visibility and character
reopening remain pending; the TITLE passes below remain the qualified baseline.

## 2026-11-14 — TITLE also passes both simulators; INN marker connected

The matching 49,492-entry core now passes in the iPhone and tablet simulators:
2,974 / 2,749 rendered frames, about 96 / 94 seconds, complete checkpoints
and no guest/frontend/save errors. Both final PNGs were inspected and show
the real title menu. These are simulator results, not physical-iPhone results.
The actual iPad result below remains the qualified device baseline.

A 239-destination batch covers the next long owner's direct continuations;
49,731 entries now compile for all three SDKs. The new INN observer checks
its original primary/control vtables, class tag, normal mode and stored0x81
at the resource-ready transition helper. Constructor state0, an audio cue or
just a resource open cannot satisfy it. It records initialization completion,
not screen visibility, and uses no remembered object address. Readback and
actual character reopening remain separate. A copied-Felix Metal run is
underway; INN has not yet passed. Headless/configuration/service tests remain
24 / 87 / 24 checks, native/sanitizer service runs pass, and 27 lifecycle
sessions pass (max stop 127.7 ms under concurrent simulator startup).

## 2026-11-14 — Visible TITLE passes on the physical iPad

Clean frontend `062a77d99e76a5e233cccf0d2ba08f31d753ee1b` with the frozen
49,492-entry `ios-game-component-20261114` core passes on iPad mini A20 Pro
(`iPad19,1`, Apple A20 Pro GPU). The automatic disposable session executes
89,813,167 trace entries, renders 3,189 game frames, makes 3,541 drawable-present
calls and completes the verified TITLE-component checkpoint with no guest,
frontend, platform or save error. Downloaded final Metal readback was inspected
and shows the real Dragon's Crown main menu. Total session is about 95 seconds.

Sampled peak dirty footprint is 1,813,237,448 bytes (1.69 GiB); thermal state
remains nominal. Captured-frame median is 16.67 ms with p95 about 50 ms across
startup; this is not a sustained gameplay performance claim. Default signing
works without increased-memory entitlements. Three game-generated files remain
in the diagnostic session's save tree, while ordinary ProbeSaves contains no
files. The app is relaunched normally after the check, leaving Play available.
This checkpoint uses paced null audio and scripted pad input; audible output,
physical input/reconnect and gameplay remain unqualified. The iPhone is away
and has not been contacted. Evidence is `ipad-title-verification.json`, final
diagnostics and first/final PNGs in the private external workspace.

The matching native arm64 Metal host also passes: 3,385 rendered frames,
85,649,897 trace entries, no error and a visually inspected title-menu frame.
A copied-Felix INN run gets beyond TITLE but reports missing `2002ef6d`; INN
component mapping and save reopening remain work in progress. The full expanded
Simulator core is compiling. Live Mac/iCloud saves and the installed Mac app
remain untouched.

## 2026-11-14 — Isolated automatic startup checks and component evidence

The 49,492-entry Mac/iPhoneOS core now compiles. Its preceding frozen Metal
run renders 3,612 frames / 40,261 draws, acknowledges unload and has no host
error, then stops at an unsupported Toolkit2 clear-presence call. Loading
content is visible, but the last inspected image is black; no visual TITLE
pass is claimed. The offline clear-presence adapter now mirrors the reference
call's unused request/future arguments and idempotent already-absent state.
The service suite passes 87 checks native/ASan+UBSan, configuration passes 24,
and six bounded negative runtime cases pass 24 checks. All 27 actual-runtime
lifecycle sessions pass (max stop 58.6 ms in this concurrent run).

Original-byte and relocation review found that the old named scene hook is
an audio-cue selector, and Remill can inline its numeric wrapper. It no longer
satisfies checkpoints. The new TITLE observer verifies the actual component's
phase-dispatch entry, original primary/control vtables and active update phase;
its initializer loads STAGE/TITLE locale resources. This is a component-state
marker, explicitly separate from visual TITLE. Runtime exports that distinction
and keeps visual verification false. The new real checkpoint run is pending.
INN's component identity remains to establish.

The explicit `--diagnostic-startup` app option starts a bounded test using its
own UUID save/checkpoint folder and a staged, validated input script. It writes
first nonuniform/final game images into the same session folder. A 27,507-entry
Simulator core tests the new frontend: 376 real rendered frames, isolated PNGs,
and two unchanged normal-save/checkpoint sentinel files. Missing input rejects
with zero frames and no stale session image; normal launch still waits for Play.
These frontend tests do not qualify the newer core.

The physical iPad still has the clean `80228bb` / 48,367-entry game candidate
at Ready to play; its automatic diagnostic replacement is being prepared.
No physical game-frame result is claimed yet. Its separate expanded CPU probe
passes 1,906 checks. Live Mac/iCloud saves remain untouched.

## 2026-11-14 — Expanded CPU suite verified on the returned iPad

The player reconnected the iPad mini A20 Pro. Clean source `cc0d767` replaces
its earlier probe without uninstalling the data container. Fresh diagnostics
identify `iPad19,1` and pass all 1,906 checks across 85 authored translated
routines / 84 native-reference routines, with two workers and zero failures.
This is synthetic CPU execution, explicitly not a game-startup result. The
iPhone remains on its earlier probe and was not contacted.

The native core now links 48,366 entry points after a separate compile-only
prefetch of original direct branch destinations in a long unwind owner. A
matching real iPhoneOS archive now compiles for hardware startup/render/memory testing.
Generic full-width table-base copies and independent post-ADD cleanup now pass
118 checks / 109 owned functions. A separate proved32-bit index copy after
CMP passes 50 checks / 23 owned functions; conditional isolation/baseline
comparison passes 34 checks. A new executed branch is being added before
packaging.
TITLE/INN, physical game frames and actual save reopening remain unverified.
The desktop app and live Mac/iCloud progress are untouched.

## 2026-11-14 — Startup coverage, bounded dispatch and real frame checks

The native corpus now links 47,980 entry points after scheduled-comparison,
termination-import and guarded/conditional prefetch work. The scanner regression
passes 74 checks across 65 owned functions, conditional isolation passes 34
checks across 11 functions, and import derivation passes 23 synthetic checks.
Derived stack-failure thunk declarations bind checked ELF/GOT/PLT bytes to the
typed fatal service, with content-addressed policy snapshots in bootstrap.
Declarations mean noreturn, not nounwind; host diagnostic unwinding remains valid.
The service/input suite now passes 82 checks native/ASan+UBSan and all SDK compiles.

A review found the scene observer reading the scene-manager object in RDI rather
than the scene name in RSI. Native Mac probe logs confirm RSI; the correction and
report marker are now in the real runtime. Completion also retains late worker
errors instead of suppressing everything after its checkpoint flag. The lifecycle
fixture exposed an initialization-timing assumption; it now waits for observed
translated progress on resume, and all 27 sessions pass. The current corrected
runtime still has no accepted TITLE/INN checkpoint.

An independently decoded 8 KiB owner explains a large remaining cluster: 125
relative-table slots / 98 distinct cases, plus a default and shared loop head.
Its 1,869 decoded instructions have no errors, overlaps or unresolved branches.
Those destinations and two reviewed factory groups add 126 missing traces; the
next fresh boot is running. Generic base-copy/post-ADD recognition is being
prepared separately. Latest frozen Metal evidence remains loading content;
no physical device, live Mac save or iCloud progress was touched.

Bootstrap now reproduces the generic internal-switch scan for both original
modules. The owned XED scanner/validator checks immutable table bytes, full-index
bounds, copy/zero-extension dominance, alternate entries and instruction overlap.
Review found and fixed three direct/table-edge proof-bypass counterexamples
before integration. Its 36-function matrix passes 45 checks: 13 accepted owners /
14 tables, 23 rejected owners, and four explicitly incomplete candidate owners.
The latter keep unresolved/external PCs in separate metadata and never count as
complete CFGs. Bootstrap may precompile those local candidates; actual dispatch
and checkpoint acceptance remain unchanged. Fresh scans identify 3,573 missing
prefetch destinations; a 3,952-target union with reviewed private tables is now
compiling. The current 41,688-entry runtime still reports loading-stage targets,
including the same `204dd973` in fresh and disposable Felix-seeded runs. Neither
TITLE nor INN/save reopening has passed.

Packaging now retains the actual version/build substitutions instead of letting
the shell execute their names. Built bundles report version 0.2/build 1, and
post-build verification records signature/profile memory-capability status.
The ordinary device app signs with the cached profile; an explicit
`DCP_EXTENDED_MEMORY=1` request currently fails because Xcode reports No Accounts
and the wildcard profile lacks those capabilities. The unsigned simulator
variant compiles with both entitlements requested. Diagnostics now include
remaining dirty-memory allowance and physical memory. Neither simulator memory
nor a successful signature proves hardware memory availability.

The post-unwind private table review expands static coverage to 41,580 entries.
A frozen 36,289-entry Metal run draws 2,080 frames / 32,709 draws, without a
frontend error, before an internal case at `2006bab3` (now compiled). Loading
content is still visible; TITLE/INN remain unverified. Original 1,800-vblank tests
were too short for fresh startup, so the default now permits 7,200 while explicit
negative budgets remain tested. Further startup branches vary with worker
scheduling and are discovered under longer actual pad-input scripts. Partial
mobile object caches are retained pending a useful runtime checkpoint.

The mobile vblank timer now uses absolute, pause-excluded monotonic deadlines
instead of appending a full 16.7 ms sleep to each frame's computation. The owned
deadline/timing fixture passes 1,006 checks, with 60 waits plus 5 ms inter-frame
work taking 1.002 s. Real-runtime lifecycle repeats all 27 sessions, maximum
stop 37.8 ms; six bounded headless cases and input/configuration fixtures still
pass. This corrects a pacing error, without claiming game or device FPS.

The validated sectionless-ELF unwind reader now identifies 32,282 function
entries in the pinned modules, exactly matching independent LLVM unwind output.
Twenty-nine owned synthetic ELF/CFI cases cover malformed metadata and valid
zero-address/null-pointer distinctions. Static indexing is not an execution
claim. The expanded corpus has 35,976 entries; all used dependency records are
current, while unchanged non-FP80 traces retain their original generation
backend records explicitly. Full linked runtime verification is continuing.

The cold libc corpus exposed mask-144 FP80 classification and extended-format
remainder operations. Integer-only classification passes 5,325,954 native LLVM
and 42,275,840 lowered arm64 checks. Production/lowered remainder each pass
554,624 rows, flags and concurrent state checks; an independent exact finite
oracle has zero differences. Rosetta FPREM1 differs on 2,896 rows and is recorded
separately, not copied into the implementation. Other classification masks and
unknown transcendental operations remain explicit failures.

Six indexed libc functions also require the Remill CAS128 reference-operand
ABI. The checked serialized bridge now passes full-width success/failure,
expected/desired preservation, bounds, cancellation, canaries and an eight-worker
counter carrying across 64 bits. Memory fixtures pass 251,076 checks in each
native/ASan+UBSan/TSAN run. A translated LOCK CMPXCHG16B fixture matches the native
oracle, bringing the CPU corpus to 1,906 checks / 85 routines (84 native-oracle
routines), with all three SDKs compiled. The six private functions are compiled;
this does not claim they have executed in the game.

Clean frontend `7c43b4e` with the 27,507-entry artifact now renders the real
loading image on both simulators. Phone/tablet replay 455/376 frames and
7,290/5,939 draws, report nonuniform snapshots, pass libc start/64 strlen checks,
and stop at the already covered `20428a47`, with no frontend error. Readback
shows the fairy and loading text during its initial fade. Simulator-only audio
muting keeps these unattended runs quiet while preserving PCM consumption and
counters; it is not audible-quality acceptance. Both complete app builds pass,
and the iPhoneOS signature verifies. Evidence is `ios-app-frames-simulator/evidence`.
No actual device was contacted. TITLE/INN remain open.

Further libc prefetch exposed a missing CAS8 bridge at link time. Checked CAS8
and CAS16 now share the serialized template used by CAS32/64. The expanded
memory suite passes 251,067 checks under native/ASan/UBSan/TSAN; translated
byte/word LOCK CMPXCHG routines bring the CPU corpus to 1,884 checks / 84 routines
with 83 native-oracle routines. Mobile SDK compilation passes. Main discovery
has passed 31,000 entries and exhausted ordinary direct-call/relocation
prefetch; additional function-pointer origins are under review. No wider lift
window is justified by the sampled gaps: all seven nonaligned demands reviewed
are switch cases already inside the original slices.

The first recognizable AOT game image now passes native Mac readback: the
27,507-entry frozen artifact renders the real animated fairy and NOW LOADING
text. The host replays 580 frames / 8,276 draw calls, sees nonuniform RGB content,
and acknowledges ordered unload in 20.06 seconds before missing `20428a47`.
Actual libc start remains 0 with 64 strlen checks. This is the same AOT path
used by mobile, with native SDL/Metal replay; Rosetta does not execute this run.
TITLE/INN have not been reached, so the host correctly returns failure despite
visible loading content. Evidence is `ios-host-frames/run/frame-final.png` and
`report.json` in the external workspace. Device/simulator builds of this frozen
artifact are in progress. Physical devices remain away.

A separate conditional catalog supplies 25 narrow-guard tables / 291 exact case
and default PCs, with 6,856 CFG instructions checked. Each records its unproven
high-index-zero condition. These are optional static case precompilations, not
proven dynamic bounds or executed paths; runtime dispatch is unchanged and any
other actual target still fails explicitly. The original strict 111-table
catalog remains intact. An end-to-end seeded discovery attempt also verifies
all three disposable Felix files and its adjacent SHA manifest, then stops at
its configured round limit without claiming boot acceptance.

Discovery can now seed each attempt from a quiescent disposable save copy,
with bounded regular-file inventory, independent copy hashes and per-attempt
manifests. Twenty-eight filesystem checks pass, and the earlier disposable
Felix snapshot copies with all three files verified. This prepares repeatable
INN/save work; it does not establish that the AOT game has reopened that save.
Live Mac/iCloud progress remains untouched.

Headless scene selection and scripted input are now connected and verified.
The default stays TITLE plus 120 advancing flips/vblanks; INN is an explicit
alternative. Configured names and positive finite budgets are validated, and
input uses the desktop parser with an exact file SHA-256 in the report. Empty
scripts mean idle. Tests pass 24 config and 80 pad/script checks (also under
ASan/UBSan), all three SDK compilations, and six actual-runtime bounded/negative
cases with 24 checks. None reports boot acceptance. A repeat of the actual
runtime lifecycle passes 27 sessions with maximum stop 10.2 ms, and storage
still passes 137 core / 108 service checks over four simulated relaunches.
See [unattended checkpoints](game/headless.md).

The independent static table catalog validates 111 guarded tables, 5,117 entries
and 1,395 unique targets including defaults, with 29,386 case-CFG instructions
checked. 1,330 targets were prefetched in a parallel batch. Ambiguous low-byte
guards and unresolved indirect exits remain rejected; static inventory does not
claim execution. The current corpus has 22,597 entries, current provenance and
three explicitly marked bulk libc adapters. Startup discovery continues; TITLE,
INN and nonuniform game images remain unverified.

Clean frontend `79fc148` now builds for both iPhoneOS and Simulator; the signed
iPhoneOS bundle verifies. The 13,307-entry artifact is linked into both. Phone
and tablet simulator runs each pass real libc startup and 64 strlen checks,
start 24 workers, and replay 106/98 captured frames before missing `2006d309`.
That target is already covered in ongoing discovery. The exported artifact
identity matches the packaged manifest. Both runs explicitly report no
nonuniform snapshot, with no frontend error. Their viewed control overlay is
intact. These are simulated iPhone 17 Pro Max/iPad mini A17 Pro results; no
physical installation is claimed. Evidence: `ios-app-stage-simulator/evidence`
in the external workspace. Recognizable game images are still outstanding.

Trace lifting now also uses bounded independent processes. Duplicate address
spellings are normalized before tasks start. Each successful trace is instrumented
and its provenance committed individually, so a failed peer does not erase that
evidence. A separate invalid-entry batch test confirms this behavior; invalid
code is not published. A successful batch verifies current provenance across
13,557 entries, and subsequent discovery has passed 16,000 entries. This larger
static corpus does not establish that every prefetched function executed.

UIKit name entry now uses a checked IME request/completion adapter. UI callbacks
copy owned UTF-16 text; only the guest poll commits it to validated game memory.
Cancel/abort preserve the original buffer, stale session IDs are rejected, and
Unicode truncation preserves surrogate pairs. Native, ASan/UBSan and TSAN each
pass 495 checks with 100 concurrent completions, plus all three SDK compilations.
The latest frontend compiles for both iOS SDKs. Real character-creation keyboard
acceptance remains open. See [name entry](game/ime.md).

Four guarded switch families were independently resolved and their missing
unaligned targets translated in one batch. Independent trace compilation is now
bounded by `--jobs`; object publication is atomic. LLVM response-file archive
inputs avoid the process argument limit hit by the larger frozen corpus. A
13,307-entry frozen Mac run captures 251 drawn frames, still uniformly black,
before the next missing target. Discovery and device/simulator cross compilation
continue; no recognizable image or TITLE result is claimed.

The goal remains active while the reference devices are away. The sixteen-patch
backend adds observed ROUND, packed float and packed integer forms. Rounding
passes 13,824,000 masked native-oracle comparisons, 2,560,000 suppression
invariance checks and two fault-before-write cases. Another 10,208,000 packed
value/control checks pass. Rosetta forces MXCSR exception masks; unmasked ROUND
checks are separate AOT properties. Packed float sticky exception flags and
architectural guest #XM delivery remain outside this coverage. See
[review boundaries](cpu/review/confirmed-simd.md).

A real worker exhausted its native stack through a non-tail Remill jump bridge.
The production bridges and game dispatcher now require tail branches. A lifted
regression executes 1,100,003 entries per route on a 128 KiB requested pthread
stack, with zero entry-SP growth and 127 checks passing. Disassembly also verifies
the real game dispatcher's service B and trace BR. See
[tail dispatch](cpu/review/tail-dispatch.md).

The game then exercised per-worker cancellation, which previously stopped the
entire runtime. Cancellation now exits only the selected worker with the PS4
canceled result 1, wakes parked execution, preserves TLS cleanup and retires its
notification context after successful joins. Native, ASan/UBSan and TSAN variants
each pass 885 adapter checks. A repeat of the actual-runtime lifecycle fixture
passes 27 sessions, with maximum measured stop latency 30.2 ms in this run.
These are startup and cleanup results, not physical-device gameplay acceptance.
See [cancellation scope](game/thread-cancel.md).

Discovery has progressed beyond 12,000 trace entries with current backend
provenance. A frozen 5,435-entry archive produces 197 captured/drawn frames and
985 submissions before the next missing target. Its actual PNG readbacks remain
uniform black; TITLE, INN and recognizable real-game images have **not passed**.
The native Mac reference renders the create-save prompt at vblank 200. The AOT
runtime still needs additional initialization paths before that comparison can
establish matching content. Live discovery continues in the external cache.

The host now requires nonuniform RGB content, drawn frames and the runtime's
TITLE checkpoint for success. It saves a final image before ordered unload.
The simulator preserves the first nonuniform game snapshot rather than freezing
its initial black frame. Uniform-render fixtures still pass and explicitly skip
the real-game content requirement. Game library manifests bind the three copied
archives to their platform, trace hash and backend; packaging checks their SHA-256
values, and exported app diagnostics retain the artifact identity. Both iOS SDKs
compile the latest app source. A fresh complete app link is still pending the
next useful frozen corpus. The devices retain the older probe.

## 2026-11-13 — Real modules and mobile host integration in progress

The active goal is to complete the iOS port as far as possible while the physical
devices are away. The last hardware-installed build remains the earlier CPU
probe. All new verification in this section uses the arm64 Mac or iOS Simulator;
it does not establish playable performance or acceptance on either physical device.

The real-module AOT runtime executes the supported libc and eboot, applies
relocations, handles independent guest stacks/TLS and starts the game's workers.
Real libc startup returns zero and its actual strlen passes 64 cases against a
native x86 reference. Execution is progressing through game initialization with
explicit missing-target diagnostics. TITLE/INN boot and live game frames have
not yet passed. The two complete CPK archives were previously copied into each
device's probe sandbox, but the new game runtime has not been verified there.

The expanded CPU corpus now passes 1,840 checks across 82 routines and two workers
on the arm64 host; 81 routines use a native Rosetta x86 oracle. This includes
concurrent 32/64-bit CAS, exact/bounded 80-bit memory bridges, string comparisons/masks, packed saturation, lane insertion,
blending, shuffles, reciprocal/rsqrt corner cases and MXCSR rounding-mode
round-trips. An independent 35-form full-YMM review passes 6,720 checks, including
legacy upper-lane preservation, VEX clearing, memory operands and 256-bit lane
locality. It found and fixed legacy BLENDPD selector names and the RCPPS signed-zero
boundary at magnitude 2^126. Complete x86 and floating-point-environment coverage
is still outside these tests.

The packed-shift/VPBLENDVB differential suite passes 46,720 full-YMM checks
across 73 routines and 80 vectors. VEXTRACTF128 fixtures cover register/memory
forms, immediate lane selection and upper-register clearing. Software x87
uses SoftFloat 3e and an LLVM pass that replaces executable FP80 values with
exact i80 storage. Direct and lowered-IR suites each pass 153,228 native-oracle
checks; the real libc FSCALE routine passes 19,800 vectors. Unknown extended
operations fail explicitly. A frozen 2,355-trace candidate builds for all three targets. Its real-module
simulator runs reach libc start and 18 workers on iPhone 17 Pro Max and iPad mini
(A17 Pro) simulated models, then stop at VMOVMSKPS, matching the arm64 host.
The new movemask patch passes 38,400 full-register native-oracle checks; the
nine-patch full refresh passed. Four further statically observed SIMD groups
(variable blends, lane insert/extract, 128-bit lane permutations and dword
shifts) pass 1,199,616 differential checks across 213 routines; their thirteen-
patch full refresh now covers 3,033 current-provenance traces, with discovery
continuing. Static presence is separate from execution. These are simulated models, not the
physical iPad mini A20 Pro.
See [FP80 boundaries and evidence](cpu/fp80/README.md).

Startup has reached one vblank and over four million trace entries. It now
stops at VROUNDSS at `20639719`, with zero flips and no actual game frames.
Three verified bulk libc exports use checked native copy/fill adapters, marked
explicitly in provenance; a comparison flag restores original translated code.
Both modes reach the same rounding gap with actual strlen/startup still passing.
The production-wrapper ABI suite passes 822,659 checks in native, ASan/UBSan and
TSAN runs, plus all three SDK compilations. See [bulk libc](game/libc-bulk.md).

`game/build.py` produces Mac, iPhoneOS and simulator libraries. `advance.py`
discovers executed targets in batches, stops explicitly on other errors and
supports graceful stopping. Prefetch includes aligned possible callbacks from
bounds-checked relocations; four unaligned constant pointers were quarantined
without removing executed demands. Generated traces now carry input/slice/backend/IR
SHA provenance. A full relift replaced the earlier unknown-provenance host
corpus; later generated entries use the same checked backend. All private images,
IR, code archives and game assets remain in the external build tree.

A sampled startup showed range scanning and nested lock acquisition dominating
CPU use. A bounded eight-range cache retains the existing synchronization and
removes redundant recursive locking. Independent native, ASan/UBSan and TSAN
runs each pass 251,055 checks, including concurrent append, registry replacement,
bounds, cancellation and CAS32/64. A controlled lookup benchmark shows strong
hot-set gains but a 59% deliberate cache-thrash regression; neither is a measured
game-frame result. See [memory review and limits](cpu/review/memory-cache.md).

The new UIKit game launcher links the CPU runtime, in-process SDL Metal replay,
touch/GameController input, AVAudioEngine output, Files import and local save
transactions. Its iPhoneOS Release build and signature verify; the simulator
build links and launches. This is a development app, with a separate bundle ID
`com.wadscorp.opencrown.mobile`. No new physical install is claimed. The earlier
probe remains available through `build-probe.sh`.

Rendering reuses the existing draw-capture and Backend interfaces with signed
trace-entry observers instead of executable x86 patches. Guest memory snapshots
are synchronized before hashing/detiling. Renderer fixtures pass 32,768 RGBA
checks, 36,864 saved-image channel checks and 1,504 native/CPU-decoded BC checks.
The concurrent snapshot fixture passes 200 frames under ThreadSanitizer. Mac GPU
and texwatch tests pass after the guarded shared-source changes. These are
synthetic rendering results, not live game scenes.

Simulator integration exposed two distinct startup problems. Its reported GPU
families are Apple1/Common1, so a narrowly marked simulator-only SDL patch permits
the renderer's basic draw subset; physical iOS keeps SDL's original hardware
check. CMake's SDK selection also misidentified normalized iPhoneSimulator paths
and embedded device shaders. Case-insensitive selection plus explicit AIR
targets now pass all 24 Mac/device/simulator artifact checks. The assembled
simulator app initializes Metal and presents while the guest starts; no captured
game frames have yet been observed. The 2,355-trace app presents 478/506 blank
drawables on phone/tablet simulators before the CPU gap; these are explicitly
zero captured game frames. Pause and touch controls appear above the Metal view.
The one-second live heartbeat reports startup progress before the first frame,
and retains the final guest error/status for Share after shutdown. Presentation
counts alone do not prove that the game has rendered.

Host input/audio tests cover four pad slots, multitouch serialization, disconnect
state, clipping/downmix and 400,000 concurrent ring-buffer frames. The sink requests
48 kHz stereo and 256-frame I/O periods, and records observed periods/underruns.
The iPad simulator also exposed portrait overlap between Menu/Pad and the right
stick buttons. Compact layouts now move Menu/Pad above the control clusters
and keep R3 separate; a new rendered UI snapshot verifies the adjustment.
Actual device sound, touch ergonomics, physical controller reconnect and audio
interruption recovery remain unverified. Real-runtime lifecycle tests pass 27
sessions, including parked stop, repeated pause/resume, concurrent-entry rejection,
pre-start cancellation and ordered renderer teardown; maximum measured stop
latency is 3.85 ms in that run.

Storage passes 137 core/retention checks and 108 integrated savedata/file-service
checks across four simulated relaunches. Writable mounts take a validated
pre-write checkpoint; completed unmounts checkpoint the new tree. Interrupted
writes roll back on relaunch while retaining the partial tree. An exclusive file
I/O gate prevents snapshots racing open/write/close. The latest ten validated
completed checkpoints are retained; interrupted copies remain. Import validates
the exact executables, archive sizes and TOC bounds before atomic promotion.
These results do not establish physical-device Files-provider behavior or cloud
handoff. Live iCloud saves remain untouched.

See [bootstrap](BOOTSTRAP.md), [real-module harness](game/README.md),
[renderer](render/README.md), [platform adapters](platform/README.md),
[storage](storage/README.md), and the [acceptance plan](../docs/ios-ipados-port-plan.md).

## 2026-11-13 — Translated routines execute on both devices

Implementation 31ffb1e adds the checked guest runtime and eight Remill AOT
synthetic routines to the signed arm64 app. Both the iPhone 17 Pro Max and
iPad mini (A20 Pro) execute the same corpus with 146 checks passing, zero
failures and two workers. Each passes again after a terminated-process relaunch,
and its persistent launch count advances from 3 to 4. All runs use devicectl
without LLDB. The app's recursive signature verifies.

The arm64 Mac harness reports exactly the same results and corpus hash:
8447df7e24d3edc8c43647f2beb892df881aa3afeb314eb74ecc9de2c56a81f7.
Seven routines compare with actual native x86-64 execution under Rosetta.
The eighth uses seeded per-worker guest FS/TLS memory and expected arithmetic.
Each worker executes eight input vectors per routine, plus nine memory/dispatch
checks. Arithmetic flags compare CF/PF/ZF/SF/OF where defined and selected by
the fixture. Coverage includes branching, rotation, unaligned reads/writes,
direct/indirect guest calls, stack/return state, AVX XOR and guest TLS isolation.
Invalid reads/writes, final valid qword, unresolved targets and a translated
out-of-range memory access are checked. Runtime bridges record faults without
unwinding across nounwind LLVM intrinsics.

Build with ios/cpu/build.py, then pass its output directory as the third
argument to ios/build-probe.sh, as documented in README.md. The build runs the
native oracle, generates the expected vectors and dispatch table, tests the
arm64 Mac library and produces a static iPhoneOS library. Remill's example
lifter leaves some callee bodies external, so the builder explicitly lifts
those entry points and verifies them through real guest call/return execution.
The generated tables, IR, objects, oracle results and device JSON stay external.

Private evidence is in cpu-harness/ and ios-probe/ under the OpenCrown build
root. mac-results.json, verification.json and cpu-results/cpu-relaunch device
JSON files identify source revision and corpus. ios-cpu-app-build.log records
the provisioned Release build. The probe displays and exports its CPU report.

P1 remains open for real eboot/libc routines, atomics, guest-to-HLE calls and
callbacks, broader SIMD/flags coverage, performance and interpreter comparison.
These eight routines are synthetic. The complete game has yet to boot on a device.

## 2026-11-13 — P0 hardware shell and initial P1 lift

The signed arm64 shell is installed on both reference devices as OpenCrown Probe
(bundle ID com.wadscorp.opencrown.probe), from clean source commit 4f8caea.
Xcode Release build and signature verification pass. devicectl launches without
LLDB succeed, then repeat installation and terminated-process relaunch increase
the persistent launch count from 1 to 2 on each device. Retrieved JSON files
contain the expected build ID and machine. The player confirms Home Screen
launch, displayed diagnostics and the share sheet on both devices.

| Device | OS | Page size | Metal device | BC capability query |
| --- | --- | --- | --- | --- |
| iPhone 17 Pro Max, iPhone18,2 | 27.2.1, 24B156 | 16384 | Apple A19 Pro GPU | true |
| iPad mini (A20 Pro), iPad19,1 | 27.2.1, 24B8156 | 16384 | Apple A20 Pro GPU | true |

P0 is complete for the device shell and existing reference inputs. A private
manifest pins the matching eboot/libc hashes, input script, Felix payload hash
and installed Mac reference log containing TITLE at vblank 677 and INN at 1322.
The fixtures use the already verified Mac runtime at 784fe46. The diagnostic
screen explicitly reports guest_execution=false. GPU capability queries still
need actual compressed-texture draws in P3.

### P1 backend work

Built Remill at 66c488c9911fd9b368e4d0c9647d74239f8bba8b with Homebrew LLVM 23.1.5.
Its upstream dependency superbuild completed using external LLVM. The main build
initially failed because LLVM 23 removed convert_from_fp16/convert_to_fp16.
The adjacent remill-llvm23.patch handles half-precision memory loads/stores with
raw i16/half bitcasts for LLVM 23+, retaining the older code on earlier LLVM.
The patched full build succeeds. Half-value runtime regression coverage is pending.
[Remill source](https://github.com/lifting-bits/remill/tree/66c488c9911fd9b368e4d0c9647d74239f8bba8b)

The first synthetic fixture, mov rax,rdi / add rax,rsi / ret, lifts to LLVM IR
and compiles into an arm64 iOS Mach-O object. Reproduce with probe-lifter.sh.
Its undefined-symbol list exposes the runtime bridges still needed for flags,
guest return, stack reads and FS/GS/stack initialization. The object has yet to
be linked into the mobile probe and executed against reference vectors. Remill
also logs a redundant return-terminator diagnostic for this trace, so inspect
control flow while implementing that harness. Translation output alone does
not complete the P1 CPU proof.

The locally available source-recomp translator uses i386 registers and 32-bit
guest addresses. Its current CPU runtime requires substantial changes for this
x86-64 guest. Remill is the first candidate under experiment. Interpreter
comparison, SIMD/flags/atomics/callbacks/TLS, two-thread device tests, and real
routines from both game modules remain in P1. Backend selection is still open.

### Reproduce the host lifter build

Use a mounted external build root. In a fresh Remill checkout at the pin above,
apply ios/remill-llvm23.patch from this fork with git apply, then follow:

```sh
cmake -G Ninja -S remill/dependencies -B remill-deps -DUSE_EXTERNAL_LLVM=ON -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/llvm -DCMAKE_INSTALL_PREFIX="$PWD/remill-install"
cmake --build remill-deps --parallel 8
cmake -G Ninja -S remill -B remill-build -DCMAKE_PREFIX_PATH="$PWD/remill-install" -DCMAKE_BUILD_TYPE=Release
cmake --build remill-build --parallel 8
/path/to/OpenCrown-PC/ios/probe-lifter.sh "$PWD/remill-build/bin/lift/remill-lift-23" /opt/homebrew/opt/llvm /path/on/external/disk/fixtures
```

Private evidence stays under the external OpenCrown build root in ios-probe/
and ios-cpu-fixtures/, with dependency source/build caches in cpu-backends/.
The device JSON, provisioning files and generated objects stay outside Git.
The next implementation task is a checked guest-memory/runtime harness that
executes lifted fixtures on the Mac reference and both signed device apps.
