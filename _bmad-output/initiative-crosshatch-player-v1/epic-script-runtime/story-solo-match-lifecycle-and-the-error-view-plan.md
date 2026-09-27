---
title: 'Solo match lifecycle and the error view'
type: 'feature'
ticket: '13'
created: '2026-09-27'
status: 'built'
baseline_revision: '169ae2916a31af890a422a9d8ca14cf50b3f3d55'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/_bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md'
  - '{project-root}/docs/contributing/touch-and-ui.md'
  - '{project-root}/docs/activity-manager.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** `GameMatchActivity` has only Starting, Playing, and Error: Back leaves the match at once, Home goes Home, a finished round keeps the canvas live, `ch.store` is flushed only every 5 s (a `set` just before leaving or sleeping is lost), and the tracer's error screen shows English one-line detail strings.

**Approach:** A pure `GameCore::MatchLifecycle` holds AD-21's solo states and transitions (host-tested); the match drives it from explicit events, shows the pause menu, the end-of-round menu (over the last frame), and AD-14's error view as one `UiAppHost` option dialog, restarts a round through the Session on Play again, and flushes `ch.store` on entering Over, on Leave (after the VM stops), and in `onExit()`.

## Boundaries & Constraints

**Always:**
- AD-21 solo machine, every other event ignored (logged at debug): Starting →Playing (started) / →Error; Playing →Paused (Back, Home) / →Over (round over) / →Error; Paused →Playing (Resume, or Back) / →Leaving (Leave) / →Error; Over →Playing (Play again) / →Leaving (Leave) / →Error; Error →Leaving (Back); a forced exit goes to Leaving from every state. The match overrides `handleHomeGesture()` and always returns true.
- `over` is delivered once by `Session` (unchanged); the VM counts ended rounds after the round's last draw, and the match enters Over when the count moves. Play again runs `Session::start()` and `draw()` on the VM task (ver keeps counting), cancelling the pending timer first.
- Leave (user): stop the VM (cancel, join 500 ms, else abandon) under `RenderLock`, flush a dirty `ch.store` unless the slot was leaked with a live task, then `goToGames()`. `onExit()`: same stop and flush under the `RenderLock` ActivityManager holds; it never takes `RenderLock` or waits on a render (12cc816).
- Paused and Over post no touch input and poll no timers (a due timer fires after Resume); failure and the 3 s watchdog are checked in Playing, Paused, and Over; the periodic flush runs whenever the VM is alive.
- Views: one `fui` option dialog per state (caption = game name; headline `tr()`; Error adds the message in small type, wrapped up to 8 lines; options Resume/Leave, Play again/Leave, Back), touch through `routeTouch`, buttons Up/Down/Confirm in `loop()`; never `rowTouch`/`wasTapInRect`. Load and start failures map to `tr(STR_GAMES_*)`; new keys appended to `english.yaml` only (ledger row 2).
- C3 rules: no new heap in steady state, locals under 256 B, no static initializers; `src/` fork `.cpp` whole-file `#if FREEINK_CAP_GAMES`.

**Never:** multiplayer states (Lobby, HandOff, Result, PeerGone), `resume.bin`, ABORT/radio; pausing on the light panel push; changing `Session`, `api-level-1.txt`, `ApiLevel.h`, `lib/lua`, `ci.yml`, `.skills/`, or the submodule pointer; upstream files other than ledger row 2.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Pause | Playing, Back or Home | pause menu over the frame; game gets no input | — |
| Resume | Paused, Resume/Back | frame redrawn in full, input resumes | — |
| Round ends | tracer's 5th tap | `over` once, store flushed, end-of-round menu | flush failure logged, slot stays dirty |
| Play again | Over, Play again | new round from setup, ver continues | setup error → Error |
| Leave | Paused/Over, Leave | VM stopped, store flushed, Games list | stuck VM abandoned; leaked slot not flushed |
| Fault | any entry 7–10 fault | error view with `tr()` headline and wrapped message; Back → Games | — |
| Load failure | folder missing, no sources, OOM | error view with the mapped `tr()` reason | — |
| Sleep | Playing, `key sleep` | sleep screen, VM stopped, store flushed | no deadlock |
| Invalid | Home in Over or Error, Back in Over | nothing | — |

</frozen-after-approval>

## Code Map

- `src/activities/games/GameMatchActivity.{h,cpp}` -- `State {Starting, Playing, Error}`, `onEnter` load/start, `onExit` stop/abandon, `stopStuckVm`, `abandonVm` (leaks `store` when the task may run), `loop` (Back → `goToGames`), `render` (frame path, `forceFull` when `frameGen` unchanged), `renderError`. Replace the state and error screen; keep the frame path, touch mapping, watchdog.
- `src/games/GameVM.{h,cpp}` -- `run()` loop (`overLogged`), `notifyTask`, `game.timer()`; add ended-round count and `playAgain()`.
- `src/games/GameAssets.{h,cpp}` -- `load()` returns English `const char*` reasons; return an enum instead.
- `lib/GameCore/Session.h` -- `start()` keeps ver and resets `overDelivered`; no change.
- `src/components/UiAppHost.h` (`resetUi`, `renderUi`, `routeTouch`, `closeRouting`), `app.setScreen`, `UiScreen::dialog`; `src/components/OptionPopup.h` (popup border from `UITheme` metrics); `EpubReaderPercentSelectionActivity.cpp` (host wiring).
- `src/activities/ActivityManager.cpp:108` (`handleHomeGesture`), `:140`/`:187` (onExit under `RenderLock`), `goToSleep` (Replace + `loop()`).
- `lib/I18n/translations/english.yaml:523-525` -- existing `STR_GAMES_*`.
- `test/game_core/CMakeLists.txt` -- add the new source and test.
- `test/game_script/fixtures/` -- `faults/*.lua` (entry 7), `loop/`, `tracer/` (ends at 5 taps), `counter/`; entries 8–10 faults exist only inline in host tests.

## Tasks & Acceptance

**Execution:**
- [x] `lib/GameCore/MatchLifecycle.{h,cpp}` -- `MatchState`, `MatchEvent`, `apply(event) -> bool`, `allows(event)`, `menuFor(state)` (option events), `name()`.
- [x] `test/game_core/MatchLifecycleTest.cpp`, `CMakeLists.txt` -- every (state, event) pair against the table; menus; forced exit from each state.
- [x] `src/games/GameVM.{h,cpp}` -- `roundsEnded()` counter, `playAgain()`.
- [x] `src/games/GameAssets.{h,cpp}` -- `LoadResult` enum.
- [x] `src/activities/games/GameMatchActivity.{h,cpp}` -- lifecycle, per-state loop, views, Leave, `onExit`, `handleHomeGesture`, error mapping.
- [x] `lib/I18n/translations/english.yaml` -- append the new `STR_GAMES_*` keys.
- [x] `test/game_script/fixtures/limits/{manifest.json,main.lua}` -- simulator game with one band per entries 8–10 fault (state, move, store, status, frame, Lua error in input).
- [x] `docs/crosshatch/game-canvas.md` -- the canvas exception and the solo state table.
- [x] `.claude/skills/run-crosshatch-player/SKILL.md` -- Back from a game opens the pause menu.
- [x] `_bmad-output/implementation-artifacts/deferred-work.md` -- mark the tracer error-screen entry resolved by entry 13.

**Acceptance Criteria:**
- Given the x4pro simulator with `tracer`, `counter`, `loop`, `limits` in `fs_/.games/`, when Back, Home, Resume, Leave, and (after 5 taps) Play again are used, then the screenshots show each view and the log shows the transitions, "Round over" once per round, and ver continuing.
- Given each `faults/*.lua` as a game's `main.lua`, `loop`'s bands, and each `limits` band, then each ends in the error view, and Back returns to Games.
- Given `counter` tapped and left at once, when reopened, then the count survived (Leave flush); given `key sleep` during play, then the sleep screen shows and the log shows the VM stopped and the store saved.
- Given sticky, the pause menu and one fault look right.

## Implementation Notes

- Implemented directly (no coding subagent in this session). New: `lib/GameCore/MatchLifecycle.{h,cpp}`, `test/game_core/MatchLifecycleTest.cpp`, `test/game_script/fixtures/limits/`, `docs/crosshatch/game-canvas.md`. Changed: `GameMatchActivity`, `GameVM` (`roundsEnded`, `playAgain`, `startRound`), `GameAssets` (`LoadResult`), `english.yaml` (12 keys appended), `SessionGameTest` (Play again through the Session; every `limits` band), `test/game_core/CMakeLists.txt`, the `counter` fixture's note (leaving now saves), the simulator skill's Back line, `deferred-work.md`.
- The dialog props (`fui::OptionDialogProps`, 1,144 B on the host) are a render-task member drawn with `fui::optionDialog` directly, not `screen.dialog()` (which copies them into a local), so every local stays under 256 B; the panel style is set field by field for the same reason.
- `handle()` requests no render for `Started` and `PlayAgain`: the first frame of a round asks for its own, as the tracer did before. Back in the pause menu resumes (the menu's dismiss, as in `OptionPopup`); Back and Home in Over and Home in Error are ignored (AD-21 lists no such transitions).
- The three `faults/` files without `status` (`loop_draw`, `loop_in_pcall`, `loop_input`) end in the error view with "game.status is not a function" when run as a game, before their loop; the loop fixture's bands cover the budget in `input`.
- Follow-up (orchestrator, for 2.16's device run): `faults/loop_draw.lua`, `loop_in_pcall.lua`, and `loop_input.lua` gained `status` and `apply` (and `loop_input` a drawn prompt), so as games they reach their loop: simulator "main.lua:7", "main.lua:10" (inside `pcall`), and "main.lua:8" (on the first tap) "instruction budget exceeded", each then Back to Games (`faults-completed-contract.png`); every other fault script already reached its named fault. `test/game_script/fixtures/README.md` lists each fixture, its SD placement as `/.games/<id>/`, and its error text; `ctest` 615/615.
- x4pro flash 5,808,698 B (entry 12: 5,803,546 B, +5,152 B); RAM unchanged at 31.1 %.

## Plan Change Log

## Review Triage Log

Pass 1 (no subagents in this session; each lens run here one at a time against `review.diff`, 64 kB: blind-hunter with a floor of 9, edge-case-hunter, verification-gap, intent-alignment). Verdicts: high 0, medium 0, low 8, false 3, maybe-false 0.

| # | Lens | Finding | Verdict | Route / evidence |
| --- | --- | --- | --- | --- |
| 1 | blind, edge | After Leave, a failed `goToGames()` (OOM on the list activity) leaves the match in Leaving with Home swallowed by `handleHomeGesture`: the device is stuck until sleep | low | Patched: `handleHomeGesture` returns false once Leaving, so Home goes Home; game-canvas.md says so. |
| 2 | edge | `buildView` read `shown` while `renderView` chose its frame from an earlier load, so a transition between them could draw one state's dialog over another's backdrop for one refresh | low | Patched: `renderView` stores the state in a render-task `viewState` that `buildView` reads. |
| 3 | blind | `dialogProps.options` points at `buildView`'s local array after it returns | false | Only `buildView` reads `dialogProps`, and it sets `options` before every draw; nothing dereferences the stale pointer. |
| 4 | blind | `handle(PlayAgain)` dereferences `vm` without a check | false | Over is entered only from Playing with a VM; every path that drops the VM (`stopStuckVm`, `stopVm`) goes on to Error or Leaving (header invariant). |
| 5 | blind | Play again keeps the previous round's `ui` table (tracer shows "Over events: 2") | low | Rejected: `ui` is the seat's local state for the match (LuaGame, entry 8); `SessionGameTest.TheTracerPlaysAgainAfterGameOver` pins it; a per-round reset would be an API change. |
| 6 | blind | Events queued just before Over reach the new round after Play again | false | The match posts nothing in Over, events queued before Over are processed during Over (their moves discarded), and the restart cancels the timer, so a fired timer event is stale (`GameTimer::accepts`). |
| 7 | blind | A 500 ms stop plus a 500 ms abandon wait hold `RenderLock` for up to 1 s on Leave | low | Rejected: same bound and design as the existing watchdog stop (entry 7); render only waits. |
| 8 | blind | The menus refresh FAST over a dithered frame | low | Rejected: popups do the same (`OptionPopup`); resuming forces a full refresh. |
| 9 | verification-gap | Transition actions (flushes, stop before `goToGames`, `handleHomeGesture`, load mapping) and `GameVM`'s round count and restart have no host test | low | Defer (`deferred-work.md`): the same `src/activities`/`src/games` harness gap as entries 1, 8, 10, 11, 12; the simulator runs below cover each. |
| 10 | verification-gap | `faults/loop_draw`, `loop_in_pcall`, `loop_input` as simulator games fail on the missing `status` before looping | low | Rejected: they end in the error view as the entry asks; the host suite runs them as intended and the `loop` bands cover the budget in the simulator (Implementation Notes). |
| 11 | blind | The watchdog reason hard-codes "3 seconds" | low | Rejected: a `static_assert(WATCHDOG_MS == 3000)` beside its use ties them. |

Edge-case claims check: the Intent and Tasks claims held (single `handle()`, Over flush after `over`, Leave stop-then-flush-then-`goToGames`, `onExit` never taking `RenderLock`, ver continuing, dialog on `UiAppHost` with no `rowTouch`/`wasTapInRect`). Deletion check: `showError` became `fail` (logging kept), `renderError` became the dialog view, the immediate Back-to-Games became the pause transition (intended), and "no render until the first frame" survived in `handle()`. Intent-alignment: readings are (a) Play again restarts through the running Session and VM, and (b) Play again rebuilds the VM; the intent names (a) and the diff implements it. The intent's expectations live at the device's match activity; the diff's tests exercise the pure `MatchLifecycle` and the Session over `LuaGame`, and the activity in the simulator.

## Design Notes

**Why a count, not a flag.** A "round over" flag would need clearing on Play again, racing the VM; the VM instead increments `roundsEnded` when status turns over (after the draw), and the match compares with the value it saw at Over entry. A round that ends while paused moves the count; the match sees it after Resume.

**Events, not raw calls.** `handle(MatchEvent)` is the single place that transitions: it applies, mirrors the state into an atomic for `render`, closes routing, runs the entry action (Over: flush; Leaving by user: `leave()`), and requests a render (not for Play again: the new round's first frame asks for its own). `onExit` applies `ForcedExit` and then always stops and flushes, since a user exit may already be Leaving.

**Render.** Menus: clear, `forceFull`, draw the front frame, the dialog, hints, one refresh. Error: clear, dialog, full refresh. Playing after a menu: clear and `forceFull`, so no dialog pixels survive a game that does not clear.

## Verification

**Commands** (builds under the shared `flock`):
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` -- all pass.
- `pio run -e x4pro`, `-e sticky`, `-e default`; `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` (default and `-e x4pro`) -- SUCCESS, no defects.
- `sim.sh build x4pro` and `sticky`; live and scripted runs per the ACs -- screenshots in `story-lifecycle-screenshots/`.
- `python3 scripts/check_upstream_touches.py`, `./bin/clang-format-fix` (twice) -- PASS, no diff.

**Verification record** (2026-09-27, working tree before commit, on 169ae291):
- Host: `ctest` 615/615 passed: new `MatchLifecycleTest` 8 (every state and event against an independent table, Back and Home never leaving directly, one error view, repeated rounds, Leaving terminal, menus, names); `SessionGameTest.TheTracerPlaysAgainAfterGameOver` (ver 6 to 7 to 12, "Over events: 2") and `EveryLimitsFixtureBandIsAScriptError` (six messages).
- `pio run -e x4pro` SUCCESS (RAM 31.1 %, Flash 88.6 %, 5,808,698 B), `-e sticky` SUCCESS (RAM 20.8 %, Flash 86.9 %), `-e default` SUCCESS (RAM 17.7 %, Flash 85.6 %); `pio check` default and `-e x4pro` with the three `--fail-on-defect` flags: PASSED after making `startRound` a free function (cppcheck `functionStatic`).
- `./bin/clang-format-fix`: exit 0, only this story's files; `check_upstream_touches.py`: PASS on the commit.
- Simulator x4pro, screenshots in [story-lifecycle-screenshots/](story-lifecycle-screenshots/): `tracer-paused.png` (Back), `tracer-resumed.png` (Resume tapped: full redraw, no dialog pixels), `tracer-home-paused.png` (Home key), `tracer-paused-leave-focused.png` (Down moves focus; Back then resumed), `tracer-over.png` (5th tap: "Over events: 1", end-of-round menu; log "Round over at ver 6", "Playing -> Over on RoundOver"), `tracer-over-after-back-home.png` (log "Back ignored in Over", "Home ignored in Over"), `tracer-play-again.png` ("Round started at ver 7", Taps 0), `tracer-over-2.png` ("Round over at ver 12", "Over events: 2"), `tracer-left.png` (Down, Confirm on Leave: "VM stopped", Games list). Counter: three taps and Leave within 2 s (`counter-paused.png`), log "saved ch.store (11 bytes)" after "VM stopped" and before the exit, store.bin `... 03 06`, `counter-reopened.png` shows 3. Sleep: `counter-sleep.png` (two taps then `key sleep`: "Playing -> Leaving on ForcedExit", "VM stopped", "saved ch.store", sleep screen 5 ms later, store.bin `... 03 0a`; wake landed on Home, `wake2.png`); `paused-sleep.png` (sleep from the pause menu); `stuck-sleep.png` (sleep while `loop`'s band 5 is stuck in one C call: "did not stop within 500 ms", leaked, no flush, sleep screen 515 ms after the key, no deadlock).
- Faults, each ending in the error view and Back returning to Games: `loop-bands.png` (budget, budget in pcall, C stack headroom, watchdog cancel, watchdog abandon with "It stopped responding: one step ran over 3 seconds"); `limits-bands-1-3.png`, `limits-bands-4-6.png` (state, move, store, status, frame, Lua error, with the host test's messages); `faults-all.png` (all 22 `faults/*.lua` placed as `/.games/f-<name>/main.lua`: 22 "Error -> Leaving on Back"), `fault-back-to-games.png`; `longerr.png` (a 159-byte Lua message wrapped to 5 lines; Home ignored; the on-screen Back tapped); `nolua.png` ("The game could not start" / "The game has no Lua files").
- After the review patches: `sim.sh build x4pro` and a scripted run (Back, Resume, Home, Leave) behave the same, `recheck-after-review.png`.
- Simulator sticky, `sticky.png`: the pause menu by the left-edge Back swipe and by the bottom-edge Home swipe, Leave, and the `limits` Lua-error band's error view left by the Back swipe.
