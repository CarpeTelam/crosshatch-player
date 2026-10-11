---
title: 'e8-pins: pin the SDK gesture thresholds (A-5) to their source'
type: 'chore'
ticket: ''
created: '2026-10-11'
status: 'built'
baseline_revision: '04c6e1bc5850170aa0ed08d3022417d5e1f2db80'
route: 'oneshot'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
followup_review_recommended: false
context: []
warnings: []
deferred:
  - summary: >-
      The 28 px tap slop and the derived 29 px (TOUCH_TAP_SLOP_PX + 1) are quoted in the docs and comments with no pin
      beside them; only the double's own copy of 28 is pinned.
    evidence: |-
      Pre-existing and outside A-5's three constants. CopiedConstantsTest.TheDoublesTapSlopIsTheDevicesTouchTapSlop pins
      the double's constant to the header, not the prose in GameTouch.h and game-canvas.md.
    location: >-
      src/games/GameTouch.h:17, docs/crosshatch/game-canvas.md:27
    severity: low
---

<intent-contract>

## Intent

**Problem:** `src/games/GameTouch.h`, `src/activities/games/GameMatchActivity.cpp` and `docs/crosshatch/game-canvas.md` copy the SDK's `TOUCH_SWIPE_MIN_PX` (60), `TOUCH_SWIPE_MAX_MS` (700) and `TOUCH_TAP_RELEASE_SLOP_PX` (`TOUCH_SWIPE_MIN_PX - 1`, so 59) as numbers and prose, and `CopiedConstantsTest` pins none of them (epic 8 retrospective A-5).

**Approach:** Add `CopiedConstantsTest` cases that read the three constants from `InputManager.h` as the existing cases do (a text pin for the slop's expression), and name the pins in a comment beside each copy and in the doc. Test-only: the only non-test edits are those comments and the doc; no device behaviour, `games/` or upstream file changes.

**Decision (2026-10-11, the orchestrator, after this run's blocked report):** the scope is A-5 alone. V3 (pinning `HostBounds.h`'s `DIALOG_TOP`, `DIALOG_BOTTOM` and `BANNER_TOP` to the host's layout) is split out and goes back to the owner, because the screen doubles cannot give those rows faithfully (evidence in Design Notes). Nothing of V3 is built here.

</intent-contract>

## Implementation Notes

Oneshot: a small mechanical change (about 90 added lines, mostly tests and comments) with the plan's design fixed beforehand.

Files: `test/game_script/harness/CopiedConstantsTest.cpp` (three pin cases, a self-test of the slop's text pin, helpers), comment-only pointers in `src/games/GameTouch.h`, `src/activities/games/GameMatchActivity.cpp`, `docs/crosshatch/game-canvas.md`, `test/game_script/harness/screen_stubs/MappedInputManager.h` and `GameMatchTest.cpp` (the last two added by review: they quote the same numbers). The pins compare the header with literals in the test (the copies are prose); formatting made no change outside these files.

## Review Triage Log

### 2026-10-11 — Review pass
- verdicts: 14 findings — high 0, medium 2, low 9, false 3, maybe-false 0 (the verification-gap lens filed no gap, and the intent-alignment lens only described: its R1 reading is the one implemented, the gap it names (copies not read by code, R2) is the row below)
- findings:
  - `[medium]` `[patch]` Blind: other copies of 59/60/700 (screen double `MappedInputManager.h:127,330-334`, `GameMatchTest.cpp:1171,1204`) are neither named nor pinned (also edge-case 1, verification-gap other finding, test header wording) — the copies are real; named the pins in the double's and the match test's comments and listed them in the test header and the failure messages. The numbers stay unpinned by code, as the intent's design (header against literals) has it.
  - `[low]` `[patch]` Blind: the test header said the thresholds were copied "not into the double" while the double's comments quote them — reworded.
  - `[medium]` `[patch]` Blind: the comments and doc name cases by truncated stems that cannot be grepped — now cite the `CopiedConstantsTest.*Pinned*` filter, which the three cases' names match.
  - `[low]` `[patch]` Blind: the doc and comments promise a retune "fails the host tests until these lines follow", true only until the test literal is edited — reworded to say the copies and literals are updated together by hand.
  - `[low]` `[patch]` Blind: the slop self-test lacks the two-definition case that the pin's exactly-one depends on — added (count 2).
  - `[low]` `[patch]` Blind: failure messages awkward (clang-format split a literal) and incomplete — one `retuneMessage` helper, listing every copy.
  - `[low]` `[patch]` Edge 2: `GameMatchActivity.cpp` comment names no case — now names the "Pinned" cases itself.
  - `[low]` `[reject]` Edge 4 and Blind (self-test): the slop's text pin fails on a harmless reformat such as parentheses or a `u` suffix — failing loudly is the safe direction for a text pin of an expression; its message now says a reformat needs SLOP_DEFINITION to follow; a looser regex adds complexity for a case nobody has.
  - `[low]` `[reject]` Edge 5: the second slop assertion is redundant — it fires when a plain-number definition exists beside the expression, which the count alone does not see.
  - `[low]` `[defer]` Edge 3: the derived "29 to 59 px" and "28 px" quotes (`TOUCH_TAP_SLOP_PX`) in the docs and comments are unpinned copies — pre-existing, outside A-5's three constants; the double's own 28 is pinned by an existing case.
  - `[false]` `[reject]` Blind: the plan's deferred-work step is missing (`deferred-work.md` note) — the orchestrator carries V3 there and this build must not edit it (brief); also a plan-edit fix.
  - `[false]` `[reject]` Blind: the plan file is stale and half-closed (status, estimate, no record of the mutation run) — plan-edit finding, and the plan is finalized here with the record below.
  - `[false]` `[reject]` Blind: the "games-check count stays 129" claim has no source — it is the orchestrator's stated constraint; no games-check file is touched (`git diff --stat`).
  - `[low]` `[reject]` Blind: the pins check the header against a literal, not the prose copies — the intent's design ("read them from the SDK header ... fails when the expected value or the source differs"); a text check of the prose would add a test of comments.

## Design Notes

**A-5.** `InputManager.h:483-485` defines `TOUCH_SWIPE_MIN_PX = 60`, `TOUCH_TAP_RELEASE_SLOP_PX = TOUCH_SWIPE_MIN_PX - 1` and `TOUCH_SWIPE_MAX_MS = 700`; `INPUT_MANAGER_HEADER_PATH` is already a compile definition of `GameMatchHarnessTest` (`match.cmake:96`), which already builds `CopiedConstantsTest.cpp`. The expected values are literals in the test (the copies are prose, not code), so a retune of the header or of the literal fails it. The slop's expression is read as text because `constexprValue` takes only a plain number; the test also asserts `constexprValue` finds nothing for it, so a rewrite of the slop as a plain number is noticed too. The games-check count stays 129 (another executable).

**V3, kept for the owner (the run halted on it before the decision above).** The evidence is below; no V3 file was written.

Evidence (a throwaway probe test, reverted, built in `build/test` of this worktree: `MatchTest` Over menu of the `onetap` fixture, and `HiddenPassTest` after `reachResultUndrawn()` + `render()`, reading `ui().strokeRects`):

| | Double (screen y; canvas y = screen y - 6, the double's origin) | Host (`HostBounds.h`, measured on the simulator, canvas y) |
|---|---|---|
| Over dialog border | stroke 50,272 379x256: rows 272-527, canvas 266 up to 521 | rows 259 up to and including 527 (269 high) |
| Result banner border | stroke 50,682 379x96: canvas top 676 | top 640 (panel 649-780, 132 high) |

Neither agrees, and the cause is structural, not a constant to retune:
- `screen_stubs/components/UiAppHost.h` `RecordingTarget` measures every font as `LINE_HEIGHT` 20 and `GLYPH_WIDTH` 10; its header says "a test pins which text is shown, never where it sits on the real panel". `fui::optionDialogHeight` (so the dialog's height and its centred top) is a function of the real font line heights.
- The double's `resetUi()` does not call `applySharedUiTheme`, so the screens lay out with FreeInkApp's default `ThemeTokens` (banner height `2 * theme.rowHeight` = 96, so rowHeight 48), where the device's `uiThemeTokens(target)` reads the settings and the uiScale fonts (the host's 132 implies rowHeight 66). Neither `UiAppHelpers.h` nor `UITheme.cpp` builds on the host.
- `GfxRenderer`'s double has insets (6, 3, 6, 3), origin (3, 6): odd, "no device has" it (its own comment), so the panel-to-canvas conversion `HostBounds.h` documents (minus 9) does not apply.

A test that "checks the drawn borders against HostBounds.h" would therefore fail today, and making it pass means retuning either side to the double's numbers (which would put the double's layout into the games check) or changing the double's fonts and theme to the device's, which is a rewrite of shared doubles that every screen suite depends on. Both are guesses at what the owner wants.

Options for V3 (taken to the owner; the orchestrator split it out, option 1 first as recommended):
1. Split: build A-5 alone, and leave V3 deferred with this evidence added to its note (`deferred-work.md` `## e8-xr`): the rows can be pinned only by a run on the simulator or a device-faithful screen double. Recommended.
2. Pin the simulator measurement instead: a `sim.sh` script that screenshots the Over menu and a Result banner and compares the border rows with `HostBounds.h` (not a host unit test; needs the display stack, and a new fork script under `docs/crosshatch/fork-scripts.md`).
3. Give the screen doubles the device's font metrics and the shared theme (a separate story: it changes every screen suite's expectations).

## Verification

**Commands (run, results):**
- `cmake --build build/test --target GameMatchHarnessTest && build/test/game_script/harness/GameMatchHarnessTest` (under `flock /tmp/crosshatch-hosttest.lock`, after the review patches) -- 186 tests passed, `CopiedConstantsTest.*` 9 passed.
- Mutations of `InputManager.h` (restored after each; the submodule is clean): `TOUCH_SWIPE_MIN_PX` 61, `TOUCH_SWIPE_MAX_MS` 701, the slop `- 2`, the slop `59`, and a second `TOUCH_SWIPE_MIN_PX` definition each failed the matching pin case only; the literal 700 changed to 701 in the test failed the time-limit case. Run before the review patches; the 61/701/`- 2`/`59` and the literal were run again after them.
- `./bin/clang-format-fix` twice, `git status` -- no formatting change outside the files above.
- `python3 scripts/check_upstream_touches.py` -- PASS.
- `git diff -U0 -- src` filtered for non-comment lines -- none: the `src/` edits are comments only, so firmware is unchanged and no `pio run` or `pio check` was needed.
- Games-check (129) -- no games-check file or the `GamesCheck*` targets touched; not re-run.

## Auto Run Result

**Summary:** The SDK's `TOUCH_SWIPE_MIN_PX` (60), `TOUCH_SWIPE_MAX_MS` (700) and `TOUCH_TAP_RELEASE_SLOP_PX` (`TOUCH_SWIPE_MIN_PX - 1`) are pinned to `InputManager.h` by three new `CopiedConstantsTest` cases (names contain `Pinned`), with a self-test of the slop's text pin; each copy's comment and the doc name them. V3 is not built (split out by the orchestrator; evidence in Design Notes).

**Files:** `test/game_script/harness/CopiedConstantsTest.cpp` (pins, helpers); comments only in `src/games/GameTouch.h`, `src/activities/games/GameMatchActivity.cpp`, `test/game_script/harness/screen_stubs/MappedInputManager.h`, `test/game_script/harness/GameMatchTest.cpp`; `docs/crosshatch/game-canvas.md` (one bullet); this plan.

**Review:** four lenses ran as context-free subagents (`lenses_ran`). Patches: 2 medium, 5 low (rows above, 7 rows). Deferred: 1 (low: the `29`/`28 px` quotes, in the plan's `deferred`). Rejected: the rest, with reasons in the log. `followup_review_recommended: false` (no high patched; 2 medium patched, comment and wording changes).

**Formatting:** `./bin/clang-format-fix` changed nothing outside the files above.

**Residual risks:** the prose copies are held by comments and the failure messages, not by code (the intent's design); the 29/28 px quotes are unpinned.
