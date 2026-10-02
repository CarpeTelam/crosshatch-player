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
- `review-rubric-walker.md`: the coverage check the owner chose to run before the files were marked final.
- `.memlog.md`: the session's decision log, owner decisions and working assumptions, in order.

## Decisions (owner, 2026-10-01 and 2026-10-02, in this session)

1. **Title screen.** It becomes a splash screen for the game, not a plain list.
   - **Splash area:** 480 × 480 under the header. It shows the game's `title.png` (at most 480 × 480). Without one, it shows the game's icon at 128 px, centred.
   - **Menu:** the three two-line rows fill the 226 px left below the splash area:
     - Continue, "Load the previous game", shown only with a save, first and selected;
     - New game;
     - Options.
   - **New game** starts the game's current mode: the mode last started. Before one has been started, it is solo unless the developer sets another default (`default_mode`). Its second line names the mode and each setting's value, with an ellipsis when long.
   - **Options** holds the mode and the game's own settings (AI level and so on), and appears only when there is a choice to make.
     - A tap on a setting cycles its value. The row shows no second line.
     - Coming back from Options keeps the Options row selected.
     - Options changes only New games; Continue resumes with the save's own choices.
2. **New over a save** keeps the built confirm question, its wording, and its mode line. Players should know they are starting a new game over the previous save.
3. **Unreadable save:** reported only when Continue is tapped, as built, error view wording included.
4. **Launcher row:**
   - The second line names the modes this host can start ("Solo · Pass and play"). An unavailable game keeps its reason there.
   - The selection after Leave stays as built.
5. **Result banner:** text, size, and place as built ("Tap to pass to player N", about 4/5 of the width, near the bottom). The banner becomes a button: only a tap on it, or Confirm, goes on.
6. **Hidden hand-off screen:**
   - **Default:** no eye icon. It shows the game's icon at 128 px in the upper half and "Player N's turn" under it.
   - **The button:** an "I'm ready" button in the middle of the screen. Only a tap on it, or Confirm, shows the next seat; a tap elsewhere does nothing.
   - **Why the two buttons sit apart:** the Result banner is at the bottom and "I'm ready" in the middle, so a double tap on one cannot land on the other.
   - **Developer override:** a full-page `handoff.png` (at most 480 × 800) takes the place of the icon and the text, and only the "I'm ready" button stays. No script draws during the hand-off, so AD-12 and CAP-6 hold. A developer's page cannot name the next seat.
7. **Sleep in a hidden pass match:** the device shows the user's CrossPoint sleep screen setting as anywhere else.
   - The match first pushes a plain white screen. The overlay modes (a transparent overlay, Quick resume) then draw over white, never over a player's view.
   - Nothing private stays on the panel during the forced exit's SD steps.
8. **Play-again gap pause menu:** it stays over the last round's frame, with "Starting the next round" centred.
9. **Remembered choices** (`prefs.bin`):
   - They are kept when the game is removed, like its other saved data ("Its saved data is kept.").
   - A failed write is logged, and the choice lasts until the title screen closes.
   - An unreadable `title.png` or `handoff.png` falls back to the icon or to the default hand-off screen, and is logged.
10. **Limits:**
    - `title.png` at most 480 × 480, `handoff.png` at most 480 × 800.
    - At most 4 settings, each with 2 to 6 values.
    - The mode descriptions (`STR_GAMES_MODE_*_DESC`) lose their surface and are removed.
11. **Scope:** everything above is built in this epic (the owner chose this over moving the API parts to a later epic).
12. **Budget:** the follow-up story measures. A measurement over this epic's share stops the run for the owner, who reallocates then.
13. **Spine:** this session amends AD-8, AD-12, AD-15, and AD-17 as well as AD-22 and the Capability map, superseding the brief's "touch only AD-22".
14. **Review:** before the files were marked final, only the coverage check ran, not the accessibility lens.

## Spine and SPEC amendments

All are dated "Amended 2026-10-02 (owner, epic-pass-and-play entry 3, the title-screen UX session)".

- **AD-22:** the game's row opens its title screen, a start there is the third tap, and Continue is offered first there. The bullet also covers:
  - the launcher's modes line and the selection after Leave;
  - the title screen's splash area, Continue, New game in the current mode, and Options;
  - the confirm question for New over a save (its fourth tap);
  - the tap counts.
- **AD-8:** `ctx.settings`, the chosen setting values, fixed for the match.
- **AD-12:**
  - The runtime alone draws the hand-off screen (the game's icon, "Player N's turn", and the "I'm ready" button, or `handoff.png` and the button).
  - Only the button, or Confirm, dismisses it; the Result banner is a button too.
  - The forced exit pushes a plain white screen before the user's CrossPoint sleep screen.
- **AD-15:**
  - two reserved images, drawn only by the runtime: `title.png` (at most 480 × 480) and `handoff.png` (at most 480 × 800);
  - the manifest keys `default_mode` and `settings`, with their limits;
  - the registry keeps only whether a game has settings;
  - all of these are level-1 preview entries.
- **AD-17:** `/.games-data/<id>/prefs.bin`, holding the last mode started and the chosen settings. `GameSaveStore` writes it on the loop task. A missing or stale file falls back to the manifest's defaults, and removing the game keeps the file.
- **Capability map:** a row for the per-game title screen and Options.
- **SPEC CAP-4 success:** "From Home, a game starts in at most 3 taps: Games, the game, and Continue or New game on its title screen, which starts the game's current mode (the mode last started, or the developer's default; another mode is picked in Options first). "Continue" is offered first when a save exists. The list pages past one screen."
  - "Offered first" replaces "listed first", as the brief asked.
  - The owner approved the 3-tap clause's change as well.

**Not amended here, for the orchestrator:**
- The epic's R9 (one New start per mode; now New game in the current mode, and Options).
- R4 (the Result banner and the hand-off screen accept a tap anywhere; now only their buttons, or Confirm), and R4/R5's "blank" hand-off screen (it now carries the game's icon and runtime text, or the developer's image, but still no script drawing).
- R6 and R14, the sleep blank (now a plain white push, then the user's sleep screen).
- SPEC CAP-6's "blank hand-off screen", which still holds in substance.
- The game-api-seed companion's `ctx` and manifest sections, which need the new fields when the follow-up story documents them.

## Changes for a follow-up story

Each change against what is built (entries 4, 7, 8, 9, and 10, merged at `38a8b75d`):

| # | Change | Screen | File or area | Screenshot it changes |
|---|--------|--------|--------------|-----------------------|
| 1 | Splash area, 480 × 480 under the header: `title.png` (at most 480 × 480), else the game's icon at 128 px, centred | Title screen | `GameModeActivity` (its icon drawing shared with `GameRowIcon`) | `story-title-screen-screenshots/title-*.png`, `story-one-row-screenshots/confirm-opens-title-continue-first.png`, `story-continue-pass-screenshots/*-title-continue.png` |
| 2 | Menu becomes Continue (with a save), New game, Options, replacing one New row per mode. New game starts the current mode (remembered, else `default_mode`, else solo), and its second line names the mode and each setting's value, with an ellipsis when long | Title screen | `GameModeActivity`; `english.yaml` (an Options label, a Mode label; the `STR_GAMES_MODE_*_DESC` keys go) | the same title-screen shots |
| 3 | Continue's second line reads "Load the previous game" | Title screen | `english.yaml` (`STR_GAMES_CONTINUE_DESC`) | `story-title-screen-screenshots/title-counter-solo-save.png`, `title-pass-open-pass-save.png`, `title-counter-unreadable-save.png` |
| 4 | New over a save: the confirm question as built, with the current mode as its second headline line | Title screen dialog | `GameModeActivity` | `story-title-screen-screenshots/new-over-save-question.png`, `story-sweep-screenshots/new-over-save-*.png` (wording unchanged) |
| 5 | Options screen (new): headed "Options"; a Mode row (only with more than one startable mode) and one row per declared setting, each showing its current value; a tap cycles the value; Back returns to the title screen with the Options row selected; shown only when there is a choice to make | Options (new) | a new activity in `src/activities/games` built on `UiListActivity`; `english.yaml` | none (new screen; `mockups/key-options.html`) |
| 6 | Remembered choices: `prefs.bin` read by the title screen, written when Options closes with a change and when New game starts a mode other than the remembered one; kept when the game is removed; a failed write logged | Title screen, Options | `GameSaveStore`, `docs/crosshatch/formats.md` | none |
| 7 | Manifest keys `default_mode` and `settings`, with their limits and Invalid reasons; the reserved images `title.png` (at most 480 × 480) and `handoff.png` (at most 480 × 800): size checks, counted toward the image total, unavailable to `ch.gfx.image`, an unreadable one falling back at runtime | install | `lib/GameCore/Manifest`, the installer's image conversion, `scripts/pack_game.py`, `docs/crosshatch/api-level-1.txt` (`manifest` and `limit` entries), `API_SURFACE_CRC` | none |
| 8 | `ctx.settings` passed to `setup`, the same table across Play again | match | `GameMatchActivity` / `GameVM` start path, game-api docs | none |
| 9 | Launcher row's second line: the modes this host can start, joined by " · " (an unavailable game's reason instead) | Launcher | `GamesLauncherActivity` | `story-one-row-screenshots/launcher-saves-one-row-each.png`, `launcher-after-leave-page2-row-selected.png` |
| 10 | Hand-off screen: the game's icon at 128 px in the upper half, "Player N's turn", and an "I'm ready" button in the middle; with `handoff.png`, the image in place of the icon and text, the button kept; only the button or Confirm dismisses it | Hand-off | `GameMatchActivity` (its HandOff view, the blank frame, the tap zone); `english.yaml` ("Player %u's turn", "I'm ready") | `story-hand-off-screenshots/1-handoff-new-match.png`, `4-handoff-blank.png`, `story-continue-pass-screenshots/hidden-continue-handoff.png` |
| 11 | Result banner becomes a button: only a tap on the banner, or Confirm, goes to the hand-off; a tap elsewhere does nothing | Result | `GameMatchActivity` (Result's tap zone) | `story-hand-off-screenshots/3-result-banner.png` (no visual change) |
| 12 | Forced exit in a hidden pass match: push a plain white screen (no icon) in the blank's place, then the user's CrossPoint sleep screen as usual | Sleep | `GameMatchActivity::onExit` (the forced-exit blank), `docs/crosshatch/game-canvas.md` | `story-continue-pass-screenshots/hidden-result-before-sleep.png` (the screen that follows it) |
| 13 | Play-again gap pause menu: "Starting the next round" centred under "Paused" | Pause menu | `GameMatchActivity` (the pause dialog's message line) | `story-hand-off-screenshots/6-gap-pause-menu.png` |
| 14 | Fixtures with `title.png`, `handoff.png`, `settings`, and `default_mode`, and the device-run packet's steps for them (the buttons, the sleep screen in each CrossPoint sleep mode, a transparent overlay included) | test, device run | `test/game_script/fixtures/`, `scripts/pack_device_run.py` | none |

Not changed: the Result banner's look and wording, the unreadable-save path and its error view (`continue-unreadable-error-view.png`), the selection after Leave, and the New-over-a-save wording.

## Verification

- `python3 scripts/check_layers.py` and `python3 scripts/check_layers_test.py` on the edited spine: both pass.
- The owner's approval recorded above, word for word and dated, with the design files listed.
