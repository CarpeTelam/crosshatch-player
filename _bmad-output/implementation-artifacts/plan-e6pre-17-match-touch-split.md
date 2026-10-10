---
title: 'e6pre-17: split the touch tagging, the canvas gate, and the aimed-gesture decision out of GameMatchActivity'
type: 'refactor'
ticket: ''
created: '2026-10-10'
baseline_revision: 'db457b36e0b4398c50d4e4d5023b1d5802f09a67'
status: 'built'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
followup_review_recommended: false
deferred:
  - summary: 'CanvasGate::frameAt treats doneMs == 0 as no push recorded, so a push completing at millis() == 0 (the 2^32 wrap) reads as none; pre-existing from 17d02483, moved verbatim'
    location: 'src/games/CanvasGate.h frameAt / recordPush'
    severity: low
context:
  - '{project-root}/docs/crosshatch/game-canvas.md'
  - '{project-root}/_bmad-output/implementation-artifacts/plan-e6pre-1-split-gamematchactivity.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** `GameMatchActivity` (912 lines `.cpp`, 323 `.h`) still carries the touch-tagging state, the round and seat gate counters, and `loopPlaying`'s choice of what to do with an aimed gesture (`deferred-work.md` `## e6pre-1`, "Not extracted, left for a later split"). Epic play-nearby's first story (a partly-remote roster) would change all three inside that activity. The owner decided the split (Decision `e6pre-17`, epic-play-nearby Notes, 2026-10-10).

**Approach:** Behaviour-preserving extraction of three units into new fork-only files in `src/games`: the touch latch (part 1), the canvas gate with the frame-on-panel record (parts 1 and 2), and the aimed-gesture decision as a pure function (part 3). The activity keeps reading the hardware, calling `postInput`, and logging.

## Boundaries & Constraints

**Always:** Behaviour, log text, and timing identical. Every log line keeps its text ("dropped a touch before the frame was on the panel", "dropped a touch that began", the `GameTouchLog` lines). Every memory order stays, with its comment: `frameDisplayed` stored before `roundsDisplayed` and `seatDisplayed` with release and read with acquire; lock order RenderLock then `pushMutex` (the loop takes `pushMutex` alone); `renderCanvas`'s `frameGen` read stays before the view's `clearScreen` (the frame-race test's seam). Same read points: a value read lazily today (`millis()`, `lastTouchHeldMs`, `frameAt`, `frameGen`) stays read only on the path that reads it. Every guard in a moved function stays, with its comment (Design Notes). No new layer edge (`scripts/check_layers.py`); the new files include only the standard library. Existing host tests pass unchanged (`GameMatchTest`, `ResumeMatchTest`, `GameVmTest`, the frame-race test among them). No heap allocation; locals under 256 bytes.

**Never:** Back-date the latch itself (`deferred-work.md` `## e5-xr`; the split makes it a one-place change in `TouchLatch::latch`). Touch `## e6pre-1`'s `setWritable` mapping or the `MatchPersistence` tests, `firstFramePushing`'s owner (it stays in the activity and is passed in), `loopHandOff`, `loopView`, the render of views. Edit an upstream file, a test's expectations, `.skills/`, the SDK pointer, `SECTION_FILE_VERSION`, or `deferred-work.md`.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Latched frame | aimed, gate open, contact latched at frame 7, panel now 9 | Post, tag 7 | none |
| Unlatched | aimed, gate open, not latched, panel 9 | Post, tag 9 | none |
| Tap back-dated | tap, `frameAt(now - held)` is 8, shown 9 | Post, tag 8; a swipe never asks `frameAt` | none |
| UNTAGGED | tag would be UINT32_MAX (either path) | Post, tag UINT32_MAX - 1 | none |
| Before display | aimed, `awaitingDisplay`, not first-frame | DropBeforeDisplay (activity logs) | none |
| First frame accepted | `firstFramePushing`, `!awaitingRound`, began >= `playingSinceMs` | Post, tag = VM's current frame | none |
| First frame dropped | same, began before `playingSinceMs` | DropBeforeHandOff, `beforeHandOffMs` = today's `-static_cast<int>(static_cast<int32_t>(began - playingSinceMs))`, wrap included | none |
| Gate wait | `roundsStarted < awaited` or `seatShownRequest < seat` | `round` and `display` both true; `seatShownRequest` read only when the first compare passes | none |
| Awaiting round | aimed, `awaitingRound` | `firstFramePushing()` is never read; DropBeforeDisplay | none |

</frozen-after-approval>

## Code Map

- `src/activities/games/GameMatchActivity.{h,cpp}` -- the class being split; `.cpp` is built unchanged by the match harness (`test/game_script/harness/match.cmake`). Sites: `handle` (255, 260 awaited stores; 249-250 `playingSinceMs`, `firstFramePushing` stay), `loop` (437-441 latch free), `loopPlaying` (495-556), `loopView` (589), `readGesture` (673-731), `renderCanvas` (754-808), `canvasUnderView` (820-823), `pauseInGap` (908).
- `src/games/GameTouch.h`, `GameTouchLog.h` -- header-only pure siblings; the new headers follow their style and are tested in `GameScriptTest` (`test/game_script/CMakeLists.txt`, include path `src/games` already set).
- `src/games/GameVM.h` -- `UNTAGGED` (line 67) stays; the decision's own constant is pinned to it by a `static_assert` in the activity `.cpp`. `roundsStarted()`, `seatShownRequest()`, `frameGen()` are single acquire loads.
- `test/game_script/harness/GameMatchTest.cpp`, `ResumeMatchTest.cpp` -- must pass unedited; their comments name `frameDisplayed` / `roundsDisplayed`, which keep those member names inside `CanvasGate`.
- `docs/crosshatch/game-canvas.md` lines 88, 252, 311, 327, 338, 344, 362 -- name moved members; reword to the new owners.

## Tasks & Acceptance

**Execution:**
- [ ] `src/games/GameTouchRoute.h` -- NEW, header-only, pure. `namespace GameTouchRoute { constexpr uint32_t UNTAGGED; enum class Action { Post, DropBeforeHandOff, DropBeforeDisplay }; struct Aimed; struct Decision { Action action; uint32_t shownFrame; int beforeHandOffMs; }; template <class Source> Decision decide(const Aimed&, Source&); }`. Body is `loopPlaying` lines 514-554 moved verbatim, the three branches as one `if` ladder returning a `Decision`.
- [ ] `src/games/TouchLatch.h` -- NEW, header-only. Owns `touchDownFrame`, `touchDownMs`, `touchDownLatched`, first and last samples, `contactHeld`; `latch`, `sample`, `setHeld`, `endIfNotHeld`, `free`, accessors. Loop task only.
- [ ] `src/games/CanvasGate.h` -- NEW, header-only. Owns `roundsStartedAwaited`, `roundsDisplayed`, `seatAwaited`, `seatDisplayed`, `frameDisplayed`, `pushMutex`, `lastPush`; `awaitRounds`, `awaitSeat`, `roundsAwaited`, `seatAwaited`, `template <class VmLike> wait(const VmLike&)`, `open(started, served)`, `frameOnPanel`, `frameAt`, `recordPush(clock)`, `markDisplayed`.
- [ ] `src/activities/games/GameMatchActivity.{h,cpp}` -- members `CanvasGate gate`, `TouchLatch contact` replace 15 data members and `frameAt`; call sites forward; `loopPlaying` builds `Aimed`, calls `decide` with a `Source` adapter over `gpio`, `gate`, `vm`, performs the `Decision`; comments move with the code; `static_assert(GameTouchRoute::UNTAGGED == GameVM::UNTAGGED)`.
- [ ] `test/game_script/GameTouchRouteTest.cpp`, `CanvasGateTest.cpp`, `TouchLatchTest.cpp` + `CMakeLists.txt` -- NEW host tests over fake sources (Design Notes); each decision branch has a red mutant, run by hand and recorded in the Verification results.
- [ ] `docs/crosshatch/game-canvas.md` -- the seven references above name `CanvasGate` / `TouchLatch` / `GameTouchRoute` owners.

**Acceptance Criteria:**
- Given the unchanged `GameMatchTest`, `ResumeMatchTest`, `GameVmTest`, and frame-race test, when the host suite runs (match suites 20 times), then all pass.
- Given the new decision tests, when any single branch of `decide` is mutated, then at least one test fails.
- Given the diff, when `check_layers.py` and `check_upstream_touches.py` run, then both pass with no new edge or ledger row.

## Implementation Notes

- Built per the plan. `CanvasGate`'s seat count member is `seatAwaitedRequest` (the method `seatAwaited()` owns the name); the others keep their names. `TouchSource` (anonymous namespace in `GameMatchActivity.cpp`) is the `Source` adapter.
- Verification results (host): suite 1896/1896 (baseline 1861 + 35 new: 17 `GameTouchRouteTest`, 11 `CanvasGateTest`, 7 `TouchLatchTest`); match suites 283/283 x20; `check_layers.py`, `check_upstream_touches.py`, every `scripts/*_test.py`, `clang-format-fix` twice: clean.
- Mutants run by hand, each restored, each failed a named test: decide (awaitingRound ladder, firstFramePushing term, began from latch, back-date unconditional, swipe back-dated on the first-frame path, hand-off boundary `>=`, first-frame UNTAGGED, beforeHandOffMs sign, drop-before-display, latched frame ignored, swipe asks frameAt, frameAt unconditional, tagged UNTAGGED); gate (`wait` seat compare, read order, round compare, display ignores round, display seat, display rounds, `open` each count, `frameAt` `doneMs != 0` and signed compare, `recordPush` before); latch (`endIfNotHeld` ignored and inverted, first sample, last sample, frame). Equivalent mutant (cannot fail): dropping the `int32_t` cast in `beforeHandOffMs` (uint32 to int is the same modular conversion on GCC).

## Plan Change Log

- Checkpoint 1 (orchestrator, 2026-10-10): (1) `firstFramePushing` is a lazy `Source` read, not a field of `Aimed`, so a pass with `awaitingRound` never reads it. (2) `DropBeforeHandOff` carries today's exact expression, not `playingSinceMs - began`. (3) `CanvasGate::wait` is a template over a VM-like type; `CanvasGate.h` stays std-only; load order kept. (4) The mutants list covers only what a test can fail; release and lock order are pinned by review and comment. KEEP: three header-only units, the member table, the guard list, the `UNTAGGED` static_assert, the doc rewording, the verification.

## Review Triage Log

All four lenses ran as context-free subagents, in the foreground, and all returned (blind-hunter, edge-case-hunter, verification-gap, intent-alignment). Pass 1 verdicts: 0 high, 0 medium, 7 low patched, rest false / low rejected / deferred. The patches were applied by the builder after the implementer had finished.

| Finding | Verdict / route | Evidence |
|---|---|---|
| Class comment says the store order is "one function, not an order callers must keep", but `recordPush` must run before `markDisplayed` (blind) | low / patch | True: `recordPush` records the frame on the panel before this push. Comment now names the remaining caller order. |
| `Aimed` built by seven positional initialisers; adjacent bools / uint32 can be transposed unnoticed (blind; intent-alignment sees the same wiring risk) | low / patch | Real: compiles and no unit test sees it. Designated initialisers (C++20) in `loopPlaying` and the test helper. |
| `TouchSource` constructor parameters shadow its members (blind, edge) | low / patch | `-Wshadow` / `pio check` risk; parameters renamed. |
| `TouchLatch::held()` has no production caller (blind, edge) | low / patch | `contactHeld` was never read outside the latch; accessor and its one assertion removed. |
| Two test names overclaim (`...ReadsTheClockOnce`, `...OnlyAfterTheRoundIsPublished`) (blind, gap) | low / patch | Names asserted more than the bodies check; renamed to what they check (the "only after" half is `AwaitingARoundNeverReadsFirstFramePushing`). |
| `game-canvas.md` / `CMakeLists.txt` lines past the hard-wrap (blind) | low / patch | Rewrapped the four changed paragraphs and the CMake comment; table rows are untouched. |
| Plan says 17 members replaced; 15 data members + `frameAt` + `LastPush` (edge) | low / patch | Plan text corrected; `TouchSource` described as a class using the global `gpio`. |
| `doneMs != 0` as the "no push yet" sentinel collides with `millis() == 0` (blind) | low / defer | Pre-existing (17d02483) and the Decision says keep every guard; one push in a 49.7-day wrap treated as none. Deferred. |
| `beforeHandOffMs` negation of `INT32_MIN` is UB (edge) | false for this change | Today's exact expression, kept byte-identical on the orchestrator's instruction; reachable only with a latch about 24 days old. |
| Inconsistent `Aimed` (round but not display), null `Clock` (edge) | low / reject | Only `CanvasGate::wait` builds the first, the only `recordPush` caller passes `millisNow`; the fix adds guards for states no caller reaches. |
| Concepts for `VmLike` / `Source`; `UNTAGGED` rule written twice in `decide`; extra wrap / read-count tests (blind, gap) | low / reject | Moved verbatim from `loopPlaying`, which wrote the rule twice; fixes add structure or tests for no named harm. |
| Test comments in `GameMatchTest.cpp` name `roundsDisplayed` / `frameDisplayed`; `deferred-work.md` `## e6pre-1` entry (blind, edge, intent) | low / reject | Existing tests stay unedited by the Decision, and `deferred-work.md` is the orchestrator's. |
| I/O matrix wording "when the first compare passes" is ambiguous (edge) | false | Frozen block; the Design Notes and `CanvasGateTest.WaitShortCircuitsTheSeatReadWhenTheRoundIsAlreadyAwaited` state the order. |
| No size measurement recorded (blind) | low / reject | Measured in Verification below (the firmware checks run after the review). |
| intent-alignment: the new tests exercise units, the wiring is covered only by the existing harness suites; ordering and mutants are not visible in the diff | no defect | Descriptive. The wiring runs under `GameMatchTest` / `ResumeMatchTest`; release order and lock order are pinned by review and comment, as the plan says; mutants are in Implementation Notes. |
| verification-gap | none | "No verification gaps found." |

## Design Notes

**Units.** Three header-only files in `src/games`, beside `GameTouch.h`. Header-only keeps the harness source globs and `match.cmake` untouched and lets `GameScriptTest` (pure) test them without a VM or renderer.

| Member today | Moves to | Written / read by |
|---|---|---|
| `touchDownFrame`, `touchDownMs`, `touchDownLatched`, `touchFirstX/Y`, `touchLastX/Y`, `contactHeld` | `TouchLatch contact` | loop task only, as now |
| `roundsStartedAwaited`, `seatAwaited` | `CanvasGate gate` | `handle` stores; loop, render, `loopView`, `canvasUnderView`, `pauseInGap` load |
| `roundsDisplayed`, `seatDisplayed`, `frameDisplayed`, `pushMutex`, `lastPush`, `LastPush` | `CanvasGate gate` | render stores (`markDisplayed`, `recordPush`); loop loads (`wait`, `frameOnPanel`, `frameAt`) |
| `firstFramePushing`, `playingSinceMs`, `announcementAwaited`, `panel`, `renderedFrame`, `shownFrame` | stay | unchanged |
| `frameAt()`, `readGesture()` | `CanvasGate::frameAt`; `readGesture` stays (hardware) and drives `contact` | |

One gate class holds the counters and the frame record on purpose: the `frameDisplayed` -> `roundsDisplayed` -> `seatDisplayed` store order is then one function, `markDisplayed`, with its comment, not an order two classes' callers must keep.

**Atomics and locks, before and after (all unchanged in kind and order).** `awaitRounds` / `awaitSeat`: seq_cst stores at the same two points in `handle`, before `shown.store` (bd176acd: a queued render must not see Playing with the old count). `roundsAwaited()` / `seatAwaited()`: seq_cst loads. `markDisplayed(taken, started, served)`: release stores in that order, then the activity stores `firstFramePushing` (release), as today. `wait` is a template over a VM-like type (`roundsStarted()`, `seatShownRequest()`), so `CanvasGate.h` stays std-only and includes no `GameVM.h`; GameVM satisfies it, tests pass a counting stub. It loads `roundsStartedAwaited`, `seatAwaited`, then `vm.roundsStarted()`, `vm.seatShownRequest()` only if the first compare fails (short-circuit), then `roundsDisplayed`, `seatDisplayed` with acquire, only when needed, as `loopPlaying` does. `recordPush(clock)`: takes `pushMutex`, then `frameDisplayed.load(relaxed)`, then `millis()` through `clock`, all inside the lock (today's brace-init order); the render task calls it under RenderLock; the lock is released before the stores. `frameAt`: `pushMutex` alone, then `frameDisplayed.load(acquire)` inside it. RenderLock then `pushMutex` holds; the loop never takes the RenderLock with `pushMutex`.

**Decision.** `Aimed{tap, awaitingRound, awaitingDisplay, latched, latchedFrame, latchedMs, playingSinceMs}` are values the loop already has. `Source` supplies what is read lazily: `firstFramePushing()` (the acquire load, read only when `!awaitingRound`, as today's `aimed && !awaitingRound && firstFramePushing.load(acquire)`), `nowMs()`, `heldMs()`, `frameOnPanel()`, `frameAt(atMs)`, `currentFrame()`. `DropBeforeHandOff` carries `beforeHandOffMs`, computed by today's exact expression `-static_cast<int>(static_cast<int32_t>(began - playingSinceMs))`, so the log text is byte-identical, wrap included. The decision is pure given a pure `Source`; tests pass a counting fake and so pin the read points too (a swipe never reaches `frameAt`; a dropped gesture reads nothing; a pass with `awaitingRound` never reads `firstFramePushing`). `decide` is called only for a gesture `classify` sent (`aimed`).

**Guards found with `git log -L` (each stays, with its comment).**
- `readGesture` (13bcd449, 17d02483, 8a0b72bf, 1acce4bf): the latch runs before any early return, so a swipe or tap lift still sees its latch; `touchFirst` is set only at the latch, `touchLast` on every pass with a finger; `contactHeld = snap.touchHeld` is set before the returns; a long press has no `heldMs` (stale `lastTouchHeldMs`); an `Ended` contact reports `-1` for a contact it never saw down.
- `loop` (c2954f7f): the latch is freed on the first non-Playing pass that sees no finger; a finger held across the transition keeps it (a stale latch dated a fresh contact and dropped it).
- `frameAt` (17d02483): `doneMs != 0` (no push yet), wrap-safe signed compare, answers the frame before the last push for a touch down before it completed.
- `renderCanvas` (b964078a, bd176acd, 17d02483): `started` and `served` read once before drawing; the gate skip sets `viewOnScreen` and returns before any store; the push record only when `drawFront` drew; the three stores happen on every path past the gate, a skipped identical frame included (a return above would drop gestures for good); `frame` read before `clearScreen` (frame-race seam); `firstFramePushing` cleared last.
- `loopPlaying` (d4f889f0, bd176acd, 17d02483, c2954f7f, 8a0b72bf): gestures are read (consuming the contact) and dropped while the gate holds; the first-frame path requires `!awaitingRound`, takes `began` from the latch or now, back-dates a tap, drops a contact that began before `playingSinceMs` (signed compare), posts with the VM's current frame; `UNTAGGED` is never posted (one less, which can only drop); the touch log line runs before the decision; `!contactHeld` frees the latch after the decision; `awaitingRound` still gates the render request.
- `handle` (bd176acd): awaited counts stored before `shown`. Not moved, only forwarded.

**API sketch (names fixed; bodies are today's code moved verbatim, comments with it).**
```cpp
// GameTouchRoute.h
namespace GameTouchRoute {
constexpr uint32_t UNTAGGED = UINT32_MAX;
enum class Action : uint8_t { Post, DropBeforeHandOff, DropBeforeDisplay };
struct Aimed { bool tap; bool awaitingRound; bool awaitingDisplay; bool latched; uint32_t latchedFrame; uint32_t latchedMs; uint32_t playingSinceMs; };
struct Decision { Action action = Action::Post; uint32_t shownFrame = 0; int beforeHandOffMs = 0; };
// Source: bool firstFramePushing(); uint32_t nowMs(); uint32_t heldMs(); uint32_t frameOnPanel(); uint32_t frameAt(uint32_t atMs); uint32_t currentFrame();
template <class Source> Decision decide(const Aimed& in, Source& source);
}
// TouchLatch.h: class TouchLatch { void latch(uint32_t frame, uint32_t ms, int x, int y); void sample(int x, int y); void setHeld(bool); void endIfNotHeld(); void free();
//   bool latched() const; uint32_t frame() const; uint32_t ms() const; int firstX/firstY/lastX/lastY() const; bool held() const; }
// CanvasGate.h: class CanvasGate { using Clock = uint32_t (*)();
//   void awaitRounds(uint32_t); void awaitSeat(uint32_t); uint32_t roundsAwaited() const; uint32_t seatAwaited() const;
//   struct Wait { bool round; bool display; }; template <class VmLike> Wait wait(const VmLike&) const;
//   bool open(uint32_t started, uint32_t served) const;   // the render gate: !(started < roundsAwaited || served < seatAwaited), loads in that order
//   uint32_t frameOnPanel() const; uint32_t frameAt(uint32_t atMs) const; void recordPush(Clock); void markDisplayed(uint32_t taken, uint32_t started, uint32_t served); }
```
The activity's `Source` adapter (`TouchSource`) is a class in `GameMatchActivity.cpp`'s anonymous namespace holding references to `gate`, `vm`, `firstFramePushing`; it reads the global `gpio` in `heldMs()`. `awaitRounds(n)` stores `n` as given (the activity passes `vm->roundsStarted() + 1`).

**Why not eager inputs.** `millis()` and the frame record are read lazily today; reading them every aimed pass would change timing (and take `pushMutex` on every tap), so `Source` stays lazy.

## Verification

**Commands:**
- `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test'` then `ctest --test-dir build/test --output-on-failure -j` and `ctest --test-dir build/test -R 'Match|Resume' --repeat until-fail:20` -- expected: all pass, count = baseline + new tests.
- `python3 scripts/check_layers.py`, `python3 scripts/check_upstream_touches.py`, every `scripts/*_test.py`, `./bin/clang-format-fix` twice -- expected: clean.
- Mutants, built and run by hand and each restored, only for what a host test can fail: each `decide` branch, the gate compares (`wait`, `open`), `frameAt`'s `doneMs != 0` and its signed compare, the latch's first-sample and long-press-no-hold guards (the latter is in `readGesture`, which stays in the activity, so it is covered by the existing `GameMatchTest` and named there) -- expected: a named test fails. `markDisplayed`'s release order and `recordPush`'s lock order are not observable in a single-threaded host test on x86: pinned by review and the comment, not by a test.
- After the review, once, under the build lock with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`: `pio run -e x4pro`, `pio run -e default`, `check_flash_budget.py build on` then `objects`, `pio check` (default and `-e x4pro`), `sim.sh build x4pro` and a short run: Ultimate tic-tac-toe in pass, one move tapped, screenshot -- expected: success, no new defects, the move shows.

**Results (worktree, incremental, after the review patches).** Host: `ctest` 1896/1896 (baseline 1861 plus 35 new: 17 `GameTouchRouteTest`, 11 `CanvasGateTest`, 7 `TouchLatchTest`); `ctest -R 'Match|Resume' --repeat until-fail:20` 283/283; `GameMatchTest`, `ResumeMatchTest`, `GameVmTest`, the frame-race test unedited. `check_layers.py` (594 edges, no new one), `check_upstream_touches.py` (no ledger row), every `scripts/*_test.py`, `./bin/clang-format-fix` twice: clean. Firmware, under the build lock: `pio run -e x4pro` and `-e default` succeed; `check_flash_budget.py build on` then `objects` pass (49 game objects, largest mutable static 4 B, no static initializer); `pio check` for `default` and `-e x4pro` (fail on low, medium, high): no defects; `sim.sh build x4pro` succeeds. Size, one method at both commits (baseline `db457b36`'s `GameMatchActivity.{h,cpp}` swapped in over the new tree, then restored; `firmware.bin` bytes, and `size -A` over `.dram0*`, `.iram0*`, `.noinit` of the ELF): x4pro flash 5,936,976 -> 5,936,832 (-144 B), static internal RAM 257,480 -> 257,480 (0); default flash 5,644,176 -> 5,644,176 (0), RAM 193,096 -> 193,096 (0; games are off). A second build of the new tree gave the same numbers.

Simulator (x4pro, `sim.sh start`, `fs_/games` holding the packed game): Games -> Ultimate Tic-Tac-Toe -> New game (Pass and play) -> one tap on the centre cell; the log shows `touch tap screen (240,354) canvas (233,345)`, the VM posts the move, `frame 2 pushed`. Screenshots (committed): `_bmad-output/initiative-crosshatch-player-v1/epic-play-nearby/story-touch-split-screenshots/pass-start.png` (the empty board, "Player 1 (X) to move") and `after-one-move.png` (the X placed, "Player 2 (O) to move", the next board highlighted). The simulator driver needed `xdotool`, installed with `apt-get install -y xdotool`.

## Auto Run Result

**Summary.** `GameMatchActivity`'s touch latch, round and seat gate with the frame-on-panel record, and the aimed-gesture decision moved into three header-only units in `src/games` (`TouchLatch`, `CanvasGate`, `GameTouchRoute`). The activity forwards to them and keeps the hardware reads, `postInput`, and the log lines. Behaviour, log text, read points, and memory and lock order are as before; the checkpoint's four changes are in the Plan Change Log.

**Files.** `src/games/{GameTouchRoute,TouchLatch,CanvasGate}.h` (new), `src/activities/games/GameMatchActivity.{h,cpp}`, `docs/crosshatch/game-canvas.md`, `test/game_script/{GameTouchRouteTest,CanvasGateTest,TouchLatchTest}.cpp` (new), `test/game_script/CMakeLists.txt`, the plan, two screenshots.

**Review.** Four lenses, context-free, foreground: 7 low patched, 0 high or medium, 1 low deferred, the rest false or rejected (Review Triage Log).

**Formatting.** `./bin/clang-format-fix` changed nothing outside my files. No upstream file touched.

**Residual risks.** `markDisplayed`'s release order and `recordPush`'s lock order are pinned by review and comment, not by a test (single-threaded x86). `TouchSource`'s wiring to `gpio` and the atomic is covered only by the existing `GameMatchTest` / `ResumeMatchTest` suites. Device behaviour is unmeasured: no device run.
