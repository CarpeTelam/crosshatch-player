---
title: 'e5-xr: fix epic-pass-and-play cross-story review rows 1-14'
type: 'bugfix'
ticket: ''
created: '2026-10-01'
status: 'built'
baseline_revision: 'a002936c59c75530742f2bac02716f3f6140e3c0'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-pass-and-play/cross-story-review.md'
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-pass-and-play/epic-pass-and-play.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** The cross-story review of epic-pass-and-play (`cross-story-review.md`, rows 1-14, verdict `fix`) found seams between the nine merged stories: a hidden seat's frame left on the panel after a Leave whose Games screen ran out of memory (1), a save this host cannot start replaced by New without asking (2, 13), Continue falling back to a pass match (3), queued input reaching seat 0 or the next seat (4, 5), a full refresh on every hand-off repaint (6), a shared dialog in the wrong header with a racy focus (7), `seatShown` not failing closed in Playing (8), an unused lifecycle overload (9), a refusal check that reuses the live store (10), a stale comment (11), and missing or weak tests (12, 14).

**Approach:** One commit fixing each row as its Reason/action says, smallest option where a row offers two, inside the brief's touches; R1-R17 of the epic bind every choice.

## Boundaries & Constraints

**Always:** shared code builds for the C3 (`default`); nothrow allocation, locals under 256 B, `LOG_*`, `tr()` for text (one appended `STR_GAMES_*` key); never take `RenderLock` where it is held (onExit); R6's forced-exit order stays (blank after the VM's wait, before the first SD step); R7's "from then on draw and input get seat 0" stays true.

**Never:** files outside the brief's touches (`LuaGame.*`, `MatchLifecycle.*`, `Session.*`, `Roster.*`, `Manifest.*`, `GameEvent.h`, `GameInput.*`, installer, CI, `api-level-1.txt`, the epic file); row 15 (narrowing the forced-exit blank by state), rows 16-18.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Leave over a seat (1) | hidden match, pause menu over seat N's frame, Leave | blank pushed (HALF, no texts) before `goToGames()`; onExit after it pushes nothing | goToGames OOM leaves the blank on the panel |
| Unstartable save (2, 13) | pass save, host without pass or with fewer seats; unknown mode byte | `peek` → `Unstartable`; no Continue row; a New row asks first; Cancel keeps bytes | Continue reaching it ends in the error view with the new reason, file byte-identical |
| Continue, save gone (3) | solo+pass game, save removed after peek | new match in the first New row's mode (solo) | pass-only game with no pass seats starts nothing |
| Queued input (4, 5) | tap queued behind a winning move; open pass double tap | dropped: never `input` for seat 0 or the next seat | timers are not dropped (R11) |
| Hand-off repaint (6) | render again in HandOff with the blank on the panel | FAST_REFRESH, not FULL | entering HandOff from any other screen stays FULL |

</frozen-after-approval>

## Code Map

- `src/activities/games/GameMatchActivity.{h,cpp}` -- `leave()` :347, `onExit()` :236, `stopVm()` :358, `pushForcedExitBlank()` :373, `seedResume()` :191, `savedForAnotherHost()` :221 (removed), `renderCanvas` :680, `renderView` :744, `renderHandOff` :728, `loopPlaying` postInput :537; header comment :21.
- `src/games/GameVM.{h,cpp}` -- `postInput`, `run()` open path (`rounds.start/restart/step`), `drawShown`, `stepHandOff`, `playing` (set for hidden only today).
- `lib/GameScript/SoloRounds.{h,cpp}` -- `shownSeat()` private; make public for GameVM.
- `lib/GameCore/SeatShown.h` -- Playing returns `status.turn` (:45); lifecycle overload :41 (no production caller).
- `src/games/GameSaveStore.{h,cpp}` -- `SaveState`, `peekStartable`, `keptQuietly`, `"unknown mode"`; formats.md :178-217 documents it.
- `src/activities/games/GameModeActivity.{h,cpp}` -- `GameConfirmDialog` (h:18, cpp:59-115), `hasSave`, `buildRows`, `activateIndex`, `startNew`, `startResume`, `saveName`. Launcher: `GamesLauncherActivity.h:8` includes `GameModeActivity.h` only for the dialog; `.cpp` keeps its include (it pushes the title screen).
- Tests: `test/game_core/SeatShownTest.cpp` (lifecycle tests :137-245); `test/game_script/GameSaveStoreTest.cpp` (`ASaveTheGameOrHostCannotStartIsNoneToBothAndKept` :1019, `APassSaveIsNoneToTheSoloOnlyFormsAndIsKept` :997, `AnUnknownModeIsDiscardedByTheNewFormsAndKept` :1094); `harness/GameMatchTest.cpp` `HiddenPassTest` (:1238; `expectBlank`, `LeavingAHiddenMatchPushesNoHalfRefresh` :1926); `harness/ResumeMatchTest.cpp` (:1336 counts only seat 1); `harness/ModePickerTest.cpp` `TitleScreenTest` (`save`, `openTitleThroughLauncher`, `rowsDrawn`, `dialogUp`, `fakesd::bytesOf`, `match::installFixture`), target ModePickerHarnessTest; `harness/GameVmTest.cpp`; `SoloRoundsTest.cpp` `PassRoundsTest.ATapAfterThePassRoundEndsReachesSeatZero...` :395 (stays: R7 at the rounds layer). FakeRtos gate: `fakertos::arm(At::Log, skip)`, `waitParked`, `pass`, `release`; ActivityManager double's `onGoToGames` hook.

## Tasks & Acceptance

**Execution:**
- [x] `lib/GameCore/SeatShown.h` -- Playing: `roster.isLocal(status.turn) ? status.turn : NO_SEAT` (8); delete the lifecycle overload and fix the state form's comment (9).
- [x] `lib/GameScript/SoloRounds.{h,cpp}` -- `shownSeat()` public (GameVM notes the seat drawn).
- [x] `src/games/GameVM.{h,cpp}` -- `postInput(event, shownFrame = UINT32_MAX)`: a touch event carries the frame the panel showed when it was posted (in `serial`, unused for touch, zeroed before the event goes on); the VM keeps `drawnSeat` and `seatFrame` (frameGen of the first frame of the seat now drawn, VM task) and drops a touch posted under an earlier frame than `seatFrame`, in the open path before `rounds.step` and in `stepHandOff` before `rounds.play`; timers pass (4, 5).
- [x] `src/activities/games/GameMatchActivity.{h,cpp}` -- `frameDisplayed` atomic stored after renderCanvas's push, passed to `postInput` (5); a `Panel {Other, Seat, Blank}` member guarded by RenderLock set by every push (renderCanvas → Seat; renderView → Seat with canvas unless Over, else Other; blank → Blank); renderHandOff pushes FAST when Blank, FULL otherwise (6); `pushBlank()` shared by `pushForcedExitBlank`, `leave()` (inside its lock, after stopVm, hidden and Seat only) and onExit with no VM (hidden and Seat) (1); `seedResume` refuses via `store.saves().peekResume(manifest, gameHostCaps())` (Unstartable → new reason, Unreadable → `STR_GAMES_RESUME_FAILED`), `savedForAnotherHost` removed (2, 10); header comment (11).
- [x] `src/games/GameSaveStore.{h,cpp}` -- `SaveState::Unstartable` for a save of this package whose mode or seats this host cannot start or whose mode byte is unknown (`keptQuietly` plus `unknown mode`), logged at INF as kept; const member `peekResume(game, host)` (the store's id and hash, its own buffer; touches neither `buffer` nor `roster`) (2, 10).
- [x] `src/activities/games/GameConfirmDialog.h` (new) -- the dialog with inline definitions, `std::atomic<uint8_t> focus`; GameModeActivity and GamesLauncherActivity include it (7).
- [x] `src/activities/games/GameModeActivity.{h,cpp}` -- `canContinue` (Valid, Unreadable) for the Continue row, `hasSave` (also Unstartable) for the question; `rosterFor(kind, roster)` shared by `startNew` and `startResume`, which passes the first New row's roster (3).
- [x] `lib/I18n/translations/english.yaml` -- append `STR_GAMES_RESUME_OTHER_HOST` (2).
- [x] `docs/crosshatch/formats.md`, `docs/crosshatch/game-canvas.md` -- the four peek states and the match's refusal; Leave's blank, the no-VM exit, the hand-off repaint, input posted under another seat's frame.
- [x] Tests -- SeatShownTest: drop the lifecycle tests, add a 3-seat roster with seats 1-2 local (turn 3 → NO_SEAT). GameSaveStoreTest: Unstartable for not-startable and unknown-mode rows (bad seat count stays None), solo-only peek of a pass save → Unstartable, `peekResume` leaves the store's roster. GameVmTest (gate at Log): open pass, a tap queued behind seat 1's turn-passing move never reaches seat 2; pass-open and pass-hidden, a tap queued behind the winning move gives no `tap for seat 0`; a timer still reaches the turn seat. HiddenPassTest: a repaint in HandOff is FAST with no texts; Leave from a seat's frame pushes one HALF blank before `goToGames` (onGoToGames hook) and onExit after it nothing; Leave from Over or the blank's pause menu pushes nothing; a forced exit with no VM after a stuck call pushes the blank before the error view is drawn and nothing after; the lag test (a tap queued behind a turn-passing move is played while the match is already in HandOff: no push holds the mover's text after the blank until the next seat's frame). ResumeMatchTest :1336 counts `"draw for seat "`; a Continue on an Unstartable save shows the new reason and Back leaves the bytes identical. TitleScreenTest: pass save on a host without pass, and an n=3 save on a 2-seat host: no Continue, New asks, Cancel keeps bytes; Continue with the save removed after the peek starts solo; end-to-end `pass-hidden` from disk through the launcher row and Pass to HandOff (FULL, no texts before any secret), Over, Play again → HandOff.

**Acceptance Criteria:**
- Given any change above, when the host suites run, then every suite passes and the tests named above fail on the pre-fix code.
- Given the firmware, when `x4pro` and `default` build and `pio check` runs on both envs, then no defect is reported and the flash delta over the base stays within 5,056 B and 32 B left.

## Implementation Notes

- **Frame tag value.** `frameDisplayed` stores the `frameGen()` that `renderCanvas` read before `drawFront`, a lower bound of the frame actually pushed (a frame published in between is coalesced in). A touch can therefore only be tagged older than the panel, never newer: the error direction drops (fails closed), and only in the one render that races a seat change. Stored before `roundsDisplayed`/`seatDisplayed`, so a loop that passes the display gate posts with it.
- **`peekResume` answers `Valid` after `loadResume` refused.** Only when the file changed between the two reads; `seedResume` refuses it like a second-read fault (`STR_GAMES_RESUME_FAILED`, file kept), so no new match starts over a save that reads.
- **Drop log.** The VM logs each dropped touch at `LOG_INF` ("Dropped a touch made under frame ..."); only a drop logs, so no existing gate count moved (the full suite passes unchanged in those tests).
- **Test follow-through outside the named tests.** `GameSaveStoreTest.AResumeThatDoesNotFitTheGameIsDiscardedWithALogLineAndKept` lost its two `unknown mode` rows (now Unstartable, pinned in `AnUnknownModeIsUnstartableAndKept`); `ResumeMatchTest`'s `WithNoUsableSaveContinueStartsANewMatchWithTheCallersRoster` lost its `unknown mode` and `seats not startable` rows, which moved to `ASaveThisHostCannotStartStopsInTheErrorViewWithItsOwnReasonAndIsKept` (with a pass-only game's solo save); five `TitleScreenTest` Continue cases now log the solo roster (row 3), and `ATwoModeGamesSaveThatWentBad...` became two solo tests (went bad, removed).
- **Mutation check.** With each fix's behaviour reverted in place (tag check, Leave/no-VM blank, FAST repaint, `hasSave`, the new reason, `seatShown`), the 11 new behaviour tests failed and the rest passed; restored afterwards.
- **Review patch: second-read refusal tested.** `fakesd::Card::onOpen` (a per-path open count and hook, standing in for a transient SD fault or a file rewritten between two reads; pinned in `HarnessDoublesTest.OpeningCallsTheHookWithThePathsOpenCountBeforeTheOpenIsDecided`) lets `PassResumeTest.ASecondReadThatFaultsOrFindsAChangedSaveStopsInTheErrorViewAndKeepsIt` fault or rewrite `peekResume`'s open.
- **Review patch: match-level frame tag tested.** `PassMatchTest.ATapMadeUnderSeatOnesFrameBehindItsTurnPassingMoveNeverReachesSeatTwo` and `...BehindTheRoundEndingMoveNeverReachesSeatZero` go through `tapCanvas`, so `loopPlaying`'s `frameDisplayed` is pinned.
- **Review patch: `loadResume` logs an Unstartable refusal at `LOG_INF` as kept** (`keptQuietly` renamed `unstartableHere`); formats.md says so.
- **Review patch: `peekResume` gates on `resumeOn()`** (no read with a nearby roster, as `loadResume`).
- **Review patch: Continue takes the first New row whose `rosterFor` succeeds** (`TitleScreenTest.AContinueWhoseFirstNewRowCannotStartTakesTheNextRowThatCan`).
- **Review patch: key renamed `STR_GAMES_RESUME_NOT_HERE`**, reworded "This saved game cannot be continued on this device. It is kept until you start a new game."
- **Review patch: docs** rewritten and rewrapped at 120 (formats.md's `peekResume` and Continue paragraphs, game-canvas.md's Resume and Leaving paragraphs), and the touch note says the tag is the frame render read before `drawFront`.

## Plan Change Log

## Review Triage Log

**Pass 1 (2026-10-01).** All four lenses ran as context-free subagents, launched together, and all four returned: blind-hunter (BH), edge-case-hunter (EC), verification-gap (VG), intent-alignment (IA). Verdicts: high 0, medium 2, low 10, false 3, maybe-false 0 (IA is descriptive; its one actionable divergence is logged as IA1). Routes: patch 7 entries, defer 3, reject the rest; no intent_gap or bad_plan, so no loopback.

| # | Lens | Finding | Verdict | Route | Evidence / action |
|---|---|---|---|---|---|
| 1 | VG1, BH6 | `seedResume`'s second-read refusal (`peekResume` Unreadable or Valid) is never exercised | medium | patch | Pre-verified gap; the only refusal test covers Unstartable. Added a match-level test with the second read failing. |
| 2 | VG2, BH7 | The loop's frame tag (`frameDisplayed` into `postInput`) is untested at the match level | medium | patch | Pre-verified; GameVmTest posts its own tags. Added two open-pass `PassMatchTest` cases through `tapCanvas`. |
| 3 | BH5 | `loadResume` logs an Unstartable refusal at ERR as "discarded" though the file is kept; `keptQuietly` names the wrong thing | low | patch | Real: `loadStartable` logs every refusal "discarded"; the device run (entry 11) reads these logs. Logged at INF as kept, helper renamed, formats.md updated. |
| 4 | EC5, VG other | `peekResume` gates only on the hash, not `resumeOn()` (nearby roster) | low | patch | Real but unreachable (the title screen maps nearby to solo); a one-line correction to match its comment. |
| 5 | EC6 | Continue's fallback takes only the first New row, ignoring a later startable one | low | patch | Real when pass is listed but no seat count fits; row 3 says "first startable mode". Now the first row whose roster starts. |
| 6 | BH9 | `STR_GAMES_RESUME_OTHER_HOST` covers more than another host; its text names "a New game", no row's label | low | patch | Real; key renamed and text reworded. |
| 7 | BH10 | Edited docs paragraphs read badly and are unwrapped | low | patch | Real; rewritten and rewrapped, plus a note on the tag's fail-closed race (BH7's second half, EC2). |
| 8 | BH2 | `GameEvent::serial` documented "Timer only" while it carries the touch tag | low | defer | Real for a reader of `GameEvent.h`; the game never sees a nonzero tag (every path that hands an event on passes `madeUnderAnotherSeat` or discards it). `GameEvent.h` is outside touches. |
| 9 | BH4 | `confirmRow` / `removeIndex` cross tasks unsynchronised like `focus` was | low | defer | Pre-existing, read once per build by design; row 7 asked for `focus` only. |
| 10 | EC4 | A later firmware's file or codec version stays `None` and is replaced | low | defer | Pre-existing; row 2 names modes, seats, and the mode byte. |
| 11 | BH1, EC1 | `SoloRounds` passes `NO_SEAT` to `draw`/`play` for a partly-local roster | low | reject | No roster has several local seats but not all (pass: all; solo, nearby: one, which returns `firstLocalSeat` before the Playing branch); the fix adds guards. Before e5-xr the same roster drew another device's seat. |
| 12 | BH3 | `postInput`'s default `UINT32_MAX` tag never drops | low | reject | One production caller, which passes the tag (now pinned by row 2's tests); making it required rewrites 20 test calls. |
| 13 | BH8 | An unknown mode byte alone makes a save `Unstartable`, so a corrupt byte hides Continue | false | reject | Cross-story row 2 lists "an unknown mode byte" as a save to keep; the outcome is the file kept and New asking first, no loss. |
| 14 | BH11 | `rosterFor` and `startNew` both switch on the mode; log verbs | low | reject | Cosmetic log wording; refactor beyond a direct correction. |
| 15 | BH12 | `GameConfirmDialog` header-only, should be `.h/.cpp` | false | reject | The brief's touches name only a new `GameConfirmDialog.h`; a `.cpp` would be outside them. Flash measured below. |
| 16 | BH13 | The end-to-end test copies HiddenPassTest's helpers | low | reject | Test-only duplication across two fixtures in different suites; refactor, not a correction. |
| 17 | BH14 | Deleted SeatShown lifecycle tests have no replacement | false | reject | Paused-from-Result and Paused-from-HandOff stay pinned at the match level: `HiddenPassTest.PauseFromResultOrTheBlankReturnsThereAndTheBlanksPauseMenuSitsOnNoFrame`, plus the new lag test. |
| 18 | BH15 | `Panel::Seat` set for seat 0's frame before Over, so a pause then Leave pushes an unneeded blank | low | reject | Over-approximation fails safe (one extra half refresh in a rare path); exact tracking needs the VM's seat on the render side. |
| 19 | EC3 | A gesture read after render stored the next seat's frame is tagged with it | low | reject | Needs a touch made while the next seat's frame is being pushed and read after the push returns; the touch has no timestamp of its own, and the same convention gates `roundsDisplayed`. |
| 20 | EC2 | `frameDisplayed` is the frame read before `drawFront`, so the new seat's first touches may drop until the next render | low | reject | Fails closed (drops, never misroutes) and the loop asks for the next render at once; documented in game-canvas.md (row 7). |
| 21 | IA1 | Row 4: a Timer queued behind the winning move still reaches `input` with seat 0 | low | reject | R7 and game-api-seed §3 give `input` seat 0 after the round; the tag drops touches only, R11 keeps timers. Row 4's literal action (drop all seat-0 input) would end R7's clause (Design Notes). |

## Design Notes

- **Row 4 vs R7.** The row's action (drop every seat-0 input) would end R7's "from then on ... input get seat 0" and game-api-seed §3, which `PassRoundsTest` pins. The trigger is an event posted while a seat 1..n frame showed and played after the round ended; row 5's tag drops exactly that (the seat drawn changed since it was posted), so one mechanism fixes both and R7 stays: the Over menu posts no game input, and any input posted under seat 0's frame would still reach seat 0.
- **Tag in `serial`.** `GameEvent` and `InputQueue` are outside touches; `serial` is read only for `Timer` (`GameTimer::accepts`), and GameVM zeroes it before the event leaves. Default `UINT32_MAX` keeps direct callers (tests) unchanged.
- **Row 10 option:** a store-free check (`peekResume`), consistent with the title screen by construction; it replaces `savedForAnotherHost`, whose guards were (a) only the host is widened, so a save the game cannot start anywhere is not kept, now widened on purpose to row 2's set (a pre-epic solo save of a pass-only game is kept), and (b) a read that faults refuses: kept (`Unreadable` refuses).
- **Row 3 option:** first New row's mode, not the error view: no new screen path, and the guard "a pass-only game with no pass seats starts nothing" stays in `rosterFor`.
- **Row 9 option:** delete the overload; `canvasUnderView` keeps the Paused-from-HandOff rule, pinned by the lag test.
- **Row 6/1 flag:** one render-side record of what the last push left on the panel, read under RenderLock by leave() and onExit; Over counts as Other, since seat 0's frame is everyone's.
- Guards kept: `stopVm`'s `!vm` return and its push-before-flushResume order (R6); `startResume`'s no-pass-seats return; `leave()`'s stop under its own RenderLock.

## Verification

**Commands:**
- `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test' && ctest --test-dir build/test --output-on-failure -j` -- expected: all pass.
- `for t in scripts/*_test.py; do python3 $t; done`, `python3 scripts/check_layers.py`, `python3 scripts/check_upstream_touches.py`, `./bin/clang-format-fix` -- expected: pass, no diff after format.
- `flock /tmp/crosshatch-build.lock sh -c 'pio run -e x4pro && pio run -e default && pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high && pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high'` -- expected: success.
- `check_flash_budget.py` `build on`, `build off`, `compare --limit-kib 250 --ram-limit-bytes 1024`, `objects` under the build lock -- expected: delta over the base (+233,440 B, +784 B at `c1902721`) within +238,496 B and +816 B.
- `sim.sh build x4pro` under the build lock -- expected: builds.

**Results (2026-10-01, after the review patches; the tree measured is this commit's, whose firmware sources the commit does not change):**
- Host suites: 1,514 of 1,514 pass (`ctest --test-dir build/test`, Ninja `build/test`). The implementer reverted each fix in place and the new behaviour tests failed, then restored.
- `scripts/*_test.py` all pass; `check_layers.py` passes (495 edges, no new one: `GameConfirmDialog.h` includes only what Screens already reach); `check_upstream_touches.py` PASS (no upstream file but `english.yaml`, ledger row 2); `./bin/clang-format-fix` clean, and a second run changes nothing.
- `pio run -e default`: success. `check_flash_budget.py build on` (x4pro) and `build off`: success.
- `pio check` (`default`) and `pio check -e x4pro`, `--fail-on-defect low/medium/high`: no defects, PASSED.
- `sim.sh build x4pro`: SUCCESS. No screenshots: the Verification names none.
- Flash (measured, `check_flash_budget.py` `build on`, `build off`, `compare --limit-kib 250 --ram-limit-bytes 1024`, `objects`, under the build lock): x4pro `firmware.bin` 5,920,640 B games on, 5,679,792 B off, +240,848 B (15,152 B under the gate); static internal RAM +784 B (`.dram0.bss` +16, `.iram0.text` +684, `.iram0.text_end` +84; 240 B under the gate); `objects` clean (43 objects, largest mutable static 4 B). Over the base at `c1902721` (+233,440 B, +784 B): +7,408 B flash and +0 B static RAM, within the 11,152 B and 32 B share; +1,312 B over entry 10's +6,096 B (both games-on minus games-off, measured the same way), leaving 3,744 B and 32 B of the share.
- Test double extended: `fakesd::Card::onOpen` and its per-path open count (`test/game_script/harness/stubs/HalStorage.h`) stand in for a transient SD open fault or a file rewritten between two reads, pinned by `HarnessDoublesTest.OpeningCallsTheHookWithThePathsOpenCountBeforeTheOpenIsDecided`; it only injects, so it is no more permissive than the device.

