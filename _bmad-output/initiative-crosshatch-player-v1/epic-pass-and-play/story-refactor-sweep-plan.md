---
title: 'Refactor sweep'
type: 'refactor'
ticket: '10'
created: '2026-10-01'
status: 'built'
route: 'full'
route_source: 'auto'
baseline_revision: 'd2b5cd38ae76fef3230afc5a683a9bd3775f6d0e'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/.skills/refactor-for-review/SKILL.md'
  - '{project-root}/.skills/control-flow-clarity/SKILL.md'
  - '{project-root}/docs/contributing/touch-and-ui.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Entries 1–9 left cleanup addressed to the sweep (R15): a title-screen confirm copied from the launcher's remove dialog, a tap-time solo-only `peek` that entry 9 made redundant, an unused `STR_GAMES_MODE_TITLE`, stale interim comments and `formats.md` text, the launcher's log calling an Invalid game "Unavailable", and `pass-hidden` not installed by the fixture test; plus `deferred-work.md` items this epic met that need a disposition.

**Approach:** Fix each item that is cleanup inside `touches` with no visible behaviour change; record every item this epic met under `## 5.10` as fixed, documented, or deferred with a reason the owner confirms at entry 11.

## Boundaries & Constraints

**Always:** refactors keep behaviour (every host test's assertion kept except the log lines and the removed key named below); the shared confirm keeps both dialogs' texts, focus rules, keys, and layout (pinned by the existing suites and a before/after `sim.sh ss` pixel diff); locals under 256 B, no new statics or strings, `LOG_*` only; flash and static RAM not above the combined tree's (+8,688 B, +0 B over `c1902721`); every guard in the functions changed kept (Design Notes).

**Never:** a new file (outside `touches`); the spine, `src/games/GamePackageInstaller.*`, any upstream file but `english.yaml`, the fixtures' `.lua`/manifests, `.github/**`, the epic file; a change to what any screen shows; `RenderLock` in a destructor.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Continue, two-mode game, solo save | Valid solo save | resumes solo (the match adopts the save's roster); no second `peek` read | — |
| Continue, pass save | pass save, two-mode or pass-only | resumes pass, as today | — |
| Continue, Unreadable | read fault | error view, file kept, as today | — |
| Continue, pass-capable game, no pass seats | one-seat host double | today's "Cannot continue ... in pass" unless the game also starts solo, then a solo-roster Continue | logged |
| New over a save / Remove | the two dialogs | same texts, Cancel focused, Back/Up/Down/Confirm/tap as today | — |
| Launcher, Invalid manifest | pass game, `seats.max` 1 | log `Invalid <id>: <reason>`; Unavailable ones keep `Unavailable` | — |

</frozen-after-approval>

## Code Map

- `src/activities/games/GameModeActivity.{h,cpp}` -- `buildConfirmDialog` :307, `handleConfirmInput` :258, `onConfirmChoice` :284 are copies of the launcher's. Add, before the class in the header, `GameConfirmDialog` (Design Notes) with `focus`, the `OptionDialogProps` buffer, `readButtons`, `answerTap`, `build`; the class holds one (`confirm`) in place of `confirmFocus`/`dialogProps`. `startResume` :181: drop the solo-only `peek` and its comment (Design Notes). Class comment :15–30: no tap-time peek, no "until entry 9".
- `src/activities/games/GamesLauncherActivity.{h,cpp}` -- `buildRemoveDialog` :550, `handleRemoveInput` :246, `onRemoveChoice` :272 use the shared helper (`removeFocus`/`dialogProps` go); the header includes `GameModeActivity.h`. `loadGames` :346 logs `Invalid` for `CheckStatus::Invalid`.
- `lib/I18n/translations/english.yaml` :563 -- drop `STR_GAMES_MODE_TITLE` (only `english.yaml` has it).
- `test/game_script/harness/ModePickerTest.cpp` -- :505 drop the `STR_GAMES_MODE_TITLE` line; :660, :675, :693, :718, :739, :759 Continue log lines (Design Notes); :405 `Invalid counter: ...`.
- `test/game_script/harness/GamePackageInstallerTest.cpp` :639 -- add `pass-hidden`; each fixture expects the mode it starts (solo, pass for `pass-hidden`); rename to `...InstallAndCanStart`.
- `docs/crosshatch/formats.md` :176–220 -- callers paragraph (title screen: new `peek` on open; match: new `loadResume`, again widened on refusal; older forms: no firmware caller), `Valid`/`None`/`Unreadable` bullets (title screen, not the launcher), `None` exception (`savedForAnotherHost`), Back goes to Games, a New row replaces the save.
- Comments: `src/games/GameSaveStore.cpp` :61, :363; `test/game_script/GameSaveStoreTest.cpp` :608, :661, :995; `test/game_script/harness/MatchSupport.h` :232–242.
- `_bmad-output/implementation-artifacts/deferred-work.md` -- `## 5.10` at the end (Design Notes' dispositions).

## Tasks & Acceptance

**Execution:**
- [x] Baseline `sim.sh ss` of both dialogs (focus on Cancel, then on the action) before any edit, under the build lock
- [x] `GameModeActivity.{h,cpp}`, `GamesLauncherActivity.{h,cpp}` -- shared confirm; `startResume`; comments; launcher log
- [x] `english.yaml`, `ModePickerTest.cpp`, `GamePackageInstallerTest.cpp` -- as in Code Map
- [x] `formats.md`, `GameSaveStore.cpp`, `GameSaveStoreTest.cpp`, `MatchSupport.h` -- text only
- [x] `deferred-work.md` -- `## 5.10`

**Acceptance Criteria:**
- Given the host suites, when run, then all pass with the changes above and no other assertion changed.
- Given the same `sim.sh script` before and after, when both dialogs are shot in both focus states, then the PNGs are pixel-identical (`compare -metric AE` 0).
- Given `check_flash_budget.py`'s four steps, then flash and static RAM stay within 11,152 B and 32 B over `c1902721`, and not above the combined tree's +8,688 B and +0 B.

## Implementation Notes

- Baseline dialog shots (this session, before any edit, at `d2b5cd38`): `sim.sh build x4pro` under the build lock, then `/tmp/claude-0/-home-user-crosshatch-player/3c29fff2-79eb-5b74-aad8-2397ce34acb1/scratchpad/5.10/dialog-shots.sh base` (restores a card copy holding the `counter` package and a solo save, then shoots Counter's title screen, the New-over-a-save question with Cancel and with New game focused, it closed, the launcher's remove question in both focus states, and it closed) into `build/sim/shots/base-*.png`. A second run (`base2`) matched every shot pixel for pixel (`compare -metric AE` 0), so the shots are deterministic. The after-run uses the same script after the firmware builds.

- Implemented by a context-free implementation subagent from this plan. Beyond the Code Map: one more stale "launcher" comment in `GameSaveStore.cpp` (about :378), and one more Continue log check in `ModePickerTest.cpp` (about :1368). `AContinueThatCannotFitAPassMatchStartsNothing` is re-based on a pass-only 1..2 game with a pass save on the one-seat host, since a two-mode game there now continues with a solo roster (the I/O matrix's row); its assertions are unchanged. The new `AContinueWithNoPassSeatsOfAGameThatStartsSoloPassesTheSoloRoster` pins that row: the match ends in the error view and the file is kept.
- This session: `test/game_script/fixtures/README.md` (changed by entries 1 and 4, so in `touches`) said twice "A pass match writes no `resume.bin`", wrong since entry 9; reworded. Re-ran here: host suites 1,498 of 1,498 (Ninja, host-test lock); every `scripts/*_test.py`, `check_layers.py` (487 edges), `check_upstream_touches.py` pass; `./bin/clang-format-fix` changed nothing.
- `GamesLauncherActivity.cpp` keeps its own `#include "GameModeActivity.h"` (it pushes the title screen), now also reached through its header.

## Plan Change Log

## Review Triage Log

Pass 1 (2026-10-01). The four lenses ran as context-free subagents, launched together in one message in the foreground (each told the worktree is read-only); all four ran in the background despite the flag, and this session waited for all four before triage. All four returned: blind-hunter (BH), edge-case-hunter (EC), verification-gap (VG), intent-alignment (IA). Verdicts: medium 1, low 16, false 1, plus IA's descriptive report. No intent_gap or bad_plan. The patches were applied in this session, not by the step-03 implementer: its earlier hand-back was routed to the orchestrator, not to this session, so re-engaging it risked a second lost report.

| # | Lens | Finding | Verdict | Route | Evidence / action |
|---|------|---------|---------|-------|-------------------|
| 1 | VG1, BH3 | No title-screen test plays the roster Continue passes (a save gone bad after the peek); dropping `roster = pass(seats)` passed every test | medium | patch | `TitleScreenTest.ATwoModeGamesSaveThatWentBadContinuesAsANewPassMatch` (first save mode 1, n 2); the mutation fails it |
| 2 | VG2 | Left/Right in the shared `readButtons` pressed by no test | low | patch | `TheDirectionKeysMoveTheFocusAndConfirmOnNewGameStartsNew` presses Right, Left (Confirm closes), then Down, Up, Right, Confirm; dropping Left fails it |
| 3 | VG other, EC1, BH4, IA b | `EXPECT_FALSE(logHas("pass match"))` in `ATapOnContinueResumesATwoModeGamesSoloSaveSolo` can never fail | low | patch | Now asserts the pass roster Continue passes and that no pass roster was resumed |
| 4 | BH5 | The Continue log dropped the seat count | low | patch | `Continue <id>: a <mode> roster of <n> seat(s) unless the save says otherwise` (`modeName`, `roster.seats`) |
| 5 | BH2 | `startResume`'s and the class comment omit the no-pass-seats branches and say "only when the save has gone" | low | patch | Both reworded: a load that finds no usable save; solo fallback; pass-only guard |
| 6 | BH1 | `GameMatchActivity::seedResume` still says "the launcher offered Continue" | low | patch | "the title screen"; listed in `## 5.10`'s stale-text line (`GameMatchActivity.cpp` is a file entries 1, 4, 6, 9 changed) |
| 7 | BH6 | `GameSaveStore.h` does not say the older forms are host-only | low | patch | One line on the class comment |
| 8 | BH8 | `GameConfirmDialog::props` is public and keeps `options` pointing into `build`'s frame | low | patch | `props` private, with its lifetime comment (the pointer pattern is the two copies' own, unchanged) |
| 9 | BH9 | The launcher's Invalid/Unavailable ternary is not exhaustive | low | patch | `switch` over `CheckStatus`, `Ok` continues |
| 10 | BH12b | Two rewrapped `formats.md` lines exceed 120 columns | low | patch | Rewrapped (the section's two other long lines are the base's) |
| 11 | BH13 | The installer fixture test special-cases `pass-hidden` by name; `pass-open`'s pass is unchecked; `## 5.10` overstates | low | patch | A `{id, modes}` table; each fixture must start every declared mode; `## 5.10` text matches |
| 12 | BH7 | `GameConfirmDialog` in `GameModeActivity.h` with no recorded follow-up | low | defer | Moving it needs a new file (outside `touches`): `## 5.10`, trigger the next story that may add a file there |
| 13 | IA d | Open items in the referenced headings had no `## 5.10` line (`## e5-inception`, `## 4.1`'s first item, the rest of `## 4.13`, `## 5.8`'s no-retry) | low | patch | One `## 5.10` line lists each with its unchanged trigger |
| 14 | IA f, IA g | The gap-pause-menu deferral routes to entry 3, not entry 11; the README fix is not under `## 5.10` | low | patch | The line says the owner confirms at entry 11; the README is in the stale-text line |
| 15 | EC2, IA a | The Code Map does not list the re-based `AContinueThatCannotFitAPassMatchStartsNothing` or the :1368 line; a host-double behaviour changed | low | reject | Fix is a plan edit; Implementation Notes already record both, and the frozen matrix's fourth row is that change (device-unreachable: `MAX_SEATS` 2) |
| 16 | BH10, IA c | `## 5.10` cites before/after shots the plan does not yet record | low | reject | Not a defect of the code: Verification below records the after-run (`compare -metric AE` 0) before the commit |
| 17 | BH11 | The shot script's path is session scratch | low | reject | Plan-only fix; Verification now spells out the script's steps |
| 18 | BH12a | The plan's two flash bars | false | reject | Not contradictory: the epic's bar (R12) and this entry's own "no flash added"; plan-only |
| 19 | IA e | Older store forms and `SoloRounds` are deferred though their files are in `touches` | -- | descriptive | Reasons stated (test churn; the spine names `SoloRounds`), for the owner at entry 11 |

## Design Notes

**Shared confirm** (in `GameModeActivity.h`: the launcher already includes it to push the title screen, and a file of its own is outside `touches`). `struct GameConfirmDialog { enum class Answer : uint8_t { None, Repaint, Cancel, Confirm }; uint8_t focus; OptionDialogProps props; Answer readButtons(const MappedInputManager&); Answer answerTap(const ActionEvent&); void build(UiScreen&, const GfxRenderer&, ActionId, title, headline, message, actionLabel); }`. Each screen keeps its touch routing (`routeTouch`, then `invalidated()` before the `if (route) return true`, order unchanged), its own open/close state, and an exhaustive `switch` over `Answer` (Repaint → `requestUpdate`, Cancel → close, Confirm → its action). Guards kept: `onConfirmChoice`/`onRemoveChoice` return when the dialog is closed (a tap from a stale table); `build` reads the row/index once (the input task may close it mid-render) and returns when it is out of range; `confirmNew` closes before starting (a failed start repaints the list); `confirmRemove`'s range check.

**`startResume`.** The solo-only `peek` chose solo only for a Valid solo save; since entry 9 `seedResume` plays the save's roster whatever the caller passed, and the caller's roster matters only when the save has gone by the time the match reads it (a new match). New rule: a game that starts pass passes `pass(passSeats)`, else solo; with no pass seats, solo when the game starts solo, else today's "Cannot continue" guard. Same outcome as today for every save `peek` can report on a device (`MAX_SEATS` 2: a game `check` leaves pass for has pass seats). Log: `Continue <id>: a <mode> roster unless the save says otherwise`; tests read the match's `resuming the save's roster: ...` line instead of the peek suffix. The older `peek`/`loadResume` forms then have no firmware caller; the host suites keep them.

**Dispositions (`## 5.10`).** Fixed: `## 5.7` confirm copy, `STR_GAMES_MODE_TITLE`, `startResume` peek, `formats.md` callers; `## 5.9` None exception, interim comments; `## 5.8` stale text, `pass-hidden` (with `## 5.4`, `## 5.7`); `## 5.2` log. Documented: `## 4.13`/`## owner-e4-homes` launcher `peek` time → R14's title-screen `peek` time (entry 11). Deferred: `SoloRounds` rename and AD-21 (the spine names `SoloRounds`, outside `touches`; next spine edit); `## 5.4` gap pause menu over the last frame (changes solo's view: owner, entry 3); `## 5.6` half refresh (entry 11); `## 4.4`/`## 4.1` drift guards, interleaving, `notLoaded`, task-vs-core (need `src` seams or a device; layout pinned here by a one-off `sim.sh ss` diff, a standing pixel gate is new CI); `## e4-y` counter comment (outside `touches`, the line is in ten fixtures) and hash check (trigger unchanged); older store forms (test churn); R10 and AI-12 (inception Decisions).

## Verification

**Commands:**
- host suites (AGENTS.md CMake/ctest, host-test lock) -- all pass
- `python3 scripts/*_test.py` each, `python3 scripts/check_layers.py`, `python3 scripts/check_upstream_touches.py` -- exit 0
- `./bin/clang-format-fix` twice -- nothing new
- under the build lock, after review: `pio run -e x4pro`, `pio run -e default`, `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` (default and `-e x4pro`), `sim.sh build x4pro`, `check_flash_budget.py` `build on`, `build off`, `compare --limit-kib 250 --ram-limit-bytes 1024`, `objects` -- succeed, within the bar
- `sim.sh script` dialog shots before/after -- `compare -metric AE` 0

**Results (2026-10-01, on the patched tree that is this entry's commit; one round after the review):**
- Host suites: 1,499 of 1,499 (Ninja, host-test lock), after the review patches and the last format run. Mutations: dropping `roster = pass(seats)` in `startResume` fails `ATwoModeGamesSaveThatWentBadContinuesAsANewPassMatch`; dropping Left from `readButtons` fails `TheDirectionKeysMoveTheFocusAndConfirmOnNewGameStartsNew`.
- Every `scripts/*_test.py` exit 0; `check_layers.py` passed (487 edges in 108 game files); `check_upstream_touches.py` PASS (`english.yaml` is ledger row 2's); `./bin/clang-format-fix` twice, nothing new.
- Build lock, `PLATFORMIO_BUILD_CACHE_DIR` the main checkout's `.cache`: `pio run -e x4pro` SUCCESS; `pio run -e default` SUCCESS; `pio check` default PASSED and `-e x4pro` PASSED ("No defects found"); `sim.sh build x4pro` exit 0.
- `check_flash_budget.py`'s four steps, exit 0 each: x4pro `firmware.bin` 5,919,168 B games on, 5,679,632 B off, +239,536 B (16,464 B under the 256,000 B gate); static internal RAM +784 B (`.dram0.bss` +16, `.iram0.text` +684, `.iram0.text_end` +84; 240 B under the gate); `objects` clean (43 objects, largest mutable static 4 B). Over the base at `c1902721` (+233,440 B, +784 B, measured the same way): +6,096 B flash and +0 B static RAM, within 11,152 B and 32 B. Against the combined tree at `02e3531d` (+242,128 B, +784 B): -2,592 B flash, +0 B static RAM.
- Dialog shots: the script restores a copy of the simulator card holding the `counter` package (packed with `pack_game.py`) and one solo save of it (made by one tap and Leave), then in a live `sim.sh start x4pro` session with `SIM_SETTLE=3`: `tap 240 562` (Games), `tap 240 138` (Counter's title screen), `ss`, `tap 240 240` (Solo under Continue: the question), `ss`, `key down`, `ss`, `key back`, `ss`, `key back` (the launcher), `hold 240 138 1500` (the remove question), `ss`, `key down`, `ss`, `key back`, `ss`. Before (`base`, at `d2b5cd38`, twice) and after (`after`): all seven shots `compare -metric AE` 0.
- Screenshots (`story-sweep-screenshots/`, the after-run, identical to the baseline): `new-over-save-cancel-focused.png` (the title screen's question, Cancel focused), `new-over-save-new-game-focused.png` (New game focused), `remove-cancel-focused.png` (the launcher's remove question, Cancel focused), `remove-remove-focused.png` (Remove focused).
