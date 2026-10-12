---
title: 'ReliableLink over FakeLink'
type: 'feature'
ticket: '4'
created: '2026-10-12'
baseline_revision: '244f3399927618228138c068d85b042681132184'
status: built
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
followup_review_recommended: true
context:
  - '{project-root}/AGENTS.md'
  - '{project-root}/docs/crosshatch/orchestrated-epics.md'
warnings: ['oversized']
deferred:
  - summary: >-
      ReliableLink's wire semantics (a receiver takes an ahead State as a skip, an ack counts only when equal to the in-flight seq, the keepalive is an ACK once a frame was received and a PING before, the three timers) are documented only in code comments and this plan.
    evidence: |-
      A second implementation or a firmware of another build would drop a skipped State as a gap. Protocol.h and docs/crosshatch/formats.md do not describe them; the fix is a spine AD-13 amendment or a formats.md section, outside this entry's touches.
    location: >-
      lib/GameCore/ReliableLink.h, _bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md AD-13
    severity: medium
  - summary: >-
      The ok() == false and allocation-failure paths of ReliableLink (constructor, a State's snapshot buffer, the rollback of a peer registered for a send that could not queue) have no test.
    evidence: |-
      test/game_core builds against the real lib/Memory/Memory.h, which has no failure seam; other host targets stub it. A mutant that drops `if (fresh) peer->reset()` or returns success on a null snapshot passes every test.
    location: >-
      lib/GameCore/ReliableLink.cpp (constructor, enqueue, sendReliable), test/game_core
    severity: low
  - summary: >-
      forget() is for a peer that starts afresh: a peer that keeps its seq gets its Join and Accept dropped as ahead (never registered, never acked) until the sender reports silence at the limit; whether the lobby should instead resync a freshly registered peer's rxNext is entry 6's to decide.
    evidence: |-
      Review pass 2026-10-12 (BH2, EC3). A resync rule would deliver a delayed duplicate of a forgotten guest's Join (a ghost join); the plan does not settle it. The behaviour is pinned by AForgottenPeerThatKeepsItsSeqIsNeverRegisteredAndTheSenderGoesSilent.
    location: >-
      lib/GameCore/ReliableLink.cpp receiveSequenced, ReliableLink.h forget()
    severity: low
---

<intent-contract>

## Intent

**Problem:** `Protocol` and `FakeLink` exist (entry 2) but nothing owns `seq`, `ack`, resends, keepalives, or the silent-peer report (R2). The lobby (entry 6) and the match (entry 7) both need reliable delivery over a link that drops, duplicates, reorders, and delays.

**Approach:** Add `GameCore::ReliableLink` in new `lib/GameCore` files, over `ILink` and `IClock`, and prove it in host suites over a seeded `FakeLink`; add the refuse-N knob `deferred-work.md` `## 6.2` names to `FakeLink`.

## Boundaries & Constraints

**Always:** `lib/GameCore` includes only std, `lib/Memory`, and `lib/JsonParser` (`scripts/check_layers.py`); no radio fact, no log, no static initializer, no mutable static (AD-2); every fallible allocation is `makeUniqueNoThrow` and null-checked. The resend interval (400 ms), the silence limit (10 s), and the keepalive interval are `ReliableLink::Settings` fields with named defaults, never literals elsewhere. Time comes only from `IClock`. `Protocol` stays the only encoder and decoder: `ReliableLink` never writes a header byte itself. A peer is named only by `PeerHandle`. Resends run on their own per-peer clock (the instant the in-flight frame was last transmitted), never on "last frame sent". Test doubles follow the Build-agent brief (Design Notes 8). Code adapted from crossplay carries a comment naming `src/apps_local/link/LinkSession.cpp` and its MIT notice.

**Never:** no lobby logic, no `NearbySession`, no session queue, no `GameLink` task (entries 6, 7, 8); no `MOVE`/`REJECT`; no change to `ILink.h`, `Protocol.h/.cpp`, or any `src/**`, `lib/GameScript/**`, `games/**`, upstream file, `freeink-sdk`, spine, `docs/`, or CMake file outside `test/game_core/` (needed AD-13 text is listed under Auto Run Result for the orchestrator). No thread safety (one task calls it at a time, as `ILink`). No selective repeat or window above one in-flight frame (Design Notes 2).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Reliable delivery, hostile net | many seeds; 30 % loss, 20 % duplicates, reorder, delay and jitter; frames sent one at a time, then in bursts | every frame sent one at a time is delivered once, in order; in bursts the delivered frames are an in-order subsequence of those sent, with no repeat, holding every `JOIN`, `ACCEPT`, `over` `STATE`, and the last frame | none |
| Resend clock | the peer never acks; `resendMs` = 400 (default) and other values | the in-flight frame is transmitted at t, t+resendMs, t+2*resendMs, no earlier or later | none |
| Silence | peer heard once, then nothing; `silenceMs` = 10 s (default) and other values | no report before the limit; exactly one `onPeerSilent` at it; none later; its queue is dropped and further reliable sends to it return false | a lost peer's frames are ignored (except `ABORT`) until `forget` |
| `seq` wrap | 66,000 reliable frames one at a time | wire `seq` runs 0 to 65,535 then 0 again; every frame delivered once, in order | none |
| Unsequenced | `ABORT`, `ADVERT`, `PING`, `ACK` between reliable frames | they carry `seq` 0 and consume none: reliable `seq` runs on without a gap | `send` of `PING`, `ACK`, or `ADVERT` (and `broadcast` of any other type) returns false |
| `ABORT` behind an unacked `STATE` | the `STATE` is lost; `ABORT` sent next | the `ABORT` reaches the peer's sink at once; the `STATE` is still unacked | none |
| Replaced `STATE` | `v1` unacked and lost, `v2` sent; or `v1` queued behind an unacked `ACCEPT`, `v2` sent | `v1` is never delivered; `v2` is, once | none |
| `over` `STATE` kept | unacked `over` `v1`, then `v2`; or non-`over` `v1` then `over` `v2` | first case delivers `v1` then `v2`; second delivers `v2` only | none |
| Sink refuses by rule | `onFrame` returns `Refused` | acked, not delivered again, never resent | none |
| Sink has no room | `onFrame` returns `NoRoom` | not acked, `rxNext` unchanged; the peer's next resend is offered again and delivered once `Taken` | none |
| Acking every tick | the peer sends a duplicate reliable frame every tick; our `STATE` unacked | we ack each one and still resend our `STATE` every `resendMs` | none |
| Link refuses a send | `FakeLink::refuseSends(n)` | a refused first transmission is an ordinary loss: `send` still returns true and the frame arrives on a later resend | none |
| Stale or early ack | an `ack` that names an abandoned or unissued `seq` | the in-flight frame stays unacked | none |
| Quiet match | two linked devices idle for 60 s | no silence report; carried frames at most 4 + 2 * (60 s / `keepaliveMs` + 1) | none |

</intent-contract>

## Code Map

- `lib/GameCore/ReliableLink.h`, `lib/GameCore/ReliableLink.cpp` -- new: `GameCore::ReliableLink` (Design Notes 1 to 6). Pattern: `Protocol.h/.cpp` (pure, no log), `Session.h` (class style), `Manifest.cpp:213` (`makeUniqueNoThrow` from `lib/Memory/Memory.h`, the one GameCore precedent).
- `test/game_core/FakeLink.h` -- add `FakeLink::refuseSends(size_t count)` (Design Notes 8); update the header's "no send queue that fills" sentence. Read, not changed otherwise: `ManualClock`, `Faults`, `FakeLinkNet::{setFaults, blackhole, carried, setCarriedBudget, overBudget, refusedSends}`, `FakeLink::handleOf`.
- `test/game_core/FakeLinkTest.cpp` -- add the knob's tests.
- `test/game_core/ReliableLinkTest.cpp` -- new suite (Design Notes 7).
- `test/game_core/CMakeLists.txt` -- add `ReliableLinkTest.cpp` and `${REPO_ROOT}/lib/GameCore/ReliableLink.cpp`.
- Read, not changed: `lib/GameCore/ILink.h` (contract; `send` false is an ordinary loss), `Protocol.h` (`Frame`, `encode`, `decode`, `ANY_SESSION`, limits), `IClock.h`, `IGameRules.h` (`Status{over, turn, winners}`), `test/game_core/LinkContract.h`.
- Crossplay reference (read-only clone, `LinkSession.cpp`): `tick` (the retry clock `lastStateMs_` separate from the heartbeat clock `lastSentMs_`, 16 packets per tick, silence by `lastHeardMs_`), `handlePacket` (ack every copy including retransmits; refresh liveness on any frame from the peer). Adapted ideas carry `// Adapted from crossplay 0a1d17f src/apps_local/link/LinkSession.cpp (MIT).`
- Not touched, and why: no existing function is rewritten (every library file is new), so no `git log -L` guard check applies; `FakeLink.h` gains one member and one check in `send`/`broadcast`, whose existing early returns (ended link, invalid or foreign handle, empty or over-limit frame) stay first and unchanged.

## Tasks & Acceptance

**Execution:**
- [x] `lib/GameCore/ReliableLink.h`, `ReliableLink.cpp` -- implement Design Notes 1 to 6 exactly -- R2.
- [x] `test/game_core/FakeLink.h` -- add `refuseSends` -- the full-send-queue failure the device produces and the double could not (`## 6.2`).
- [x] `test/game_core/FakeLinkTest.cpp` -- tests for `refuseSends` (Design Notes 8).
- [x] `test/game_core/ReliableLinkTest.cpp` -- every test of Design Notes 7, with a recording `Sink` and a `RawPeer` double (Design Notes 8).
- [x] `test/game_core/CMakeLists.txt` -- register the new sources.
- [x] Mutants (recorded in Implementation Notes, then reverted): see Design Notes 7; each makes a named test red.

**Acceptance Criteria:**
- Given any seed at 30 % loss with duplicates, reorders, and delays, when frames are sent one at a time, then each is delivered once and in order, and when they are sent in bursts, then the delivered frames are an in-order subsequence that holds every non-replaceable frame and the last.
- Given an in-flight frame no one acks, when time advances, then it is retransmitted exactly every `resendMs`, however often the link acks the peer's frames, at defaults and at other settings.
- Given a peer last heard at t, when `silenceMs` elapses, then `onPeerSilent` is called once, at t + `silenceMs` and not before.
- Given a session with no room, when a reliable frame arrives, then it is not acked and is offered again on the peer's resend; given a frame refused by rule, then it is acked and not offered again.
- Given an `ABORT` sent while a `STATE` is unacked, then the peer's sink gets it with no wait.
- Given `scripts/check_layers.py`, then it passes with no new edge; given an x4pro games-on build, then growth over the epic base (+258,048 B flash, +784 B static RAM) is within what is left of 9,504 B and 160 B (unreferenced code, expected 0 B).

## Implementation Notes

- Baseline measurement taken before any change, in this worktree at `244f3399927618228138c068d85b042681132184` (`scripts/check_flash_budget.py` `build on`, `build off`, `compare`, incremental tree, shared cache, build lock; exit 0): `firmware.bin` 6,213,808 B games on, 5,955,312 B off, +258,496 B flash; static internal RAM 190,248 B on, 189,464 B off, +784 B. It equals the orchestrator's main-checkout reading of the same commit, so the worktree path costs nothing.

- Implemented (host tests and fast checks only; firmware checks still to run). Host suite: 2,154 tests pass (2,124 baseline + 27 `ReliableLinkTest` + 3 `FakeLinkTest` for `refuseSends`); `ReliableLinkTest` 20 repeats pass; ASan/UBSan build of `ReliableLinkTest`, `FakeLinkTest`, `ProtocolTest` with `ReliableLink.cpp`, `Protocol.cpp`, `Roster.cpp` passes (84 tests); `check_layers.py`, `check_layers_test.py`, `check_upstream_touches.py` pass; `./bin/clang-format-fix` run twice, second run changed nothing.
- Readings where the plan text and its tests disagree (decide at review):
  - Ack of the in-flight frame: Design Notes 2 says "acked when `uint16_t(ack - seq) < 0x8000`", but the I/O matrix and test 12 need an ack of a later-than-issued `seq` to leave it unacked, and `ack - seq` below 0x8000 would ack it. Implemented as serial order bounded by the highest `seq` issued; with one frame in flight that is `ack == seq` (`applyAck`).
  - `Settings` default argument: `const Settings& settings = {}` inside the class body does not compile (nested class with member initializers), so the constructor is two overloads (without and with `Settings`); call sites are unchanged.
  - `Stats` meanings: `framesSent` counts the first transmission of a Join, Accept, or State and an Advert broadcast; a keepalive or ack is `acksSent`/`pingsSent`; `framesReceived` excludes stray frames and frames dropped for a full peer table.
  - `send` of Abort returns whether the link took it (a refused Abort is `linkRefused` and false).
- Mutants (each applied alone to `ReliableLink.cpp`, host suite run, reverted; red tests):
  - resend clock keyed to the last frame sent: `ResendsFollowTheConfiguredInterval`, `ASideThatAcksEveryTickStillResendsOnSchedule`.
  - `NoRoom` acks: `AFrameWithNoRoomIsLeftUnackedAndDeliveredOnALaterResend`.
  - `Refused` does not ack: `AFrameRefusedByRuleIsAckedAndNotResent`.
  - skip rule off: `AReplacedStateIsNeverDelivered`, `AStateAheadOfAGapIsTakenAsASkip`, `AnOverStateIsKeptAndNewerStatesFollowIt`, `BurstsDeliverAnInOrderSubsequenceHoldingEveryKeptFrame`.
  - stale-ack check inverted: `AStaleOrEarlyAckNeverAcksTheInFlightFrame` and 14 others.
  - `over` tail replaced: `AnOverStateIsKeptAndNewerStatesFollowIt`, `BurstsDeliverAnInOrderSubsequenceHoldingEveryKeptFrame`.
  - Abort held behind an unacked State: `AnAbortBehindAnUnackedStateIsDeliveredAtOnce`.
  - silence reported twice (`lost` not set): `SilenceIsReportedOnceAtTheLimit`, `AReportedPeerIsDroppedUntilForgotten`, `TwoGuestsHaveIndependentSeqAndSilence`.
  - keepalive ignores the ack case: `KeepaliveIsAnAckOnceAFrameWasReceivedAndAPingBefore`.
- After the review (2026-10-12): the counts and mutant list above are those of the first pass; the patches added 7 `ReliableLinkTest` cases (34 in all) and changed `sendAbort`, `receiveSequenced`, `setSession`, `transmit`, and header comments (Review Triage Log). Final host suite 2,161 of 2,161; mutants for the three new rules (Abort `ack` 0 for an unknown peer, no reset on a changed session, a peer registered on every frame) each make their test red and were reverted. Design Notes 2 was corrected to the implemented ack rule (`ack` equal to the in-flight `seq`), and the receiver's comment about skipped `seq`s was corrected (an `over` State that replaces an in-flight State is itself a State, so it may skip). Two Settings constructor overloads replace the default argument (does not compile in-class).

## Plan Change Log

## Review Triage Log

### 2026-10-12 — Review pass
- verdicts: 29 findings — high 0, medium 9, low 15, false 5, maybe-false 0
- findings (lens in brackets: BH blind hunter, EC edge-case hunter, VG verification gap, IA intent alignment):
  - `[medium]` `patch` [BH1] An Abort to a peer with no record carried `ack` 0, which a receiver reads as acking its in-flight `seq` 0 (a host rejecting an unregistered guest's Join would pop the guest's Join) — confirmed at `sendAbort`; fixed: `NOTHING_RECEIVED` (0xFFFF) for an unknown peer; test `AnAbortToAnUnknownPeerCarriesTheNothingReceivedAck` (mutant red).
  - `[medium]` `patch` [EC1] Same defect as BH1 (`sendAbort` to an unknown peer, `ack` 0) — carried: one root cause, one fix and test.
  - `[medium]` `patch` [IA5] The intent-alignment lens's note that an Abort to an unregistered peer puts `ack` 0 on the wire — same defect as BH1, same fix.
  - `[medium]` `patch` [BH2] After a one-sided `forget()` the peer's Join/Accept at a higher `seq` is ahead-dropped forever while its keepalives keep it "heard", so silence never fires — confirmed; fixed with the EC2 rule below (the unregistered peer is never heard back, so the sender reports silence at the limit) plus a header note that `forget` is for a peer that starts afresh; test `AForgottenPeerThatKeepsItsSeqIsNeverRegisteredAndTheSenderGoesSilent` (mutant red). A resync-on-fresh-registration rule was not adopted: it would deliver delayed duplicates of a forgotten guest's Join (ghost joins), and the plan does not settle it; entry 6 may revisit (deferred).
  - `[medium]` `patch` [EC3] Same defect as BH2 (one-sided `forget()`, a fresh peer's `rxNext` 0 against a live sender's `seq`) — carried: same fix and test.
  - `[low]` `patch` [BH3] Worst-case heap and lazy allocation during play unstated — the plan states about 25 KB; the header now states it (snapshot buffers allocated on first use and kept, no churn after warm-up).
  - `[low]` `reject` [BH4] `Settings` is not validated (`maxPeers` 0, `keepaliveMs` >= `silenceMs`, `resendMs` 0) — values come from named constants of the lobby and session code; a guard adds branches for no reachable caller; the header already says keepalive < silence is the caller's.
  - `[low]` `patch` [BH5] "One frame in flight" wording, and a replacing State is transmitted at once — wording fixed in the header; no rate limit added (callers send one State per accepted move, at human pace).
  - `[low]` `patch` [BH6] Header and test names overclaim "never delivered": a replaced State that already reached the peer is still delivered — header says "may be skipped"; test `AReplacedStateThatAlreadyArrivedIsStillDelivered` added.
  - `[low]` `patch` [IA1] Replacement is sender-side only and the case of an old State that already arrived is untested — same finding as BH6; the same test covers it.
  - `[medium]` `defer` [BH7] The skip rule, the ack-equals-in-flight rule, and the timers are wire behaviour documented only in code comments and the plan — real; the fix is a spine AD-13 amendment or `docs/crosshatch/formats.md` entry, outside this entry's `touches` (listed for the orchestrator).
  - `[low]` `reject` [BH8] MIT notice only in a `.cpp` comment — the form the owner approved for `FakeLink.h` (entry 2, C8); a release notices file is outside this entry.
  - `[low]` `patch` [BH9] Comments cite planning artifacts ("entries 6 and 7") — that wording replaced by "the lobby and the match session"; `AD-n` citations follow `Protocol.h`'s convention.
  - `[medium]` `patch` [BH10] Test gaps in general — the specific ones are VG1 to VG3 below (added) and VG4 (deferred).
  - `[medium]` `patch` [VG1] The `onPeerSilent` re-entrancy contract (`send` returns false, `forget` safe) has no test — confirmed (`silentHook` never set); `ASinkMayCallSendAndForgetInsideOnPeerSilent` added.
  - `[low]` `patch` [VG2] An Abort from a live peer refreshing liveness and applying its `ack` is untested — `AnAbortFromALivePeerRefreshesLivenessAndAppliesItsAck` added.
  - `[low]` `patch` [VG3] The `elapsed()` backward-clock guard is untested — `AClockSteppedBackwardsCausesNothingUntilItPassesTheOldMark` added.
  - `[low]` `patch` [BH11] Smaller code issues — `transmit`'s `default:` replaced by explicit cases (the others rejected: `findPeer` const-ness is cosmetic, the validation encode is deliberate, `FakeLink.h`'s comment rewrap follows the changed sentence, `Stats` needs no reset).
  - `[false]` `reject` [BH12] Test-time cost — measured: the slowest `ReliableLinkTest` case takes 63 ms.
  - `[low]` `patch` [EC2] A peer registered by a frame that is never offered (ahead-dropped or behind) holds a slot and is later reported silent though the sink never saw it — confirmed; fixed: a peer is registered only by a frame that will be offered (`d` == 0 or an ahead State), otherwise it is counted and left unregistered, with no ack.
  - `[low]` `reject` [EC4] `ack` 0xFFFF from a receiver that has taken nothing could ack an in-flight `seq` 0xFFFF — needs 65,535 abandoned States to a peer that delivered none; silence ends that long before (Design Notes 5).
  - `[medium]` `patch` [EC5] `setSession` with a changed id left queues and `seq` to transmit under the new id (a guest backing out of one host and joining another) — fixed: a changed id resets every peer; test `ChangingTheSessionDropsEveryPeersState` (mutant red).
  - `[false]` `reject` [EC6] An Advert never refreshes a registered peer's liveness — by design (Design Notes 3): only the guests of a host are peers and guests send no Advert.
  - `[low]` `patch` [EC7] The skip-rule comment said an `over` State is released only after the frame before it is acked — wrong for an `over` State replacing an in-flight one; comment reworded (soundness holds because it is a State).
  - `[low]` `defer` [VG4] The `ok() == false` and allocation-failure paths have no test — real; `game_core` has no injected allocation failure and adding one is outside this entry.
  - `[low]` `reject` [VG5] Link-refusal counters for Abort, broadcast, and control frames are not asserted — a counter only; the data path is tested.
  - `[false]` `reject` [IA2] The one-at-a-time hostile test never replaces a State — by design; the burst test covers replacement and its oracle.
  - `[false]` `reject` [IA3] "Cumulative ack" is an exact match with one frame in flight — Design Notes 2 states it and why; a range ack has nothing to pop.
  - `[false]` `reject` [IA4] The quiet-match bound is the test's own — the plan states the formula (4 + 2 * (T / `keepaliveMs` + 1)) and the test uses it.

## Design Notes

**Settled by.** Multi-peer table: R3 "Nothing in it assumes n = 2", AD-11, entry 6's verify ("with `maxSeats` 3 two guests reach `n = 3`"); one in-flight frame and the skip rule: R2 ("an unacked `STATE` may be replaced by a newer one, except one that ends a round"), AD-13's latest-wins sentence, entry 4's verify (replaced `STATE` never delivered; unacked `over` followed by a newer one delivers both in order); a sink verdict with a no-room outcome: R2, R5 ("a frame that finds the queue full is left unacked"), entry 7's description; keepalive: entry 4's description and crossplay `kHeartbeatIntervalMs` = 1,000 ms (adopted, epic Notes "Adopted from crossplay"); `ACK` is type 7 and unsequenced: epic Notes Decision 2026-10-12 (entry 2's checkpoint, Q2); strict `proto`: same Decision, Q1. The keepalive default (1,000 ms) is the plan's, as the entry's `unknown` says; R12's device run confirms or changes it.

### 1. API (`lib/GameCore/ReliableLink.h`, namespace `GameCore`)

```cpp
class ReliableLink {
 public:
  static constexpr uint32_t DEFAULT_RESEND_MS = 400;      // AD-13
  static constexpr uint32_t DEFAULT_SILENCE_MS = 10000;   // AD-13
  static constexpr uint32_t DEFAULT_KEEPALIVE_MS = 1000;  // crossplay kHeartbeatIntervalMs; must be < silenceMs
  static constexpr size_t DEFAULT_MAX_PEERS = 4;
  static constexpr size_t QUEUE_DEPTH = 4;          // reliable frames held per peer: one in flight, the rest waiting
  static constexpr size_t MAX_FRAMES_PER_TICK = 16;  // crossplay kMaxPacketsPerTick

  struct Settings {
    uint32_t resendMs = DEFAULT_RESEND_MS;
    uint32_t silenceMs = DEFAULT_SILENCE_MS;
    uint32_t keepaliveMs = DEFAULT_KEEPALIVE_MS;
    size_t maxPeers = DEFAULT_MAX_PEERS;
  };
  enum class Verdict : uint8_t { Taken, Refused, NoRoom };  // Taken, Refused: acked. NoRoom: not acked.
  class Sink {  // entries 6 and 7 implement it
   public:
    virtual ~Sink() = default;
    // A frame for the session: Join, Accept, State (reliable: in order, once each, replaced States skipped), or
    // Advert, Abort (unsequenced: at once, any number of times; the return value is ignored). `frame` and every
    // span in it alias the link's receive buffer: copy what you keep before returning. May call send(); must
    // not call tick() or forget().
    virtual Verdict onFrame(PeerHandle from, const Protocol::Frame& frame) = 0;
    // A peer sent nothing valid for silenceMs. Once per peer until forget(). May call send() (it returns false).
    virtual void onPeerSilent(PeerHandle peer) = 0;
  };
  struct Stats {  // all counters since construction
    uint32_t framesSent, resends, acksSent, pingsSent, abortsSent, linkRefused;   // sent: ILink::send took it
    uint32_t framesReceived, duplicates, aheadDropped, noRoom, badFrames, strayFrames, peerTableFull, queueFull;
    uint32_t silentReports;
  };

  ReliableLink(ILink& link, const IClock& clock, Sink& sink, const Settings& settings = {});
  bool ok() const;  // false when construction could not allocate: every call then does nothing / returns false
  void setSession(uint32_t session);  // 0 (ANY_SESSION, the start state) or the host's id
  uint32_t session() const;
  // Reliable: Join, Accept, State (header.type; header.session/seq/ack are overwritten). Unsequenced: Abort.
  // False, nothing queued: not ok(), session unset, a field Protocol::encode refuses, the peer invalid, lost or
  // out of table room, the type is Advert/Ping/Ack, the queue is full (QUEUE_DEPTH), or allocation failed.
  bool send(PeerHandle to, const Protocol::Frame& frame);
  bool broadcast(const Protocol::Frame& advert);  // Advert only, unsequenced; false for any other type
  void tick();  // receive up to MAX_FRAMES_PER_TICK frames, then per peer: silence, resend, keepalive
  void forget(PeerHandle peer);  // drop its state, queue, and lost flag; a later frame registers it afresh
  size_t unacked() const;              // reliable frames held (in flight or waiting) for peers not lost
  size_t unacked(PeerHandle peer) const;
  const Stats& stats() const;
};
```

Storage: one 1,470 B receive buffer and one 1,470 B transmit buffer (frames alias the receive buffer while a sink runs, and a sink may call `send`, so they are two), an array of `maxPeers` peer slots, and per peer `QUEUE_DEPTH` queue entries whose 1,400 B snapshot buffers are allocated when an entry first holds a `STATE` and kept for the peer's life (no churn after warm-up). All of it comes from `makeUniqueNoThrow` (nothing static); worst case at defaults: 2 * 1,470 + 4 peers * 4 * 1,400 = about 25 KB, one peer about 8.5 KB; a v1 match has one peer. Locals stay under 256 B (a `Protocol::Frame` is about 120 B).

### 2. Sender: one frame in flight, latest-wins by skipping

Per peer: a queue (`QUEUE_DEPTH` entries; entry 0 is in flight once it has been given a `seq`), `nextSeq` (first 0, `uint16_t` wrap), `rxNext` (the next `seq` expected from the peer, first 0), `hasRx`, `lastHeardMs`, `lastSentMs`, `lost`.

Why one frame in flight. A cumulative `ack` plus a replaced frame is unsound with a window: reusing the replaced frame's `seq` for the newer `STATE` fails twice (the peer that already took the old frame calls the new one a duplicate; a stale ack for the old frame acks the new one), and a fresh `seq` leaves a gap the peer cannot tell from a loss. With one frame in flight a gap can only be a skipped `STATE`, and the receiver's rule (3) is then sound. Cost: a queued frame waits one round trip behind the one before it; v1 sends a move at a time, so none noticed.

`send` of Join, Accept, or State:
1. Validate by encoding the frame once into the transmit buffer with `seq` 0 (this catches every field `encode` refuses, so later transmissions cannot fail).
2. A State whose queue tail is a non-`over` State coalesces: the tail's content is replaced. If that tail was in flight (it is then the only entry), it is abandoned: its `seq` is never reused or resent, and the entry becomes unsent. An `over` tail is never replaced (a newer `over` State may replace a non-`over` one). A State whose tail is anything else, and every Join and Accept, is appended; a full queue returns false (`queueFull`).
3. If entry 0 has no `seq` it is released: `seq = nextSeq++`, transmitted, `sentAtMs = now`.
An `ack` that names entry 0 pops it and releases the next. The in-flight frame is acked by an `ack` equal to its `seq` (serial order bounded by the highest `seq` issued, which with one frame in flight is its own; an unbounded `uint16_t(ack - seq) < 0x8000` test would ack on a later-than-issued `ack`, against the matrix). An `ack` of an abandoned or later-than-issued `seq` never acks it.

Transmit builds a `Protocol::Frame` (header type, session, `seq`, `ack` = `rxNext - 1`) and calls `encode` then `ILink::send`; a false from the link counts `linkRefused` and is an ordinary loss (the resend clock still starts). Every transmission sets the peer's `lastSentMs`.

Resend: in `tick`, if entry 0 is in flight and `now - sentAtMs >= resendMs`, transmit it again (`resends`). The clock is the entry's own `sentAtMs`; acks, keepalives, and the peer's traffic never move it (crossplay `lastStateMs_` against `lastSentMs_`).

`send` of Abort: encoded and sent at once, `seq` 0, `ack` = `rxNext - 1` if the peer is known else 0; it is not queued, never waits behind a reliable frame, and works for a lost or unknown peer (`abortsSent`). `broadcast` of Advert: `seq` 0, `ack` 0, session as set, never registers a peer.

### 3. Receiver

`tick` takes up to `MAX_FRAMES_PER_TICK` frames from the link, each decoded with `Protocol::decode(in, session, frame)` (a refusal counts `badFrames` and is dropped). With the session unset only Advert is processed (others count `strayFrames`). Per type:
- **Advert, Abort:** handed to `Sink::onFrame` at once, verdict ignored; an Abort also refreshes the peer's liveness and applies its `ack` when the peer is known and not lost. They are delivered even from a lost or unknown peer and never register one.
- **Ping, Ack:** from a known, not-lost peer: refresh `lastHeardMs` and apply the `ack`; from anyone else `strayFrames`. They are never delivered to the sink.
- **Join, Accept, State** (sequenced): from a peer not in the table, register it (`lastHeardMs = now`) or, with the table full, drop the frame (`peerTableFull`); from a lost peer drop it. Then refresh liveness and apply the `ack`, then with `d = uint16_t(seq - rxNext)`:
  - `d == 0`, or `d < 0x8000` and the frame is a State (a skip: every skipped `seq` is a replaced State, because the sender releases a Join, Accept, or `over` State only once the frame before it is acked): offer to the sink. `Taken` or `Refused`: `rxNext = seq + 1`, `hasRx = true`, send an `ACK` (`acksSent`). `NoRoom`: change nothing, send nothing (`noRoom`); the peer's resend offers it again.
  - `d < 0x8000` and not a State: drop, no ack (`aheadDropped`).
  - otherwise a duplicate: send an `ACK` (`duplicates`), never offer it. Every copy is acked, retransmits included, or the sender resends forever (crossplay).
  The sink may `send` or `forget` the peer inside the callback: re-find the peer by handle afterwards and stop if it is gone.
`framesReceived` counts frames that decoded and were not stray.

### 4. Silence and keepalive

Per not-lost peer, in `tick` after receiving: if `now - lastHeardMs >= silenceMs`, mark lost, drop its queue, count `silentReports`, call `Sink::onPeerSilent` once, and do nothing more for it. `lastHeardMs` starts at the peer's registration (its first received sequenced frame, or the first reliable `send` to it). Else resend (2), then if `now - lastSentMs >= keepaliveMs` send the keepalive: an `ACK` when `hasRx` (it acknowledges the last frame and so doubles as the keepalive), else a `PING`. A transmission of any kind defers the keepalive. A quiet match therefore carries, per device, at most one frame per `keepaliveMs`: the bound the tests assert (4 handshake frames + 2 * (T / `keepaliveMs` + 1) for an idle span T). Settings need `keepaliveMs < silenceMs`; the constructor does not check it, and a larger value reports a healthy peer silent.

### 5. Choices a reader may question

- **Constants.** `seq` starts at 0 and wraps naturally at 65,535 to 0 (`uint16_t`); the initial `ack` on the wire is `0xFFFF` (`rxNext - 1`), which `uint16_t(0xFFFF - 0)` keeps from acking `seq` 0. Only a sender that issued 65,535 abandoned States to a peer that delivered none could be misled; silence ends that long before.
- **Why the sink returns a verdict, not the link a queue.** The queue is entry 7's, over storage `src/games` injects (R5); this entry only needs to know whether the session took the frame.
- **Abort and a lost peer.** A received `ABORT` is delivered even after a silence report, since the session latches the first reason and may still be reading.
- **Not decided here:** the lobby's use of `forget`, the `LINK` log lines and the flush-then-`ABORT` call (entry 8; `unacked()` is what its flush reads), and the sink's queue (entry 7).

### 6. Existing code and guards

No existing function is moved or rewritten. `FakeLink::send` and `broadcast` keep their order (ended link, invalid or foreign handle, empty or over-limit frame); the knob is checked after them and before the frame is submitted, because the device refuses a valid frame when its queue is full.

### 7. Tests (`test/game_core/ReliableLinkTest.cpp`, one `TEST` each unless a loop is named)

Fixtures: `Duo` (a `ManualClock`, a seeded `FakeLinkNet`, two `FakeLink`s, two `ReliableLink`s with recording `Sink`s whose verdict is scriptable, `step(ms)` = advance by 1 ms and tick both, repeated), `RawPeer` (a `FakeLink` with `take()` decoding a frame and `put(frame)` encoding and sending one by hand). Names below are the tests:
1. `DefaultsAre400And10000AndAKeepalive` -- `Settings` defaults equal `DEFAULT_*`, 400, 10,000, 1,000.
2. `EveryFrameSentOneAtATimeIsDeliveredOnceInOrder` -- 200 seeds, 30 % loss, 20 % duplicates, 30 % reorder, 20 ms delay, 30 ms jitter, traffic both ways; a mix of Join, Accept, States (incl. `over`); each frame sent when the last is acked; delivered list equals sent list per direction; progress within a time cap.
3. `BurstsDeliverAnInOrderSubsequenceHoldingEveryKeptFrame` -- same net, frames sent in bursts with random 0 to 300 ms between; per direction: delivered is a subsequence (by unique tag), no repeat, holds every Join, Accept, `over` State, and the last frame.
4. `ResendsFollowTheConfiguredInterval` -- a `RawPeer` that never acks, defaults and `resendMs` 150, 1,234: the State is on the wire at t, t + resendMs, t + 2 * resendMs and at no other time.
5. `SilenceIsReportedOnceAtTheLimit` -- defaults and (silence 2,500, keepalive 500), (30,000, 2,000): none at limit - 1 ms, one at the limit, none by 3 * limit; a peer that is heard every 900 ms is never reported; a peer only sent to (never heard) is reported at the limit from the first send.
6. `AReportedPeerIsDroppedUntilForgotten` -- its queue is cleared, `send` returns false, its frames are ignored, an `ABORT` from it is delivered, `forget` lets it register again.
7. `SeqWrapsAt65535` -- a `RawPeer` acking each frame by hand: 66,000 `over` States; wire `seq` runs 0 to 65,535, 0, 1; the sink saw every one.
8. `UnsequencedFramesConsumeNoSeq` -- `ABORT`, keepalive `PING`/`ACK`, and an `ADVERT` between reliable frames carry `seq` 0; the reliable `seq` runs 0, 1, 2 unbroken; `send` of Advert/Ping/Ack and `broadcast` of a non-Advert return false.
9. `AnAbortBehindAnUnackedStateIsDeliveredAtOnce` -- the State is lost (loss window), the Abort arrives with the State still unacked and undelivered.
10. `AReplacedStateIsNeverDelivered` -- both lost then healed: only `v2` is delivered (`seq` 1 on the wire, skip rule); and `v1`, `v2` queued behind an unacked `ACCEPT`: `ACCEPT` then `v2`.
11. `AnOverStateIsKeptAndNewerStatesFollowIt` -- over `v1` then `v2`: both delivered in order; non-`over` `v1` then `over` `v2`: only `v2`; over, over, then a plain State: all three in order.
12. `AStaleOrEarlyAckNeverAcksTheInFlightFrame` -- abandoned `seq` acked late; `ack` 0xFFFF against `seq` 0; later-than-issued `ack`s do not matter; the right ack pops it.
13. `AFrameRefusedByRuleIsAckedAndNotResent` -- `Refused`: sender `unacked()` 0, no resend over 5 s, sink offered once.
14. `AFrameWithNoRoomIsLeftUnackedAndDeliveredOnALaterResend` -- `NoRoom` for the first three offers: no `ACK` sent for them (`acksSent` 0), offers 400 ms apart, then `Taken`: delivered once, acked; a second frame behind it waits.
15. `ASideThatAcksEveryTickStillResendsOnSchedule` -- a `RawPeer` puts a duplicate reliable frame every tick for 3 s while the link's own State is unacked: acks go out every tick and the State is still on the wire every 400 ms.
16. `ALinkRefusalIsAnOrdinaryLoss` -- `refuseSends(2)` on the sender: `send` returns true, the State arrives on a later resend, `linkRefused` is 2.
17. `AQuietMatchStaysUnderTheFrameBound` -- handshake then 60 s idle with `setCarriedBudget(4 + 2 * (60 + 1))`: not over budget, no silence report, and at least 2 * 59 frames carried (keepalives really flow); also at `keepaliveMs` 250.
18. `KeepaliveIsAnAckOnceAFrameWasReceivedAndAPingBefore` -- raw capture of each side's idle frame types.
19. `OtherSessionsAndUnsetSessionAreIgnored` -- other session: not delivered, not acked, no peer; unset session: Advert delivered, State stray.
20. `ADuplicateIsAckedAndNeverDeliveredTwice`; `AnAheadJoinIsDroppedUnacked` (a Join at the wrong `seq`).
21. `AdvertBroadcastReachesTheSinkAndRegistersNoPeer` -- no silence report after 10 s.
22. `ASinkMayReplyInsideOnFrame` -- the reply goes out; a `forget` inside the callback is safe.
23. `TwoGuestsHaveIndependentSeqAndSilence` -- host with two guests: separate `seq` and `ack`, one guest silenced, the other unaffected; a full peer table drops the next new peer (`peerTableFull`), and `QUEUE_DEPTH` queued Joins make the next `send` false.
24. `SendRefusesWhatEncodeRefuses` -- snapshot of 1,401 B, session unset, invalid peer: false and nothing queued; a 1,400 B State is accepted.
Mutants (each applied alone, host suite run, then reverted, recorded with the red test names): resend clock keyed to the last frame sent; `NoRoom` acks; `Refused` does not ack; the skip rule off; the stale-ack check inverted; `over` tail replaced; an Abort queued behind the State; silence reported twice; the keepalive ignores the ack case.

### 8. Test doubles

- `FakeLink::refuseSends(n)` stands in for the device's full send queue (`ILink.h`: `send`/`broadcast` return false, nothing sent, "its send queue is full"; `esp_now_send` failing or the adapter's queue full). It refuses the next `n` calls that would otherwise succeed; it is more permissive than the device in that the refusal is by count, not by queue fill and drain over time, and it applies to `broadcast` too. `FakeLinkTest` pins the agreement: a refused call returns false, queues nothing, counts in `refusedSends()`, does not touch `foreignHandleSends()`, the (n+1)th call succeeds, and the knob does not change the number of rolls the net draws.
- `RawPeer` stands in for a remote device speaking `Protocol` by hand (an oracle independent of `ReliableLink`); `Sink` stands in for entries 6 and 7's session code. The sink is more permissive than entry 7's queue: unbounded recording, and a scripted verdict instead of a 4-entry queue. Host timing: every bound is in fake milliseconds from the settings, not host speed.

## Verification

**Commands** (host suites and fast checks during implementation; the firmware checks once, after the review and its patches, before the commit):
- `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j'` -- expected: all pass; baseline 2,124 tests at `244f3399` plus the new cases (counts recorded after the run). Also the new suites alone under `--gtest_repeat` (ReliableLinkTest 20 times) as a determinism check.
- `python3 scripts/check_layers.py`, `python3 scripts/check_layers_test.py`, `python3 scripts/check_upstream_touches.py`, `./bin/clang-format-fix` twice then `git status` -- expected: pass, no new edge, the second format run changes nothing; a formatting-only change outside this entry's paths is named under Auto Run Result.
- Mutants (Design Notes 7) -- each makes a named test red.
- ASan/UBSan build of `ReliableLinkTest.cpp`, `FakeLinkTest.cpp`, `ProtocolTest.cpp` with `ReliableLink.cpp`, `Protocol.cpp` (as entry 2 did, outside CMake) -- expected: all pass.
- `export PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`, then under `flock /tmp/crosshatch-build.lock sh -c '...'`: `pio run -e x4pro`, `pio run -e default`, `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` and the same with `-e x4pro`, and `python3 scripts/check_flash_budget.py` `build on`, `build off`, `compare`, `objects` -- expected: all succeed. Size cost = games on minus off at the baseline commit `244f3399` (measured in this worktree, before any change, log under the scratch directory) against the same at this story's commit; the orchestrator's main-checkout reading of `244f3399` is +258,496 B flash, +784 B static RAM (epic growth so far +448 B flash, +0 B static RAM over the base; share left 9,056 B flash and 160 B static RAM). Expected about 0 B (unreferenced code); `objects` reports no static initializer in `ReliableLink.cpp`.
- No simulator screenshots (no screen) and no fresh-clone gate run (no CI gate or workflow change).

**Results** (final tree, after the review patches; commit below): host suite 2,161 of 2,161 (`ctest -j`); `ReliableLinkTest` 20 repeats, no failure; ASan/UBSan build (outside CMake) of `ReliableLinkTest`, `FakeLinkTest`, `ProtocolTest` with `ReliableLink.cpp`, `Protocol.cpp`, `Roster.cpp`: 91 of 91; `check_layers.py` (619 edges), `check_layers_test.py`, `check_upstream_touches.py` pass; `./bin/clang-format-fix` twice, the second changed nothing, no formatting change outside this entry's paths; `pio run -e x4pro` and `-e default` succeed (`ReliableLink.cpp` compiled in both); `pio check` default and `-e x4pro` PASSED with the high/medium/low defect gates; `check_flash_budget.py` `build on`, `build off`, `compare`, `objects` exit 0, `objects` "No problems". Size, measured the same way in this worktree (incremental tree, shared cache, build lock): at the baseline `244f3399927618228138c068d85b042681132184` games on 6,213,808 B, off 5,955,312 B, +258,496 B; static internal RAM 190,248 B on, 189,464 B off, +784 B. With this story's changes (the same build, run before the commit): 6,213,808 B on, 5,955,312 B off, +258,496 B; 190,248 B and 189,464 B, +784 B. This story's own cost: +0 B flash, +0 B static RAM. Reading over the epic base (+258,048 B, +784 B): +448 B flash, +0 B static RAM, all of it entry 6.1's (the orchestrator's finding), so 9,056 B flash and 160 B static RAM of the epic share remain. Quiet-match bound the tests assert: at most 4 + 2 * (60 s / `keepaliveMs` + 1) frames for a 60 s idle match (keepalive 1,000 ms: 126; 250 ms: 486), i.e. one keepalive per device per `keepaliveMs`.

**Manual checks:**
- `ReliableLink.h` contains no radio word (MAC, channel, RSSI, ESP) and no log call; every header byte is written by `Protocol::encode`.
- Each early return in `FakeLink::send`/`broadcast` is still ahead of the new check.

## Auto Run Result

**Status `built`** (full route; review `thorough`, four context-free lenses). Baseline `244f3399927618228138c068d85b042681132184`, branch `epic-play-nearby-lane-a`; the commit is the branch tip with the subject `feat: add ReliableLink over the link interface (nearby play)`.

**Summary.** `GameCore::ReliableLink` (new `lib/GameCore/ReliableLink.h/.cpp`) owns `seq`, `ack`, resends, keepalives, and the silent-peer report over `ILink` and `IClock`, for several peers by `PeerHandle`. One reliable frame (`JOIN`, `ACCEPT`, `STATE`) is in flight per peer; a newer non-`over` `STATE` replaces an unacked one by abandoning its `seq`, and the receiver takes a `STATE` ahead of a gap as a skip (sound because only replaced `STATE`s can lie in a gap). A `Sink` returns `Taken`, `Refused` (both acked), or `NoRoom` (not acked, so the peer's resend delivers it later). Resends run on each frame's own clock, never starved by acks; `ABORT` goes out at once and is delivered even from a lost peer; the keepalive (default 1,000 ms) is an `ACK` once a frame was received and a `PING` before; a peer silent for the limit is reported once. `FakeLink::refuseSends(n)` stands in for a full send queue. 34 `ReliableLinkTest` cases and 3 `FakeLinkTest` cases.

**Files.**
- `lib/GameCore/ReliableLink.h`, `lib/GameCore/ReliableLink.cpp` -- the class (new; crossplay `LinkSession.cpp` notice in the `.cpp`).
- `test/game_core/ReliableLinkTest.cpp` -- the suite (new).
- `test/game_core/FakeLink.h`, `test/game_core/FakeLinkTest.cpp` -- the refuse-N knob and its tests.
- `test/game_core/CMakeLists.txt` -- registers the sources.
- This plan.

**Review.** 29 findings: patched 18 rows over 8 root causes (Abort `ack` for an unknown peer; peers registered only by a frame that is offered, which also bounds the one-sided-`forget` livelock by silence; `setSession` resets peers; test additions for the sink's re-entrancy in `onPeerSilent`, Abort liveness and ack, a backward clock, and a replaced `STATE` that had already arrived; comment fixes and an exhaustive `transmit` switch). Patched by verdict: medium 8 rows (BH1, EC1, IA5, BH2, EC3, BH10, EC5, VG1), low 10. Deferred 3 (below). Rejected: BH4, BH8, BH12 (false), EC4, EC6 (false), VG5, IA2 (false), IA3 (false), IA4 (false), with reasons in the log.

**Follow-up review recommended: true.** The patched mediums include two state-logic changes (a peer is registered only when its first frame is offered; `setSession` resets peers) that the same context verified only with mutants. The unverified risk is the lobby's use of them (entry 6): a first frame that is a duplicate or ahead is silently not registered, and nothing ack-s it.

**Deferred** (frontmatter `deferred`): the wire semantics written down only in code (needs a spine AD-13 or `formats.md` entry); the allocation-failure paths untested; `forget()` semantics for entry 6.

**For the orchestrator (outside this entry's `touches`).** (1) Spine AD-13 / `docs/crosshatch/formats.md`: add that `seq` is per direction, one reliable frame is in flight, a replaced `STATE` abandons its `seq` and the receiver takes an ahead `STATE` as a skip, an `ack` counts only when equal to the in-flight `seq`, resends 400 ms, silence 10 s, keepalive 1,000 ms (an `ACK` once a frame was received, else a `PING`), all `ReliableLink::Settings`; the keepalive default is this plan's (R12 confirms it). (2) Epic Notes: this entry's size cost is +0 B flash and +0 B static RAM, measured like for like in this worktree (above); the epic growth over the base after this entry is still +448 B flash, +0 B static RAM. (3) Entry 8's flush reads `ReliableLink::unacked()`; entry 6 decides who calls `forget()`.

**Verification.** See the plan's Verification, Results. No simulator screenshots (no screen) and no fresh-clone gate run (no CI gate or workflow change). No formatting-only change outside this entry's paths.

**Residual risks.** `ReliableLink` allocates up to about 25 KB at four peers (8.5 KB for the one peer of a v1 match), on first use and kept; on the device that comes from internal heap. Host timing is fake milliseconds, so the 400 ms, 1,000 ms, and 10 s values are unconfirmed on a radio (R12). The deferred allocation-failure paths are untested.

