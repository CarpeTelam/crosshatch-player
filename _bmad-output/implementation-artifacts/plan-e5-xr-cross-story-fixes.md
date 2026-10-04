---
title: 'e5-xr: fix epic-pass-and-play cross-story review rows 1-14'
type: 'bugfix'
ticket: ''
created: '2026-10-01'
status: 'done'
baseline_revision: 'a002936c59c75530742f2bac02716f3f6140e3c0'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment', 'blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment', 'blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
followup_baseline_revision: '00ca40702f7b0ba3fb4cbfdcbc87c38d3e5ec16f'
last_round_baseline_revision: 'e47e33d9'
h_round_baseline_revision: '0c18e437'
j_round_baseline_revision: '2ae3587f'
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

**Follow-up execution (pass 2: the orchestrator's review of `ef46f8a5`, rows F1-F13 of `cross-story-review.md` "Review of the fix commit"; one follow-up commit; touches as above plus `lib/GameCore/GameEvent.h`, comment only):**
- [x] F1 `src/games/GameSaveStore.cpp` `readResume` -- read the prefix even when the file is too large; classify as kept (Unstartable, logged INF): a `CHRS` header whose file version byte is above `RESUME_FILE_VERSION` (its layout is unknown, so no hash check: it sits in this game's folder); a codec version byte above `Codec::VERSION` with this package's hash; a header that checks `ok` with this package's hash and a snapshot over this firmware's limit ("too large"). Other header failures, and those of another package, stay None. Tests in `GameSaveStoreTest` (each kind, and an older or garbage version stays None); formats.md.
- [x] F2 `src/games/GameVM.{h,cpp}`, `GameMatchActivity.cpp` -- `drawFront` also reports the `frameGen()` it drew, read inside `takeFront` under the frame mutex (publish bumps it under the same mutex), and `renderCanvas` stores that in `frameDisplayed`; `madeUnderAnotherSeat` compares wrap-safe (`static_cast<int32_t>(shownFrame - seatFrame) < 0`), `postInput`'s untagged default is a named constant checked first, and the loop never posts a real tag equal to it (it posts one less, which fails closed). Test: a publish between renderCanvas's `frameGen()` read and `drawFront` (gate the VM, or a hook) leaves the new seat's next tap delivered.
- [x] F3 `GameModeActivity.{h,cpp}`, `GamesLauncherActivity.{h,cpp}` -- `confirmRow` and `removeIndex` become atomics; each opener stores `focus = 0` first, then the row with release; readers load acquire once.
- [x] F4 `lib/GameScript/SoloRounds.cpp` -- `start`, `restart`, `step`: when `shownSeat()` is `NO_SEAT`, return Ok with no input and no draw. Test in `SoloRoundsTest` with a roster of two local seats out of three, turn on the third.
- [x] F5 `src/games/GameVM.cpp` `showSeatNow` -- when the turn seat is not local (`seatShown(Playing, …)` is `NO_SEAT`), stay in HandOff and leave `seatServed` unchanged. Test in GameVmTest if a roster can be built (else note why not).
- [x] F6 `src/games/GameSaveStore.{h,cpp}` -- `peekResume` (no longer const) checks through the store's own buffer (nothing references it after an empty `loadResume`), never allocates, never touches the roster; static `peek` keeps its own allocation; fix `peekStartable`'s comment; update `PeekResume...` test (buffer spent, roster kept, no allocation).
- [x] F7 `test/game_script/harness/GameVmTest.cpp` -- open pass (`pass-open`): a timer queued behind the winning move reaches seat 0's `input` ("timer for seat 0"), the VM does not fail, and the match is over. Design Notes record the departure.
- [x] F8 `lib/GameCore/GameEvent.h` -- `serial`'s comment: a touch carries GameVM's frame tag in transit, zeroed before `input`.
- [x] F9 `GameModeActivity.cpp` -- `rosterFor` logs nothing; `startNew` logs its failure, `startResume` only when no row starts; tests follow.
- [x] F10 `GameMatchTest.cpp` -- a Leave whose `goToGames()` refuses (the manager double's `goToGames` replaces nothing, as the device's does when `makeUniqueNoThrow` fails; say so in the double's comment and the test): the match stays Leaving, the last push is the blank, and a later sleep pushes nothing.
- [x] F11 `GameMatchTest.cpp` -- `AForcedExitWithNoVm...` asserts `fakelock::selfDeadlocks == 0`.
- [x] F12 `ResumeMatchTest.cpp` -- after the blank's tap, exactly one `draw for seat 2` (and none for any seat before it).
- [x] F13 `ResumeMatchTest.cpp` -- the Unstartable error-view case asserts the whole `STR_GAMES_RESUME_NOT_HERE` text is drawn, untruncated.

**Last round (pass 4: the review of `723f08fb`, rows G1-G6 of `cross-story-review.md` "Review of the follow-up fix commit"; one more commit, implemented directly):**
- [x] G1 `_bmad-output/implementation-artifacts/deferred-work.md` -- a `## e5-xr` entry that blocks epic-play-nearby, naming the four partly-local-roster paths and the F5 test's second `showTurnSeat()` call.
- [x] G2 `GameMatchActivity.{h,cpp}`, `docs/crosshatch/game-canvas.md` -- latch `frameDisplayed` at the touch-down the loop sees (`touchDownFrame`), reset when the contact's gesture is read and on every transition; restore the caveat naming both windows. Test: `PassMatchTest.AContactBegunUnderSeatOnesFrameAndLiftedUnderSeatTwosIsDropped`.
- [x] G3 `GameSaveStore.cpp` -- a newer codec version of another package's save is `other package` (None, INF line); test row changed.
- [x] G4 `GameSaveStore.cpp` -- `too large` is checked after the mode and seat count; test row for an oversized file with a bad seat count (None).
- [x] G5 `GameModeActivity.cpp` -- `startNew`'s failure line names the row's mode (`MODE_TEXTS[kind].log`).
- [x] G6 `GameVmTest.cpp`, `GameVM.h` -- a tag 2^31 ahead of the seat's frame is dropped (pins the wrap-safe compare); a comment that the counter cannot reach `UNTAGGED` in practice.

**H round (pass 6: the review of `df63158f`, rows H1-H8 of `cross-story-review.md` "Review of the third fix commit"; one more commit, implemented directly):**
- [x] H1, H4 `GameMatchActivity.{h,cpp}` -- the latch is freed on any Playing pass with no finger down (`isScreenTouchHeld`), which covers a lift with or without a gesture, a long press's suppression, and a contact that ended where the loop did not read it (Result, the light panel); `handle()`'s reset is removed. Test: `HiddenPassTest.ALatchFromAContactThatEndedOutsidePlayingIsFreedForTheNextSeat`.
- [x] H2 `GameSaveStore.cpp` -- the newer-codec return comes after the mode and seat checks, as `too large` does; test row for a newer-codec save with a bad seat count.
- [x] H3, H5 `screen_stubs/MappedInputManager.h` -- the held contact follows the device's touch path on the harness clock (touch-down level from 90 ms, held flag, a 500 ms long press that suppresses the rest, a lift frame with no touch-down); `quickTap` (no touch-down) and `liftWithoutTap`; pinned by `InputDoubleTest.AHeldContactFollowsTheDevicesTouchPath`. Tests: `PassMatchTest.AQuickTapWithNoTouchDownCarriesTheFrameAtItsLift`, `...ThatBecomesALongPressUnderSeatTwosIsDropped`.
- [x] H6 `docs/crosshatch/game-canvas.md`, comments -- reworded for what the code does.
- [x] H7 `GameMatchActivity.{h,cpp}` -- render records the frame before each canvas push and when the push returned (`LastPush`, `pushMutex`); a tap is back-dated by `getHeldTime()` (`frameAt`) and posted with the older frame; the latch starts at the first pass with the finger down. The input double answers a tap's held time. Tests: `PassMatchTest.ATapDownBeforeTheNextSeatsPushAndSeenOnlyAtItsLiftIsDropped`, `...ALatchWhoseContactEndedUnseenIsFreedForTheNextTap`.
- [x] H8 `GameMatchTest.cpp` -- the renderer double's comment is back above its test.

**J round (pass 8: the review of `4fbb9a87`, rows J1-J8 of `cross-story-review.md` "Review of the fourth fix commit"; one more commit, implemented directly):**
- [x] J1 `screen_stubs/MappedInputManager.h`, `GameMatchTest.cpp`, comments, `game-canvas.md` -- the double stamps a contact's touch-down at its first read (the device samples touch only in `update()` on the loop task); the H7 test becomes `PassMatchTest.ATapWhoseFingerCameDownWhileTheLoopWasBlockedReachesTheSeatPushedMeanwhile`, showing the window open; "What remains" lists it, for taps too.
- [x] J2 `GameMatchActivity.cpp`, `screen_stubs/HalGPIO.h` -- back-dating reads the touch-only `HalGPIO::lastTouchHeldMs()` (already public; no upstream edit); the double models `getHeldTime()`'s button precedence. Test: `PassMatchTest.ATapReadWithAButtonEdgeIsBackDatedByItsOwnHeldTime`.
- [x] J3 -- not reachable without an upstream edit (the release sample's time is not exposed); noted beside J1 in the comment and game-canvas.md.
- [x] J5 `deferred-work.md` -- the copied thresholds, under `## e5-xr`.
- [x] J6 `GameMatchTest.cpp` -- `PassMatchTest.ATapSampledDuringTheNextSeatsPushIsBackDatedToTheFrameBeforeIt` makes the push take 300 ms (`onDisplay`) and samples the finger during it.
- [x] J7 `GameMatchTest.cpp` -- `HiddenPassTest.AFingerHeldFromSeatOnesTurnIntoSeatTwosKeepsItsLatchAndIsDropped`.
- [x] J8 `screen_stubs/MappedInputManager.h` -- a long press is reported on every read of its frame; `InputDoubleTest` follows.

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
- **F1.** `readResume` reads the fixed part of any file; `newer file version` (CHRS, version above 1, no hash check), `newer codec version` (this package) and `too large` (header ok, this package) join `unstartableHere`. `ALaterFirmwaresSaveIsUnstartableAndKept` covers each kind and the older, garbage, and other-package versions that stay None; the three newer rows left `AResumeThatDoesNotFit...`, which gained the older versions.
- **F2.** `drawFront(…, &taken)` reads `frameGen()` inside `takeFront`; `renderCanvas` stores it. To give the race a seam, `renderCanvas` reads its `frame` before the view-clear, so `clearScreen` falls between that read and `drawFront`; the screen renderer double's new `onClear` hook (the VM publishing on the other core mid-render; pinned by `ScreenRendererDoubleTest.ClearScreenRunsTheHookAfterTheScreenIsCleared`) places the publish there in `PassMatchTest.AFramePublishedWhileRenderDrawsIsTheOneTheNextTapIsMadeUnder`. `GameVM::UNTAGGED` is checked first, the compare is wrap-safe, and the loop posts `UNTAGGED - 1` for a real frame of that number. No test wraps the count (2^32 frames).
- **F3.** `confirmRow` (`std::atomic<int8_t>`) and `removeIndex` (`std::atomic<int>`); openers store `focus` 0, then the row with release; `buildScreen` loads it once with acquire and hands it to `buildConfirmDialog` / `buildRemoveDialog`. No test: a race the host harness cannot stage (it renders on the test thread).
- **F4.** `SoloRounds::drawShown()` (private) draws nothing for NO_SEAT; `step` reads no input for it. `PassRoundsTest.ATurnSeatThisDeviceDoesNotPlayGetsNoInputAndNoDraw`.
- **F5.** `showSeatNow` returns before `handOffView` and `seatServed` when the turn seat is not local, with an INF line. `GameVmTest.AHiddenVmLeavesTheHandOffUnservedWhenTheTurnSeatIsNotThisDevices` builds a pass(3) roster with seats 1-2 local.
- **F6.** `peekStartable` takes a scratch span (empty: allocate, the static peeks); `peekResume` (no longer const) passes `buffer`. The test arms the nothrow-new failure and checks it was never consumed, the buffer holds the snapshot, and the roster is kept.
- **F7.** `GameVmTest.OpenPassATimerQueuedBehindTheWinningMoveReachesSeatZero` uses an inline game, not `pass-open`: pass-open's `over` handler cancels its timer, so a timer queued behind the winning move would be stale and dropped before any seat; the fixture is outside touches.
- **F8.** `GameEvent::serial`'s comment names the touch tag in transit.
- **F9.** `rosterFor` logs nothing; `startNew` logs its own failure (Continue, or pass with no seats); `startResume` logs only "no New row". Two `TitleScreenTest` assertions flipped to `EXPECT_FALSE("Cannot start counter")`.
- **F10.** `HiddenPassTest.ALeaveWhoseGamesScreenCannotOpenLeavesTheBlankAndASleepPushesNothing`; the manager double's `goToGames` comment names the device's `makeUniqueNoThrow` failure it stands in for (the double already replaced nothing).
- **F11.** `AForcedExitWithNoVm...` asserts `selfDeadlocks == 0` right after the exit.
- **F12.** The resumed hidden match asserts exactly one `draw for seat 2` and one `draw for seat ` in all after the blank's tap.
- **F13.** The Unstartable error-view case renders the view and finds the whole `STR_GAMES_RESUME_NOT_HERE` text in the drawn lines (joined across wraps).
- **Pass-2 mutation check.** With F2 (`frameDisplayed` back to `frame`), F4, F5, and F6 (allocate in `peekResume`) reverted in place, their four tests failed; restored afterwards. F7, F10, F12, F13 pin behaviour the fix commit already had.
- **Pass-2 review patch.** The F2 test's clear hook disarms with a local flag (no self-reset inside the running closure), and both the test and `renderCanvas` say the `frameGen()` read before the view clear is the test's seam; `ALaterFirmwaresSaveIsUnstartableAndKept` gained oversized other-package, bad-magic, and older-version rows (moving the `too large` check above the package check now fails it); the unused `big` left `AResumeThatDoesNotFit...`; stale comments fixed (`GameSaveStore.h` peek buffer, `seedResume`'s Unstartable kinds, `SoloRounds.h` class comment, the `confirmRow`/`removeIndex` orders), formats.md's blob-header paragraph names resume.bin's newer-version exception and the header/hash offsets it relies on; `ScreenRendererDoubleTest` moved to the doubles section and the race test got its own comment.
- **Follow-up: `pio check -e x4pro` failure fixed.** `unstartableHere`'s raw loop drew cppcheck's low `useStlAlgorithm` (`GameSaveStore.cpp:82`); it is `std::any_of` over the same list now, and both `pio check` runs pass.
- **Last round (G2-G6, then pass 5's patches):** implemented directly; mutation checks: the wrap compare reverted to `>=`, the latch removed, the latch re-taken on every pass, and the other-package codec branch reverted each fail their test (the store, VM, and match tests above).
- **H round:** implemented directly; mutation checks: freeing the latch on a gesture only (the old rule; both the light-panel and the hidden test fail), the fallback taking the stale latch, removing the back-dating, and the newer-codec early return each fail their test.
- **J round:** implemented directly (pass 9's patches too); mutation checks: restoring `handle()`'s reset, back-dating with `getHeldTime()`, and stamping the push before `displayBuffer` each fail their test.
- **K round (docs and comments only):** game-canvas.md states back-dating's coverage as the same-pass race and names the reachable swipe/long-press gap; the J6 test's comment follows; K1's latch back-dating and K2's three double gaps deferred under `## e5-xr`; the final Assumption lines are in Design Notes.

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

**Pass 2 (2026-10-01, orchestrator-relayed cross-story findings on `ef46f8a5`; raw `xreview/fix-*.md`, checked against the code).** F1-F13 are verdict `fix` in `cross-story-review.md`; F14 (packet) and F15 (rejected) are not this change's. Verdicts: high 0, medium 3, low 10, false 0. All route to the follow-up commit (orchestrator's call; no loopback).

| # | Lens | Finding | Verdict | Route | Evidence / action |
|---|---|---|---|---|---|
| F1 | A2, EC3 | A later firmware's save (newer file or codec version, larger snapshot) reads as None and New replaces it unasked | medium | patch | Real: `readResume` returns the header status, and "too large" before reading, ahead of the hash and mode. Classified as Unstartable (Follow-up F1). Supersedes pass-1 deferral row 10. |
| F2 | A1, EC4 | The touch tag can name an older frame than the one pushed; no wrap-safe compare; sentinel inside the range | medium | patch | Real: `renderCanvas` reads `frameGen()` before `drawFront`, and `madeUnderAnotherSeat` compares with `>=`. `drawFront` reports the generation it drew under the frame mutex; wrap-safe compare; sentinel kept out. |
| F3 | A3 | `confirmRow` / `removeIndex` plain across tasks; row stored before `focus = 0` | medium | patch | Real: `openConfirm` sets `confirmRow` then `confirm.focus`, so a render between them draws the old focus. Atomics, focus first, row with release. Supersedes pass-1 deferral row 9. |
| F4 | A4, EC1 | `SoloRounds` passes `NO_SEAT` to `play`/`draw` | low | patch | Real for a partly-local roster (epic-play-nearby); pass-1 row 11 rejected it as unreachable, the orchestrator asks for the guard. |
| F5 | EC2 | `showSeatNow` opens the gate for a non-local turn seat | low | patch | Real, unreachable today: `drawShown` returns Ok without drawing and `seatServed` is stored. Stay in HandOff. |
| F6 | A5 | `peekResume` allocates a second snapshot buffer while the match runs | low | patch | Real: `peekStartable` allocates `SNAPSHOT_LIMIT` with nothrow new; under low heap the generic text shows. Reuses the store's buffer. |
| F7 | A7 | The timer path to seat 0 and the reversal of entry 1's plan row 3 are unrecorded and untested | low | patch | Real; Design Notes record it, a VM test pins the timer. |
| F8 | A8 | `GameEvent.h` says `serial` is Timer only | low | patch | Real; comment updated (now in touches). Supersedes pass-1 deferral row 8. |
| F9 | A9 | `rosterFor` logs an error for a row the search skips | low | patch | Real: `startResume`'s loop calls `rosterFor`, which logs `LOG_ERR`. Logging moves to the callers. |
| F10 | A10 | No test drives `goToGames()` refusing | low | patch | Real; test added with the double's behaviour named. |
| F11 | VG2 | The no-VM onExit push test does not assert no self-deadlock | low | patch | Real; assertion added. |
| F12 | VG1 | The resumed hidden match's zero-draw check has no barrier | low | patch | Real; exact count after the tap. |
| F13 | A12 | No test shows the new reason drawn untruncated | low | patch | Real; asserted in the error-view case. |

**Pass 3 (2026-10-01, this build's own review of the follow-up diff `00ca4070..` working tree).** All four lenses ran as context-free subagents, launched together, and all four returned: blind-hunter (BH), edge-case-hunter (EC), verification-gap (VG), intent-alignment (IA, descriptive). Verdicts: high 0, medium 0, low 16, false 0. Routes: patch 8 entries, defer 1, reject the rest; no loopback.

| # | Lens | Finding | Verdict | Route | Evidence / action |
|---|---|---|---|---|---|
| P1 | VG1, BH8 | No row pins an oversized save of another package, or an oversized garbage file, as None | low | patch | Pre-verified: the only oversized fixtures carry `PKG` and a valid header; reordering the `tooLarge` return passes. Rows added. |
| P2 | VG other | The F2 test clears `onClear` from inside the running `std::function` (UB) | low | patch | Real: assigning `nullptr` destroys the running closure. A flag inside, reset after `render()`. |
| P3 | EC7, BH2, IA | The F2 race test's seam is the read-before-clear reorder; moved back, it would pass with the bug | low | patch | Real: on `ef46f8a5` the read came after the clear, so the hook's publish preceded it. Commented as load-bearing; the steady path is right by construction (generation read under the frame mutex). |
| P4 | BH6, BH7, VG other, BH5 | Stale text: `GameSaveStore.h` peek comment, formats.md blob-header paragraph, seedResume's Unstartable comment, SoloRounds class comment; the newer-version rule's layout assumption unwritten | low | patch | Real; direct corrections. |
| P5 | BH9, VG other, EC6 | Unused `big` in `AResumeThatDoesNotFit...` | low | patch | Real; deleted. |
| P6 | BH12 | New tests under an unrelated comment; the double's test among PassMatchTest's | low | patch | Real; moved and commented. |
| P7 | BH13 | `confirmRow` / `removeIndex` orders undocumented | low | patch | Real; one comment line each. |
| P8 | BH1, EC1 | `renderedFrame` keeps the frame number read before `drawFront`, so an overlay repaint after a mid-render publish may skip `forceFull` | low | defer | Pre-existing since entry 4's `renderCanvas`; not caused by the follow-up. |
| P9 | BH3, VG2 | The wrap-safe compare and `UNTAGGED - 1` are untested; `0` is still a sentinel elsewhere | low | reject | Needs 2^32 frames (years at e-ink rates); a seam on `FrameBuffers::generation` is outside touches. |
| P10 | BH4 | `SoloRounds::step` drops a timer for a non-local turn seat | low | reject | No roster has a non-local turn seat in an open match today (F4's premise); the rows asked for no input there. |
| P11 | BH5, EC2, EC3 | A round or hand-off that reaches a non-local turn seat waits forever | low | reject | Unreachable until epic-play-nearby; fails closed (no seat drawn), as F4/F5 asked; class comment now says so (P4). |
| P12 | BH10, EC5 | `peekStartable`'s scratch size and `peekResume`'s call order are not enforced | low | reject | The one caller passes the store's buffer (`BUFFER_BYTES >= SNAPSHOT_LIMIT`, static_assert) after an empty load; a guard adds a branch for no caller. |
| P13 | BH11, EC4 | `startNew` names pass for any `rosterFor` failure | low | reject | Only pass can fail today; cosmetic log. |
| P14 | IA (F1) | A newer file version of another package is kept too | low | reject | By design: its layout, and so its hash, cannot be read; the file sits in this game's folder; keeping and asking is the safe side (F15's reasoning). |
| P15 | IA (F6) | F6 reuses the store's buffer, reversing row 10's "store-free" for the buffer | low | reject | Row 10's harm was the roster adoption, which stays out; F6 named this option first. |
| P16 | IA (F10, F11, F13) | F10 drives the double, not the device's OOM; F11 duplicates an existing check; F13 covers one screen | low | reject | The double's `goToGames` does what the device's does on OOM (named in its comment); the duplicate assert is harmless; the error view's wrap is the theme's, not the screen's, beyond the harness's one screen. |

**Pass 4 (2026-10-01, orchestrator-relayed review of `723f08fb`; raw `xreview/fix2-*.md`, checked against the code).** G1 defer, G2-G6 fix (the orchestrator's verdicts). Verdicts: high 0, medium 1 (G2), low 4, deferred 1 (G1, medium if reachable, unreachable today).

| # | Lens | Finding | Verdict | Route | Evidence / action |
|---|---|---|---|---|---|
| G1 | EC, A | Partly-local rosters stall on four paths | medium (unreachable) | defer | Real in code (`SoloRounds::step`/`drawShown`, `GameVM::showSeatNow`); no roster has it until epic-play-nearby. `deferred-work.md` `## e5-xr` entry blocks that epic. |
| G2 | A | The tag is read at release, not touch-down; the caveat was deleted | medium | patch | Real: `readGesture` sees the release; a contact spanning the next seat's push went to that seat. Latched at touch-down (Design Notes), caveat restored with both remaining windows. |
| G3 | EC, A | Another package's newer-codec save logs ERR "discarded" each title-screen open | low | patch | Real: it fell through to `unknown_codec_version`. Now `other package`. |
| G4 | EC | An oversized file with a bad seat count is kept as Unstartable | low | patch | Real: `tooLarge` came before the mode. Moved after the seat checks. |
| G5 | A | `startNew` names pass for any failure | low | patch | Real; names the row's mode. |
| G6 | VG, A | The wrap-safe compare is unpinned; the sentinel is remapped, not out of range | low | patch | Real: `>=` failed no test. Pinned with a tag 2^31 ahead; comment on `UNTAGGED`. |

**Pass 5 (2026-10-01, this build's own review of the last-round diff `e47e33d9..` working tree).** All four lenses ran as context-free subagents, launched together, and all four returned: blind-hunter (BH), edge-case-hunter (EC), verification-gap (VG), intent-alignment (IA, descriptive). Verdicts: high 0, medium 1, low 9, false 0. Routes: patch 6, reject the rest; no loopback.

| # | Lens | Finding | Verdict | Route | Evidence / action |
|---|---|---|---|---|---|
| Q1 | VG, EC | The device's `wasScreenTouchDown` is a level (true each update of a still hold past 90 ms), so the latch re-took the next seat's frame; the stub modelled an edge | medium | patch | Real: `MappedInputManager.cpp` `TOUCH_DOWN_SELECT_DELAY_MS` over `isTouchTapCandidate`. Latch only at the first pass; the input double's `holdTouch`/`liftTouch` report the level across `clear()` (pinned by `InputDoubleTest.AHeldContactReportsItsTouchDownOnEveryFrameUntilItLifts`); the G2 test holds through the push and two more passes and fails with the re-latch. |
| Q2 | BH, VG, EC | A lift with no gesture leaves the latch set | low | patch | Real: cleared only on a gesture. Cleared on any `touchReleased`. |
| Q3 | BH, EC, IA | The doc's windows undercount (a tap under 90 ms is tagged at its lift; the latch semantics) | low | patch | Real; game-canvas.md names three windows and the 90 ms rule. |
| Q4 | BH | G3 left another package's older-codec save at ERR "discarded" | low | patch | Real; any codec version of another package is `other package`; row added. |
| Q5 | BH | `too large` docs do not name `bad seat count` or the startable checks before it; constant comment stale | low | patch | Real; formats.md and the constant's comment. |
| Q6 | BH | The G6 drop is not tied to the cell-6 tag | low | patch | Real; asserts the drop names seat 1's first frame. |
| Q7 | BH | The wrap's accept side (a tag past a `seatFrame` near `UINT32_MAX`) is untested | low | reject | Needs `FrameBuffers::generation` near 2^32 (outside touches, unreachable in use). |
| Q8 | BH | The `handle()` reset and the no-touch-down fallback are untested | low | reject | VG found removing the reset unobservable on the device's input; the fallback is every short tap, which every existing tap test exercises. |
| Q9 | EC | This package's newer-codec save with a malformed mode is kept, unlike its oversized twin | low | reject | A codec-only bump keeps the layout (formats.md); keeping a file of this package is the safe side (F15's reasoning). |
| Q10 | BH, IA | G5's line omits the host's seat limit; "only pass today" is unenforced; G4 tested at the store only | low | reject | Cosmetic log detail; the Unstartable-to-UI mapping is pinned by the title-screen tests. |

**Pass 6 (2026-10-01, orchestrator-relayed review of `df63158f`; raw `xreview/fix3-*.md`, checked against the code and against `src/MappedInputManager.cpp` and freeink-sdk's `InputManager.cpp`).** H1-H8 are verdict `fix`. Verdicts: high 0, medium 3 (H1, H5, H7), low 5.

| # | Lens | Finding | Verdict | Route | Evidence / action |
|---|---|---|---|---|---|
| H1 | EC, A | A latched contact that ends unseen (the light panel's swipe) keeps its latch | medium | patch | Real: `ActivityManager::loop` takes the panel's swipe before the match's loop, and the old rule freed the latch only on a seen release or gesture. Freed on any pass with no finger down. |
| H2 | EC | A newer-codec save with a bad seat count is kept | low | patch | Real: the codec return preceded the switch. Moved after it. |
| H3 | VG, A | The no-latch fallback and a gesture-less lift are never driven: the double's `tap()` reports a touch-down on the lift frame | low | patch | Real: `InputManager.cpp` clears `touchPressed` on the release update. `quickTap`, `liftWithoutTap`, and a test (fails with the fallback mutated). |
| H4 | VG, A | `handle()`'s reset untested and nearly inert | low | patch | Real; removed (Design Notes); the H1 test pins the rule that replaces it. |
| H5 | VG, A | The double lacks the 500 ms long press, its suppression, and the held flag | medium | patch | Real: `InputManager.cpp` fires `touchLongPressEvent` at `TOUCH_LONG_PRESS_MS` and `MappedInputManager::wasScreenLongPress` suppresses the contact. Modelled and pinned; a G2 long-press variant. Not modelled: tap slop, multi-touch, the getHeldTime override (packet, row 16). |
| H6 | A | Docs and comments mis-state the latch | low | patch | Real; reworded (three windows, the 90 ms delay on every contact). |
| H7 | A | Back-dating via `getHeldTime()` would close the windows | medium | patch | Real (corrected after pass 7: `displayBuffer` blocks until the refresh completes). Taps back-dated against the last push's completion time; the latch starts at the first held pass (Design Notes). Test: `PassMatchTest.ATapDownBeforeTheNextSeatsPushAndSeenOnlyAtItsLiftIsDropped`. |
| H8 | A | The new test split a comment from its test | low | patch | Real; moved back. |

**Pass 7 (2026-10-01, this build's own review of the H-round diff `0c18e437..` working tree).** All four lenses ran as context-free subagents, launched together, and all four returned: blind-hunter (BH), edge-case-hunter (EC), verification-gap (VG), intent-alignment (IA, descriptive). Verdicts: high 0, medium 3, low 9, false 0. Routes: patch 8, defer 1, reject the rest; no loopback.

| # | Lens | Finding | Verdict | Route | Evidence / action |
|---|---|---|---|---|---|
| R1 | IA | H7's stated reason is false: `displayBuffer` blocks until the refresh completes on the SSD1677 boards | medium | patch | Real: `Ssd1677Driver.cpp` `if (!async) bus.waitRefreshComplete`. Back-dating implemented for taps (H7 row above). |
| R2 | EC | A contact that slides past the 28 px slop within 90 ms is never latched, so a long hold of it is tagged at its lift | medium | patch | Real: `isTouchTapCandidate` drops it, `wasTouchTap` still taps under 60 px. Latched from `isScreenTouchHeld`, which has no slop gate or delay. |
| R3 | BH, EC, IA | The H1 test passes on the pre-fix code (its contact ended across `handle()`); the light-panel case is untested | medium | patch | Real. `PassMatchTest.ALatchWhoseContactEndedUnseenIsFreedForTheNextTap` drops the contact with no frame reading it; it fails with the gesture-only rule. |
| R4 | BH, VG, EC | The double's long press stays pending until read; the device's lasts one update | low | patch | Real: `touchLongPressEvent` is cleared each `update()`. `clear()` now loses an unread one; pinned. |
| R5 | BH | The long-press test's after-lift checks are unsynchronized | low | patch | Real; ends with seat 2's own tap played. |
| R6 | BH | Newer-codec precedence rows missing (unknown mode, seats not startable, newer codec plus oversized); formats.md's "Both" unclear | low | patch | Real; three rows and the wording. |
| R7 | BH, EC | `liftTouch` with no contact scripts a phantom tap; positional `Contact` init; `InputDoubleTest` lacks the 89 ms edge, `clear()` keeping the contact, `liftWithoutTap` | low | patch | Real; guarded, named fields, pinned. |
| R8 | VG | `liftWithoutTap`'s comment cites the wrong slop | low | patch | Real: losing the tap is the 59 px release slop. Reworded. |
| R9 | VG | The double's thresholds are copies the host cannot check against the SDK (private constants) | low | defer | Real; the device-run packet (row 16) holds them. Recorded in deferred-work.md. |
| R10 | BH, EC | A finger already down at the first Playing pass after a contact that ended outside Playing inherits the old latch | low | reject | Fails closed: that finger was put down before the new seat's frame was shown (the first Playing pass after the hand-off runs before the frame is pushed); documented in game-canvas.md. |
| R11 | BH, VG | The quick-tap and long-press tests pin the double more than a production regression | low | reject | They pin the fallback and the long-press path against the device-modelled double; the production rules each have a failing mutation (Implementation Notes). |
| R12 | IA | The `Assumption for entry 11:` line for H7 is not written | low | reject | H7 is now done; no assumption is needed. |

**Pass 8 (2026-10-01, orchestrator-relayed review of `4fbb9a87`; raw `xreview/fix4-*.md`, checked against `src/main.cpp`'s loop, `src/MappedInputManager.cpp`, and freeink-sdk's `InputManager.cpp`).** J1, J2, J6, J7, J8 fix; J3 fix if small; J4 packet; J5 defer. Verdicts: high 0, medium 2 (J1, J2), low 5, deferred 1.

| # | Lens | Finding | Verdict | Route | Evidence / action |
|---|---|---|---|---|---|
| J1 | EC, A | A finger landing during the SD write is first sampled after it; the H7 test passed only because the double stamped at `holdTouch` | medium | patch (+ entry 11) | Real: `InputManager` stamps `touchDownPoint.timestamp` at its first poll in `update()`. The double stamps at first read; the test now shows the window open; docs and comment corrected; proposed `Assumption for entry 11:` line in Design Notes. |
| J2 | EC, A, VG | `getHeldTime()` answers a button's hold on a pass with a button edge, 0 with a mapped home action | medium | patch | Real (`MappedInputManager.cpp:390-398`). Reads `HalGPIO::lastTouchHeldMs()`, public, so no upstream edit. Double models the precedence; test fails with `getHeldTime()`. |
| J3 | EC | The anchor is `millis()` at read, not the release sample | low | patch (note) | Real, a few ms; the release sample's time is not exposed without an upstream edit. Noted beside J1. |
| J4 | A | The drop window runs to `displayBuffer`'s return, after post-waveform work | -- | packet | The orchestrator's; not this change's. |
| J5 | A | The double's thresholds are copied numbers | low | defer | Recorded under `## e5-xr`. |
| J6 | VG | The after-the-refresh stamp is unpinned | low | patch | Real; a 300 ms push in the test; it fails with the stamp taken before `displayBuffer`. |
| J7 | VG | `handle()`'s reset removal is unpinned | low | patch | Real; the hidden test fails with the reset restored. |
| J8 | VG | The double reports a long press on the first read only | low | patch | Real (`InputManager.cpp` `touchLongPressEvent` holds for the update); every read of its frame now. |

**Pass 9 (2026-10-01, this build's own review of the J-round diff `2ae3587f..` working tree).** All four lenses ran as context-free subagents, launched together, and all four returned: blind-hunter (BH), edge-case-hunter (EC), verification-gap (VG, no gaps), intent-alignment (IA, descriptive). Verdicts: high 0, medium 2, low 8, false 0. Routes: patch 7, reject 3; no loopback.

| # | Lens | Finding | Verdict | Route | Evidence / action |
|---|---|---|---|---|---|
| S1 | BH, VG, EC | `gpio.touchHeldMs` goes stale: `tap()` never sets it, `SetUp` never resets it, a suppressed or gesture-less lift skips it | medium | patch | Real (process-global `gpio`). `tap()` sets it, `ScreenTest::SetUp` resets it, every sampled release sets it, as the device's release update does. |
| S2 | IA, VG, EC | The double's "first sample is the lift" path does not exist on the device; it samples only on reads, not every pass | medium | patch | Real (`main.cpp` runs `update()` each pass; a release needs an earlier sample). `update()` samples and `frame()` calls it; an unsampled contact's lift is never seen; the J1 test samples after the block; `InputDoubleTest` pins both. |
| S3 | EC | The J6 test clears `onDisplay` inside its own call (UB) | low | patch | Real; a flag inside, reset after `render()`. |
| S4 | BH | The two-push clause in "What remains" contradicts J1's model | low | patch | Real; removed; the list is now bullets, one window each. |
| S5 | BH, EC | Back-dating's device path is unnamed; the doc's guarantee overstates swipes and long presses | low | patch | Real: it serves a tap first sampled on a pass that returned before `readGesture`; named in the doc and the J6 test; the swipe/long-press case listed. |
| S6 | BH | The window names one SD write; the pass has three | low | patch | Real (`flushIfDue`, `flushResume`, `retryResumeDelete`); all named. |
| S7 | BH | "entry 11" unqualified; the loop comment duplicates the doc | low | patch | Real; qualified as the device run that measures it; the comment trimmed to the mechanism with a pointer. |
| S8 | BH, EC | The double's `pressed` is a level, the device's an edge | low | reject | Production no longer calls `getHeldTime()`; noted in the double's comment as more permissive. |
| S9 | BH | No test for a swipe or long press in the SD-step window; the home-action case | low | reject | Documented as open; production reads `lastTouchHeldMs`, so the home action has no consumer. |
| S10 | IA | J6's path is the latch's second line on the device | low | reject | By design: the test names the early-return path it stands for. |

## Design Notes

- **Row 4 vs R7.** The row's action (drop every seat-0 input) would end R7's "from then on ... input get seat 0" and game-api-seed §3, which `PassRoundsTest` pins. The trigger is an event posted while a seat 1..n frame showed and played after the round ended; row 5's tag drops exactly that (the seat drawn changed since it was posted), so one mechanism fixes both and R7 stays: the Over menu posts no game input, and any input posted under seat 0's frame would still reach seat 0.
- **Tag in `serial`.** `GameEvent` and `InputQueue` are outside touches; `serial` is read only for `Timer` (`GameTimer::accepts`), and GameVM zeroes it before the event leaves. Default `UINT32_MAX` keeps direct callers (tests) unchanged.
- **Row 10 option:** a store-free check (`peekResume`), consistent with the title screen by construction; it replaces `savedForAnotherHost`, whose guards were (a) only the host is widened, so a save the game cannot start anywhere is not kept, now widened on purpose to row 2's set (a pre-epic solo save of a pass-only game is kept), and (b) a read that faults refuses: kept (`Unreadable` refuses).
- **Row 3 option:** first New row's mode, not the error view: no new screen path, and the guard "a pass-only game with no pass seats starts nothing" stays in `rosterFor`.
- **Row 9 option:** delete the overload; `canvasUnderView` keeps the Paused-from-HandOff rule, pinned by the lag test.
- **Row 6/1 flag:** one render-side record of what the last push left on the panel, read under RenderLock by leave() and onExit; Over counts as Other, since seat 0's frame is everyone's.
- **F7: the seat-0 departure, recorded.** Entry 1's plan (row 3 of its I/O matrix, pinned by `PassRoundsTest.ATapAfterThePassRoundEndsReachesSeatZeroAndItsMoveIsDiscarded`) had a tap queued after the round reach seat 0, its move discarded. Through `GameVM` that is reversed for touches: a touch made under a seat 1..n frame and played after the round ended is dropped (row 4's trigger), while the rounds layer still delivers seat 0 input (the test stays). Timers keep reaching seat 0's `input` after the round (R7's "input get seat 0", R11 has no turn seat to name); `GameVmTest` pins that a timer queued behind the winning move is delivered without error.
- **G2: latch at touch-down, chosen because it stayed small** (three loop-task members: the first pass that sees `touchPressed` latches, since the device's `wasScreenTouchDown` is a level reported on every pass of a hold past 90 ms; freed on any release, gesture or not, and in `handle()`). It closes the contact that begins under one seat's frame and lifts under the next (now dropped, was delivered). It cannot close the touch-down made while the loop is blocked, which is seen at the next pass, nor a touch during the refresh; both stay documented in game-canvas.md. Latching at the touch itself needs the input layer's timestamps (upstream `MappedInputManager`).
- **H7: back-dating done for taps (it stayed small).** `displayBuffer` blocks until the panel's refresh completes (`Ssd1677Driver::display`, `waitRefreshComplete` unless async), so render notes the frame before each canvas push and `millis()` when the push returned (`LastPush` under `pushMutex`, ~15 lines), and a tap, whose held time `getHeldTime()` gives on its frame, is back-dated to `now - held` and posted with the older of that frame and its latch. The latch itself now starts at the first pass with the finger down (`isScreenTouchHeld`, no 90 ms delay, no slop gate), so a slid contact and a short tap are latched too. Left: a swipe or long press first seen after a loop pass that ran past the push (no held time; a long press needs a 500 ms hold, which the loop sees unless blocked that long), and a tap the loop never saw down that spanned two pushes (one push of history). The earlier note here, that `displayBuffer` returns before the refresh, was wrong.
- **H4: `handle()`'s latch reset removed.** The touch-down is a level, so a finger still held would be re-latched on the first Playing pass anyway; the no-finger-down rule frees a latch whose contact ended outside Playing, and a finger held through a state change keeps the earlier latch (dropped if the seat changed: fails closed; it was put down before the new seat's frame was shown).
- **J1: the SD-write window stays open.** Touch is sampled only by `mappedInputManager.update()` on the loop task, so a finger that lands while the loop is blocked in `flushResume`'s SD write is first sampled after the block; if the next seat's push completed meanwhile, neither the latch nor back-dating reaches before it, and the tap, swipe, or long press may reach that seat. Closing it needs touch sampled off the loop task (upstream input code). Proposed line: "Assumption for entry 11 (e5-xr's plan, 2026-10-01): a touch made while the loop task is blocked in an SD step after a move (the resume write, the ch.store flush, or the delete retry), during which the next seat's frame finished pushing, can reach the next seat in an open pass match; the device run times those steps after a move and checks how often a quick tap in that window lands on the next seat. Closing it needs touch sampled off the loop task (upstream `MappedInputManager`/`InputManager`)." J3's few milliseconds (the anchor is the loop's read, not the release sample) sit beside it.
- **Final proposed `Assumption for entry 11:` lines (K round, 2026-10-01): every touch window still open.** These replace the earlier proposed J1 line.
  - Assumption for entry 11 (e5-xr's plan, 2026-10-01): in an open pass match, a touch made while the device is busy writing to the SD card after a move (saving the match, saving the game's store, or retrying a delete), during which the next player's screen finished drawing, can count as the next player's move, whether a tap, a swipe, or a long press. The device run times those writes after a move and checks how often a quick tap made then lands on the next player. Closing it needs touch read off the loop task, in upstream input code.
  - Assumption for entry 11 (e5-xr's plan, 2026-10-01): in an open pass match, a swipe or long press whose finger landed just before the next player's screen finished drawing, and that the match first registers just after, can count as the next player's move; a tap in the same moment is caught by its held time. The device run checks a long press started just as the turn passes. Closing it means back-dating the latch itself (deferred-work.md `## e5-xr`).
  - Assumption for entry 11 (e5-xr's plan, 2026-10-01): a touch made on the next player's screen after the panel shows it but before the screen push returns (the work after the refresh waveform) is dropped without feedback; the device run measures that gap and checks that the next player's first tap registers (cross-story review J4).
  - Assumption for entry 11 (e5-xr's plan, 2026-10-01): a finger already resting on the screen when a new turn begins is ignored for that whole contact; the next player's first move must be a fresh touch. The device run checks it once in a hidden match.
  - Assumption for entry 11 (e5-xr's plan, 2026-10-01): the host tests' input double copies the device's 90 ms touch-down and 500 ms long-press timings and its sampling once per loop pass; the device run checks both timings on an X4 Pro (deferred-work.md `## e5-xr`).
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

**Results of the follow-up (2026-10-01, pass 2 and 3 fixes; the tree measured is the follow-up commit's, whose firmware sources the commit does not change):**
- Host suites: 1,521 of 1,521 pass. `scripts/*_test.py`, `check_layers.py` (496 edges), `check_upstream_touches.py` PASS, `./bin/clang-format-fix` clean twice.
- `pio run -e default`: success. `pio check` (`default`) and `pio check -e x4pro`: no defects, PASSED (the first x4pro run failed on one low `useStlAlgorithm`, fixed as noted above, then both re-run).
- `sim.sh build x4pro`: SUCCESS.
- Flash (measured the same way, four steps under the build lock): x4pro `firmware.bin` 5,921,216 B games on, 5,679,792 B off, +241,424 B (14,576 B under the gate); static internal RAM +784 B (240 B under); `objects` clean. Over the base at `c1902721`: +7,984 B flash and +0 B static RAM, within the 11,152 B and 32 B share; +576 B over the fix commit `ef46f8a5`'s +240,848 B, leaving 3,168 B and 32 B of the share.
- Test doubles extended: `screen_stubs/GfxRenderer.h` `onClear` (the VM publishing on the other core mid-render), pinned by `ScreenRendererDoubleTest.ClearScreenRunsTheHookAfterTheScreenIsCleared`; `screen_stubs/ActivityManager.h` comment only (its `goToGames` replaces nothing, as the device's does when `makeUniqueNoThrow` fails).

**Results of the last round (2026-10-01, G2-G6 and pass 5's patches; the tree measured is this commit's, whose firmware sources the commit does not change):**
- Host suites: 1,523 of 1,523 pass; the three timing-sensitive new or changed tests passed 30 repeats each.
- `scripts/*_test.py`, `check_layers.py` (496 edges), `check_upstream_touches.py` PASS, `./bin/clang-format-fix` clean.
- `pio run -e default`: success. `pio check` (`default`) and `pio check -e x4pro`: no defects, PASSED. `sim.sh build x4pro`: SUCCESS.
- Flash (four steps under the build lock): x4pro `firmware.bin` 5,921,312 B games on, 5,679,792 B off, +241,520 B (14,480 B under the gate); static internal RAM +784 B (240 B under); `objects` clean. Over the base at `c1902721`: +8,080 B flash and +0 B static RAM; +96 B over `723f08fb`'s +241,424 B; 3,072 B and 32 B of the share left.
- Test double extended: the screen input double's `holdTouch`/`liftTouch` (`screen_stubs/MappedInputManager.h`) model the device's touch-down level, pinned by `InputDoubleTest.AHeldContactReportsItsTouchDownOnEveryFrameUntilItLifts`.

**Results of the H round (2026-10-01, H1-H8 and pass 7's patches; the tree measured is this commit's, whose firmware sources the commit does not change):**
- Host suites: 1,528 of 1,528 pass; the seven touch-latch tests passed 20 repeats each.
- `scripts/*_test.py`, `check_layers.py` (497 edges; `<mutex>` in GameMatchActivity.h is the standard library), `check_upstream_touches.py` PASS, `./bin/clang-format-fix` clean twice.
- `pio run -e default`: success. `pio check` (`default`) and `pio check -e x4pro`: no defects, PASSED. `sim.sh build x4pro`: SUCCESS.
- Flash (four steps under the build lock): x4pro `firmware.bin` 5,921,504 B games on, 5,679,792 B off, +241,712 B (14,288 B under the gate); static internal RAM +784 B (240 B under); `objects` clean. Over the base at `c1902721`: +8,272 B flash and +0 B static RAM; +192 B over `df63158f`'s +241,520 B; 2,880 B and 32 B of the share left.
- Test double extended: the screen input double models the device's touch path (90 ms touch-down level, held flag, one-update 500 ms long press with suppression, a lift frame without touch-down, a tap's held time), pinned by `InputDoubleTest.AHeldContactFollowsTheDevicesTouchPath`; not modelled: tap slop, multi-touch, the held-time override's 250 ms life (deferred-work.md, the packet).

**Results of the J round (2026-10-01, J1-J8 and pass 9's patches; the tree measured is this commit's, whose firmware sources the commit does not change):**
- Host suites: 1,531 of 1,531 pass; the touch-latch tests passed 20 repeats each.
- `scripts/*_test.py`, `check_layers.py` (497 edges), `check_upstream_touches.py` PASS, `./bin/clang-format-fix` clean twice.
- `pio run -e default`: success. `pio check` (`default`) and `pio check -e x4pro`: no defects, PASSED. `sim.sh build x4pro`: SUCCESS.
- Flash (four steps under the build lock): x4pro `firmware.bin` 5,921,504 B games on, 5,679,792 B off, +241,712 B (14,288 B under the gate); static internal RAM +784 B (240 B under); `objects` clean. Over the base at `c1902721`: +8,272 B flash and +0 B static RAM; +0 B over `4fbb9a87`'s +241,712 B; 2,880 B and 32 B of the share left.
- Test doubles extended: the screen input double samples a held contact on `update()` (which the match tests' `frame()` calls) or its first read, never sees a contact no update sampled, and sets the touch-only held time on every sampled release; the HalGPIO double gains `lastTouchHeldMs()`; pinned by `InputDoubleTest.AHeldContactFollowsTheDevicesTouchPath`.

