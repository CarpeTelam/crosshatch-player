---
name: crosshatch brand mark
status: final
created: 2026-10-03
updated: 2026-10-03
design: DESIGN.md
sources:
  - _bmad-output/planning-artifacts/ux-designs/ux-crosshatch-player-2026-10-03/.memlog.md
  - _bmad-output/planning-artifacts/ux-designs/ux-crosshatch-player-2026-10-03/imports/crosspoint-sleep-screen-photo.jpg
  - _bmad-output/planning-artifacts/ux-designs/ux-crosshatch-player-2026-10-01/DESIGN.md
  - _bmad-output/planning-artifacts/ux-designs/ux-crosshatch-player-2026-10-01/EXPERIENCE.md
  - src/activities/boot_sleep/SleepActivity.cpp (renderDefaultSleepScreen)
  - src/activities/boot_sleep/BootActivity.cpp
  - src/images/Logo120.h
  - lib/I18n/translations/english.yaml (STR_CROSSPOINT, STR_SLEEPING, STR_BOOTING)
  - docs/crosshatch/game-icons.md (The launcher's row icons)
  - docs/crosshatch/upstream-touches.md
---

# crosshatch brand mark: Experience Spine

Where a mock and a spine differ, the spine wins. Mocks: [mark variants](mockups/mark-variants.html), [wordmark variants](mockups/wordmark-variants.html), [sleep screen](mockups/sleep-screen.html), [color variants](mockups/colour-variants.html). Visual specs live in `DESIGN.md`.

This spine records where the Crosshatch mark and name appear and what they replace. It adds no screen and no interaction; every device surface below already exists.

## Foundation

- **Form factors:** the Xteink X4 Pro (`x4pro`) and the Seeed reTerminal Sticky (`sticky`), both 480 x 800 portrait, 1-bit, as in the games spine. The sleep and boot layout is relative to the screen (`DESIGN.md` Layout & Spacing). The future website is the third surface, in a light and a dark theme, and is the only full-color one.
- **UI system:** the games area's spines (`ux-crosshatch-player-2026-10-01`, FreeInkUI) remain the UI system for the games area: launcher, title screen, hand-off screen. This run adds the brand mark to them and changes none of their behavior.
- **Visual reference:** `DESIGN.md`. The mark is `{components.brand-mark}`; the device is 1-bit (`{colors.ink}` on `{colors.paper}`), the website uses `{colors.mark-x}` and `{colors.mark-o}`.
- **Stakes:** consumer. The mark is identity, not an interaction; nothing here is a tap target.

## Inspiration & Anti-patterns

- **Match Phosphor:** the mark is drawn with Phosphor's regular-weight construction (256 grid, stroke 16, round caps and joins) so it sits beside the firmware's icon set; in the long term the mark should match that set.
- **Don't mimic CrossPoint's logo:** Crosshatch is a fork, but the mark stands on its own and need not look like CrossPoint's.
- **Rejected:** see `DESIGN.md` Do's and Don'ts, plus a hand-drawn pen-and-paper mark (not in that table).

## Information Architecture

Where the mark appears and which variant each place uses.

| Surface | Reached from | Variant | Replaces |
|---|---|---|---|
| Boot screen | device power-on | `{components.sleep-boot-screen}`: 120 px mark, title "Crosshatch", small line BOOTING | the CrossPoint logo and name |
| Sleep screen (default) | the device going to sleep, with the default sleep-screen mode | `{components.sleep-boot-screen}`: 120 px mark, title "Crosshatch", small line SLEEPING | the CrossPoint logo and name |
| Games launcher row | Home, then Games | `{components.game-icon-default-64}`, at `{spacing.icon-row}`, for a game with no icon of its own | the Phosphor `game-controller` fallback |
| Title screen splash | a launcher row | `{components.game-icon-default-128}`, at `{spacing.icon-hero}`, for a game with no `title.png` and no icon of its own | the Phosphor `game-controller` fallback, drawn large |
| Hand-off screen band | the hidden pass match | `{components.game-icon-default-128}`, when the game has no `handoff.png`, no `title.png` and no icon of its own | the Phosphor `game-controller` fallback |
| Website header | the website | `{components.lockup-formal}` | not applicable |
| Short-name placements: website footer, small headers, link previews, release notes | the website, repo and release pages | `{components.lockup-short}`, where "Player" is redundant or space is tight | not applicable |
| Square and portrait placements: social preview cards, poster and splash images, a narrow mobile header | the website and shared images | `{components.lockup-stacked}`, where a horizontal lockup does not fit | not applicable |
| Repo and release avatar | the repo and release pages | `{components.brand-mark}` alone (symbol only), a square asset with one cell of padding around the mark (the clear-space rule), so a circular crop does not clip the corner O rings | not applicable |

Surfaces other than those in the table are out of scope for this run. The device sleep and boot screens keep the stock layout and use no lockup.

The default icon is a built-in bitmap shared by the launcher row, the title screen and the hand-off fallback. A game that supplies its own icon (`icon.bmp` or a library `icon`) is unaffected on every surface. Games cannot select the default icon, because it is not an icon-library name.

The sleep screen shows the mark only through the default screen, which the DARK and LIGHT modes use and other modes fall back to (see State Patterns). A user's custom or cover sleep image is untouched.

→ Import: [photo of the current CrossPoint sleep screen on device](imports/crosspoint-sleep-screen-photo.jpg) (the layout this run keeps: logo centered, name under it, SLEEPING under that, inverted).

→ Composition reference: [sleep screen](mockups/sleep-screen.html) (the device layout; the mock is superseded in part, the spine wins, see `DESIGN.md` Layout & Spacing), [mark variants](mockups/mark-variants.html) (the mark at 128, 64 and 32 px, 1-bit and color), [wordmark variants](mockups/wordmark-variants.html) (the lockups), [color variants](mockups/colour-variants.html) (the measured palette). Spine wins on conflict.

## Voice and Tone

Microcopy. Brand voice lives in `DESIGN.md` Brand & Style. This run writes no new copy: the strings below are the ones the firmware already has, with the name changed where a string carries CrossPoint's name.

| Place | String (exact) | Key |
|---|---|---|
| Boot and sleep screen title | "Crosshatch" | `STR_CROSSPOINT` (today "CrossPoint"; its value becomes "Crosshatch") |
| Sleep screen small line | "SLEEPING" | `STR_SLEEPING` (unchanged) |
| Boot screen small line | "BOOTING" | `STR_BOOTING` (unchanged) |
| Website header lockup | "Crosshatch Player" | none (website) |
| Short-name lockup | "Crosshatch" | none (website footer, small headers, link previews, release notes) |

The formal name is "Crosshatch Player", capitalized; the short name is "Crosshatch". The device screens use the short name only, never "Crosshatch Player". The small line stays in capitals as it is today.

| Do | Don't |
|---|---|
| "Crosshatch" on the device screens | "Crosshatch Player" on the device screens |
| Capitalize both words of "Crosshatch Player" | "crosshatch player", "CROSSHATCH" |
| Keep SLEEPING and BOOTING as they are | New taglines or a slogan under the name |

## Component Patterns

Behavioral. Visual specs live in `DESIGN.md` Components.

| Component | Use | Behavioral rules |
|---|---|---|
| Brand mark (`{components.brand-mark}`) | Everywhere below | Decorative. Not a tap target on any surface. The device draws it from a pre-baked 1-bit bitmap (no scaling at run time); the website draws the SVG. |
| Lockup, formal (`{components.lockup-formal}`) | Per Information Architecture | Names the product in full. |
| Lockup, short (`{components.lockup-short}`) | Per Information Architecture | Names the product briefly. The website header keeps the formal lockup. Not used on the device screens (those draw the title as text). |
| Lockup, stacked (`{components.lockup-stacked}`) | Per Information Architecture | Used where a horizontal lockup does not fit. Not used on the device sleep and boot screens, which keep the stock layout. |
| Every lockup | Wherever a lockup is placed | Clear space, gap, minimum size and proportions: `DESIGN.md` Layout & Spacing. |
| Repo and release avatar | Repo and release pages | The mark alone on a square asset, one cell of padding around it. |
| Sleep and boot screen (`{components.sleep-boot-screen}`) | Boot, default sleep screen | Draws once and stays; no input. Boot also draws the version line at the bottom, unchanged. The sleep screen is inverted (white on black) except when the sleep-screen setting is LIGHT; the boot screen is never inverted. |
| Default game icon, 64 px (`{components.game-icon-default-64}`) | Launcher row | Shown only for a game with no `icon.bmp` and no usable manifest `icon`. The row's own behavior (tap, long-press, selection) is the games spine's and is unchanged. |
| Default game icon, 128 px (`{components.game-icon-default-128}`) | Title screen band, hand-off band | Shown only when the games spine's fallback chain reaches the game-icon step and the game has none. A tap on the band does nothing, as for any splash. |

## State Patterns

| State | Surface | Treatment |
|---|---|---|
| Game supplies an icon | Launcher row, title screen, hand-off screen | The game's own icon; the default mark is never drawn. |
| Game supplies no icon | Launcher row | The default mark at 64 px. |
| Game supplies no icon and no `title.png` | Title screen | The default mark at 128 px in the band. |
| Game supplies no icon, `title.png` or `handoff.png` | Hand-off screen | The default mark at 128 px in the band. |
| Manifest `icon` names something the library lacks | Launcher row | The default mark (the fallback is the last step in the chain; confirmed). |
| Sleep, DARK (the default) | Sleep | Mark. `renderDefaultSleepScreen`: the mark and "Crosshatch", SLEEPING under it, inverted (white on black). |
| Sleep, LIGHT | Sleep | Mark. The same default screen, not inverted: ink on paper. |
| Sleep, CUSTOM | Sleep | No mark while `renderCustomSleepScreen` finds an image (`/sleep.bmp`, else a random file from the sleep folders). With no usable image it falls back to the default screen: mark. |
| Sleep, COVER | Sleep | No mark while `renderCoverSleepScreen` has the open book's cover. With no open book or no cover it falls back to the default screen: mark. |
| Sleep, COVER_CUSTOM | Sleep | From the reader it draws the cover as COVER does; otherwise it draws the custom image as CUSTOM does; if the path it takes has nothing to show, it falls back, through the custom path, to the default screen: mark. |
| Sleep, BLANK | Sleep | No mark: `renderBlankSleepScreen` draws blank. |
| Sleep, QUICK_RESUME (also after a timeout, when the quick-resume-after-timeout setting is on) | Sleep | No mark: `renderLastScreenSleepScreen` keeps the last frame and never draws the default screen. |
| Sleep, TRANSPARENT_CUSTOM | Sleep | No mark while `renderTransparentCustomSleepScreen` finds a valid overlay on the retained frame. With none it logs an error and falls back to the default screen: mark. |
| Boot | Boot | The mark, "Crosshatch", BOOTING and the version line, not inverted: ink on paper (`BootActivity` never calls `invertScreen`). |

The default screen's inversion depends on the setting at the time, so a fallback from any mode other than LIGHT is inverted.

## Interaction Primitives

None. The mark and the name are not interactive on any surface in this spine.

- **Banned:** a tap or long-press handler on the mark, a game choosing the default icon by name.

## Accessibility Floor

Behavioral. Visual contrast lives in `DESIGN.md` Colors.

- **X and O are told apart without color.** On the device and in any grayscale or color-blind view, a cross and a ring differ by shape alone (both are drawn at stroke 16); the mark never relies on red against blue. The color-blind simulations on the color page were approximations: under deuteranopia and protanopia the red X goes olive in every red set tried. The shape rule is the guard, not the colors.
- **Non-text graphics are at least 3:1** against the surface they sit on (X and O on `{colors.surface-light}` and `{colors.surface-dark}`: see the contrast table in `DESIGN.md` Colors). Do not use slate (see Colors).
- **1-bit legibility, from the 1-bit rasters:** the mark is readable at 32 px (the O's inner diameter is 28 of 256 units, 3.5 px at 32 px before rasterizing; the 1-bit raster showed it to be tight). 64 px (the launcher row) was checked and raised no issues. 120 px (the sleep screen) was checked in the sleep mocks, clear in both polarities.
- **The name is text on the device.** "Crosshatch" is drawn as text by the firmware, so it follows the firmware's existing font handling; it is not baked into an image.
- **No flow depends on seeing the mark.** A game's row has its name; a title screen has its header. The mark is identity, never the only cue.

## Responsive & Platform

| Surface | Size and layout |
|---|---|
| x4pro and sticky, sleep and boot (480 x 800 portrait) | The stock layout in `DESIGN.md` Layout & Spacing (`{components.sleep-boot-screen}`); the same on both devices. |
| x4pro and sticky, launcher row | 64 px icon (`{spacing.icon-row}`). |
| x4pro and sticky, title and hand-off band | 128 px icon (`{spacing.icon-hero}`) centered in the 480 x 480 band. |
| Website, light | Mark and lockups in color on `{colors.surface-light}`; grid and wordmark black. |
| Website, dark | The same on `{colors.surface-dark}`; grid and wordmark white. |

Website layout (header size, breakpoints, footer) is deliberately out of scope here and left to a future website story; this spine fixes only the mark, lockups, colors, fonts, and light and dark.

## Key Flows

The owner accepted both flows as drafted. The protagonists and settings are illustrative.

### Flow 1: Tom installs a game with no icon (Tom, 11, at the kitchen table after school)

Tom's uncle sent him a small paper-and-pencil game as a package. Its developer did not include an icon.

1. Tom installs the game and opens Games from Home.
2. The launcher shows a row for it with the 64 px Crosshatch mark at the left, the game's name, and its modes line ("Solo").
3. He taps the row.
4. The title screen opens: the game's name in the header, and in the band the Crosshatch mark at 128 px, then Continue or New game below.
5. **Climax:** he sees the same small board of crosses and rings in the row and big on the title screen, so the game does not look broken or half-installed; it looks like a Crosshatch game.
6. He taps New game and plays.

Failure: the game's manifest names an icon the library lacks. The row shows the Crosshatch mark in its place (the same fallback); the game still opens.

### Flow 2: Maya puts the device to sleep (Maya, 34, in bed, finishing a chapter before turning the light off)

Maya has left the sleep-screen setting on its default.

1. She presses the power button.
2. The panel refreshes to the sleep screen: white on black, the Crosshatch mark 120 px at the center.
3. Under it, in the device's own font, "Crosshatch"; under that, in small type, SLEEPING.
4. She puts the device on the nightstand.
5. **Climax:** the last thing on the screen is a small game of tic-tac-toe in progress and the name Crosshatch. The panel holds the image with no power, so it is what she sees when she picks the device up in the morning.

Failure: Maya's setting is a custom image, and the image file is missing. The default screen is shown instead, with the mark and name (the firmware's existing fallback to the default screen).

## Implementation notes for builders

Repo facts, not design decisions. The draw coordinates and offsets are in `DESIGN.md` Layout & Spacing.

- `SleepActivity.cpp`, `BootActivity.cpp`, `src/images/Logo120.h` and `src/network/html/FilesPage.html` are upstream files not listed in `docs/crosshatch/upstream-touches.md`, and `STR_CROSSPOINT` in `english.yaml` falls outside the ledger's `STR_GAMES_*` entry. The ledger needs entries (or a guard) before any code change.
- `Logo120` is a pre-baked 120 x 120 1-bit bitmap. The A2 bitmap's polarity must match what `Logo120` does today.
- Builder acceptance check: the ring's outer edge is at 78 and the neighboring grid stroke's edge at 80, which is 0.94 px at 120 px, 0.5 px at 64 px and 1 px at 128 px, so a plain threshold can fuse the O into the grid. Pixel-check the 120 px and 64 px bitmaps (hand-corrected pixels are allowed); the 128 px icon is a native drawing.
- `GameRowIcon::choose` (`src/games/GameRowIcon.h`) picks `icon.bmp`, then the manifest `icon`, then `game-controller`; the mark replaces the last step. The icon library is Phosphor-only (`docs/crosshatch/game-icons.md`), so the mark is a separate built-in bitmap, not a library name.
- `STR_CROSSPOINT` and `Logo120` are used only by `BootActivity` and `SleepActivity`.
- Follow-up, out of scope for this run: `FilesPage.html` shows "CrossPoint Reader" in its title and header and "CrossPoint E-Reader • Open Source" in its footer, and `STR_CALIBRE_INSTRUCTION_1` ("1) Install CrossPoint Reader plugin") names a third-party plugin.

## Open Questions

None.
