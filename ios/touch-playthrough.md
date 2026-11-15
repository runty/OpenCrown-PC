# Fresh touch-only acceptance route

Run with a disposable diagnostic save root. Preserve normal saves and live cloud
progress. Record source/app/core hashes, device or Simulator identity, save-root
UUID and whether controllers are connected. A Simulator pass does not qualify
physical ergonomics, audio or sustained performance.

## Route

1. Start with no imported character. Create save data, choose Data 1 and create
   a Fighter. Open Edit name, erase the default with Square, select letters with
   the virtual D-pad and Cross, then use Menu to confirm. This build presents
   the game's own keyboard here; this route does not exercise UIKit IME.
2. Create/select the character and advance the Inn introduction. Complete each
   movement prompt: Move, Jump, Dash (move plus held Square) and R1 Evade. Keep
   two fingers down for direction plus action; confirm both the game result and
   concurrent input in the host snapshot when available.
3. Use repeated Square for normal attacks; combine Move up/down with Square for
   strike-up/sliding. Jump, then press Square in the air, with up/down as directed.
   Face the goblin and hold Square to guard. Complete Power Smash with Circle;
   wait for the dropped weapon's cooldown and use Triangle nearby to retrieve it.
4. Defeat the crossbow goblin, approach the dropped crossbow and press Triangle.
   Face and align with the remaining enemies, then fire with Square. Complete
   the potion lesson: cycle the selected item with D-pad right/left, select
   Heal Potion (S), then use D-pad down. Retain the completed checks and count.
5. Move through the marked exit. Break the wooden crate, approach/pick up the
   apple and stop moving/attacking to eat it. Retain the health increase.
6. Approach the locked door and advance Rannie's introduction. Let the camera
   settle. Tap the door's lock on the free game canvas; retain the two completed
   cursor/open checks and Rannie unlocking the door. Move up/right into it.
7. Advance the chest introduction. Let the camera settle, then tap its lock.
   Wait for Rannie to finish opening it and retain the open-chest image.
8. Defeat the Orc using the virtual controls. Retrieve dropped weapons after
   cooldown, heal as necessary and use the public Pause/Resume controls when
   inspecting between batches. Advance the return dialogue. Choose Leave tavern
   and wait for recognizable Town, not just the scene observer or loading image.
9. Preserve the newly written save and its hash, use Pause → Stop Game, retain
   final diagnostics and terminate. Cold-launch the same save root without a seed.
   Load Data 1, confirm the edited name/level/gold, leave the Inn and wait for a
   recognizable Town image. Repeat in another cold process and compare save
   hashes. Keep interrupted/early transition captures as excluded evidence.

## Simulator evidence, 2026-11-15

Clean packaged source `280b856a08eb7447102c8ace8285ff03fbbd2baf` and the
50,464-entry frozen core complete the route in the owned iPhone Simulator.
The unseeded save-data file starts at SHA-256
`625651e2de00fabd0837e7ffbb75e6c1c214e75dd5d2ceec04796a2b2220e0fc`.
The first attempt stops during the potion section when the host force-unmounts
a mapped backing volume; the crash report names that condition. Its save remains
unchanged. A new process reopens that same test root and recreates a Fighter,
edits its name to `R`, finishes the tutorial including door/chest taps and Orc,
and reaches Town. The save now has SHA-256
`df59b0681ff021bf4609361f0abb8d6b50f895a9902db701d09cda91bccb1fb4`.
No guest scene, cursor or save fields are patched.

The held two-finger Move-right/Square snapshot has button mask `0x8000`,
left X `239`, left Y `127` and all four physical pads disconnected. Multi-touch
synthesis uses the application's actual interface orientation; the earlier
hardcoded-portrait helper is excluded. The private Simulator shim only prevents
Mac-forwarded controllers. No helper or shim is shipped in the app.

The completed route captures 45,844 frames, 34,132 cursor samples and zero cursor
read errors; runtime/frontend/save errors are empty. The run includes pauses
and read-only input snapshots. Repeated audio restarts record AVFAudio `-10851`
and 5,632 underrun frames; this is not an audio or performance qualification.
The user's mute request is applied through the existing Simulator mute setting
and retained for later launches; source consumption/accounting continues.

Private screenshots, input snapshots, command journal, save copies and final
reports are under `cursor-investigation/touch-tutorial-driver/`. Three cold processes retain the exact hash; two wait for recognizable Town.
The UI helper exits 0 with one test, zero failures and successful teardown.
See the current [progress ledger](progress.md) for the individual results. Physical
fresh-character/adventure/save, controller/reconnect, audio recovery, sustained
thermal/performance and remote-provider handoff remain open.
