---
title: 'A nearby round in GameMatchActivity'
type: 'feature'
ticket: '9'
created: '2026-10-12'
baseline_revision: 'fed11cfc4d53ee4f4677904215723eb75c00bdaa'
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
      No test abandons a stuck VM in a nearby match, so the destructor's unfreed release of the NearbySession (beside store.leak()) is unpinned.
    evidence: |-
      GameMatchTest's abandon tests are solo matches; deleting the release line leaves every host test green while a leaked VM task would call adoptNext and ship on a freed session. Closing it means extending the stuck-VM rig (SPIN_GAME) to take a NearbySession.
    location: >-
      src/activities/games/GameMatchActivity.cpp (~GameMatchActivity)
    severity: low
---

<intent-contract>

## Intent

**Problem:** Entries 7 and 8 built everything a nearby match needs from the link (`Session::adopt`, the pulled shipment, `NearbySession` with its `GameLink` task), but no match runs on it: `GameMatchActivity` and `GameVM` know only solo and pass rosters, so a device cannot adopt its peer's `STATE`, hand its own moves to the link, wait for seat 1, or leave Over on the host's Play again (R4, R6's end-of-round part).

**Approach:** `GameMatchActivity` takes the `NearbySession` by move and passes a pointer to `GameVM`, whose task, between two `MatchRounds` calls, hands a pending shipment to the link and adopts one queued `STATE`, drawing the device's own seat; the activity shows "waiting for the host" until seat 1's first `STATE`, offers Play again only on seat 1's device, and saves nothing. A `nearby` fixture (two seats, a rejected move, a script error, a `ch.store` write in `apply`) lets two activities over `FakeLink` play it in the harness.

## Boundaries & Constraints

**Always:** The VM task alone calls `Session`, `NearbySession::adoptNext`, and `NearbySession::ship` (the three handoffs, entry 8). Nothing outside `Session` tracks whose turn it is (AD-11); the activity gates on the VM's counts only. A device that is not seat 1 never calls `Session::start()` (entry 7, C9) and Play again waits until `!shipmentPending()` (C6). A refusal by `adopt` changes nothing and is only logged. Solo and pass behave exactly as today: every new branch is behind `nearby != nullptr` or `hasState()`, and every existing test passes unchanged. `-fno-exceptions`: no bare `new`; locals under 256 B; text through `tr(STR_GAMES_*)`, new keys in `english.yaml` only. A double names the device behaviour it stands in for and a test pins that they agree.

**Never:** any `lib/GameCore/**` change (a `GameCore` change returns to entry 7's scope: stop), `src/games/NearbySession.*`, `GameLink*`, `EspNowLink.*`, `GameModeActivity.*`, `games/**`, `test/game_script/first_party/**`, any upstream file but `english.yaml` (STR_GAMES_* only), `freeink-sdk`. No `resume.bin` read, write, or delete (R9, AD-17). No `PeerGone`, `ABORT`, `leave()`, `takeEnd`, or script-error report to the peer (entry 15's). No lobby, no `NEARBY_BUILT`, no mode plumbing (entry 10). No second round driver in the VM.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Waiting for the host | seat 2's match entered, seat 1's VM not started | the screen says "Waiting for the host"; `roundsStarted` 0; a tap is dropped and never reaches `input`; no frame | none |
| Seat 1 starts | seat 1's VM runs `setup`, draws seat 1, ships `STATE` ver 1 | seat 2 adopts it (`RoundStarted`), draws its own seat 2, the canvas replaces the waiting screen | none |
| A round to over | seats alternate moves, each applied on its mover's device only | both devices end with the same board, one `over` event each, both in Over | none |
| Rejected move | the mover taps a taken square | `input` gets `rejected` on that device; no shipment, no `STATE` on the link; the turn stays | none |
| Tap out of turn | a device taps while the peer holds the turn | `input` runs, the returned move is discarded; nothing shipped | none |
| `ch.store` in `apply` | a move is applied | the mover's `store.bin` is written; the other device's is not | none |
| Play again | seat 1 chooses it in Over | both start the next round; seat 2 leaves Over by itself; seat 2's Over menu had only Leave under "Waiting for the host" | none |
| Seat 2's move ends the round | seat 2 plays the last move | its `over` `STATE` is shipped; seat 1 reaches Over once | none |
| Script error in `apply` | the mover taps the fixture's boom square | the mover shows the error view; no `STATE` is shipped | peer is told by entry 15 |
| Persistence | any nearby match, Leave, forced exit | no `resume.bin` written or deleted; a pass save beside it is byte-identical | none |
| Overlay open | the loop stops polling | queued `STATE`s wait (R5); the link keeps acking | adopts on the loop's return |

</intent-contract>

## Code Map

- `lib/GameScript/MatchRounds.{h,cpp}` -- the one round driver (`start`, `restart`, `step`, `draw`, `drawShown`, `countRoundEnd`, `beginRound`'s `roundOver`/`firstFramePending`): add `attach(Session&)` (a device with no state yet), `adopted(Adoption)` (draw after an adopt), and a no-state guard at the top of `step`. Keep `step`'s stale-timer, late-timer, no-seat-timer, and `NO_SEAT` early returns as they are.
- `src/games/GameVM.{h,cpp}` -- `create` (add a trailing `NearbySession* nearby = nullptr`), `run()` (first round; the `while` loop's top; the Play again take), `postInput`/`pollTimer` (pattern for the new `pollNearby`), `logRound`, `publishCommitted`, `noteSeatDrawn`. The `handOff` machine is untouched.
- `src/games/NearbySession.h` (entry 8, read only) -- `matchBegun`, `beginMatch`, `roster`, `localSeat`, `adoptNext(Session&)`, `ship(Shipment)`, `counters().queueSize`. Strictly the VM task for `adoptNext`/`ship`; `counters` is any task.
- `lib/GameCore/Session.h` (entry 7, read only) -- `hasState`, `adopt`/`Adoption`, `shipmentPending`/`pendingShipment`/`shipped`, `pending`.
- `src/activities/games/GameMatchActivity.{h,cpp}` -- constructor, destructor (`slotLeaked`), `onEnter` (roster, `GameVM::create`), `handle` (the settings guard, `awaitRounds`, the `Playing` case's request), `startNextRound`, `loopPlaying`, `loopView` (the menu, `menu.count < 2`), `renderCanvas` (the gate-closed branch), `viewScreen`; new `preventAutoSleep`, `followsHost`, `renderWaiting`. Members: `nearby` goes before `vm`.
- `src/activities/games/GameMatchView.{h,cpp}` -- `Input`, `build`, `buildHandOffView` (plain centred text pattern); add the follower's menu (one function used by the loop and the view) and the waiting view.
- `src/games/GameSaveStore.cpp` (read only) -- `resumeOn()` is false for a `Mode::Nearby` roster and `deleteResume()` returns true then, so the existing `setRoster(roster)` already makes persistence a no-op; `MatchPersistence` needs no edit.
- `src/games/CanvasGate.h`, `lib/GameCore/MatchLifecycle.*` (read only) -- the solo machine and the gate counts the follower reuses (`Over --PlayAgain--> Playing`).
- `lib/I18n/translations/english.yaml` -- `STR_GAMES_WAITING_FOR_HOST`.
- `test/game_script/fixtures/nearby/{manifest.json,main.lua}` (new), `fixtures/README.md` (its row); `test/game_script/MatchRoundsTest.cpp` (adopt and attach cases); `test/game_script/harness/NearbyMatchTest.cpp`, `nearby_match.cmake` (new), `match.cmake`, `nearby_session.cmake`, `MatchSupport.h` (the two files of entry 8 move into `game_match_src`; a fixture-as-another-id helper). Reused read only: `NearbySessionSupport.h` (`Duo`, `fastSettings`, `waitFor`), `screen_stubs/*`, `GameMatchTest.cpp`'s `MatchTest` helpers (pattern, not shared: they are file-local).
- `docs/crosshatch/game-canvas.md` is not in this entry's `touches` (entry 15's): the Overlays wording stays as entry 7 left it.

## Tasks & Acceptance

**Execution:**
- [ ] `test/game_script/fixtures/nearby/manifest.json`, `main.lua`, `fixtures/README.md` -- the `nearby` fixture (noughts and crosses, seats 2..2, modes pass and nearby) -- Design Note 7
- [ ] `lib/GameScript/MatchRounds.{h,cpp}` -- `attach`, `adopted`, the no-state guard -- Design Note 2
- [ ] `src/games/GameVM.{h,cpp}` -- `create(..., nearby)`, the follower's first round, `nearbyStep`, the Play again deferral, `pollNearby` -- Design Notes 1, 3
- [ ] `src/activities/games/GameMatchActivity.{h,cpp}` -- constructor takes the session; follower flow, waiting screen, Over menu, polling, `preventAutoSleep`, teardown order -- Design Notes 1, 4, 5
- [ ] `src/activities/games/GameMatchView.{h,cpp}`, `english.yaml` -- the waiting view, the follower's Over menu and its line -- Design Note 5
- [ ] `test/game_script/harness/match.cmake`, `nearby_session.cmake`, `nearby_match.cmake`, `MatchSupport.h`, `NearbyMatchTest.cpp`; `test/game_script/MatchRoundsTest.cpp` -- Design Note 8's tests, a mutant per rule, each red
- [ ] Measure the cost like for like -- Design Note 9

**Acceptance Criteria:**
- Given two `GameMatchActivity` instances over `FakeLink` and the `nearby` fixture, when they play a round to over, then each move is applied only on its mover's device, both end on the same board and one `over` event each, and both are in Over.
- Given the follower entered before seat 1's VM started, when no `STATE` has arrived, then it shows "Waiting for the host", takes no tap, and draws no frame until the first `STATE`.
- Given a rejected move on either device, when it is tapped, then that device's `input` gets `rejected` and no frame or shipment leaves it.
- Given a finished round, when seat 1 chooses Play again, then the next round starts on both devices, and the follower's Over menu offered only Leave.
- Given any nearby match, when it plays, is left, or takes the forced exit, then no `resume.bin` is written, deleted, or changed, and the fixture's `ch.store` write exists on the mover's device only.
- Given every solo and pass test, when run, then each passes unchanged.

## Implementation Notes

- Environment (written by the build session for the implementer): work only in the git worktree `/home/user/wt/epic-play-nearby-lane-a` (branch `epic-play-nearby-lane-a`), never in `/home/user/crosshatch-player`; use absolute paths under it. Host tests build in this worktree's `build/test` with AGENTS.md's commands under `flock /tmp/crosshatch-hosttest.lock` (configure once; a full rebuild is slow). Scratch files go under `/tmp/claude-0/-home-user-crosshatch-player/f3b56526-12a6-5481-9360-b3275a33119e/scratchpad/6.9/`. Never run `pio`, `pio check`, or `sim.sh` (the build session runs them after the review), and do not `git commit`. Read AGENTS.md first. Order that keeps each step testable: the fixture, `MatchRounds` with its `MatchRoundsTest` cases, the CMake moves (`match.cmake`, `nearby_session.cmake`) with `NearbySessionHarnessTest` still green, `GameVM`, the activity and view, `english.yaml`, then `NearbyMatchTest`. `git add` every new file before the last `./bin/clang-format-fix`, which formats only tracked files. The nearby VM's real-thread tests use `Duo` and `nearby::waitFor` from `NearbySessionSupport.h`, which move the fake clock; the match tests' own `waitFor` does not.
- 2026-10-12, implemented by one subagent as the plan reads, then adjusted in the main session. Host evidence: full `ctest` 2,429 of 2,429 (the existing solo and pass tests unchanged; 5 new `NearbyRoundsTest` cases in `MatchRoundsTest.cpp`, 18 new `NearbyMatchTest` cases); `ctest -R 'NearbyMatch|NearbyRounds|MatchRounds' --repeat until-fail:20` passed before the change below; ten mutants (no `hasState` guard; `adopted` without arming the first frame; `RoundStarted` keeping `roundOver`; the follower calling `start`; `shipped()` before `ship` answers; Play again taken while a shipment is pending; no `pollNearby`; `awaitRounds(+1)` for the follower; the follower's menu keeping Play again; Resume honoured with a session) each red alone. Main session: the Play-again-deferral test's host task step went from 300 to 1,000 fake ms with a silence limit of 20,000 (a 300 ms step left about 150 real ms of margin under load); it passes once (9 s) and its 20 repeats are not yet run.
- Touches deviation, resolved (epic Notes, "Scope (orchestrator, 2026-10-12, entry 9's stop)", commit `e7fd6966`: entry 9's `touches` gained `test/game_core/ManifestTest.cpp`, `EveryFixtureManifestIsListed`'s mode check only, for the `nearby` fixture; the run was reset to `in-review` and resumes at the review). Why the run HALTed: `test/game_core/ManifestTest.cpp`'s `EveryFixtureManifestIsListed` walks every folder of `test/game_script/fixtures/` and requires a solo mode of each but the hidden pass fixtures, so the `nearby` fixture (seats 2..2, modes pass and nearby, as the entry says) fails it. The subagent added a five-line `nearby` branch (no solo, pass offered). That file is outside the entry's `touches` and not in its `stays_out`; the other way out, a solo mode in the fixture's manifest, would change the fixture the entry describes. Both are the orchestrator's call; nothing else is open.
- `GameMatchActivity::~GameMatchActivity` releases `nearby` unfreed when `slotLeaked` (`(void)nearby.release()`); `pio check` has not run on it. `beginMatch()` failing shows `STR_GAMES_START_FAILED` with the detail `STR_GAMES_NOT_LOADED` (no string added for it).
- Baseline firmware figures at `fed11cfc` (x4pro games on minus off, measured in this worktree, `check_flash_budget.py build on`, `build off`, `compare`): +258,672 B flash, +784 B static RAM (games on 6,213,984 B, off 5,955,312 B).

## Plan Change Log

## Review Triage Log

### 2026-10-12 — Review pass
- lens method: four context-free subagents (blind-hunter, edge-case-hunter, verification-gap, intent-alignment), started in one message in the foreground and awaited; the diff given to them (`review-diff.patch`, 108 kB, `lib`, `src`, and `test` since `fed11cfc`) leaves out this plan, which the edge-case lens read as its claims file. The implementation subagent was re-engaged (by id) for the patches; checked by the host suites (2,432 of 2,432; the 49 nearby and match-rounds tests 20 times) and by a mutant for each added test.
- verdicts: 24 findings (blind 12, edge-case 6, verification-gap 3 gaps + 2 others, intent-alignment 1 descriptive row) — high 0, medium 0, low 20, false 4, maybe-false 0; 7 patches (all low, six of them tests), 1 deferred, the rest rejected. The Blind and Edge numbers are the order each lens listed them.
- findings:
  - Blind 1 `[false]` `[reject]` a hidden manifest that also offers nearby would start a round on the follower — `GameVM::create`'s `handOff` is `lifecycle.hiddenPass()`, which `isHiddenPass` makes true only for a `Pass` roster, so a nearby roster never takes the `handOff` branch.
  - Blind 2 `[low]` `[reject]` a follower cannot tell a host that left from one that has not started — peer loss and `ABORT` are entry 15's by the entry's own words ("Endings are entry 15's"); the fix is `PeerGone`.
  - Blind 3 / VG gap 2 `[low]` `[defer]` the leaked-VM release of `nearby` is untested and leaves the radio up — real as a gap (no nearby test abandons a stuck VM); the line is `store.leak()`'s twin and the radio's teardown on a leaked task is entry 8's documented leak path. Deferred below.
  - Blind 4 `[false]` `[reject]` an overlay stops the local mover's shipment — a shipment `ship()` has staged is sent by the `GameLink` task, which an overlay does not stop; only `shipped()`'s bookkeeping waits for the loop's poll, and the peer is not held up.
  - Blind 5 `[low]` `[patch]` the `Playing` case's compound negated condition is hard to read — now `needsRenderAskOnEnteringPlay(event)`, same behaviour, no flash.
  - Blind 6 `[low]` `[reject]` seat 1 as the host is a literal in `GameMatchActivity::followsHost`, `GameVM::followsHost`, and `Session::adopt` — seat 1 starting a round is fixed by the spine (AD-8, AD-11), the two copies are one line beside their own roster, and a shared constant is a public symbol in `GameCore` (frozen here).
  - Blind 7 `[low]` `[reject]` `adopted` has no test for `Busy`, `BadSnapshot`, `OutOfTurn`, `BadStatus` — the switch returns Ok for every refusal in one `break`; `Stale` and `NotPeer` are tested through it, and the refusals themselves are `SessionAdoptTest`'s (entry 7).
  - Blind 8 / VG other 2 `[low]` `[reject]` the Play-again deferral test depends on a fake-clock ratio — its window is counted in `nearby::waitFor` polls (2 fake ms each), not wall time, with a 1,000 fake ms step against a few dozen polls; widened from 300 in this pass, and passed 50 repeats under a parallel firmware build (`flake.log`). The ratio is an estimate until a device run, as the test says.
  - Blind 9 `[low]` `[reject]` `TheTwoDevicesShareNoGameFiles` tests the helper — it is the plan's double-agreement pin (the two ids stand for two cards, Design Note 8), named for the property the suites rely on.
  - Blind 10 `[low]` `[patch]` the `ManifestTest` comment says more than its assertions — reworded to say what is asserted (no solo form, pass startable); the branch stays the mode check the Scope note allows.
  - Blind 11 `[low]` `[patch]` the README lists `nearby/` out of order, on an overlong line — moved after `loop/` and re-wrapped.
  - Blind 12 `[low]` `[reject]` nothing constructs a nearby match — entry 10's lobby does (AD-18); the constructor parameter is defaulted, and the host suites are the only caller by design until then.
  - Edge 1 `[low]` `[reject]` Play again is ignored while a shipment never goes — `ship()` refuses only an empty or over-limit snapshot (`Session` bounds it) or a lost peer, whose ending is entry 15's.
  - Edge 2 `[low]` `[reject]` `step` dereferences a null `session` for a stale timer before `begin` — the VM attaches or starts before its loop, and the old code would have dereferenced for any live event.
  - Edge 3 `[false]` `[reject]` a nearby host skips the settings-unread guard — the match never had a guard for a New game (`GameModeActivity::startMatch` refuses it before the match is built); the guard here is for Play again only, and the new test pins the follower's exemption.
  - Edge 4 `[low]` `[patch]` the installer test's fixture list lacks `nearby` though the README lists it — `{"nearby", MODE_PASS}` added.
  - Edge 5 `[low]` `[patch]` `playToOver` asserts inside a void helper — every call is `ASSERT_NO_FATAL_FAILURE(playToOver(...))`.
  - Edge 6 `[low]` `[reject]` the loop wakes the VM every pass while a shipment never goes — one notify per loop pass until `PeerGone` (entry 15) ends the match; no state grows.
  - VG gap 1 `[low]` `[patch]` the follower's settings exemption is unpinned — `TheGuestNeedsNoSettingsToFollowTheHostsPlayAgain`, red with `!followsHost() &&` removed (times out).
  - VG gap 3 `[low]` `[patch]` `adopted(RoundStarted)`'s `timer.cancel()` is unobserved — `AdoptingTheHostsNextRoundCancelsTheTimerTheFollowerArmedInTheLastOne`, red with the cancel removed.
  - VG other 1 `[low]` `[patch]` the VM's handling of a failing adopted `over` is untested — `AScriptErrorInTheAdoptedOverEventShowsTheErrorViewOnThatDeviceOnly`, red when `nearbyStep` ignores the outcome.
  - Intent-alignment (descriptive) `[false]` `[reject]` the readings: the activity-level one is implemented, tested through harness stand-ins in one process (two ids for two cards), the snapshot equality read from the board each `over` line logs, no production caller until entry 10; no defect, the verify names the harness.


## Design Notes

**Sources that settle the choices.** Epic R4 (mover applies, `STATE` shipped, adopt rules, seat 1 runs `setup` and Play again, `ch.store` on the mover, nothing saved), R5 (adoption waits for `loop()` and the VM), R6 (end-of-round menu "waiting for host" and Leave), R9 (never `resume.bin`), R10's one-local-seat rule (`seatShown`), `preventAutoSleep` (R6, AD-18); spine AD-8, AD-9, AD-11, AD-17 (2026-10-12 amendments), AD-20 (the match owns the `NearbySession`), AD-21 (Over, "waiting for host" is a view inside the match); entry 7's plan Design Notes 1 to 3 and its checkpoint Decision (C6: the owner of the `Session` defers Play again until `!shipmentPending()`; C9: the device without seat 1 never calls `start()` and uses `hasState()` and `RoundStarted`); entry 8's plan Design Notes 1 to 3 (the three handoffs; `adoptNext` does nothing while `session.pending()`, `ship` is false until the link took the snapshot, the match calls `shipped()` on true only) and its deferred list (the clock outlives a leaked task; `game_nearby_src` links `game_match_src`, so the two sources move into the match library); `deferred-work.md` `## 6.8`, `## e6pre-1`.

### 1. Who owns what, and the constructor

`GameMatchActivity(renderer, mappedInput, manifest, roster, start, settings, std::unique_ptr<NearbySession> nearbySession = nullptr)`: the last parameter is new and defaulted, so `GameModeActivity` (entry 10's) compiles unchanged. The activity member `nearby` is declared before `vm`, so it is destroyed after the VM task is gone (the VM task calls `adoptNext` and `ship`); `GameVM` holds a non-owning pointer. When `slotLeaked` (an abandoned VM task may still call the session) the destructor releases `nearby` unfreed beside `store.leak()`, as entry 8's header requires for a task that will not stop. The roster of a nearby match is the session's (`beginMatch()`, which is idempotent, then `roster()`), whatever the caller passed; a session whose `beginMatch()` is false ends in the error view. `start` is `New` for a nearby match (it saves nothing, so there is nothing to resume).

"Seat 1's device" is `roster.isLocal(1)`: the **host** runs `rounds.start` (setup, draw, ships `STATE` ver 1) and offers Play again; the other device **follows the host** (`followsHost()`): `rounds.attach`, no `start()`, no Play again. The clock and random handed to the session are the caller's (entry 10's lobby keeps them process-long: `GameClock` is stateless); this entry only forwards the deferred note.

### 2. `MatchRounds` stays the one round driver (the entry's `unknown`)

The VM adopts between `MatchRounds` calls by calling `NearbySession::adoptNext` at the top of its pass; the draw, the started and ended counts, and `lastLocal` stay in `MatchRounds`:
- `attach(Session&)`: sets the session and arms `firstFramePending` for the round to come; runs nothing and draws nothing.
- `adopted(Adoption)`: `Adopted` draws the shown seat (an over status counts the round's end in `draw`, once); `RoundStarted` first cancels the pending timer (as `beginAgain` does), clears `roundOver`, arms `firstFramePending`, then draws, so `roundsStarted` moves after the first frame is published, exactly as after `start`; every refusal returns Ok and changes nothing.
- `step` returns Ok at once while `!session->hasState()`: a follower before its first `STATE` reads nothing and counts no round (without it `draw` would publish an empty first frame and count a started round).
The guards in `step` that must survive (history: `e418a7f1`, `c2954f7f`, `9f677a17`) are untouched: a stale timer returns before anything, a late timer and a no-local-seat timer are dropped with no draw, a `NO_SEAT` turn reads no input. The new guard comes first and returns the same Ok.

### 3. The VM pass, the wake, and Play again

`GameVM::run`, for a nearby VM, adds at the top of each pass (after `endedBefore` and `startedBefore` are read) `nearbyStep`: (1) if `session.shipmentPending()`, `pendingShipment`, then `nearby->ship(shipment)` and `session.shipped()` on true only; `shipOutstanding` mirrors `shipmentPending()`; (2) `nearby->adoptNext(session)`: nothing taken ends the step; a taken `STATE` with a non-Ok outcome (the `over` input's ScriptError or Cancelled) ends the VM as any step does; Ok runs `rounds.adopted(result)`, then `noteSeatDrawn`, `publishCommitted`, `logRound`, and the pass starts again (one `STATE` per pass, entry 7 DN2). The seat-1 branch is `rounds.start` as today; the follower's is `rounds.attach` with no `logRound` start line (there is no round yet: the call passes `session.hasState()`).
**Play again waits:** `rounds.takePlayAgain()` is skipped while a nearby `session.shipmentPending()`, and the request stays set (`takePlayAgain` is the only consumer), so an `over` `STATE` the link has not taken is never replaced by the next round's (entry 7 C6; `NearbySession::ship` refuses a different version over a staged `over`).
**The wake:** the VM blocks in `ulTaskNotifyTake(portMAX_DELAY)` when its queue is empty, and the simulator's FreeRTOS shim ignores a timeout (entry 8, DN3), so the loop task wakes it: `GameVM::pollNearby()` (loop task, called where `pollTimer()` is, and in the Paused and Over passes) calls `notifyTask()` while `shipOutstanding` or `nearby->counters().queueSize > 0`. An overlay stops the loop, so adoption waits behind it and the link does not (R5, entry 7's `game-canvas.md` wording). `Session::adopt` clears a pending shipment older than the adopted state (entry 7 DN1).
The mailbox: `publishCommitted` stays unconditional (`GameSaveStore::flushResume` returns at once for a nearby roster, so the mailbox is never drained and no file is touched); calling it is the smaller diff than a nearby fork of every call site.

### 4. The follower's lifecycle in the activity (no new `MatchState`; `GameCore` is frozen)

`MatchLifecycle` is the solo machine. The follower reuses it: Starting --Started--> Playing (the waiting screen, the gate awaiting round 1, taps dropped by `CanvasGate::wait().round`), Playing --RoundOver--> Over, and **Over --PlayAgain--> Playing taken by the loop when the host's next round has started**: `loopView` in Over sees `vm->roundsStarted() > vm->roundsEnded()` (a started round that has not ended; robust to the VM having adopted the `over` and the next `STATE` before the loop ran) and calls `handle(PlayAgain)`. For the follower, `handle(PlayAgain)` skips the settings guard (it runs no setup), awaits the current `roundsStarted()` (not +1), and `startNextRound` asks the VM for nothing (`playAgain()` is the host's). The first frame is already published, so the loop's ordinary frame test asks for its render. `handle(Started)` asks for a render when the follower has no frame to ask for. `loopPlaying` and `loopView` (not Error) call `vm->pollNearby()`.
**Guards in the functions edited (history read, nothing moved or rewritten):** `handle`'s settings guard (`PlayAgain` from Over with settings unread ends in the error view) stays for the host; `awaitRounds` before `shown`, `awaitSeat` before `shown`, the order `closeRouting` / `shown.store` stay; `startNextRound`'s `clearResumeBackoff` and `shownFrame = frameGen()` before `playAgain()` stay for the host; `loopView`'s `vmHealthy` first, Back, the gap redraw, `menu.count < 2` (the error view's one control) become `state == Error || menu.count == 0` so Confirm chooses the follower's single Leave.

### 5. Views: waiting for the host, and the follower's end-of-round menu

`GameMatchView::menuFor(state, followsHost)` is the one definition of a menu the loop (`choose`) and the build share: the follower's Over menu is `{Leave}`; every other case is `MatchLifecycle::menuFor`. `Input` gains `followsHost` and `waiting`. Over for the follower adds the dialog's message "Waiting for the host" (centred, as the Play-again gap line is). The waiting screen (`renderWaiting`, called from `renderCanvas`'s gate-closed branch for a follower with `roundsStarted() == 0`) is a cleared screen with the same string centred, drawn once in full (a repeat, such as the light panel closing, in fast), no hints: Back opens the pause menu as in play. One string, `STR_GAMES_WAITING_FOR_HOST` "Waiting for the host", serves both (R6's and R8's wording).
The `## e5-xr` `renderedFrame` item (store the number `drawFront` reports) does not ride this entry: the change to `renderCanvas` is in the gate-closed branch and does not touch the frame path the item concerns.

### 6. Persistence

`store.saves().setRoster(roster)` is the nearby roster, so `GameSaveStore::resumeOn()` is false: `flushResume` takes nothing, `deleteResume` returns true, and `MatchPersistence::onOver`, the retry, and the forced exit's write are no-ops. A pass `resume.bin` beside it is neither read (no `Start::Resume`) nor changed; the test places one and compares its bytes. `store.bin` is the `ch.store` file: an `apply` runs on its mover only, so only the mover's slot is dirty.

### 7. The `nearby` fixture

Noughts and crosses for two seats (`seats` 2..2, modes `pass` and `nearby`), the board and log lines of `pass-open` so the two read alike: a tap on a free cell is the move; a taken cell is **rejected** in `apply` ("That square is taken"); a tap on a **boom** square under the board returns `{boom=true}` and `apply` raises a **script error**; every accepted `apply` writes `ch.store.set({moves = n, last = seat})`; each call logs its seat (`apply seat S cell C`, `rejected for seat S`, `over for seat S board B`). No timer. The game reads `ctx.mode` only to log it. It lives in `test/game_script/fixtures/` (never `games/`) and packs (`manifest.json` and `main.lua`).

### 8. Tests, doubles, and mutants

- `MatchRoundsTest` (single `LuaGame`, two `Session`s over it, as one device each, hand-fed shipments): a follower's `step` before its first `STATE` publishes nothing and counts nothing; `attach` then `adopted(RoundStarted)` draws its own seat and counts the round started; a move adopted draws; an adopted `over` counts the end once; a refused adopt draws nothing; an `over` followed by the next round's `STATE` counts end then start; `restart` after a deferred take still counts.
- `NearbyMatchTest` (new suite, `Device` = a renderer, a scripted input, an activity, over a `Duo` of two `NearbySession`s on a `FakeLinkNet` with real `GameLink` tasks): the entry's `verify` bullet by bullet (the round to over with identical boards and one `over` each; apply only on the mover's device; the rejection on both devices with the shipped-STATE count unchanged; waiting until the first `STATE` with a dropped tap and the waiting text; Play again with the follower's menu; no `resume.bin` and a kept pass save; `store.bin` on the mover only; solo/pass untouched is the existing suites), plus the follower's `Over` race (both `STATE`s adopted before the loop looked), the Play again deferral, the script error on the mover, the overlay (no loop pass: nothing adopted, then adopted), `preventAutoSleep`, and the nearby VM's wake.
- **Doubles.** The two "devices" share one `fakesd` card and one `fakelog`; they are told apart by the game id they run (`nearby-a`, `nearby-b`: the fixture installed under both ids by `installFixtureAs`), which stands in for each device having its own SD card and its own log, so `ch.log` lines and `/.games-data/<id>/store.bin` are per device. More permissive than two devices: one clock (`fakertos`), one process, a shared `activityManager` double that records both activities' asks, and a `FakeLink` (its header lists its faults); the loop is the test thread alternating `frame()` on each device, not two cores. `Duo` (entry 8) is the device pair; `GameClock` and `FakeRtosClock` agreement is entry 8's pinned test.
- Mutants, each applied alone and red: no `hasState` guard in `step`; `adopted` without arming `firstFramePending`; `RoundStarted` without clearing `roundOver`; the follower calling `rounds.start`; `shipped()` called before `ship` answered; Play again taken while a shipment is pending; no `pollNearby` (the VM never wakes); `awaitRounds(+1)` for the follower; the follower's menu keeping Play again; `Resume` honoured with a session.

### 9. Measurement

The first entry whose firmware references the nearby session from the match: `GameMatchActivity` and `GameVM` now call `NearbySession`, so `NearbySession`, `GameLink`, `GameLinkStore`, `GameLinkLog`, the sink gate, and `Protocol`, `ReliableLink`, `Lobby`, `HostLobby`, `GuestLobby`, `StateQueue`, `MatchSink`, `Session::adopt` and the shipment become reachable (the linker kept none of them before). `EspNowLink` stays unreferenced (entry 11 wires it; its measured +11,456 B / +80 B lands then). Measured like for like in this worktree: the baseline `fed11cfc` (before any edit, `build on`, `build off`, `compare`, under the build lock with the shared cache): **+258,672 B flash, +784 B static RAM** (games on 6,213,984 B, off 5,955,312 B); then the same commands on the commit. The delta is the difference of the two; the orchestrator re-measures the combined tree.

## Verification

**Commands:**
- Host suites (Ninja `build/test`, AGENTS.md): `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test'`, then `ctest --test-dir build/test --output-on-failure -j` -- expected: every test passes, every existing test unchanged (`ctest -N` differs by the new tests only).
- `ctest --test-dir build/test -R 'NearbyMatch|MatchRounds' --repeat until-fail:20` -- expected: all pass (the harness threads are fake-clocked, so a failure is a defect).
- Mutants (Design Note 8) -- expected: each red against its test, then reverted.
- Fast: `python3 scripts/check_layers.py`, `python3 scripts/check_upstream_touches.py`, `./bin/clang-format-fix` twice (`git add` the new files first), `git status` unchanged by the second run.
- Firmware, once, after the review and its patches, under `flock /tmp/crosshatch-build.lock` with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`: `pio run -e x4pro`, `pio run -e default`, `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` and the same with `-e x4pro`, `scripts/check_flash_budget.py` `build on`, `build off`, `compare`, `objects`, and `.claude/skills/run-crosshatch-player/sim.sh build x4pro` -- expected: success; the delta over the baseline in Design Note 9, with its method.

**Manual checks (if no CLI):**
- Read `GameVM.cpp`: only the VM task calls `adoptNext` and `ship`; read `GameMatchActivity.cpp`: no `RenderLock` is taken in the destructor (12cc816), `nearby` is declared before `vm`.

## Auto Run Result

Status: `built`. Reviewed by four lenses (`lenses_ran`); 7 low patches applied (six of them tests), 1 low item deferred, nothing high or medium. Follow-up review recommended: `false` (no high patched, no two mediums).

**Summary:** `GameMatchActivity` takes a `NearbySession` by move (defaulted last constructor parameter) and gives `GameVM` a non-owning pointer. The VM runs the match for its one local seat as pass does; at the top of each pass it hands a pending shipment to the session (`shipped()` only once `ship` answered true) and adopts one queued `STATE` (`adoptNext`), and `MatchRounds` (still the one round driver) draws it (`attach`, `adopted`, and a no-state guard in `step`). Seat 1's device runs `setup` and Play again, held while a shipment is pending; the other device follows the host: "Waiting for the host" and no taps until the first `STATE`, an end-of-round menu of Leave under the same line, and it leaves Over by itself when the host's next round has started. The loop task wakes the VM (`pollNearby`). `preventAutoSleep` is set; `GameSaveStore` already saves nothing for a nearby roster, so no `resume.bin` is touched; a `nearby` fixture (rejected move, script error, `ch.store` write in `apply`) and an 18+ case host suite over two activities, `Duo` and `FakeLink` cover it.

**Files:** `lib/GameScript/MatchRounds.{h,cpp}`; `src/games/GameVM.{h,cpp}`; `src/activities/games/GameMatchActivity.{h,cpp}`, `GameMatchView.{h,cpp}`; `lib/I18n/translations/english.yaml` (`STR_GAMES_WAITING_FOR_HOST`); `test/game_script/fixtures/nearby/{manifest.json,main.lua}`, `fixtures/README.md`; `test/game_script/MatchRoundsTest.cpp` (`NearbyRoundsTest`); `test/game_script/harness/NearbyMatchTest.cpp`, `nearby_match.cmake` (new), `match.cmake` and `nearby_session.cmake` (`GameLink.cpp` and `NearbySession.cpp` move into `game_match_src`; `game_nearby_src` is gone, entry 8's deferred item), `nearby_session.sources.cmake` (comment), `MatchSupport.h` (`installFixtureAs`), `GamePackageInstallerTest.cpp` (the fixture list); `test/game_core/ManifestTest.cpp` (five-line `nearby` branch of the fixture mode check, in scope by the epic Notes' "Scope (orchestrator, 2026-10-12, entry 9's stop)", commit `e7fd6966`); this plan.

**Review breakdown:** 24 findings: high 0, medium 0, low 20, false 4. Patched: the named predicate in `handle()`, the `ManifestTest` comment, the README order, the installer test's fixture list, `ASSERT_NO_FATAL_FAILURE` around `playToOver`, and three tests (the follower's settings exemption, `RoundStarted` cancelling the follower's timer, a failing adopted `over`), each red against its mutant. Deferred: no nearby test abandons a stuck VM (the unfreed release of the session). Rejected, with reasons in the Review Triage Log: hidden-plus-nearby (cannot take the branch), host-gone and a never-taken shipment (entry 15's endings), the overlay's outbound half, the null-session `step`, the seat-1 literal, refusals of `adopted` (one `break`), the real-time ratio of the Play-again deferral test (poll-counted, widened to a 1,000 fake ms step, 50 repeats green), no production caller (entry 10's lobby).

**Verification performed** (the code is the commit's; the plan file is the only addition after it):
- Host: full `ctest` 2,432 of 2,432 (the existing solo, pass, and `NearbySessionHarnessTest` suites unchanged); `ctest -R 'NearbyMatch|NearbyRounds|MatchRounds' --repeat until-fail:20 -j4` passed (49 tests); `PlayAgainWaitsWhileTheOverStateHasNotGoneAndThenBothDevicesSeeBothRounds` 50 repeats passed under a parallel firmware build. Ten mutants as the implementation notes list, each red alone, and one more for each of the three added tests.
- Fast: `scripts/check_layers.py` passed (770 edges), `scripts/check_upstream_touches.py` PASS, the 14 `scripts/*_test.py` pass, `./bin/clang-format-fix` twice with the second changing nothing, nothing outside these paths.
- Firmware, once, under the build lock with the shared cache: `pio run -e x4pro` (via `check_flash_budget.py build on`), `build off`, `compare`, and `objects` exit 0 ("No problems", 61 game objects, largest mutable static 4 B); `pio run -e default` SUCCESS; `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` for `default` and for `-e x4pro`: "No defects found"; `sim.sh build x4pro` SUCCESS (after `sim.sh setup`; `platformio.local.ini` is gitignored).
- No simulator screenshots: nothing opens a nearby match from a screen until entry 10 (the lobby) and entry 16 (the UDP link).

**Cost, like for like** (x4pro games on minus off, `check_flash_budget.py`, an incremental tree in this worktree): baseline `fed11cfc` +258,672 B flash, +784 B static RAM; this tree games on 6,228,288 B, off 5,955,408 B, **+272,880 B flash, +784 B static RAM**: a delta of **+14,208 B flash, +0 B static RAM** (the new string is +96 B in both builds and cancels; the review patches added 32 B over the +14,176 B first measured). Code made reachable from the match: `NearbySession`, `GameLink`, `GameLinkStore`, `GameLinkLog`, `GameLinkSinkGate`, `Protocol`, `ReliableLink`, `Lobby`, `HostLobby`, `GuestLobby`, `StateQueue`, `MatchSink`, `Session::adopt` with the shipment; `EspNowLink` is not. The epic total over its base (+258,048 B / +784 B) in this worktree is **+14,832 B flash, +0 B static RAM** (about 624 B of it entries 1 to 8 and this worktree's longer `__FILE__` paths, entry 8's report). With entry 5's measured driver (+11,456 B / +80 B) added: +26,288 B / +80 B against 29,984 B / 160 B, 3,696 B and 80 B left for entries 15, 10, and 11. No new mutable static or IRAM.

**Residual risks:**
- Everything ran on host doubles: two activities in one process, one fake card told apart by two game ids, a `FakeLink`, a fake clock; no radio, no two cores, no real panel. The device runs (entries 11 and 14) are the check for timing and for the 4 KB `GameLink` stack beside a real VM.
- The Play-again deferral test relies on the fake clock's poll counting (an estimate until a device run).
- A follower whose host leaves or goes silent shows "Waiting for the host" until it leaves by Back: entry 15 adds `PeerGone`.
- The deferred item: the leaked-VM path of a nearby match is untested.
