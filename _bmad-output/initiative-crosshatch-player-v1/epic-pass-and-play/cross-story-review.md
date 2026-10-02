# Cross-story review: epic-pass-and-play

Run by the orchestrator on 2026-10-01, after entry 10 merged (`ac8c1a87`; reviewed tree `7aa08374`), as `orchestrated-epics.md` "Before the epic PR" asks.

- **Diff:** `git diff c1902721..7aa08374`, excluding `_bmad-output/**`, `*.png`, `*.generated.h` and the generated I18n files. The production part is 221 KB and the tests 397 KB.
- **Reviewers:** three context-free subagents, `bmad-review`'s adversarial (A), edge-case-hunter (EC) and verification-gap (VG) lenses. Each was told to weight the boundaries between the nine story merges.
- **Raw reports:** in the orchestrator's scratchpad (`xreview/`). The rows below carry their substance.
- **Verdicts:** fix (a build agent fixes it in this PR), owner (goes to the owner at entry 11 as an `Assumption for entry 11:` line, since it changes a requirement's literal text or a spine decision), packet (goes into the device-run packet), reject (with the reason).

## Triage

| # | Lens | Story or boundary | Finding | Verdict | Reason / action |
|---|---|---|---|---|---|
| 1 | A1 | 5.4 × 5.6 | Leave from a pause menu over a seat's frame, with `goToGames()` out of memory, leaves the match current in Leaving with no VM and the seat's frame on the panel. A later sleep's `stopVm()` returns before `pushForcedExitBlank`, so no blank is pushed (R6). | fix | Hidden-information leak. Push the blank in a hidden pass match whenever the panel may hold a seat frame, whether or not a VM exists: before `goToGames()` in `leave()`, and in `onExit()` outside the VM-only path. Never take `RenderLock` again where it is already held. |
| 2 | EC3, A2, A7 | 5.5 × 5.7 × 5.9 | A `resume.bin` of this package that this host cannot start (pass `n` out of range, a solo save of a pass-only game, an unknown mode byte) reads as `None` on the title screen. It gets no Continue and no "This replaces the saved game" question, and the new match's first write replaces it. The error text of `savedForAnotherHost` meanwhile tells the person to "try Continue again". | fix | Data on a device at risk; contradicts R8's "kept". `peek` reports a distinct state for a file of this package that exists but cannot be resumed here. The title screen asks before New over it (Continue stays hidden). That error view gets its own `tr()` reason (an appended `STR_GAMES_*` key, ledger row 2). |
| 3 | A3 | 5.7 × 5.9 × 5.10 | For a game that starts solo and pass, Continue always passes a pass roster. When the load then finds no usable save (removed or malformed after the peek), a new two-seat pass match starts, and in a hidden game the hand-off. The sweep removed the solo-only peek that used to choose solo here. | fix | When `Start::Resume` finds no usable save, start the game's first startable mode in `MODE_TEXTS` order (solo first), never pass by default; or end in the error view if that is smaller. Record which in the fix plan. |
| 4 | EC1 | 5.1 × 5.2 × 5.4 | In a pass match, an event still queued after the round-ending move is played with `seatShown` = 0, so `rules.input(…, 0, …)` runs for seat 0. That is a seat game code never gets as an input seat, so per-seat indexing raises a ScriptError over Over. | fix | Seat 0 is a frame, never an input seat: drop input for seat 0 (or once `status.over`) in `SoloRounds::play`/`step` and `GameVM::stepHandOff`, before `Session::handle`. Pin it with a queued tap behind the winning move. |
| 5 | EC2 | 5.1 × 5.2 | Open pass: seat A taps twice fast, the second tap is dequeued after A's move passed the turn, and it becomes seat B's move (the turn check uses the seat chosen at dequeue time). | fix | Tag a posted input with the turn (or frame) generation it was posted under, and discard it when the turn seat has changed since. The hidden path already routes late taps to the mover. |
| 6 | A6 | 5.4 | Every render while in HandOff pushes `FULL_REFRESH`, for example after the light panel closes or a status repaint, not only on entering HandOff. | fix | R4 needs a full refresh when the blank is shown. Track the blank on the panel, and repaint without a full refresh afterwards. |
| 7 | A8 | 5.10 | The shared `GameConfirmDialog` lives in `GameModeActivity.h`, so the launcher depends on the title screen. Its `focus` is a plain `uint8_t` written by the loop task and read by the render task: a data race. | fix | Move it to its own fork-only header `src/activities/games/GameConfirmDialog.h` and make `focus` atomic. `check_layers.py` gets any new edge. |
| 8 | A9 | 5.1 × 5.2 | `seatShown` returns `status.turn` in Playing without the `roster.isLocal` check that Result has; it relies on `LuaGame` rejecting a bad turn. epic-play-nearby brings partial `localSeats`. | fix | `return roster.isLocal(status.turn) ? status.turn : NO_SEAT;`, pinned in `SeatShownTest`. |
| 9 | A4 | 5.1 × 5.2 × 5.4 | `seatShown(const MatchLifecycle&, …)` has no production caller; `canvasUnderView` re-implements its rule, and the VM keeps its own Result/HandOff view, which lags the match: it stays Result after the match moves to HandOff (kept off the panel by the seat gate). | fix | Make the render's choice call the one pure function, or delete the unused overload. Add a test that the VM's view and the match's state agree at each hidden transition, or that the lag can never reach the panel. |
| 10 | A12 | 5.9 | `savedForAnotherHost` reuses the live store and adopts the save's roster into it. That Error never writes is stated only in a comment. | fix | Restore the store's roster after the check (or use a store-free check), and add a test that Back from this error view leaves `resume.bin` byte-identical. |
| 11 | A11 | 5.10 | `GameMatchActivity.h`'s headline comment says "solo or an open pass match" and leaves out hidden pass. | fix | Comment only. |
| 12 | VG1 | 5.4 × 5.7 × 5.8 | No test takes `pass-hidden`'s real `manifest.json` (`hidden: true`) through the launcher, the title screen's Pass row, the first HandOff, Over and Play again. Every hidden test sets `manifest.hidden` in code. | fix | Add that end-to-end harness test: blank with FULL and no texts before any seat's push, then Over and Play again into HandOff. |
| 13 | VG2 | 5.5 × 5.7 | R8's "not offered" is tested only on `peek` with explicit caps. No title-screen test opens a pass save on a host without pass, or with fewer seats than the save. | fix | Covered by row 2's tests: no Continue, the New question shown, bytes unchanged. |
| 14 | VG3 | 5.9 | The resumed-hidden-match test counts only seat 1's draws before the blank's tap, but the saved turn seat is 2. | fix | Count every seat's draws (`"draw for seat "`), or assert the VM publishes nothing until `showTurnSeat()`. |
| 15 | A5 | 5.6 | The forced exit's half-refresh blank is pushed even in Over (seat 0 is public), HandOff and Paused-from-HandOff (already blank), and Error, spending the 1,500 ms window the SD steps share for no privacy gain. | owner | R6 and AD-12/AD-20 say a hidden pass match's `onExit()` pushes the blank, with no state condition. Narrowing it changes a requirement's literal text, which is the owner's call. It goes to entry 11 with the half-refresh measurement as an `Assumption for entry 11:` line: if the device's push skips SD steps, push only from Playing, Result, or Paused from either. |
| 16 | VG4 | 5.4, 5.5 (R13) | The screen double (text since the last clear, a push that costs no time) and the storage double (`failWriteAt`, more permissive than exFAT) are pinned to their own comments, not to the device. | packet | The device-run packet lists them as unpinned assumptions: the half-refresh time, no ghost after the blank, and exFAT's partial write. |
| 17 | VG5 | epic | Device-only lines: R14 entire; R6's bound with a real half refresh; ghosting after FULL and HALF (Done when 2); R8's "wake from sleep lands on Home"; R13's fixture calibration; R9's first tap from Home (host tests count from the launcher). R12's size and layers are CI jobs. | packet | Steps in the device-run packet. |
| 18 | A10 | 5.2 | `CheckReason::NearbyNeedsTwoSeats` also covers pass but keeps its name. | reject | Decided in entry 2's ticket: the enumerator keeps its name so the installer's and launcher's switches need no change; only `describe` is generalised. |

## Result

Accepted for the fix story: rows 1–14. Row 15 goes to the owner, rows 16–17 to the packet, and row 18 is rejected.

- **Who fixes them:** a build agent fixes rows 1–14 in one commit (plan `_bmad-output/implementation-artifacts/plan-e5-xr-cross-story-fixes.md`).
- **Review of the fix:** that commit gets the same three context-free lenses before the push, recorded below.
- **Flash:** the share left is 5,056 B and 32 B (entry 10's measurement), and the fix measures its own delta.

## Review of the fix commit

**What was reviewed:** the fix commit `ef46f8a5` (plan `plan-e5-xr-cross-story-fixes.md`), diff `a002936c..ef46f8a5` without planning documents, checked by the same three context-free lenses on 2026-10-01. Raw reports are in the orchestrator's scratchpad (`xreview/fix-*.md`).

**Overall result:**
- The lenses found rows 1–14 closed, with a test pinning each.
- They judged sound the builder's departure from row 4's literal action. Seat 0 still gets input after Over, as R7 requires; only a touch made under another seat's frame is dropped.
- The findings below go to a second fix commit, which gets the same review.

| # | Lens | Finding | Verdict | Reason / action |
|---|---|---|---|---|
| F1 | A2, EC3 | A save of this package with a newer file or codec version, or a snapshot over this firmware's limit (a later firmware's save after a downgrade), still reads as None, so New replaces it without asking. Row 2's trigger is still open for those files. | fix | Report such a file as Unstartable, as an unknown mode byte now is. |
| F2 | A1, EC4 | The touch tag is the frame number read before `drawFront` and stored after `displayBuffer`. If a publish lands between the two, the panel shows seat B while the tag names A, and B's taps are dropped. A tap read during the refresh carries the old tag. A wrap past `UINT32_MAX`, or the sentinel itself, misroutes. | fix | `drawFront` returns the generation it actually drew, under the frame mutex, and the match stores that. Compare with wrap-safe arithmetic and keep the sentinel out of the range. Add a test with a publish between the read and the draw. |
| F3 | A3 | `confirmRow` and `removeIndex`, the dialogs' open flags, are still plain integers written by the loop task and read by render. Each opener sets the row before `focus = 0`, so a render in between draws the destructive button focused. | fix | Make them atomic, store `focus` first, then publish the row with release ordering. |
| F4 | A4, EC1 | `SoloRounds::start`, `restart` and `step` pass `seatShown`'s new `NO_SEAT` to `play` and `draw` as seat 255. This cannot happen until a roster has a non-local seat, which epic-play-nearby brings. | fix | Return Ok without input or draw when `shownSeat()` is `NO_SEAT`. |
| F5 | EC2 | `GameVM::showSeatNow` to a non-local turn seat: `drawShown` returns Ok without drawing, `seatServed` opens the render gate, and the mover's private frame is pushed after the blank. Not reachable until epic-play-nearby. | fix | When the turn seat is not local, stay in HandOff and do not open the gate. |
| F6 | A5 | `peekResume` allocates a second `SNAPSHOT_LIMIT` buffer while the match holds the store's buffer. Under low heap an Unstartable save then shows the generic text, and `peekStartable`'s "never while a match runs" comment is now false. | fix | Reuse the store's buffer (nothing references it after an empty `loadResume`), or correct the comment and pin the fallback. |
| F7 | A7 | A timer queued behind the winning move still reaches seat 0's `input`, by design (R7, R11). This reverses entry 1's plan row 3 for taps, and neither the reversal nor the timer path is recorded or tested. | fix | Record the departure and the reversal in the fix plan, and add a VM test that a seat-0 timer after Over is delivered without error. |
| F8 | A8 | `GameEvent.h`'s `serial` says "Timer only", but touches now carry the frame tag in it. | fix | Comment only (`lib/GameCore/GameEvent.h` joins the fix's `touches` for this line). |
| F9 | A9 | `rosterFor` logs `LOG_ERR` "Cannot start … in pass" for a row its search skips, even when a later row starts, and a test asserts that line. | fix | Search quietly, and log only when no row can start. |
| F10 | A10 | Row 1's real trigger, `goToGames()` running out of memory, is never driven. | fix | Add a manager double whose `goToGames` refuses. Assert the match stays current, the panel holds the blank, and a later sleep pushes nothing. |
| F11 | VG2 | The no-VM blank push in `onExit` runs under the manager's `RenderLock`, but its test does not assert `fakelock::selfDeadlocks == 0`. | fix | Add the assertion. |
| F12 | VG1 | The resumed hidden match's zero-draw check runs after 20 non-blocking loops, with no barrier on the VM task, so an early draw of the turn seat could pass unseen. | fix | Assert exactly one draw of the turn seat after the blank's tap, or wait for the VM's first publish before the zero count. |
| F13 | A12 | `STR_GAMES_RESUME_NOT_HERE` is about 100 characters, and no test shows it fits the error view on every screen. | fix | Assert the full text is drawn untruncated in the error-view case, or shorten it. |
| F14 | A6 | Leave's blank uses HALF_REFRESH, while the hand-off blank uses FULL. When `goToGames()` fails, the half-refreshed blank stays. | packet | Ghosting is a device check: entry 11 checks the Leave blank beside the sleep blank (row 17). |
| F15 | A11 | A corrupt mode byte is now kept for good as "cannot be continued on this device", and New asks about it every time. | reject | Keeping a file of this package and asking before New is the safe side for a person's save (R8). Telling corruption from a later firmware's mode needs a format the save does not carry. |

## Review of the follow-up fix commit

**What was reviewed:** `723f08fb` (rows F1–F13; diff `00ca4070..723f08fb` without planning documents), checked by the same three context-free lenses on 2026-10-01. Raw reports are in the orchestrator's scratchpad (`xreview/fix2-*.md`).

**Overall result:** all three lenses found F1–F13 closed, each pinned by a named test except F3. F3's store ordering cannot be staged in the host harness, and no cheaper pin was found. The verification-gap lens also noted that F11's premise was false: `expectOneBlankPush` already asserted `selfDeadlocks == 0`, so the added line duplicates it, harmlessly.

| # | Lens | Finding | Verdict | Reason / action |
|---|---|---|---|---|
| G1 | EC (3), A | With a roster where some seats are local and some are not, F4 and F5 fail closed but stall. A local move that passes the turn to a non-local seat is never drawn. A hidden hand-off to a non-local seat leaves the match in Playing while the VM stays in HandOff, so the blank stays for good. A timer due under a non-local seat is lost. A round whose first turn is non-local never publishes a frame. | defer | No match can have such a roster until epic-play-nearby, whose remote seats decide what the device shows while it waits. `deferred-work.md` gets a `## e5-xr` entry that blocks epic-play-nearby on it, naming the four paths and the F5 test's second `showTurnSeat()` call, which the match cannot make. |
| G2 | A | The touch tag is read when the loop handles the release, not at touch-down. A tap released during the refresh, or while the loop is blocked in `flushResume`, can carry the previous frame (dropped) or the next one. The fix also deleted `game-canvas.md`'s "may be dropped" caveat. | fix | Restore the caveat in `game-canvas.md`, naming both windows. Latch the tag at touch-down only if it stays small, and record the choice in the plan. |
| G3 | EC, A | Another package's save with a newer codec version logs `LOG_ERR` "discarded" on every title-screen open, while `formats.md` promises `LOG_INF` for another package's save. | fix | A newer codec from another package is `OTHER_PACKAGE` (None, `LOG_INF`), and the test case changes with it. |
| G4 | EC | `tooLarge` is checked before mode and seat count, so this package's oversized file with a bad seat count is kept as Unstartable, and New asks about it every time. | fix | Check mode and seat count before the size. |
| G5 | A | `startNew` logs every non-Continue failure as "Cannot start … in pass". | fix | Name the row's mode (`MODE_TEXTS`) in the line. |
| G6 | VG, A | Nothing pins the wrap-safe compare: reverting to `>=` fails no test, though a tag about 2^31 ahead of the seat's frame pins it cheaply. Separately, the sentinel is remapped rather than kept out of the counter's range. | fix | Add the cheap pin. A comment states that the counter cannot reach `UNTAGGED` in practice (2^32 publishes). |

**Who fixes them:** the build agent fixes G2–G6 in one more commit, and records G1's deferral. That commit gets a last pass from the same three lenses before the merge.

## Review of the third fix commit

**What was reviewed:** `df63158f` (rows G2–G6; diff `e47e33d9..df63158f`), checked by the same three context-free lenses on 2026-10-01. Raw reports are in `xreview/fix3-*.md`.

**Overall result:** all three lenses found G3–G6 right and their pins effective. The findings below concern G2's touch-down latch, its test double, and one save case G4 missed.

| # | Lens | Finding | Verdict | Reason / action |
|---|---|---|---|---|
| H1 | EC, A | A contact that latched and then opened the light panel by a top-edge swipe ends without the match's loop seeing its release. `ActivityManager` handles the swipe first and `handle()` is not called, so the latch survives. The first tap after the panel closes carries the pre-panel frame and can be dropped wrongly. | fix | Clear the latch on any pass with no contact present. |
| H2 | EC | This package's newer-codec save with a bad seat count or unknown mode byte returns before the mode and seat checks, so it is kept as Unstartable and New asks every time. G4's rule covered only the oversized case. | fix | Run the mode and seat checks before the newer-codec return. |
| H3 | VG, A | Two paths are untested. The no-latch fallback is never driven, because the double's `tap()` reports a touch-down and a tap on the same pass, which the device never does. A lift that makes no gesture is never driven either. | fix | The double's `tap()` stops reporting a touch-down, as the device does, or a `quickTap()` is added. Add a release with no gesture. Test both paths. |
| H4 | VG, A | `handle()`'s latch reset on a state change is untested and does almost nothing: touch-down is a level, so a finger still held re-latches on the first Playing pass. | fix | Remove the reset, or test it. Either way, word the comment for what the code does. |
| H5 | VG, A | `holdTouch`/`liftTouch` lack the device's 500 ms long press, which fires during a still hold and suppresses the rest of the contact, and `isScreenTouchHeld` reads false while held. `InputDoubleTest` checks the double only against its own comment. | fix | Model the long press and the held flag against `MappedInputManager.cpp`/`InputManager.cpp`, name them in the double's comment, and add a G2 variant whose long press lands after the push. What still cannot be pinned on the host goes to the packet (row 16). |
| H6 | A | The new `game-canvas.md` paragraph and comments mis-state the latch. A still-held finger is re-latched at the first Playing pass, not tagged at lift. The 90 ms delay applies to every contact, so the "dropped" window is wrong for touches in the refresh's last 90 ms. | fix | Reword to match the code. |
| H7 | A | The premise that closing the remaining windows (a touch-down first seen after an SD write; a tap under 90 ms) needs upstream input timestamps is false. `MappedInputManager::getHeldTime()` already gives a tap's held duration for 250 ms after it, so a contact can be back-dated to `now - getHeldTime()` against a per-frame display time. | fix | Back-date the contact if that stays small, closing the windows in which a touch reaches the next seat. Otherwise record the real reason (a display time per frame) in the plan and `game-canvas.md`, and this goes to entry 11 as an `Assumption for entry 11:` line. |
| H8 | A | The new `InputDoubleTest` was inserted between `ScreenRendererDoubleTest` and its comment. | fix | Move the comment back. |

## Review of the fourth fix commit

**What was reviewed:** `4fbb9a87` (rows H1–H8; diff `0c18e437..4fbb9a87`), checked by the same three context-free lenses on 2026-10-01. Raw reports are in `xreview/fix4-*.md`.

**Overall result:** H1, H2, H4, H6 and H8 check out:
- the new loop fields are loop-only, and `lastPush` is guarded by `pushMutex`;
- the lock order holds and the compares are wrap-safe;
- the save checks run in H2's order.

The findings concern how far H7's back-dating reaches on the device, and its test double.

| # | Lens | Finding | Verdict | Reason / action |
|---|---|---|---|---|
| J1 | EC, A | The device samples touch only in `mappedInputManager.update()` on the loop task, so a finger that comes down during `flushResume`'s SD write is first stamped after the block. Back-dating cannot reach before the push that completed meanwhile, and that tap can still reach the next seat. The H7 test passes only because the double stamps the touch-down when the test calls `holdTouch`. | fix + owner | Make the double stamp at the first frame that reads the contact. Turn the test into one showing this window is still open. Correct the comment and `game-canvas.md` ("What remains" lists taps too). The remaining window goes to entry 11 as an `Assumption for entry 11:` line. Closing it needs touch sampled off the loop task, which is upstream input code. |
| J2 | EC, A, VG | `getHeldTime()` returns a button's hold, not the touch's, when a button was pressed or released on that update, and 0 when a home action is mapped. A tap read on such a pass is back-dated by seconds and wrongly dropped. The double ignores both cases. | fix | Use a touch-only held time if one is reachable without an upstream edit. Otherwise skip back-dating on a pass with a button edge or a mapped home action, so the tap falls back to the latch. Model the precedence in the double and test it. |
| J3 | EC | The back-dating anchors to `millis()` at read rather than to the release sample's time, so it is late by the update-to-read delay, a few milliseconds. | fix if small | Anchor to the release sample when it is reachable without an upstream edit. Otherwise note it beside J1. |
| J4 | A | The drop window now runs until `displayBuffer` returns, after the post-waveform resync and power-off. The next seat's first tap on a frame that is already fully visible may be dropped silently. Unmeasured. | packet | The device-run packet measures the gap between the waveform's end and `displayBuffer`'s return, and checks that the next seat's first tap registers. |
| J5 | A | The double's 90 ms and 500 ms thresholds are copied numbers. The real constants are private to upstream `MappedInputManager.cpp` and the SDK's `InputManager.h`. | defer | A CI check that reads them is a new gate in upstream-owned files' terms. `deferred-work.md` (`## e5-xr`) records it, with the packet checking both thresholds on the device. |
| J6 | VG | H7's after-the-refresh stamp is unpinned, because the fake `displayBuffer` takes no time. | fix | In the H7 test, use `onDisplay` to advance the clock during the push. |
| J7 | VG | H4's removal of the latch reset in `handle()` is unpinned. | fix | Add a hidden test that holds a contact from Playing through Result and HandOff into seat 2's first Playing pass, then long-presses; assert "Dropped a touch". |
| J8 | VG | The double reports a long press only on the first read of a pass, while the device reports it for the whole update, and `InputDoubleTest` pins the "once" behaviour. | fix | Match the device and fix the test. |

## Review of the fifth fix commit

**What was reviewed:** `0c35be0d` (rows J1–J8; diff `2ae3587f..0c35be0d`; production change 12 lines), checked by the same three context-free lenses on 2026-10-01. Raw reports are in `xreview/` (`fix5-*`, summarised here).

**Overall result:** verification-gap found nothing material, and each of J1, J2 and J6–J8 has a test that fails on revert. Edge-case found no gap in the production change. Adversarial confirmed that:
- `HalGPIO::lastTouchHeldMs()` is the tap's own hold and cannot be stale;
- touch is sampled only in `update()` on the loop task;
- the double's per-update sampling matches the device.

| # | Lens | Finding | Verdict | Reason / action |
|---|---|---|---|---|
| K1 | A | `game-canvas.md` and a test comment overstate H7's back-dating. Each early return in `loopPlaying` (Back, a VM check, RoundOver, TurnChanged) moves the match out of Playing, so it never covers a tap "sampled on a pass that returned early". What it covers is the same-pass race: the push completes between a pass's `update()` and its latch. The second "What remains" bullet names a nearly unreachable path and leaves out the reachable one: a swipe or long press first sampled before the push completed, but latched after it in the same pass, carries the next seat's frame. | fix (docs and comments) | Restate the coverage as the same-pass race, replace the bullet with the reachable gap, and fix the test comment. Back-dating the latch itself through `InputManager::isTouchTapCandidate` would close the long-press half; it is deferred under `## e5-xr`. The change is documentation and comments only, so it does not get another three-lens round: the orchestrator checks the wording against this row before the merge. |
| K2 | EC, VG, A | The input double has three gaps. `liftWithoutTap()` reports a raw release for a contact no update sampled. Touch queries sample when no `update()` preceded them, earlier than the device would. `ResumeMatchTest`'s `frame()` skips `input->update()`. No current test depends on any of them. | defer | `deferred-work.md` `## e5-xr`, with the trigger "the next change to the input double or a held-contact test in `ResumeMatchTest`". |

## Review of story 5.12 (combined diff)

**What was reviewed:** merge `13287eed` (story 5.12, built after the cross-story review; diff against its pre-merge base, production and tests). Three context-free lenses reviewed it on 2026-10-02. The raw reports are in `xreview/` (`e12-*`) and are summarised here.

**Overall result:** adversarial confirmed:
- the seat gate, touch tagging and the held-back hand-off screen;
- the white blanks before SD steps;
- RenderLock, Unstartable saves, the prefs.bin write path, the C++ and `pack_game.py` parity, and the repo rules.

Verification-gap found every change pinned except L7–L9 below, and confirmed `API_SURFACE_CRC` matches the list at `13287eed`. Edge-case found L2–L6. The flash headroom inside the share is 352 B (20,000 − 19,648), so the fixes stay minimal; a fix that would cross the share stops for the owner.

| # | Lens | Finding | Verdict | Reason / action |
|---|---|---|---|---|
| L1 | A | A home-key Confirm has no hold time: `getHeldTime()` returns 0 when a home action is mapped, so the guard dates it `began = now`. The second press of a double press begun during the hand-off push passes it, contrary to `game-canvas.md` and the header comment. | fix | When Confirm comes from a mapped home action, fail closed: drop it unless `now − passScreenShownMs` is at least the home key's double-tap window, or use the key's own press time if reachable without an upstream edit. Model `homeAction` in the input double and test that a home-key Confirm during the push does not pass. Correct the doc. |
| L2 | EC, A | When `loadSettings` fails (ManifestRead OOM, a card fault, or a hash or count mismatch), the match still starts with `ctx.settings == {}`, against the API text "every declared setting is present". The OOM log claims defaults. | fix | In `startMatch`, refuse to start while `settings.count != manifest.settingsCount`: `LOG_ERR`, the existing start-failed text, repaint. Fix the log line and change `WhileTheSettingsCouldNotBeReadPrefsBinIsLeftAlone` to assert the refusal. |
| L3 | EC | Play again after Continue runs setup with the title screen's current choices, not the save's. | keep, document | `startMatch`'s comment states this is intended: the player's Options choice at the title screen is the latest choice. Storing settings in `resume.bin` would change its format for little gain. Add one sentence to the `ctx.settings` API text in `game-canvas.md` saying Play again uses the title screen's current choices. |
| L4 | EC | A remembered or default pass mode on a host where `passSeats()` is 0 leaves New game a logged no-op, though solo could start. | fix if small | Pass `resolvePrefs` the modes `rosterFor` accepts, so `startMode` falls back to a startable mode. If it costs more than a few dozen bytes, defer under `## e5-xr` instead; no current host allows pass with fewer than two seats. |
| L5 | EC | After an Unreadable prefs.bin (a transient card fault), any Options change overwrites a good prefs.bin with defaults for every untouched setting. | fix | When the state is Unreadable, re-run `loadPrefs` before `rememberChoices` saves. If it is still Unreadable, save as designed. Test both. |
| L6 | EC | An installed game calling `ch.gfx.image("title")` now gets an unknown-image error. | no change | Level 1 is not frozen (`API_LEVEL_FROZEN false`), and reserving `title` and `handoff` is part of the approved design. No game has been released against level 1. |
| L7 | VG | Nothing checks a resumed hidden match's hand-off picture, so moving the art load above `seedResume` would fail no test. | fix | In `AHiddenPassSaveResumesOnTheBlankAndItsTapShowsTheSavedTurnSeat`, assert the hand-off push's fills equal the icon fallback's; add a Continue case with `handoff.bmp` on the card. |
| L8 | VG | Result's keep-the-stamp-on-repaint rule (F3) is tested for HandOff only. | fix | Add a Result copy of `ATapStraddlingARepaintOfTheHandOffScreenPassesIt`. |
| L9 | VG | The wrap-safe compare in `loopHandOff` is unpinned. | fix | Add a hidden test with the fake clock just under 2^32 across the push. |
