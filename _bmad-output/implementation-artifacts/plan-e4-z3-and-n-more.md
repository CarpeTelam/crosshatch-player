---
title: 'Say "and N more" when several packages fail to install (e4-z3, A22)'
type: 'feature'
ticket: ''
created: '2026-09-30'
status: 'built'
baseline_revision: '07a95af23bc0e44d52796c4cb138444d13f77284'
route: 'oneshot'
route_source: 'auto'
review: 'quick'
review_source: 'auto'
lenses_ran: ['quick', 'quick-2']
review_loop_iteration: 1
context:
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-install-and-launcher/epic-install-and-launcher.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** `GamePackageInstaller::Report` keeps the first failure's reason and counts every failure in `failed`, but the launcher's note shows only the first reason. With two or more bad packages in `/games/`, the others become `.chgame.bad` and nothing on screen says so (`deferred-work.md`, `## 4.3` and `## 4.13`). At the entry-14 device run the owner accepted Assumption A22: the note says "and N more".

**Approach:** When `Report.failed > 1`, `GamesLauncherActivity::installInbox` appends a second line "and %u more" (`failed - 1`) to the note through one new English string; the note's popup, dismissal, and once-per-visit behavior stay as they are.

</frozen-after-approval>

## Implementation Notes

Why oneshot: about 25 lines of source in the launcher and one yaml line; the tests are the larger part.

What N counts. `installAll` counts every file that ends a judgement in an error: `if (report.failed < UINT8_MAX) ++report.failed;` runs for every `error != None`, and the `TooManyGames` test after it only decides whether the file uses the per-run cap. So today `Report.failed` counts invalid packages (renamed `.chgame.bad`), card and memory faults (file stays), and packages that wait for room (`TooManyGames`, file stays). N is `failed - 1`: every other package this visit did not install, for any reason. The installer's behavior does not change (`Report::failed` in `GamePackageInstaller.h` gains a comment). `GamePackageInstallerTest` already pins the count: a waiting package gives `failed == 1` (line 944) and `failed == WAITING` (line 1031).

Should `TooManyGames` count? Recommend yes, keep it counted. The first line already explains a `TooManyGames` (STR_GAMES_INSTALL_TOO_MANY_GAMES: "remove one first"), so the note treats it as a not-installed package the person can act on; a person with a full card and one bad file otherwise sees "bad file" and never learns the other package waits. Not counting it would need a second counter in `Report` (the installer's contract) for no gain. The "more" packages' reasons are not named: a waiting package and an invalid one both read "and 1 more".

Saturation. `failed` stops at 255, so at 255 the line reads "and 254 more", which is a floor. Reaching it needs 255 failed files in one visit; the per-run cap is 32 judged files plus any number that wait for room, so it is not a case a card produces in practice. No new string for it.

Plural. The file has no plural convention (`STR_LIBRARY_POSITION: "%d/%d books"`, `STR_SLEEP_TIMER_VALUE_FORMAT: "%u min"`, `STR_NETWORKS_FOUND: "%zu networks found"`); "and %u more" reads the same for one or many, so one key, `STR_GAMES_INSTALL_AND_MORE`, formatted through `snprintf(tr(...))` as the sleep timer does.

Layout. First try: one buffer with "\n" before the more-line. The FreeInkUI popup measures a "\n" as a line break (`layoutText`), but the drawing path on the device and in the simulator (`FreeInkUIGfxRenderer::text`, then upstream `GfxRenderer::wrappedText`) splits on spaces only, so the first simulator screenshot read "...not sourceand 1 more" in a panel one line short. `GfxRenderer.cpp` is an upstream file outside this entry's paths, so the launcher lays the panel out itself: `note` keeps its 128 B and its four lines, a new `noteMore[48]` holds the formatted line, and `render` measures the reason with `measureWrappedText`, adds one line height to the panel and to the popup's bottom padding, draws the panel with `fui::popup` (the same call `Screen::popup` ends in), and draws the more-line with `DrawTarget::text` in the room left at the bottom. With no more-line (one failure, or the remove-failed note) it still calls `Screen::popup`, so that path is byte for byte what it was. The 48-byte local of the first try is gone; `render` adds no local over 256 B.

## Review Triage Log

Both lenses ran as context-free subagents (foreground for the first; the second was started in the background by the harness and returned before the checks finished). Pass 1 reviewed the `\n` version; pass 2 the hand-laid panel.

Pass 1 (quick): 0 high, 0 medium, 1 low, 2 false.
- low, rejected: no test in the installer suite for "a waiting package counts in `failed`" -- false as stated: `GamePackageInstallerTest.cpp` line 944 (`failed == 1`, `TooManyGames`) and line 1031 (`failed == WAITING`) pin it. The test name `TheMoreLineCountsEveryFailure...` overstated a scripted count; renamed `TheMoreLineIsTheFailureCountLessTheFirstEvenWhenTheFirstWaitsForRoom` and its comment names the installer test.
- false: the plan and `deferred-work.md` say the installer is unchanged while `GamePackageInstaller.h` is edited -- true only of the wording; both now say "behavior unchanged, `Report::failed` gains a comment".
- false (not a defect): plan Verification unfilled at review time.

Pass 2 (quick, on the panel layout): 0 high, 2 medium, 4 low.
- medium, patched: the hand-laid panel had no test of position. Added `TheMoreLineIsDrawnUnderTheReasonAndCenteredWithIt` (the more-line's rect starts where the reason's last line ends, and shares its center).
- medium, rejected: `deferred-work.md`'s earlier `## 4.3` and `## 4.13` items are not marked resolved in place -- the brief says to leave the earlier sections as history and record the resolution under `## e4-z3`. The quote there now reads `.cpgame.bad` (today `.chgame.bad`), as the original.
- low, patched: stale comment and name in the longest-reason test (renamed `...IsDrawnWholeAboveTheMoreLine`, comment rewritten); comment placement in `GamesLauncherActivity.h` (`noteMore` now has its own comment); the `contentW > 1` clamp `Screen::popup` applies is copied (`std::max<int16_t>(1, ...)`).
- low, accepted: the panel sizing duplicates `FreeInkApp::popup` and can drift if the SDK changes it; the reason is that `GfxRenderer::wrappedText` (upstream, no ledger row) does not break on `\n`. A future fix in `wrappedText` (honor `\n`) would let this path shrink back to one `screen.popup` call with a `\n`; not deferred as a separate item because `## owner-e4-homes` A21 already opens with "the next ledger change".
- low, rejected: plan unfinished at review time -- filled in now.

Independent review (orchestrator, commit 90ced5df; lenses adversarial, edge-case, verification-gap; table in the scratchpad `e4-z3/review-all.md`). Follow-up commit on top:
- 1 medium, patched: the tests could not catch a `\n` in the note (the recording target lays text out with `layoutText`, which breaks on `\n`; the device does not). The stub now records every raw `text()` call (`textCalls`) and every stroke rect (`strokeRects`); `TheMoreLineIsATextCallOfItsOwnAndNoCallHoldsANewline` asserts the reason is one call, "and 1 more" another, and no call holds a `\n`. The `\n` mutant (append `\n` and the more-line to `note`) now fails three launcher tests, including this one.
- 2 low, patched: the one-failure panel's height, and 4 low, patched: the panel's width and border, in `TheMoreLineAddsOneLineToThePanelAndNothingElse` (panel = lines x 20 + 24 high, widest line + 32 wide, 2 px border, grows by one line and stays centered). Mutants "custom panel for one failure" and "no border/width" fail it.
- 3 low, patched: `noteMore` reset in `confirmRemove` -- `AFailedRemoveAfterASeveralFailureInstallNoteShowsNoMoreLine` in `GameRemoveLauncherTest.cpp`; the mutant without the reset fails it.
- 5 low, patched: the "every package not counted" claim was wrong for files past the 32-per-run cap and names over 62 bytes; the comments in the launcher and `GamePackageInstaller.h`, this plan, and `deferred-work.md` `## e4-z3` now say so. Behavior kept.
- 8 low, patched: the plan's simulator file names now match the screenshot.
- 6, 7, 9 low, recorded in `deferred-work.md` `## e4-z3` (the `TooManyGames`-first wording for the retro; the copied popup sizing; a UTF-8 cut in a future translation).
- 10 low, rejected: 63-byte names in two tests are a margin, not a claim; the tests assert the note is drawn whole.
- M7 and M9 (mutants that no test can tell apart: a fresh activity per visit; the reason is always wider than the more-line): equivalent, no test.

## Verification

All commands ran in this worktree at the final code, under the shared build lock; logs are in the scratchpad `e4-z3/`.

- Host suites: `GamesLauncherHarnessTest` 45 tests passed (39 in `ListTest`, 7 of them new); the installer, hardening, and launcher-related subset 83/83; full `ctest --test-dir build/test -j8`, five runs in a row, 1339/1339 passed each time.
- `scripts/*_test.py`: all 11 exit 0. `check_layers.py`: passed. `check_upstream_touches.py` (`upstream` fetched, complete clone): PASS, `lib/I18n/translations/english.yaml` is ledger row 2.
- `pio run -e x4pro` (the `build on` step of the flash gate) exit 0; `pio run -e default` exit 0; `sim.sh build x4pro` exit 0.
- `check_flash_budget.py`, four steps, exit 0 each. Games on 5,910,736 B, games off 5,678,992 B: +231,744 B flash (limit 256,000 B at `--limit-kib 250`). Static internal RAM: +784 B (on 187,848 B, off 187,064 B; limit 1,024 B). Objects: 43 game objects, largest mutable static 4 B (`lastOpened`, existing), no static initializer. Both figures are games-on minus games-off from the two builds at this one commit, the orchestrator's method; the last recorded measurement was +231,408 B and +784 B, so this change costs about +336 B flash and no static RAM by that comparison (a comparison with a recorded figure, not a re-measured base). The brief's bars are +240,496 B and +808 B.
- Simulator: two invalid packages in `fs_/games/` (`binary-lua-stored.chgame` and `name-uppercase.chgame` from `gen_hardening_packages.py`, copied to `fs_/games/` as `first-bad.chgame` and `second-bad.chgame`); `sim.sh setup`, `build x4pro`, `start`, tap Games, `ss`. Both became `.chgame.bad`; the log shows both reasons; the note reads "first-bad.chgame: A Lua file is compiled, not source" and, on a line of its own, "and 1 more". Screenshot: `_bmad-output/initiative-crosshatch-player-v1/epic-install-and-launcher/story-and-n-more-screenshots/two-bad-packages-and-1-more.png`. (The first screenshot, of the `\n` version, showed "sourceand 1 more" in a short panel; that is why the layout changed.)
- `./bin/clang-format-fix` twice: no change either time.

Follow-up commit (independent review fixes), at its code, same method and lock:
- Host: `ctest -j8` three runs, 1342/1342 each; mutants run against the launcher and remove suites: the `\n` note, a custom panel for one failure, and a missing `noteMore` reset each fail at least one new test; the unmutated tree passes.
- `scripts/*_test.py` 11/11, `check_layers.py` passed, `check_upstream_touches.py` PASS.
- `pio run -e x4pro` (flash gate `build on`), `build off`, `compare`, `objects`, `sim.sh build x4pro`: exit 0 each. Games on 5,910,736 B, off 5,678,992 B: +231,744 B flash; static RAM +784 B (187,848 B on, 187,064 B off). Identical to the first commit: this commit changes only a comment in `src/`.
- `./bin/clang-format-fix` twice, no change.

Assumption for entry 14: with two invalid packages in `/games/`, the note shows the first reason and "and 1 more".
