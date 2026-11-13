# macOS gameplay and save smoke test

Verified on 2026-11-13 against source `784fe20`, supported CUSA10454 patch 1.03,
on an M5 Max running macOS 27.2.1 and Xcode 27.2. The x86-64 runner and guest
execute under Rosetta 2; SDL 3.4.18 presents through Metal. This was live
keyboard/mouse testing of the existing implementation; no runtime patch was
needed for this route.

## Reproduce with disposable state

Build using [the Mac instructions](README.md), then set both state overrides.
Use a new test directory, or a backed-up copy of a test save. Keep it separate
from player saves and cloud folders. The recorded run started with a copy of
the headless smoke test's save and created a new character through the inn.

```sh
mkdir -p /path/to/test-state/user /path/to/test-state/saves
ROSETTA_ADVERTISE_AVX=1 DCP_USER_DIR=/path/to/test-state/user DCP_SAVE_ROOT=/path/to/test-state/saves DCP_PROBE='68840*' DCP_STATS=1 MTL_DEBUG_LAYER=1 /path/to/macos-rendered/dcp_run /path/to/CUSA10454/eboot.bin --render --window --vsync --renderer gpu --size 1280x720 > /path/to/test-state/session-1.log 2>&1
```

Omit `--frames` and `--input` for live play. On macOS the window and SDL event
loop must stay on the process main thread. A temporary development app with a
compiled native executable launched this exact runner and environment during
the recorded test; that wrapper is not the planned packaged product or launcher.
Archive each log before relaunch if a wrapper truncates a fixed log path.

## Route and observed results

| Checkpoint | Observed result |
| --- | --- |
| Character creation | Fighter, default name Felix, Sterling Silver color, English voice, level 1 / 300 HP. Declined the level-15 story skip. Custom text entry was not tested. |
| Tutorial | Completed movement, jump/dash/evade, melee and air attacks, guard, Power Smash, crossbow, goblin/Orc combat, potion/food and door/chest interactions. Returned to the inn. |
| Hydeland town | Entered the Fighters Guild and Morgan's shop, repaired the starter Short Sword from 0 to 1000 durability for 100 gold, returned to the guild and accepted the first mission. |
| Adventure | Entered Ancient Temple Ruins on Normal. Advanced through goblins, caged fairy, snakes, spiders, traps, bats, adventurer bones and lizardmen to the Harpy boss. |
| Boss/result return | Fought Harpy and was defeated. Game-over, adventure-result and return-to-town transitions worked. This is not a completed adventure or boss-clear result. |
| Save | The game opened SAVE0.DAT for writing on return to town. Slot 1 subsequently displayed Felix, level 1, 47 gold and play time 0:53:55. |
| Reload 1 | Returned to title through the inn, quit the entire process, relaunched, loaded slot 1, left the inn and inspected character stats in town. Same progress recovered. |
| Reload 2 | Quit the entire process from the stats screen, relaunched again and repeated the slot/inn/town/stats checks. Same progress recovered again. Final normal quit also completed. |

In both fresh processes, the stats screen showed **Felix, LV 1, EXP 351,
NEXT 850, HP 300, STR 16, CON 14, DEX 12, INT 4, MGR 7, LUC 10,
Skill Points 1**; the character menu showed **47 gold**. Neither restart repeated
creation or the tutorial. Confirming these earned values distinguishes a real
progress reload from merely finding a save file.

The scene probe recorded this first-session sequence (vblank at dispatch):

```text
892 OPENING               2040 TITLE
3530 INN                  19994 CAMP
85214 INN                 85545 TOWN
92296 FIGHTERS_GUILD       93464 TOWN
96967 MAGICIANS_GUILD      100396 TOWN
107503 FIGHTERS_GUILD      108796 TOWN
112303 ENTRANCE            115060 TOWN
117376 ENTRANCE            122052 WORLD_MAP
123659 R_MYSTIC_RUINS_A    150922 ROOT_EVENT_01
152839 R_MYSTIC_RUINS_A    193583 GAME_OVER
194719 ADVENTURE_RESULT    197030 TOWN
199214 INN                201162 TITLE
```

The last periodic frame sample in each archived session was:

| Session | Flips | Vblanks | Submissions | Label writes |
| --- | ---: | ---: | ---: | ---: |
| Creation/tutorial/town/combat/save | 202680 | 203115 | 1013400 | 1418760 |
| First reload | 10260 | 10283 | 51300 | 71820 |
| Second reload | 15000 | 15030 | 75000 | 105000 |

All three logs end with `render: window closed`, with no SIGSEGV, SIGBUS,
FATAL or Metal validation error found. The runner process was absent after each
full quit. Exit status was not captured for these interactive window quits.
Visible frames were inspected during the route: readable menus/dialogue, town,
animated characters/enemies and combat effects. These observations extend the
previous title/inn captures; they do not replace matched Linux image comparisons.

## Save evidence and repeat procedure

Take a recursive backup of `DCP_SAVE_ROOT` after the game finishes saving, and
record byte counts and SHA-256 hashes. After quitting, verify the runner has
exited rather than only hiding its window. Relaunch with the **same overrides**,
load the same slot and compare in-game earned progress. Repeat the full
quit/relaunch once more. Hashes alone cannot prove the game loaded the data.

The following files in `SAVEDATA00/` matched the pre-quit backups after both
reloads and the final quit:

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| SAVE0.DAT | 2157336 | `400fed80dcf4b5f06486e36c2ae40087e60741a00a8bb32163758a4727889abc` |
| sce_sys/param.bin | 1328 | `7e27e0ec703452d1364d40eb6e61f251bb381164eed4450150e91337505bb932` |
| sce_sys/icon0.png | 49152 | `14aefa9dff816b9e87f1e540cd4d820370d3fdc3a52b7285aa0d9e08ce7f06e7` |

The backups and raw logs stay in external test output, not in the repository.
The save itself is not distributed.

## Input notes for repeating this route

- WASD moves on both axes; match enemy depth before attacking. Space confirms
  menus and jumps. Mouse 1 attacks; holding it guards while stationary or dashes
  while moving. Mouse 2 is Circle/cancel/Power Smash. E is Triangle/pickup.
- Arrow keys are the directional pad. Left/Right opens and changes the item
  selector; Down uses the highlighted item while the selector is open. Down
  with a closed selector did not use a potion. Potion healing and quantity
  decrement were observed in the tutorial.
- Escape opens the character menu. Disabled menu rows are skipped: at this
  checkpoint Equipment → Armory → Stats takes two Down presses. M opens the map;
  Tab maps the touchpad.
- IJKL moves the fairy pointer. Aim and interact promptly: it recenters after
  inactivity. Some interactions worked with Shift+E after aiming, but the
  precise timing and the shared Shift L1/R1 mapping need a separate input audit.
  No platform defect has been isolated from that observation.
- Town onboarding restricts travel and some inn/menu actions until the initial
  guild/shop/mission sequence. Obstacles and depth can block movement; use the
  lower path around them. These are not evidence of a renderer stall.

## Acceptance still open

The M5 **save progress / quit fully / recover twice** check passes on this route.
M4 and M5 remain open. Complete an adventure with a boss victory and return to
town during the required 60-minute run. Still verify custom name/chat overlays,
audio and drift, a controller with disconnect/reconnect, pointer behavior,
display changes, broader texture-tracking cases and matched Linux images.
Measure produced/dropped/replayed/presented frames and presentation percentiles;
the changing window-title FPS average is not a performance acceptance result.

These normal-quit tests do not establish crash/power-loss safety, short-write or
unwritable-path recovery, or damaged slot/PARAM/ICON handling. Test those using
separate copies. App packaging, signing, relocation and installation are also
outside this development-runner result. See [the milestone plan](../docs/macos-port-plan.md).
