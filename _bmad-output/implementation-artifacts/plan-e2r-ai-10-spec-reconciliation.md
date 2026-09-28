---
title: 'Reconcile the spine and the epic-script-runtime epic with the as-built runtime (retro AI-10)'
type: 'chore'
ticket: ''
created: '2026-09-28'
status: 'built'
route: 'oneshot'
route_source: 'auto'
baseline_revision: '69c04796a1248bd05e494b0de02c93996be657fb'
review: 'quick'
review_source: 'auto'
lenses_ran: ['quick']
review_loop_iteration: 0
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** The epic-script-runtime retrospective (AI-10: the Spec-to-implementation table, A1, A2, F4, and "Other divergences") found the architecture spine, and the epic file, stale against the as-built runtime: AD-6's 256 KB arena, R6/AD-14's silent `Cancelled`, AD-21 without `Starting`, AD-5's join semaphore and `Quit` post, the envelope's CI row, the Structural Seed's names, the allowed edges, `reject_reason_bytes` and load failures, AD-19's release freeze (owner decision F4), and the epic Notes' flash delta without its baseline.

**Approach:** Amend `ARCHITECTURE-SPINE.md` and `epic-script-runtime.md` in place to the as-built, each change sourced from the code, marking spine amendments the spine's way ("**Amended 2026-09-28 (owner):**") and epic edits with the date; touch `game-api-seed.md` only where it contradicts the as-built. Docs only, no new decisions; `tickets.toml` is history and stays; the Screens row stays (owner chose to restore the code, AI-5); AD-19 states F4 as the rule, since another lane changes `fork_release.py`.

</frozen-after-approval>

## Implementation Notes

Oneshot: about 40 changed lines of Markdown across three docs, each a factual edit from a code citation.

- Spine (`ARCHITECTURE-SPINE.md`, `updated` 2026-09-28): each edited rule is prefixed "**Amended 2026-09-28 (owner):**", the spine's own marker, and the stale text is replaced, not left beside the amendment. Layer table: `GameScript → lib/Utf8` (`TextMetrics.h`), `src/games → lib/EpdFont` + `src/fontIds.h` (`FrameReplay.cpp`), plus a marked note; the Screens row is unchanged. AD-5: quit flag plus task notify, `finished` polled every 5 ms (`GameVM.cpp` `STOP_POLL_MS`, `join`, `cancel`), abandon's delete-in-Lua path and leak-all fallback with the kept store slot (`GameVM::abandon`, `deleteIfStuckInLua`; `GameMatchActivity::abandonVm`, `slotLeaked`); the `Cancelled` bullet names the watchdog exception. AD-6 first bullet: 464 KiB block = 448 KiB region + 16 KiB reserve, 256 KiB count cap (`ArenaAllocator.h`). AD-7: the policy is `GameScript::RefreshPolicy`. AD-8: `reject_reason_bytes 64`, cut at a UTF-8 boundary (`Session.h`, `LuaGame.cpp` `copyReason`), not an error. AD-14: the watchdog view and load failures ("The game could not start", `GameMatchActivity::onEnter`, `loadFailureReason`). AD-19: F4 stated as the rule. AD-21: `Starting`, Back in Paused resumes (`MatchLifecycle.cpp`), diagram edge "Resume or Back". Structural Seed: A2's names, `GamesListActivity`, `SoloRounds` and `RefreshPolicy` in the task view, the two missing fork scripts, and the CI jobs; envelope CI row lists the five `crosshatch-ci.yml` jobs.
- Epic (`epic-script-runtime.md`): R4, R6, R7 (`Starting`), Done when 2, and the Notes' flash baseline, each marked "(amended 2026-09-28, retro AI-10)". `tickets.toml` untouched.
- `game-api-seed.md` unchanged: nothing in it contradicts the as-built. Its one near-miss, "never less" in `ch.gfx.refresh` against an unchanged frame never refreshing, is retro R8, which AI-11 routes to epic-game-api-docs, so it is left for that epic. Its "256 KB of Lua memory" matches the cap.
- Seen in pass 1, not changed then: `src/games` also includes `Utf8.h` (`FrameReplay.cpp:11` only) and `FreeInkUICore.h` (`GameTouch.h:3`, the SDK touch types AD-20 names), which the layer row did not list. Added in pass 2 (triage row 7).

## Review Triage Log

Pass 1, quick lens, run in this session (no subagent tool here), over the diff from `69c04796`: high 0, medium 0, low 1 (patched), false 2.

| # | Finding | Verdict | Action / evidence |
|---|---------|---------|-------------------|
| 1 | The layer row said `EpdFont`/`fontIds.h` "in `FrameReplay` only", a restriction nobody decided | low | Patched: the row names the edge and its user without "only"; no new decision. |
| 2 | AD-14's first sentence gains "except the 3 s watchdog's" without its own marker | false | The marked 2026-09-28 bullet directly under it states the change; the 2026-09-27 AD-5 amendment already made that exception. |
| 3 | AD-21's diagram does not show `Starting` | false | Kept as text in the marked bullet: `Starting` precedes whichever initial state applies, so drawing it would rewrite every `[*]` edge, beyond a minimal amendment. |

Pass 2, the orchestrator's independent context-free review of `8d8d2969`, plus two orchestrator decisions: high 0, medium 3, low 7, false 0; all patched except row 4a (a decision).

| # | Finding | Verdict | Action / evidence |
|---|---------|---------|-------------------|
| 4a | `tickets.toml` entry 1 still says "one 256 KB block" | accepted as is | Orchestrator decision: `tickets.toml` entries are the record of what each build was given, so they stay untouched; the orchestrator notes it in the retro. |
| 4b | AD-19's F4 rule is not concrete or testable | medium | Patched with the orchestrator's wording: refused only when a level an earlier fork release shipped frozen is no longer frozen here (`API_LEVEL` below it, or equal with `API_LEVEL_FROZEN` false); an open preview above every frozen-released level is allowed; marked pending the `fork_release.py` change. |
| 5 | AD-5 "for up to 500 ms more" is not a real bound | medium | Confirmed: `deleteIfStuckInLua` can wait up to 10 `vTaskDelay(1)` per 5 ms turn (`GameVM.cpp`). Now "about 100 polls at 5 ms, not wall-clock, retro AI-4", and each poll first deletes normally if the task finished. |
| 6 | AD-14/AD-21 load failures overstated | low | Confirmed: `LuaGame::load` OOM is a `ScriptError` ("not enough memory", "The game stopped with an error"), and Session OOM and load failures run after `Started`, so `Playing → Error`. AD-14 now splits `onEnter` failures plus Session OOM ("could not start") from `load` OOM; AD-21 says only `onEnter` failures leave `Starting` for `Error`. |
| 7 | `src/games` also includes `lib/Utf8` and SDK `FreeInkUICore.h` | medium | Confirmed (`FrameReplay.cpp:11`, `GameTouch.h:3`); added to the Device adapters row and the marked note, since AI-5's include check will read the table. |
| 8 | Screens sentence read as a description of current code | low | Reworded as the rule, which AI-5 restores the code to. |
| 9 | The invariants diagram lacks the new edges; "launcher" label | low | Added `lib/Utf8`, `EpdFont`/`fontIds.h`, and `FreeInkUICore.h` nodes and edges, and "Games list / launcher", marked with a Mermaid `%%` comment. |
| 10 | Epic R3 still says `FrameReplay` is the only refresh policy | low | Patched to "applies ... `GameScript::RefreshPolicy`", marked. |
| 11 | Structural Seed drops the planned `GamesLauncherActivity` | low | Patched: `GamesListActivity`, which epic-install-and-launcher replaces with `GamesLauncherActivity` (named in that epic's Boundaries). |
| 12 | Unmarked edits (AD-5 cancel bullet, AD-14 "except", the task-view labels) | low | Each now carries the 2026-09-28 marker (a `%%` comment in Mermaid). |
| 13 | Plan note says `ForkReleaseProbe.cpp` includes `Utf8.h` | low | Wrong; corrected to `FrameReplay.cpp:11` only. |

## Verification

**Commands:**
- `python3 scripts/check_upstream_touches.py` -- PASS on the commit (only fork-owned `_bmad-output/` paths change); trial merge clean
- `./bin/clang-format-fix` twice -- exits 1 here (only clang-format 18 is installed; the wrapper needs 21); the change is Markdown only, which the whole-tree C/C++ format check does not read. `git status` shows only the two docs and this plan

**Manual checks (if no CLI):**
- Each amended statement matches its cited source: `lib/GameScript/ArenaAllocator.h`, `src/games/GameVM.{h,cpp}`, `lib/GameCore/MatchLifecycle.{h,cpp}`, `src/activities/games/GameMatchActivity.cpp`, `lib/GameCore/Session.h`, `lib/GameScript/LuaGame.cpp` (`copyReason`), `lib/GameScript/RefreshPolicy.{h,cpp}`, `lib/GameScript/SoloRounds.h`, `lib/GameScript/TextMetrics.h`, `src/games/FrameReplay.cpp`, `.github/workflows/crosshatch-ci.yml`, `docs/crosshatch/api-level-1.txt`.
