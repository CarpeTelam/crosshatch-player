---
title: 'Refactor sweep'
type: 'refactor'
ticket: '13'
created: '2026-10-13'
status: done
baseline_revision: '0027d23e0229be221f829f3cd8d08fd4db13b3f0'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
followup_review_recommended: false
context:
  - '{project-root}/AGENTS.md'
  - '{project-root}/.agents/skills/refactor-for-review/SKILL.md'
  - '{project-root}/.agents/skills/heap-discipline/SKILL.md'
warnings: ['multiple-goals', 'oversized']
deferred:
  - summary: >-
      When the USB drive takes the card, the device's volume is ended but the SD device stays live, so a diagnostics-log writer batch whose `HalFile` was opened before the drive took the card would write to a card the host now owns.
    evidence: |-
      Found by entry 13 reading SdFat 2.3.1 for `## 6.19`'s sixth item: `FsVolume::end()` only nulls pointers, and after `SDCardManager::detachFilesystemForRawAccess()` the block device stays live, unlike the sleep path, where `SdmmcBlockDevice::end()` makes sector I/O fail. Unverified: no run has logged through a USB drive attach with the define on. Trigger: a define-on device run that opens the USB drive while lines are logged, or the next change to the writer's open and write.
    location: >-
      src/diagnostics/DiagLogStorage.cpp (StorageFileStore::append, writeAndClose)
    severity: medium (unverified)
  - summary: >-
      `docs/crosshatch/game-canvas.md` (the user exit's paragraph) still says Back from the player-left view sends three `ABORT(left)`; since entry 13 `NearbySession::leave` sends none to a seat whose `ABORT` was heard.
    evidence: |-
      F8 changed the behaviour (`## 6.11`'s second item); the doc is outside entry 13's touches. Trigger: the next entry that touches game-canvas.md, or entry 14's docs pass.
    location: >-
      docs/crosshatch/game-canvas.md:170-172
    severity: low
  - summary: >-
      `GameMatchActivity::renderView`, drawing the canvas under a view (the Result banner, a menu), still stores the `frameGen()` it read before `drawFront` into `renderedFrame`, so a frame the VM publishes between the two is asked for once more (a redundant full redraw), the defect entry 13 fixed in `renderCanvas`.
    evidence: |-
      Review pass 1 (verification-gap): `renderView` stores `vm->frameGen()` and then calls `drawFront` without `&taken`; `loopHandOff`'s Result branch reads `renderedFrame` as `loopPlaying` does. Pre-existing (the `## e5-xr` item named `renderCanvas`). The fix is two lines (pass `&taken`, store it after `drawFront`), but no harness seam reaches the window (`renderView` clears before it reads `frameGen()`), so a test needs a new hook; the member's comment now says `renderView` still stores the read.
    location: >-
      src/activities/games/GameMatchActivity.cpp (renderView)
    severity: low
  - summary: >-
      In `device_log.py merge`, the boots a floating root's `walk` places by their STATEs are not checked against their own devices' other boots, so one can still overlap a boot of its device with no note, and the clamp then holds its lines.
    evidence: |-
      Review pass 1 (blind hunter, edge-case hunter): entry 13's F10 checks and pulls back only the root; in its own two-device test B's boot 2, placed by its STATEs with the pulled-back A boot 2, lands inside B's 40 s boot 1 with no note. Pre-existing for walked boots (before F10 the root had the same gap); reachable with three devices or a device whose boots align with different groups. A fix checks every boot the walk places against its device's placed boots, or notes the overlap.
    location: >-
      scripts/device_log.py (align_clocks, walk)
    severity: low
---

<intent-contract>

## Intent

**Problem:** The epic's build records (`deferred-work.md` `## 6.1` to `## 6.23`, and the pre-epic items the entry names: `## e6pre-15`, `## e6pre-17`, `## e5-xr`'s `renderedFrame`, `## e5-close`'s D3) hold findings that were deferred to this sweep or have no outcome yet. Entry 14 (the owner's sign-off) needs each one fixed, documented, or deferred with a reason it can confirm.

**Approach:** Fix the items with a concrete remedy inside `touches` (Design Notes, F1 to F17), each behaviour-preserving except where the item names the behaviour to change (F2, F3, F8, F9, F10). Record every item's outcome in `deferred-work.md` in place, in the file's own "Resolved by ... It read: ..." form. Stay within the 1,696 B of flash and 80 B of static RAM left in the epic's share.

## Boundaries & Constraints

**Always:**
- Entry 13's `touches` only: `lib/Game*/**`, `src/games/**`, `src/activities/games/**`, `test/**`, `scripts/pack_game{,_test}.py`, `scripts/device_log{,_test}.py`, `lib/DiagLog/**`, `src/diagnostics/**`, `docs/crosshatch/fork-scripts.md`'s device-log sections, and `deferred-work.md`'s entries the epic met (the orchestrator widened the brief's rule for this entry). A needed file outside them is a stop, never a guess.
- A refactor keeps behaviour, log text, and memory orders. Before moving a function, read its history with `git log -L` and keep every guard, as Design Notes says for each.
- Measure flash and static RAM like for like (`scripts/check_flash_budget.py` `build on`, `build off`, `compare`, `objects` under the build lock). The epic total over the base `c28120a7` (+258,048 B / +784 B) must stay at most +42,000 B / +160 B: games-on minus off at most 300,048 B and 944 B. A total over it stops the run.
- A test double named here says what device behaviour it stands in for; a new test is shown red against a mutant of the code it pins.

**Never:**
- Touch `games/**`, `docs/crosshatch/api-level-1.txt`, `.github/workflows/**`, any upstream file (ledger rows 13 and 14 included), `lib/Memory`, `lib/hal`, or `freeink-sdk`.
- Change the protocol, `ReliableLink`'s wire behaviour, the lobby rules, or any owner Decision in the epic Notes (no restart after nearby is settled).
- Take on an item's open design question (a wire epoch, n >= 3 rules, a "Not your turn" message, peer-table eviction, a refused-send counter): those stay with the owner.
- Run `tickets.py mark` (other than a HALT), `pull`, push, or open a PR.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Leave after the peer's ABORT | the peer sent ABORT(left); this device leaves from "player left" (Back or a forced exit) | no ABORT is sent to that peer; the `leave` line says `copies=0` | none |
| Leave after silence | the end is Timeout (no ABORT heard) | three ABORT(left), as today | none |
| Leave with no peer end | an ordinary Leave or a forced exit | unchanged: flush (Leave), then three copies | none |
| Push at millis() 0 | `recordPush` with the clock at 0; a touch down 16 ms before | `frameAt` answers the frame before the push | none |
| Frame published mid-render | the VM publishes between render's `frameGen()` read and `drawFront` | the next loop pass asks for no second render; a later repaint (an overlay closed) is drawn in full | none |
| Floating aligned root (merge) | a device's first aligned boot reached by no STATE path from the reference | its note names the boots it shares STATEs with and keeps "placed after boot N of the same device" | none |
| Floating root would overlap | the root, placed after its device's earlier boots, runs past a later boot of its device already placed | it is pulled back to end when that boot starts, and the note says so (and any overlap with the earlier boot, as pass 2 does) | none |

</intent-contract>

## Code Map

- `scripts/pack_game.py:82` `MODES`; `scripts/pack_game_test.py` (`ReadManifestTest` ~:680, `REPO` from `__file__`; precedent :968 reads `StreamingJsonParser.h` as text) -- F1, F14.
- `lib/GameCore/ModeTable.h:23-27` `MODE_TABLE` rows `{Mode::X, Manifest::MODE_X, "name"},` -- read only.
- `src/games/CanvasGate.h` `LastPush`, `frameAt`, `recordPush`; `test/game_script/CanvasGateTest.cpp` (`FrameAtComparesAcrossTheClockWrap` :137) -- F2.
- `src/activities/games/GameMatchActivity.cpp` `renderCanvas` (~:855-875: `renderedFrame` stored from `frameGen()` before `drawFront`), `renderView`, loop checks at ~:643 and ~:740; `GameMatchTest.cpp:1679` `AFramePublishedWhileRenderDrawsIsTheOneTheNextTapIsMadeUnder` and `GfxRenderer` stub's `onClear` seam -- F3.
- `src/activities/games/GameMatchView.cpp:168-180, :200-210`, `GameConfirmDialog.h:77-86`, `GameModeActivity.cpp:639-648` (framed panel); `GameModeActivity.cpp:528-584` and `GamesLauncherActivity.cpp:233-279` (confirm glue: open, close, handle input, answer switch, tap callback); `GameMatchActivity.cpp` `loopPlaying` ~:635, `loopView` ~:657, `loopHandOff` ~:705 (flush tail) -- F4. All fork-only. Upstream `OptionPopup.h`, `UiSliderDialog.h`, `WifiSelectionActivity.cpp` hold the same style block and stay untouched.
- `lib/GameCore/Protocol.h:66` `PROTO` (its comment names entry 13), `lib/GameScript/Codec.h:15` `VERSION`; `test/game_script/CodecTest.cpp` (GameScriptTest sees both) -- F5.
- `test/game_core/LinkContract.h`, `test/game_script/harness/ThreeLinkContract.h` (deps: gtest, LinkContract.h, ILink.h, Protocol.h), users `UdpLinkTest`, `FakeLinkTest`, `EspNowLinkTest.cpp` (`FakeAir`, `FakeRadio`, `TwoLinksOnOneAirPassTheLinkContract` ~:779) -- F6.
- `src/games/EspNowLink.h:394-395` stale comment -- F7. `EspNowLink.cpp:54-69` (`activeRing`, `inCallback`, `onReceive`), `:115-123` `registerReceive`, `:175-200` `shutdownEspNow`'s quiesce loop; `esp_now_link.cmake` (EspNowLinkHarnessTest; EspNowLink.cpp itself is not host-built); `FrameRingTest.TwoThreadStressNeverTearsOrReordersAFrame` the pattern -- F15.
- `src/games/NearbySession.cpp:369-405` `leave`; `src/games/GameLinkSinkGate.h` `onFrame`'s Abort branch (seat known); `GameMatchActivity.cpp:336-341` (`handle` Leaving comment); `test/game_script/harness/NearbyLeaveTest.cpp`, `NearbySessionSupport.h` (`Duo`, `LinkProbe::abortFrames`); `NearbySession.h:51` lifetime comment -- F8, F16.
- `scripts/device_log.py:963-983` `align_clocks` pass 1 (notes at :979, :982), pass 2 :987-1000 (overlap note wording to mirror); `device_log_test.py:1103-1127`, `:1273`; `fork-scripts.md:161-174` -- F9, F10.
- `lib/GameScript/MatchRounds.cpp:111-121`; `test/game_script/MatchRoundsTest.cpp`; non-Lua stub pattern `test/game_core/SessionTest.cpp:17` `FakeRules` -- F11.
- `test/game_script/harness/screen_stubs/freertos/task.h:17`, `FakeRtos.h:50-59` `fakertos::Task`; `GameLinkTest.cpp:77`; `src/games/GameLink.cpp:22-23` -- F12.
- `GameMatchActivity.cpp` `~GameMatchActivity` (`(void)nearby.release()`), `leave()` (`if (!slotLeaked) nearby.reset()`), `abandonVm` log "The ch.store slot stays with the leaked VM"; stuck rig `GameMatchTest.cpp:727`; nearby rig `NearbyMatchTest.cpp:113-140`; `LinkProbe::endCalls` -- F13.
- `lib/DiagLog/DiagLogWriter.h` (header :264-275, `MAGIC`), `test/diag_log/DiagLogWriterTest.cpp` (`REPO_ROOT_DIR`), `scripts/device_log_test.py` `card_text` ~:944 -- F17.
- `src/diagnostics/DiagLogStorage.cpp:47-52` `writeAndClose` / `StorageFileStore::append` -- D-6.19-6 comment.
- `_bmad-output/implementation-artifacts/deferred-work.md` -- every item in Design Notes' ledger gets its outcome.

## Tasks & Acceptance

**Execution:**
- [x] `scripts/pack_game_test.py` -- F1 a test reading `ModeTable.h`'s names in order equals `pack_game.MODES`; F14 a test that both first-party manifests list `nearby` in `modes` -- `## e6pre-15`, `## 6.12` item 3.
- [x] `src/games/CanvasGate.h`, `test/game_script/CanvasGateTest.cpp` -- F2 `LastPush.recorded` set by `recordPush`, read by `frameAt` instead of `doneMs != 0`; a test pushes at clock 0 -- `## e6pre-17`.
- [x] `src/activities/games/GameMatchActivity.cpp`, `test/game_script/harness/GameMatchTest.cpp` -- F3 store drawFront's `taken` into `renderedFrame` right after `drawFront`, before `displayBuffer`; keep the `frameGen()` read before the view clear and the repaint check before `drawFront`; a test (the frame-race seam) asserts no second render request after a mid-render publish -- `## e5-xr`.
- [x] `src/activities/games/GamePanelStyle.h` (new), `GameMatchView.cpp`, `GameConfirmDialog.h`, `GameModeActivity.{h,cpp}`, `GamesLauncherActivity.{h,cpp}`, `GameMatchActivity.{h,cpp}` -- F4 one framed-panel style function for the four sites; the confirm glue's shared parts (handle input, answer dispatch) in `GameConfirmDialog`, each activity keeping its open guards, close and accept bodies; a private `flushDue()` for the three loops' `flushIfDue(millis()); flushResume(); retryResumeDelete(false);` tail -- `## e5-close` D3.
- [x] `test/game_script/CodecTest.cpp` -- F5 the pinned (`Codec::VERSION`, `PROTO`) pair, with the rule in its message; `lib/GameCore/Protocol.h`'s PROTO comment points at the test -- `## 6.2` item 1.
- [x] `test/game_core/LinkContract.h`, `test/game_script/harness/ThreeLinkContract.h` (removed), its users, `EspNowLinkTest.cpp` -- F6 the three-link contract in `LinkContract.h`; `EspNowLink` over three `FakeRadio`s runs it -- `## 6.2` item 2, `## 6.16` item 4.
- [x] `src/games/EspNowLink.h` -- F7 the `makeEspNowLink()` comment names its caller (`NearbyRadio::open()`) -- `## 6.11` item 1.
- [x] `src/games/GameLinkSinkGate.h`, `src/games/NearbySession.cpp`, `src/activities/games/GameMatchActivity.cpp` (comment), `test/game_script/harness/NearbyLeaveTest.cpp` -- F8 the gate records which seats sent an ABORT; `leave()` sends to the other peers only, `copies` counts only when at least one peer was sent to -- `## 6.11` item 2.
- [x] `scripts/device_log.py`, `scripts/device_log_test.py`, `docs/crosshatch/fork-scripts.md` -- F9 the floating-root notes; F10 the pull-back before a later placed boot of the root's device; tests for both, the two-device A1-B1, B1-A3, A2-B2 case among them -- `## 6.23`.
- [x] `test/game_script/MatchRoundsTest.cpp` -- F11 a non-Lua rules stub whose turn is 3 in a 2-seat roster: `step` and `drawShown` draw nothing, deliver nothing, `roundsStarted` stays 0 -- `## 6.1` item 1 (MatchRounds half).
- [x] `test/game_script/harness/screen_stubs/freertos/task.h`, `FakeRtos.h`, `GameLinkTest.cpp` -- F12 the double records name, priority, core; the GameLink test asserts them -- `## 6.8` item 4.
- [x] `test/game_script/harness/NearbyMatchTest.cpp` -- F13 a nearby Leave and a nearby forced exit with a stuck VM never end the leaked session's link (`endCalls == 0`); the test leaks the `Duo` it cannot free -- `## 6.9`, `## 6.15` item 1.
- [x] `src/games/RxGate.h` (new), `src/games/EspNowLink.cpp`, `test/game_script/harness/` (new `RxGateTest.cpp`, `esp_now_link.cmake`) -- F15 the receive-callback quiesce handshake as a header-only gate with an injected tick source; the device keeps one `constinit` instance and the same memory orders; a two-thread test pins that nothing is pushed after `quiesce` returns true and that a held callback makes it false at the limit -- `## 6.5` item 1.
- [x] `src/games/NearbySession.h` -- F16 the lifetime comment: the clock outlives the task (`NearbyRadio::clock()` is a static); the IRandom is read only in `HostLobby`'s constructor -- `## 6.8` item 2.
- [x] `test/diag_log/` (a committed sample card file), `DiagLogWriterTest.cpp`, `scripts/device_log_test.py` -- F17 the Writer's bytes for a fixed identity, clock, and lines equal the sample; `merge` reads the sample -- `## 6.19` item 4.
- [x] `src/diagnostics/DiagLogStorage.cpp` -- a comment at `writeAndClose` with SdFat 2.3.1's answer (D, `## 6.19` item 6).
- [x] `_bmad-output/implementation-artifacts/deferred-work.md` -- every ledger item's outcome, in place.
- [ ] Firmware checks once, after the review (Verification).

**Acceptance Criteria:**
- Given the host suites (the games check included) and the script tests, when they run, then all pass, and each fix's new test fails against the mutant the Implementation Notes name.
- Given `deferred-work.md`, when the ledger's items are read, then each carries "Resolved", "Resolved in part", "Documented", or "Kept open" with its reason.
- Given x4pro games on and off at the commit, when measured like for like, then the epic total is at most +42,000 B flash and +160 B static RAM over the base.
- Given `./bin/clang-format-fix` and `check_upstream_touches.py`, when run, then no upstream file changed and the tree is formatted.

## Implementation Notes

Build session 2026-10-13 (lane A worktree, uncommitted). Host suites 2597/2597 pass (2 skipped dump tests), every
`scripts/*_test.py` passes, `check_upstream_touches.py` PASS (no changed path exists in `upstream/develop`),
`check_layers.py` passes, `./bin/clang-format-fix` run twice with nothing new. Firmware builds, `pio check`, the flash
budget, and `sim.sh build` are not run: they follow the review.

Mutants (each new or changed test red against it, then the code restored):
- F1: `MODES` without `nearby`. F14: `nearby` removed from `games/battleship/manifest.json` (restored at once).
- F2: `frameAt` back to `doneMs != 0`.
- F3: `renderedFrame.store(frame, ...)` (the old value): no-second-render and full-repaint asserts both fail.
- F5: `Codec::VERSION = 2`.
- F8: the gate's `abortSeats |=` dropped; `leave()`'s skip dropped; `any &&` dropped (copies=3).
- F9: the old note text (three tests). F10: the pull-back disabled.
- F11: either `NO_SEAT` guard (`step`, `drawShown`) removed.
- F12: `TASK_PRIORITY + 1`; core 0.
- F13: `leave()`'s `if (!slotLeaked)` removed (Leave test); the destructor's `(void)nearby.release()` removed (both).
- F15: the count dropped (fetch_add alone, and both); the null store moved after the wait; the limit inverted
  (`<`); the limit returning true.
- F17: two header lines swapped in the Writer (gtest); `SD_MAGIC` changed in device_log.py (merge test errors).

Choices the plan left open, and deviations:
- F4: `setGamePanelStyle(StyleSet&)` writes in place (no StyleSet copy on the render stack); it sets every field the
  four sites set, and `corners` stays the default none of them set, so the styles are identical.
  `GamePanelStyle.h` takes freeink::ui through `components/UiAppHost.h`: `check_layers.py` refuses `<FreeInkUICore.h>`
  in `src/activities/games`. The confirm glue is `GameConfirmDialog::handleInput(host, input, respond)` (route first,
  Repaint after a routed touch that changed the app, early `true` after a routed touch, else the buttons) and the
  static `dispatch(answer, repaint, cancel, accept)`; each activity keeps its open guards, its tap callback's guard,
  close, and accept bodies.
- F6: the three-FakeLink run moved from `UdpLinkTest` to `FakeLinkTest` (the contract is in `test/game_core` now).
- F8: three `NearbyMatchTest` cases asserted the old behaviour (ABORTs after the peer's ABORT) and now assert none;
  the script-error case's host now latches the guest's silence (Timeout), not its ABORT(left).
  `docs/crosshatch/game-canvas.md:170-172` still says Back from the player-left view sends three: outside touches,
  recorded in this plan's `deferred` list.
- F13: FakeRtos's gate armed for every task held the GameLink tasks at their LINK lines under the session's lock, which
  deadlocked the goodbye and teardown (and would let the F13 mutants pass, as a held GameLink task never ends the
  link). `fakertos::arm` gained an optional task name (`GameVM`): the gate then holds only the stuck VM, as on the
  device. The test leaks the `Duo` and deletes the leaked session's GameLink task (FakeRtos parks it for good).
- F15: `RxGateT<Ring>` is templated on the ring so the test can hold a push (a `HeldRing` double); the device uses
  `RxGate = RxGateT<FrameRing>`. `quiesce(stopCallbacks, now, wait, limit)` owns the order (null store, then the
  caller's unregister/deinit/power-save step, then the wait), so the null-store-first order is host-tested.
  `EspNowLink.cpp` is not host-built: its edit (onReceive, registerReceive, shutdownEspNow) compiles first in the
  x4pro build after the review.
- D (6.19 item 6): the comment at `writeAndClose` records SdFat 2.3.1's answer; the USB drive's live device is a new
  item in this plan's `deferred` list (medium, unverified).
- Pre-existing, not touched: `GameLinkTest.StartRunsStepsOnATaskOfTheRightStackAndStopJoinsIt` asserts the FakeRtos
  task registry holds one task, so it fails under `--gtest_repeat` in one process (ctest runs each test alone).
- Run note (the plan's author, after the implementation subagent returned): the subagent had written its two new
  findings into `deferred-work.md` under `## 6.19` and `## 6.11`; they were moved to this plan's frontmatter `deferred`
  list (the orchestrator carries them under `## 6.13`), leaving `deferred-work.md` with outcomes only. The four planning
  investigation subagents were started without `run_in_background: false`, and the implementation subagent with it, yet
  all ran detached; the
  plan's author waited for each report before going on (the orchestrator noted the waits).
- Firmware checks (after the review and its patches; the plan's author): the first x4pro measurement read +1,376 B of
  flash for this entry. A symbol diff of the two games-on ELFs showed a second file-local `optionDialog<24>` `isra`
  clone (+1,088 B) and `GameConfirmDialog::build` inlined into `GameModeActivity::buildConfirmDialog` (+292 B): with
  its style block reduced to one call, `build` became small enough to inline, and the title screen's file got its own
  specialized `optionDialog`. `GameConfirmDialog::build` is now `[[gnu::noinline]]`, with a comment giving the figure;
  the entry then measures -304 B. The host suites and fast checks ran again after it, then every firmware check once
  more.

## Plan Change Log

## Review Triage Log

### 2026-10-13 — Review pass
Four lenses ran as context-free subagents, launched together over `git diff 0027d23e..` of the working tree less this plan (181,519 B; the plan was the edge-case lens's claims file only). Each was started with `run_in_background: false` and ran detached all the same; triage began only after all four had reported. The implementation subagent was not re-engaged (that would run detached too); the plan's author applied the patches.
- verdicts: 20 findings — high 0, medium 0, low 18, false 2, maybe-false 0 (plus 5 descriptive rows from intent alignment, not graded)
- findings:
  - `[low]` `[patch]` (blind) The USB-detach item is cited at `## 6.13`, which does not exist yet, and `DiagLogStorage.cpp`'s comment points at `## 6.19` — the item is in this plan's `deferred` list, which the orchestrator carries under `## 6.13`; the comment now says `## 6.13` too.
  - `[low]` `[patch]` (blind) `## 6.11`'s F8 outcome promises "a new item below" for `game-canvas.md`, which is not there — reworded to "a new item, deferred by entry 13 (`## 6.13`)"; the item is in this plan's `deferred` list.
  - `[low]` `[patch]` (blind) `## 6.2` item 2 is marked Resolved but "a handle is never reused after the link forgets a peer" is not checked — true: no adapter forgets a peer (`ILink` has no forget, no peer table evicts), so nothing can exercise it; the outcome is now "Resolved in part" with that reason.
  - `[low]` `[patch]` (blind) `## 6.15` item 1 claims the script-error and watchdog paths by argument — only Leave and the forced exit are tested; the outcome is now "Resolved in part", the error-view paths kept open for entry 14.
  - `[low]` `[patch]` (blind) The per-seat ABORT rule has no test with more than two seats — `NearbySessionGateTest.AnAbortMarksOnlyTheSeatThatSentIt` builds the gate with seats 2 and 3 and a stranger; red with the gate marking a fixed seat (every NearbyLeave test stayed green under that mutant).
  - `[low]` `[defer]` (blind) Boots a floating root's `walk` places can still overlap their own device's boots unnoted — real and pre-existing for walked boots (F10 fixed the root only); deferred (frontmatter), and `## 6.23` item 2 is now "Resolved in part".
  - `[low]` `[reject]` (blind) The pull-back's overlap note names the last placed earlier boot, not an unaligned boot between — the overlap it states is a true overlap with the named boot; pass 2 notes the unaligned boots' own overlap; a renamed note adds branches for a three-device shape no run has.
  - `[low]` `[patch]` (blind) `ProtoIsBumpedWithTheCodecVersion` pins two literals, so editing the codec literal alone passes — replaced by a table of shipped (codec, PROTO) pairs whose last row is the build's and whose rows need a rising PROTO, so a codec bump on the old PROTO fails.
  - `[low]` `[reject]` (blind) The sample card file's line endings are not pinned in `.gitattributes` — `.gitattributes` is outside entry 13's touches, and CI and every lane check out on Linux without `autocrlf`.
  - `[low]` `[patch]` (blind) `MatchRoundsNoSeatTest` covers a tap, not a timer — the test now arms, takes, and steps a due timer for turn 3 in the pass roster (every seat local, so not `noSeatTimer`'s drop): nothing delivered or drawn.
  - `[low]` `[patch]` (blind) `setGamePanelStyle` is not a full reset (`corners`) — values are identical today (no caller sets `corners`); its comment now says it sets only the listed fields and leaves the rest.
  - `[low]` `[patch]` (blind) `esp_now_link.sources.cmake`'s edited comment runs past the file's width — re-wrapped.
  - `[low]` `[defer]` (verification-gap) `renderView` still stores the `frameGen()` read before `drawFront`, the defect F3 fixed in `renderCanvas`, and the member comment now claims otherwise — pre-existing (the `## e5-xr` item named `renderCanvas`), and no seam reaches its window; deferred (frontmatter); the member comment is patched to say `renderView` still stores the read.
  - `[low]` `[patch]` (verification-gap) No test pulls back a first-boot root (no earlier boots) — `test_a_first_boot_root_that_would_run_past_its_devices_later_placed_boot_is_pulled_back_too` (A1-B2, B1-C1); red with the pull-back moved under `if earlier`.
  - `[low]` `[reject]` (edge-case) `leave(flushMs > 0)` after every peer's ABORT still waits for acks — pre-existing (it waited before F8 too), bounded by the 800 ms deadline, and reachable only by a Leave in the same pass the end latched; a guard adds a branch for that.
  - `[false]` `[reject]` (edge-case) The FakeRtos gate named `GameVM` also matches the guest's VM, so `waitParked` may hold the wrong one — the guest's VM logs nothing in that window, and the tests' pinning assertions are on the host's own link probe (`hostProbe.endCalls`, `abortFrames`): were the guest held instead, the host's Leave would stop its VM and end its link, failing them (the F13 mutants were red).
  - `[false]` `[reject]` (edge-case) `logHas("VM stuck ...")` reads both devices' log — same refutation: the host-specific probe assertions decide the test, not the shared log line.
  - `[low]` `[reject]` (edge-case) The overlap note can understate when the pulled-back root starts before the earlier boot starts — same root and reason as the blind hunter's naming row: a root longer than the gap and that boot together, three-device shapes only.
  - `[low]` `[defer]` (edge-case) The root's own `walk` can place a later boot of its device before the root ends — same root cause as the walked-boots row above; one deferral.
  - `[low]` `[reject]` (edge-case) CRLF checkout breaks the byte-exact sample test — same as the blind hunter's `.gitattributes` row.
  - descriptive (intent alignment) The diff implements the reading "the four named items plus every `## 6.x` item gets an outcome"; the wider reading (every section the epic met, `## e8-touch-log`, `## e6pre-5`, `## e5-run4`) is not touched — the orchestrator's scope note for this entry set `## 6.1` to `## 6.23` plus the named pre-epic ones; entry 14 owns the device-run items.
  - descriptive (intent alignment) The record pointers to `## 6.13` and a missing `game-canvas.md` item — same as the blind hunter's first two rows, patched.
  - descriptive (intent alignment) Kept-open outcomes name entry 14 in some rows and give only a reason in others — every kept-open outcome carries its reason; the ticket's own text says the owner confirms them at entry 14.
  - descriptive (intent alignment) The budget lives at the x4pro measurement, unmeasured in the diff — the firmware checks run next (Verification).
  - descriptive (intent alignment) `## e8-pins` (re-measure the host dialog and banner bounds when their style changes) and `## sync-e54c0087`'s `readButtons` item sit near F4 — F4 sets the same style fields to the same values, so the bounds do not move and `## e8-pins`' trigger is not met; `readButtons` is unchanged.

## Design Notes

Intent sources: the entry's description and `references`; the orchestrator's scope note (epic Notes, 2026-10-13, entry 13); entry 2's checkpoint C7 (PROTO tie test to entry 13); the owner's "for entry 13's sweep" marks in `## 6.11` and `## 6.23`.

**Fixes (F).** F1 `e6pre-15`; F2 `e6pre-17`; F3 `e5-xr` renderedFrame (entry 9 left it: `renderCanvas` still stores `frameGen()`); F4 `e5-close` D3; F5 `6.2`-1; F6 `6.2`-2 and `6.16`-4; F7 `6.11`-1; F8 `6.11`-2; F9 `6.23`-1; F10 `6.23`-2; F11 `6.1`-1 (MatchRounds half); F12 `6.8`-4; F13 `6.9`, `6.15`-1; F14 `6.12`-3; F15 `6.5`-1; F16 `6.8`-2; F17 `6.19`-4.

**Resolved earlier, documented now:** `6.2`-3 (`FakeLink::refuseSends`, entry 4); `6.4`-3, `6.4`-4 (entry 6, 8abb9271: `forget()` on every departure, `LobbyHandshakeTest.AGuestThatLeavesAndRejoinsReachesAStartedRoster`, `AGuestThatRestartsOnItsAddressRejoins`; the no-epoch remainder is `6.6`'s); `6.6`-2 in part (entry 10, af15f7b2: Start only while live; a ghost still holds the seat up to the silence limit); `6.8`-3 (entry 10's `teardown()` cancels first); `6.8`-5 (entry 9, 2b126b6c: `match.cmake` builds both sources); `6.11`-5 (owner Decision 2026-10-13, no restart); `6.19`-6 (SdFat 2.3.1: `FsVolume::end()` only nulls pointers and the volume lives in the static `SDCardManager`; `SdmmcBlockDevice` then refuses sector I/O, so a write or close fails and `writeAndClose` reports Failed; nothing freed is touched. New: after the USB drive's detach the device stays live, so a file opened before it would write to a card the host owns: a new deferral, medium, unverified).

**Kept open, with the reason the owner confirms at entry 14:** `6.1`-1 GameVM half (`GameVM` always runs Lua and `popSeat` refuses such a turn: a seam is new production surface); `6.1`-2 (`game-canvas.md` is outside `touches`); `6.3`-1, `6.3`-2, `6.11`-7 (need a run over USB); `6.12`-1, `6.12`-2 (`games/**` stays out; the owner's); `6.4`-1 remainder (`formats.md` outside `touches`); `6.4`-2, `6.6`-3 (no allocation seam in `test/game_core`; `lib/Memory` is outside `touches`); `6.6`-1, -4, -5, -6, -7, `6.8`-1, `6.10`-1, -2, `6.11`-3 (a wire change or n >= 3, unreachable in v1); `6.5`-2, `6.5`-3, `6.5`-4, `6.11`-4 (owner choices: eviction, a second-owner guard, a refused-send counter for entry 14's figures, a "Not your turn" message); `6.5`-5 (ThreadSanitizer is a repo-wide choice); `6.10`-3 (`ILink` has no failure signal; the smoke run saw no link die); `6.15`-2, `6.19`-1 (device measurements, entry 14); `6.16`-1, -2, -3, `6.19`-5 (the skill folder, `sim.sh`, the simulator shim, and a `SIMULATOR` harness are outside `touches`); `6.19`-2, `6.19`-3, `6.21`-1 (`.github/workflows`, `lib/hal` are out; entry 14's packet builds define-on by hand); `6.11`-6 (a later option; entry 14's packet renames it); `e5-close` D2 remainder (not D3; it has four copies, not two: `loadStore`, `peekStartable`, `loadStartable`, `loadPrefs`).

**F3, the race it leaves.** Storing `taken` right after `drawFront` ends the redundant render and gives a later repaint its `forceFull`. A loop pass inside `drawFront`'s own window (after the VM published, before the store) still asks for a render, which then reads as a repaint and is drawn in full: rarer than today's case (it needs the pass inside `drawFront`), and a full refresh, never stale pixels. The code comment says so.

**F8, why per seat.** R3 and AD-11 forbid assuming two seats, so the skip is for the seats whose ABORT was heard, not for the whole match. Silence (Timeout) still sends: the peer may hear us one way.

**Guards kept (moves).** F4's helpers keep each caller's order: `loopPlaying` flushes after the gesture post and polls; `loopView` only outside Error and PeerGone after `vmHealthy`; `loopHandOff` after `vmHealthy` and before Back (its `now` becomes `millis()` at the call, which `vmHealthy`'s early return makes equal in effect). The confirm glue keeps the early `return true` after a routed touch (a tap answers inside `routeTouch`), the launcher's open guards, and each accept's own `clearTapFlash` order. F15 keeps `activeRing`'s null store before unregister, the count around the whole callback body, the null-ring and argument checks, and the 100 ms limit whose false return abandons the PSRAM block.

## Verification

**Commands:**
- `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j'` -- expected: all pass.
- `python3 scripts/pack_game_test.py`, `python3 scripts/device_log_test.py`, every `scripts/*_test.py` -- expected: pass.
- `python3 scripts/check_upstream_touches.py`, `python3 scripts/check_layers.py`, `./bin/clang-format-fix` (twice, `git status` clean after the second) -- expected: pass.
- After the review, under `/tmp/crosshatch-build.lock` with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`: `pio run -e x4pro`, `pio run -e default`, `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` (default and `-e x4pro`), `scripts/check_flash_budget.py build on`, `build off`, `compare`, `objects`, `sim.sh build x4pro` -- expected: pass; epic total at most +42,000 B / +160 B.

**Results (2026-10-14, at the committed tree):**
- Host suites: `ctest` 2,598 tests, all pass (2 dump tests skipped by design). Every `scripts/*_test.py` passes (`device_log_test.py` 76 tests). `check_layers.py` passes; `./bin/clang-format-fix` twice, nothing new; `check_upstream_touches.py` run on the commit (Auto Run Result).
- Firmware, under `/tmp/crosshatch-build.lock` with the shared cache, in this worktree (incremental): `check_flash_budget.py build on`, `build off`, `compare` (exit 0), `objects` ("No problems"); `pio run -e default` SUCCESS; `pio check` default and `-e x4pro` "No defects found" (PASSED); `sim.sh build x4pro` SUCCESS. No `sticky` build (no board-specific code changed).
- Flash and static RAM, like for like (`check_flash_budget.py` `build on`, `build off`, `compare` in this worktree, the same way at both commits): baseline `0027d23e` games on 6,255,680 B, off 5,957,312 B, +298,368 B; static RAM 190,328 B on, 189,464 B off, +864 B. This tree: 6,255,376 B on, 5,957,312 B off, **+298,064 B**; +864 B static RAM. **This entry: -304 B flash, +0 B static RAM.** Over the epic base `c28120a7` (+258,048 B / +784 B): **+40,016 B flash and +80 B static RAM**, within the 42,000 B / 160 B share (1,984 B and 80 B left). The baseline reads +40,320 B over the epic base, 16 B more than entry 11 recorded (+40,304 B), from the entries merged after it (19 to 23) or the path: not attributed.

## Auto Run Result

**Status:** built. **Summary:** the epic's refactor sweep. Seventeen fixes (Design Notes F1 to F17): a test ties `pack_game.py`'s `MODES` to `ModeTable.h` (F1) and pins `nearby` in both first-party manifests (F14); `CanvasGate` records a push with its own flag (F2); `renderCanvas` stores the frame `drawFront` took (F3); one framed-panel style, shared confirm-dialog glue, and one loop flush tail (F4, `## e5-close` D3); a shipped (codec, PROTO) pairs test (F5); the three-link contract in `LinkContract.h`, run by `EspNowLink` too (F6); the stale `makeEspNowLink()` comment (F7); no `ABORT` copies to a seat whose `ABORT` was heard (F8); the floating-root clock note and pull-back in `device_log.py` (F9, F10); tests for a turn that is no seat (F11), the task double's priority and core (F12), and a stuck VM in a nearby match (F13); the receive callback's quiesce handshake as a host-tested `RxGate` (F15); the session lifetime comment (F16); a committed card file the Writer writes and `merge` reads (F17). Every item of `## 6.1` to `## 6.23` and the named pre-epic items carries its outcome in `deferred-work.md` (Resolved, Resolved in part, Documented, or Kept open with the reason the owner confirms at entry 14).

**Files:**
- `src/activities/games/GamePanelStyle.h` (new), `GameMatchView.cpp`, `GameConfirmDialog.h`, `GameModeActivity.cpp`, `GamesLauncherActivity.cpp`, `GameMatchActivity.{h,cpp}` -- F3, F4, and F8's comment.
- `src/games/CanvasGate.h` (F2), `EspNowLink.{h,cpp}` and `RxGate.h` (new) (F7, F15), `GameLinkSinkGate.h`, `NearbySession.{h,cpp}` (F8, F16); `src/diagnostics/DiagLogStorage.cpp` (the SdFat 2.3.1 comment); `lib/GameCore/Protocol.h` (PROTO comment).
- `scripts/device_log.py`, `device_log_test.py`, `docs/crosshatch/fork-scripts.md` (F9, F10, F17); `scripts/pack_game_test.py` (F1, F14).
- Tests: `test/game_core/LinkContract.h`, `FakeLinkTest.cpp`; `test/game_script/{CanvasGateTest,CodecTest,MatchRoundsTest}.cpp`; `test/game_script/harness/{EspNowLinkTest,GameLinkTest,GameMatchTest,NearbyLeaveTest,NearbyMatchTest,NearbySessionTest,RxGateTest (new),UdpLinkTest}.cpp`, `ThreeLinkContract.h` (removed), `esp_now_link{,.sources}.cmake`, `screen_stubs/FakeRtos.h`, `screen_stubs/freertos/task.h`; `test/diag_log/DiagLogWriterTest.cpp`, `test/diag_log/sample/b0001-s00-3F2A.log` (new).
- `_bmad-output/implementation-artifacts/deferred-work.md` -- each item's outcome, in place.

**Review breakdown:** one pass, four lenses (context-free subagents); 20 findings: high 0, medium 0, low 18, false 2, plus 5 descriptive. Patched 11 (all low): the `## 6.13` pointers, three ledger outcomes reworded to "Resolved in part" (`## 6.2` item 2, `## 6.15` item 1, `## 6.23` item 2), a per-seat gate test, the shipped-pairs PROTO test, a timer case in the no-seat test, the panel-style comment, the cmake comment wrap, the `renderedFrame` member comment, and a first-boot pull-back merge test. Deferred 2 (frontmatter): `renderView`'s `renderedFrame` (pre-existing) and walked boots overlapping unnoted in `merge` (pre-existing). Rejected 7: two false (the stuck-VM tests' assertions are host-specific), five low (the overlap note's naming and size in three-device shapes, CRLF on a checkout with `autocrlf`, outside touches, and a pre-existing bounded flush wait). Two new findings from the build itself are deferred too (the USB drive's live card under an open file, medium unverified; `game-canvas.md`'s stale three-ABORT sentence, low).

**Follow-up review:** not recommended (no high, no medium patched).

**Verification:** `check_upstream_touches.py` PASS on the commit (no upstream file changed; trial merge of `upstream/develop` clean). See Verification's Results: host suites 2,598 pass, script tests pass, firmware builds, `pio check`, `objects`, and `sim.sh build` pass; this entry -304 B flash, +0 B static RAM; the epic at +40,016 B / +80 B of its 42,000 B / 160 B.

**Residual risks:** `EspNowLink.cpp`'s `RxGate` wiring is compiled only by the device build and runs only on a device (entry 14's run). F8 changes what goes on the wire after a peer's `ABORT` (nothing more is sent to it), tested over `FakeLink` only. `GameConfirmDialog::build`'s `[[gnu::noinline]]` holds a code-size choice the compiler made; a toolchain change can move it. The formatter made no change outside this entry's files.
