---
title: 'e5-r3 settings read and reserved image log'
type: 'bugfix'
ticket: ''
created: '2026-10-03'
status: 'built'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
baseline_revision: '77bbd87b'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** (F8) When the title screen cannot read the manifest's settings (OOM, card fault, manifest changed), `GameModeActivity::startMatch` refuses Continue as well as New, though a resume runs no setup (AD-8), and shows no on-screen reason. (F11) A package with an image named `title` or `handoff`, valid before those names were reserved, is skipped by the asset loader without a log line, so a later `ch.gfx.image('title')` fails as not found with no cause in the log.

**Approach:** Continue starts the saved match with unread settings; the match ends in its error view at Play again, which would run setup without `ctx.settings`. New game starts nothing and says why on its row (new key `STR_GAMES_SETTINGS_NOT_READ`). The loader logs each reserved image except icon.bmp (log only).

## Boundaries & Constraints

**Always:** fail closed: no round runs with a missing `ctx.settings`; `tr()` for new text, key in english.yaml only; log severity as the neighbouring lines.

**Never:** change install or pack behaviour, add an api-level entry, retry the manifest read at the tap (deferred-work ## 5.12), touch Manifest.cpp, GameVM, SoloRounds, GameSaveStore, docs/ or the epic folder.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected | Error handling |
|---|---|---|---|
| Continue, settings unread | save + manifest read fails | saved match starts, no setup | Play again: error view, log names cause |
| New, settings unread (with or without a save) | same | nothing starts, no replace question, row line is the notice | log line kept |
| New / Continue, settings read | normal | unchanged | n/a |
| title.bmp / handoff.bmp / TITLE.BMP in folder | load | skipped, one info line each | none |
| icon.bmp | load | skipped, no line | none |

</frozen-after-approval>

## Code Map

- `src/activities/games/GameModeActivity.cpp/.h` -- `startMatch` guard now `!resume`; `activateIndex` NewGame checks before the confirm; new `showSettingsNotice()` sets `settingsNotice` and rebuilds rows (RenderLock); `buildRows` shows the notice as New game's subtitle.
- `src/activities/games/GameMatchActivity.cpp/.h` -- `handle()`: PlayAgain from Over with `settings.count != manifest.settingsCount` goes to `fail()` before any round bookkeeping.
- `src/games/GameAssets.cpp` -- `scanFolder`: the reserved-image branch logs (INF) except `icon`.
- `lib/I18n/translations/english.yaml` -- `STR_GAMES_SETTINGS_NOT_READ`.
- Tests: `ModePickerTest.cpp` (two cases rewritten), `GameMatchTest.cpp` (new case; one `logHas("title.bmp")` narrowed), `GameAssetsLoadTest.cpp` (reserved case rewritten).
- Not changed: `GameImages.cpp/.h` (`isReservedImage` already says what is reserved).

## Tasks & Acceptance

**Execution:**
- [x] Mode screen, Match activity, assets loader, yaml, tests as the Code Map says.
- [x] deferred-work.md entries under `## e5-r3`.

**Acceptance Criteria:**
- Given unread settings and a save, when Continue is tapped, then the match starts, `setup` does not run and resume.bin is unchanged.
- Given the same and the match reaches Over, when Play again is chosen, then the match is in Error showing `STR_GAMES_START_FAILED` and `STR_GAMES_SETTINGS_NOT_READ`, and no round begins.
- Given unread settings, when New game is tapped, then no match starts, no replace question opens, and New game's line is the notice.
- Given a folder with title.bmp, handoff.bmp, TITLE.BMP and icon.bmp, when it loads, then three info lines name the game folder and file, none for icon.bmp, and no image of those names loads.

## Implementation Notes

Assumption for entry 11: e5-r3 widened its touches to GameMatchActivity (orchestrator, 2026-10-03). Widened files: `GameMatchActivity.cpp/.h`, `GameMatchTest.cpp`.

## Plan Change Log

## Review Triage Log

Pass 1. The four lenses ran as context-free subagents and all returned (blind-hunter 9 findings, edge-case-hunter 4, verification-gap 1 plus a note, intent-alignment descriptive).

| Finding | Verdict | Route | Evidence |
|---|---|---|---|
| Edge: a Continue whose save vanished before the match loads starts a new round with empty settings (`seedResume` None path) | medium | patch | Real: `seedResume` returns true with an empty snapshot; setup then runs. Fixed in `onEnter` (fail closed before `GameVM::create`) and pinned by `AContinueWithNoSaveAndTheManifestsSettingsUnreadStopsBeforeSetup`. |
| Edge: New branch of `activateIndex` skips `app.clearTapFlash()` | low | patch | Direct one-line fix in `showSettingsNotice`. |
| Verification gap: ModePickerTest comment names a test that does not exist | low | patch | Comment now names the added test. |
| Blind: grammar of the GameMatchActivity comment | low | patch | Reworded. |
| Edge: reserved branch has no `fits` check | false | none | `isReservedImage` matches only 4 or 7 byte stems, so a name that filled the buffer (48) cannot reach it. |
| Blind: `strings.h` missing for `strncasecmp` | false | none | Already included at the top of GameAssets.cpp (used by `looksLikeLua`). |
| Blind: log on every scan is noise for a package's own title/handoff | low | none | Known and chosen (Design Notes): INF, not ERR; the loader cannot tell them apart; owner scope is log-only. Recorded in deferred-work. |
| Blind: `settingsNotice` never cleared, hides the mode line | low | defer | Deferred-work entry already written (no retry at the tap, ## 5.12). |
| Blind: possible RenderLock deadlock in `showSettingsNotice` | false | none | Called on the loop task from `activateIndex`, as `onOptionsClosed` already takes the lock there; the destructor pitfall does not apply. |
| Blind: the settings predicate is repeated | low | none | Three one-line comparisons in two classes; a shared helper would add public surface. The logs differ in text. |
| Blind: only Play again is guarded | false | none | Over -> PlayAgain is the only event that starts a round (`startNextRound` is called only for it); the seedResume path is the second, patched above. |
| Blind: weak assertions (`titles.bmp`, narrowed `title.bmp`) | low | none | The `titles.bmp` check pins that a real image is not reported; the narrowing keeps the old meaning (the page-read line). |
| Blind: docs/AD amendments not updated | low | none | docs/ and the epic folder are outside this build's touches. |
| Intent: notice appears after the tap, not on open (R1/R2); no end-to-end test from title to Play again | low | none | The intent says Continue and New, "says why" on New's row; R1 matches the orchestrator's brief (notice for New). The two halves are tested on each side; the end-to-end chain needs a VM drive to Over from the title screen, which the rewritten test's `pumpMatchTo` does not reach. |

## Design Notes

- Play-again refusal: chosen over hiding the option (that would need `MatchLifecycle::menuFor`, in lib/GameCore, outside touches, and a new constructor flag): one check in `handle()` that needs no new parameter, because `manifest.settingsCount` and `settings.count` are already members. Fails before `roundsStartedAwaited` and the lifecycle step, so nothing is half-started; `fail()` is allowed from Over. The save was already deleted at Over, so no snapshot is lost.
- Guards kept in `startMatch` (git log -L not needed, nothing moved): the `!resume` guard still blocks New; the OOM and prefs write paths are untouched. `rememberChoices` still skips on unread settings. The notice shows before the replace question so the player is not asked to replace a save for a New game that cannot start.
- F11 severity: INF, not ERR, because a package's own valid title.bmp/handoff.bmp (the runtime's pages) trigger the same line on every load; the loader cannot tell them from a pre-reservation image. "First time" is once per file per load.
- Test double note: the screen doubles record `text()` calls only; the notice is asserted as New game's second line (`lineUnder`). Test red proof: the old Continue test asserted `asks.replaced == 0`, the old behaviour; the rewrite asserts the opposite and could not pass on the base.

## Verification

**Commands (all on this tree, locks as AGENTS.md says):**
- Host tests: `cmake -S test -B build/test -G Ninja ... && cmake --build build/test && ctest --test-dir build/test -j` -- 1624 of 1624 passed (after the review patches and the final wording).
- `./bin/clang-format-fix` twice, `git status` clean; `python3 scripts/check_upstream_touches.py` -- PASS (no upstream file is touched).
- Review lenses (blind-hunter, edge-case-hunter, verification-gap, intent-alignment) ran as context-free subagents and all returned; see the Triage Log.
- `pio run -e x4pro` (the `build on` step of check_flash_budget.py) and `pio run -e default` -- SUCCESS. `pio check` with all three defect levels, default and `-e x4pro` -- "No defects found".
- Flash and static internal RAM, check_flash_budget.py's four steps (`build on`, `build off`, `compare`, `objects`), run on the base commit 77bbd87b (tree stashed clean) and on this tree the same way: games-on minus games-off firmware.bin, base +254,192 B, this tree +254,480 B, so +288 B; static internal RAM (DRAM + IRAM) delta +784 B on both, so +0 B. The tree measured is the final one (the notice text was shortened after the screenshot's first look). Both pass the 276,480 B and 1,024 B limits; `objects` passes (46 game objects).
- Red proof: no commit holds the old code beside the new tests. The base's tests asserted the old behaviour (`Continue` replaced 0 activities, no notice); the rewritten ones assert the opposite and could not pass on it.

**Screenshots:** `_bmad-output/initiative-crosshatch-player-v1/epic-pass-and-play/story-settings-notice-screenshots/new-game-settings-not-read.png` -- the pass-art title screen in the x4pro simulator after its manifest.json was removed (so the settings could not be read) and New game was tapped: New game's second line reads "Settings not read. Reopen the game." (the line fits on one row).
