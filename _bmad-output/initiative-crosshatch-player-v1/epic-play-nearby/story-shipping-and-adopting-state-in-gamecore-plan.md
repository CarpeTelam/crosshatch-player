---
title: 'Shipping and adopting STATE in GameCore'
type: 'feature'
ticket: '7'
created: '2026-10-12'
baseline_revision: 'affc2feef4ec99c6dd7c08f34c247ed94f1bc3a8'
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
deferred: []
---

<intent-contract>

## Intent

**Problem:** Entries 8, 9, and 15 build on the match half of the protocol, and none of it exists: `Session` runs only before `start` (`restore`), hands the link nothing after a move, and nothing holds the peer's `STATE` between the `GameLink` task and the VM or latches an `ABORT` or silence. In the mover-applies model (R4) a device's `Session` must adopt its peer's `STATE` mid-match, under rules only `Session` knows, and the policy for the queue and end slot between the tasks must be fixed before the cross-task code is written.

**Approach:** Add to `Session` a mid-match `adopt` beside `restore` and a pulled shipment of each snapshot a partly-remote roster makes; add `StateQueue` (the R5 policy over an injected `IStateStore`), `EndSlot` (the latched reason), and `MatchSink` (the `ReliableLink::Sink` that joins them to the link) as new `lib/GameCore` files; test it all over lossy `FakeLink` with a fake `IGameRules`. **This plan's Design Notes 1 to 5 are the owner's checkpoint: the same dispatch implements them once the owner's answers come back as dated Decisions.**

## Boundaries & Constraints

**Always:** `lib/GameCore` includes only std, `lib/Memory`, and `lib/JsonParser` (`scripts/check_layers.py`), with no allocator call, no log, and no static state; every buffer the queue uses is the injected store's. Nothing outside `Session` tracks whose turn it is (AD-11). A refusal changes nothing. `SessionTest`'s tests stay as they are, solo and pass behave exactly as today, and `Session` grows by a few bytes (it must still fit the VM arena's reserve: `LuaGame.cpp`'s static check). Code adapted from crossplay names its source file. Test doubles name the device behaviour they stand in for (Build-agent brief; Design Note 6).

**Never:** `MOVE` or `REJECT` on the wire; a second session or rules call on the receiving side; a call to `IGameRules::status` for control flow after an adopt (AD-9); any `src/**`, `lib/GameScript/**` (so `MatchRounds` is entry 9's), `games/**`, upstream file, or `freeink-sdk` change; the PSRAM storage, the cross-task mutex, the `GameLink` task, or any `LINK` log line (entry 8's); relaying `STATE` between guests (more than two seats, spine Deferred). No spine edit: the AD-13 as-built line is the orchestrator's, from this plan's Auto Run Result.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Seat 1 starts the round | host `start()`; guest has no state | host `pendingShipment` = {ver 1, status, snapshot}; guest `adopt` of it = `RoundStarted`, ver 1, status gates input | none |
| Peer moves | guest status turn 1; STATE from seat 1, ver newer | `Adopted`; ver, status, snapshot replaced; `over` delivered once if shipped over | none |
| Keep-turn run | three STATEs from one mover, each turn = the mover | each `Adopted` in order; replaced ones skipped | none |
| Peer STATE while the turn is ours | guest status turn 2, STATE from seat 1 | `OutOfTurn`, nothing changed | acked at the queue, refused at `adopt` (R2) |
| Old, equal, or non-peer STATE | ver not newer; `fromSeat` local or no seat; sender a stranger handle | `Stale` / `NotPeer`; a stranger is `Refused` at `MatchSink` | acked, never offered again |
| Round ends, next round at once | queue holds `over` STATE (ver v) then seat 1's STATE (v + 1) | `Adopted` (Over delivered once), then `RoundStarted`; a newer non-over STATE replaces v + 1, never the `over` | none |
| Local move with a peer STATE queued | status turn = peer, tap | move discarded, `apply` never called; later the queued STATE is adopted | none |
| Rejected move | local `apply` rejects | `rejected` event on this device; no shipment, no frame | none |
| Queue full | 4 entries, tail ends a round, a fifth STATE | `Full`, overflow counted, `MatchSink` answers `NoRoom` (unacked); resend delivers it once room exists | nothing lost |
| End slot | ABORT (any reason) or silence, consumer stopped | first reason latched; later ones counted and ignored; read once by `take` | never dropped |

</intent-contract>

## Code Map

- `lib/GameCore/Session.h`, `Session.cpp` -- `restore` (before `start`), `start`, `handle` (turn gate), `applyPending`, `afterSnapshot` (status then `over` delivery), `keepState` (`++version`): add `adopt`, `hasState`, the shipment, `deliverOver` (extracted); update the `REJECT` comment (line 13).
- `lib/GameCore/Protocol.h` -- `Protocol::State {ver, status, snapshot}` is the shipment's shape (it includes `Session.h`, so `Session.h` cannot include it); `AbortReason` is the end slot's reason type. Not edited.
- `lib/GameCore/ReliableLink.h` -- `Sink`, `Verdict {Taken, Refused, NoRoom}`: `NoRoom` is the leave-unacked signal. `enqueue` replaces only the tail STATE, and not an `over` one; `send` is false when its 4-deep queue is full. Not edited.
- `lib/GameCore/Lobby.h` (`LobbyHandOff`) -- forwards a seated peer's State and Abort to the match's sink once attached, replays a latched abort or silence on attach; `HostLobby::peerForSeat` and `GuestLobby::peerForSeat` give `MatchSink` its peer table. Not edited.
- `lib/GameCore/SeatShown.h`, `Roster.h` -- `isLocal`, `localSeatCount`, `firstLocalSeat`, `Roster::nearby`. Not edited.
- New: `lib/GameCore/StateQueue.{h,cpp}`, `EndSlot.h`, `MatchSink.{h,cpp}`.
- `test/game_core/SessionTest.cpp` (unchanged); new `SessionAdoptTest.cpp`, `StateQueueTest.cpp`, `MatchSinkTest.cpp`, `NearbyMatchTest.cpp`, `MatchSupport.h`; `CMakeLists.txt` lists them and the new sources. `FakeLink.h`, `LobbySupport.h` (`ManualClock`, `Faults`, `hostile()`) are reused.
- `test/game_script/CMakeLists.txt`, `harness/match.cmake`, `pass_and_play.sources.cmake`, `games_check.cmake` -- list `Session.cpp`, which gains no dependency, so no line changes (a change would be reported, not guessed).
- `docs/crosshatch/game-canvas.md` -- Overlays (lines 139 to 147): the wording in Design Note 6.
- Reference, read-only: crossplay `0a1d17f` `LinkSession.h`/`.cpp` (`submit`, `takeIncoming`, the three decisions) and `host-tests/link/test_link.cpp` (`testSoak`, `testStaleStateIsIgnored`, `testSubmittingBeforeTakingDeliveryIsRefused`).

## Tasks & Acceptance

**Execution:**
- [ ] `lib/GameCore/Session.h`, `Session.cpp` -- add `PeerState`, `Adoption`, `Shipment`, `adopt`, `hasState`, `shipmentPending`, `pendingShipment`, `shipped`; guard `handle` and `draw` for no state; mark the shipment after `start`'s setup path and an accepted `applyPending` that returned Ok; extract `deliverOver`; update the `REJECT` comment -- Design Notes 1 to 3.
- [ ] `lib/GameCore/StateQueue.{h,cpp}`, `EndSlot.h` -- the queue policy and the latched slot -- Design Note 4.
- [ ] `lib/GameCore/MatchSink.{h,cpp}` -- the `ReliableLink::Sink` over them -- Design Note 4.
- [ ] `test/game_core/SessionAdoptTest.cpp`, `StateQueueTest.cpp`, `MatchSinkTest.cpp`, `NearbyMatchTest.cpp`, `MatchSupport.h`, `CMakeLists.txt` -- Design Note 6's tests, a mutant per rule, each red.
- [ ] `docs/crosshatch/game-canvas.md` -- Overlays wording, Design Note 6.

**Acceptance Criteria:**
- Given two sessions with one local seat each over a lossy `FakeLink` and fake rules, when they play rounds to over for many seeds, then both end with identical snapshots, ver, and status, each move applied (`apply`) on its mover's side only.
- Given a queued turn-passing peer `STATE`, when a local tap arrives, then the move is discarded and `apply` is never called on the stale state.
- Given an `over` `STATE` followed at once by seat 1's next-round `STATE`, when the other device drains its queue, then it reaches Over (one `over` event) before the new round starts.
- Given a `STATE` from a non-peer, with an older or equal `ver`, or while the turn is ours, when it is offered, then it is never adopted, and a `STATE` across the u16 wrap is adopted as newer.
- Given 200 matches over randomly hostile links, when each plays two rounds, then every match agrees, makes at least 20 moves, and carries frames under Design Note 6's bound.
- Given a queue holding an `over` `STATE`, when newer `STATE`s arrive, then it is kept with the newest later one, and when full the frame is left unacked and delivered on a resend with nothing lost.
- Given any `ABORT` reason or silence, when the consumer is stopped, then the end slot holds the first reason once, and `take` returns it exactly once.
- Given the existing suites, when run, then `SessionTest` and every solo and pass test pass unchanged.

## Implementation Notes

- 2026-10-12, implemented as the plan reads (one dispatch, C1 to C10 as recommended). Host evidence: full `ctest` 2301/2301 (2236 before, plus 65 new, none removed); the four new suites 20 times with `--repeat until-fail:20`, all pass; each of the nine named mutants applied alone is red (turn rule: 5 tests; ver check: 6; Busy: 2; RoundStarted for any sender: 2; replacing an `over`: 7; Full acked: 2; end slot overwritten: 5; shipment cleared early: 3; status recomputed: 6).
- Soak frame bound, measured (200 matches, loss 0 to 35 %, duplicates 0 to 30 %, reorder 0 to 30 %, delay 0 to 40 ms, jitter 0 to 60 ms, 10 % rejected moves in half of them, two rounds, `net.carried()` counting every queued copy): the largest carried count was 109 frames, the largest ratio to the plan's estimate `4 + 2 * (T / keepaliveMs + 1) + 12 * S` was 0.261, the fewest moves in a match 27, the slowest match 12,430 fake ms. The estimate holds with room, so the test asserts half of it, `2 + (T / keepaliveMs + 1) + 6 * S` (1.9 times the measured maximum, inside the plan's "never beyond twice it"). Fake time only; no host speed is used.
- `MatchSink` carries `using Verdict = ReliableLink::Verdict`. It does not call `ReliableLink::forget()` for a stranger it refuses (`Sink` says a sink that turns a stranger away calls it): the plan gives `MatchSink` no link reference, so the stranger keeps a peer slot until entry 8 decides where the forget lives (`MatchSink::Stats::strangers` counts them).
- `Session` grew by one `bool` (`unshipped`), which sits in existing padding after `restored`; `LuaGame.cpp`'s reserve check is a firmware-build check and is unmeasured here.

## Plan Change Log

- 2026-10-12, resumed after the owner's checkpoint: the epic Notes carry the Decision (owner, 2026-10-12, entry 7's plan checkpoint; Design Notes 1 to 5 approved as written; C1 to C9 as recommended; C10: one dispatch). No section changed. `baseline_revision` moves from `6f6ad0d5` to `affc2fee` (HEAD at resume) so the review diff holds this build's work only: the commits between them change four `_bmad-output` docs (the epic file, `tickets.toml`, the spine, SPEC) and no source, so Design Note 8's base measurement at `6f6ad0d5` holds at `affc2fee`.

## Review Triage Log

### 2026-10-12 — Review pass
- lens method: four context-free subagents (blind-hunter, edge-case-hunter, verification-gap, intent-alignment), each started in one message and awaited; the diff given to them (`review-diff.patch`) leaves out this plan file, which the edge-case lens read as its claims file. The implementation subagent had finished, so the three patches below were applied in the main session (small doc and one-function edits).
- verdicts: 26 findings (blind 15, edge-case 11, verification-gap 0; the intent-alignment audit is descriptive and has one row) — high 0, medium 0, low 22, false 4, maybe-false 0; each row below groups the findings that share a claim, and the Blind and Edge numbers are the order each lens listed them; 3 patched, 0 deferred, the rest rejected
- findings:
  - Blind 1 / Edge 1 `[low]` `[reject]` a device with no state reads a first wire ver of 32,768 or more as `Stale` — only reachable at 32,768 snapshots (a match makes a few hundred; Design Note 2 states it and `StaleIsAVerThatIsNotNewer` pins it), and a nearby match saves nothing (epic R4: no `resume.bin`), so no `restore` brings a high ver to a fresh guest; the fix would change a rule the owner approved (Design Note 1).
  - Blind 2 / Edge 2 `[low]` `[patch]` `StateQueue` has no stated threading contract though two tasks use it — real as a documentation gap (only `IStateStore` said it): added the contract comment above `class StateQueue` (`lib/GameCore/StateQueue.h`): no lock, the owner serializes every call (entry 8's handoff).
  - Blind 3 `[low]` `[patch]` an acked STATE is lost if the caller takes it before checking `pending()` and `adopt` answers `Busy` — plan precondition (Design Note 1) was missing from the header: added the sentence to `adopt`'s comment in `Session.h`.
  - Blind 4 / Edge 5 `[low]` `[reject]` `start()` while an `over` shipment is pending overwrites it — the owner's C6 (A) Decision defers Play again to the owner of the `Session` (the reference loop and a test do); a refusal would add a public outcome the Decision declined, and the window needs a peer gone for four frames. Carried in Residual risks for entry 9.
  - Blind 5 `[low]` `[reject]` a device that is not seat 1 can call `start()` — misuse by the caller; C9 (A) chose documentation (entry 9 uses `hasState()` / `RoundStarted`).
  - Blind 6 / Edge 3 / Edge 9 `[low]` `[reject]` the replace path returns `Invalid` (acked) when the store's buffer is smaller than the snapshot, the acquire path returns `Full` — only for a store that breaks `IStateStore::bytes`'s 1,400-byte contract; neither answer is right for a misconfigured store and entry 8's adapter owns it (`ok()` and the contract).
  - Blind 7 `[low]` `[reject]` overflow counter counts per refused push and `Stats` are plain integers — counted per refused push by Design Note 4 (a resend counts again) and tested; single-writer fields read under the owner's serialization (see Blind 2), so no race beyond the documented one.
  - Blind 8 / Edge 8 `[low]` `[patch]` `EndSlot::take` overwrites `out` on a false return — verified (`peek(out)` ran first): `take` now writes `out` only on true; `EndSlotTest` pins it (a failed take leaves `out` alone).
  - Blind 9 `[low]` `[reject]` `EndSlot`'s atomics on the C3 (RV32IMC) may not be lock-free — `PauseClock.h` already uses `std::atomic<uint64_t>` in `lib/GameCore` on the same targets, ESP-IDF supplies the `__atomic_*` fallbacks, and the `default` build below compiles the header; any fallback is a short critical section that never waits on the match's code.
  - Blind 10 `[low]` `[reject]` `adopt` trusts the snapshot bytes without decoding — C1 (A), approved: the status is trusted after a roster check; the codec validates when the rules next run, which AD-9 gives to the mover.
  - Blind 11 / Edge 4 `[false]` `[reject]` `push` replaces the tail without comparing `ver`, so an older STATE could overwrite a newer one — `ReliableLink` delivers each peer's frames in order and drops duplicates (`rxNext`), so an older frame cannot arrive after a newer one from the same seat, and `adopt`'s `Stale` check is the second guard.
  - Blind 12 `[low]` `[reject]` `handle()` and `draw()` return Ok with no state — Design Note 1 and the plan's task list; solo and pass call `start()` first and the unchanged `SessionTest` and the pinned `SoloAndPass...` test pass.
  - Blind 13 `[low]` `[reject]` the soak's frame bound is fitted to one run and a `printf` — the plan set the bound as at most twice the measured maximum and asked for the measurement in Implementation Notes; the bound is in fake time and the seeds are fixed.
  - Blind 14 `[low]` `[reject]` comments cite plan and entry numbers — the tree already cites spine and epic ids (`AD-13`, `R10`, `e6pre-3`); the plan's own wording for `game-canvas.md` used them.
  - Blind 15 `[low]` `[reject]` four test gaps (an ABORT while the queue is full, a handle of a seat this device plays, a duplicate `over` STATE, Ping and Ack at the sink) — an ABORT never reaches the queue (`MatchSink::onFrame` branches before `push`), a duplicate `over` is `Stale`, and `ReliableLink` consumes Ping and Ack before the sink (`ReliableLink.cpp:422`); each is a short-circuit visible in the code, and the listed verification is covered.
  - Edge 6 `[low]` `[reject]` a `ScriptError` or `Cancelled` from the `Over` input after an accepted move ships nothing — Design Note 3 states it ("a ScriptError or Cancelled ships nothing"); a script error ends the match through ABORT (entry 15), and `Cancelled` is a stopped VM.
  - Edge 7 `[low]` `[reject]` `adopt` after `restore` and before `start` leaves `restored` set — no caller does it: a nearby match saves nothing, and `restore` is solo and pass's.
  - (Edge 9, `push` never calls `ok()`, is in the Blind 6 row: `ok()` is the bring-up check of Design Note 4 and the adapter, entry 8, refuses a small store; a failing `push` is `Full`, counted.)
  - Edge 10 (claim check) `[false]` `[reject]` the docs say a newer STATE replaces the queued one but `push` compares no ver — the same as Blind 11: order comes from the link, and the doc's "newer" means later in link order.
  - Edge 11 (claim check) `[false]` `[reject]` a seat-1 STATE is adopted as `RoundStarted` while the local turn is ours — it needs the local status to be `over` (turn 0) or no state, so no turn is ours then; `OutOfTurn` holds for every mid-round status (`SessionAdoptTest`).
  - Verification-gap `[none]` returned "No verification gaps found" — 0 findings.
  - Intent-alignment (descriptive) `[false]` `[reject]` the intent's expectations are device-level and the diff's tests run them on a test-owned loop (`MatchPeer::step`) — the production loop is entry 8 and 9's (the intent's Never list excludes the `GameLink` task and the consumer); Design Note 3 names the reference loop as theirs to read. No defect.

## Design Notes

**Owner checkpoint.** Settled: the epic Notes' "Decision (owner, 2026-10-12, entry 7's plan checkpoint)" approves Design Notes 1 to 5 as written, C1 to C9 as recommended (the A options), and C10 as one dispatch (no split); this dispatch implements the plan as written. Sections 1 to 5 were what the owner approved; the orchestrator brings the answers back as dated Decisions, and the same dispatch then implements this plan as written, unless a Decision overrules a section. Every choice among readings is in section 5, with a recommendation and the alternative. The sources that settle the rest: epic Notes (Decisions of 2026-10-12: the mover applies; the round-ending `STATE` never replaced; `ABORT` consumes no `seq`; the entry 2 link contract; the rejoin `forget()` and ghost-seat Assumptions), R4 and R5 (lines 28 and 29), spine AD-8 (the 2026-10-12 `apply` line), AD-9 (amended), AD-11 (amended), AD-13 (the 2026-10-12 `ver` line and the mover-applies amendment), AD-17 (`ch.store` lands on the mover), AD-18 (the queue and end slot), AD-21 (Over and Play again), and `Lobby.h`'s `LobbyHandOff`.

### 1. The `Session` adopt call beside `restore`

```cpp
// A STATE the link brought from the peer, as adopt() takes it: Protocol::State's fields plus the sender's seat.
struct PeerState {
  // The sender's seat, which the caller derives from the link identity of the frame's sender
  // (MatchSink::seatOf), never from the payload (AD-11).
  uint8_t fromSeat = 0;
  uint16_t ver = 0;  // the wire ver: the low 16 bits of the sender's Session::ver()
  Status status;     // the status the sender shipped (AD-9); this device does not compute it
  std::span<const uint8_t> snapshot;
};

// What adopt() did. Adopted and RoundStarted adopt the state; every other value changed nothing.
enum class Adoption : uint8_t {
  Adopted,       // the peer held the turn: its move
  RoundStarted,  // seat 1 started a round: this device had no state, or its status was over
  NotPeer,       // fromSeat is no seat of the roster, or is a seat this device plays
  Busy,          // a local move is pending (handle() kept one that applyPending() has not taken)
  BadSnapshot,   // empty, or longer than SNAPSHOT_BYTES
  Stale,         // ver is not newer than this device's (u16, compared across its wrap)
  OutOfTurn,     // the sender neither holds the turn in this device's status nor starts a round
  BadStatus,     // a turn or winner that is no seat of the roster, or `over` with a turn / not over with winners
};

// In class Session, beside restore():
  // True once this device holds a state: after start(), or after restore() and start(), or after a first adopt().
  // A nearby device that is not seat 1 never calls start() (it runs no setup); until its first adopt it has no
  // state, and handle() and draw() do nothing and call no rules (the match shows "waiting for the host").
  bool hasState() const { return stateLength != 0; }
  // Mid-match, on the VM task, only between two input-to-apply cycles (never while pending()): makes the
  // peer's snapshot this device's state, and its shipped status this device's status, when the rules in
  // section 2 allow. `result` says what happened. The return value is the Over delivery's: Ok, or the
  // ScriptError or Cancelled of the input() call that took the `over` event (the state is adopted by then,
  // as after applyPending). A refusal returns Ok and changes nothing.
  Outcome adopt(const PeerState& peer, Adoption& result);
```

**Preconditions.** The caller checks `!pending()` before it takes a `STATE` off the queue (`Busy` is the guard, not the flow); `peer.fromSeat` is the link-identity seat, `peer.snapshot` stays valid for the call.

**Refusals, checked in this order, each changing nothing and returning Ok:** `NotPeer` (`!roster.isSeat(fromSeat) || roster.isLocal(fromSeat)`); `Busy` (`pending()`); `BadSnapshot` (size 0 or over `SNAPSHOT_BYTES`; `Protocol::decode` already bounds it, so this is a guard); `Stale`; `OutOfTurn`; `BadStatus` (`over` needs turn 0 and winners within the roster's seats; not over needs a turn that is a seat and no winners; the roster check `Protocol` cannot make).

- `Stale`: `int16_t(uint16_t(peer.ver - uint16_t(ver()))) > 0` is newer. A device with no state has ver 0, so any ver from 1 to 32,767 is newer.
- `OutOfTurn`: the sender holds the turn (`hasState() && !status().over && status().turn == fromSeat`), or it starts a round (`fromSeat == 1 && (!hasState() || status().over)`); neither is `OutOfTurn`. So a status that is over accepts only seat 1, a device with no state accepts only seat 1, and a mid-round STATE from seat 1 is a move, accepted only on seat 1's turn.

**Where it runs.** The VM task, with every other `Session` call; `Session` is confined to it (class comment). `adopt` is called from the consumer's step, which first checks `!pending()`, takes one entry from `StateQueue`, adopts it, and releases it (section 4).

**What it does to `ver`.** On adoption, `version += int16_t(wire - uint16_t(version))`: the local u32 counter takes the peer's value, keeping its upper 16 bits, so it never goes backwards and runs on through the wrap and through rematches. `settled = version` (the status is the shipped one, "computed" by the mover's rules). `ver()` is unchanged by a refusal. Local snapshots still do `++version` in `keepState` (the turn holder advances it; seat 1 advances it at a round start, `start()`).

**The shipped status.** `current` becomes `peer.status` and `rules.status` is not called (AD-9: "guests gate input and the end-of-round flow on that shipped status and never call `status` for control flow"). If it is `over` and the round's `over` was not delivered, `deliverOver()` delivers `Over` to each local seat once, as `afterSnapshot` does; a round start first clears `overDelivered`. A pending shipment older than the adopted state is cleared (a newer peer `STATE` means the peer saw ours).

**The local seat's gating.** Unchanged `handle()`: a move is kept only from the turn seat of `status()` (`!over && turn == seat && isLocal(seat)`) with none pending, so after an adopt the local seat is gated by the shipped status, and between a peer's `STATE` being sent and adopted the status still names the peer, which is what discards a tap made meanwhile (crossplay's `submit` refusal, in this model). Without a state (`!hasState()`) `handle()` and `draw()` return Ok without calling the rules; `start()` is unchanged.

**How it differs from `restore`.** `restore(snapshot, ver)` runs only before `start()`, takes the pair with no check, and leaves the status to `start()`, which calls `rules.status`, skips `setup`, and delivers `Over` if over. `adopt` runs mid-match or with no state, applies the ver, sender, and turn rules, takes the status from the wire, calls no `setup` or `status`, delivers `Over`, and reports a verdict. Both copy the snapshot (at most `SNAPSHOT_BYTES`) into `Session`'s own buffer. Neither is called for solo or pass.

**A refusal is not an error.** A refused `STATE` was acked at the queue (R2), so the sender does not resend it; `Session` counts nothing and the consumer may log the verdict.

**Guards in the code this plan moves** (`git log -L` on `afterSnapshot`: `a4212569`, `d45dd71d`, `17d02483`): `deliverOver()` is the tail of `afterSnapshot` moved verbatim, and keeps `overDelivered = true` before the loop (a failed delivery is not retried by a later call), the loop bound `seat <= seats.seats && seat <= Roster::MAX_SEATS`, `!isLocal → continue`, and the first non-Ok delivery returning at once so later seats get no `Over` (`AFailedOverDeliveryEndsTheStepAndLaterSeatsGetNoOver`). `afterSnapshot` keeps `settled = version` only when the status is Ok (`d45dd71d`: a call cancelled between the snapshot and its status leaves `settledVer()` behind, which the save step reads) and its early return on `outcome != Ok || !current.over || overDelivered`. `applyPending` keeps `moveLength = 0` before the outcome check and the rejected path's `reason` cut. `start()` keeps `moveLength = 0`, `overDelivered = false`, and the `restored` branch that skips `setup`.

### 2. The round-start rule

- **Who may start a round:** seat 1's device only, by `Session::start()` (round one) and by `start()` again (Play again), as in solo. The device that is not seat 1 never runs `setup` and never calls `start()`; its first state is the first `adopt`.
- **How seat 1 ships:** `start()` ends in `afterSnapshot()`; when that returns Ok after the `setup` path and the roster has a seat this device does not play, `shipmentPending()` is true and `pendingShipment` returns {ver, status, snapshot} (section 3). A round that is already over at `setup` ships an `over` STATE, never replaced. The `restore`+`start()` path ships nothing (both devices would hold the restored state already; used by the wrap test).
- **How the other device recognises it:** by its own state, not the content: `adopt` with `fromSeat == 1` while `!hasState() || status().over` is `RoundStarted`, whatever the STATE's snapshot (so a round whose first STATE was replaced by a newer one from seat 1 is still recognised). The device clears `overDelivered`, takes the state, and the consumer (entry 9) counts a started round from the verdict. A round whose first turn is seat 2 is the same STATE with turn 2.
- **`ver`:** one u32 counter per `Session` that only the turn holder advances (a move, `++version`), and seat 1 at a round start (`start()`'s `setup` snapshot, `++version`); it is never reset, so a rematch continues from the last ver and ver keeps rising across rounds. On the wire it is `uint16_t(ver)`. Newer is serial-number order (`int16_t` difference above 0), so the wrap at 65,535 is crossed without a reset, and a peer would have to be 32,768 snapshots ahead to look older (a match makes a few hundred).
- **The round-ending STATE is never replaced:** `ReliableLink` keeps an unacked `over` STATE and sends the next one after it (R2); `StateQueue` keeps a queued `over` entry and appends (section 4); so seat 1's Play again, shipped right after an `over` STATE, reaches the other device after it, which first adopts the `over` (one `over` event) and then sees `RoundStarted`. The consumer adopts at most one entry per VM step so each state, `over` included, is drawn before the next is adopted (entry 9's contract).

### 3. Shipping after an accepted local move

```cpp
// A snapshot this device made that the peer must be told of: Protocol::State's fields (Protocol.h includes
// Session.h, so the type is repeated; MatchSupport's pair rig builds a Protocol::Frame from it and the suites
// pin that the two agree).
struct Shipment {
  uint16_t ver = 0;  // uint16_t(ver()) of the snapshot
  Status status;     // the status computed for it (AD-9)
  std::span<const uint8_t> snapshot;  // aliases the Session's snapshot: valid until the next Session call
};

// In class Session:
  // True while a snapshot made here has not been handed to the link: set when start() runs setup, or applyPending()
  // accepts a move, and the call returns Ok (a ScriptError or Cancelled ships nothing), for a roster with a seat this
  // device does not play. Never set in solo or pass.
  bool shipmentPending() const { return unshipped; }
  // The latest snapshot to ship; false when none is pending. Latest wins: a second snapshot before shipped() replaces
  // the first, except that the owner never starts a round while an `over` shipment is pending (section 5, C6).
  bool pendingShipment(Shipment& out) const;
  // The owner handed the shipment to the link (ReliableLink::send returned true). Until then it stays pending and the
  // owner offers it again at its next step, so a send the link refused loses nothing.
  void shipped();
```

**When:** after every call that makes a snapshot here and returns Ok: `start()` (the `setup` path) and an accepted `applyPending()`; never after a rejection (the `rejected` event stays on the device and puts nothing on the link), after an `adopt`, or in solo and pass. **Who calls the link:** the owner of the `Session` on the VM task (entry 9's step, through entry 8's cross-task handoff to the `GameLink` task): `pendingShipment` → copy into the handoff slot → `shipped()`; the `GameLink` task builds the `Protocol::Frame {State}` and calls `ReliableLink::send(peer, frame)`; a false (queue full, lost peer) is an ordinary loss for every frame but a `STATE`, so the owner keeps the shipment pending and retries each step. The reference single-threaded loop is `MatchSupport.h`'s `MatchPeer::step`, which entries 8 and 9 read.

### 4. The queue policy, its storage, and the end slot

```cpp
// lib/GameCore/StateQueue.h
using StateHandle = uint8_t;                          // an index into the store: 0..capacity() - 1
inline constexpr StateHandle NO_STATE_HANDLE = 0xFF;  // capacity() is at most 255

// Port: a fixed pool of snapshot buffers for the state queue. The device adapter (entry 8) allocates it once at
// bring-up in PSRAM (never on the Wi-Fi task, never per frame); the host suites pass an array. Not thread-safe:
// whoever shares a StateQueue serializes every call on it and on its store.
class IStateStore {
 public:
  virtual ~IStateStore() = default;
  virtual size_t capacity() const = 0;      // buffers in the pool; fixed for the store's life
  virtual StateHandle acquire() = 0;        // a free buffer, or NO_STATE_HANDLE when none is free
  virtual void release(StateHandle h) = 0;  // returns a buffer; NO_STATE_HANDLE and a free one are no-ops
  // The buffer's Protocol::MAX_SNAPSHOT_BYTES (1,400) bytes; empty for NO_STATE_HANDLE or a free handle.
  virtual std::span<uint8_t> bytes(StateHandle h) = 0;
};

// One queued STATE; its snapshot is in the store under `handle`.
struct QueuedState {
  StateHandle handle = NO_STATE_HANDLE;
  uint8_t fromSeat = 0;  // the sender's seat, from its link identity (MatchSink)
  uint16_t ver = 0;
  Status status;
  uint16_t length = 0;
};

class StateQueue {
 public:
  static constexpr size_t DEPTH = 4;  // R5
  enum class Push : uint8_t { Queued, Replaced, Full, Invalid };
  explicit StateQueue(IStateStore& store);
  ~StateQueue();  // releases every handle it holds
  bool ok() const;  // store.capacity() >= DEPTH + 1 (four queued, one being adopted)
  // GameLink task. Queued: appended. Replaced: the newest queued STATE did not end a round and came from the same
  // seat, so its buffer takes the new one (latest wins). Full: DEPTH entries and the newest ends a round (or is
  // another seat's), or the store had no buffer: nothing changed, overflows() + 1. Invalid: empty or over 1,400 bytes.
  Push push(uint8_t fromSeat, uint16_t ver, const Status& status, std::span<const uint8_t> snapshot);
  // VM task. The oldest entry, removed: the queue forgets it and the caller owns out.handle until release().
  bool take(QueuedState& out);
  std::span<const uint8_t> bytes(const QueuedState& taken);  // the snapshot, `length` bytes
  void release(QueuedState& taken);                           // returns the buffer; taken.handle = NO_STATE_HANDLE
  size_t size() const;                                        // queued, not counting a taken entry
  uint32_t overflows() const;                                 // pushes that found it full, since construction
  void clear();                                               // teardown: releases every queued handle
};
```

- **A handle** is an opaque small index issued by the store; the queue compares it only with `NO_STATE_HANDLE` and passes it back; the bytes are the store's, valid from `acquire` to `release`. **Who owns memory:** the store owns the bytes for its life (entry 8: PSRAM, allocated at bring-up with `makeUniqueNoThrow`); the queue owns a handle from `push` until `take` (replacement reuses the tail's buffer, so a push after the first never allocates); the consumer owns a taken handle until `release`. Five buffers (7,000 B of PSRAM on the device) cover four queued and one being adopted. Metadata is a few dozen bytes inside the queue.
- **Policy:** only the newest queued entry can be replaced, and not when it ends a round (`over`): the same rule `ReliableLink::enqueue` applies on the send side, so order is kept (an older non-over entry before an `over` is never replaced, since the newer one would then precede the `over`). With `[over v10]` and a push of v11 the queue holds `[v10, v11]`; a push of v12 replaces v11. A full queue is only reachable with `over` entries at the tail, so overflow is rare by design (R5); it is counted per refused push, a resend of the same frame counting again.
- **Leave-unacked signal:** `MatchSink::onFrame` returns `Verdict::NoRoom` for `Push::Full`, so `ReliableLink` does not ack and does not advance `rxNext`; the peer's resend (400 ms) offers the frame again (`Stats::noRoom`). Nothing is lost.
- **The latched end slot**, `lib/GameCore/EndSlot.h`, header-only, lock-free (`std::atomic`), reasons are `Protocol::AbortReason` (`Left`, `ScriptError`, `Timeout`, `Full`, `VersionMismatch`), silence being `Timeout` exactly as `LobbyHandOff` already latches it:

```cpp
class EndSlot {
 public:
  // GameLink task. True when this call set the reason; false when one is already latched (the first wins): counted.
  bool latch(Protocol::AbortReason reason);
  bool latched() const;                       // any task: a reason has been latched (taken or not)
  bool peek(Protocol::AbortReason& out) const;  // the latched reason, still latched
  // The match's loop(): the latched reason, once. True once after a latch; later calls false. A later latch() stays
  // ignored after a take, so the reason the match acted on is the one the log shows.
  bool take(Protocol::AbortReason& out);
  uint32_t ignored() const;  // latch() calls after the first
};
```

- **`MatchSink`** (`lib/GameCore/MatchSink.{h,cpp}`), `final : ReliableLink::Sink`, constructed with `(StateQueue&, EndSlot&, std::span<const SeatPeer>)`, `struct SeatPeer {PeerHandle handle; uint8_t seat;}` (copied; from `HostLobby`/`GuestLobby::peerForSeat`). `onFrame`: a `State` from a peer goes to `queue.push` (`Queued`/`Replaced` → `Taken`; `Full` → `NoRoom`; `Invalid` → `Refused`); an `Abort` from a peer → `end.latch(frame.abortReason)` → `Taken`; a `State` or `Abort` from a handle that is no peer → `Refused`, `Stats::strangers`; `Join`, `Accept`, `Advert` → `Refused`. `onPeerSilent(peer)`: a peer → `end.latch(Timeout)`; a stranger → ignored. `seatOf(PeerHandle)` returns 0 for a stranger. It attaches behind the lobby with `handOff(sink)`, which replays a latched `Abort` or silence into it. No allocator, no log; `Stats` {queued, replaced, strangers} plus `queue.overflows()` and `end.ignored()` are what entry 8 turns into `LINK` lines.

### 5. Choices among readings (recommended unless the owner overrules)

- **C1 Status after an adopt.** (A, recommended) Trust the shipped status after checking it against the roster; never call `rules.status` (AD-9). (B) Recompute with `rules.status` and refuse on a mismatch: one more Lua call per adopt and a new refusal, catching a peer on other game code, which the package hash and `surface` checks already prevent.
- **C2 A pending local move when a STATE is offered.** (A) `Busy`, nothing changes; the consumer never takes an entry while `pending()`, so it cannot arise in the VM's single task. (B) `adopt` discards the pending move and adopts. A is stricter and cannot lose a taken entry the caller already removed from the queue, because the caller checks first.
- **C3 How a shipment reaches the link.** (A) Pull: `pendingShipment` plus `shipped()`, so a send the link refused is offered again and an `over` shipment is not lost. (B) Push through an injected `IStateOut` the `Session` calls itself: simpler call sites, but the callee must always accept, so the cross-task slot would need two entries for `over`.
- **C4 When a roster ships.** (A) When it has a seat this device does not play (`localSeatCount() < seats`): mode-agnostic, AD-11. (B) When `mode == Nearby`.
- **C5 Silence in the end slot.** (A) Silence is `AbortReason::Timeout`, as `LobbyHandOff` latches it; the view tells "left" from "silent" by reason. (B) A separate `Silent` value, so a received `ABORT(timeout)` (never sent today) differs from a detected silence.
- **C6 `start()` while an `over` shipment is pending.** (A) The owner defers Play again until `!shipmentPending()` (reference loop + test). (B) `Session::start()` refuses, which needs a new refusal outcome. A leaves `Session::start()` as today; the window needs the link to refuse four frames, i.e. a gone peer.
- **C7 Who filters strangers.** (A) `MatchSink` by link handle (`Refused`, acked) and `adopt` by seat (`NotPeer`), both tested. (B) `adopt` only. The sink-level filter keeps a stranger's frame out of the PSRAM queue.
- **C8 What replaces in the queue.** (A) Only the newest entry, same seat, not `over` (mirrors `ReliableLink`). (B) The newest non-over entry anywhere, which can put a newer STATE ahead of an `over`.
- **C9 A device with no seat 1.** (A) `start()` unchanged; that device never calls it (entry 9 uses `hasState()` and `RoundStarted`). (B) `start()` waits when seat 1 is not local, which the existing partly-remote tests (`MatchRoundsTest`, `SeatShownTest`) may contradict.
- **C10 One session or two.** (A, recommended) One dispatch. The estimate is about 450 production lines and 1,600 test lines, near entry 6.6's. (B) Split as `7a` = `Session` (adopt, shipment, `SessionAdoptTest`, a pair test over a recording sink) and `7b` = `StateQueue`, `EndSlot`, `MatchSink`, their tests, the soak, and the docs; `7b` after `7a`, since the entry's `unknown` names the queue and end slot as the split-off.

### 6. Tests, doubles, and the docs wording

- `MatchSupport.h`: `CounterRules` (stands in for `LuaGame`, the script adapter: state `{sum, turn, lastMover}`, a move adds `m & 0x7F` and keeps the turn on the high bit, `0xFF` is rejected, over at `limit` with the last mover the winner, `firstTurn` 1 or 2 at `setup`; `status` validates like `LuaGame`: a turn outside the seats would be a ScriptError; more permissive: no codec, no byte limits beyond `Session`'s; a test pins `status`, `apply`, and a rejection against the `IGameRules` contract the way `SessionTest`'s `FakeRules` is used); `ArrayStateStore` (stands in for the PSRAM pool: fixed buffers at construction, `acquire` never allocates, a double release is a failure, a leak count; more permissive: no PSRAM latency or allocation failure unless `denyAcquire` is set); `MatchPeer` (a `FakeLink` + `ReliableLink` + `MatchSink` + `StateQueue` + `EndSlot` + `Session` rig whose `step()` is the reference loop: `link.tick()`, `end.take`, `if (!session.pending())` take and adopt one entry, a scripted tap through `handle` and `applyPending`, then offer `pendingShipment` to `link.send` and call `shipped()` on true; it stands in for `GameLink` plus the VM task in one thread, more permissive than the device: no cross-task race).
- `SessionAdoptTest`: every verdict and its order, the u16 wrap on `ver` and the upper bits kept, `RoundStarted` for no state and for over, `Over` once per round, `BadStatus` cases, atomicity (a refusal leaves snapshot, ver, status, shipment untouched), a script error from the `Over` input after the adopt, `Busy`, shipment set after `start`/accepted move only, not after a rejection, a failure, an adopt, solo, or pass; a shipment stays pending until `shipped()`; `handle`/`draw` without a state call no rules.
- `StateQueueTest`: replace, keep `over`, append behind `over`, full with the tail an `over`, a different seat, no buffer (`denyAcquire`), counter per refused push, `take`/`release` leave no leaks, clear, `ok()` false below five buffers, `Invalid`.
- `MatchSinkTest`: verdicts per frame and sender, end slot per reason once, stranger handle ignored, silence of a stranger ignored, `NoRoom` then the resend delivered through a real `ReliableLink` with the queue drained between, handoff replay from a lobby.
- `NearbyMatchTest`: two `MatchPeer`s over `FakeLink` + `hostile()` for 100 seeds (rounds to over, identical snapshots, ver, and status; each move applied on its mover's side only, counted by `apply` calls per side; runs of keep-turn moves; first turn seat 2; a rematch with ver strictly rising; the `over`-then-next-round case; a rejected move with the net's carried count unchanged; a queued turn-passing STATE with a tap discarded and `apply` uncalled; a stranger and a stale replay not adopted; the wrap by `restore` on both at ver 65,533 and play across; a refused `link.send` retried until delivered, and the `over` shipment kept); the 200-match soak (loss to 35 %, duplicates to 30 %, jitter to 60 ms, two rounds, at least 20 moves each, agreement, frames at most `4 + 2 * (T / keepaliveMs + 1) + 12 * S` for `T` fake milliseconds and `S` STATEs shipped in that match: an estimate from the per-STATE worst case of resends at 400 ms and an ack each way, to be confirmed or revised by the measured maximum in Implementation Notes and never loosened beyond twice it). Every bound is in fake time; no host speed.
- Mutants, each applied alone and red: turn rule dropped; ver check dropped; `Busy` dropped; `RoundStarted` for any sender; replacement of an `over`; `Full` acked instead of `NoRoom`; end slot overwritten by a later reason; shipment cleared before the link took it; the shipped status recomputed.
- `docs/crosshatch/game-canvas.md`, Overlays, the paragraph from "Owner decision (R10, e6pre-3, 2026-10-04)" on is rewritten to say: the pause list (watchdog, timer poll, store flush) is unchanged; what must not pause is the link pump (draining the link, `ReliableLink` resends and acks) and the peer-silence detection, both on the `GameLink` task; the task hands the match two things and calls nothing in it: the peer's `STATE`s go into the four-entry `StateQueue` (a newer `STATE` replaces the newest queued one unless that one ends a round, and a `STATE` that finds it full is left unacked, so the peer resends it after the resend interval and nothing is lost, each overflow counted), and a lost peer or a received `ABORT` sets the `EndSlot`, which keeps the first reason until the match's `loop()` reads it once; the move to `PeerGone` and the adoption of queued `STATE`s wait for `loop()` and the VM, that is until the overlay closes, while the link and the detection do not; the last sentence ("The link is not built yet") becomes "`GameCore` builds the policy (entry 7); the `GameLink` task and the PSRAM storage are entry 8's".
- `Session.h`: the `REJECT_REASON_BYTES` comment becomes "A rejection's reason, as input() receives it in every mode; no REJECT frame crosses a link (a rejected move stays on its device, AD-13, 2026-10-12). The 64-byte cut is the event's."

### 7. Sizing and the split

One dispatch is recommended (C10). If the owner prefers the split, `7a` ends at `Session` plus `SessionAdoptTest` and a pair test over a recording sink; `7b` adds the rest. Either way the order inside the dispatch is the same: `Session` and its tests, then the queue and end slot, then the sink, then the pair rig and soak, then the docs.

### 8. Measurement

The firmware cost is expected near zero: nothing in `src/` calls `adopt`, the queue, the slot, or the sink, so the linker drops them, leaving only the `handle`/`draw` guards and the shipment flag in `Session` (tens of bytes, an estimate, unmeasured). It is measured like for like in this worktree: the base `6f6ad0d5` first (x4pro `check_flash_budget.py build on`, `build off`, `compare`, before any edit, under the build lock), then after the last review patch; the delta is the difference of the two. The orchestrator's latest figures (+258,496 B and +784 B, `244f3399`) are for another tree and are not subtracted from.

## Verification

**Commands:**
- Host suites (Ninja `build/test`; AGENTS.md): `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test'` then `ctest --test-dir build/test --output-on-failure -j` -- expected: every test passes, `SessionTest` and the solo, pass, and `MatchRounds` suites unchanged (`ctest -N` count before and after differs by the new tests only).
- `ctest --test-dir build/test -R 'SessionAdopt|StateQueue|MatchSink|NearbyMatch' --repeat until-fail:20` -- expected: all pass 20 times (seeded, so a failure is a defect, not a flake).
- Mutants (Design Note 6) -- expected: each red against its named test, then reverted.
- Fast checks: `python3 scripts/check_layers.py`, `python3 scripts/check_upstream_touches.py` (no upstream file is touched), `./bin/clang-format-fix` twice (`git add` the new files first) with `git status` clean afterwards.
- Firmware, once, after the review and its patches, under `flock /tmp/crosshatch-build.lock`, with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`: `pio run -e x4pro`, `pio run -e default`, `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` and the same with `-e x4pro`; the flash and RAM delta of Design Note 8 -- expected: builds and checks succeed, delta reported with its method.

**Manual checks (if no CLI):**
- Read `Session.h`'s class comment and the `game-canvas.md` paragraph: no "ESP-NOW link pump", no "through the session's queue" for a lost peer, no REJECT-on-the-wire wording remains.

## Auto Run Result

Status: `built`. Implemented as the plan reads, after the owner's checkpoint Decision (epic Notes, "Decision (owner, 2026-10-12, entry 7's plan checkpoint)": Design Notes 1 to 5 approved, C1 to C9 as recommended, C10 one dispatch). Reviewed by four lenses (`lenses_ran`); three low patches applied; nothing deferred.

**Summary:** `Session` gains `adopt` (refusals `NotPeer`, `Busy`, `BadSnapshot`, `Stale`, `OutOfTurn`, `BadStatus`, in that order; the shipped status is trusted, `rules.status` is not called), `hasState`, and the pulled shipment (`shipmentPending`, `pendingShipment`, `shipped`); `handle` and `draw` do nothing without a state; `afterSnapshot`'s tail became `deliverOver` with every guard kept (Design Note 1). New `lib/GameCore` files: `StateQueue` (4 deep over an injected `IStateStore`, newest non-`over` same-seat entry replaced, `Full` leaves the frame unacked), `EndSlot` (first reason wins, read once), `MatchSink` (the `ReliableLink::Sink` that joins them). `game-canvas.md` Overlays and `Session.h`'s `REJECT` comment are reworded as the plan says.

**Files:**
- `lib/GameCore/Session.h`, `Session.cpp` -- adopt, shipment, `hasState`, `deliverOver`, one added `bool` (`unshipped`, in existing padding).
- `lib/GameCore/StateQueue.{h,cpp}` -- `IStateStore`, `QueuedState`, the queue policy.
- `lib/GameCore/EndSlot.h` -- the latched end reason (lock-free).
- `lib/GameCore/MatchSink.{h,cpp}` -- peer table, verdicts, end-slot and queue hand-over.
- `test/game_core/SessionAdoptTest.cpp`, `StateQueueTest.cpp` (with the `EndSlot` tests), `MatchSinkTest.cpp`, `NearbyMatchTest.cpp` (pair rig, 100 seeds, 200-match soak), `MatchSupport.h` (`CounterRules`, `ArrayStateStore`, `MatchPeer`), `CMakeLists.txt` -- 65 new tests; `SessionTest.cpp` untouched.
- `docs/crosshatch/game-canvas.md` -- Overlays paragraph.
- This plan.

**Review breakdown:** 26 findings across the lenses (blind 15, edge-case 11, verification-gap 0), plus the intent-alignment audit (descriptive, no defect). high 0, medium 0, low 22, false 4. Patched (3, all low): the `StateQueue` threading contract comment; the `adopt` caller precondition (check `pending()` before taking an entry); `EndSlot::take` writes `out` only on true, with a test. Deferred: none. Rejected: every other row, each with its reason in the Review Triage Log (unreachable ver, approved decisions C1, C6, C9, a store breaking its contract, in-order link delivery, and the like). Follow-up review recommended: `false` (patched 3 low, 0 high, 0 medium).

**Verification performed** (all on this worktree, after the review patches; the flash figures at the tree of the commit below, taken the same way as the base):
- Host: full `ctest` 2301 of 2301 pass (2236 before + 65 new); `SessionAdopt|StateQueue|MatchSink|NearbyMatch|EndSlot` pass 20 times under `--repeat until-fail:20`; the nine mutants were each red when applied alone before the review (turn rule 5 tests, ver check 6, Busy 2, RoundStarted for any sender 2, replacing an `over` 7, Full acked 2, end slot overwritten 5, shipment cleared early 3, status recomputed 6).
- Fast: `scripts/check_layers.py` pass; `scripts/check_upstream_touches.py` PASS (no upstream file touched); `./bin/clang-format-fix` twice, `git status` unchanged by the second run, no formatting change outside this story's paths.
- Firmware, under the build lock, `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`: `pio run -e default` SUCCESS; `pio run -e x4pro` (via `check_flash_budget.py build on`) SUCCESS, so `LuaGame.cpp`'s `Session` arena static check holds; `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` PASSED for `default` and for `-e x4pro`. No `src/**` file changed, so no `sim.sh build` and no screenshots.
- Cost, like for like (x4pro games on minus off, `check_flash_budget.py`; base measured in this worktree at `affc2fee` before any edit, source identical to `6f6ad0d5`; then the final tree): flash +258,496 B -> +258,688 B = **+192 B**; static internal RAM +784 B -> +784 B = **+0 B** (.dram0.bss +16 both sides unchanged in difference). The epic's spend on a combined tree adds this: 448 B + 192 B = about 640 B of the 29,984 B share (the orchestrator measures the combined tree). The plan's estimate was tens of bytes; the 192 B is the `handle`/`draw` guards, `deliverOver`, and the shipment flag in `Session` (the adopt, queue, slot, and sink are dropped by the linker as nothing in `src/` calls them).

**Residual risks:**
- A new `Session::start()` while an `over` shipment is unshipped overwrites it (C6 (A): the owner of the `Session`, entry 9, defers Play again until `!shipmentPending()`; the reference loop and a test do). The window needs the link to refuse four frames, i.e. a gone peer.
- `StateQueue` and `MatchSink::Stats` are not thread-safe; the cross-task serialization is entry 8's (now stated in `StateQueue.h`). `MatchSink` does not call `ReliableLink::forget()` for a stranger it refuses (the plan gives it no link reference): the stranger keeps a peer slot until entry 8 decides where the forget lives.
- All timing is fake time on one thread; the soak's frame bound is half the plan's estimate (measured maximum 109 frames, ratio 0.261 of the estimate, 1.9 times the measured maximum), an estimate until a device run measures it.
- `MatchPeer::denySends` stands in for `ReliableLink::send` returning false by count, not by a full queue (stated in `MatchSupport.h`).
