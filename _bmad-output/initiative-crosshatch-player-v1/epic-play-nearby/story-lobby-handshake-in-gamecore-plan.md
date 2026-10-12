---
title: 'Lobby handshake in GameCore'
type: 'feature'
ticket: '6'
created: '2026-10-12'
baseline_revision: '3a36b97a826946c41b4365fe5ed309ec850ed15b'
status: 'built'
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
      A newer peer's API level is taken as a preview, so a frozen older device refuses a frozen newer peer whose surface differs, against AD-19's mixed levels.
    evidence: |-
      versionVerdict treats peerApi > own api as a preview and then requires equal surface; the wire carries a level, not whether it is frozen. Unreachable in v1 (one level, not frozen). The fix is a frozen flag on the wire (a Protocol change) or an owner decision on the rule. Unverified until a second level exists.
    location: >-
      lib/GameCore/Lobby.cpp versionVerdict
    severity: medium (unverified)
  - summary: >-
      A departed guest can keep a seat on the host until the silence limit: a delayed copy of its JOIN arriving after its ABORT, or all three ABORT copies lost, leaves a seated ghost, and start() then sends ACCEPT to a gone device.
    evidence: |-
      Edge-case lens (EC3, claim): without an epoch the host cannot tell a delayed JOIN from a live one, and ABORT is unsequenced. Bounded by the 10 s silence limit; the rejoin tests wait 500 ms between Joined, cancel() and the new guest. The screens (entries 8 and 10) should offer Start only while the guest is live; a Start on a ghost leaves the real guest without an ADVERT.
    location: >-
      lib/GameCore/HostLobby.cpp onJoin, onAbort; Design Notes 7
    severity: medium
  - summary: >-
      JoinResult::LinkRefused and StartResult::LinkFailed (a lost peer, a peer table or allocation failure) have no test.
    evidence: |-
      refuseSends cannot reach them because ReliableLink::send returns true for a queued reliable frame; test/game_core has no allocation seam (same gap as deferred-work ## 6.4 item 2).
    location: >-
      lib/GameCore/GuestLobby.cpp join, lib/GameCore/HostLobby.cpp start
    severity: low
  - summary: >-
      The host's hand-off latches one end before attach (first wins), so with three or more seats a second guest's abort or silence in that window is never replayed.
    evidence: |-
      Edge-case lens (follow-up), confirmed by reading LobbyHandOff::latch. Only n >= 3 and only between Start and attach; what one guest's end does to a match is entry 8's rule. Unverified until entry 8 attaches.
    location: >-
      lib/GameCore/Lobby.cpp LobbyHandOff::latch
    severity: medium (unverified)
  - summary: >-
      A delayed duplicate ABORT(left) of a guest that has already rejoined removes its fresh seat and forgets the link, until the guest's silence limit.
    evidence: |-
      Edge-case lens (follow-up). Same root as the ghost-seat item (no epoch on the wire), the other direction; bounded by the 10 s silence limit; the rejoin tests wait 500 ms between a cancel and the new guest, longer than any hostile delay. The fix is a Protocol epoch, outside this entry.
    location: >-
      lib/GameCore/HostLobby.cpp onAbort; Design Notes 7
    severity: medium
  - summary: >-
      The guest's rule that a same-session ADVERT after Start ends the match is unreachable from a real HostLobby (a new lobby draws a new session, which the guest's link drops), so a host that left the match is found at the silence limit unless it cancelled.
    evidence: |-
      Edge-case lens (follow-up), confirmed: HostLobby stops advertising at Start and ReliableLink drops an ADVERT of another session once a session is set. The rule is implemented as the ticket's verify names it. A faster end would need the link to pass another session's ADVERT (a ReliableLink change) or an owner decision.
    location: >-
      lib/GameCore/GuestLobby.cpp onFrame (Advert, Started)
    severity: low
  - summary: >-
      In a match of three or more seats with different API levels a guest's ctx.api is the lower of its own and the host's, so two guests can differ from each other and from the host.
    evidence: |-
      Intent-alignment lens (follow-up); pinned as the expected result by ARosterApiIsTheLowestLevel because the wire's ACCEPT is {seat, n}. The spine says ctx.api is the lowest host level in the roster and apply runs on every device. Unreachable in v1 (n = 2, one level). The fix is a level in ACCEPT (a Protocol change) or an owner decision on the rule.
    location: >-
      lib/GameCore/GuestLobby.cpp onAccept; lib/GameCore/Protocol.h Accept
    severity: medium (unverified)
---

<intent-contract>

## Intent

**Problem:** `ReliableLink` (entry 4) and `Protocol`/`FakeLink` (entry 2) exist, but nothing speaks the lobby (R3): no host that draws a `session`, advertises, seats guests by `JOIN` arrival and starts, and no guest that lists hosts, joins, and waits. Entry 10's screens and entry 8's `NearbySession` need that logic, tested without a radio.

**Approach:** Add `GameCore::HostLobby` and `GameCore::GuestLobby` in new `lib/GameCore` files, each owning a `ReliableLink` as its `Sink`, and prove them in host suites over a seeded hostile `FakeLink`, including crossplay's discovery regressions in this protocol's terms and the rejoin and restart cases entry 4's follow-up routed here.

## Boundaries & Constraints

**Always:** `lib/GameCore` includes only std, `lib/Memory`, `lib/JsonParser` (`scripts/check_layers.py`); no radio fact (MAC, channel, RSSI, ESP), no log, no static initializer or mutable static (AD-2); every fallible allocation is `makeUniqueNoThrow` and null-checked (the lobbies add none beyond `ReliableLink`'s); function locals stay under 256 B. A peer is named only by `PeerHandle`; time comes only from `IClock`, the session id only from `IRandom`; `Protocol` stays the only encoder and decoder (the lobby builds `Protocol::Frame` values). Nothing assumes n = 2 (seat count from the roster, limits from `LobbyGame`). The lobby calls `ReliableLink::forget()` for a guest that leaves, is refused, or goes silent, and for the host on the guest side when its wait ends. Code adapted from crossplay carries a comment naming the crossplay source file (MIT notice is already in `ReliableLink.cpp`/`FakeLink.h`; a lobby file that adapts a crossplay rule names `src/apps_local/link/LinkSession.cpp` and `host-tests/link/test_link.cpp`). Test doubles follow the Build-agent brief (Design Notes 10).

**Never:** no change to `Protocol.h/.cpp`, `ReliableLink.h/.cpp`, `ILink.h`, any `src/**`, `lib/GameScript/**`, `games/**`, upstream file, `freeink-sdk`, spine, or `docs/` (needed AD-13 text goes under Auto Run Result). No wire-level epoch or resync rule: if the lobby cannot converge without one, HALT as blocked for the owner. No `NearbySession`, session queue, `GameLink` task, `STATE` handling, or screens (entries 7 to 10). No `MOVE`/`REJECT`. No new `tr()` text.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Two devices start | host Waiting, one compatible guest joins, host Start; 200 seeds at 30 % loss, 20 % duplicates, reorder, delay | host roster seat 1, guest seat 2, n = 2, mode Nearby, `ctx.api` the lowest level; `ADVERT` stops | none |
| Three seats | `maxSeats` 3, guests join one after the other | n = 3, seats 2 and 3 in `JOIN` order | none |
| Full | `maxSeats` 2, second guest joins | `ABORT(full)`; first guest unaffected and the host still starts | guest state Refused(Full), host forgets the stranger |
| Different version | other package hash, or other `surface` while either level is a preview; other `proto` | listed as `DifferentVersion` and `join` refuses without sending; a raw `JOIN` is answered `ABORT(version_mismatch)`; a host on another `proto` is not listed | none |
| Start refused | no guest, or the only guest left or went silent | `start()` returns `NotEnoughSeats`, nothing sent | none |
| Guest leaves | joined guest cancels (`ABORT(left)`) or stops answering for the silence limit | seat freed, `forget()` called, event queued; host keeps waiting | host `ADVERT` shows the new count |
| Host leaves | host cancels, or goes silent | waiting guest reaches HostGone(Left or Silent) and forgets the host | guest `backToList()` returns to Browsing |
| Other session | a frame of another `session` | ignored by host and guest alike | counted as a bad frame by `ReliableLink` |
| Lost `JOIN`, one-way range | `JOIN` lost; guest-to-host or host-to-guest blackholed for 2 s | `JOIN` resent until acked; both sides converge to a started roster | none |
| Host broadcasts after Start | a same-session `ADVERT` reaches a started guest after the grace window | the match is ended (Left); inside the window, ignored | none |
| `JOIN` echo | duplicate `JOIN`s of a seated guest, and bursts from a refused stranger, after Start | one `ACK` per copy of a seated guest's `JOIN`, never an `ABORT`; one `ABORT` per refusal interval for a stranger; `FakeLink` carried count stays under a budget | none |
| Rejoin | guest cancels and joins the same host again (`ABORT` delivered, or all copies lost); guest restarts on its address (no `ABORT`); guest restarts on a new address | a started roster in every case; on a new address the old seat is held at most the silence limit | the new address gets `ABORT(full)` until then |

</intent-contract>

## Code Map

- `lib/GameCore/ReliableLink.h/.cpp` -- read, not changed. `Sink::onFrame` returns `Taken`/`Refused`/`NoRoom`; an offered frame registers its peer (a JOIN at seq 0); the sink calls `forget(from)` to turn a stranger away (the link then neither acks it nor keeps its slot); `send(Abort)` goes out at once, once, even to an unknown peer; `broadcast(Advert)`; `unacked(peer)`; `setSession(0)` accepts only `ADVERT`, a changed id resets every peer; a duplicate of an already-taken reliable frame is acked, never offered; silence reported once per peer, slot kept until `forget`. Adopted from it: nothing to change.
- `lib/GameCore/Protocol.h` -- read: `Advert`, `Join`, `Accept`, `AbortReason`, `Frame`, limits (`MAX_HOST_LABEL_BYTES` 16, `MAX_GAME_ID_BYTES` 32, `PKG_HASH_BYTES` 8), `PROTO`. `decode` already drops a wrong `proto` and another `session`.
- `lib/GameCore/Roster.h/.cpp` -- add `Roster::nearby(seats, localSeat, api)` beside `Roster::pass`. Compiled by the `game_script` targets already; no CMake change there.
- `lib/GameCore/HostCaps.h`, `IClock.h`, `IRandom.h`, `ApiLevel.h` (`API_LEVEL_FROZEN`) -- read.
- New: `lib/GameCore/Lobby.h/.cpp` (shared types), `HostLobby.h/.cpp`, `GuestLobby.h/.cpp`.
- `test/game_core/FakeLink.h` -- add `FakeLink::restart()` (Design Notes 10); `FakeLinkTest.cpp` -- its tests. Read: `ManualClock`, `Faults`, `blackhole`, `carried`, `setCarriedBudget`, `handleOf`.
- New: `test/game_core/LobbySupport.h` (rig, raw peer, fake `IRandom`), `LobbyTest.cpp` (verdicts, host, guest, hand-off, `Roster::nearby`), `LobbyHandshakeTest.cpp` (host and guest together, regressions).
- `test/game_core/CMakeLists.txt` -- register the new sources and tests. The `test/game_script` CMake files are not touched (no new `lib/GameCore` source reaches those targets).
- Not touched, and why: no existing function is rewritten, so no `git log -L` guard check applies (`Roster.h/.cpp` only gain a factory; `FakeLink.h` gains `restart()` and one `NetState` method, its existing early returns unchanged).
- Crossplay reference (read-only): `src/apps_local/link/LinkSession.cpp` (`kHelloIntervalMs` 400, `kJoinIntervalMs` 200, `kCandidateForgetMs` 3000, `kByeRepeats` 3, the Hello-after-match rule and its `peerConfirmed_` guard, the Join rate limit) and `host-tests/link/test_link.cpp` (`testMatchSurvivesALostJoin`, `testOneWayRangeStillConverges`, `testMatchedPeersDoNotEchoJoins`, `testPeerRestartingEndsTheMatch`, `testJoinAnswersAreRateLimited`).

## Tasks & Acceptance

**Execution:**
- [ ] `lib/GameCore/Roster.h`, `Roster.cpp` -- add `Roster::nearby` -- the roster a started lobby hands to the match.
- [ ] `lib/GameCore/Lobby.h`, `Lobby.cpp` -- `LobbyGame`, `LobbySettings`, `HostVerdict`/`versionVerdict`, `LobbyEnd`, `LobbyHandOff`, `drawSession` (Design Notes 2 to 4, 7).
- [ ] `lib/GameCore/HostLobby.h`, `HostLobby.cpp` -- the host (Design Notes 5).
- [ ] `lib/GameCore/GuestLobby.h`, `GuestLobby.cpp` -- the guest (Design Notes 6).
- [ ] `test/game_core/FakeLink.h`, `FakeLinkTest.cpp` -- `restart()` and its tests.
- [ ] `test/game_core/LobbySupport.h`, `LobbyTest.cpp`, `LobbyHandshakeTest.cpp`, `CMakeLists.txt` -- every test of Design Notes 9, with a mutant list recorded in Implementation Notes (each applied alone to the library, run, reverted; each makes a named test red).

**Acceptance Criteria:**
- Given any of 200 seeds on a hostile `FakeLink`, when a host Waits and a compatible guest joins and the host starts, then both hold a started roster (seat 1 / seat 2, n = 2) within 5 s of fake time and no `ADVERT` is broadcast after Start.
- Given `maxSeats` 3, when two guests join in turn, then Start gives n = 3 with seats 2 and 3 in `JOIN` order; given `maxSeats` 2, a second guest gets `ABORT(full)`.
- Given a host with no guest, or whose only guest cancelled or went silent, when `start()` is called, then it returns `NotEnoughSeats` and sends nothing.
- Given a guest that cancels, restarts on its address, or restarts on a new one, when it joins the same host again, then a started roster follows without a `Protocol` or `ReliableLink` change.
- Given `scripts/check_layers.py` it passes with no new edge; given an x4pro games-on build, the growth over the epic base (+258,048 B flash, +784 B static RAM) stays within what is left of 9,504 B and 160 B (new code is unreferenced from firmware, expected +0 B over the baseline).

## Implementation Notes

- Baseline size measurement, before any change, in this worktree at `3a36b97a826946c41b4365fe5ed309ec850ed15b` (`scripts/check_flash_budget.py` `build on`, `build off`, `compare`, `objects`, incremental tree, shared cache, build lock; all exit 0, `objects` "No problems"; log `6.6/baseline.log` in the scratchpad): `firmware.bin` 6,213,808 B games on, 5,955,312 B off, +258,496 B flash; static internal RAM 190,248 B on, 189,464 B off, +784 B. Over the epic base (+258,048 B, +784 B): +448 B flash, +0 B static RAM; share left 9,056 B flash, 160 B static RAM.

- Host suites (2026-10-12, this worktree, before the review): `ctest` 2,224 of 2,224 pass; before this story it was 2,164 (this story adds 56 `Lobby*` and 4 `FakeLinkTest` tests, counted by `--gtest_list_tests`). The `Lobby*` tests alone under `--gtest_repeat=20`: 20 of 20 pass (the seeds are fixed, so a repeat re-runs the same schedules). ASan/UBSan outside CMake over `LobbyTest`, `LobbyHandshakeTest`, `FakeLinkTest`, `ReliableLinkTest`, `ProtocolTest` with the lobby, `ReliableLink`, `Protocol`, `Roster` sources: 154 of 154 pass, no report. `check_layers.py` (654 edges), `check_layers_test.py` (53 tests), `check_upstream_touches.py` pass; `./bin/clang-format-fix` twice, the second run changes nothing.
- Mutants (each applied alone to the library, `Lobby*` run, reverted; every one red): no `forget` on a leaving guest (`AGuestsCancelFreesItsSeat`, `ASilentGuestFreesItsSeat`, `AGuestThatLeavesAndRejoinsReachesAStartedRoster`); no `forget` on a refusal (`AFullHostAnswersAbortFullAndKeepsItsGuest`, `HostRefusesAForeignHashAndAForeignSurfaceJoin`, `RefusalAnswersAreRateLimited`, `ARefusedGuestCanRetryWhenASeatFrees`, `ASecondGuestAtTwoSeatsGetsFullAndTheFirstStarts`, `AStrangerWithTheSessionIdCannotExhaustTheHostsPeers`, `JoinsFromMatchedDevicesNeverEcho`, `AGuestThatRestartsOnAnotherAddressRejoinsAfterTheSilenceLimit`); no `forget` on the guest side (`GuestReportsFullVersionMismatchHostLeftAndHostSilence`: no keepalive goes to a host the guest left); ADVERT not stopped at Start (`AHostAndAGuestReachAStartedRosterForManySeeds`, `ASeatChangeAdvertisesAtOnceAndNothingIsAdvertisedAfterStartOrCancel`, `HostCancelSendsAbortLeftThriceAndStopsAdvertising`, `NoFalseEndFromLateCopiesAcrossSeeds`, `JoinsFromMatchedDevicesNeverEcho`); the grace window removed (`AHostHeardBroadcastingAgainAfterStartEndsTheMatch`, `NoFalseEndFromLateCopiesAcrossSeeds`); the refusal rate limit removed (`RefusalAnswersAreRateLimited`, `JoinsFromMatchedDevicesNeverEcho`); seats given at JOIN time, not compacted (`ThreeSeatsGiveContiguousSeatsInArrivalOrder`, `StartNeedsASeatedGuest`, `AGuestThatLeavesAndRejoinsReachesAStartedRoster`, `ARefusedGuestCanRetryWhenASeatFrees`); `start()` without the seat check (`StartNeedsASeatedGuest`, `StartNeedsAtLeastTheGamesMinimumSeats`); the version rule's surface clause dropped (`VersionVerdictTable`, `HostRefusesAForeignHashAndAForeignSurfaceJoin`, `GuestListsOnlyItsGameWithVerdictsLabelsAndSeats`); `maxPeers` lowered by one (`AFullHostAnswersAbortFullAndKeepsItsGuest`, `AStrangerSendingAcceptStateOrAdvertHoldsNoSlot`, `RefusalAnswersAreRateLimited`, `ASecondGuestAtTwoSeatsGetsFullAndTheFirstStarts`, `ARefusedGuestCanRetryWhenASeatFrees`, `JoinsFromMatchedDevicesNeverEcho`, `AGuestThatRestartsOnAnotherAddressRejoinsAfterTheSilenceLimit`).
- Deviations from the Design Notes, found while building (none changes `Protocol` or `ReliableLink`): (1) `HostLobby::cancel()` (and a failed `start()`) also calls `forget()` for every guest after the ABORT copies, so a guest that lost all three copies hears the host's silence and leaves at the limit instead of being kept alive by the host's keepalives (the plan's "host cancels, then guest gone within the silence limit" under 30 % loss needs it). (2) `HostEntry` gained `hostApi` (the guest needs the host's level for `ctx.api`). (3) An already-seated handle's `JOIN` is `Taken` in every state, not `ABORT(full)` in Started (AD-13's "late JOIN" is a stranger's). (4) `startSeats()` clamps at 255 so a manifest minimum above 16 never starts. (5) `AGuestThatRestartsOnItsAddressRejoins` measured a worst case of 3,520 ms over 100 hostile seeds (the plan said 3 s); its bound is 4,000 ms, an estimate until R12.
- Residue, as Design Notes 7 states it, now pinned by how the tests are timed: a delayed copy of a guest's `JOIN` that reaches the host after that guest's `ABORT` seats a ghost until the silence limit (the rejoin tests wait 500 ms, longer than any hostile delay, between `Joined` and `cancel()` and between `cancel()` and the new `GuestLobby`); and a host that calls `start()` while a ghost holds the only guest seat starts without the real guest, which then never sees an ADVERT (the rejoin tests start the host only once the new guest is `Joined`). Neither needed a wire epoch.

- After the review (2026-10-12): patches to `HostLobby::ok()` (also `startSeats() <= seatLimit()`), `GuestLobby::onAccept` (an ACCEPT outside the guest's own seat range is a host fault: HostGone(Left), host forgotten), `HostLobby::onJoin` (Closed is refused first with `ABORT(left)`), one shared `elapsed()` in `Lobby.h`, comments, and 11 new or extended tests (State before ACCEPT answered `NoRoom`, clock stepped back, event ring wrap, cancel from Joining and Started, host cancel from Started, non-default `LobbySettings`, a three-level roster, carried budgets on three regressions, the refused-first-transmission pin, `ok()`, the Closed host). Host suite 2,231 of 2,231 (baseline 2,164; this story adds 67); ASan/UBSan (outside CMake) over `LobbyTest`, `LobbyHandshakeTest`, `FakeLinkTest`, `ReliableLinkTest`, `ProtocolTest` with the lobby, `ReliableLink`, `Protocol`, `Roster`, `Manifest` sources: 161 of 161. Mutants for the three logic patches (the `ok()` clause, the ACCEPT range check, the Closed check) each red in `ALobbyWithoutSeatsForTheGameIsNotOk`, `AnAcceptOutsideTheRangeEndsTheWait`, `HostCancelSendsAbortLeftThriceAndStopsAdvertising` and `ACancelledHostAnswersAResentJoinAbortLeftAndHoldsNoSlot`, reverted.
- The guest's `ctx.api` in a three-seat match with different levels is the lowest of its own and the host's (the wire's `ACCEPT` is `{seat, n}`); the host's roster and the lowest guest's carry the true minimum (`ARosterApiIsTheLowestLevel`).

- Follow-up review pass (2026-10-12, fresh four-lens pass over the `built` commit `8abb9271`): patches to `GuestLobby::onAccept` (an ACCEPT the guest ends the wait on now also sends `CANCEL_ABORT_COPIES` copies of `ABORT(left)` to the host, so a started host hears the guest is gone at once), a stale `ok()` comment in `HostLobby.h`, missing includes in `LobbyHandshakeTest.cpp`, and 5 new tests (`AnAbortFromAnotherDeviceDoesNotEndAWaitingGuest`, `AMidListExpiryKeepsTheSurvivorsInOrder`, `ALateAdvertBeforeAttachEndsTheMatchOnAttach`, `RefusalAnswersAreRateLimitedPerStrangerWhenTheyInterleave`, `AGuestThatCannotSeatTheStartedMatchTellsTheHost`) plus an assertion in `expectHostFaultEndsTheWait`. Host suite 2,236 of 2,236 (this story adds 72 over the 2,164 baseline); `Lobby*` and `FakeLink*` (108 tests) under `--gtest_repeat=20`: 20 of 20 pass; ASan/UBSan (outside CMake) over `LobbyTest`, `LobbyHandshakeTest`, `FakeLinkTest`, `ReliableLinkTest`, `ProtocolTest` with the lobby, `ReliableLink`, `Protocol`, `Roster`, `Manifest` sources: 166 of 166. Mutants, each applied alone, run, reverted, each red: the guest's `from != joined_.handle` ABORT rule removed (`AnAbortFromAnotherDeviceDoesNotEndAWaitingGuest`, `HandOffForwardsAfterStartAndLatchesBefore`); `expireHosts` compaction removed (`AMidListExpiryKeepsTheSurvivorsInOrder`); `LobbyHandOff::forwardAbort` not latching (`ALateAdvertBeforeAttachEndsTheMatchOnAttach`); the refusal ring index fixed at 0 (`RefusalAnswersAreRateLimitedPerStrangerWhenTheyInterleave`); the new ABORT send removed (`AGuestThatCannotSeatTheStartedMatchTellsTheHost`, `AnAcceptOutsideTheRangeEndsTheWait`). The test double `RawPeer` stands where it did (Design Notes 10); the new tests add no double and no timing bound beyond the default `LobbySettings`.

## Plan Change Log

## Review Triage Log

### 2026-10-12 — Review pass
Four context-free lenses ran as subagents (BH blind hunter, floor N = min(floor(sqrt(116.2)+1),10) = 10; EC edge-case hunter; VG verification gap; IA intent alignment, descriptive) over `git diff 3a36b97a..` of `lib` and `test` (119 kB; the plan went only to the edge-case lens). The implementation subagent from step 3 fixed the patches (re-engaged by message).
- verdicts: 36 findings — high 0, medium 12, low 19, false 5, maybe-false 0 (BH 13, EC 10 including 2 claims, VG 7, IA 6; a row names every finding id it covers when several share one root cause)
- findings:
  - `[low]` `defer` [BH1, EC1] A newer peer's level is taken as a preview, so a frozen older device refuses a frozen newer peer whose surface differs, against AD-19's mixed levels — real but unreachable in v1 (one level); the wire carries no frozen flag, so the fix is a `Protocol` change or an owner decision (outside the touches); the header now says so; deferred (medium if a second level ships, unverified).
  - `[low]` `patch` [BH2] `HostLobby::ok()` accepts a game with `startSeats() > seatLimit()` that can never start — confirmed at `ok()`; `ok()` requires `startSeats() <= seatLimit()`, test added.
  - `[medium]` `patch` [BH3, EC4, EC5] The guest trusts an ACCEPT that passes decode: seat 1 or n 1 is acked and dropped (the guest stays Joined forever), n above its own `seatLimit()` or below `startSeats()` starts an oversized roster — confirmed at `onAccept`; both are a host fault now: HostGone(Left), host forgotten; tests added.
  - `[medium]` `patch` [BH4, VG3, EC7(test part), VG4, VG5, VG6, VG1] Untested paths: `JoinResult::LinkRefused` and `StartResult::LinkFailed`, host event-queue overflow and ring wrap, guest cancel from Joining or Started and host cancel from Started, a `State` before ACCEPT answered `NoRoom`, the roster api over three seats with different levels — confirmed by grep and by the lens's mutants; one test each added.
  - `[medium]` `patch` [BH5, VG2, EC8(test part)] The `elapsed()` clock-stepped-back guard has no test — confirmed; a test per lobby added (host list kept, no extra ADVERT, no forged ABORT, refusal limiter quiet).
  - `[low]` `patch` [BH6] Custom `LobbySettings` are never tested; bounds are literals of the defaults — one test with non-default values and bounds derived from the settings added.
  - `[low]` `reject` [BH7] Zero or odd settings are not validated — values come from named constants in the screens' code; a guard adds branches for no reachable caller (entry 4 rejected the same for `ReliableLink::Settings`).
  - `[low]` `patch` [BH8] The refusal limiter's ring of four can be defeated by more than four rotating strangers — comment states the bound; each stranger's own JOIN resend is 400 ms, so a flood costs one ABORT per resend per stranger.
  - `[low]` `reject` [BH9] The 4,000 ms restart bound is brittle — the PRNG is seeded and fixed, so the measured 3,520 ms repeats exactly; the bound is labelled an estimate until R12.
  - `[low]` `patch` [BH10, EC6] `host(index)` returns entry 0 for a bad index — documented (check `hostCount()`), as is `joinedHost()`.
  - `[low]` `patch` [BH11] `elapsed()` duplicated in two files — one definition in `Lobby.h`; the cancel loops differ in recipients and stay, the duplicated test helper stays.
  - `[low]` `patch` [BH12] `ACtxApiIsTheLowestLevel` name and `Join::hostApi` naming — test renamed `ARosterApiIsTheLowestLevel`, a comment names `hostApi` as the sender's own level.
  - `[low]` `reject` [BH13] `manifestApi` unused by the guest; a full host is listed unflagged — the guest shows what the host says and the host answers `ABORT(full)` (R8 "That game is full"); the screen decides.
  - `[medium]` `patch` [EC2] A Closed host (after cancel or `LinkFailed`) keeps `guests_`, so a forgotten guest's resent `JOIN` is `Taken`, re-registers the peer and resumes keepalives — confirmed at `onJoin` (seated check ran before the state check); Closed is refused first with `ABORT(left)`; test added.
  - `[medium]` `defer` [EC3, claim-high] A departed guest can still hold a seat: a delayed copy of its `JOIN` arriving after its `ABORT`, or all three `ABORT` copies lost, leaves the host a seated ghost until the silence limit, and `start()` then sends `ACCEPT` to a gone device — real, bounded by 10 s, and inherent without an epoch or the guest's presence being re-proved; Design Notes 7 states it; the screens (entries 8, 10) should offer Start only while the guest is live; no `Protocol` change was needed for convergence.
  - `[low]` `reject` [EC7 code part] The event queue drops the oldest silently on overflow — eight unpolled events means a stalled screen, which resyncs from `seatsTaken()`; the header says so; a drop counter adds surface for no reachable caller.
  - `[low]` `reject` [EC8 code part] A backward clock stops ADVERTs until it catches up — `IClock` is monotonic by contract (`IClock.h`); the guard exists so a step back does not underflow, as `ReliableLink`'s does; tested (above).
  - `[low]` `reject` [claim-low] A guest that restarts on a new address ends in Refused(Full) and the caller must retry — by design (Design Notes 7 d); the screen offers the list again.
  - `[low]` `defer` [VG3, remainder] `JoinResult::LinkRefused` and `StartResult::LinkFailed` are not reachable by `FakeLink::refuseSends`: `ReliableLink::send` returns true for a reliable frame it queued whatever the link does with the first transmission (an ordinary loss, resent). `ARefusedFirstTransmissionOfAJoinOrAnAcceptIsResentNotFailed` pins that; the two branches that remain (a lost peer, a table or allocation failure) need an allocation seam `test/game_core` does not have (deferred-work `## 6.4`, item 2).
  - `[low]` `patch` [VG-other] `elapsed()` is duplicated — carried to BH11.
  - `[false]` `reject` [IA1] Some items are shown with a raw peer, not two real lobbies — by design (an oracle independent of the lobbies; the two-lobby flows cover the rest).
  - `[low]` `patch` [IA2] Only the echo test counts traffic with `FakeLink`, though the verify says each regression is counted — budgets added to the lost-JOIN, one-way, and restart tests.
  - `[false]` `reject` [IA3] No `NearbySession` or screens in the diff — entries 8 and 10 own them; the lobby offers `handOff`, `roster`, `seat`, `peerForSeat`.
  - `[false]` `reject` [IA4] Rejoin uses a new `GuestLobby` — the radio is torn down on Cancel (R8), so a new lobby is the real flow; the host side is the same lobby throughout.
  - `[false]` `reject` [IA5] Seat compaction after a leave — the plan settles it (AD-11: `n` seats filled at Start, contiguous).
  - `[false]` `reject` [IA6] Firmware checks not visible in the diff — they are verification, run on the tree.

### 2026-10-12 — Review pass (follow-up pass on the `built` plan at `8abb9271`)
Four context-free lenses ran again as subagents (BH floor N = min(floor(sqrt(133.8)+1),10) = 10; EC; VG; IA) over `git diff 3a36b97a..8abb9271` of `lib` and `test` (137 kB; the plan went only to the edge-case lens). Rows that repeat a first-pass row at the same location with the same claim are `carried`. The step-3 implementation subagent was gone, so the patches were applied directly. (BH 14, VG 5 plus 1 other, EC 7, IA 6.)
- verdicts: 33 findings — high 0, medium 7, low 20, false 6, maybe-false 0
- findings:
  - `[low]` `reject` [BH1, EC6] `Join::hostApi` / `ADVERT hostApi` is not checked against `caps.minApi`, so a peer at level 0 drags the roster's `api` down — an honest peer cannot reach it: both devices passed `Manifest::check` (`api >= minApi`, `Manifest.cpp:278`) for the same package, and a device's level is at least the manifest's; only a lying device (the wire is unauthenticated, as for the hash) reaches it; a guard adds a branch for no reachable caller.
  - `[low]` `reject` [BH2] `elapsed()` also exists in `ReliableLink.cpp` — that copy is in an anonymous namespace of a file this entry must not change; the first pass already merged the two lobby copies.
  - `[low]` `reject` [BH3] The three ABORT copies leave in one tick, so one fade kills all — crossplay's `kByeRepeats` loop (`LinkSession.cpp:103`) sends them back to back too; spacing needs a send queue for unsequenced frames (outside the entry); R12 measures it.
  - `[low]` `reject` [BH4] Test bounds are literals (`2900`, `9900`, `3100`) — fake milliseconds derived from the default settings, with `EveryLobbySettingChangesTheBehaviourItNames` deriving its own from non-default ones; a default change is a deliberate test change.
  - `[low]` `reject` [BH5] The 3,520 ms and budget figures in comments go stale — `carried` (first pass BH9: fixed seeds, labelled an estimate until R12).
  - `[low]` `reject` [BH6] Comments cite "Design Notes 7", R3, AD-13 — the lobby headers cite the spine's AD numbers as the rest of `lib/GameCore` does; the plan is in the repo beside the entry.
  - `[low]` `defer` [BH7, VG5] `JoinResult::LinkRefused` and `StartResult::LinkFailed` have no test — `carried` (first pass deferred item 3; needs an allocation seam).
  - `[low]` `reject` [BH8] Coverage stops at 3 seats — the lobby has no per-count branch; the compaction loop is pinned by `ThreeSeatsGiveContiguousSeatsInArrivalOrder` (a leaver ahead of two guests).
  - `[low]` `reject` [BH9] `GuestLobby::ok()` is weaker than the host's — a game whose `seatsMin` exceeds the device's `maxSeats` is `Unavailable(TooManySeats)` in `Manifest::check` (`Manifest.cpp:280`) and never opens a lobby.
  - `[low]` `patch` [BH10] `host(index)` returns entry 0 for a bad index — `carried` (first pass BH10: documented, check `hostCount()`).
  - `[false]` `reject` [BH11] A game id over 32 bytes is silently truncated — `Manifest::MAX_ID_BYTES` is 32, equal to `Protocol::MAX_GAME_ID_BYTES`, so no valid id is cut.
  - `[low]` `reject` [BH12] `HostEntry` drops `manifestApi` — `carried` (first pass BH13).
  - `[low]` `defer` [BH13] A frozen-vs-frozen mixed-level handshake is untested — the rule's limit is first-pass deferred item 1 (`carried`); the unit table pins both clauses.
  - `[low]` `patch` [BH14] `LobbyHandshakeTest.cpp` uses `std::min`, `std::pair`, `std::deque` without their headers (patched: `<algorithm>`, `<deque>`, `<utility>`); `FakeLink::restart()` clears `done` outside the lock — rejected: `end()` sets it the same way, single-threaded test double.
  - `[medium]` `patch` [VG1] The guest's "ABORT only from the joined host" rule has no test while waiting — confirmed (the only stranger-ABORT test runs in Started); `AnAbortFromAnotherDeviceDoesNotEndAWaitingGuest` added; mutant (rule removed) red there.
  - `[medium]` `patch` [VG2] `expireHosts` compaction is only tested with all or one entry expiring — confirmed; `AMidListExpiryKeepsTheSurvivorsInOrder` added; mutant (compaction removed) red.
  - `[medium]` `patch` [VG3] The lobby's own "host advertising again" end, before the match attaches, is untested — confirmed; `ALateAdvertBeforeAttachEndsTheMatchOnAttach` added; mutant (latch removed in `forwardAbort`) red.
  - `[low]` `patch` [VG4] The refusal ring's "four rotating strangers" bound is untested — confirmed; `RefusalAnswersAreRateLimitedPerStrangerWhenTheyInterleave` added; mutant (ring index fixed at 0) red. (VG filed `defer`; the test is one small addition to a comment-stated bound.)
  - `[low]` `patch` [VG other] `HostLobby.h` documents `ok()` without the `startSeats() <= seatLimit()` clause — comment corrected.
  - `[medium]` `patch` [EC1] A guest that ends the wait on an ACCEPT its own device cannot seat (its `caps.maxSeats` below the host's) sends nothing, so a started host keeps it as a seat until the silence limit — confirmed at `onAccept`; the branch now sends `CANCEL_ABORT_COPIES` copies of `ABORT(left)` before `forget`; `AGuestThatCannotSeatTheStartedMatchTellsTheHost` (a real host, 3 seats, one guest at 2) and an assertion in `expectHostFaultEndsTheWait` added; mutant (send removed) red in both.
  - `[medium]` `defer` [EC2] The host's hand-off latches one end (first wins), so with 3+ seats a second guest's abort or silence before attach is dropped — real, but only for n >= 3 in the window between Start and attach (entry 8 attaches at once) and only entry 8 knows what one guest's end does to a match; deferred, medium unverified.
  - `[low]` `reject` [EC3] A delayed older-session ADVERT from a host replaces its newer entry — the host's next ADVERT (500 ms) replaces it again; a JOIN in that window is resent until the silence limit and the guest returns to the list; the fix adds ordering state for a delay longer than the host's re-open time.
  - `[medium]` `defer` [EC4] A late duplicate `ABORT(left)` of a guest that has already rejoined removes its fresh seat and forgets the link — the same root as first-pass deferred item 2 (no epoch; a delayed copy outlives its sender's intent), the other direction; bounded by the silence limit; the rejoin tests wait 500 ms, longer than any hostile delay; deferred, a wire epoch is a `Protocol` change.
  - `[low]` `defer` [EC5] After Start a same-session `ADVERT` never comes from a real `HostLobby` (a new lobby draws a new session, which the guest's link drops, and `HostLobby` stops advertising at Start), so the rule only catches a same-session copy and a host that left the match is found by the 10 s silence — real; the verify names the rule and it is implemented as named; a faster end needs the link to pass another session's `ADVERT` (a `ReliableLink` change, outside the entry); a host's cancel sends `ABORT(left)` and ends it at once.
  - `[low]` `reject` [EC7] `join()` can join an entry that expired but was not yet pruned — `tick()` prunes every step in Browsing, so the entry is stale by one tick at most; the JOIN is resent and the silence limit applies as for any lost host.
  - `[false]` `reject` [IA-A] `ctx.api` is checked at `Roster.api`, not at `Session` — `Session` consuming the lobby's roster is entry 8; `Roster::nearby` stores the level and `ARosterApiIsTheLowestLevel` pins what each lobby hands over.
  - `[medium]` `defer` [IA-B] In a match of three or more seats with different levels a guest's `ctx.api` is the lower of its own and the host's (the wire's `ACCEPT` is `{seat, n}`), so two guests can differ from each other and from the host, and `apply` runs on every device — real against the spine's "lowest host level in the roster"; unreachable in v1 (n = 2, one level); the fix is a level in `ACCEPT` (a `Protocol` change) or an owner decision; deferred, medium unverified.
  - `[false]` `reject` [IA-C] "Hands to the match" is shown against a recording sink — `NearbySession` is entry 8; the sink double names the hand-off contract.
  - `[false]` `reject` [IA-D] Behaviour is shown at the protocol level, not through screens — entry 10 owns the screens; no `src/**` is touched.
  - `[false]` `reject` [IA-E] Behaviour beyond the intent's list (rate limits, rejoin, queue) — all named by R3, the epic Notes, or entry 4's follow-up.
  - `[false]` `reject` [IA-F] The script-harness CMake files are not changed — no new source reaches those targets (Design Notes 1); `Roster.cpp` is already compiled there.

## Design Notes

**Settled by** (the line that settles each choice). Seats by `JOIN` arrival, host seat 1, `ACCEPT {seat, n}` to every guest, `ADVERT` stops at Start, `ABORT(full)` for a late `JOIN`, `ABORT(version_mismatch)`, `ctx.api` the lowest `hostApi`: R3, spine AD-11 (Nearby bullet) and AD-13 (session and matching bullets). Freeing a seat before Start and the host-left report: R3, R8, Notes Decision 2026-10-12 ("the lobby before Start"). A host on another `proto` never listed: Notes Decision 2026-10-12 (entry 2's checkpoint, Q1); `Protocol::decode` already does it. `forget()` on a guest that leaves, is refused, or goes silent, and the rejoin and restart tests: Notes Assumption 2026-10-12 (from entry 4's follow-up review), `deferred-work.md` `## 6.4`. Discovery regressions: Notes "Adopted from crossplay regardless of the model" and entry 6's verify. `Start` at least 2 seats: R3, AD-11. The roster and `ctx.api` handed to the match: entry 6's description. Settings values below are this plan's (R12 confirms them on a radio).

### 1. Files

`Lobby.h/.cpp` (shared), `HostLobby.h/.cpp`, `GuestLobby.h/.cpp`, all `namespace GameCore`. Each lobby owns a `ReliableLink` member built over the `ILink&` and `IClock&` it is given and is that link's `Sink` (private base). One task calls a lobby at a time (as `ReliableLink`); no locking. Sizes: a lobby is a few hundred bytes plus the `ReliableLink`'s heap (about 3 KB and 260 B per peer slot; snapshot buffers are only allocated by `STATE`, which the lobby never sends).

### 2. `Lobby.h`: shared types

```cpp
struct LobbyGame {                       // what a lobby knows about the game, copied in
  char gameId[Protocol::MAX_GAME_ID_BYTES + 1] = {};   // the manifest id
  std::array<uint8_t, Protocol::PKG_HASH_BYTES> pkgHash{};
  uint8_t manifestApi = 0;               // the game's declared api (ADVERT carries it)
  int32_t seatsMin = 0, seatsMax = 0;    // the manifest's seats
  uint32_t surface = 0;                  // API_SURFACE_CRC
  bool apiPreview = false;               // !API_LEVEL_FROZEN: this device's level is a preview
  HostCaps caps;                         // caps.api = this device's API_LEVEL; caps.maxSeats
  void setGameId(std::string_view id);   // copies at most 32 bytes
  uint8_t seatLimit() const;             // min(seatsMax, caps.maxSeats, Roster::MAX_SEATS), clamped to 0..16
  uint8_t startSeats() const;            // max(2, seatsMin): the fewest seats Start accepts
};
enum class HostVerdict : uint8_t { Joinable, DifferentVersion };
// Equal package hash, and equal surface when either device's level is a preview. The wire carries a peer's level,
// not whether it is frozen; a peer at or below this device's level is frozen when this device's level is
// (ApiLevel.h: every level below a frozen one is), a newer peer's level is taken as a preview.
HostVerdict versionVerdict(const LobbyGame& own, const std::array<uint8_t, 8>& peerHash, uint8_t peerApi, uint32_t peerSurface);
struct LobbySettings {
  uint32_t advertMs = 500;         // host: ADVERT interval (a seat change advertises at once)
  uint32_t hostExpireMs = 3000;    // guest: a host not heard for this long leaves the list (crossplay kCandidateForgetMs)
  uint32_t advertGraceMs = 1000;   // guest, started: a same-session ADVERT within this long of ACCEPT is a late copy
  uint32_t refusalAnswerMs = 200;  // host: one ABORT answer per refused peer per this long (crossplay kJoinIntervalMs)
  ReliableLink::Settings link;     // resend 400, silence 10,000, keepalive 1,000; maxPeers is set by the lobby
};
inline constexpr uint8_t CANCEL_ABORT_COPIES = 3;   // crossplay kByeRepeats: ABORT(left) is unreliable
struct LobbyEnd { enum class Kind : uint8_t { None, Abort, Silent } kind = Kind::None; Protocol::AbortReason reason = Left; PeerHandle peer; };
uint32_t drawSession(IRandom&);    // next32() until nonzero, at most 8 draws, then 1 (unreachable with a real source)
```
`LobbyHandOff` (used by both lobbies): after Start the lobby stays the link's `Sink`; `attach(ReliableLink::Sink& next)` makes it forward. `Verdict forward(PeerHandle from, const Protocol::Frame&)`: attached, returns `next->onFrame(...)`; not attached, a `State` returns `NoRoom` (the peer resends it after `attach`), an `Abort` is latched (the first wins) and returns `Refused`, anything else `Refused`. `forwardSilent(PeerHandle)`: attached, `next->onPeerSilent`; else latched. `attach` replays a latched end into `next` (an `Abort` as a synthesized `Frame` of type `Abort` with the lobby's session). `end()` returns the latched `LobbyEnd` (None when none arrived or after replay: kept readable).

### 3. Version rule and `ctx.api`

`versionVerdict`: `DifferentVersion` when `peerHash != own.pkgHash`, or when `(own.apiPreview || peerApi > own.caps.api) && peerSurface != own.surface`. In v1 (one level, not frozen) this is "hash, then surface" for both devices, so every reading of "either device's level is a preview" gives the same answer today; the peer-newer clause only matters once a second level exists. The roster's `api`: the host takes the lowest of its `caps.api` and every seated guest's `JOIN.hostApi`; a guest takes the lowest of its `caps.api` and the `hostApi` of its host's `ADVERT` (a guest cannot know another guest's level, the wire's `ACCEPT` is `{seat, n}`, and only seat 1's device runs `setup`, so `ctx.api` is read from the host's value; with n = 2 the two are equal).

### 4. Timing values

`advertMs` 500 (a guest on the join screen sees a host within one interval), `hostExpireMs` 3000 (six missed adverts; crossplay 3000), `advertGraceMs` 1000, `refusalAnswerMs` 200 (crossplay `kJoinIntervalMs`), 3 `ABORT(left)` copies on a cancel (crossplay `kByeRepeats`; R6's forced exit sends three too). All are `LobbySettings` fields, never literals elsewhere; unmeasured, R12 confirms.

### 5. `HostLobby`

```cpp
enum class HostState : uint8_t { Waiting, Started, Closed };
enum class StartResult : uint8_t { Started, NotEnoughSeats, NotWaiting, LinkFailed };
enum class HostEventKind : uint8_t { GuestJoined, GuestLeft, GuestSilent };
struct HostEvent { HostEventKind kind; uint8_t seat; };   // seat: the seat the guest held in arrival order (2 = first guest)
class HostLobby final : private ReliableLink::Sink {
 public:
  static constexpr size_t EVENT_CAPACITY = 8;
  HostLobby(ILink&, const IClock&, IRandom&, const LobbyGame&, std::span<const uint8_t> label, const LobbySettings& = {});
  bool ok() const;            // the link allocated and game.seatLimit() >= 2
  void tick();                // link.tick(), then ADVERT when Waiting and due (first tick, interval, or a seat change)
  HostState state() const;  uint32_t session() const;
  uint8_t seatsTaken() const; uint8_t seatLimit() const; bool canStart() const;   // seatsTaken >= startSeats()
  StartResult start();  void cancel();
  bool poll(HostEvent&);      // oldest event; the queue keeps the newest EVENT_CAPACITY
  const Roster& roster() const;  PeerHandle peerForSeat(uint8_t seat) const;  // valid once Started; NO_PEER otherwise / for seat 1
  ReliableLink& link();  void handOff(ReliableLink::Sink& next);  LobbyEnd ended() const;
};
```
The constructor draws the session (`drawSession`), sets it on the link, and clamps `label` to 16 bytes. Guests are kept in arrival order in a fixed array (`Roster::MAX_SEATS - 1`); a leaver is removed and the later ones close the gap, so seats are given at Start (rank + 2), always contiguous 1..n. The link's `maxPeers` is `seatLimit()` (every possible guest plus one free slot, so a stranger can still be registered and answered when the seats are full).
- `Join` offered while Waiting: `versionVerdict` (using the `JOIN`'s hash, `hostApi`, `surface`) else `ABORT(version_mismatch)`; else seats full (`seatsTaken >= seatLimit`) `ABORT(full)`; else seat the guest (an already-seated handle is `Taken` again, no second seat), queue `GuestJoined`, advertise at once, `Taken`. Every refusal answer is sent only if this peer has had no answer within `refusalAnswerMs` (a 4-entry ring of handle and time), and is followed by `forget(from)`; the verdict returned is `Refused`. A `JOIN` offered in Started or Closed is answered `ABORT(full)` the same way (AD-13: "answers a late JOIN with ABORT(full)").
- `Abort` from a seated guest: Waiting: free the seat, `GuestLeft`, `forget`, advertise; Started: `forward`. From anyone else: ignored. `onPeerSilent`: Waiting and seated: free, `GuestSilent`, `forget`; Waiting and not seated: `forget`; Started: `forwardSilent`.
- `Advert`, `Accept`, and `State` from an unseated stranger: `forget(from)`, `Refused` (a stranger must not hold a peer slot). `State` from a seated guest while Waiting: `Refused`; Started: `forward`.
- `start()`: `NotWaiting` unless Waiting; `NotEnoughSeats` unless `seatsTaken >= startSeats()` (nothing sent); then `ACCEPT {rank + 2, n}` to each guest in order via `ReliableLink::send`; a failed send ends the lobby (`ABORT(left)` to every guest, Closed) and returns `LinkFailed`; else state Started, the roster is `Roster::nearby(n, 1, api)`, ADVERT stops.
- `cancel()`: Waiting or Started: `CANCEL_ABORT_COPIES` copies of `ABORT(left)` to every seated guest, state Closed, no more ADVERT.

### 6. `GuestLobby`

```cpp
enum class GuestState : uint8_t { Browsing, Joining, Joined, Started, Refused, HostGone, Cancelled };
enum class Refusal : uint8_t { None, Full, VersionMismatch };
enum class GoneReason : uint8_t { None, Left, Silent };
enum class JoinResult : uint8_t { Sent, NotBrowsing, UnknownHost, DifferentVersion, LinkRefused };
struct HostEntry {
  PeerHandle handle;  uint32_t session = 0;
  std::array<uint8_t, 16> label{};  uint8_t labelLen = 0;
  uint8_t seatsTaken = 0, seatsMax = 0;  HostVerdict verdict = HostVerdict::Joinable;  uint64_t heardMs = 0;
};
class GuestLobby final : private ReliableLink::Sink {
 public:
  static constexpr size_t MAX_HOSTS = 8;
  GuestLobby(ILink&, const IClock&, const LobbyGame&, const LobbySettings& = {});   // link.maxPeers = 1, session 0
  bool ok() const;  void tick();  GuestState state() const;
  size_t hostCount() const;  const HostEntry& host(size_t index) const;   // first heard first; never reordered
  JoinResult join(PeerHandle host, uint32_t session);
  const HostEntry& joinedHost() const;  Refusal refusal() const;  GoneReason gone() const;
  void backToList();   // from Refused or HostGone: forget the host, session 0, empty list, Browsing
  void cancel();
  const Roster& roster() const;  uint8_t seat() const;  uint32_t session() const;  PeerHandle peerForSeat(uint8_t seat) const;  // Started
  ReliableLink& link();  void handOff(ReliableLink::Sink& next);  LobbyEnd ended() const;
};
```
- Browsing (link session 0, so only `ADVERT` is delivered): an `ADVERT` whose `gameId` equals the game's is upserted by (handle, session) (a new session for the same handle replaces the entry; a full table drops the new host unless an entry has expired); other games are not listed; the verdict is `versionVerdict(advert.pkgHash, advert.hostApi, advert.surface)`; `heardMs`, label (clamped), seats are refreshed. `tick` removes entries not heard for `hostExpireMs`.
- `join`: `NotBrowsing` unless Browsing; `UnknownHost` if (handle, session) is not listed; `DifferentVersion` if its verdict says so (nothing sent); else `setSession(session)`, send `Join {pkgHash, caps.api, surface}` to the handle; a false send undoes the session and returns `LinkRefused`; else Joining, `joinedHost` set, the other entries dropped.
- `tick`: Joining becomes Joined once `link.unacked(host) == 0` (the `JOIN` was acked, a duplicate's ack included).
- Joining or Joined: `Accept` from the host (`seat` in 2..n, n in 2..16) becomes Started (roster `Roster::nearby(n, seat, min(caps.api, hostApi))`, the time recorded), `Taken`; any other `Accept` is `Refused`. `Abort` from the host: `Full` is Refused(Full), `VersionMismatch` is Refused(VersionMismatch), `Timeout` is HostGone(Silent), `Left` and `ScriptError` are HostGone(Left); each then `forget(host)`. `onPeerSilent(host)`: HostGone(Silent), `forget(host)`. Frames from any other handle: ignored (`Refused`; a registered stranger is `forget`-ed). An `ADVERT` is ignored.
- Started: `State` and `Abort` from the host and `onPeerSilent` go through `LobbyHandOff`. A same-session `ADVERT` from the host later than `advertGraceMs` after the `ACCEPT` ends the match: a synthesized `Abort(Left)` goes through the hand-off (crossplay's Hello-after-match rule; earlier ones are late copies of pre-Start adverts, which a delaying link delivers). Only the host broadcasts, so a restarted guest is never announced: the host's silence limit catches it.
- `cancel()`: Joining, Joined, or Started: `CANCEL_ABORT_COPIES` copies of `ABORT(left)` to the host, `forget(host)`, Cancelled; other states: Cancelled, nothing sent. Cancelled, Refused, HostGone: `tick` still pumps the link harmlessly.

### 7. Why the lobby converges without an epoch (the Assumption's stop condition)

`ReliableLink` keeps a peer's `seq` state until `forget()`, and a frame carries no epoch. The host sends a guest no reliable frame before Start (`ACCEPT` is its first, `seq` 0) and the guest sends exactly one (`JOIN`, `seq` 0). So: (a) a guest that leaves with its `ABORT` delivered is forgotten by the host, and the guest forgets the host, so the next `JOIN` registers afresh; (b) if every `ABORT` copy is lost, the host keeps the old record, the new `JOIN` (`seq` 0) is a duplicate of it, is acked (`ack` 0 pops the new `JOIN`) and not offered, and the host still holds the seat the old `JOIN` took, so `ACCEPT` at `seq` 0 reaches the fresh guest, which expects `seq` 0; (c) a guest that restarts on its address is case (b); (d) a guest that restarts on a different address is a different handle, and its old seat is freed by the silence limit (10 s), during which the host answers the newcomer `ABORT(full)` if the seats are full; the guest returns to the list and joins again. A refused or departed guest is forgotten on both sides, so a retry never meets a stale `seq`. The one residue is a delayed copy of a guest's `JOIN` arriving after its `ABORT`: it seats a ghost, freed by the silence limit. If a test finds a path these four cases do not cover, HALT for the owner (a `Protocol` epoch).

### 8. Existing code and guards

No existing function is moved or rewritten. `Roster::nearby` follows `Roster::pass`'s clamp (`seats` above `MAX_SEATS` clamps the mask). `FakeLink::restart()` calls the existing `end()` first, so `end()`'s idempotence and its queue purge stay.

### 9. Tests

`LobbySupport.h`: `Rig` (a `ManualClock`, a seeded `FakeLinkNet`, `advance(ms)` stepping 10 ms and ticking every registered lobby, `until(pred, maxMs)`), `FixedRandom` (a scripted `IRandom`), `RawPeer` (a `FakeLink` that decodes what it receives and puts hand-built frames, an oracle independent of the lobbies), `testGame()` (id "ttt", hash, `caps{api 1, maxSeats 2}`, `surface`, `apiPreview` true, seats 2..2), and a `hostile()` fault preset (30 % loss, 20 % duplicates, 30 % reorder, delay 20 ms, jitter 30 ms, reorder extra 100 ms). `LobbyTest.cpp`:
1. `RosterNearbyHasOneLocalSeat` -- mode, seats, local mask, api; `localSeat` out of range gives no local seat (no crash).
2. `VersionVerdictTable` -- hash differs; surface differs with `apiPreview` true / false and a peer api lower / equal / higher.
3. `LobbyGameLimits` -- `seatLimit` and `startSeats` for (min, max, caps) combinations, including a manifest max above `MAX_SEATS` and a negative host max; `setGameId` clamps.
4. `ASessionIsNeverZero` -- `FixedRandom` returns 0, 0, 7: session 7; all zeros: 1.
5. `HostAdvertCarriesTheGameTheLabelAndTheSeats` -- a `RawPeer` hears an `ADVERT` at once and every `advertMs`, fields exact, label opaque (bytes incl. 0x00), clamped at 16; seat changes advertise at once; none after Start or cancel.
6. `HostRefusesAForeignHashAndAForeignSurfaceJoin` -- raw `JOIN`s: `ABORT(version_mismatch)`, no seat, peer not held (peer table never fills over 20 strangers, `peerTableFull` 0).
7. `AFullHostAnswersAbortFullAndKeepsItsGuest`; `AStrangerSendingAcceptStateOrAdvertHoldsNoSlot`.
8. `RefusalAnswersAreRateLimited` -- 8 copies of a refused `JOIN` at once: one `ABORT`; after `refusalAnswerMs`, one more (crossplay `testJoinAnswersAreRateLimited`).
9. `StartNeedsASeatedGuest` -- `NotEnoughSeats`, nothing on the wire; after a guest cancels or goes silent, again.
10. `AGuestsCancelFreesItsSeat` and `ASilentGuestFreesItsSeat` -- events `GuestLeft` / `GuestSilent`, seat count, `forget` effect (a fresh `JOIN` seq 0 seats again), ADVERT count.
11. `ThreeSeatsGiveContiguousSeatsInArrivalOrder` -- guest A leaves between B and C joining: seats 2 and 3 at Start, `ACCEPT`s exact.
12. `HostCancelSendsAbortLeftThriceAndStopsAdvertising`; `GuestCancelSendsAbortLeftThrice`.
13. `GuestListsOnlyItsGameWithVerdictsLabelsAndSeats`, `AHostOnAnotherProtoIsNeverListed` (patch the `proto` byte of an encoded `ADVERT`), `ListedHostsExpire`, `ANewSessionForTheSameHandleReplacesTheEntry`, `TheTableDropsExtraHostsWhenFull`.
14. `JoinRefusesADifferentVersionWithoutSending` (net `carried` unchanged), `JoinOfAnUnknownHostIsRefused`, `JoinWhenNotBrowsingIsRefused`.
15. `GuestReportsFullVersionMismatchHostLeftAndHostSilence` -- raw host scripts each: states, reasons, `forget` (a later `backToList` and `join` sends a `JOIN` at `seq` 0), `backToList`.
16. `GuestIgnoresFramesOfAnotherSession`, `HostIgnoresFramesOfAnotherSession` -- ABORT, ACCEPT, JOIN, ADVERT framed with a foreign session: no state change; `badFrames` counted.
17. `AnAcceptOutsideTheRangeIsRefused` -- seat 1, seat above n (decode already refuses), n above `MAX_SEATS`.
18. `HandOffForwardsAfterStartAndLatchesBefore` -- before `attach`: `State` is `NoRoom` (resent, delivered after `attach`), an `Abort` and a silence are latched and replayed once on `attach`; after: frames reach the sink; frames from strangers do not.
`LobbyHandshakeTest.cpp` (host and guest over `FakeLink`, `Rig`):
19. `AHostAndAGuestReachAStartedRosterForManySeeds` -- 200 seeds, hostile: roster (seat 1 / 2, n 2, mode Nearby, local masks, api, session equal, `peerForSeat`), within 5 s fake time, no `ADVERT` after Start.
20. `TwoGuestsReachThreeSeatsInJoinOrder` -- 100 seeds, hostile, `maxSeats` 3.
21. `ASecondGuestAtTwoSeatsGetsFullAndTheFirstStarts` -- 100 seeds.
22. `ACtxApiIsTheLowestLevel` -- guest `caps.api` lower than the host's and the reverse (host and guest rosters).
23. `TheHostsCancelReachesTheWaitingGuest` (reason Left, perfect link; hostile: gone within the silence limit, reason Left or Silent) and `TheHostsSilenceReachesTheWaitingGuestAtTheLimit` (the host stops ticking: Silent at the limit after the last frame, not before).
24. Crossplay regressions: `ALostJoinIsResentUntilTheMatchStarts` (50 % loss, 100 seeds), `OneWayRangeStillConverges` (guest to host blackholed for 2 s, then host to guest blackholed for 2 s after the guest joined, then clear), `AHostHeardBroadcastingAgainAfterStartEndsTheMatch` (a raw same-session `ADVERT` from the host's address: ignored inside `advertGraceMs`, ends the match after; and no false end across 100 hostile seeds from late copies), `ARestartedGuestIsCaughtByTheHostsSilence` (guest link restarted without `ABORT` after Start: the host's hand-off sink sees the silence at 10 s), `JoinsFromMatchedDevicesNeverEcho` (after Start: 100 rounds of 8 duplicate raw `JOIN`s of the seated guest and of a stranger over 3 s: `carried` under a stated budget, `setCarriedBudget`, no `ABORT` to the seated guest, at most one per `refusalAnswerMs` to the stranger, both matches stay Started).
25. `AGuestThatLeavesAndRejoinsReachesAStartedRoster` -- 100 seeds hostile: (a) cancel with the `ABORT` delivered; (b) cancel with the guest-to-host direction blackholed for the three copies, then restored; (c) cancel and a silent host-side freeing after 11 s; each then a fresh `GuestLobby` on the same link joins and the host starts.
26. `AGuestThatRestartsOnItsAddressRejoins` (`FakeLink::restart`, no `ABORT`, started within 3 s, the host's seat count never above 2) and `AGuestThatRestartsOnAnotherAddressRejoinsAfterTheSilenceLimit` (a new `FakeLink`: first `Refused(Full)`, retry every second with `backToList`, started by the silence limit plus 3 s; the retries never start earlier than the limit minus the time since the old guest was last heard).
27. `ARefusedGuestCanRetryWhenASeatFrees` -- second guest Refused(Full), first leaves, `backToList` and `join` again: started (the `forget` on refusal).
28. `AStrangerWithTheSessionIdCannotExhaustTheHostsPeers`.
Mutants (each applied alone, host suite run, reverted, red tests recorded): no `forget` on a leaving guest; no `forget` on a refusal; no `forget` on the guest side; ADVERT not stopped at Start; the grace window removed; the refusal rate limit removed; seats given at JOIN time (not compacted); `start()` without the seat check; the version rule's surface clause dropped; `maxPeers` lowered by one.

### 10. Test doubles

- `FakeLink::restart()` stands in for a device that brings its radio up again on the same address (ESP-NOW: the same MAC, so its peers keep their handle for it; `UdpLink`: the same port when it can rebind). It ends the link as `end()` does, then reopens the same endpoint with an empty handle table; in-flight copies addressed to it are discarded (the device was down). More permissive than a device: no boot time, and a peer's handle for it never changes (a rebind to another port would change it, which the new-`FakeLink` test covers). `FakeLinkTest` pins: handles the peers hold stay equal, the restarted link's old handles are dead and its new ones differ, traffic flows both ways after, the contract (`expectLinkContract`) holds for a restarted pair.
- `RawPeer` stands in for a remote device speaking `Protocol` by hand, an oracle independent of the lobbies. `FixedRandom` stands in for the device's `IRandom` (scripted values). The lobby tests' timing bounds are fake milliseconds from `LobbySettings`, not host speed; a bound derived from a device ratio is labelled an estimate.

## Verification

**Commands** (host suites and fast checks during implementation; firmware checks once, after the review and its patches, before the commit):
- `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j'` -- expected: all pass; baseline count recorded in Implementation Notes. The new suites alone under `--gtest_repeat=20`, and ASan/UBSan (outside CMake) over `LobbyTest`, `LobbyHandshakeTest`, `FakeLinkTest`, `ReliableLinkTest`, `ProtocolTest` with the lobby sources.
- `python3 scripts/check_layers.py`, `python3 scripts/check_layers_test.py`, `python3 scripts/check_upstream_touches.py`, `./bin/clang-format-fix` twice then `git status` -- expected: pass, second run changes nothing.
- Mutants (Design Notes 9) -- each makes a named test red.
- `export PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`, under `flock /tmp/crosshatch-build.lock`: `pio run -e x4pro`, `pio run -e default`, `pio check` (default and `-e x4pro`, all three `--fail-on-defect` gates), `python3 scripts/check_flash_budget.py` `build on`, `build off`, `compare`, `objects` -- expected: all exit 0. Size cost = games on minus off at the baseline commit `3a36b97a` (measured in this worktree before any change) against the same at this story's commit; expected about +0 B (unreferenced code), `objects` reports no static initializer in the lobby files.
- No simulator screenshots (no screen) and no fresh-clone gate run (no CI gate or workflow change).

**Results** (final tree, after the review patches; commit below): host suite 2,231 of 2,231 (`ctest -j`; baseline 2,164); `Lobby*` and `FakeLink*` 103 tests, three runs in a row; ASan/UBSan (outside CMake) 161 of 161; `check_layers.py` (654 edges), `check_layers_test.py`, `check_upstream_touches.py` pass; `./bin/clang-format-fix` twice, the second changed nothing, no formatting change outside this entry's paths; mutants (Implementation Notes) each red and reverted. Firmware, under the build lock with the shared cache (log `6.6/fw.log` in the scratchpad): `check_flash_budget.py` `build on` (`pio run -e x4pro`, the lobby sources compiled), `build off`, `compare`, `objects` exit 0, `objects` "No problems"; `pio run -e default` SUCCESS; `pio check` default and `-e x4pro` "No defects found" with all three `--fail-on-defect` gates. Size, measured the same way at the baseline `3a36b97a826946c41b4365fe5ed309ec850ed15b` (before any change) and on this tree (incremental, shared cache, build lock, this worktree): baseline games on 6,213,808 B, off 5,955,312 B, +258,496 B; static internal RAM 190,248 B on, 189,464 B off, +784 B. This tree: 6,213,808 B on, 5,955,312 B off, +258,496 B; 190,248 B and 189,464 B, +784 B. This story's own cost: +0 B flash, +0 B static RAM (the new code is unreferenced from firmware). Over the epic base (+258,048 B, +784 B): +448 B flash, +0 B static RAM, all entry 6.1's, so 9,056 B flash and 160 B static RAM of the epic share remain. Size is taken before the commit that carries these edits; the commit changes no `lib` or `test` file after that build.

**Follow-up pass results** (2026-10-12, after the follow-up patches; the commit follows this edit and changes no `lib` or `test` file after these runs): host suite 2,236 of 2,236 (`ctest -j`; baseline 2,164, this story adds 72); `Lobby*` and `FakeLink*` 108 tests, `--gtest_repeat=20`, 20 of 20; ASan/UBSan (outside CMake) 166 of 166; `check_layers.py`, `check_layers_test.py`, `check_upstream_touches.py` pass; `./bin/clang-format-fix` run again, nothing new in `git status`, no formatting change outside this entry's paths; the five mutants of Implementation Notes each red and reverted. Firmware, under the build lock with the shared cache (log `6.6/fw2.log` in the scratchpad): `check_flash_budget.py` `build on`, `build off`, `compare`, `objects` exit 0, `objects` "No problems"; `pio run -e default` SUCCESS; `pio check` default and `-e x4pro` "No defects found" with all three `--fail-on-defect` gates. Size, measured the same way (incremental, shared cache, build lock, this worktree) on this tree and at the baseline `3a36b97a` (6,213,808 B on, 5,955,312 B off, +258,496 B; static internal RAM 190,248 B on, 189,464 B off, +784 B, Implementation Notes): this tree games on 6,213,808 B, off 5,955,312 B, +258,496 B; 190,248 B and 189,464 B, +784 B. This story's own cost: +0 B flash, +0 B static RAM (the lobby is unreferenced from firmware). Over the epic base (+258,048 B, +784 B): +448 B flash, +0 B static RAM, all entry 6.1's.

## Auto Run Result

**Status `built`** (full route; review `thorough`, four context-free lenses, first pass 2026-10-12). Baseline `3a36b97a826946c41b4365fe5ed309ec850ed15b`, branch `epic-play-nearby-lane-a`; the commit is the branch tip with the subject `feat: add the nearby-play lobby handshake in GameCore`.

**Summary.** `GameCore::HostLobby` and `GuestLobby` (new `lib/GameCore/HostLobby.*`, `GuestLobby.*`, shared `Lobby.*`) speak R3 over `ReliableLink`. The host draws a `session` from `IRandom`, advertises the game, label, and seats every 500 ms (and at once on a seat change), seats guests in `JOIN` arrival order (compacted at Start, so seats are contiguous), answers an incompatible `JOIN` `ABORT(version_mismatch)` and a full or late one `ABORT(full)` (one answer per refused peer per 200 ms), frees a seat when a guest cancels or goes silent, and Start (at least `max(2, seats.min)` seats) sends `ACCEPT {seat, n}` and stops `ADVERT`. The guest lists the hosts of its game with a verdict (joinable or a different version: package hash, or `surface` while a level is a preview), joins, waits, and reports the host's refusal, cancel, or silence; a cancel sends `ABORT(left)` three times. A started lobby hands the match its `Roster::nearby` roster (new factory; `ctx.api` the lowest level the lobby can know), `seat`, `peerForSeat`, the `ReliableLink`, and a `handOff(Sink&)` that forwards what arrives after Start. Every lobby timing is a `LobbySettings` field. `ReliableLink::forget()` is called for a guest that leaves, is refused, or goes silent and for the host on the guest side, which is all convergence needs: the leave-and-rejoin cases (ABORT delivered, all copies lost, silence) and a restart on the same address all reach a started roster, and a restart on another address does after the old seat's silence limit, with no `Protocol` or `ReliableLink` change (Design Notes 7, verified by `LobbyHandshakeTest`). `FakeLink::restart()` stands in for a device that comes back on its address. 67 new host tests (63 `Lobby*`, 4 `FakeLinkTest`).

**Files.**
- `lib/GameCore/Lobby.h/.cpp`, `HostLobby.h/.cpp`, `GuestLobby.h/.cpp` -- the lobby (new; crossplay source files named in comments).
- `lib/GameCore/Roster.h/.cpp` -- `Roster::nearby`.
- `test/game_core/LobbySupport.h`, `LobbyTest.cpp`, `LobbyHandshakeTest.cpp` -- the suites (new); `FakeLink.h`, `FakeLinkTest.cpp` -- `restart()`; `CMakeLists.txt` -- registration.
- This plan.

**Review.** 36 findings (BH 13, EC 10, VG 7, IA 6): high 0, medium 12, low 19, false 5. Patched: the guest trusting an out-of-range or unsupported ACCEPT (medium), a Closed host re-seating a forgotten guest's `JOIN` (medium), `ok()` for a game that can never start, a shared `elapsed()`, comments, and the missing tests (a `State` before ACCEPT, clock stepped back, event ring, cancel from Joining or Started, non-default settings, a three-level roster, carried budgets). Deferred 3 (below). Rejected with reasons in the log: unvalidated settings, the brittle 4,000 ms bound (fixed seeds), unused advert fields, the event-queue drop counter, the backward-clock code change, a restart on a new address ending Refused(Full), and the descriptive intent-alignment rows (false).

**Follow-up review recommended: true.** Twelve medium findings were raised and the state-logic ones (the ACCEPT range fault, the Closed host's refusal) were patched by the same context that wrote the lobby and checked by mutants only. The unverified risk is the lobby's behaviour around a guest that departs with its `ABORT` lost or delayed (a ghost seat, deferred item 2) and whether the new HostGone path for a bad ACCEPT interacts with a host that resends it.

**Deferred** (frontmatter `deferred`): a newer peer's level taken as a preview (mixed frozen levels; needs a wire flag or an owner decision); the ghost seat for a departed guest until the silence limit; the untested `LinkRefused` and `LinkFailed` branches.

**For the orchestrator (outside this entry's `touches`).** (1) Spine AD-13 "As built": the lobby sends no reliable frame to a guest before Start (so `ACCEPT` is `seq` 0), a refused or departed guest is `forget`-ten on both sides, `ABORT(left)` goes out three times, a refusal answer is one per peer per 200 ms, the ADVERT interval is 500 ms and a guest drops a host not heard for 3,000 ms, a same-session `ADVERT` more than 1,000 ms after `ACCEPT` ends the match, and a guest's `ctx.api` is the lower of its own and the host's level. All are `LobbySettings`; R12 confirms them on a radio. (2) Entry 8 attaches `NearbySession` with `handOff(Sink&)` and reads `ReliableLink::unacked()` for its flush; entry 10's screens read `HostLobby::poll`, `seatsTaken`, `canStart`, `GuestLobby::host(i)`, `state`, `refusal`, `gone`, and must start only on a live guest. (3) Epic Notes: this entry's size cost is +0 B flash and +0 B static RAM, measured like for like above; the epic growth over the base is still +448 B flash, +0 B static RAM.

**Verification.** See the plan's Verification, Results. No simulator screenshots (no screen) and no fresh-clone gate run (no CI gate or workflow change). No formatting-only change outside this entry's paths.

**Residual risks.** Lobby timings (500 ms, 3,000 ms, 1,000 ms, 200 ms) are fake-millisecond values, unmeasured on a radio (R12). A departed guest can hold a seat for up to the silence limit (deferred item 2). A restart on a new address needs the screen to offer the list again. `ReliableLink` heap (about 3 KB plus 260 B per peer slot) is allocated when a lobby opens, from internal heap on the device.

**Follow-up review pass (2026-10-12), final status `built`.** Four context-free lenses (`blind-hunter`, `edge-case-hunter`, `verification-gap`, `intent-alignment`) ran again as subagents over `git diff 3a36b97a..8abb9271` of `lib` and `test`; the findings are triaged in the second Review Triage Log entry. 33 findings: high 0, medium 7, low 20, false 6. Patched (one commit on top of `8abb9271`): the guest that ends the wait on an ACCEPT its own device cannot seat now tells the host with `ABORT(left)` (medium); four missing tests (a stranger's ABORT to a waiting guest, a mid-list host expiry, the lobby's own late-ADVERT end before attach, the refusal ring with interleaved strangers) and one end-to-end test of the ABORT above (each guarded by a mutant that goes red); a stale `ok()` comment; missing test includes. Deferred 4 more (frontmatter `deferred`, seven in all): the host's single latched end with three or more seats, a delayed `ABORT(left)` removing a rejoined guest's seat, the post-Start same-session `ADVERT` rule that a real `HostLobby` never triggers, and a guest's `ctx.api` differing from the host's at three or more seats with mixed levels. Rejected with reasons in the log: the unchecked `minApi` (honest peers cannot reach it), the duplicate `elapsed()` in `ReliableLink.cpp` (outside the entry), the burst of ABORT copies (crossplay does the same), literal test bounds, comments citing the plan, 3-seat coverage, the weaker guest `ok()`, an older-session ADVERT, a stale entry at join, and the descriptive intent-alignment rows. A game id over 32 bytes is cut (BH11) was false.

**Follow-up review recommended: false.** This pass patched no `high`; the one state-logic patch (the ABORT on an out-of-range ACCEPT) is pinned by two tests and a mutant. Patched counts by verdict: medium 4 (VG1, VG2, VG3, EC1), low 3 (VG4, VG other, BH14), plus the carried BH10.

**For the orchestrator.** (1) Two items for the spine's AD-13 "As built" besides the first pass's: a guest whose ACCEPT is out of its range sends `ABORT(left)` before it forgets the host, and the host's `ctx.api` rule at three or more seats with mixed levels needs a level in `ACCEPT` (deferred item, owner decision). (2) Entry 10's screens should not offer Start on a guest they cannot show as live (first-pass item 2, unchanged). (3) This entry's size cost is still +0 B flash and +0 B static RAM like for like; the epic growth over the base is +448 B flash, +0 B static RAM.
