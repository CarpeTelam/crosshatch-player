---
title: 'GameLink task and NearbySession over the link interface'
type: 'feature'
ticket: '8'
created: '2026-10-12'
baseline_revision: '37d922ae2026e2ae4ff8b8fe5c4fb69cc5c4a1a0'
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
      With three or more seats one permanently refusing peer would hold the staged shipment (shipStep waits for every peer).
    evidence: |-
      v1 has two seats, where a lost peer ends the match through the end slot; relaying STATE between guests is deferred in the spine, so the case is unreachable now.
    location: >-
      src/games/NearbySession.cpp shipStep
    severity: low
  - summary: >-
      The IClock and IRandom handed to a NearbySession must outlive a leaked task, not only the session.
    evidence: |-
      The destructor leaks Core when the task will not stop; the task keeps reading the clock. The header says the caller keeps them alive for the session's life; entry 9, which owns the session, should keep them process-long (GameClock is stateless).
    location: >-
      src/games/NearbySession.h (Lifetime)
    severity: low
  - summary: >-
      Entry 10's lobby must call HostLobby/GuestLobby::cancel() before it destroys a NearbySession with players seated; the destructor sends no goodbye.
    evidence: |-
      Teardown ends the link only; seated peers would learn by silence (10 s). leave() sends nothing before beginMatch by design.
    location: >-
      src/games/NearbySession.h (Lifetime)
    severity: low
  - summary: >-
      GameLink's priority and core are asserted as constants, but the FakeRtos double drops both, so no host test observes them on the created task.
    evidence: |-
      FakeRtos.h xTaskCreatePinnedToCore ignores priority and core; entry 11's smoke run on two devices is what can show them (and the 4 KB stack headroom).
    location: >-
      test/game_script/harness/GameLinkTest.cpp
    severity: low
  - summary: >-
      game_nearby_src (GameLink.cpp, NearbySession.cpp) links game_match_src, so entry 9 moving NearbySession into the match must move the two sources into the match library and drop them from nearby_session.sources.cmake's exclusion.
    evidence: |-
      The match library is built from the same glob less HARNESS_EXCLUDE_GAMES, which now lists the two files; a dependency running both ways between the libraries would not link.
    location: >-
      test/game_script/harness/nearby_session.sources.cmake
    severity: low
---

<intent-contract>

## Intent

**Problem:** Everything the nearby match needs from the link exists as pure `lib/GameCore` policy (`ReliableLink`, the lobbies, `StateQueue`, `EndSlot`, `MatchSink`, `Session::adopt` and its pulled shipment), but nothing runs it: no task drains the link, resends, and detects silence while an overlay stops the match's `loop()`, no code owns the PSRAM pool the queue needs or hands snapshots between tasks, no one gives the match one call to say goodbye, and no `LINK` line exists for the device runs (R5, R6, R7, R13).

**Approach:** Add `src/games` `NearbySession` (owns a handed-in `ILink`, the lobby, the match half, the PSRAM pool, the serialized cross-task handoffs, the `LINK` lines) and `GameLink` (the priority 1, core 1, 4 KB task that steps it), both written against `ILink` and tested in the host harness over `FakeLink` on real threads, plus the harness source list the later entries reuse.

## Boundaries & Constraints

**Always:** `NearbySession` and the task never take `RenderLock` and never touch activity state; the task only steps `NearbySession`, which calls nothing in the match. Every call on the link, the lobby (and its `ReliableLink`), the sink, the queue, and the pool is serialized by one lock (`StateQueue` is not thread-safe). The pool (five 1,400 B buffers, plus one outbound buffer) is allocated once at bring-up in PSRAM with a null check. Teardown stops and joins the task, then ends the link; a task that will not stop leaks its state rather than freeing it. `shipped()` is called only when the link took the snapshot. Code adapted from crossplay names its source file. A double names the device behaviour it stands in for and a test pins that they agree (Build-agent brief).

**Never:** any `lib/GameCore/**` change (a `GameCore` change returns to entry 7's scope: stop); `src/activities/**`, `src/games/EspNowLink.*`, `games/**`, any upstream file, `freeink-sdk`; a literal radio fact; `Serial.print*`; a `LINK` line without the `session` id; a source listed twice in the harness; relaying `STATE` between guests (more than two seats is deferred); reconnect.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Lobby to round | host and guest `NearbySession`s on their own tasks over `FakeLink`; guest joins, host starts, both `beginMatch` | both reach a started lobby; a round of `CounterRules` is played by two test-driven `Session`s through `adoptNext` and `ship`; both end with the same snapshot, ver, and status | none |
| Consumer stopped | guest never calls `takeState`; host ships | the guest's task keeps acking (host `unacked` reaches 0), the host's task resends over a lossy direction, and a silent peer is detected | none |
| Over held | queue holds an `over` STATE, newer STATEs arrive | the queue keeps the `over` and the newest later STATE; the consumer later drains both in order | none |
| Queue full | four `over` STATEs queued, a fifth arrives | the fifth is left unacked, one `LINK` overflow line carries the count, the resend delivers it once the consumer takes one | nothing lost |
| Lost peer | peer silent for the limit, consumer stopped | the end slot latches `Timeout`; `takeEnd` returns it once | never dropped |
| Ship | `ship()` called each step while the Session's shipment is pending | false until the link took it, then true once; a staged `over` is never replaced; a refused send is retried | `shipped()` only on true |
| Leave with flush | `leave(Left, 800)` with the frame acked, then with the peer unreachable | returns when acked or at the deadline, then three `ABORT`s on the link | deadline is the bound |
| Leave without flush | `leave(ScriptError, 0)` | three `ABORT`s put on the link, returns at once | none |
| Stranger | a third device sends `STATE` into the match | refused, forgotten (its slot is freed), counted; the peer still works | none |
| Ghost seat | the guest vanished before Start, the host starts | the host's end slot latches `Timeout` within the silence limit | never a hang |
| Teardown | session destroyed, with and without a match | no task is left, the link was ended, the PSRAM block is freed | a task that will not stop leaks, logged |
| Out of memory | PSRAM pool or lobby allocation fails | the factory returns null and ends the link | `LOG_ERR`, no task started |

</intent-contract>

## Code Map

- `lib/GameCore/ILink.h`, `ReliableLink.h`, `HostLobby.h`, `GuestLobby.h`, `Lobby.h` (`LobbyHandOff`), `MatchSink.h`, `StateQueue.h` (`IStateStore`), `EndSlot.h`, `Session.h` (`Shipment`, `PeerState`, `Adoption`), `Protocol.h`, `Roster.h` -- the contracts this entry runs; read-only. The lobbies own the `ReliableLink` (`link()`), stay its `Sink` after Start, and forward to the match sink once `handOff()` is called; `peerForSeat` names the peers.
- `src/games/GameVM.cpp`/`.h` (task creation `start()`, `taskEntry`, `run()`'s `done` flag and stack high-water line) -- the pattern for the task; read-only.
- `src/games/EspNowLink.h`, `FrameRing.h` -- the device `ILink` the wiring (entry 11) hands in; read-only.
- `src/games/GameClock.h`, `GameRandom.h` -- the `IClock`/`IRandom` providers the wiring passes; read-only.
- `test/game_core/FakeLink.h` (`FakeLinkNet`, mutex-protected), `LobbySupport.h` (`testGame`, `hostile`, `FixedRandom`), `MatchSupport.h` (`CounterRules`, `tapEvent`, `stateFrameOf`) -- reused read-only through the existing include path.
- `test/game_script/harness/screen_stubs/FakeRtos.h`, `freertos/task.h`, `Logging.h`, `ScreenDoubles.cpp`, `RenderLockProbe.h` -- FreeRTOS on threads with a clock the test moves, a thread-safe fake log, and the `RenderLock` double (its record gains the acquiring thread's kind).
- `test/game_script/harness/match.cmake` (`game_match_src`), `esp_now_link.sources.cmake`, `harness_base.sources.cmake` -- how a source list and a suite are written; `HARNESS_EXTRA_CORE_SOURCES` is where the new `lib/GameCore` sources go.
- New: `src/games/GameLink.{h,cpp}`, `GameLinkStore.{h,cpp}`, `GameLinkLog.h`, `NearbySession.{h,cpp}`; `test/game_script/harness/nearby_session.sources.cmake`, `nearby_session.cmake`, `NearbySessionSupport.h`, `GameLinkTest.cpp`, `GameLinkStoreTest.cpp`, `GameLinkLogTest.cpp`, `NearbySessionTest.cpp`, `NearbyLeaveTest.cpp`.
- Edited: `test/game_script/harness/screen_stubs/ScreenDoubles.cpp`, `RenderLockProbe.h` (the lock record).
- Not touched: `lib/GameCore/**`, `test/game_core/**`, `test/game_script/CMakeLists.txt`, `harness/CMakeLists.txt` (it globs new `*.sources.cmake` and `*.cmake`).

## Tasks & Acceptance

**Execution:**
- [x] `src/games/GameLink.{h,cpp}` -- the task: constants (4,096 B stack, priority 1, core 1, 10 ms step), `start()`, cooperative `stop(joinMs)`, `GameLinkWork` interface, stack high-water handed to the work on exit -- Design Note 3.
- [x] `src/games/GameLinkStore.{h,cpp}` -- `IStateStore` over one PSRAM block: five 1,400 B pool buffers plus the outbound buffer -- Design Note 2.
- [x] `src/games/GameLinkLog.h` -- every `LINK` line's text, one function each, plus `abortReasonName` -- Design Note 5.
- [x] `src/games/NearbySession.{h,cpp}` -- the factories, the lock, the lobby accessor, `beginMatch`, `takeState`/`releaseState`/`adoptNext`, `ship`, `takeEnd`, `leave`, `counters`, the sink gate, the stats reporter, teardown -- Design Notes 1, 2, 4, 5, 6.
- [x] `test/game_script/harness/nearby_session.sources.cmake` -- appends `Protocol.cpp`, `ReliableLink.cpp`, `Lobby.cpp`, `HostLobby.cpp`, `GuestLobby.cpp`, `StateQueue.cpp`, `MatchSink.cpp` to `HARNESS_EXTRA_CORE_SOURCES` and `GameLink.cpp`, `NearbySession.cpp` to `HARNESS_EXCLUDE_GAMES` -- Design Note 7.
- [x] `test/game_script/harness/nearby_session.cmake` and the five test files, `NearbySessionSupport.h` -- Design Note 7's tests; a mutant per rule, each red.
- [x] `test/game_script/harness/screen_stubs/ScreenDoubles.cpp`, `RenderLockProbe.h` -- count acquisitions and attempts made on a task thread (`fakertos::current`) -- Design Note 7.

**Acceptance Criteria:**
- Given two `NearbySession`s on their own tasks over `FakeLink`, when the guest joins and the host starts, then both reach a started lobby and two driven `Session`s exchange a round to the same final state.
- Given the consumer stopped, when frames arrive, are lost, or the peer goes silent, then the task acks, resends, and latches the silence, and the end slot holds the reason until read once.
- Given an `over` STATE queued, when newer STATEs arrive, then the queue keeps both; given the queue full, when a STATE arrives, then it is left unacked and delivered on a resend.
- Given the whole suite, when run, then the `RenderLock` double records no acquisition or attempt from any task thread and no `ActivityManager` call.
- Given a destroyed session, when its task is awaited, then no task remains, the link was ended, and no PSRAM block is live.
- Given each `LINK` event, when it happens, then exactly one line of the pinned text is logged naming the `session` id (STATE lines their `ver`).
- Given `leave(reason, flushMs)`, when `flushMs` is 800 and the frame is acked, then it returns at the ack; when the peer is unreachable, then it returns by the deadline; when `flushMs` is 0, then three `ABORT`s are on the link and it returns at once.
- Given the existing suites, when run, then every test passes unchanged and the new `lib/GameCore` sources appear in the harness exactly once.

## Implementation Notes

- Environment (written by the build session for the implementer): work only in the git worktree `/home/user/wt/epic-play-nearby-lane-a` (branch `epic-play-nearby-lane-a`), never in `/home/user/crosshatch-player`; use absolute paths under it. Host tests build in this worktree's `build/test` with AGENTS.md's commands under `flock /tmp/crosshatch-hosttest.lock`; a full rebuild is slow, so configure once. Scratch files go under `/tmp/claude-0/-home-user-crosshatch-player/f3b56526-12a6-5481-9360-b3275a33119e/scratchpad/6.8/`. The checked-in `lib/GameCore/**` and `test/game_core/**` are read-only for this story. Never run `pio`, `pio check`, or `sim.sh` (the build session runs them after the review). Do not `git commit`. The crossplay clone is at `/tmp/claude-0/-home-user-crosshatch-player/f3b56526-12a6-5481-9360-b3275a33119e/scratchpad/crossplay-src/crossplay` (read only; MIT, name the source file where you adapt). Baseline firmware figures at `37d922ae` (x4pro games on minus off, measured in this worktree): +258,672 B flash, +784 B static RAM.
- 2026-10-12, implemented by one subagent as the plan reads, then patched in the main session after the review. Deviations from the plan's wording: the sink gate is its own header `src/games/GameLinkSinkGate.h` (renamed from the subagent's `NearbySinkGate.h`, which was outside the entry's `touches`), so the stranger test can drive it directly; `GameLinkStore.cpp` stays in the shared source glob and is not listed again in `game_nearby_src`; `NearbySessionOptions` is declared at namespace scope (a nested struct with default member initializers cannot default its own class's argument); `GameLink::ticksFor` is the pure tick conversion; `task started` is logged by the first task step; `session()` reads an atomic refreshed each step; the lobbies already `forget()` a stranger before the match sink sees it, so at session level the sink's `strangers` counter stays 0 and the gate's own `forget()` is the second guard (tested on the gate directly, where the mutant that removes it is red).
- `ReliableLink::send` returns true once a frame is queued, even if the adapter refused its first transmission, so a refused `ILink::send` does not keep a shipment staged: `ReliableLink` retransmits it and the `link refused` line counts it. `ship()` retries only when `ReliableLink::send` itself returns false (queue full, peer lost), which the ship tests force by filling its 4-deep queue.
- Host evidence: full `ctest` 2,406 of 2,406 (59 new tests; `ctest -N` before was 2,301 + the implementer's count, and the delta is the new suite only); the 59 new tests pass 30 and 50 times under `--repeat until-fail` with `-j4`; the implementer's nine mutants were each red alone (ship true at staging, replacing a staged `over`, a refused send clearing the slot, the gate not forgetting, `leave` with one copy, `leave` without a deadline, the destructor not ending the link, `linkStep` taking `RenderLock`, a changed line text), and the review added two (the staged-shipment wait in `leave`, red 5 of 5; a task that waits less than configured, red).

## Plan Change Log

## Review Triage Log

### 2026-10-12 — Review pass
- lens method: four context-free subagents (blind-hunter, edge-case-hunter, verification-gap, intent-alignment), each started in one message and awaited; the diff given to them (`review-diff.patch`, 123 kB, `src` and `test` since `37d922ae`) leaves out this plan, which the edge-case lens read as its claims file. The implementation subagent had finished, so the patches below were applied in the main session (small edits). Patches were checked by the host suites (59 new tests pass 50 times; 2,406 of 2,406) and two by a mutant (the staged-shipment wait in `leave` red 5 of 5, a task that waits less than configured red).
- verdicts: 39 findings (blind 14, edge-case 18 including 3 claim checks, verification-gap 3 gaps + 3 others, intent-alignment 1 descriptive row) — high 0, medium 0, low 29, false 10, maybe-false 0; 11 distinct patches (all low), 5 deferred items (all low), the rest rejected. The Edge and Blind numbers are the order each lens listed them.
- findings:
  - Blind 1 / Edge 5 `[low]` `[patch]` `start()` after a stopped `stop()` returned true with no task — fixed: `start()` returns `running()` when `started`, and `AStoppedGameLinkStartsNoSecondTask` pins both answers.
  - Blind 2 / Edge 4 `[low]` `[patch]` `stop()`'s bound stretches at a 100 Hz tick (2 ms rounds to one 10 ms tick) — fixed: the poll count uses the longer of `STOP_POLL_MS` and `portTICK_PERIOD_MS`.
  - Blind 3 / Edge 7 / Edge 8 / Edge 13 / Edge 14 `[false]` `[reject]` a match with no reachable peer, `copies` with zero peers, `startHost` ignoring `beginMatchLocked`, a guest seat of 0 — cannot happen: `HostLobby::start()` returns Started only with at least one seated guest whose `peerForSeat` is valid (`start()` needs `canStart()`), and `GuestLobby` is Started only with a valid `joined_.handle` and the seat from ACCEPT (1..n, `Protocol` refuses 0).
  - Blind 4 `[low]` `[patch]` the destructor sends no goodbye in the lobby phase — by design (R3: the lobby's `cancel()`); the header now says so, and entry 10 is told (deferred).
  - Blind 5 / Edge 9 `[low]` `[reject]` `leave()` twice sends three more ABORTs and another line — ABORT is unsequenced and idempotent for the peer; a second call needs a caller bug; the guard would add state for no reader.
  - Blind 6 `[low]` `[reject]` the 4 KB stack and priority 1 are unmeasured — the ticket's `unknown` names it; entry 11's smoke run asserts the device figure (the high-water line exists).
  - Blind 7 `[low]` `[patch]` `framesSent - resends >= 5` arithmetic in `AFullQueueLeavesTheFifthUnacked...` — fixed: `EXPECT_GE(framesSent, 5u)` on one snapshot.
  - Blind 8 `[low]` `[patch]` the silence bounds `1500 + 1500` are twice the limit — tightened to `1500 + 500` and the messages say "plus a few steps"; 50 repeats pass.
  - Blind 9 `[low]` `[patch]` `GameClockAgreementTest` cross-reference stale — fixed to `GameLinkClockTest.FakeRtosClockAgreesWithGameClock`; the other two comment nits (poll constants named in prose, "one function each") rejected as cosmetic, the `GameLinkLog.h` line wording about the four counter lines now says one line per step.
  - Blind 10 `[low]` `[patch]` the class contract comment sat above `NearbySessionOptions` — fixed: the options struct moved above it.
  - Blind 11 `[false]` `[reject]` `GameLinkStoreTest` clears `fakelog::lines` directly — it runs no task; 20 other suites do the same with the shared log double.
  - Blind 12 `[low]` `[reject]` destroying a session while a `Lobby` guard is alive would deadlock into the leak path — a caller bug the header forbids ("never held across a wait"); the leak path has its own test.
  - Blind 13 `[false]` `[reject]` a guest's `session()` stays 0 until its first step — correct and documented (`00000000` before a guest joined, `GameLinkLog.h`); a step is 10 ms.
  - Blind 14 `[low]` `[reject]` no device compile for the new files, headers unguarded — the firmware checks in Verification build them (results below); `GameVM.h` and the other `src/games` headers are unguarded the same way, and nothing includes them outside `FREEINK_CAP_GAMES` code.
  - Edge 1 / Edge 16 `[low]` `[patch]` `finish()` returned null on a failed `new` without ending the link — fixed (`core->link->end()` first; the constructor had not run, so `core` still owns it). The allocation failure itself cannot be injected on the host, so no test.
  - Edge 2 `[low]` `[reject]` the leak path does not end the link — deliberate (Design Note 3): the task may be inside a link call and `ILink` is one task at a time.
  - Edge 3 `[low]` `[reject]` `joinMs` below one poll or below `stepMs` declares a healthy task stuck — option misuse; the defaults are 250 and 10 and every test sets a sane pair.
  - Edge 6 `[low]` `[defer]` with three or more seats one permanently refusing peer would hold the staged slot — two seats in v1 (a lost peer ends the match through the end slot); relaying between guests is deferred in the spine.
  - Edge 10 `[false]` `[reject]` `ship` of another version while one is `Taken` loses the answer — latest wins by design: the Session's pending shipment is the newer snapshot and `shipped()` clears it.
  - Edge 11 `[low]` `[reject]` `ship()` before `beginMatch` stages against no peers — harmless: nothing is sent until the match begins, then it is.
  - Edge 12 `[false]` `[reject]` the gate may return a non-Refused verdict for a stranger — `MatchSink::onFrame` returns `Refused` for every handle with no seat, and the gate returns that verdict.
  - Edge 15 `[low]` `[defer]` the clock and random must outlive a leaked task — documented as the caller's for the session's life; the leak case outlives the session, so entry 9 (the owner) should keep them process-long.
  - Edge 17 `[false]` `[reject]` claim: `leave` sends nothing without a begun match — stated in the header and the plan ("before `beginMatch` it sends nothing"); `NearbyLeaveTest.BeforeTheMatchBeganItSendsNothing` pins it.
  - Edge 18 `[low]` `[reject]` the four counter lines are one per step, not per event — Design Note 5 says so; the header comment now says it.
  - Verification-gap 1 `[low]` `[patch]` the one-tick floor of `sleepMs` was never exercised (the host tick is 1 ms) — fixed: the conversion is the pure `GameLink::ticksFor(ms, tickPeriodMs)`, tested at 1 and 10 ms ticks.
  - Verification-gap 2 `[low]` `[patch]` `TheStepWaitIsTheConfiguredOne` passed with a shorter wait — fixed: the work records the least gap between two steps and the test asserts it is at least `stepMs`; a task that waits 2 ms is red.
  - Verification-gap 3 `[low]` `[patch]` `leave` waiting for a staged shipment was not pinned — fixed: the test uses a 60 ms step, waits for nothing unacked, then asserts `state sent ver=1` is logged once before `leave` returns; the mutant that drops the Staged condition is red 5 of 5.
  - Verification-gap other 1 (`finish()` allocation), other 2 (stale cross-reference) `[low]` — same rows as Edge 1 and Blind 9 (patched).
  - Verification-gap other 3 `[low]` `[reject]` the high-water test pins the plumbing, not the 4 KB — the ticket's `unknown`; entry 11.
  - Intent-alignment (descriptive) `[low]` `[defer]` the readings: link-side component proven in the harness (implemented); the intent's device surface (`EspNowLink`, `UdpLink`, priority and core, the real VM task) is outside this entry's harness by the entry's own wording (entries 11 and 16, "the harness value is a constant"). Two follow-ups are deferred below: priority and core are not observable in the FakeRtos double, and `game_nearby_src` links `game_match_src`, so entry 9 moving `NearbySession` into the match must restructure the two sources' build.

## Design Notes

Sources that settle the choices: epic R5 (queue, latched slot, task never takes `RenderLock`), R6 (flush at most 800 ms, then `ABORT(left)` three times; forced exit three times without flushing), R7 (`NearbySession` and the task, priority 1, core 1, 4 KB, against `ILink`), R13 (`LINK` lines name `session`, `STATE` lines `ver`); spine AD-18 (task, "exchanges session events ... through queues", teardown never takes `RenderLock`) and AD-20 (`NearbySession` is passed by move into the match); the epic Notes Decisions of 2026-10-11 (queue, Q1) and 2026-10-12 (entry 7's checkpoint, entry 2's link contract, the ghost-seat Assumption); entry 7's plan Design Notes 3 and 4 and its Residual risks; crossplay `LinkSession.cpp` (`kByeRepeats = 3`, `leave()`), read only.

### 1. Shape

`NearbySession` is a non-copyable object created by `openHost(link, clock, random, game, label, settings, options)` or `openGuest(link, clock, game, settings, options)`; the caller keeps `clock` and `random` alive for its life. It hands the match half (the `ReliableLink` is the lobby's) to a pimpl `Core` so a task that will not stop can be leaked without freeing what it touches. `Core` holds, in destruction-safe order: the `ILink`, the lobby (host or guest), the pool, the `StateQueue`, the `EndSlot`, the sink gate (an `std::optional`, built at `beginMatch`, so Start allocates nothing), the outbound slot, the lock, and the `GameLink`. The factory allocates the lobby with `new (std::nothrow)`/`makeUniqueNoThrow`, the pool with `GameLinkStore::create`, checks `ok()` of each, starts the task last, and on any failure ends the link and returns null.

The lobby phase uses the same task: it steps the lobby (`tick()` runs the `ReliableLink` pump and the ADVERTs). The lobby screen reaches the lobby through `Lobby lobby()`, an RAII guard that holds the lock and gives `host()` or `guest()`; never held across a render or a wait. `startHost()` is `HostLobby::start()` plus `beginMatch()` under one lock; a guest whose state reads `Started` calls `beginMatch()`. `beginMatch()` copies the roster, builds the `SeatPeer` table from `peerForSeat` for every seat this device does not play, builds the gate over a `MatchSink`, and calls `handOff(gate)`, which replays an ABORT or silence the lobby latched.

The public surface (shape, not final spelling; every call thread-safe, taking the lock itself):

```cpp
class NearbySession {
 public:
  struct Options { uint32_t stepMs = GameLink::STEP_MS; uint32_t joinMs = GameLink::JOIN_MS; };
  enum class Role : uint8_t { Host, Guest };
  struct NearbyState { GameCore::PeerState peer; GameCore::QueuedState queued; };  // peer.snapshot aliases the pool buffer
  struct AdoptStep { bool taken = false; GameCore::Adoption result = GameCore::Adoption::NotPeer;
                     GameCore::Outcome outcome = GameCore::Outcome::Ok; uint16_t ver = 0; uint8_t fromSeat = 0; };
  struct LeaveResult { uint32_t waitedMs = 0; size_t unackedAtSend = 0; uint8_t copies = 0; };
  struct Counters { GameCore::ReliableLink::Stats link; size_t unacked = 0; uint32_t queueOverflows = 0, queued = 0,
                    replaced = 0, strangers = 0, endIgnored = 0; size_t queueSize = 0; };
  class Lobby { /* RAII: holds the lock */ public: GameCore::HostLobby* host() const; GameCore::GuestLobby* guest() const; };

  static std::unique_ptr<NearbySession> openHost(std::unique_ptr<GameCore::ILink>, const GameCore::IClock&, GameCore::IRandom&,
      const GameCore::LobbyGame&, std::span<const uint8_t> label, const GameCore::LobbySettings& = {}, const Options& = {});
  static std::unique_ptr<NearbySession> openGuest(std::unique_ptr<GameCore::ILink>, const GameCore::IClock&,
      const GameCore::LobbyGame&, const GameCore::LobbySettings& = {}, const Options& = {});
  ~NearbySession();                      // stops the task, ends the link, frees the pool
  Role role() const;  uint32_t session() const;  bool taskRunning() const;
  Lobby lobby();                         // lobby phase; never held across a render or a wait
  GameCore::StartResult startHost();     // host: HostLobby::start() then beginMatch(), one lock
  bool beginMatch();                     // roster copy, SeatPeer table, sink gate, handOff; false unless the lobby Started; idempotent
  bool matchBegun() const;  GameCore::Roster roster() const;  uint8_t localSeat() const;
  bool takeState(NearbyState&);  void releaseState(NearbyState&);
  AdoptStep adoptNext(GameCore::Session&);
  bool ship(const GameCore::Shipment&);  // false until the link took it, then true once
  bool takeEnd(GameCore::Protocol::AbortReason&);  bool endLatched() const;   // lock-free
  LeaveResult leave(GameCore::Protocol::AbortReason, uint32_t flushMs);
  Counters counters() const;
};
```

### 2. The lock, and the three handoffs to the VM task

One `std::mutex` (as `GameVM` uses) serializes every call on the link, lobby, `ReliableLink`, sink, queue, and pool. The task takes it for one step; every public call takes it for its own short body; none holds it across a Lua call, a render, or a wait. `EndSlot` is lock-free and read without it.
- **Receive (GameLink to VM):** `takeState(NearbyState&)` takes the oldest queued STATE under the lock and returns its `PeerState` (the snapshot aliases the taken pool buffer, which only the consumer touches until `releaseState` returns it, again under the lock: `acquire` hands out free buffers only, and replacement touches only queued entries). `adoptNext(Session&)` is the convenience the match calls once per step: it does nothing while `session.pending()` (a taken entry `adopt` answers `Busy` is lost: `Session.h`), takes, calls `adopt` outside the lock, releases, and logs the verdict. The match adopts at most one entry per step (entry 7, Design Note 2).
- **Send (VM to GameLink):** `ship(const Shipment&)` is the pull call. The first call for a version copies the snapshot into the outbound buffer and marks it staged and returns false; the next task step builds `Frame{State}` and calls `ReliableLink::send` to each peer (a false is an ordinary loss: retried every step); when every peer's send returned true the slot is marked taken, the `LINK` line is logged, and the next `ship()` for that version returns true and empties the slot. The match calls `shipped()` on true only. A staged `over` is never replaced by a different version (R2; entry 7 C6: the match defers Play again until `!shipmentPending()`, which this keeps true until the link took the `over`); a newer non-`over` staged one is replaced (latest wins). A snapshot over 1,400 B or empty returns false and logs once.
- **End (GameLink to VM):** `takeEnd(AbortReason&)` is `EndSlot::take`; `endLatched()` is `peek`. Both are lock-free.

The pool is `GameLinkStore`: one PSRAM block of six 1,400 B buffers, `capacity() == 5` for the queue (four queued and one being adopted: entry 7 DN4) and the sixth the outbound buffer. A free mask guards double release (counted). 8,400 B of PSRAM, none of internal RAM.

### 3. The GameLink task

`GameLink` mirrors `GameVM::start`/`taskEntry`: `xTaskCreatePinnedToCore(..., 4096, ..., priority 1, ..., core 1)` (the stack depth is in bytes on ESP-IDF, as `GameVM::TASK_STACK_BYTES` is), the entry runs `while (!quit) { work.linkStep(); vTaskDelay(step) }`, then `work.linkStopped(uxTaskGetStackHighWaterMark(nullptr))`, then publishes `done` with release and ends with `vTaskDelete(nullptr)` touching nothing of the object. The step wait is `vTaskDelay`, not a timed notify: the simulator's FreeRTOS shim ignores `ulTaskNotifyTake`'s timeout (`freertos/task.h` of `crosspoint-simulator`: it waits for a notify), and `vTaskDelay` is the one wait the device, the shim, and `FakeRtos.h` share. `stop(joinMs)` sets `quit` and polls `done` every 2 ms for `joinMs / 2` polls (counted, not clocked, so a stopped test clock cannot hang it); false when the task did not end. `NearbySession`'s destructor calls it; on false it logs `LOG_ERR` and `release()`s `Core` (a leak, as `GameVM::abandon`'s leak of the object, but the task is not stuck in Lua: a link call never blocks on the air, `ILink.h`). The step is 10 ms (R5/R7 give none; `LobbySupport.h`'s `STEP_MS` and crossplay's per-loop tick are of that order); `Options::stepMs` lets a test use 1 or 2. An estimate until entry 11 measures battery and latency (R12).

### 4. Leave and abort: one call

`leave(AbortReason reason, uint32_t flushMs)` returns `LeaveResult{waitedMs, unackedAtSend, copies}`. With `flushMs > 0` it polls (no lock held) `unacked()` every 5 ms of `vTaskDelay` until it reads 0, `flushMs` of `IClock` time have passed, or `flushMs / 5 + 2` polls were made (so a clock that never moves cannot hang it), and the task keeps resending and acking meanwhile; then, under the lock, it sends `Frame{Abort, reason}` three times to every peer (`CANCEL_ABORT_COPIES`, crossplay `kByeRepeats`), reports the stats, and logs the `leave` line. With `flushMs == 0` it skips the wait and sends at once (the forced exit, `ABORT(left)` or `ABORT(script_error)`). The bound (800 ms for Leave) is the caller's, entry 15's. Before `beginMatch` it sends nothing (the lobby's `cancel()` is the lobby phase's goodbye). The ABORT is unsequenced and ignores unacked frames (R2).

### 5. LINK lines

All through `LOG_INF("LINK", ...)`, text from `GameLinkLog.h`, `session` the 8-digit lower-case hex id (`00000000` before a guest joins). One line per event, never duplicated:
- `session=%08x state sent ver=%u` (the task, when every peer's send returned true)
- `session=%08x state recv ver=%u seat=%u` (the gate, Taken)
- `session=%08x state adopt ver=%u seat=%u result=%s` (`adoptNext`; `Adopted`, `RoundStarted`, `NotPeer`, `Busy`, `BadSnapshot`, `Stale`, `OutOfTurn`, `BadStatus`)
- `session=%08x queue full ver=%u overflows=%u` (the gate, `NoRoom`; the running count is `StateQueue::overflows()`)
- `session=%08x abort recv reason=%s seat=%u` (the gate; reasons `left`, `script_error`, `timeout`, `full`, `version_mismatch`)
- `session=%08x leave reason=%s waited=%ums unacked=%u copies=%u` (`leave`)
- `session=%08x resend n=%u total=%u`, `session=%08x silence n=%u total=%u`, `session=%08x abort sent n=%u total=%u`, `session=%08x link refused n=%u total=%u` (the stats reporter: it diffs `ReliableLink::Stats` against the last report at the end of every step, of `leave`, and once at task exit, so each counter step is logged once; `link refused` answers the entry 5 deferral that the adapter's refused sends were uncounted)
- `session=%08x task started; stack %u bytes`, `session=%08x task stopped; stack high-water %u bytes free` (the stack figure is a constant in the harness; entry 11 asserts the device's)

A test per line pins the exact text, and the flow tests assert the lines appear.

### 6. Strangers, silence, and the ghost seat

`MatchSink` does not call `ReliableLink::forget()` for a stranger it refuses (entry 7's Residual risks). The gate wraps it: after the sink answers, a frame or silence report from a handle with no seat is followed by `forget(handle)` (allowed inside `onFrame`/`onPeerSilent`, `ReliableLink.h`), so a third device cannot fill the peer table; a test sends from three more endpoints than `maxPeers` and asserts `peerTableFull` stays 0 and the real peer still works (red without the forget). The ghost-seat Assumption (epic Notes, 2026-10-12, for entries 8 and 10): Start on a guest that vanished is ended by the silence path: the host's end slot latches `Timeout` within the limit, tested with a guest whose link ended before Start. Offering Start only while the guest is live is entry 10's.

### 7. Harness and tests

`nearby_session.sources.cmake` lists the seven `lib/GameCore` sources entries 6 and 7 added and no harness list had (`Session.cpp`, `Roster.cpp`, `MatchLifecycle.cpp`, `Manifest.cpp` are listed already by `match.cmake`, `pass_and_play.sources.cmake`, `harness_base.sources.cmake`; entries 9, 15, and 10 link `game_match_src` plus `game_harness_core` and get the seven from here). `GameLink.cpp` and `NearbySession.cpp` include `freertos/task.h`, so they leave the shared libraries (`HARNESS_EXCLUDE_GAMES`) and `nearby_session.cmake` builds them with `GameLinkStore.cpp` and `Session.cpp` in `game_nearby_src` against the screen doubles, linking `game_match_src` for the `RenderLock` and `ActivityManager` doubles. Doubles (each named in its header comment):
- `FakeRtosClock` stands in for `GameClock` (esp_timer): `IClock` over `fakertos::millis()`, moved by the test and by every `vTaskDelay`; more permissive: it runs faster than real time while tasks sleep.
- `ObservedLink` stands in for `EspNowLink`/`UdpLink`: forwards to a `FakeLink` the test keeps and records `end()`; `NearbySessionTest` pins that a destroyed session calls it. More permissive than the device: `FakeLink`'s (its header lists them).
- The VM-task stand-in is the test thread calling `adoptNext`, `ship`, `Session` (`CounterRules`), as `MatchPeer::step` does in one thread; more permissive than the device: no Lua, no overlay.
- `RenderLockProbe.h` gains `taskAcquisitions` and `taskAttempts`, counted when `fakertos::current` is set; a self-test shows a task that takes the lock is counted (so the "none from the task" assertion is not vacuous).
Tests wait on conditions (a poll every real millisecond that also moves the fake clock by 2 ms, capped at 10 s real), never on a fixed sleep; time bounds are fake-clock figures, estimates until a device run (about 95x host to device, `plan-e4-z1` M1, does not apply: no host speed is used). Mutants, each red alone: `ship` true at staging; `ship` replaces a staged `over`; a refused send clears the slot; the gate does not forget; `leave` sends one copy; `leave` has no deadline; the destructor does not end the link; `linkStep` takes `RenderLock`; a changed line text.

### 8. Measurement

Nothing in firmware references `NearbySession` until entries 9 to 11, so the linker drops it: expected +0 B flash and +0 B static RAM (an estimate). Measured like for like in this worktree: `check_flash_budget.py build on`, `build off`, `compare` at `37d922ae` before any edit, and at the commit after the last patch, under the build lock; the delta is the difference. The epic's projection is the orchestrator's.

## Verification

**Commands:**
- Host suites (Ninja `build/test`, AGENTS.md): `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test'`, then `ctest --test-dir build/test --output-on-failure -j` -- expected: every test passes; `ctest -N` differs from before by the new tests only.
- `ctest --test-dir build/test -R 'GameLink|NearbySession|NearbyLeave' --repeat until-fail:50` -- expected: all pass 50 times.
- Mutants (Design Note 7) -- expected: each red against its test, then reverted.
- Fast checks: `python3 scripts/check_layers.py`, `python3 scripts/check_upstream_touches.py`, `./bin/clang-format-fix` twice (`git add` the new files first), `git status` unchanged by the second run.
- Firmware, once, after the review and its patches, under `flock /tmp/crosshatch-build.lock` with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`: `pio run -e x4pro`, `pio run -e default`, `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` and the same with `-e x4pro`, `scripts/check_flash_budget.py` `build on`, `build off`, `compare`, `objects`, and `.claude/skills/run-crosshatch-player/sim.sh build x4pro` -- expected: success; delta reported with its method (Design Note 8).

**Manual checks (if no CLI):**
- Read `NearbySession.h`: no `RenderLock`, `ActivityManager`, or `Serial` include; every public call says which task calls it.

## Auto Run Result

Status: `built`. Reviewed by four lenses (`lenses_ran`); 11 distinct low patches applied, 5 low items deferred, nothing high or medium.

**Summary:** `NearbySession` (src/games) owns an `ILink` handed to it, the lobby (host or guest) and the `ReliableLink` the lobby owns, the PSRAM pool (`GameLinkStore`: five 1,400 B queue buffers and the outbound buffer, 8,400 B, allocated at bring-up), the `StateQueue`, the `EndSlot`, and the `GameLink` task (priority 1, core 1, 4,096 B stack, 10 ms step, cooperative stop). One lock serializes every call on the link, lobby, sink, queue, and pool. The match gets `beginMatch`, `takeState`/`releaseState`/`adoptNext`, `ship` (false until the link took the snapshot, then true once), `takeEnd`, and `leave(reason, flushMs)` (flush until acked or the deadline, then three `ABORT`s; `flushMs` 0 sends at once). Teardown stops and joins the task and ends the link; a task that will not stop leaks its state and logs. The `LINK` lines come from `GameLinkLog.h` (pinned text, each naming the `session`). The harness gains `nearby_session.sources.cmake` (the seven `lib/GameCore` sources no harness list had) and the `NearbySessionHarnessTest` suite over `FakeLink` on real threads.

**Files:**
- `src/games/GameLink.{h,cpp}` -- the task, `ticksFor`, `stop(joinMs)`.
- `src/games/GameLinkStore.{h,cpp}` -- `IStateStore` over one PSRAM block.
- `src/games/GameLinkLog.h` -- every `LINK` line.
- `src/games/GameLinkSinkGate.h` -- the sink behind the lobby: `MatchSink` plus the `LINK` lines and `forget()` of a handle with no seat.
- `src/games/NearbySession.{h,cpp}` -- the session.
- `test/game_script/harness/nearby_session.sources.cmake`, `nearby_session.cmake`, `NearbySessionSupport.h`, `GameLinkTest.cpp`, `GameLinkStoreTest.cpp`, `GameLinkLogTest.cpp`, `NearbySessionTest.cpp`, `NearbyLeaveTest.cpp` -- the suite.
- `test/game_script/harness/screen_stubs/ScreenDoubles.cpp`, `RenderLockProbe.h` -- the `RenderLock` double counts acquisitions and attempts made on a task thread.
- This plan.

**Review breakdown:** 39 findings — high 0, medium 0, low 29, false 10. Patched (all low): `start()` after `stop()` (returns `running()`); `stop()`'s bound at a 100 Hz tick; the doc comment on the lobby-phase goodbye; the counter arithmetic in a test; the silence-limit test bounds; a stale cross-reference; the class comment's place; `finish()` not ending the link on a failed allocation; the pure `ticksFor` and its test; a step-cadence assertion that a shorter wait fails; the staged-shipment wait in `leave` pinned. Deferred: three-seat slot hold, clock lifetime with a leaked task, the lobby must call `cancel()` before destroying a session, priority and core unobservable in the fake, entry 9's restructure of the two sources' build. Rejected with reasons (Review Triage Log): unreachable no-peer cases, repeated `leave`, the unmeasured stack (the ticket's `unknown`), the leak path not ending the link, and the rest. Follow-up review recommended: `false` (no high patched, no medium).

**Verification performed** (on the working tree at the commit's content; the firmware figures were taken after the last patch):
- Host: full `ctest` 2,406 of 2,406 pass; `ctest -R 'GameLink|NearbySession|NearbyLeave' --repeat until-fail:50 -j4` passed (and 30 and 20 after the later patches); mutants as in Implementation Notes.
- Fast: `scripts/check_layers.py` passed; `scripts/check_upstream_touches.py` PASS (no upstream file touched); `./bin/clang-format-fix` twice, the second changing nothing; no formatting change outside this story's paths.
- Firmware, under the build lock, `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`: `pio run -e x4pro` and `-e default` SUCCESS; `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` passed for `default` and for `-e x4pro` (after fixing three `accessMoved`, two `shadowFunction`, and one `useStlAlgorithm` finding the first x4pro run reported); `scripts/check_flash_budget.py` `build on`, `build off`, `compare`, `objects` exit 0 ("No problems", 61 game objects, largest mutable static 4 B); `sim.sh build x4pro` SUCCESS (it caught `TickType_t` and `pdPASS`, which the simulator's FreeRTOS shim does not define, and the fix names neither).
- Cost, like for like (x4pro games on minus off, `check_flash_budget.py`, an incremental tree in this worktree; base `37d922ae` before any edit, then the tree of the commit): flash +258,672 B -> +258,672 B = **+0 B** (games on 6,213,984 B both times, off 5,955,312 B); static internal RAM +784 B -> +784 B = **+0 B**. Nothing in firmware references `NearbySession` yet, so the linker drops it; the cost lands when entries 9 to 11 reference it. Against the epic base (+258,048 B / +784 B) this tree reads +624 B flash (entries 6.1, 6.7 and earlier), +0 B RAM. The orchestrator re-measures the combined tree.
- No simulator screenshots: no screen changed.

**Residual risks:**
- The 4 KB stack, the 10 ms step, priority 1 and core 1 are R7's figures and estimates until entry 11's two-device run (the high-water line is logged at task stop; the harness value is a constant).
- Everything ran on `FakeLink` and `FakeRtos` threads: no radio, no real cross-core timing, no overlay. `EspNowLink` and `UdpLink` meet this code in entries 11 and 16.
- A task that will not stop leaks the session's state (logged); a link call never blocks on the air, so it is unreachable unless an adapter breaks that contract.
- `start()` while an `over` shipment is unshipped (entry 7's C6): `ship()` keeps a staged `over` and refuses another version until the link took it, but only once the match has offered it; the match must still defer Play again until `!shipmentPending()` (entry 9).
- The deferred items above (all low).
