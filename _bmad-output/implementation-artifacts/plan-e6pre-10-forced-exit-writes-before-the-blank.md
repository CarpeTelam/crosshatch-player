---
title: 'e6pre-10 forced exit writes before the blank'
type: 'fix'
ticket: ''
created: '2026-10-04'
baseline_revision: '38087567'
status: 'built'
route: 'full'
route_source: 'auto'
review: ''
review_source: ''
lenses_ran: []
review_loop_iteration: 0
context:
  - '{project-root}/docs/crosshatch/game-canvas.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** In a hidden pass match's forced exit (sleep, any Replace) the privacy blank is pushed before the SD steps and counts against `FORCED_EXIT_DEADLINE_MS` (1,500 ms). On the X4 Pro the half refresh took 1,654 ms (owner's log, 2026-10-03), so the resume write, the `resume.bin` delete retry, and the `ch.store` flush were skipped even when the VM joined at once (deferred-work `## 5.6`).

**Approach:** Owner decision, option (a): run those three steps before the blank, inside the same deadline; the blank comes last in `onExit()`, is never gated, and always runs.

## Boundaries & Constraints

**Always:** Keep every guard of the moved code (Design Notes). The deadline gate (`sdStepAllowed`) still bounds when each SD step may start; the blank still runs when a step is skipped or fails. Keep the cheap fixes (no blank where nothing private shows: the `panel == Seat` rule).

**Never:** Change `FORCED_EXIT_DEADLINE_MS`, `STOP_TIMEOUT_MS`, `GameVM`, the Leave path's own blank, or the non-Seat panels' behaviour. Edit the spine (e6pre-8 amends AD-20 on another branch), `.skills/`, or the SDK pointer.

</frozen-after-approval>

## Code Map

- `src/activities/games/GameMatchActivity.cpp` -- `onExit`, `stopVm`, `pushForcedExitBlank` (the only code change); `MatchPersistence` (`beginForcedExit`, `sdStepAllowed`) is unchanged.
- `test/game_script/harness/GameMatchTest.cpp` (`HiddenPassTest`), `ResumeMatchTest.cpp` (`PassResumeTest`) -- the order and deadline tests.
- `docs/crosshatch/game-canvas.md` -- The forced exit, Hidden pass states, Saves, the open-items list. `formats.md` and `device-run-packet.md`: checked, they state no forced-exit order (the packet records the owner's 2026-10-03 results and stays as history).

## Design Notes

History read with `git log -L` on `onExit`/`stopVm` (17d02483 added the blank, d45dd71d the deadline, 68ec417b the resume write). The order is now: `handle(ForcedExit)`; `stopVm()` (cancel and join, resume write while the VM that holds the snapshot exists, abandon if it did not join, whose hook writes the last snapshot); `retryResumeDelete(true)`; `flushStore()`; `pushForcedExitBlank()`.

What each guard protects:

- Blank's `persistence.forcedExit()`: a user Leave pushes its own blank after its stop (`leave()`), so the shared helper must not push a second one; `lifecycle.hiddenPass()`: solo and open pass show nothing private, and the push would only cost time; `panel == Seat`: the blank is for a seat's frame on the panel, and in HandOff, the blank, Over, a drawn error view, or a menu on no frame it would only add 1,654 ms. These three stay as they were. The old no-VM special case in `onExit` (blank first, R6) is the same check, so it folds into the one call at the end.
- Blank not gated by `sdStepAllowed`: privacy beats the SD steps' deadline; a skipped or failed step must never keep a seat's frame on the panel through sleep. Nothing follows the blank, so its time costs nothing.
- Resume write before the VM is freed or abandoned (`stopVm`): the snapshot lives in the VM's memory; unchanged, only the blank that sat between the join and it is gone.
- `sdStepAllowed` on each SD step: bounds the lock hold when the join ran long or a step was slow; unchanged.

What the blank is for: hiding a seat's private hand-off content before sleep or Home. Reordering delays the push by the SD steps' time and nothing else; the exit still returns only after the blank, so the sleep screen still draws over white.

**Worst-case timeline** (clock from `onExit()`'s start; host-modelled, device figures from the 2026-10-03 log): joined VM: join about 5 ms, resume write 91 ms, delete retry and store flush tens of ms, blank starts about 100 ms in and returns about 1,750 ms in (1,654 ms push). Stuck VM: join ends about 500 ms, the resume write starts then, the abandon ends about 1,030 ms plus the write, the store flush follows, the blank starts about 1,150 ms and returns about 2,800 ms. Bound: a step starts only before 1,500 ms, so the blank starts by 1,500 ms plus the longest step that started in time (if a step takes 1,000 ms, 2,500 ms in), and `onExit()` returns that plus the half refresh. Before: the blank started at about 5 ms (joined) or about 500 ms (stuck) and the steps after it were skipped. So the seat's frame stays on the panel for the SD steps' time longer: about 100 ms joined, about 650 ms stuck, never more than the deadline plus one step. These are estimates from the logged figures, not a device measurement. A started SD step is not interruptible: a card that hangs inside one delays the blank, as it already delayed every later step.

## Verification

**Commands (host):** `ctest --test-dir build/test -j4` -- 1,670/1,670 (e6pre-1's recorded 1,656 predates e6pre-2 and e6pre-9, so no delta is claimed; this build adds 5 tests net in `PassResumeTest`/`HiddenPassTest`, replaces 3, renames 2). New/changed tests: `HiddenPassTest.TheForcedExitOnASeatsFrameFlushesTheStoreAndThenPushesTheBlank`, `TheForcedExitWithAStuckVmPushesTheBlankAfterTheAbandon`, `TheBlanksRefreshNoLongerCostsTheSdSteps`, `AStoreFlushStartedInsideTheDeadlineMayRunPastItAndTheBlankStillFollows` (deadline bounds a step; the blank follows past it), `TheBlankIsPushedEvenPastTheDeadline` (blank runs when a step is skipped); `PassResumeTest.AHiddenForcedExitWritesThePendingSnapshotAndThenPushesTheBlank`, `AHiddenForcedExitWritesTheResumeThenFlushesTheStoreThenPushesTheBlank`, `AHiddenForcedExitRetriesTheResumeDeleteAndThenPushesTheBlank`, `AResumeWriteSkippedPastTheDeadlineStillGetsTheBlank`, `AResumeWriteThatFailsInTheExitStillGetsTheBlank`. Non-Seat panels: the unchanged HandOff, blank-pause-menu, Over, drawn-error-view, Leave, and open-pass/solo tests. Mutation: putting `pushForcedExitBlank()` back before `stopVm()` fails 8 of them (checked, then restored).

**Not run:** B7.6 (sleep with a dirty store) on the X4 Pro is still UNRUN: the reorder is verified on the host only. Firmware builds and checks: see Results.

## Review Triage Log

Lenses ran as context-free subagents and both returned: blind/edge-case hunter, intent/verification-gap. 0 high, 2 medium, rest low.

| Finding | Verdict / route | Evidence |
|---|---|---|
| `GameMatchActivity.h:48` still says blank "before the SD steps" (gap) | medium / patch | Reworded to "after the VM's stop and the SD steps". |
| No-VM test passes under either order (gap) | medium / patch | No SD step is pending in that scenario; comment and assertion reworded to say what it pins; order is pinned by the VM tests and the mutation. |
| Test counts / baseline unreconciled (gap) | low / patch | Plan claims no delta; the mutation result (8 fail) was run by me. |
| Doc table wording on the resume write inside the abandon span (gap) | low / patch | Rows reworded. |
| `onExit` comment says the blank "always" runs (blind) | low / patch | Reworded: a skipped or failed step never prevents it, panel guards aside. |
| Doc lines over 120 columns (both) | low / patch | Rewrapped the edited paragraph. |
| Unbounded blank delay if a card stalls inside a started step (blind) | low / accept | Stated in Design Notes and the doc; the owner chose option (a). |
| No test for the delete retry skipped past the deadline (gap) | low / defer | Same `sdStepAllowed` gate as the resume write and flush, unit-tested in MatchPersistenceTest. |
| Spine AD-20 not edited (gap) | false | e6pre-8 owns the amendment on another branch; the deferred entry's wording narrowed. |

## Results

(filled after builds)
