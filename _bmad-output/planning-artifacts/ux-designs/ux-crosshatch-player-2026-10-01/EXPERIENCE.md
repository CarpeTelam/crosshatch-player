---
name: crosshatch-player games
status: draft
created: 2026-10-01
updated: 2026-10-02
design: DESIGN.md
sources:
  - _bmad-output/planning-artifacts/ux-designs/ux-crosshatch-player-2026-10-01/.memlog.md
  - docs/contributing/touch-and-ui.md
  - docs/crosshatch/game-canvas.md
  - _bmad-output/initiative-crosshatch-player-v1/epic-pass-and-play/epic-pass-and-play.md (R4, R5, R9, R16, R17)
  - _bmad-output/initiative-crosshatch-player-v1/epic-pass-and-play/story-the-title-screen-plan.md
  - _bmad-output/initiative-crosshatch-player-v1/epic-pass-and-play/story-one-launcher-row-per-game-plan.md
  - _bmad-output/initiative-crosshatch-player-v1/epic-pass-and-play/story-the-hidden-hand-off-in-the-match-plan.md
  - _bmad-output/initiative-crosshatch-player-v1/epic-pass-and-play/story-title-screen-screenshots/
  - _bmad-output/initiative-crosshatch-player-v1/epic-pass-and-play/story-one-row-screenshots/
  - _bmad-output/initiative-crosshatch-player-v1/epic-pass-and-play/story-hand-off-screenshots/
  - _bmad-output/initiative-crosshatch-player-v1/epic-pass-and-play/story-continue-pass-screenshots/
  - _bmad-output/initiative-crosshatch-player-v1/epic-pass-and-play/story-sweep-screenshots/
  - _bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md (AD-12, AD-15, AD-21, AD-22, AD-24)
  - _bmad-output/specs/spec-crosshatch-player/SPEC.md (CAP-4, CAP-6)
  - lib/I18n/translations/english.yaml
---

# crosshatch-player games: Experience Spine

The spines win over any mock. Mocks: [title screen](mockups/key-title-screen.html), [Options](mockups/key-options.html), [launcher](mockups/key-launcher.html), [hand-off and Result](mockups/key-hand-off.html), [Play-again gap pause menu](mockups/key-gap-pause.html).

This spine describes the games area as the owner decided it in the review of the built prototype (epic-pass-and-play entry 3, held as the entry 5.3 session, with the owner's decisions of 2026-10-02). Where the target differs from what is built, the line says "Built today: ...; changes in the follow-up story". The owner approved the whole design on 2026-10-02.

## Foundation

- **Form factor:** Xteink X4 Pro, 480 x 800 portrait, 1-bit e-ink with a touchscreen, plus front buttons the user can remap (`MappedInputManager::Button`). The Seeed reTerminal Sticky shares the same screens. Every screen works with touch alone and with buttons alone.
- **UI system:** FreeInkUI through the firmware hosts (`docs/contributing/touch-and-ui.md`): the launcher, title screen and Options are `UiListActivity` screens; the match's runtime views are option dialogs and tap zones on `UiAppHost`. The game canvas is the one declared input exception (`docs/crosshatch/game-canvas.md`). Global gestures (Back swipe, Home swipe, menu swipe) are FreeInkUI's and are not restated here.
- **Visual reference:** `DESIGN.md`. This spine is the behaviour.
- **Stakes:** consumer. A child must finish a flow alone (the restaurant test, AD-22).

## Information Architecture

| Surface | Reached from | Purpose |
|---|---|---|
| Home | wake, the Home gesture | Its "Games" entry opens the launcher |
| Games launcher | Home → Games | One row per installed game: icon, name, the modes it can start here or why it cannot |
| Title screen | a launcher row (tap or Confirm) | The game's splash and menu: Continue, New game, Options |
| Options | title screen → Options | Mode and the game's own settings, each cycling in place |
| New-over-save confirm | title screen → New game over a save | "Start a new game?" before a save is replaced |
| Match | Continue or New game | The game's canvas and the runtime views below |

The match's views (states per `docs/crosshatch/game-canvas.md` and AD-21):

| View | When |
|---|---|
| Canvas (Playing) | A round in play; the game draws, the runtime draws nothing over it |
| Pause menu | Back or Home in play, in Result, or on the hand-off screen |
| End-of-round menu | The round is over |
| Error view | The game could not start, stopped with an error, or a save could not be continued |
| Result banner | Hidden pass only: a move passed the turn |
| Hand-off screen | Hidden pass only: between seats, before the first seat of a new round, Play again, and a resumed match |
| Forced-exit blank | Hidden pass only: the device sleeps or another screen replaces the match; a plain white screen pushed before the user's CrossPoint sleep screen or the next screen |

Leave and the error view's Back return to the launcher with the game's own row selected on the page holding it (decided, as built).

## Voice and Tone

Every user-facing string goes through `tr()`; new keys are added to `lib/I18n/translations/english.yaml` only. Game names, setting names and setting values come from the game's manifest and are shown as written. Short, plain, complete; no exclamation marks; a child reads them.

| Surface | String (exact) | Key |
|---|---|---|
| Launcher header | "Games" | `STR_GAMES_TITLE` |
| Launcher, empty | "No games found" | `STR_GAMES_EMPTY` |
| Mode names (launcher modes line, New game line, Mode row, confirm) | "Solo", "Pass and play", "Play nearby" | `STR_GAMES_MODE_SOLO`, `_PASS`, `_NEARBY` |
| Modes line joiner | " · " (e.g. "Solo · Pass and play") | none (a separator) |
| Unavailable reasons | "Needs newer firmware", "Too old to run on this firmware", "Needs more players than fit here", "None of its modes work here", "The game's manifest is not valid" | `STR_GAMES_UNAVAILABLE_*` |
| Title screen header | the game's name | manifest `name` |
| Continue row | "Continue" / "Load the previous game" | `STR_GAMES_CONTINUE`, `STR_GAMES_CONTINUE_DESC` (new value; was "Go on with the saved game") |
| New game row | "New game" / the current mode's name, then each setting's current value, joined by " · " (e.g. "Solo · Hard") | `STR_GAMES_NEW_GAME`; manifest values |
| Options row and header | "Options" | new key |
| Mode row | "Mode" / the current mode's name | new key |
| Setting row | the setting's `name` / its current value | manifest `settings` |
| New-over-save confirm | "Start a new game?" / the current mode's name / "This replaces the saved game." / "Cancel", "New game" | `STR_GAMES_NEW_OVER_SAVE_TITLE`, `STR_GAMES_NEW_OVER_SAVE`, `STR_CANCEL`, `STR_GAMES_NEW_GAME` |
| Remove confirm (launcher long-press) | "Remove this game?" / "Its saved data is kept." / "Cancel", "Remove" | `STR_GAMES_REMOVE_TITLE`, `STR_GAMES_REMOVE_KEPT`, `STR_GAMES_REMOVE` |
| Pause menu | "Paused" / "Resume", "Leave" | `STR_GAMES_PAUSED`, `STR_GAMES_RESUME`, `STR_GAMES_LEAVE` |
| Pause menu in the Play-again gap | "Starting the next round" | `STR_GAMES_NEXT_ROUND_STARTING` |
| End-of-round menu | "Game over" / "Play again", "Leave" | `STR_GAMES_OVER`, `STR_GAMES_PLAY_AGAIN` |
| Result banner | "Tap to pass to player N" (N from 1) | `STR_GAMES_PASS_TO_PLAYER` |
| Hand-off screen | "Player N's turn" (N the next seat, from 1); not shown over a developer's `handoff.png` | new key |
| Hand-off button | "I'm ready" | new key |
| Error view headlines | "The game could not start", "The game stopped with an error" | `STR_GAMES_START_FAILED`, `STR_GAMES_ERROR` |
| Error view, save reasons | "The saved match could not be resumed. It is unchanged: try Continue again, or open the game for a new match." / "This saved game cannot be continued on this device. It is kept until you start a new game." | `STR_GAMES_RESUME_FAILED`, `STR_GAMES_RESUME_NOT_HERE` |
| Error view, other reasons | as listed in `docs/crosshatch/game-canvas.md` "The views" (e.g. "Not enough memory", "It stopped responding: one step ran over 3 seconds"), or Lua's own message | `STR_GAMES_*` |
| Error view option | "« Back" | `STR_BACK` |

The mode descriptions ("Play on your own", "Take turns on this device", "Play with another device") have no surface in the target design; the follow-up story removes their keys (`STR_GAMES_MODE_SOLO_DESC`, `_PASS_DESC`, `_NEARBY_DESC`). The forced-exit blank and the user's CrossPoint sleep screen carry no games text.

## Component Patterns

Behavioural. Visual specs live in `DESIGN.md` Components.

| Component | Use | Behavioural rules |
|---|---|---|
| Header bar | Launcher, title screen, Options | Its back arrow, Back, or the Back swipe pops one screen. |
| List row | Every list here | Tap or Confirm acts on the row; Up and Down move the selection; swipes scroll without moving it (FreeInkUI). |
| Launcher game row | Launcher | Tap or Confirm opens the game's title screen. Long-press (or a held Confirm) asks to remove the game. The second line names the modes this host can start, in solo, pass, nearby order, read from the manifest; no save is read for it. An unavailable game's row keeps its reason there, and tapping it opens nothing. After Leave the game's own row is selected on the page holding it. [Mock](mockups/key-launcher.html). |
| Title-screen splash | Title screen | Not a target: a tap on the splash does nothing. Shows `title.png` (at most 480 x 480) when the package has one, else the game's icon at 128 px centred in the band. A `title.png` that cannot be read at runtime falls back to the icon, logged. |
| Title-screen menu rows | Title screen | Rows top to bottom: Continue (only with a save; first and selected), New game, Options (only when the host can start more than one of the game's modes or the game declares settings). With no save, New game is first and selected. Returning from Options keeps the Options row selected. Continue resumes the save with its own mode and settings. New game starts the current mode with the current settings: at once with no save, through the confirm over one. [Mock](mockups/key-title-screen.html). |
| Options row | Options | Mode first (only with more than one startable mode), then one row per setting in manifest order. A tap or Confirm sets the next value in place, wrapping after the last; the choice holds at once and is written for the game when Options closes. Back returns to the title screen, whose New game line shows the new choice and whose Options row stays selected. Options changes only New games. [Mock](mockups/key-options.html). |
| Option / confirm dialog | New-over-save; remove; runtime views | Opens with its safe option focused (Cancel; Resume). Up and Down move the focus; tap or Confirm chooses; Back is Cancel in a confirm and Resume in the pause menu. |
| Result banner | Hidden pass match | Shown over the mover's own frame after a move that passes the turn. It is a button: only a tap on the framed banner, or Confirm, goes to the hand-off screen; a tap elsewhere does nothing (a late tap of the mover still reaches the mover's game, its move discarded). Back or Home pauses. A tap or press made before the banner is on the panel is dropped. [Mock](mockups/key-hand-off.html), third column. |
| Hand-off screen | Hidden pass match | Only a tap on the "I'm ready" button, or Confirm, shows the next seat's frame (full refresh); a tap elsewhere does nothing; Back or Home pauses on no frame. Nothing counts until the screen is on the panel. The banner's button is at the bottom and "I'm ready" in the middle, so a double tap on the banner cannot land on "I'm ready". Drawn only by the runtime, with or without the game's `handoff.png`; no game script draws on it (AD-12, CAP-6). [Mock](mockups/key-hand-off.html), first and second columns. |
| "I'm ready" button | Hand-off screen (default and with `handoff.png`) | The hand-off screen's only tap target, drawn by the runtime in the middle of the screen; a tap on it or Confirm shows the next seat's frame; it takes nothing until the screen is on the panel. [Mock](mockups/key-hand-off.html), first and second columns. |
| Forced-exit blank | Hidden pass match, forced exit | On sleep or Replace the runtime first pushes a plain white screen (no icon, no text), so nothing private stays on the panel during the forced exit's SD steps; the device then shows the user's CrossPoint sleep screen setting (Dark, Light, Custom, Cover, Blank, Transparent custom overlay, Quick resume, ...) as anywhere else, and the overlay modes draw over white, never over a player's view. It takes no input; the match has ended its forced exit, so the device wakes on Home. |
| Pause menu | Match | Resume or Back returns to the state it was opened from; Leave writes the save and returns to the launcher. In the Play-again gap the menu is inert and shows "Starting the next round" until the new round's first frame, then is drawn again without the line. [Mock](mockups/key-gap-pause.html). |
| End-of-round menu | Match | Play again starts a new round (hidden pass: through the hand-off screen); Leave returns to the launcher. Back does nothing. |
| Error view | Match | One option, "« Back", which leaves to the launcher. The save, if any, is never changed by it. |

## State Patterns

| State | Surface | Treatment |
|---|---|---|
| No save | Title screen | No Continue row; New game first and selected. |
| Valid save | Title screen | Continue first and selected, so a second Confirm from the launcher resumes. New game asks before replacing it. |
| Unreadable save | Title screen | Continue shows as for a good save. Its tap ends in the error view ("The game could not start" and the reason); the file is kept. New game still asks first. Kept as built. |
| One startable mode | Launcher, title screen, Options | Modes line shows that one name. No Mode row in Options. |
| Several startable modes | Launcher, title screen, Options | Modes line lists them. Options appears, its Mode row first. |
| No settings | Title screen | New game's second line is the mode's name alone. |
| Options hidden | Title screen | One startable mode and no settings: the menu is Continue (with a save) and New game only. |
| Current mode | Title screen, New game | The mode last started, else the manifest's `default_mode`, else solo when listed, else the first listed. A remembered or default mode this host cannot start falls back to the first one it can. A pass-only game's current mode is pass. |
| Remembered choices | Options, title screen | Stored per game by the runtime on the SD card (`prefs.bin`), not in the game's `ch.store`; written when Options closes with a change and when New game starts a mode other than the remembered one; kept, like the game's other saved data, when the game is removed ("Its saved data is kept."); an unknown or no-longer-startable value falls back to the developer's default. A failed write is logged, and the choice lasts until the title screen closes. |
| Unavailable game | Launcher | Row shows the reason line; tap opens nothing. |
| Empty launcher | Launcher | "No games found". |
| Launcher out of memory | Launcher | A registry load (on entry, or the reload after a remove) that runs out of memory shows "Not enough memory" (`STR_GAMES_OUT_OF_MEMORY`), never "No games found"; tap and Confirm open nothing. As built. |
| Hidden pass | Match | Before each seat: Result banner (after a move), then the hand-off screen, then the seat's frame. Before the first seat of a new or resumed match and after Play again: the hand-off screen. |
| Open pass | Match | No Result and no hand-off: the next seat's frame follows the move. |
| Result | Match, hidden pass | The mover's own frame with the banner. A late tap of the mover still reaches the mover's game; its move is discarded. |
| HandOff | Match, hidden pass | The game's icon, "Player N's turn" and the "I'm ready" button; or the developer's `handoff.png` and the same button. An unreadable `handoff.png` falls back to the default screen, logged. Built today: the `eye-closed` icon alone, dismissed by a tap anywhere; changes in the follow-up story. |
| Forced exit | Match, hidden pass | The plain white push, then the user's CrossPoint sleep screen (or the screen that replaced the match), whatever was on screen before. Built today: the `eye-closed` icon as the blank; changes in the follow-up story. |
| Play-again gap | Match | The pause menu opened before the new round's first frame stays over the last frame, inert, with "Starting the next round" centred under "Paused". Built today: left-aligned; changes in the follow-up story. |
| Paused from HandOff | Match, hidden pass | The pause menu sits on a cleared screen, no seat's frame. |
| Error | Match | The error view on a cleared screen. |

## Interaction Primitives

- **Tap**: acts on a row, an option, or a button. On the Result banner only a tap on the banner counts, and on the hand-off screen only a tap on "I'm ready"; a tap elsewhere on either does nothing.
- **Confirm**: the same as a tap on the selected row or focused option; on the banner and the hand-off screen, the same as a tap on their button.
- **Back** (button, header arrow, or the Back swipe): pops a list screen; cancels a confirm; pauses a match (never leaves it); resumes from the pause menu; leaves only from the error view.
- **Home**: pauses a round in play; otherwise goes Home as everywhere.
- **Tap cycles a setting**: on Options, the row's value moves to the next one in place, wrapping; there is no choice list.
- **Long-press** (or a held Confirm): on a launcher row only, asks to remove the game.
- **Banned**: text entry anywhere in the games area; a tap that starts a New game over a save without the confirm; any input reaching the next seat's game before that seat's frame is on the panel; a tap anywhere that dismisses the banner or the hand-off screen.

## Accessibility Floor

- Touch targets are FreeInkUI's list rows and option rows (`DESIGN.md` `{spacing.row-two-line}`, `{spacing.option-row}`), the Result banner (`{spacing.view-width}` x `{spacing.banner-height}`), and the "I'm ready" button (`{spacing.view-width}` x `{spacing.option-row}`). The two buttons sit apart (bottom, middle) so one double tap cannot press both.
- No flow needs text entry, reading beyond a short line, or a gesture more precise than a tap.
- Every flow works with buttons alone (Up, Down, Confirm, Back) and with touch alone.
- **The restaurant test:** a child finishes a start alone. From Home, Continue and New game in the current mode each take 3 taps (Games, the game, the start); New over a save takes 4 (the confirm); a different mode or setting goes through Options first, after which it is the current one and costs 3 again. This is AD-22 as amended.
- A stray Confirm never destroys a save: Continue is first and selected over a save, and New over it asks with Cancel focused.

## Key Flows

### Flow 1: Mia continues her solo game (Mia, 8, waiting at a restaurant table)

Mia played Counter in the car and left it mid-game; the device slept.

1. She wakes the device; it shows Home. She taps Games.
2. The launcher shows one row per game, Counter's reading "Solo". She taps it.
3. The title screen shows Counter's icon large, then Continue (selected), New game "Solo", and no Options.
4. She taps Continue.
5. **Climax:** her game comes back exactly where she left it, three taps from Home, with no grown-up asked to help.

Failure: the save cannot be read. Continue still shows; her tap ends in the error view, "The game could not start" with "The saved match could not be resumed. It is unchanged: try Continue again, or open the game for a new match." Her "« Back" returns to the launcher with Counter selected; the file is kept. If she taps New game instead, the confirm asks "Start a new game?" with Cancel focused.

### Flow 2: Sam and Priya pass the device in a hidden game (Sam and Priya, siblings, on the sofa)

1. Sam taps Games, then Pass hidden (its row reads "Pass and play"). The title screen offers New game "Pass and play"; he taps it (3 taps).
2. The hand-off screen shows the game's icon, "Player 1's turn" and an "I'm ready" button. Sam taps "I'm ready".
3. Sam's own frame appears; he makes his move.
4. His frame stays, with "Tap to pass to player 2" in a framed banner at the bottom. He taps the banner and hands the device over.
5. The hand-off screen reads "Player 2's turn" over the "I'm ready" button; nothing of Sam's frame remains on the panel. Priya's fingers brush the screen as she takes it: nothing happens.
6. **Climax:** Priya taps "I'm ready" and sees only her own frame. Neither has seen the other's hand.

Failure: the device sleeps mid-turn. The screen first goes plain white, then shows the sleep screen Priya's family chose in the device's settings, never Sam's or Priya's view; it wakes on Home. Games, Pass hidden, Continue resumes on the hand-off screen, "Player N's turn" naming the seat whose turn it is.

### Flow 3: Priya picks Pass and play for the first time (Priya, 10, wants to play Pass open with her dad)

1. Priya taps Games, then Pass open (its row reads "Solo · Pass and play").
2. The title screen shows New game "Solo" (nobody has played it, so the default) and Options. She taps Options.
3. Options shows Mode "Solo". She taps the row; it reads "Pass and play".
4. She goes Back. New game now reads "Pass and play".
5. She taps New game; a pass match starts.
6. **Climax:** next evening, three taps start Pass and play again: the game remembered her choice.

Failure: a save exists from her solo game. New game asks "Start a new game?", "Pass and play", "This replaces the saved game.", Cancel focused; Cancel or Back keeps the save.

## Developer overrides

What a game's developer controls through its package (AD-15 as amended), and what the runtime always keeps.

| Developer controls | How | Runtime always keeps |
|---|---|---|
| The title-screen splash | `title.png`, at most 480 x 480, drawn centred and clipped in the 480 x 480 band (absent or unreadable: the game's icon, centred) | The header with the game's name and back arrow; the menu rows, their order and text |
| The hidden hand-off page | `handoff.png`, at most 480 x 800, drawn centred and clipped; one image for every seat and match; it replaces the game's icon and the "Player N's turn" text (unreadable: the default screen, logged) | The "I'm ready" button, drawn by the runtime in the middle of the screen as on the default screen; only it or Confirm dismisses; the full refresh. No script draws during the hand-off (AD-12, CAP-6). |
| The mode New game starts first | `default_mode`, one of the manifest's modes (absent: solo when listed, else the first) | The player's remembered mode wins once one is chosen; a mode this host cannot start falls back |
| The game's own settings | `settings`: up to 4, each with an id, a name, 2 to 6 values, and a default | The Options screen, the cycling, the storage of the player's choices; Continue resumes with the save's own settings |

Consequence of the full-page override: the runtime draws no text over `handoff.png`, so a developer's page cannot name the next seat; one image serves every seat. There is no manifest key for placing hand-off text.

The forced exit is never overridden: it pushes the plain white screen, then the user's own CrossPoint sleep screen setting, never the developer's image.

## Built vs designed

What the follow-up story changes against the built prototype. Screenshots are in `_bmad-output/initiative-crosshatch-player-v1/epic-pass-and-play/`.

| Surface | Built today | Designed | Screenshot it changes |
|---|---|---|---|
| Title screen layout | A plain list under the header | A 480 x 480 splash band (the icon at 128 px or `title.png`) above the menu | `story-title-screen-screenshots/title-counter-solo-save.png`, `title-counter-no-save.png` |
| Title screen menu | Continue ("Go on with the saved game"), then one row per mode with its description | Continue ("Load the previous game"), New game (current mode and settings), Options | `story-title-screen-screenshots/title-pass-open-no-save.png`, `title-pass-hidden-no-save.png` |
| Choosing a mode | Tap the mode's row | New game starts the current mode; another mode is chosen in Options first | `story-title-screen-screenshots/start-pass-open-pass.png` |
| Options screen | Does not exist | Mode and setting rows, a tap cycles the value | none |
| Remembered mode and settings | None | The last mode and each setting kept per game by the runtime | none |
| New-over-save confirm | "Start a new game?", the tapped mode, "This replaces the saved game.", Cancel focused | Unchanged, with the current mode as its second line | `story-sweep-screenshots/new-over-save-cancel-focused.png` (no visual change) |
| Unreadable save | Continue shown; error view on tap | Unchanged | `story-title-screen-screenshots/continue-unreadable-error-view.png` (no change) |
| Launcher row | Icon and name; second line only for an unavailable game | Icon, name, and the modes line (or the reason) | `story-one-row-screenshots/launcher-saves-one-row-each.png` |
| Selection after Leave | The game's row on its page | Unchanged | `story-one-row-screenshots/launcher-after-leave-page2-row-selected.png` (no change) |
| Hand-off screen | The `eye-closed` icon alone; a tap anywhere dismisses it | The game's icon at 128 px in the upper half, "Player N's turn", and an "I'm ready" button in the middle, the only tap target; or the developer's `handoff.png` with the same button | `story-hand-off-screenshots/1-handoff-new-match.png`, `4-handoff-blank.png`, `story-continue-pass-screenshots/hidden-continue-handoff.png` |
| Forced exit (sleep, Replace) | The `eye-closed` icon as the blank | A plain white push (no icon, no text), then the user's CrossPoint sleep screen setting | `story-continue-pass-screenshots/hidden-result-before-sleep.png` (the screen that follows it) |
| Result banner | Framed box about 4/5 wide near the bottom; a tap anywhere passes | Look and wording unchanged; it becomes a button: only a tap on it, or Confirm, passes | `story-hand-off-screenshots/3-result-banner.png` (no visual change) |
| Play-again gap pause menu | "Starting the next round" left-aligned under "Paused" | Centred | `story-hand-off-screenshots/6-gap-pause-menu.png` |
| Package and manifest | `icon.png` only | Adds `title.png` (at most 480 x 480), `handoff.png` (at most 480 x 800), `default_mode`, `settings` | none |
