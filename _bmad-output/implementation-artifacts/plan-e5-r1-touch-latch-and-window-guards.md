---
title: 'e5-r1: free the touch latch on leaving Playing, pin the forced-exit deadline, fix stale 5.12 comments'
type: 'bugfix'
ticket: ''
created: '2026-10-03'
status: 'done'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
baseline_revision: '0264a1f4'
context:
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-pass-and-play/epic-pass-and-play-retrospective.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Retrospective F4: `touchDownLatched` is freed only at the end of `loopPlaying`, so a latch set in Playing survives a pass that returned early or a visit to HandOff/Result/menus, and the first pass back in Playing dates a fresh contact by a stale touch-down and drops it as "began before the hand-off". V4: `FORCED_EXIT_DEADLINE_MS == 1500` is not pinned against game-canvas.md's 1,500 ms bound. F2: three comments still describe the 5.12 time guard that 5.13 removed. V5 (part): a contact latched after "I'm ready" and lifting during the first frame's push has no test.

**Approach:** Free the latch on the first non-Playing pass that sees no finger down; keep the owner's rule (epic Notes line 138) unchanged. Add a `static_assert`, correct the three comments, add host tests.

</frozen-after-approval>

## Boundaries & Constraints

- touches: `GameMatchActivity.cpp`, `GameMatchTest.cpp`; plus an entry under `## e5-r1` in deferred-work.md. No behaviour change except the latch clear. Comments only for F2.
- A finger still down across the transition keeps its latch: it began before the hand-off and must still be dropped.

## Code Map

- `GameMatchActivity::loop` (the clear), `loopPlaying` (end-of-pass clear, unchanged), `readGesture` (the latch).
- Tests: `HiddenPassTest.AContactLatchedBeforeTheTurnPassedDoesNotDropTheNextSeatsFirstMove`, `...BegunAfterTheHandOffPassedAndLiftedDuringTheFirstFramesPushIsAccepted`; the existing drop test `...BegunBeforeTheHandOffPassed...IsDropped` and `TheFirstMoveTapDuringTheFirstFramesPushReachesTheGame` still pass.

## Design Notes

- Why only the first-Playing-pass case matters: any Playing pass without a finger frees the latch, so the defect needs the fresh contact to be the very first Playing pass after the transition (the push-window tap). The reproducing test therefore waits on the VM's own log (no loop pass) before the tap.
- Guards in the functions touched: `loop()` gained only the clear; `loopPlaying`'s `!contactHeld` clear stays (it runs after `readGesture` reads the latch on a release pass, which is why the new clear is limited to non-Playing states: there is no latch read to protect). `loopHandOff` change is a comment only.
- Reproducing test: the game's timer (due after `advance(1000)`) passes the turn while the finger is down and latched (a timer cannot be under 1000 ms, and a finger held 500 ms becomes a long press, which frees the latch by itself, hence the due-before-touch ordering). Failed before the fix (`dropped a touch that began`, no `tap for seat 2`), passes after.
- `isScreenTouchHeld` is the same query `touchSnapshotFrom` makes each Playing pass.

## Review Triage Log

All four lenses ran as context-free subagents and returned.

| Finding | Verdict | Evidence / route |
|---|---|---|
| VG: the held-finger guard in `loop()` is unpinned by a test | low | A Tap held across the transition is dropped anyway by `lastTouchHeldMs` back-dating, so a host test cannot tell the guard from an unconditional clear; only a non-tap gesture (swipe) differs, and the owner's rule names taps. Rejected: the fix is a test that cannot fail. |
| EC: test 2 passes before and after the fix | low | True and intended: it is the V5 coverage (a latched post-transition contact), not a guard on the clear; stated in its comment. Rejected. |
| EC: reproduction depends on the timer being due only after `advance(1000)` / long press | false | The test failed before the fix and passes after (Verification), so it is not vacuous; the 1000 ms minimum (`TIMER_MIN_MS`) and long-press reason are in the plan. |
| EC: lift and new contact between two passes keeps a stale latch | maybe-false | The loop passes every few ms; two contacts inside one pass cannot be told apart without a HAL touch-down time. Rejected as low, fix adds surface. |
| EC: a contact begun in HandOff and held into Playing latches fresh | false (as a defect) | Unchanged behaviour; Tap is back-dated by `lastTouchHeldMs`, the owner's rule covers a plain tap. |
| BH: `static_assert` access to `FORCED_EXIT_DEADLINE_MS` | false | The constant is a public static constexpr in the header (line 78) and used in the same file; the firmware builds pass (Verification). |
| BH: removed comment leaves `now` undocumented | false | `now` is only passed to `store.flushIfDue`; the removed sentence described the deleted time guard. |
| BH: `HIDDEN_TIMER_PASS_GAME` lives inline | false | Matches the file's other inline `HIDDEN_*_GAME` constants; AGENTS.md's fixtures rule is about files under `games/`. |
| BH: plan Boundaries lists the header | low | Fixed (header untouched). Ledger: no new upstream file touched beyond the fork's own game files. |
| BH: deferred entry bundles two items | low | Both are V5 leftovers sharing a source row; kept as one. |
| IA: tests exercise host fakes, not the device | low | Stated in Verification; F4's consequence was unverified in the retrospective too. |

## Verification

- Host tests: full `ctest` 1622/1622 pass. The F4 test failed before the fix (`dropped a touch that began`, no `tap for seat 2`) and passes after; the V5 test passes before and after (coverage only).
- Flash/static RAM, `scripts/check_flash_budget.py` (build on, build off, compare, objects), x4pro, same way at base 0264a1f4 (src file restored to base) and with the change: `firmware.bin` games-on 5,933,712 B both, games-off 5,679,824 B both; static internal RAM total 187,848 B both. Delta 0 B at firmware.bin granularity (image size is padded; the clear is a few instructions). Objects check: no problems. Measured on the uncommitted tree whose src is the commit's.
- `pio run -e x4pro` and `-e default` build; `pio check` (default, and `-e x4pro`) with all three fail-on-defect levels exit 0.
- `./bin/clang-format-fix` twice, `git status` unchanged by the second run.
