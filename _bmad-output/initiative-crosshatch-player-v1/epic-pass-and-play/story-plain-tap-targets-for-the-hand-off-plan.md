---
title: 'Plain tap targets for the hand-off'
type: 'feature'
ticket: '13'
created: '2026-10-03'
status: 'draft'
route: 'full'
route_source: 'auto'
review: ''
review_source: ''
lenses_ran: []
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

## Plan Change Log

## Review Triage Log

## Design Notes

`loopPlaying`'s display wait stays for `awaitingRound` (the VM has not published the frame). A touch is also let through when `firstFramePushing` (set on HandOff→Playing, cleared after the push), `!awaitingRound`, and `touchDownMs >= playingSinceMs` (the date of that transition): it bypasses `touchDownFrame` and `frameAt` and is posted with `vm->frameGen()`, so `GameVM::madeUnderAnotherSeat` keeps it. Every other touch follows today's path, so open pass and solo are untouched. A touch's date is `millis() - gpio.lastTouchHeldMs()` for a tap, as `loopPlaying` back-dates it now. `loopPlaying`'s existing 500 ms long-press path is unchanged.

## Verification

**Commands:**
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test -j --output-on-failure` (under the host-test lock) -- expected: all pass
- `for t in scripts/*_test.py; do python3 $t; done`, `scripts/check_layers.py`, `scripts/check_upstream_touches.py`, `./bin/clang-format-fix` twice -- expected: clean
- `pio run -e x4pro`, `pio run -e default`, `pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high` (under the build lock), then `check_flash_budget.py`'s four steps -- expected: pass, delta within 20,000 B and 32 B over the epic base

**Manual checks (if no CLI):**
- On the X4 Pro (owner): B3 again; "I'm ready" and the banner take one tap, the first move registers during its refresh.
