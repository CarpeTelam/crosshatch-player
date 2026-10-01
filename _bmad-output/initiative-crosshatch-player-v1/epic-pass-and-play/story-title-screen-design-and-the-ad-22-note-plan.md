---
title: 'Title screen design and the AD-22 note'
type: 'design'
ticket: '3'
created: '2026-10-01'
status: in-review
route: 'bmad-ux'
context:
  - '{project-root}/docs/contributing/touch-and-ui.md'
  - '{project-root}/docs/crosshatch/game-canvas.md'
---

## Intent

**Problem:** Entries 7, 8, and 4 laid out the title screen, the one-row launcher, and the hidden hand-off from the epic's Requirements and AD-22 as it stood (the owner's "entry 3 moves after the prototype" Decision). The owner had not yet reviewed those layouts, and AD-22 and SPEC CAP-4 still described a mode step and launcher Continue rows.

**Approach:** The owner and a `bmad-ux` session reviewed the built screens from their screenshots, topic by topic. The session recorded each decision in the UX workspace's memlog and distilled them into `DESIGN.md` and `EXPERIENCE.md`, with key-screen mocks. It then amended the spine and SPEC CAP-4 to match, and lists here each change the owner asked for against what is built. The changes are built later, as their own story in this epic's PR.

**Touches:** the UX design files under `_bmad-output/planning-artifacts/ux-designs/ux-crosshatch-player-2026-10-01/`; the spine's AD-22 and Capability map, plus AD-8, AD-12, AD-15, and AD-17, which the owner asked this session to amend as well (below); SPEC CAP-4's success line; this plan. **Stays out:** `src/**`, `lib/**`, `test/**`, the epic file, `tickets.toml`.

## The owner's approval

APPROVAL-PENDING

## Design files

All under `_bmad-output/planning-artifacts/ux-designs/ux-crosshatch-player-2026-10-01/`:

- `DESIGN.md`: how the games screens look. Its tokens and component specs are taken from the built screenshots, with the owner's changes.
- `EXPERIENCE.md`: how they behave: the screen map, strings, states, interactions, key flows, developer overrides, and a built-versus-designed table.
- `mockups/key-title-screen.html`, `mockups/key-options.html`, `mockups/key-hand-off.html`, `mockups/key-launcher.html`, `mockups/key-gap-pause.html`: the key screens as designed. The spines win over any mock.
- `.memlog.md`: the session's decision log, owner decisions and working assumptions, in order.

## Decisions (owner, 2026-10-01, in this session)

1. **The title screen** is a splash screen for the game, not a plain list:
   - **Splash graphic:** above the menu. By default it is the game's icon drawn large; a developer can ship their own (`title.png`).
   - **Menu:** Continue (only with a save; first and selected), New game, and Options.
   - **New game** starts the game's current mode: the mode last started, or solo unless the developer sets another default (`default_mode`).
   - **Options** holds the mode and the game's own settings (AI level and so on); a tap on a setting cycles its value.
2. **New over a save** keeps the built confirm question, its wording, and its mode line: players should know they are starting a new game over the previous save.
3. **An unreadable save** is reported only when Continue is tapped, as built, error view wording included.
4. **Launcher row:**
   - The second line names the game's modes ("Solo · Pass and play"); an unavailable game keeps its reason there.
   - The selection after Leave stays as built (the game's own row, on the page holding it).
5. **The Result banner** stays as built: "Tap to pass to player N", about 4/5 of the screen width, near the bottom.
6. **The hidden hand-off screen** gets a design:
   - **Default:** "Player X, tap when ready". The session's design keeps the `eye-closed` icon above the text; this is an assumption the approval below covers.
   - **Developer override:** a full-page `handoff.png`, which the runtime draws with its own text box at the manifest's `handoff_text` position. A script never draws during the hand-off, so AD-12 and CAP-6 hold.
   - **Sleep:** the forced-exit (sleep) blank stays the icon alone (the session's assumption, stated to the owner, not objected to).
7. **The Play-again gap's pause menu** stays over the last round's frame, with "Starting the next round" centred.
8. **Scope:** everything above is built in this epic (the owner chose this over moving the API parts to a later epic).
9. **Budget:** the follow-up story measures, and a measurement over this epic's share stops the run for the owner, who reallocates then.
10. **Spine:** this session amends AD-8, AD-12, AD-15, and AD-17 as well as AD-22 and the Capability map, superseding the brief's "touch only AD-22".

## Spine and SPEC amendments

All dated "Amended 2026-10-01 (owner, epic-pass-and-play entry 3, the title-screen UX session)":

- **AD-22:** the game's row opens its title screen, a start there is the third tap, and Continue is offered first there. The bullet also covers:
  - the launcher's one row per game with its modes line, and the selection after Leave;
  - the title screen's splash, Continue, New game in the current mode, and Options;
  - the confirm question for New over a save (its fourth tap);
  - the tap counts.
- **AD-8:** `ctx.settings`, the chosen setting values, fixed for the match.
- **AD-12:** the runtime draws the whole hand-off screen: `handoff.png` or the `eye-closed` icon, plus "Player N, tap when ready". The sleep blank stays the icon alone.
- **AD-15:**
  - two reserved, runtime-drawn package images: `title.png` (at most 480 × 360) and `handoff.png` (at most 480 × 800);
  - three manifest keys: `default_mode`, `settings` (at most 4 settings of 2 to 6 values), and `handoff_text`;
  - the registry keeps only whether a game has settings;
  - all of these are level-1 preview entries.
- **AD-17:** `/.games-data/<id>/prefs.bin`, holding the last mode started and the chosen settings. `GameSaveStore` writes it on the loop task, and a missing or stale file falls back to the manifest's defaults.
- **Capability map:** a row for the per-game title screen and Options.
- **SPEC CAP-4 success:** "From Home, a game starts in at most 3 taps: Games, the game, and Continue or New game on its title screen, which starts the game's current mode (the mode last started, or the developer's default; another mode is picked in Options first). "Continue" is offered first when a save exists. The list pages past one screen."
  - "Offered first" replaces "listed first", as the brief asked.
  - The 3-tap clause changes as well. With a remembered mode, the 3 taps hold for the game's current mode; a mode the player has not used yet goes through Options first.

Not amended, for the orchestrator: the epic's R9 (one New start per mode) and R16 (as written, still true), and the R4/R5 text calling the hand-off screen "blank". Under this design the screen carries runtime text, and a developer's image, but still no script drawing. The game-api-seed companion's `ctx` and manifest sections also need the new fields when the follow-up story documents them.

## Changes for a follow-up story

Each change against what is built (entries 4, 7, 8, 9, and 10, merged at `38a8b75d`):

| # | Change | Screen | File or area | Screenshot it changes |
|---|--------|--------|--------------|-----------------------|
| 1 | Splash area above the menu: `title.png` centred (at most 480 × 360), else the game's icon at 128 px | Title screen | `GameModeActivity` (and its icon drawing, shared with `GameRowIcon`) | `story-title-screen-screenshots/title-*.png`, `story-one-row-screenshots/confirm-opens-title-continue-first.png`, `story-continue-pass-screenshots/*-title-continue.png` |
| 2 | Menu becomes Continue (with a save), New game, Options, replacing one New row per mode. New game starts the current mode (remembered, else `default_mode`, else solo), and its second line names the mode and each setting's value | Title screen | `GameModeActivity`; `english.yaml` (an Options label, a Mode label; the New game row's second line) | the same title-screen shots |
| 3 | New over a save: the confirm question as built, with the current mode as its second headline line | Title screen dialog | `GameModeActivity` | `story-title-screen-screenshots/new-over-save-question.png`, `story-sweep-screenshots/new-over-save-*.png` (wording unchanged) |
| 4 | Options screen (new): a list headed "Options", with a Mode row (only when the host can start more than one mode) and one row per declared setting, each showing its current value; a tap cycles the value; Back returns to the title screen. Shown only when there is a choice to make | Options (new) | a new activity in `src/activities/games` built on `UiListActivity`; `english.yaml` | none (new screen; `mockups/key-options.html`) |
| 5 | Remembered choices: `prefs.bin` read by the title screen, written when Options closes with a change and when New game starts a mode other than the remembered one; removed with the game | Title screen, Options | `GameSaveStore`, `docs/crosshatch/formats.md`, the game's remove path | none |
| 6 | Manifest keys `default_mode`, `settings`, `handoff_text`, with their limits and Invalid reasons; the reserved images `title.png` and `handoff.png` (size checks, counted toward the image total, unavailable to `ch.gfx.image`) | install | `lib/GameCore/Manifest`, the installer's image conversion, `scripts/pack_game.py`, `docs/crosshatch/api-level-1.txt` (`manifest` and `limit` entries), `API_SURFACE_CRC` | none |
| 7 | `ctx.settings` passed to `setup`, the same table across Play again | match | `GameMatchActivity` / `GameVM` start path, game-api docs | none |
| 8 | Launcher row's second line: the modes this host can start, joined by " · " (an unavailable game's reason instead) | Launcher | `GamesLauncherActivity`; `english.yaml` (the separator, if not literal) | `story-one-row-screenshots/launcher-saves-one-row-each.png`, `launcher-after-leave-page2-row-selected.png` |
| 9 | Hand-off screen: "Player N, tap when ready" under the `eye-closed` icon; with `handoff.png`, the image centred and the text in a framed white box at `handoff_text` (default bottom); the sleep blank unchanged (icon only) | Hand-off | `GameMatchActivity` (its HandOff view; the blank `FrameReplay` frame); `english.yaml` (`STR_GAMES_HANDOFF_READY`, "Player %u, tap when ready") | `story-hand-off-screenshots/1-handoff-new-match.png`, `4-handoff-blank.png`, `story-continue-pass-screenshots/hidden-continue-handoff.png` |
| 10 | Play-again gap pause menu: "Starting the next round" centred under "Paused" | Pause menu | `GameMatchActivity` (the pause dialog's message line) | `story-hand-off-screenshots/6-gap-pause-menu.png` |
| 11 | Fixtures with `title.png`, `handoff.png`, `settings`, and `default_mode`, and the device-run packet's steps for them | test, device run | `test/game_script/fixtures/`, `scripts/pack_device_run.py` | none |

Not changed: the Result banner (`3-result-banner.png`), the unreadable-save path and its error view (`title-counter-unreadable-save.png`, `continue-unreadable-error-view.png`), the selection after Leave, and the New-over-a-save wording.

## Verification

- `python3 scripts/check_layers.py` and `python3 scripts/check_layers_test.py` on the edited spine.
- The owner's approval recorded above, word for word and dated, with the design files listed.
