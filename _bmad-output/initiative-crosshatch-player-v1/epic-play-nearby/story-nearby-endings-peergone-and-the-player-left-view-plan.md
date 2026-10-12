---
title: 'Nearby endings: PeerGone and the player-left view'
type: 'feature'
ticket: '15'
created: '2026-10-12'
baseline_revision: '9fe036699ba4eae1a183dfdac93a829b618c8f40'
status: done
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
followup_review_recommended: false
context:
  - '{project-root}/AGENTS.md'
  - '{project-root}/docs/crosshatch/orchestrated-epics.md'
warnings: ['oversized']
deferred:
  - summary: >-
      A nearby Leave, script error, or watchdog stop with an abandoned (stuck) VM is untested, so the `!slotLeaked` guard before `nearby.reset()` and the unfreed release in the destructor can regress silently.
    evidence: |-
      Closing it means extending the stuck-VM rig (SPIN_GAME in GameMatchTest) to take a NearbySession and asserting the link's end() is not called while a leaked task may still call adoptNext and ship; the same gap entry 9 deferred for the destructor. The guard mirrors the destructor's and reads right; a regression would be a use-after-free on the device.
    location: >-
      src/activities/games/GameMatchActivity.cpp (leave, ~GameMatchActivity)
    severity: low
  - summary: >-
      A nearby match that ended in PeerGone or Error keeps the radio up and preventAutoSleep() on until the player presses Back, so a device left on the view stays awake.
    evidence: |-
      R6 says the match sets preventAutoSleep and the error view has kept the session alive since entry 9; releasing the lock in those two states is one expression, but whether the battery cost matters is a device question (entry 11/14's runs).
    location: >-
      src/activities/games/GameMatchActivity.h (preventAutoSleep)
    severity: low
---

<intent-contract>

## Intent

**Problem:** A nearby match has no ending. Entry 7's `EndSlot` and entry 8's `NearbySession::leave` exist, but `GameMatchActivity` never reads the slot or says goodbye: a peer that leaves, goes silent, or hits a script error leaves the other device on its board or on "Waiting for the host" forever, and a Leave, a sleep, or a script error tells the peer nothing (R5's match part, R6, AD-14, AD-20, AD-21).

**Approach:** `MatchLifecycle` gains the `PeerGone` state and event; the match's `loop()` takes the end slot once and enters `PeerGone`, whose view has one reason line and Back; Leave, the forced exit, and a script error each call `NearbySession::leave` (flush 800 ms, none, none); entering `PeerGone` logs its reason; `handle()`'s `setWritable` calls become one state-to-writable mapping.

## Boundaries & Constraints

**Always:** `PeerGone` is entered only from the match's own `loop()` reading `NearbySession::takeEnd`, so it waits behind an open overlay (R5, AD-20); the first reason wins and is read once. The goodbye is `NearbySession::leave(reason, flushMs)` and nothing else (three copies; Leave 800 ms, forced exit and script error 0). Solo and pass behave exactly as today: every new branch is behind `nearby != nullptr` or a state only a nearby match reaches, and every existing solo and pass test passes unchanged. `-fno-exceptions`: no bare `new`; locals under 256 B; text through `tr(STR_GAMES_*)`, new keys in `english.yaml` only; logs through `LOG_*`. No new mutable static and no IRAM. The Leave order is flush and ABORT, VM stop, radio down, then `goToGames()` (AD-20). `RenderLock` is never taken in the destructor or in `onExit()` (12cc816). A double names the device behaviour it stands in for and a test pins that they agree.

**Never:** any `lib/GameCore/**` change but `MatchLifecycle.*`; `src/games/NearbySession.*`, `GameLink*`, `EspNowLink.*`, `GameModeActivity.*`, `games/**`, `test/game_script/first_party/**`, any upstream file but `english.yaml` (STR_GAMES_* only), `freeink-sdk`, the spine. No lobby, no `NEARBY_BUILT`, no mode plumbing (entry 10). No `resume.bin` read, write, or delete. No reconnect, no PeerGone for solo or pass.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Leave, reachable peer | A chooses Leave (pause or end-of-round menu) | A flushes, puts 3 `ABORT(left)` on the link, stops the VM, ends the link, asks for Games; B shows "player left" with the left line | none |
| Leave with a STATE unacked | A's peer is unreachable, one STATE unacked | the flush ends at 800 ms (not before), the 3 `ABORT(left)` go all the same | the peer learns by silence |
| Forced exit | sleep or any Replace in a live nearby match | 3 `ABORT(left)` at once, no wait, before the VM stops | none |
| Silence | no frame from the peer for 10 s | PeerGone with the silent line, not before 10 s | none |
| Script error, the mover | A's `apply` fails | A shows the error view and puts 3 `ABORT(script_error)` at once; B shows "player left" with the game-error line | none |
| Version mismatch or full | `ABORT(version_mismatch)` or `ABORT(full)` | PeerGone with the version line, or with the left line | none |
| Overlay open | the loop is held when the reason latches | nothing changes; the next `loop()` pass enters PeerGone | none |
| From each live state | Starting, Playing, Paused, Over, (Result, HandOff) | PeerGone | none |
| Error or PeerGone already | A is in Error and its peer then leaves | A keeps its error view; the slot is not read | none |
| Two reasons | `ABORT(left)` then silence | one PeerGone, one log line, the first reason | none |
| Back or the Back row in PeerGone | Back button, or a tap on the row | Leaving (the Leave order above); Home and Confirm do nothing | none |

</intent-contract>

## Code Map

- `lib/GameCore/MatchLifecycle.{h,cpp}` -- add `MatchState::PeerGone` and `MatchEvent::PeerGone`; `next()` (a ForcedExit/Leaving line pattern: Leaving first, ForcedExit next, then the new event), `menuFor` (PeerGone shares the error view's `{Back}` menu), `name()` of both enums; update the header's machine comment (`Leaving is terminal`, the `ScriptError` line, the new rows).
- `src/games/GameViewIcons.h` -- `forView(PeerGone)` = `"users"`; `forOption(PeerGone)` is never a menu choice.
- `src/games/MatchPersistence.h` -- `static constexpr bool writableIn(MatchState)`, an exhaustive switch (no `default`), and the include of `MatchLifecycle.h`.
- `src/activities/games/GameMatchView.{h,cpp}` -- `Input::endReason` (`Protocol::AbortReason`); `viewHeadline`, `optionLabel` and `build` gain PeerGone (the reason line is the headline; the one row is Back, `MAX_OPTIONS` unchanged); keep `menuFor(state, followsHost)`.
- `src/activities/games/GameMatchActivity.{h,cpp}` -- `loop()` (the end read before the state switch), `handle()` (the `setWritable` calls become `persistence.setWritable(MatchPersistence::writableIn(to))` for every `to` but Leaving; `case PeerGone` logs the reason line), `fail()` (ABORT), `leave()` (goodbye before the lock; radio down inside it), `onExit()` (goodbye before `handle(ForcedExit)`), `loopView()` (PeerGone is Error-like), `render()`, `canvasUnderView()`, `renderView()`, `viewScreen()`; members `endReason`, `LEAVE_FLUSH_MS`.
- `src/games/NearbySession.h` (read only) -- `leave(reason, flushMs)` returns `LeaveResult`; `takeEnd`, `endLatched`; its body is lock-protected and holds no lock across the wait, so the loop task may call it.
- `src/games/GameLinkLog.h` (read only) -- `abortReasonName` (`left`, `script_error`, `timeout`, `full`, `version_mismatch`), reused for the `GAME` line.
- `src/games/GameVM.*` -- unchanged (not needed; PeerGone stops the loop's `pollNearby`).
- `lib/I18n/translations/english.yaml` -- `STR_GAMES_PEER_LEFT`, `STR_GAMES_PEER_SILENT`, `STR_GAMES_PEER_ERROR`, `STR_GAMES_PEER_VERSION` (Design Note 4).
- `docs/crosshatch/game-canvas.md` -- the state table, Leaving, the forced exit, the views table, Overlays' last sentence (the move to PeerGone now exists).
- Tests: `test/game_core/MatchLifecycleTest.cpp`; `test/game_script/GameViewIconsTest.cpp`, `harness/MatchPersistenceTest.cpp`, `harness/NearbyMatchTest.cpp` (the rig of entry 9: `Device`, `Duo`, `RawHostRig`).

## Tasks & Acceptance

**Execution:**
- [ ] `lib/GameCore/MatchLifecycle.{h,cpp}`, `test/game_core/MatchLifecycleTest.cpp` -- PeerGone state and event, the table rows, the tests -- Design Notes 1, 2
- [ ] `src/games/GameViewIcons.h`, `test/game_script/GameViewIconsTest.cpp` -- the icon and the counts
- [ ] `src/games/MatchPersistence.h`, `test/game_script/harness/MatchPersistenceTest.cpp` -- `writableIn` and its test -- Design Note 6
- [ ] `lib/I18n/translations/english.yaml`, `src/activities/games/GameMatchView.{h,cpp}` -- the four strings, the view -- Design Notes 3, 4
- [ ] `src/activities/games/GameMatchActivity.{h,cpp}` -- the end read, PeerGone, the three goodbyes, the radio teardown, the mapping -- Design Notes 2, 5, 6, 7
- [ ] `test/game_script/harness/NearbyMatchTest.cpp` -- the endings suite and the one updated expectation -- Design Note 8
- [ ] `docs/crosshatch/game-canvas.md` -- the states, Leaving, the forced exit, the views
- [ ] Measure the cost like for like -- Design Note 9

**Acceptance Criteria:**
- Given two devices over `FakeLink` in the `nearby` fixture, when either chooses Leave, then it flushes for at most 800 ms (even with a `STATE` unacked), puts three `ABORT(left)` on the link, ends the link, and asks for Games, and the other shows "player left" with the left line and leaves on Back.
- Given a live nearby match, when the forced exit runs, then three `ABORT(left)` are on the link at once with no wait, before the VM stops, and the other device shows "player left".
- Given 10 s of fake time without a frame, when the loop runs, then PeerGone shows the silent line; given 9 s it does not.
- Given a script error on a device, when its `apply` fails, then it shows the error view and sends three `ABORT(script_error)` at once, and the other shows "player left" with the game-error line; the erroring device keeps its error view when its peer then leaves.
- Given an overlay, when a reason has latched, then PeerGone waits for the loop; given two reasons, then one `GAME` line names the first.
- Given each reason, then the `GAME` line's text and the view's line are pinned; `MatchLifecycleTest` covers every transition into and out of PeerGone; every solo and pass test passes unchanged.

## Implementation Notes

- Environment (written by the build session for the implementer): work only in the git worktree `/home/user/wt/epic-play-nearby-lane-a` (branch `epic-play-nearby-lane-a`), never in `/home/user/crosshatch-player`; use absolute paths under it. Host tests build in this worktree's `build/test` with AGENTS.md's commands under `flock /tmp/crosshatch-hosttest.lock` (a full rebuild is slow; the tree is configured already, run the configure line only if cmake says so). Scratch files go under `/tmp/claude-0/-home-user-crosshatch-player/f3b56526-12a6-5481-9360-b3275a33119e/scratchpad/6.15/`. Never run `pio`, `pio check`, or `sim.sh` (the build session runs them after the review), and do not `git commit`. Read AGENTS.md first. Order that keeps each step testable: `MatchLifecycle` and its test, `GameViewIcons` and its test, `MatchPersistence` and its test, `english.yaml`, the view, the activity, then `NearbyMatchTest`, then `game-canvas.md`. `git add` every new file before the last `./bin/clang-format-fix`, which formats only tracked files. The nearby tests' real-thread waits use `nearby::waitFor` (it moves the fake clock); a `leave()` flush runs in real time on the test thread (up to about 810 ms). Run each mutant of Design Note 8 once and revert it.
- Baseline firmware figures at `9fe03669` (x4pro games on minus off, measured in this worktree, `check_flash_budget.py build on`, `build off`, `compare`, incremental tree, under the build lock with the shared cache): +272,880 B flash (games off 5,955,408 B), +784 B static RAM.
- 2026-10-12, implemented by one subagent as the plan reads (the Agent tool ran it detached, so this session waited for it through its transcript; the orchestrator relayed its report). Its report: full `ctest` 2,454 of 2,454; the 11 mutants of Design Note 8 each red alone; `check_layers.py`, `check_upstream_touches.py`, and the fork script tests pass; `./bin/clang-format-fix` stable; no firmware run. Two deviations from the plan's text, both kept: (1) `AScriptErrorInTheAdoptedOverEventShowsTheErrorViewOnThatDeviceOnly` (entry 9's) now expects the host in PeerGone with the game-error line, because the guest's error now sends `ABORT(script_error)` (the plan's own rule; entry 9's test stood for "told by entry 15"); (2) `onExit()` skips the goodbye in Leaving (a user Leave said it already; matters when `goToGames()` failed or an abandoned VM kept the session).
- Flake record: the subagent saw `LeaveWithAStateUnackedFlushesForTheLimitAndSendsTheAbortsAnyway` fail once (`waited=0`) in about 60 stressed runs and could not reproduce it. Cause found by reading, not by a repeat: the test waited for `unacked >= 1`, which an ack still in flight for the round's first STATE satisfies; when that ack landed before the VM had shipped the tapped move, `leave()` found nothing unacked and nothing staged and returned at its first poll. The test now also waits for the move's `state sent` LINK line (counted before the tap) with `unacked >= 1`. Proof at the bar is recorded under Verification once run on the final tree.
- 2026-10-12, review patches applied by the same implementation subagent (re-engaged by id; five patch groups of the Review Triage Log): `leave(flushMs)` with 0 from PeerGone and Error; the end slot is not read in Playing while the VM has counted a round end the loop has not entered; `viewScreen` copies `endReason` for PeerGone only; tests for Back from the Error view, the silence limit measured from the raw host's last frame (9.5 s still Playing, 10.2 s gone), a renamed writes-nothing test, an unused helper removed; comment and doc wording. A second existing test also changed (see the first note). In the Back-from-Error test `goToGames` is 2 because the guest's Back asked once already. The subagent ran the four filtered suites (95 of 95, 10 repeats) and both review mutants (always-flush, gate removed), each red.

## Plan Change Log

## Review Triage Log

### 2026-10-12 — Review pass
- lens method: four context-free subagents (blind-hunter, edge-case-hunter, verification-gap, intent-alignment), started in one message; the Agent tool ran them detached, so the report of three reached this session as hand-backs and the edge-case lens's reached the orchestrator, which relayed it verbatim (recorded here as relayed). The diff given to them (`review-diff.patch`, 72 kB, `docs`, `lib`, `src`, and `test` since `9fe03669`) leaves out this plan, which the edge-case lens read as its claims file. The implementation subagent was re-engaged (by id) for the patches.
- verdicts: 26 findings (blind 13, edge-case 8, verification-gap 2 gaps + 2 others, intent-alignment 1 descriptive row) — high 0, medium 0, low 23, false 3, maybe-false 0; 5 patch groups (all low; patches 1 to 5 below), 2 deferred, the rest rejected.
- findings:
  - Blind 1 `[low]` `[patch]` Back from the player-left or error view waits up to 800 ms for acks from a peer already known gone — real (the same `leave()` flush runs for every user exit): patch 1, `leave()` takes the bound, 0 from PeerGone and Error, the three `ABORT(left)` still go (AD-20).
  - Blind 2 `[low]` `[defer]` PeerGone leaves the VM idle, the radio up, and `preventAutoSleep()` on until Back — the VM is idle (nothing is polled or posted; Back stops it) and a hung one is stopped by Back, so only the sleep lock matters: a device left on the view stays awake. Same for the error view today, and R6 says the match sets `preventAutoSleep`; the device run (entry 11/14) shows whether it matters. Deferred below.
  - Blind 3 `[low]` `[patch]` the "end beats the queue" rule has no code or test — the doc overstated it; patch 2 gates the end read in Playing behind a round end the VM already counted (R4: Over is reached), with a test, and the docs say no absolute ordering.
  - Blind 4 `[low]` `[defer]` the `nearby.reset()` branches (an abandoned VM keeps the session; a failed `goToGames()` after the radio is down) are untested — same gap entry 9 deferred for the destructor (the stuck-VM rig takes no `NearbySession`); the guard mirrors the destructor's. Deferred below with Verification-gap 1.
  - Blind 5 `[low]` `[patch]` Back from Error untested, and PeerGone from Starting, Result, HandOff only in the pure table — Back from Error patched (test, patch 4a); Starting never reaches a loop pass (onEnter leaves it) and a nearby match has no Result or HandOff, so those stay in `MatchLifecycleTest`'s table (reject for the rest).
  - Blind 6 `[low]` `[patch]` the silence test would pass at 9.5 s — patch 4b advances 450 ms past the nine seconds and asserts it has not ended.
  - Blind 7 `[low]` `[patch]` `ThePlayerLeftViewWritesNothingToTheCard` cannot fail on a `writableIn` regression — true (a nearby match never writes `resume.bin`; `MatchPersistenceTest` pins `writableIn`): patch 4c renames it to what it checks.
  - Blind 8 `[false]` `[reject]` the `setWritable` rewrite is a refactor in a feature change — the ticket asks for it (`## e6pre-1`, trigger a new `MatchState`); it is the old calls moved with the same values, Leaving making none, checked against the Over/Error/Playing/Paused/Result/HandOff cases one by one (Design Note 6).
  - Blind 9 `[false]` `[reject]` `fail()` is not gated on `allows(ScriptError)` — it is: its first line is `if (!lifecycle.allows(MatchEvent::ScriptError)) return;`, before the text is written or the ABORT sent; in PeerGone and Error `allows` is false.
  - Blind 10 `[low]` `[patch]` helper and doc drift (`probeOf` unused, the `duo` comment, the forced-exit list's "0.", "has not left already") — patched in 4d and 5; one real defect each, direct corrections.
  - Blind 11 `[low]` `[patch]` the doc paragraphs run past the file's wrap — re-wrapped in 5.
  - Blind 12 `[low]` `[reject]` the string checks are a character proxy, `Full` shows the left line, the version line gives no next step — `Full` cannot reach a match (`MatchSink` refuses strangers), a measured-width check needs the device font and the title fits two lines by the strlen bound, and the wording is the owner's to refine; not worth a branch.
  - Blind 13 `[low]` `[reject]` Leaving shows no feedback during the flush — AD-20 specifies the flush on the loop task with the menu still up (800 ms at most); a transient view would be a new state.
  - Edge 1 `[low]` `[patch]` Back from PeerGone and Error freezes the loop for 800 ms (relayed) — same as Blind 1.
  - Edge 2 `[low]` `[patch]` the flush runs on the loop task, which is the only waker of the VM, so a shipment the VM has not yet staged is not waited for (relayed) — real in the narrow case of a tap followed within milliseconds by Leave (a Leave takes two input events, hundreds of ms); `leave()` waits for what the link task has staged or sent. The comments and the doc claimed more: patch 5 corrects the wording; no code change.
  - Edge 3 `[low]` `[patch]` `endReason` is read by the render task for every state and written by `loop()` (relayed) — a formal race on a one-byte enum; patch 3 reads it only for PeerGone.
  - Edge 4 `[low]` `[patch]` the end read before `loopPlaying`'s `roundsEnded` check skips the final Over (relayed) — the same root as Blind 3: patch 2.
  - Edge 5 `[low]` `[patch]` no test of Back from the nearby Error view (relayed) — patch 4a.
  - Edge 6 `[low]` `[patch]` claim: "the VM keeps running, so a STATE it still has to ship goes out before the ABORT" (relayed) — same root as Edge 2: wording, patch 5.
  - Edge 7 `[false]` `[reject]` claim: "after a user Leave `nearby` is null, so nothing is sent twice" is wrong for an abandoned VM (relayed) — the guard is `lifecycle.state() != Leaving`, which is right for that case; the sentence is in this plan's Design Note 5, and a finding whose fix is to edit the plan is rejected. The code comment says the true mechanism.
  - Edge 8 `[low]` `[patch]` claim: "a queued over STATE is not adopted before a latched ABORT" is not guaranteed (relayed) — same root as Blind 3: the doc no longer claims an ordering (patch 2).
  - Verification-gap 1 `[low]` `[defer]` a nearby Leave or script error with an abandoned (stuck) VM is untested, so the `!slotLeaked` guard can regress silently — real as a gap; closing it means extending the stuck-VM rig (SPIN_GAME) to take a `NearbySession`, the item entry 9 deferred for the destructor. Deferred below (one item for both).
  - Verification-gap 2 `[low]` `[patch]` Back from the nearby Error view is documented to say goodbye and end the link, and no test presses it — patch 4a.
  - Verification-gap other 1 `[low]` `[patch]` "the end beats the queue" has no code or test — Blind 3.
  - Verification-gap other 2 `[low]` `[patch]` the writes-nothing test's name overstates — Blind 7.
  - Intent-alignment (descriptive) `[low]` `[reject]` readings A1/A2 (Error), C1/C2 (goodbyes beyond the three), the Error exclusion, the extra goodbye from Error and PeerGone, `writableIn(PeerGone)` false, the activity tests' fixed double surface, the log line's text not run by entry 11's reader — no defect: A2 and C2 are Design Notes 1 and 5 (the orchestrator has the Error reading with the owner), the flush on those two exits becomes 0 (Blind 1), the double is named in the test header, and entry 11 reads a line it can grep ("the other player is gone: <reason>").

## Design Notes

**Sources that settle the choices.** Epic R5 (the latched slot, read by the match's next `loop()`, "waits behind an open overlay"), R6 (Leave: flush at most 800 ms, then `ABORT(left)` three times, before the VM stops and the radio goes down; the forced exit three times without flushing; a script error sends `ABORT(script_error)` and shows the error view; one reason line: left, silent, game error, different version; Back leaves), the Notes' Decision 2026-10-04 (e6pre-8: PeerGone on the next `loop()`), the Decision of entry 7's checkpoint (C5: silence is `Timeout`; `EndSlot` first wins, read once), `deferred-work.md` `## e6pre-1` (the mapping, trigger a new `MatchState`), and the spine: AD-14 (a script failure ends the session, a peer is sent `ABORT(script_error)`, the error view has a single Back control), AD-20 (Leaving order; the forced exit), AD-21 (PeerGone, "Back" to Leaving). Entry 8's plan Design Note 4 (`leave` is one call, the bound is the caller's) and its header: three copies, `flushMs == 0` sends at once, nothing before `beginMatch()`.

**1. The machine.** `MatchEvent::PeerGone` moves any state but Error, PeerGone, and Leaving to `MatchState::PeerGone`, in both machines (the hidden ones' Result and HandOff included; a nearby match never reaches them). From PeerGone: `Back` and `ForcedExit` go to Leaving; nothing else is a transition (Home, Resume, Leave, PlayAgain, `ScriptError`, a second `PeerGone`). Its menu is the error view's `{Back}`. *The reading, not an intent gap:* the entry says "every non-terminal state"; `MatchLifecycle.h` calls only Leaving terminal, and AD-21's diagram gives Error one exit, Back. Error is where AD-14 ends the session ("a single Back control"): the erroring device has told its peer already, and its peer's later `ABORT(left)` (it leaves from its own "player left" view, AD-20) would otherwise replace the one view that says why. So Error is outside the sources of PeerGone, the same way PeerGone is outside Error's (a second end replaces nothing). The rows are in `MatchLifecycleTest` and are one `if` to flip.

**2. Reading the end (`loop()`).** Before the state switch: `if (nearby && lifecycle.allows(MatchEvent::PeerGone) && nearby->takeEnd(reason))` store `endReason`, then `handle(MatchEvent::PeerGone)` and return. `allows` is checked first so a reason is never taken in Error, PeerGone, or Leaving (it stays latched; nothing reads it). In Playing the read also waits one pass while the VM has counted a round end the loop has not entered (`roundsEnded() != roundsSeen`): `loopPlaying` enters Over first (R4: the other device reaches Over, `over` delivered once), and the next pass reads the end. A `STATE` still queued when the end is read is not adopted (the VM is not polled in PeerGone); no ordering is promised beyond that. `takeEnd` is lock-free and once-only, so two reasons give one PeerGone, the first (`EndSlot`). `endReason` is a plain member written before `handle()` and read by the render task after it saw `shown == PeerGone` (the pattern of `errorDetail`).

**3. The view.** PeerGone shows the error view's shell (`GameMatchView::build`): the game's name as the caption, the reason line as the headline (up to two lines), the library icon `users`, one row, Back (the sign-out row icon of the Back event). The buttons are the error view's: Back leaves; Confirm does nothing; Home is ignored. `renderView` treats it like Error (stands alone: no canvas under it, a full refresh, the Back hint). The mapping from `AbortReason` to the line: Left and Full to the left line (Full cannot reach a match, `MatchSink` refuses strangers, and a left line is true if one did), ScriptError to the game-error line, Timeout to the silent line, VersionMismatch to the version line.

**4. Strings.** Four, all `STR_GAMES_*`, no separate headline (the reason line is the headline: one string fewer than a headline plus a message, against the budget): `STR_GAMES_PEER_LEFT` "The other player left", `STR_GAMES_PEER_SILENT` "The other player went silent", `STR_GAMES_PEER_ERROR` "The other game stopped with an error", `STR_GAMES_PEER_VERSION` "The other player has a different version". (The implementer may adjust the wording but keeps each under 42 characters, which fit two lines of the title font in the dialog's 4/5 width, and records the final text here.)

**5. The three goodbyes.** All through `nearby->leave(reason, flushMs)`; the result is ignored (it is logged by `GameLinkLog`).
- *Leave* (`leave()`, on the loop task): `nearby->leave(AbortReason::Left, LEAVE_FLUSH_MS)` first, before `RenderLock` is taken, because the flush waits up to 800 ms and the VM keeps running meanwhile (a `STATE` it has still to ship goes out before the `ABORT`, and `leave` waits for a staged one); then, inside the lock after `stopVm()`, `if (!slotLeaked) nearby.reset();` (radio down, so Games is built with the heap back: `goToGames()` constructs its activity at once; the render task is excluded by the lock, which `followsHost()`'s read of `nearby` needs; an abandoned VM keeps the session, which the destructor releases unfreed). AD-20 says the same user exit covers Back from the error and "player left" views, so those say goodbye too (three `ABORT(left)`, harmless: the peer's slot is first-wins) but with a flush of 0 (review patch: the peer is known gone or told, and an unacked frame would freeze the view for the full 800 ms).
- *Forced exit* (`onExit()`): `nearby->leave(AbortReason::Left, 0)` right after `persistence.beginForcedExit()` and before `handle(ForcedExit)` and `stopVm()`, so the peer learns at once and a slow VM stop cannot delay it. It is not an SD step, takes no `RenderLock`, and costs three sends under the session's short lock: `FORCED_EXIT_DEADLINE_MS` (1,500 ms from the start of `onExit()`) bounds the steps after it and the send starts at once (the entry's `unknown`). After a user Leave `nearby` is null, so nothing is sent twice.
- *Script error* (`fail()`): after the `lifecycle.allows(ScriptError)` guard (so only the first failure sends), before `handle(ScriptError)`: `nearby->leave(AbortReason::ScriptError, 0)`. `fail()` is the one door of every ScriptError (a script error in a call, a stuck call from `stopStuckVm`, a failure to start), AD-14 says a peer is told for the session's end, and before `beginMatch()` `leave` sends nothing.

**6. The writable mapping.** `MatchPersistence::writableIn(state)`: true in Playing, Paused, Result, HandOff; false in Over, Error, PeerGone; the switch has no `default`, so a new state warns at compile time (the e6pre-1 trigger). In `handle()` the call is made for every `to` except Leaving, in the position the old calls had (after `shown.store`, before the per-state work). What each old call protected: Over false (a finished round must not write `resume.bin`), Error false (never writes), Playing/Paused/Result/HandOff true; Leaving made no call (so the forced exit's last write still sees the writability of the state it left, and `stopStuckVm`'s own `setWritable(false)` before Error stays).

**7. Guards in the functions edited (history read with `git log -S`; nothing moved).** `handle()`: the settings guard for Play again from Over stays for the host (a follower runs no setup); `lifecycle.apply` false returns; `closeRouting()` before the new view; `firstFramePushing` cleared by any transition but the hand-off's tap; `selected` reset; the gate counts stored before `shown`; `case Leaving` returns without `requestUpdate`. `loopView()`: `vmHealthy` first for every state but Error (now also PeerGone: its VM is idle and the loop polls nothing for it; Back stops it), the follower's Over exit, Back, the gap redraw, `menu.count` handling. `canvasUnderView`: the error view stands alone; PeerGone is the same. `fail()`: the `allows` guard keeps the first error view. `leave()`: the hidden-pass blank before Games, `retryResumeDelete(true)` and `flushStore()` after the lock. `onExit()`: the order of the SD steps and the blank is unchanged; the goodbye is added before them.

**8. Tests, doubles, mutants.** `MatchLifecycleTest`: PeerGone in `ALL_STATES`, `ALL_EVENTS`, `SOLO_STATES`, both tables (rows for the six sources, and Back and ForcedExit out), `reach` and `reachHidden`, the menu, the names, and named tests for "not from Error, PeerGone, Leaving", "leaves only by Back or ForcedExit", "a second PeerGone keeps the first". `GameViewIconsTest`: four views, the new name, eight non-menu events. `MatchPersistenceTest`: `writableIn` per state. `NearbyMatchTest` (the rig of entry 9; the doubles are its, listed in its header, nothing new): Leave on the host and on the guest with the other showing the left line and Back leaving (and the link ended before Games is asked for: `endCalls`); the 800 ms flush with a STATE unacked (blackhole, the `LINK` line's `waited=`); the forced exit (three `ABORT(left)`, `waited=0ms`, the peer's PeerGone); the script error (error view on the mover, three `ABORT(script_error)`, the other's game-error line, the mover keeping its view when the peer then leaves); silence at 10 s over a `RawHostRig` (9 s no, 10.1 s yes); version and full via `RawHostRig::putAbort`; the overlay; two reasons; from Paused and from Over; the log line per reason (`left`, `script_error`, `timeout`, `version_mismatch`, `full`). The existing test `AScriptErrorInApplyShowsTheErrorViewOnTheMoverAndShipsNoState` ("the other device is told by entry 15") changes its last two expectations to the new behaviour. Mutants, each red alone: PeerGone allowed from Error; `loop()` never takes the end; Leave without the flush; Leave's flush 2,000 ms; the forced exit with a flush; no ABORT in `fail()`; no goodbye in `onExit()`; `writableIn(Over)` true; swapped silent and left lines; no `nearby.reset()` in `leave()`; no log line. Host timing bounds are fake milliseconds, estimates until a device run.

**9. Measurement.** Baseline: `check_flash_budget.py build on`, `build off`, `compare` at `9fe03669` (this worktree, before any edit, under the build lock with the shared cache); then the same commands on the commit after the last patch. The delta is the difference of the two, with the new strings counted in both builds (the games-off build carries the I18n tables too: say what cancels). Gate: the projected total (the epic's +14,832 B / +0 B after entry 9, this entry's delta, and entry 5's driver +11,456 B / +80 B) must stay within 29,984 B / 160 B; if it would not, HALT blocked with the figures. No new mutable static, no IRAM.

**Residual choices recorded.** A `STATE` still queued when the end is read is not adopted (a peer that wins and leaves at once can show the other device "player left", not its last board, unless the VM had adopted the `over` before the loop looked: then Over is entered first). `ARCHITECTURE-SPINE.md` AD-21's "implements every state but Lobby and PeerGone" is outside this entry's `touches`; the orchestrator updates it.

## Verification

**Commands:**
- Host suites (Ninja `build/test`, AGENTS.md): `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test'`, then `ctest --test-dir build/test --output-on-failure -j` -- expected: every test passes, every existing solo and pass test unchanged (`ctest -N` differs by the new tests only).
- `ctest --test-dir build/test -R 'NearbyMatch|MatchLifecycle|MatchPersistence|GameViewIcons' --repeat until-fail:20` -- expected: all pass.
- Mutants (Design Note 8) -- expected: each red against its test, then reverted.
- Fast: `python3 scripts/check_layers.py`, `python3 scripts/check_upstream_touches.py`, the fork script tests, `./bin/clang-format-fix` twice (`git add` the new files first), `git status` unchanged by the second run.
- Firmware, once, after the review and its patches, under `flock /tmp/crosshatch-build.lock` with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`: `pio run -e x4pro`, `pio run -e default`, `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` and the same with `-e x4pro`, `scripts/check_flash_budget.py` `build on`, `build off`, `compare`, `objects`, and `.claude/skills/run-crosshatch-player/sim.sh build x4pro` -- expected: success; the delta over `9fe03669` in Design Note 9.

**Manual checks (if no CLI):**
- Read `GameMatchActivity.cpp`: no `RenderLock` is taken in the destructor or `onExit()`; `nearby` is declared before `vm`; the goodbye precedes `stopVm()` in `leave()` and in `onExit()`.
- No simulator screenshot: nothing opens a nearby match from a screen until entries 10 and 16.

## Auto Run Result

Status: `built`. Reviewed by four lenses (`lenses_ran`); 5 patch groups applied (all low), 2 low items deferred, nothing high or medium. Follow-up review recommended: `false` (no high patched, no two mediums; low patches only: 5 groups).

**Summary:** `MatchLifecycle` gains `PeerGone` (state and event, both machines): it is entered from every state but Error, PeerGone, and Leaving, leaves only by Back or the forced exit, and shares the error view's `{Back}` menu. `GameMatchActivity::loop()` takes the latched end (`NearbySession::takeEnd`) once, before the state switch and only when the state allows PeerGone, so it waits behind an open overlay; in Playing it first lets a round end the VM already counted reach Over. Entering PeerGone writes one `GAME` line, "<id>: the other player is gone: <reason>" (`left`, `script_error`, `timeout`, `full`, `version_mismatch`, the `LINK` vocabulary), and shows the error view's shell with one reason line as its headline (four new `STR_GAMES_PEER_*` strings), the `users` icon, and Back. Leave calls `NearbySession::leave(Left, 800)` before the VM stops and ends the link before Games is asked for (0 ms flush from PeerGone and Error); the forced exit calls `leave(Left, 0)` before anything else; `fail()` calls `leave(ScriptError, 0)`. `handle()`'s `setWritable` calls are one `MatchPersistence::writableIn(state)` mapping. `game-canvas.md` states it all.

**Files:** `lib/GameCore/MatchLifecycle.{h,cpp}`; `src/games/GameViewIcons.h`, `MatchPersistence.h`; `src/activities/games/GameMatchActivity.{h,cpp}`, `GameMatchView.{h,cpp}`; `lib/I18n/translations/english.yaml` (four `STR_GAMES_PEER_*` keys); `docs/crosshatch/game-canvas.md`; tests `test/game_core/MatchLifecycleTest.cpp`, `test/game_script/GameViewIconsTest.cpp`, `harness/MatchPersistenceTest.cpp`, `harness/NearbyMatchTest.cpp` (the endings suite; `AScriptErrorInApplyShowsTheErrorViewOnTheMoverAndShipsNoState` and `AScriptErrorInTheAdoptedOverEventShowsTheErrorViewOnThatDeviceOnly`, both nearby tests of entry 9, now expect the peer's PeerGone); this plan. `GameVM.*` was not needed.

**Review breakdown:** 26 findings: high 0, medium 0, low 23, false 3. Patched: Back from PeerGone and Error waits for no acks; a counted round end reaches Over before the end is read; `endReason` read for PeerGone only; Back-from-Error, silence-limit, and renamed-test tightening; comment and doc corrections (the "end beats the queue" and "the VM ships it before the ABORT" claims). Deferred: a nearby Leave or error with an abandoned VM is untested; the radio and the sleep lock stay on in PeerGone and Error until Back. Rejected with reasons in the log: the `setWritable` refactor (asked for), `fail()` ungated (it is gated), string-width and wording, no feedback during the flush, Starting/Result/HandOff at activity level, the plan-edit finding, the descriptive audit.

**Verification performed** (tree at the commit's code; the commit adds only this plan to the tree the figures were taken on):
- Host: full `ctest` 2,456 of 2,456, 20 times in a row (`full1.log` to `full20.log`, 0 failures; the last run of the patched tree), and `ctest -R NearbyMatchTest.LeaveWithAStateUnacked --repeat until-fail:200` passed. Eleven mutants (Design Note 8) and two review mutants (always-flush, gate removed), each red alone.
- Flake record: `LeaveWithAStateUnackedFlushesForTheLimitAndSendsTheAbortsAnyway` failed once (`waited=0`) in about 60 stressed runs of the first implementation. Cause: it waited for `unacked >= 1`, which an ack in flight for the first STATE satisfies; the flush then found nothing to wait for if the move was not yet shipped. Fixed in the test (it also waits for the move's `state sent` line); after the fix the bar above passed (200 repeats, 20 full runs, the full runs overlapping the firmware builds).
- Fast: `scripts/check_layers.py` passed (773 edges), `scripts/check_upstream_touches.py` PASS, the fork script tests pass, `./bin/clang-format-fix` twice with the second changing nothing, nothing outside these paths.
- Firmware, once, after the review and its patches, under the build lock with the shared cache: `check_flash_budget.py build on` (`pio run -e x4pro`), `build off`, `compare`, and `objects` exit 0 ("No problems", 61 game objects, largest mutable static 4 B); `pio run -e default` SUCCESS; `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` for `default` and for `-e x4pro`: "No defects found"; `sim.sh build x4pro` SUCCESS.
- No simulator screenshots: nothing opens a nearby match from a screen until entries 10 and 16.

**Cost, like for like** (x4pro games on minus off, `check_flash_budget.py`, an incremental tree in this worktree): baseline `9fe03669` +272,880 B flash, +784 B static RAM; this tree games on 6,229,872 B, off 5,955,808 B, **+274,064 B flash, +784 B static RAM**: a delta of **+1,184 B flash, +0 B static RAM** (the four strings add 400 B to both builds and cancel in the difference). The epic total over its base (+258,048 B / +784 B) in this worktree is **+16,016 B flash, +0 B static RAM**. With entry 5's measured driver (+11,456 B / +80 B) added: **+27,472 B / +80 B** against 29,984 B / 160 B, so **2,512 B flash and 80 B static RAM** are left for entries 10 and 11 together. No new mutable static or IRAM.

**Residual risks:**
- Everything ran on host doubles (one clock, one process, a `FakeLink`); the flush bound and the 10 s silence are fake milliseconds, estimates until a device run (entries 11 and 14).
- Design Note 1's reading (Error is outside PeerGone's sources) is the orchestrator's to confirm with the owner; flipping it is one `if` and the table rows.
- A device that sits on the player-left or error view stays awake and keeps the radio on until Back (deferred).
- A `STATE` still queued when the end is read is not adopted: a peer that wins and leaves at once can show "player left" instead of its last board unless the VM had adopted the `over` first.
- Back from PeerGone and Error still sends three `ABORT(left)` to a peer already gone (AD-20), without a wait.
- `ARCHITECTURE-SPINE.md` AD-21's "implements every state but Lobby and PeerGone" is outside this entry's `touches`; the orchestrator updates it.
