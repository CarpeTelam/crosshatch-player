---
title: 'The hidden hand-off in the match'
type: 'feature'
ticket: '4'
created: '2026-10-01'
status: 'built'
route: 'full'
route_source: 'auto'
baseline_revision: '084199873b70c5d071d65a468ba657a83210a5fe'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/.skills/control-flow-clarity/SKILL.md'
  - '{project-root}/.skills/heap-discipline/SKILL.md'
  - '{project-root}/.skills/scope-discipline/SKILL.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Entry 2's `Result` and `HandOff` states exist only in `MatchLifecycle`: no match drives them, the VM always draws the turn seat right after a move (so a hidden game would show the next player's private view to the mover), there is no blank hand-off screen, published frames carry no seat for a gate to wait on, and the Play-again gap's pause menu says nothing (R4, R5, R7, R11, R13, R17).

**Approach:** A hidden pass match (`manifest.hidden` and a pass roster) builds its lifecycle with `hiddenPass` and its `GameVM` with a hand-off flag. That VM draws only the seat the match asks for: nothing after a round begins, the turn seat after the drawn-seat command (`showTurnSeat()`), and the mover after a move that passes the turn, which it counts so the match enters `Result` (mover's frame plus a runtime banner); it holds a due timer until the next seat is shown. A tap in `Result` pushes a `FrameReplay` blank frame with the `eye-closed` icon and a full refresh (`HandOff`); a tap there sends the command, and a canvas gate on the served request number pushes nothing until that seat's frame is published. Solo and open pass keep today's code path.

## Boundaries & Constraints

**Always:** Solo and open pass behave exactly as today (`rounds.start/restart/step`, no Result/HandOff, every existing host test unchanged in its assertions). No frame drawn for one seat is pushed after the blank before that seat's own frame is published; Paused from `HandOff`, or before the next seat's frame, draws no canvas under the menu. The hand-off tap counts only once the blank is on the panel (its tap zone is published after `displayBuffer`). The VM never draws `NO_SEAT`. Every guard in the functions extended stays (Design Notes). Each double extended names, in a comment and here, the device behaviour it stands in for, with a test pinning it. AGENTS.md memory rules (no new statics, locals under 256 B); user text through `tr()`; new keys appended to `english.yaml` only.

**Never:** Edit `lib/GameCore/Session.*`, `MatchLifecycle.*`, `SeatShown.h`, `lib/GameScript/SoloRounds.*`, `GameSaveStore.*`, `GamesLauncherActivity.*`, `GameModeActivity.*`, the epic file, or `api-level-1.txt`. No `onExit()` blank (entry 6), no pass `resume.bin` (entry 9: pass still skips the hash). No text on the hand-off screen beyond the icon. No new include edge.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| New hidden match | `pass-hidden`, pass(2) | Starting → HandOff: blank, FULL; tap → seat 1's frame, FULL | taps before the blank is displayed are dropped |
| Turn passes | seat 1's accepted move, turn → 2 | VM draws seat 1; match → Result: seat 1's frame + "Tap to pass to player 2" (FAST) | — |
| Pass on | tap in Result; tap in HandOff | HandOff: blank, FULL; then seat 2's frame only once published | gate closed: no render, gestures dropped |
| Move in Result | a tap queued behind the turn-passing move | seat 1's `input` runs, its move is discarded (never `apply`) | — |
| Timer | falls due in Result or HandOff (polled or already queued) | held; delivered to the next seat right after its first frame | stale timer dropped as today |
| Round ends | the last move | `over` per seat, seat 0's frame, Over menu (RoundOver, never TurnChanged) | — |
| Play again | Over + Play again | HandOff (blank); gap, tap, seat 1's frame of the new round | — |
| Pause | Back/Home in Result or HandOff; Resume | back to Result / HandOff; from HandOff the menu sits on no canvas | — |
| Gap pause | Paused while `roundsStarted() < roundsStartedAwaited` | pause menu shows "Starting the next round"; redrawn without it once the round starts | — |
| Open pass | `pass-open`, pass(2) | never Result or HandOff, never a blank push | — |

</frozen-after-approval>

## Code Map

- `src/games/GameVM.{h,cpp}` -- `create(..., roster, bool handOff = false)` (callers unchanged). Public: `uint32_t showTurnSeat()` (loop task; `seatRequests.fetch_add(1)+1`, notify; returns the request), `uint32_t seatShownRequest() const` (last request whose frame is published, acquire), `uint32_t turnsPassed() const`, `uint8_t passedTo() const` (stored before the count, release). Private VM-task state: `GameCore::Session* playing`, `MatchState handOffView` (HandOff/Playing/Result), `uint8_t mover`, `bool timerHeld` + `InputEvent heldTimer`, `uint32_t seatTaken`, `bool seatPending`; atomics `seatRequests`, `seatServed`, `turnChanges`, `nextSeat`. Helpers: `drawShown()` = `rounds.draw(GameCore::seatShown(handOffView, roster, status, mover))`, never for `NO_SEAT`; `showSeatNow()` (view Playing, draw, `seatServed.store(seatTaken)` after the publish, then a held timer through `stepHandOff`); `stepHandOff(event)` (stale timer dropped; view HandOff/Result: timer held, HandOff drops other events; else `rounds.play(event, seat)`; if the view was Playing, the status is not over and `turn != seat`: view Result, `mover = seat`; draw; then store `nextSeat` and bump `turnChanges`). `run()`: hidden start = `rounds.begin` + view HandOff (no draw); each iteration reads `seatRequests` into `seatPending` BEFORE `takePlayAgain()` (so a seat asked after Play again is served in the new round); Play again = `beginAgain` + view HandOff + drop the held timer; a pending seat is served before the queue; events go to `stepHandOff`. `publishCommitted` and `logRound` after every call as today; hidden logs "Round started" when `roundsStarted()` moved. Solo/open lines unchanged.
- `src/activities/games/GameMatchActivity.{h,cpp}` -- ctor builds `lifecycle(roster.mode == Pass && manifest.hidden)` and passes the flag to `GameVM::create`. New atomics `seatAwaited{0}`, `seatDisplayed{0}`, `passTo{0}`, `resumesTo{Playing}`; loop-task `turnsSeen`, `gapWhenPaused`; `ACTION_PASS = 2`. `handle`: before `shown.store`, HandOff+Tap stores `seatAwaited = vm->showTurnSeat()`, and `resumesTo` is stored; cases Result (resumeWritable) and HandOff (resumeWritable; PlayAgain runs the same new-round steps as Playing's, moved into a helper `startNextRound()`); Paused sets `gapWhenPaused`. `loop`: Result/HandOff → new `loopHandOff()` (vmHealthy, store and resume flushes as loopView; Back → handle(Back); a routed `ACTION_PASS` or Confirm while `routingReady()` → `clearTapFlash`, handle(Tap); in Result a new frame asks for a render). `loopPlaying`: after the RoundOver check, `turnsPassed()` moved → `passTo.store(passedTo())`, handle(TurnChanged); the gate also waits for `seatShownRequest() >= seatAwaited` and `seatDisplayed >= seatAwaited`. `loopView`: Paused re-requests a render when the gap closes. `render`: Result → `renderView`, HandOff → new `renderHandOff()` (`replay.drawBlank`, `displayBuffer(FULL_REFRESH)`, `viewOnScreen = true`, then `renderUi()` for the tap zone, `closeRouting` if the state moved). `renderView`: canvas under it only when `canvasUnderView(state)` (not Error, a VM, not Paused from HandOff, seat gate open); no button hints in Result. `renderCanvas`: gate on both counts; stores `seatDisplayed` beside `roundsDisplayed`. `buildView`: Result → full-safe-rect `fui::tapZones` (`ACTION_PASS`) plus a framed `fui::button` (no action) at the bottom with `tr(STR_GAMES_PASS_TO_PLAYER)` and `passTo`; HandOff → the tap zone only; Paused in the gap → `props.message = tr(STR_GAMES_NEXT_ROUND_STARTING)`. `optionLabel`, `viewHeadline`, `loop`, `render` cover every enumerator. Class comment: hidden pass.
- `src/games/FrameReplay.{h,cpp}` -- `void drawBlank(const GfxRenderer&, const GameViewport&)`: `clearScreen`, the `GameViewIcons::forView(HandOff)` icon at 128 px, black, centred on the canvas, then `policy.forceFull()`. Draws no game command; the caller pushes (entry 6 reuses it with HALF).
- `src/games/GameViewIcons.h` -- `forView`: Result → null (the banner has no icon), HandOff → `"eye-closed"`; `forOption`: TurnChanged, Tap → null.
- `test/game_script/GameViewIconsTest.cpp` -- `ALL_STATES`/`ALL_EVENTS` complete; HandOff's name agreed and in the library; Result no icon; `nonMenu` 7.
- `test/game_script/harness/screen_stubs/GfxRenderer.h` -- `Shown` gains `texts`: every text on the framebuffer at the push (canvas `DrawText` runs since the last `ClearScreen`, joined as `match::drawnTexts` joins them, plus UI texts noted since then), for `GfxRenderer::displayBuffer` (pushes the whole framebuffer) and `clearScreen` (wipes it). `noteUiText(const char*)` records a UI text at the current call index; the derived `forget()` also drops the notes. `screen_stubs/components/UiAppHost.h` -- `RecordingTarget::text` also calls `renderer.noteUiText(text)`: the device's `FreeInkUIGfxRenderer::text` draws into the same framebuffer.
- `test/game_script/fixtures/pass-hidden/` (new) -- manifest `{"id":"pass-hidden","name":"Pass hidden","version":"1.0.0","api":1,"seats":{"min":2,"max":2},"modes":["pass"],"hidden":true}`; `main.lua`: secrets `apple` (seat 1), `river` (seat 2); `setup` arms `ch.timer.after(5000)`; `status`: over after 4 moves (winners `{}`), else `turn = moves % seats + 1`; `apply` logs `apply seat S`; `input`: tap logs `tap for seat S`, re-arms the timer, returns `{tap = true}`; timer logs `timer for seat S`; over logs `over for seat S`. `draw` logs `draw for seat S` and shows "Player S", "Player S's secret: <word>", "Moves: k", or at seat 0 "Everyone: the secrets were apple and river". `fixtures/README.md`: Games row, packable list, Device-run packages; its outcome does not depend on device timing.
- `scripts/pack_device_run.py` / `_test.py` -- `('pass-hidden', 'pass-hidden')` after `pass-open`, docstring, expected files and hashes.
- `lib/I18n/translations/english.yaml` -- append `STR_GAMES_PASS_TO_PLAYER: "Tap to pass to player %u"`, `STR_GAMES_NEXT_ROUND_STARTING: "Starting the next round"`.
- `docs/crosshatch/game-canvas.md` -- a "Hidden pass states" section (Result, HandOff, the banner, the blank and its refresh, the tap zone after the push, the canvas gate, held timers, Paused from each, Play again), the views table rows, and the gap row's pause-menu line.

## Tasks & Acceptance

**Execution:**
- [x] `src/games/GameViewIcons.h`, `test/game_script/GameViewIconsTest.cpp` -- as in Code Map.
- [x] `src/games/FrameReplay.{h,cpp}` -- `drawBlank`.
- [x] `src/games/GameVM.{h,cpp}` -- as in Code Map.
- [x] `src/activities/games/GameMatchActivity.{h,cpp}`, `english.yaml` -- as in Code Map.
- [x] `test/game_script/harness/screen_stubs/{GfxRenderer.h,components/UiAppHost.h}` -- as in Code Map, each with its device comment.
- [x] `test/game_script/fixtures/pass-hidden/`, `fixtures/README.md`, `scripts/pack_device_run{,_test}.py`.
- [x] `test/game_script/harness/GameMatchTest.cpp` -- `HiddenPassTest` (manifest `hidden = true`, `Roster::pass(2)`): the `shown` sequence over a new match, a turn change, the round's end, and Play again (blank FULL first; seat 1; Result with the banner text; blank FULL; seat 2; never "river" in a push before seat 2's first blank-following push, nor "apple" after it until the next hand-off); a tap before the blank is displayed is dropped; Paused from Result and HandOff returns there and the HandOff pause push holds no secret; a tap queued behind the turn-passing move (VM held with `fakertos::arm`) reaches seat 1's `input` but no `apply`; a timer due in HandOff logs after `draw for seat 2`; an open `pass-open` match logs no `-> Result`/`-> HandOff`; `PlayAgainGapTest`: the gap's pause menu draws the line and the redraw after the round starts does not. Double pins: a push holds the canvas and UI texts drawn since the last `clearScreen`, none before it.
- [x] `test/game_script/harness/GameVmTest.cpp` -- a hidden VM publishes no frame until `showTurnSeat()`; a timer polled during the hand-off is held and logged after `draw for seat 1`.
- [x] `docs/crosshatch/game-canvas.md` -- as in Code Map.
- [x] `_bmad-output/implementation-artifacts/deferred-work.md` -- `## 5.4` at the end: resolve `## 5.2`'s `-Wswitch` item, `## 4.13`'s first item and `## e3r-x`'s second (the line), and anything deferred.

**Acceptance Criteria:**
- Given the X4 Pro simulator with `pass-hidden`, when a match starts and seat 1 moves, then screenshots show the blank hand-off, seat 1's frame, Result with "Tap to pass to player 2", the blank, and seat 2's frame; with `slow-restart`, Play again then Back shows "Starting the next round".
- Given the x4pro build, when `check_flash_budget.py` runs its four steps, then the delta over the base (+233,440 B, +784 B at `c1902721`) is within 11,152 B and 32 B.

## Implementation Notes

- `loopHandOff` also polls `ch.timer` (`vm->pollTimer()`) in Result and HandOff, the matrix's "polled" case: the VM holds the event (`stepHandOff`) and delivers it to the next seat after its first frame. The Code Map's list for `loopHandOff` did not name the poll.
- The gap line is limited to a pause menu that returns to Playing (`gapWhenPaused`, `pauseInGap()`: `resumesTo() == Playing` and `roundsStarted() < roundsStartedAwaited`). A hidden match's HandOff also has `roundsStarted() < roundsStartedAwaited` (its round counts as started at the first seat's frame, both in a new match and after Play again), but Resume there draws the blank, not an inert menu, so "Starting the next round" would be wrong there; `HiddenPassTest.PauseFromResultOrTheBlankReturnsThereAndTheBlanksPauseMenuSitsOnNoFrame` pins its absence.
- Outside the Code Map: `test/game_core/ManifestTest.cpp`'s `EveryFixtureManifestIsListed` required every fixture to offer solo; `pass-hidden` is pass-only by this plan's manifest, so the test now expects it to offer pass alone. `scripts/pack_device_run_test.py`'s real-tree test is renamed `test_hashes_lists_the_nine_files_in_order` and counts nine distinct hashes.
- Approved by the orchestrator (2026-10-01): the `test/game_core/ManifestTest.cpp` change, outside the touches (a test follow-through of the pass-only fixture).
- `GameVM::stepHandOff` fails closed on `NO_SEAT` (no input read) as `drawShown` does; `run()` clears `playing` before the Session is destroyed. Solo and open pass keep `rounds.start/restart/step` with the same `logRound` arguments as before.
- One test beyond the plan's list, `HiddenPassTest.SeatOnesLateFrameIsNeverDrawnAfterTheBlank`, holds seat 1's queued tap in the VM across the hand-off so seat 1's late frame is the newest one when the match asks for seat 2: the loop asks for no render and a render pushes nothing until seat 2's frame is published. Removing either seat gate (loop or `renderCanvas`) fails it.
- The Result banner is a framed `fui::button` with no action, 4/5 of the safe width and two theme rows tall, `spaceLg` above the safe area's bottom, over a `fui::tapZones` zone covering the safe area; its text is formatted into a 96-byte buffer on the render task. Result pushes FAST with no button hints; the error view still pushes FULL.

Review patches (pass 1, applied by this session; Review Triage Log rows 1-15):
- `GameVM::showSeatNow` drains the input queue through `stepHandOff` under the view it leaves before it draws the next seat (row 1).
- `GameMatchActivity`: the banner's `fui::ButtonProps` and text are render-task members (`bannerProps`, `bannerText`); `passScreenShown` (a `MatchState`, written by render after it pushes Result's or HandOff's own screen, reset to Starting by `handle()`) gates the pass tap and Confirm in `loopHandOff` instead of `routingReady()`; `pauseInGap()` also needs `roundsStartedAwaited > 1`, and `handle()` sets `gapWhenPaused` from it.
- Tests: `GameVmTest`'s hidden cases lose their sleeps (the drain decides the outcome) and gain `ATimerDueInResultIsHeldForTheNextSeat`; `HiddenPassTest` gains `ATapStillQueuedWhenTheNextSeatIsAskedForReachesTheMoverNotTheNextSeat`, `ATapBeforeResultsBannerIsOnThePanelIsDropped`, `ACallStuckInResultIsStoppedIntoTheErrorView`, `TheBlankIsOnlyTheEyeClosedIconCentredOnTheCanvas`, a pause inside the seat gate, pauses on the first blank and the post-Play-again blank, and Result's redraw request. Removing the drain fails two of them; letting the pass tap through on `routingReady()` alone, or opening `canvasUnderView`'s seat gate, fails two others (checked, then restored).
- `screen_stubs/GfxRenderer.h`: `textsOnScreen` aborts without `keepCalls`. Docs: the hidden states section moved before `## The views`; the taps and moves-in-Result paragraphs follow the patches. Fixture and README: only the timer depends on device timing. `ManifestTest`: `pass-hidden` must say hidden. `## 5.4` defers the solo gap's pause menu over the last round's frame (row 17).
- The doubles (R13): the renderer double's `Shown::texts` stands in for `GfxRenderer::displayBuffer` pushing the whole framebuffer and `clearScreen` wiping it; `RecordingTarget::text`'s note stands in for `FreeInkUIGfxRenderer::text` drawing into that framebuffer. `MatchTest.APushHoldsTheCanvasAndUiTextsDrawnSinceTheLastClearScreenAndNoneBeforeIt` pins both. The model holds texts only and is stricter than the panel in one way (a text covered by a later white fill still counts as shown), which can only over-report a secret.

## Plan Change Log

## Review Triage Log

Pass 1 (2026-10-01). The four lenses ran as context-free subagents, launched together in one message in the foreground (each with the worktree read-only), and all four returned: blind-hunter, edge-case-hunter, verification-gap, intent-alignment. The intent auditor's report came back to this session; the other three hand-backs reached the orchestrator, which relayed them verbatim (saved in the orchestrator's scratchpad, `5.4/relayed/`). Verdicts: high 1, medium 3, low 21, false 3, maybe-false 0 (plus the intent auditor's descriptive report). No intent_gap or bad_plan: every accepted fix is small, private, and inside the plan's design, so the patches were applied by this session (the implementer's reply would have gone to the orchestrator) and verification re-run.

| # | Lens | Finding | Verdict | Route | Evidence / action |
|---|------|---------|---------|-------|-------------------|
| 1 | edge, intent (5) | A mover's tap still queued when the seat request is served is played under the Playing view: it reaches the next seat's `input` and its move can be applied | high | patch | Real: `run()` serves the request ahead of the queue and nothing drained it. `showSeatNow` now first drains the queue through `stepHandOff` under the view it leaves (Result: the mover, move discarded; HandOff: dropped; timers held); the match posts no game input until the gate opens, so every queued event predates the hand-off. Test added. |
| 2 | blind (1) | `buildHandOffView` puts `fui::ButtonProps` (about 640 B on the host) and a `BoxStyle` on the render task's stack, over AGENTS.md's 256 B | medium | patch | Real. The banner's props and the formatted text are render-task-only members, as `dialogProps` is. |
| 3 | blind (4), edge (2) | Result's tap zone is published before its push, so a quick second tap or Confirm skips the banner | medium | patch | Real: `renderView` calls `renderUi()` before `displayBuffer`. A render-written `passScreenShown` flag, set after the push of Result's or HandOff's screen and cleared by `handle()`, now gates both the routed tap and Confirm. Test added. |
| 4 | verification-gap (2) | No test pauses between the hand-off tap and the seat's first frame, where only `canvasUnderView`'s seat gate keeps the last seat's frame out from under the menu | medium | patch | Real gap on the leak guard. Test added to `SeatOnesLateFrameIsNeverDrawnAfterTheBlank`. |
| 5 | blind (6, 7), edge (5, 6) | `GameVmTest`'s two hidden cases sleep 20 ms for the VM to take an event; under load they flake, or pass without the hold running; the already-queued timer path is untested | low | patch | Real. With row 1 the drain decides both outcomes whatever the timing, so the sleeps go and each test asserts the outcome; the queued-timer path is the timer test's. |
| 6 | edge (3) | A pause before a new match's first frame says "Starting the next round" | low | patch | Real: `roundsStartedAwaited` starts at 1. The line now also needs a Play again (`roundsStartedAwaited > 1`). Direct correction. |
| 7 | verification-gap (1) | The `resumesTo == Playing` half of the line's rule is not pinned: no hidden test pauses on the first blank or the blank after Play again | low | patch | Test added (both blanks: no line). |
| 8 | verification-gap (3) | Result's redraw for a newer mover frame is never observed | low | patch | Test asserts the render request and the newer frame. |
| 9 | blind (8), verification-gap (4) | No test reaches Error from Result or HandOff | low | patch | Test: a stuck call in Result ends in the error view. |
| 10 | blind (9), verification-gap (5), intent (2) | Nothing checks the blank's `eye-closed` icon, size, or centre | low | patch | Test counts the icon's black runs inside the 128 px square centred on the canvas. |
| 11 | blind (5) | The new section sits above `### Resume` etc., which now read as its subsections | low | patch | Moved to just before `## The views`. |
| 12 | blind (15) | The fixture says nothing depends on device timing, but whether its 5 s timer falls due in a hand-off does | low | patch | Reworded in `main.lua` and the README. |
| 13 | blind (14) | `inputMask` on an action-less button does nothing; `ManifestTest`'s comment still says starting needs solo | low | patch | Line removed; comment corrected. The five style assignments mirror `buildView`'s: rejected. |
| 14 | edge (7) | `noteUiText`/`textsOnScreen` give wrong texts when `keepCalls` is off | low | patch | Aborts with a message, as `fillsOutside` does; no screen suite turns it off. |
| 15 | blind (10) | The manifest's `"hidden": true` is never read into the match's path | low | patch | `ManifestTest.EveryFixtureManifestIsListed` also asserts `pass-hidden` parses hidden (the approved edit's case). A launcher-to-HandOff test is rejected: the launcher builds `Roster::pass` for a pass-only game (entry 1's tests) and the match's flag reads `manifest.hidden` (every HiddenPassTest). |
| 16 | blind (2), edge (4) | Paused in the seat gate, Resume leaves the menu inert with no line | low | reject | The window is one VM draw (plus the drain of row 1), milliseconds; the line's text ("Starting the next round") would be wrong mid-round, and a separate line or redraw adds a string and branches. |
| 17 | blind (3) | In the Play-again gap the pause menu sits over the last round's late frame | low | defer | Pre-existing for solo and open pass (`renderView` always drew the front frame); a hidden match's gap is covered by the seat gate and Paused-from-HandOff. `## 5.4`. |
| 18 | blind (7, second half) | Play again's `timerHeld = false` is untested | low | reject | `beginAgain` cancels the timer, so a held event is stale and `accepts()` drops it anyway; the line only frees the slot early. |
| 19 | blind (11) | `ManifestTest.cpp` is outside the touches and recorded only in Implementation Notes | low | reject | The fix is a plan edit; the orchestrator approved the edit (2026-10-01), now named in Implementation Notes. |
| 20 | blind (12), intent (4) | No verification evidence, screenshots, or flash measurement yet | false | reject | They run after review (AGENTS.md order) and are recorded in Verification before the commit. |
| 21 | blind (13) | `drawBlank` reaches the lifecycle enum through `GameViewIcons`; `if (icon)` is dead | low | reject | One source for the views' icon names is the design (`GameViewIconsTest` checks it); entry 6 calls it the same way. |
| 22 | blind (16) | A pause whose gap closes before its first render asks for one redundant redraw | low | reject | One extra fast refresh in a rare race. |
| 23 | intent (1) | The push model sees texts only, not icons or images | low | reject | `pass-hidden`'s private view is text by design, and the gate is content-independent (request numbers, not content); row 10 checks the blank's icon. |
| 24 | intent (3) | The doubles are pinned to their documented device model, not to device code | false | reject | R13 asks a test to pin the double's stated device behaviour; the device code (`FreeInkUIGfxRenderer`, `GfxRenderer::displayBuffer`) cannot link on the host, and the comments name it. |
| 25 | intent (8) | R5's resumed match is not handled | false | reject | Entry 9 owns pass resume ("starts a hidden match at HandOff"); pass writes no `resume.bin` yet, so no resume reaches a hidden match. |
| 26 | intent (6, 7) | Descriptive: the line's scope, and shared code paths changing for solo | low | reject | Rows 6 and 7 settle the scope; the solo changes are counters that stay 0 and the R17 line, with every existing test unchanged. |

## Design Notes

**Why the VM picks the seat, the match only asks:** the seat depends on the status, which lives on the VM task; the match names only the moment (`showTurnSeat`). The served request number is the frame's seat tag the entry's unknown asked for, without touching `FrameBuffers`: the VM stores it after the publish, so a render that sees it reach `seatAwaited` takes that seat's frame or a later one of the same seat (no other seat is drawn before the next turn change, which needs a served request first). Reading `seatRequests` before `takePlayAgain()` keeps a request made after Play again from being served in the old round (the loop sets the two in that order).

**Guards kept** (`git log -L` of `GameVM::run`, `GameMatchActivity::handle`, `loopPlaying`, `renderCanvas`): `publishCommitted` after every Session call, a Cancelled one only when settled; Play again before the queue; `roundsStartedAwaited` stored before `shown`; Back first, then `vmHealthy`, then RoundOver before any gesture is read; gestures read and dropped while awaiting display; every way past `renderCanvas`'s gate stores the displayed counts; `renderView` closes routing a state change raced. New code adds branches beside them; it moves only Playing's Play-again lines into `startNextRound()`, unchanged.

**Taps:** the Result banner and the hand-off accept a tap anywhere (a `fui::tapZones` over the safe rect, routed by `routeTouch`, so no hand-rolled hit test) and Confirm, gated on `routingReady()` like a tap, so a double tap or press cannot skip the blank.

## Verification

**Commands:**
- Host tests under `/tmp/crosshatch-hosttest.lock` (AGENTS.md) -- expected: all pass, no `-Wswitch` warning left.
- `for t in scripts/*_test.py; do python3 $t; done`; `python3 scripts/pack_device_run_test.py`; `python3 scripts/check_layers.py`; `python3 scripts/check_upstream_touches.py`; `python3 scripts/pack_game.py test/game_script/fixtures/pass-hidden <scratch>`; `./bin/clang-format-fix` twice -- expected: pass, nothing new.
- Under `/tmp/crosshatch-build.lock` with the shared cache: `pio run -e x4pro`, `pio run -e default`, `pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high` -- expected: success, no defects.
- `check_flash_budget.py` `build on`, `build off`, `compare --limit-kib 250 --ram-limit-bytes 1024`, `objects` on the commit's tree -- expected: within the bar; recorded with the commit.

**Results (2026-10-01, after the review patches, on the tree this entry commits):**
- Host suites: CMake/Ninja configure, build, and `ctest -j4` under `/tmp/crosshatch-hosttest.lock`: 1,455 of 1,455 pass. No warning from the changed sources (the `-Wswitch` warnings of `## 5.2` are gone). `HiddenPassTest.*`, the hidden `GameVmTest` cases, `PlayAgainGapTest.*`, `PassMatchTest.*`, and the double's pin: `--gtest_repeat=30`, no failure.
- Every `scripts/*_test.py` (12) passes, `pack_device_run_test.py` included; `check_layers.py`: 483 include edges pass, no new component edge; `check_upstream_touches.py`: PASS (`english.yaml`, ledger row 2); `pack_game.py test/game_script/fixtures/pass-hidden`: packs (hash `5e20548ec727951d`); `./bin/clang-format-fix` twice: nothing new, nothing outside these paths.
- Under `/tmp/crosshatch-build.lock` with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`: `pio run -e x4pro` SUCCESS, `pio run -e default` SUCCESS, `pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high` PASSED.
- `check_flash_budget.py` `build on`, `build off`, `compare --limit-kib 250 --ram-limit-bytes 1024`, `objects` (same lock and cache), on the working tree whose sources are this entry's commit (only this plan's text changed after the measurement): games on 5,916,912 B, off 5,679,360 B, **+237,552 B** (18,448 B under the gate); static internal RAM **+784 B** (`.dram0.bss` +16, `.iram0.text` +684, `.iram0.text_end` +84; 240 B under the gate); `objects`: 43 game objects, no static initializer, largest mutable static 4 B. Over the orchestrator's base at `c1902721` (+233,440 B, +784 B, measured the same way): **+4,112 B flash and +0 B static RAM** for the epic so far (entries 1, 2, and 4), within 11,152 B and 32 B, leaving 7,040 B and 32 B.
- No fork-only CI gate changed, so no fresh-tree run.
- Simulator (`sim.sh build x4pro`, `pass-hidden` and `slow-restart` packed by `pack_game.py` into `fs_/games/`), screenshots in `story-hand-off-screenshots/`:
  - `1-handoff-new-match.png`: Games, Pass hidden (a pass-only game starts its pass match directly): the blank hand-off screen, the `eye-closed` icon centred, before any seat is drawn.
  - `2-seat1.png`: the tap on the blank: seat 1's frame ("Player 1's secret: apple", Moves: 0).
  - `3-result-banner.png`: seat 1's move: Result, seat 1's own frame (Moves: 1) with the "Tap to pass to player 2" banner at the bottom.
  - `4-handoff-blank.png`: the tap on the banner: the blank again (identical to 1).
  - `5-seat2.png`: the tap on the blank: seat 2's frame ("Player 2's secret: river", Moves: 1); the log shows `Result -> HandOff on Tap`, `HandOff -> Playing on Tap`.
  - `6-gap-pause-menu.png`: `slow-restart`, Play again, then Back within its 2 s setup: the pause menu with "Starting the next round" under "Paused" (over the last round's frame, row 17's deferral).
  - `7-gap-menu-after-round-starts.png`: the same menu redrawn once round 2's first frame is published, without the line, over round 2's frame.
- Layout points for the owner's question: the banner is a framed panel, 4/5 of the safe width, two theme rows tall, near the bottom; the blank has only the 128 px icon; the gap line is left-aligned under the headline (the option dialog's message style).

**Manual checks:**
- `sim.sh build x4pro`; screenshots copied to `story-hand-off-screenshots/`: `1-handoff-new-match.png`, `2-seat1.png`, `3-result-banner.png`, `4-handoff-blank.png`, `5-seat2.png`, `6-gap-pause-menu.png`.
