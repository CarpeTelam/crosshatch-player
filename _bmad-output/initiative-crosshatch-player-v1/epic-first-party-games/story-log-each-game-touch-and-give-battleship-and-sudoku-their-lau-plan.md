---
title: 'Log each game touch, and give Battleship and Sudoku their launcher icons'
type: 'feature'
ticket: '14'
created: '2026-10-10'
status: done
route: 'full'
route_source: 'auto'
baseline_revision: '1ccef0db7e55d87224f36c588133fc640178c9c2'
review: 'thorough'
review_source: 'pinned'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
followup_review_recommended: false
context: ['{project-root}/docs/crosshatch/game-canvas.md', '{project-root}/test/game_script/first_party/README.md']
warnings: ['oversized']
deferred:
  - summary: >-
      The device-run packet's Packages paragraph (and the packet's hand-run steps) still say little about where the new Battleship and Sudoku launcher icons and the touch lines show on the device, and cite no commit for the repack.
    evidence: |-
      Blind hunter finding; the orchestrator owns the packet and asked that only one sentence (the touch lines need the entry-14 firmware) be added to the paragraph. Battleship is hidden, but a hidden game still has a launcher row, as the simulator list shows.
    location: >-
      _bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/device-run-packet.md (Packages)
    severity: low
  - summary: >-
      The packet's release dry-run record (R13) is stale: games/** changed after the dry run on b5b379ff (Battleship's manifest, Sudoku's icon.png), so "the packages below" are not the ones that run packed.
    evidence: |-
      Blind hunter, follow-up pass; the packet text says the dry run "is valid while games/** and scripts/pack_game.py stay as they are there". The orchestrator owns the R13 section and the re-run.
    location: >-
      _bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/device-run-packet.md (release dry run)
    severity: medium
  - summary: >-
      The simulator's HalGPIO::wasTouchReleased ignores suppressTouchContact, so after a long press its lift logs one extra "contact ended screen unknown" line there; the SDK's InputManager reports no release for a suppressed contact.
    evidence: |-
      Seen in the simulator run (sim-touch-log.txt, 26884 ms): a long press is followed by `contact ended screen unknown`. .pio/libdeps/simulator_x4pro/simulator/src/HalGPIO.cpp:680 returns releasedThisFrame unconditionally; freeink-sdk InputManager.cpp:648 returns false when touchSuppressed. Not seen on a device build; unverified there.
    location: >-
      the simulator library (crosspoint-simulator HalGPIO.cpp:680)
    severity: low
---

<intent-contract>

## Intent

**Problem:** Several taps on Ultimate tic-tac-toe's right column did nothing and showed no message on the device, and the log cannot say whether each was a long press, a slide, or a tap off the canvas (epic Notes, Finding 2026-10-10). Battleship and Sudoku also show the Crosshatch mark in the launcher.

**Approach:** Log every gesture of a running match at `LOG_DBG`, with its fate, from a pure formatter that host tests pin; change no game's behaviour. Battleship's manifest names the library `boat` in the fill weight, and Sudoku ships a 64 x 64 `icon.png` made by a committed stdlib-only generator whose `--check` runs under the `games-check` label.

## Boundaries & Constraints

**Always:** `LOG_DBG` only, no setting. A log line costs nothing when `LOG_DBG` is compiled out (the formatter runs inside the macro's arguments). No static initializer in game objects, no function-local static, locals under 256 B (the flash-budget `objects` gate). The games check names no game (R2): game-specific icon pins sit in each companion's `checks.lua`. A double the story adds names the device behaviour it stands in for, and a test pins that they agree.

**Never:** Change what any gesture does (which events reach `input`, their order, `postInput` tags, the drop lines already logged). Touch `games/ultimate-tic-tac-toe/` (it keeps the default mark), an upstream file, `freeink-sdk`, or `.skills/`. Fix the missed taps (the fix waits on a re-test). Add a dependency: the generator is standard library only, as `make_note_images.py` is.

## I/O & Edge-Case Matrix

Line text after `<id>: touch ` (the pure formatter's output; `held` is `gpio.lastTouchHeldMs()` as the SDK latches it at release, so it can include the controller's release hold-over):

| Scenario | Input / State | Expected log text | Error Handling |
|----------|--------------|-------------------|----------------|
| Tap | tap at screen (240,400), held 82 ms, Sticky canvas | `tap screen (240,400) canvas (237,391) held 82 ms` | n/a |
| Long press | at (240,400) | `long press screen (240,400) canvas (237,391)` (no hold: the SDK fires it at 500 ms and the lift is suppressed, so `lastTouchHeldMs` is stale) | n/a |
| Swipe | (400,400) to (150,420), held 120 ms | `swipe left screen (400,400)->(150,420) canvas (397,391) held 120 ms` | n/a |
| Off canvas | tap (477,400), held 64 ms | `tap screen (477,400) held 64 ms: not sent, off the canvas` | the game gets nothing, as before |
| Edge swipe | down swipe (240,5)->(240,300) | `swipe down screen (240,5)->(240,300) held 90 ms: not sent, system edge swipe` | as before |
| Contact with no gesture | released, no tap, no swipe, first sample (10,20), last (80,30), held 640 ms | `contact ended screen (10,20)->(80,30) held 640 ms: not sent, not a tap, long press or swipe` | as before (no event). Unknown positions print `screen unknown`; unknown hold prints no `held` |

## Code Map

- `src/games/GameTouch.h` -- `Kind`, `Gesture`, `isSystemEdgeSwipe`, `toEvent`. Add `Kind::Ended`, `Gesture::heldMs` (int32, -1 = none), `enum class Outcome`, `classify`; `toEvent` becomes `classify(...) == Outcome::Sent`.
- `src/games/GameTouchLog.h` -- new, pure: `struct Line { char text[128]; }` and `line(gesture, outcome, event)` (snprintf, no static).
- `src/activities/games/GameMatchActivity.cpp` (`readGesture` 665, `loopPlaying` 460-546) and `.h` (touch latch fields ~295) -- latch first/last contact sample, set `heldMs`, return `Kind::Ended` for a release with no tap and no swipe, log in `loopPlaying` right after classify. The existing `aimed`, `awaitingDisplay`, `firstFrameTouch` logic reads the same `aimed`.
- `test/game_script/GameTouchTest.cpp` (+ new `GameTouchLogTest.cpp`, listed in `test/game_script/CMakeLists.txt`) -- pure tests. `test/game_script/harness/GameMatchTest.cpp` and `screen_stubs/MappedInputManager.h` (`tap`, `quickTap`, `longPress`, `swipe`, `liftWithoutTap`, `holdTouch`/`liftTouch`) -- activity-level pins.
- `games/battleship/manifest.json` -- add `"icon": "boat", "icon_weight": "fill"`. `games/sudoku/icon.png` -- new. Sudoku already has 36 members; the cap is 64 and `icon.png` is outside the images budget (`formats.md` line 329).
- `test/game_script/first_party/sudoku/tools/make_icon.py` -- new generator; imports `png_bytes` from `make_note_images.py` (same folder, via `sys.path` insert of its own directory so `-I` works). Hooked into `test/game_script/harness/games_check.cmake` beside `GamesCheckNoteImages` (a ctest `GamesCheckSudokuIcon`, label `games-check`).
- `test/game_script/harness/games_check/{GameCheck.cpp,ScriptVm.cpp,ScriptVm.h}` -- `Installed` gains the launcher's pick (`GameRowIcon::hasPackageIcon`, `readPackageIcon`, `choose` over the installed game); a check VM gets `host.launcher_icon() -> source, name, weight` (`"package"`; `"library", name, "fill"|"regular"`; `"fallback"`). `GameRowIcon.cpp` joins `GAMES_CHECK_INSTALLER_SOURCES`. `GamesCheckEngineTest.cpp` pins it over scratch games. Companion `battleship/checks.lua` and `sudoku/checks.lua` each gain one check. `first_party/README.md` and `docs/crosshatch/game-canvas.md` document both.
- Read-only: `src/games/GameRowIcon.{h,cpp}`, `GamesLauncherActivity.cpp`, `GameRegistry`, the installer (it converts `icon.png` to `icon.bmp`), `scripts/pack_game.py`.

## Tasks & Acceptance

**Execution:**
- [ ] `GameTouch.h`, `GameTouchLog.h`, `GameTouchLogTest.cpp`, `GameTouchTest.cpp` -- classify + formatter + pure tests for every row of the matrix, each unknown (positions, hold), a swipe off canvas, a swipe with no direction, and `toEvent(g) == (classify(g) == Sent)` over a table of gestures -- pins the lines and keeps `toEvent`'s guards
- [ ] `GameMatchActivity.{h,cpp}`, `GameMatchTest.cpp` -- latch, `Ended`, `heldMs`, the `LOG_DBG` call; tests drive the real activity with a tap, a long press, a swipe, an off-canvas tap, a system edge swipe and a lifted-without-tap contact and read the lines from `fakelog`; the existing tap/drop tests stay as they are
- [ ] `make_icon.py`, `games/sudoku/icon.png`, `games_check.cmake` -- generator (`--check`, `--out`), committed icon, ctest
- [ ] `games/battleship/manifest.json`, the two `checks.lua`, `GameCheck.cpp`, `ScriptVm.{h,cpp}`, `GamesCheckEngineTest.cpp`, `games_check.cmake` -- the launcher pick, its pins, its engine tests
- [ ] `first_party/README.md`, `game-canvas.md` -- document `host.launcher_icon`, the icon ctest, and the touch lines
- [ ] `device-run-packet/` -- repack with `scripts/pack_device_run.py` so the packet holds the new `sudoku.chgame` and `battleship.chgame` and `HASHES.txt` (precedent 305fd677)

**Acceptance Criteria:**
- Given a running match, when a tap, long press, swipe, off-canvas tap, system edge swipe and a contact with no gesture arrive, then `LOG_DBG` shows the line of the matrix for each, and the game receives exactly what it received before.
- Given the installed Battleship, when the launcher picks its row icon, then it is library `boat` in fill; given the installed Sudoku, then it is the package icon; Ultimate tic-tac-toe stays the fallback mark.
- Given `games/sudoku/icon.png`, when `make_icon.py --check` runs, then it exits 0, and 1 after the file changes.
- Given the simulator, when Games opens with both packages installed, then the list shows the boat and the grid.

## Implementation Notes

- Implemented by one subagent from the plan (agent aff73257d0085adde); its first report: host tests 1849 of 1849, fast checks green. Review patches (ten items, see the Review Triage Log) went back to the same subagent: host tests 1851 of 1851.
- Deviations from the plan, all accepted: the packet was repacked with `scripts/pack_game.py` per precedent 305fd677 (`scripts/pack_device_run.py` packs fixtures, not this packet), `sudoku-costly.chgame` was repacked too (it copies `games/sudoku/`), and the input double gained `moveTouch` and a sticky slop flag so a held contact can slide. No Ultimate tic-tac-toe check was in the plan; one was added in review.
- A context interruption of the build agent's turns happened twice while waiting on subagents; the run resumed from this plan's status each time, with nothing redone.

## Plan Change Log

## Review Triage Log

### 2026-10-10 — Review pass
- verdicts: 28 rows (the lenses' findings, a lens item with two claims split in two) — high 0, medium 5, low 15, false 4, maybe-false 0, descriptive 4 (intent-alignment divergences, logged without a verdict)
- Coordinator direction (orchestrator, 2026-10-10): the edge-case findings that could make a line lie (stale latch, first sample, stale hold, double vs device) are medium and are patched or made to say what they measure; also patch the int16 test inputs and truncation test, the Ultimate tic-tac-toe fallback check, the GameCheck.cpp fallback failure; the packet paragraph says only that the touch lines need the entry-14 firmware.
- findings:
  - Blind hunter
    - `[low]` `patch` The truncation test never truncates — rewritten to go through `detail::append` with a 300-character tail and to assert the cut at `sizeof-1`, NUL, prefix kept.
    - `[low]` `patch` The same test relies on signed overflow in `swipeDirection` — no coordinate beyond int16 remains.
    - `[low]` `patch` "Fate" overstated: the line is `classify`'s result; later drops (`dropped a touch ...`) have their own lines and the VM or game declining a tap is not logged — header and `game-canvas.md` reworded.
    - `[low]` `patch` GameCheck.cpp turned a failed icon allocation or unreadable `icon.bmp` into a misleading fallback pick — each now `report.fail`s on its own.
    - `[low]` `patch` README promised a Ultimate tic-tac-toe pick no check pinned — `fallback` check added to its `checks.lua`.
    - `[false]` `reject` "Nothing checks `boat` is a real library icon" — `GameRowIcon::choose` returns Library only when `hasLibraryIcon(name)`, so a typo gives `fallback` and the Battleship check fails; the installer also refuses an unknown icon.
    - `[low]` `defer` Device-run-packet paragraph contradicts itself and cites no commit — the orchestrator owns the packet; the paragraph was restored to its 1ccef0db text plus the one sentence the orchestrator asked for.
    - `[low]` `reject` Packet gives no step for checking the new behaviour and Battleship is `hidden` — packet content belongs to the orchestrator; a hidden game still has a launcher row, and the story's screenshots show it.
    - `[low]` `reject` `Outcome::NoDirection` can never happen — kept as the exhaustive-switch guard of `classify` (the SDK may change its tie rule); removing it would need a default branch for the same unreachable case.
    - `[false]` `reject` "Compiled out pays nothing" is unverified — `lib/Logging/Logging.h` defines `LOG_DBG(origin, format, ...)` as empty below `LOG_LEVEL` 2, so its arguments, `GameTouchLog::line(...)` included, are never evaluated.
    - `[low]` `patch` Doc does not say how to see the log — one sentence added (LOG_LEVEL 2 in the development envs and the simulator; release envs use 1). Checked against `platformio.ini`: dev envs set 2, `gh_release` and `gh_release_rc` set 1.
    - `[medium]` `patch` The `Ended` path is covered only by a hand-written double — the double was brought in line with the device (finding below) and pinned; the simulator run in Verification shows a tap, a long press and a slide against the real firmware classifier (the SDK input path itself is the simulator's, not the device's, and stays a residual risk).
    - `[low]` `patch` `make_icon.py` fails when `--out` does not exist — the write path now creates it.
    - `[false]` `reject` Icon cells fuse with the grid, `--check` compares bytes only — the cells are the owner's chosen option B, pixel-identical to `sudoku-b.png` (0 of 4096 differ); by construction the encoder writes 64 x 64 1-bit, and the installer converts it (the launcher pick check reads the converted `icon.bmp`).
  - Edge-case hunter
    - `[low]` `patch` Truncation test never truncates (duplicate of the blind finding).
    - `[low]` `patch` int overflow in the test input (duplicate).
    - `[false]` `patch` Stale latch after `loopPlaying`'s early returns — every early return (Back, `vmHealthy` failure, round over, turn passed) leaves Playing, and `loop()` frees `touchDownLatched` on the first pass elsewhere without a finger; the first and last fields are read only under that latch. Pinned anyway by `ASlideAfterAPauseLogsItsOwnSamplesNotTheLatchOfTheContactBeforeIt`, which passes with no production change.
    - `[medium]` `patch` `touchFirst` is the live point the first pass saw, not the touch-down point — the line text stays as the plan's matrix gives it; comments in `GameMatchActivity`, `GameTouch.h`, `GameTouchLog.h` and `game-canvas.md` now say "the first sample the loop saw", which for a fast slide can be past the true touch-down.
    - `[medium]` `patch` An ended contact the loop never saw down printed a stale `held` — `heldMs` is `-1` then (`touchDownLatched ? lastTouchHeldMs : -1`); the unknown-positions test expects no `held`.
    - `[low]` `reject` Multi-contact or system-consumed releases read as the user's slide — the reason text says only "not a tap, long press or swipe", which is true of them, and the doc says it is every release the classifier reported as none of the three.
    - `[medium]` `patch` The double's `wasScreenTouchDown` returned the live point and stayed true after a slide — it now returns the touch-down point, only while within `TOUCH_TAP_SLOP_PX` (28, sticky once exceeded, as the device's `touchMovedBeyondTapSlop`), pinned by the double's own test and by `CopiedConstantsTest`.
    - `[medium]` `patch` The plan's "a test pins that the double agrees" had no test (same root cause as the row above) — the same fix.
    - `[low]` `patch` Comment says "contact's first sample" — reworded with the `touchFirst` row.
  - Verification gap
    - `[low]` `patch` Ultimate tic-tac-toe's fallback pick documented but not pinned — the `checks.lua` check added.
  - Intent alignment (descriptive)
    - `[descriptive]` The log sits in `loopPlaying` only (state Playing); Paused, Over, Result, HandOff and Error are the host's own screens, not game input — the intent says "while a game match runs" and "reaches the game", which is the canvas. No change.
    - `[descriptive]` `canvas (x,y)` means `classify` accepted the gesture; later drops and the game declining are not logged — documented (see the fate row).
    - `[descriptive]` The launcher pick is checked by the real `GameRowIcon::choose` over the installed package, not through `GamesLauncherActivity` — the activity's use of `choose` has its own tests (GamesLauncherTest, GameRowIconTest) and the simulator screenshot shows the rows.
    - `[descriptive]` The committed icon is compared with its own generator, not the mock — pixel equality with `sudoku-b.png` was verified once (0 of 4096 differ) and is recorded in Design Notes.
- patch counts (this pass, by entry verdict): medium 5 (the Ended path's coverage, first-sample wording, stale hold, the double against the device, the plan's pinned-agreement claim: three root causes), low 11, false 1 (the stale latch, refuted and pinned); no high. `followup_review_recommended: true`.

### 2026-10-10 — Review pass (follow-up pass, 1ccef0db..8a0b72bf)
- verdicts: 18 rows — high 0, medium 2, low 11, false 3, maybe-false 0, descriptive 2 (intent-alignment, restating pass 1's divergences; carried)
- Orchestrator direction for this pass: the double's long-press point; device-true "contact ended" boundary, shapes and docs; say exactly what the first and last points are; a doc note on early-return passes; the packet and its dry-run record are the orchestrator's alone.
- findings:
  - Blind hunter
    - `[false]` `reject` The repacked `.chgame` files are not in the diff — they were excluded from the lens's diff file (`:!*.chgame`) and are in 8a0b72bf; the verification-gap lens recomputed their SHA-256 and sizes against the packet table.
    - `[low]` `defer` Packet prose contradicts its numbers and names no packing commit — the orchestrator rewrites the Packages paragraph on merge (his direction); carried by the deferred packet item of pass 1.
    - `[medium]` `defer` The release dry-run record in the packet is stale: `games/**` changed (Battleship manifest, Sudoku `icon.png`) after the dry run on `b5b379ff` — added to `deferred`; the orchestrator owns the R13 section and the re-run.
    - `[low]` `defer` The packet has no steps for the icons or the touch lines, and names no firmware commit — packet content, the orchestrator's.
    - `[low]` `patch` "Contact ended" misattributes contacts the system handled — a Back, Home or light-panel swipe reaches `classify` as a Swipe (SystemEdge), not Ended, and a Back early-returns before `readGesture`; what does end as Ended is documented as exactly three device contacts, including a multi-finger one.
    - `[false]` `reject` The tap and swipe hold has no stale-hold guard — the SDK latches `lastTouchHeldDurationMs` at the release that produced the tap or swipe (`wasSwipe` itself tests it), so it is this contact's; only an Ended contact the loop never saw can carry another's, and that is guarded.
    - `[low]` `patch` The icon docstring says 5 x 5 cells but they merge with box lines — docstring now says the cells are drawn over the grid and merge with a box line next to them (the owner's mock, pixel for pixel).
    - `[low]` `patch` `--check` hides a missing file behind "differs" — prints `missing: icon.png`. A 64 x 64 / 1-bit assertion is rejected: the generator always writes it and the installer's launcher read (`host.launcher_icon`) would fail otherwise.
    - `[medium]` `patch` The double's long-press point and slop were unpinned — see the edge-case row (same root cause); also the `liftWithoutTap` comment now explains the 28 px against the 59 px slop and where the double is more permissive.
    - `[low]` `patch` The overrun test covers `append`, not `line()` — `TheLongestRealisticLinesAreNotCut` runs `line()` with int16 extremes and the largest hold for each outcome and asserts no cut.
    - `[low]` `patch` The `game-canvas.md` bullet is hard to read — split into a rule and a list, with the plain statement that logging changes no gesture's outcome.
  - Edge-case hunter
    - `[medium]` `patch` The double's `wasScreenLongPress` returned the live point and fired after a slide — it reports the touch-down point and fires only while within the 28 px slop; `InputDoubleTest.AHeldContactFollowsTheDevicesTouchPath` pins both.
    - `[medium]` `patch` The "contact ended" examples and tests used shapes the device turns into a swipe or a tap — the device's `wasTouchTap` holds to 59 px and `wasSwipe` needs net 60 px within 700 ms, so Ended is (a) held over 700 ms with net 60 px or more, (b) an out-and-back past 59 px, or (c) multi-finger. Tests now use a 1200 ms slow slide and a 90 px out-and-back; the docs and the code comments say which contact gives each line; the plan's matrix numbers are format examples only.
    - `[low]` `defer` Packet paragraph provenance — the orchestrator's (see above).
    - `[low]` `patch` First and last points lag the live finger by up to 28 px — documented exactly (first is the first position the loop saw; while the contact is within 28 px and over 90 ms old the loop reads the touch-down point), no code change.
    - `[low]` `patch` A gesture released on an early-return pass is never logged — one doc note added; the stale-latch half is refuted again (every early return leaves Playing and `loop()` frees the latch) and stays pinned by the pause/resume test.
  - Verification gap
    - none reported.
  - Intent alignment (descriptive)
    - `[descriptive]` `carried` The log hooks in the Playing path only, and a host-accepted gesture the VM or game ignores is "sent" — as pass 1, documented.
    - `[descriptive]` `carried` The launcher pick is checked through `GameRowIcon::choose` over the installed package, not the activity; the input path is the double plus the simulator — as pass 1; the double is now pinned to the device for the touch-down, long-press and slop behaviour.
- patch counts (this pass): medium 2 (one root cause each: the double's long press; the Ended boundary), low 5; no high. `followup_review_recommended: false` (follow-up pass: true only for a patched high).

## Design Notes

- **Settled by:** the entry's description (what to log, `LOG_DBG`, no setting, no behaviour change), epic Notes Finding 2026-10-10 (a hold of 500 ms or more is a long press; a slide past 28 px stops being a tap; "the log does not record which"), epic Notes Decision 2026-10-10 (boat in fill; Sudoku option B; Ultimate tic-tac-toe keeps the default mark), formats.md line 329 (`icon.png` is the launcher icon), R2 (the check names no game).
- **The hold time.** The entry's unknown: `Gesture` did not carry it. `HalGPIO::lastTouchHeldMs()` is latched at a tap's or swipe's release, and `loopPlaying` already reads it for a tap's back-dating, so `readGesture` copies it into `heldMs` for a tap, a swipe and an ended contact. A long press has none (the SDK fires it at 500 ms, `TOUCH_LONG_PRESS_MS`, and suppresses the lift), so its line says nothing of hold.
- **The ended contact is in scope.** The Finding names "a slide past 28 px" as a cause. `wasTouchTap` refuses a contact that passed 59 px and `wasSwipe` one under 60 px or over 700 ms, so a slow slide makes no gesture and today `readGesture` returns nothing for it: without this line the log still could not tell. It is log-only, adds no event, and is the same reason the entry gives. Not an intent gap: no reading changes behaviour.
- **`toEvent` rewrite.** Its guards, kept in `classify` in this order: `Kind::None` returns false before anything is read; an edge swipe is refused before the direction is read (a Back or Home swipe must never reach a game); `SwipeDir::None` is refused (no direction to deliver); `viewport.toCanvas` false refuses a gesture that starts on the bezel; `out` is written only on `Sent`, so a refused gesture leaves the caller's event as it was. `toEvent` stays a wrapper so the existing tests and callers hold; the activity calls `classify` to learn the reason. `GameTouchTest` already pins each guard; the parity table adds the rest.
- **Doubles.** `screen_stubs/MappedInputManager.h`'s `liftWithoutTap` stands in for a contact past the 59 px release slop with no swipe, and its suppressed lift reports no release, as `InputManager::wasTouchReleased` does with `touchSuppressed`. Its `swipe()` does not set `gpio.touchHeldMs`; the swipe test sets it, so the double is more permissive than the device there (a swipe on the device always latches a hold). `host.launcher_icon` is the real `GameRowIcon::choose` over the real installer on the fake card, not a double: the device draws the same pick.
- **Why a host function, not a data file.** The entry asks for a test that "the launcher picks" boat and the package icon. R2 forbids naming a game in the check, so the check reports the pick and each game's `checks.lua` states what it expects.
- **The icon.** `opt_b()` of `scratchpad/icons/mock.py` drew with Pillow. A rule-based redraw (vertical and horizontal line rectangles from x or y 5 to 59, 3 px at every third line, 1 px between; corners (4,4), (60,4), (4,60), (60,60) left white; a 5 x 5 black cell inside each box at (r,c) = (0,1) (1,4) (2,7) (3,2) (4,5) (5,8) (6,0) (7,3) (8,6)) was compared with `sudoku-b.png` decoded by hand: all 4,096 pixels identical (scratch `try_rules.py`). The committed PNG is written by this repo's encoder, so it is pixel-identical, not byte-identical, to the mock.
- **Hashes.** Pack hashes at the base: sudoku `e35f23efd835450d`, battleship `10a07de10cb14489`. Both change; the packet is repacked.

## Verification

**Commands:**
- Host: `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j'` -- all pass, including `-L games-check` at both canvases and `GamesCheckSudokuIcon`
- `python3 test/game_script/first_party/sudoku/tools/make_icon.py --check` (exit 0; 1 after a changed byte) and `make_note_images.py --check`
- `python3 scripts/pack_game_test.py`, other fork script tests, `python3 scripts/check_upstream_touches.py`, `python3 scripts/check_layers.py`, `./bin/clang-format-fix` twice
- Once, under `flock /tmp/crosshatch-build.lock`, `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`: `pio run -e x4pro`, `pio run -e default`, `pio check` (default) and `pio check -e x4pro`, `check_flash_budget.py` `build on`, `build off`, `compare` -- measured at `1ccef0db` before the change and on the commit after; the delta is those two, recorded with the x4pro `firmware.bin` size and SHA-256
- `sim.sh build x4pro`, install both packages, `ss` the Games list (both icons) and start Ultimate tic-tac-toe, `tap` and `hold 800` on its board, `log` the touch lines; screenshots into `story-touch-and-icons-screenshots/`

**Manual checks (if no CLI):**
- Look at each screenshot; the boat and the 9 x 9 grid read as icons at 64 px.

**Results (this run):**
- Host, after the review patches: `ctest` 1851 of 1851 (games-check 129, `GamesCheckSudokuIcon`, `GameTouchLogTest.*`, `GameTouchTest.*`, `TouchLogTest.*`, `InputDoubleTest.*`, `CopiedConstantsTest.*`); every row of the matrix has a covering test that ran. `make_icon.py --check` exit 0 and exit 1 after one flipped byte; `make_note_images.py --check` 0. Fork script tests, `check_upstream_touches.py` (PASS; only `test/CMakeLists.txt`, a ledger row, is upstream-listed and unchanged), `check_layers.py` (passed), `./bin/clang-format-fix` twice (stable, no upstream file changed).
- Firmware, under the build lock with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`, on the tree this run commits (no source changed after): `pio run -e x4pro` 0, `pio run -e default` 0, `pio check` (default) PASSED, `pio check -e x4pro` PASSED (both with `--fail-on-defect low/medium/high`).
- Flash budget, same method on both sides (`check_flash_budget.py build on`, `build off`, `compare`): base `1ccef0db` x4pro `firmware.bin` 5,936,048 B on, 5,680,016 B off, +256,032 B; this tree 5,937,008 B on, 5,680,016 B off, +256,992 B, so +960 B over the base, 19,488 B to spare of 276,480. Static internal RAM +784 B on both (limit 1,024). `objects`: 49 game objects, no static initializer, largest mutable static 4 B. Against the epic base Measurement (`eca7e6c7`, +255,616 B) the same-method figures are the base `1ccef0db` line above, not that older one.
- x4pro firmware, first pass (tree of 8a0b72bf): 5,937,008 B, SHA-256 `09981c8382c857233502b116f5ee5e7e65dcf6372b1d648c72376a44d8f1c3c9`. After the follow-up pass (comments and docs in `src/`, no logic change) `pio run -e x4pro` was rebuilt under the lock and `check_flash_budget.py build on`, `build off`, `compare` and `objects` re-run: still 5,937,008 B on, 5,680,016 B off, +256,992 B (+960 B over the base `1ccef0db`, 19,488 B to spare), RAM +784 B, objects clean, but the SHA-256 differs (the comment edits moved source line numbers the binary embeds): `0f10b8b4bc54145b561835a554df46f90814fb40c1160b37b361852df563b02d`, at `/tmp/claude-0/-home-user-crosshatch-player/0bdf8073-ad95-5670-8b31-e69743e4a448/scratchpad/8.14/x4pro-firmware.bin` (copy of the lane worktree's `.pio/build/x4pro/firmware.bin`). Flash this one. The host suites after the follow-up pass: `ctest` 1853 of 1853; the follow-up changed no `games/**` file, so the package hashes stand.
- Packages (`scripts/pack_game.py games/<id>`, equal to the repacked packet files): sudoku `0d3f38fe297ddb6e`, 40,895 B, file SHA-256 `b3cd08528b8a495ced84a7fada1148fe81b1a42d78da8b13ee241b0b69f3f767`, at `_bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/device-run-packet/sudoku.chgame`; battleship `a0de5525493b90da`, 9,518 B, `77136f87cd017ffedfd27e793251b8aa50cf35c8e3c895613775c7252c8a5e50`, at `.../device-run-packet/battleship.chgame`. Before: sudoku `e35f23efd835450d`, battleship `10a07de10cb14489`; Ultimate tic-tac-toe `a7e63b542144938b` unchanged.
- Simulator (`sim.sh build x4pro`, x4pro insets, the three packages in `fs_/games/`):
  - `_bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/story-touch-and-icons-screenshots/games-list-boat-and-grid.png` -- the Games list: Battleship with the library boat (fill), Sudoku with its 9 x 9 grid icon, Ultimate tic-tac-toe with the default mark.
  - `.../story-touch-and-icons-screenshots/sim-touch-log.txt` -- the firmware log of a tap, a long press, a slow 40 px slide, off-canvas taps and a fast swipe on Ultimate tic-tac-toe, from the real `GameMatchActivity`: `touch tap screen (400,300) canvas (393,291) held 68 ms`, `touch long press screen (250,500) canvas (243,491)`, `touch contact ended screen (305,401)->(340,420) held 215 ms: not sent, ...`, `touch tap screen (477,400) held 68 ms: not sent, off the canvas`, `touch swipe left screen (400,400)->(200,410) canvas (393,391) held 239 ms`. A top-edge swipe opened the light panel (ActivityManager takes it before the match), so the system-edge line is pinned by tests only. The third line of that file, `contact ended screen unknown` after the long press, is the simulator's HAL reporting a release the SDK suppresses (deferred item).

## Auto Run Result

**Status:** built, after the one follow-up review pass (a second commit). `followup_review_recommended: false` (the follow-up pass patched two mediums, no high). Residual: the `Ended` path and the first-sample latch are checked against the input double (now pinned to the device's tap-slop, select-delay and long-press rules) and the simulator's HAL, never against the real SDK on a device.

**Summary.** While a round is in play, `GameMatchActivity` logs every gesture at `LOG_DBG` after `<id>: touch `: its kind, screen point(s), canvas point when `classify` sent it, the SDK's hold time for a tap or swipe, and the reason when not sent (off the canvas, system edge swipe, or a contact that was no tap, long press or swipe). Battleship's manifest names the library `boat` in fill; Sudoku ships a 64 x 64 `icon.png` made by `make_icon.py` (pixel-identical to the owner's mock option B), checked under the `games-check` label. No game's behaviour changed.

**Files.**
- `src/games/GameTouch.h` -- `Kind::Ended`, `Gesture::heldMs`, `Outcome`, `classify`; `toEvent` is a wrapper.
- `src/games/GameTouchLog.h` (new) -- the pure line formatter.
- `src/activities/games/GameMatchActivity.{h,cpp}` -- first/last sample latch, `heldMs`, `Ended`, the `LOG_DBG` call.
- `games/battleship/manifest.json`, `games/sudoku/icon.png` (new) -- the launcher icons.
- `test/game_script/first_party/sudoku/tools/make_icon.py` (new), `harness/games_check.cmake` -- the generator and its `GamesCheckSudokuIcon` ctest.
- `harness/games_check/{GameCheck.cpp,ScriptVm.cpp,ScriptVm.h}`, the Battleship, Sudoku and Ultimate tic-tac-toe `checks.lua` -- `host.launcher_icon()` and each game's pick.
- `test/game_script/{GameTouchLogTest.cpp (new),GameTouchTest.cpp,CMakeLists.txt}`, `harness/{GameMatchTest.cpp,CopiedConstantsTest.cpp,screen_stubs/MappedInputManager.h,games_check/GamesCheckEngineTest.cpp}` -- tests and the corrected touch double.
- `docs/crosshatch/game-canvas.md`, `test/game_script/first_party/README.md` -- documentation.
- `_bmad-output/.../device-run-packet/` (`sudoku.chgame`, `battleship.chgame`, `sudoku-costly.chgame`, `HASHES.txt`) and `device-run-packet.md` (Packages paragraph, one sentence added) -- repacked packet.
- `_bmad-output/.../story-touch-and-icons-screenshots/` -- the simulator evidence; this plan.

**Review.** Four lenses ran as context-free subagents (blind-hunter, edge-case-hunter, verification-gap, intent-alignment): 28 rows, high 0, medium 5, low 15, false 4, descriptive 4. Patches applied: 17 rows (5 medium, 11 low, 1 false-but-pinned), all in the Review Triage Log. Deferred: two low items (packet paragraph detail; the simulator HAL's release after a long press). Rejected with a refutation or reason: `boat` validity, LOG_DBG cost, icon cells and `--check`, `NoDirection`, packet steps, multi-contact wording, the stale-latch claim (refuted and pinned by a test). Formatting: `./bin/clang-format-fix` changed no file outside this story's paths.

**Verification.** See the plan's Verification Results: host 1851 of 1851; both `pio run`, both `pio check`, flash budget +256,992 B on against +256,032 B at the base `1ccef0db` (+960 B, 19,488 B to spare), RAM +784 B unchanged, `objects` clean; simulator screenshot and touch log. x4pro `firmware.bin` 5,937,008 B, SHA-256 `0f10b8b4bc54145b561835a554df46f90814fb40c1160b37b361852df563b02d` (after the follow-up pass; the first-pass SHA `09981c83...` is superseded), at `/tmp/claude-0/-home-user-crosshatch-player/0bdf8073-ad95-5670-8b31-e69743e4a448/scratchpad/8.14/x4pro-firmware.bin`. Packages: sudoku `0d3f38fe297ddb6e`, battleship `a0de5525493b90da`, in `_bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/device-run-packet/{sudoku,battleship}.chgame` (SHA-256 of the files in the Verification section).

**Screenshots.**
- `_bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/story-touch-and-icons-screenshots/games-list-boat-and-grid.png` -- the Games list with the boat, the Sudoku grid and the default mark.
- `.../story-touch-and-icons-screenshots/sim-touch-log.txt` -- the touch lines of a tap, a long press, a slide, off-canvas taps and a swipe.

**Residual risks.** `Ended` and the first/last latch are verified against the double (pinned to the device's tap-slop and select-delay constants) and the simulator, not the SDK on a device; the simulator's HAL adds one `contact ended screen unknown` line after a long press, which the SDK does not report. The first sample is the first one the loop saw, so a fast slide can start past the true touch-down. `held` is the SDK's latched figure and can include the controller's release hold-over. No Ultimate tic-tac-toe fix was attempted; the missed taps wait on a re-test with these lines. Pre-existing and out of this story: the packet's other paragraphs, left to the orchestrator.

**Follow-up pass.** Four fresh lenses over `1ccef0db..8a0b72bf`: 18 rows, high 0, medium 2, low 11, false 3, descriptive 2; patched 7 (2 medium, 5 low): the double's long-press point and slop, the device-true "contact ended" shapes and docs (which contact gives which line), the first/last point wording, the early-return note, the icon tool's docstring and `missing:` message, the full-line truncation test. Deferred: the stale release dry-run record (medium) and the packet prose, both the orchestrator's. The follow-up commit changes docs, comments and tests only; x4pro firmware.bin was rebuilt (same size, flash delta +256,992 B, SHA-256 `0f10b8b4bc54145b561835a554df46f90814fb40c1160b37b361852df563b02d`). Package hashes unchanged: sudoku `0d3f38fe297ddb6e`, battleship `a0de5525493b90da`.

