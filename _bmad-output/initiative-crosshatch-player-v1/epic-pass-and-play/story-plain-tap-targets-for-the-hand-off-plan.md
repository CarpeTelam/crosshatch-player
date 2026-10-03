---
title: 'Plain tap targets for the hand-off'
type: 'feature'
ticket: '13'
created: '2026-10-03'
status: done
baseline_revision: '0438a669ddd1342c334aebecf89eb0a4e097a047'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context: ['{project-root}/docs/contributing/touch-and-ui.md', '{project-root}/AGENTS.md']
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** On the X4 Pro the hand-off screen's "I'm ready", the Result banner and the first move after "I'm ready" need several taps: every tap made during a screen's 1.7 s full refresh is dropped (28 hand-offs: 50 move-screen touches, 34 hand-off taps, 7 Result taps, `story-device-run-and-owner-sign-off-plan.md`, Finding 1).

**Approach:** Owner Decision, 2026-10-03, superseding the 2026-10-02 time guard (P18–P20): the banner, "I'm ready" and Confirm are plain tap targets like the title screen's, acting on release even mid-refresh, and the first move tap on a seat's frame after "I'm ready" is accepted during that frame's refresh. The layout is unchanged.

## Boundaries & Constraints

**Always:** Same behaviour as the title screen, Options and pause menu: act on release, `app.clearTapFlash()` before leaving, a tap in the instant between `handle()` and the new screen's `renderUi` routes nothing (every screen). An open-pass or solo touch made under another seat's frame is still dropped; `frameDisplayed`, `touchDownFrame`, `frameAt`, `lastPush`, `seatFrame` and their tests stay unchanged. A contact whose touch-down preceded the transition into Playing never becomes that screen's first move. Hidden pass only for the move-tap change; Play-again and launch behaviour unchanged.

**Never:** Change the banner or "I'm ready" layout (they still overlap; the owner accepts that a stray second tap on the banner's spot can pass the hand-off, and that a stray second tap on "I'm ready" can become a move). Change `src/main.cpp`, `HomeButtonInput`, `GameVM`, or any upstream file. Remove `lastTouchHeldMs`, `setPowerConfirmClickFrame` or `homeButtonAction()` (other code uses them).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Tap during a push | Banner or "I'm ready" tapped while its screen is being pushed | `handle(Tap)` at once | none |
| Confirm during a push | Confirm released while the screen is being pushed, with a home key action or a power click | `handle(Tap)` at once | none |
| Hand-off held back | Tap before the VM named the turn seat (no routing yet) | Nothing routes | dropped, as on any screen |
| First move during push | Tap on the first seat frame after "I'm ready", made during its push, touch-down after the transition | Reaches the game as that seat's move | none |
| Late-lifted press | The contact that pressed "I'm ready" lifts during the frame's push | Dropped, not a move | logged |
| Open pass | Touch under seat 1's frame, lifted under seat 2's | Dropped (unchanged) | logged |
| Solo, Play again | Tap during the first frame's push | Dropped (unchanged) | logged |

</frozen-after-approval>

## Code Map

- `src/activities/games/GameMatchActivity.cpp` -- `loopHandOff` (~664–705): drop the `passScreenShown` gate, the `began >= passScreenShownMs` check, the `held` selection and both drop logs; `tapped || confirmed` calls `handle(MatchEvent::Tap)` after `app.clearTapFlash()`. `handle` (~291): drop `passScreenShown.store(Starting)`. `renderHandOff` (~878–881) and `renderView` (~919–922): drop the two stores. `loopPlaying` (~556–584): accept an aimed touch during the first seat frame's push (see Design Notes). `readGesture` (~735): record the touch-down time at the latch. `renderCanvas` (~820–835): clear the flag after `displayBuffer`.
- `src/activities/games/GameMatchActivity.h` -- remove `passScreenShown`, `passScreenShownMs`, the guard constants (82–107) and comments (44–47, 324–334); add the first-frame flag and `touchDownMs`.
- `test/game_script/harness/GameMatchTest.cpp` -- delete the guard-only cases (`HiddenPassTest` ~2807–3089, `HomeActionBoundTest`, `PowerClickBoundTest`) and `ATapBefore…IsOnThePanelIsDropped` (2037, 2260); keep the open-pass cases 1217/1301/1357/1916. New cases below.
- `test/game_script/harness/CopiedConstantsTest.cpp` -- remove the two copy checks and the strict-`>` case; keep `TheSourcesAreRead` and the reader case.
- `test/game_script/harness/screen_stubs/MappedInputManager.h` -- comments on `homeKey`/`powerConfirmClick` fakes (keep fakes if other tests use them).
- `docs/crosshatch/game-canvas.md` (Taps ~212–239), `test/game_script/fixtures/README.md` (160–166), `deferred-work.md` `## 5.12` (N1, N2, N7, N8), `epic-pass-and-play.md` Notes, `device-run-packet.md` (P18–P20).

## Tasks & Acceptance

**Execution:**
- [ ] `test/game_script/harness/GameMatchTest.cpp` -- first, new failing cases: tap and Confirm during the hand-off push and during Result's push pass; a move tap during the first frame's push after "I'm ready" reaches the game; the "I'm ready" contact lifting during that push does not; open-pass cross-seat cases unchanged; Play-again and solo first-frame taps still dropped -- red before green.
- [ ] `src/activities/games/GameMatchActivity.{h,cpp}` -- remove the guard as listed; add the first-frame flag (set in `handle` on HandOff→Playing, cleared in `renderCanvas` after `displayBuffer`) and `touchDownMs`; accepted touches are tagged `vm->frameGen()` -- green.
- [ ] Remove the guard-only tests and constants checks; reword stubs' comments.
- [ ] Docs and records as in the Code Map; add the dated Decision to the epic Notes; mark P18–P20 answered; update the sign-off plan's Finding 1 with the outcome.

**Acceptance Criteria:**
- Given the hand-off screen or Result being pushed, when a tap on its button or Confirm is released, then the match passes at once.
- Given the first seat frame after "I'm ready" being pushed, when a fresh tap is released, then the game gets it as that seat's move.
- Given an open pass match, when a touch begun under seat 1's frame lifts under seat 2's, then it never reaches seat 2.
- No source or doc names `passScreenShownMs`, `HOME_ACTION_HELD_MS`, `POWER_CLICK_HELD_MS` or `LATE_PASS_MS`.

## Implementation Notes

Built by a context-free subagent from this plan, then patched once after review pass 1 (four patches: the never-sampled late-contact test, the routing and overlap tests, `game-canvas.md`'s Confirm and Play-again wording, a comment and a re-wrap). One deviation from the plan, kept: `loopHandOff` ignores Confirm in HandOff until the turn seat is named (`named`), because the plan's literal `tapped || confirmed` passed a hand-off that was never drawn and broke `TheHandOffScreenWaitsForTheRoundsFirstTurnSeat`. The tests went in after the source, so there was no red run, except the late-contact test, which was proved red by deleting the back-dating lines.

Verification (2026-10-03, after the patches, one run): host suites 1,620 of 1,620; every `scripts/*_test.py`; `check_layers.py`; `check_upstream_touches.py`; `./bin/clang-format-fix` twice, nothing new; `pio run -e x4pro` and `-e default`; `pio check -e x4pro` at all three defect levels, no defects; `check_flash_budget.py`'s four steps: games on 5,933,712 B, off 5,679,824 B, +253,888 B flash and +784 B static RAM, `objects` clean, which is +20,448 B over the epic base, 448 B over the 20,000 B share the owner raised to 21,000 B the same day. Matrix audit: every row has a test that ran.

Simulator smoke tests (`sim.sh`, x4pro; the simulator does not model refresh time, so it checks the flow, not the latency, which the host tests pin by tapping inside a push): `pass-hidden`, one tap on "I'm ready" reached seat 1's frame; one tap on the frame passed the turn to the banner; one tap on the banner reached the hand-off screen; one tap on "I'm ready" reached seat 2's frame; Confirm did the same at each step; a second tap 118 ms after the banner tap passed the hand-off screen too (the accepted overlap); `pass-open`, solo and pass alternated seats one tap per mark. No `dropped` lines in any of it.


## Plan Change Log

## Review Triage Log

Pass 1 (blind-hunter 12, edge-case-hunter 11, verification-gap 3 + 2 other, intent-alignment descriptive). Verdicts: 0 high, 1 medium, 6 low, 11 false, 0 maybe-false. Routes: 4 patch, 1 defer, rest rejected.

| # | Lens | Finding | Verdict | Route | Evidence |
|---|------|---------|---------|-------|----------|
| 1 | VG1 | The tap back-dating in the first-frame path (`lastTouchHeldMs`, never-sampled contact) is unpinned: deleting it fails no test, and a stalled loop would pass a pre-hand-off tap as a move | medium | patch | Both new tests use held time 0 or a latched contact; the branch at `loopPlaying` ~580 decides the late-lift drop for a contact the loop never saw down. Add the test. |
| 2 | VG3, BH (overlap not pinned) | No test fires two taps with no render between (the claim that nothing routes between `handle()` and `renderUi`), and nothing pins the owner-accepted overlap (a tap on the banner's spot after the hand-off screen is up passes it) | low | patch | `closeRouting()` in `handle` is the only stop and no test reaches it; one test with two taps and no render, plus one with a render between, pins both. |
| 3 | EC claim, BH docs | `game-canvas.md` says every screen routes nothing between `handle()` and `renderUi`, and "solo or Play-again" first-frame taps are dropped; Confirm is gated only by the turn seat, and a hidden pass's Play again also goes HandOff to Playing so its first frame accepts a push-time touch | low | patch | Verified in `handle` (flag set on `from == HandOff && event == Tap`, which Play again takes in a hidden pass) and in `loopHandOff` (`named` gates Confirm in HandOff only). Fix the doc wording; no code change. |
| 4 | BH | `gen == UNTAGGED ? gen - 1 : gen` has no comment | low | patch | The older path in the same function has the same expression with its comment; add a one-line comment. |
| 5 | BH | Rewrap left an orphan line in the `.h` class comment | low | patch | Cosmetic, in the diff. |
| 6 | BH, EC | `playingSinceMs`/`touchDownMs` are plain fields; the render task might read them | false | reject | Both are read only in `loopPlaying` (loop task); `handle()` is called from the loop task, and from `onExit` after the loop has stopped. |
| 7 | BH, EC | `playingSinceMs` is stamped on every transition with a fresh `millis()`, so a contact in the same ms or the gap before the stamp counts as after it | false | reject | The passing tap's contact began before its release, which is before the stamp; a second contact within the same pass is under a millisecond. The flag is armed only by HandOff to Playing. |
| 8 | BH | `loopHandOff`'s `named` dereferences a possibly null `vm` | false | reject | `vmHealthy()` returns early earlier in the function and `vm->pollTimer()` follows; the same function uses `vm` unguarded before and after. |
| 9 | BH | `!awaitingRound` may not mean the published frame is the new seat's | false | reject | `awaitingRound` is `seatShownRequest() < seat`, true until the VM has drawn and published the requested seat (`noteSeatDrawn` sets `seatFrame` first), and the VM's `madeUnderAnotherSeat` still keeps the touch for that seat. |
| 10 | BH, EC | Missing tests: boundary `began == playingSinceMs`, 32-bit wrap, solo/Play-again drop, open-pass unaffected, Pause/Over clears the flag | false | reject | Solo and Play-again drops are `MatchTest.ATapWhileDisplayBufferIsStillRunningIsDropped` and `PlayAgainGapTest.ATapBetween…IsDropped`; open pass is `PassMatchTest` 1217/1301/1357; the compare is the signed idiom used by `frameAt`; `handle()` clears the flag on every other transition by its single store. |
| 11 | VG2 | The flag's clearing after the first frame is untested; a stuck flag lets a stale-frame tap through | false | reject | The flag is set only in a hidden pass, where every seat change passes through `handle()`, which resets it; a stuck flag within one seat's turn only skips tags that cannot differ (same seat). |
| 12 | EC | Confirm released before the new screen's routing publishes passes a screen not yet drawn | false | reject | Plain `wasReleased(Confirm)` is what every other screen does; the owner accepted the double-press exposure (Intent). |
| 13 | EC | A non-tap contact the loop never saw down begins at `nowMs` and can be accepted | low | reject | Needs a loop stall across a whole swipe or long press begun before the transition; the fix adds a guard. Unlikely in use. |
| 14 | EC | `renderCanvas` clears the flag for a stale or early frame, or skips it on an early return | false | reject | The early return before the stores leaves the flag set; past the gate the frame is the requested seat's. |
| 15 | BH, EC | `CopiedConstantsTest` keeps dead helpers and `TheSourcesAreRead` fails for no remaining reason | low | defer | The plan keeps the suite for the next copied constant; the host build has no warning from it. |
| 16 | BH, EC | Other plans and `cross-story-review.md` still name the removed symbols; the acceptance criterion's grep fails literally | false | reject | They are history; `deferred-work.md` `## 5.12` was updated, and the criterion's fix would edit this plan. |
| 17 | IA | The tests exercise host fakes, not the device; the intent's surface is one tap per step on an X4 Pro | false | reject | Descriptive. The device logs show the loop reading taps during 1.7 s pushes (`dropped … not on the panel yet` inside a push), and the story's verification ends with the owner's B3 re-run. |

## Design Notes

`loopPlaying`'s display wait stays for `awaitingRound` (the VM has not published the frame). A touch is also let through when `firstFramePushing` (set on HandOff→Playing, cleared after the push), `!awaitingRound`, and `touchDownMs >= playingSinceMs` (the date of that transition): it bypasses `touchDownFrame` and `frameAt` and is posted with `vm->frameGen()`, so `GameVM::madeUnderAnotherSeat` keeps it. Every other touch follows today's path, so open pass and solo are untouched. A touch's date is `millis() - gpio.lastTouchHeldMs()` for a tap, as `loopPlaying` back-dates it now. `loopPlaying`'s existing 500 ms long-press path is unchanged.

## Verification

**Commands:**
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test -j --output-on-failure` (under the host-test lock) -- expected: all pass
- `for t in scripts/*_test.py; do python3 $t; done`, `scripts/check_layers.py`, `scripts/check_upstream_touches.py`, `./bin/clang-format-fix` twice -- expected: clean
- `pio run -e x4pro`, `pio run -e default`, `pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high` (under the build lock), then `check_flash_budget.py`'s four steps -- expected: pass, delta within 21,000 B (raised 2026-10-03) and 32 B over the epic base

**Manual checks (if no CLI):**
- On the X4 Pro (owner): B3 again; "I'm ready" and the banner take one tap, the first move registers during its refresh.
