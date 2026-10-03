---
name: crosshatch brand mark
status: draft
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

Where a mock and a spine differ, the spine wins. Mocks: [mark variants](mockups/mark-variants.html), [wordmark variants](mockups/wordmark-variants.html), [sleep screen](mockups/sleep-screen.html), [colour variants](mockups/colour-variants.html). Visual specs live in `DESIGN.md`.

This spine records where the Crosshatch mark and name appear and what they replace. It adds no screen and no interaction; every surface below already exists.

## Foundation

- **Form factors:** the Xteink X4 Pro (`x4pro`) and the Seeed reTerminal Sticky (`sticky`), both 800 x 480 panels, 1-bit. The sleep and boot canvas is portrait, 480 x 800. The future website is the third surface, in a light and a dark theme, and is the only full-colour one.
- **UI system:** the games area's spines (`ux-crosshatch-player-2026-10-01`, FreeInkUI) remain the UI system for the games area: launcher, title screen, hand-off screen. This run adds the brand mark to them and changes none of their behaviour.
- **Visual reference:** `DESIGN.md`. The mark is `{components.brand-mark}`; the device is 1-bit (`{colors.ink}` on `{colors.paper}`), the website uses `{colors.mark-x}` and `{colors.mark-o}`.
- **Stakes:** consumer. The mark is identity, not an interaction; nothing here is a tap target.

## Information Architecture

Where the mark appears and which variant each place uses.

| Surface | Reached from | Variant | Replaces |
|---|---|---|---|
| Boot screen | device power-on | `{components.sleep-boot-screen}`: 120 px mark, title "Crosshatch", small line BOOTING | the CrossPoint logo and name |
| Sleep screen (default) | the device going to sleep, with the default sleep-screen mode | `{components.sleep-boot-screen}`: 120 px mark, title "Crosshatch", small line SLEEPING | the CrossPoint logo and name |
| Games launcher row | Home, then Games | `{components.game-icon-default-64}`, at `{spacing.icon-row}`, for a game with no icon of its own | the Phosphor `game-controller` fallback |
| Title screen splash | a launcher row | `{components.game-icon-default-128}`, at `{spacing.icon-hero}`, for a game with no `title.png` and no icon of its own | the same fallback, drawn large |
| Hand-off screen band | the hidden pass match | `{components.game-icon-default-128}`, when the game has no `handoff.png`, no `title.png` and no icon of its own | the same fallback |
| Website header | the website | `{components.lockup-formal}` | not applicable |
| Short-name placements | not decided | `{components.lockup-short}` | not applicable |
| Repo and release avatar | not decided | not decided | not applicable |

[NOTE FOR UX: the memlog does not say which places use the short lockup, where the stacked lockup (`{components.lockup-stacked}`) is used now that the device screens use the stock layout, or whether the mark is the repo and release avatar. Surfaces other than those in the table are out of scope.]

The default icon is a built-in bitmap shared by the launcher row, the title screen and the hand-off fallback. A game that supplies its own icon (`icon.bmp` or a library `icon`) is unaffected on every surface. Games cannot select the default icon, because it is not an icon-library name.

The sleep screen shows the mark only in the default sleep-screen mode (and where a custom or cover mode falls back to it). A user's custom or cover sleep image is untouched.

→ Import: [photo of the current CrossPoint sleep screen on device](imports/crosspoint-sleep-screen-photo.jpg) (the layout this run keeps: logo centred, name under it, SLEEPING under that, inverted).

→ Composition reference: [sleep screen](mockups/sleep-screen.html) (the device layout), [mark variants](mockups/mark-variants.html) (the mark at 128, 64 and 32 px, 1-bit and colour), [wordmark variants](mockups/wordmark-variants.html) (the lockups), [colour variants](mockups/colour-variants.html) (the measured palette). Spine wins on conflict.

## Voice and Tone

Microcopy. Brand voice lives in `DESIGN.md` Brand & Style. This run writes no new copy: the strings below are the ones the firmware already has, with the name changed where it carries CrossPoint's.

| Place | String (exact) | Key |
|---|---|---|
| Boot and sleep screen title | "Crosshatch" | `STR_CROSSPOINT` (today "CrossPoint"; its value becomes "Crosshatch") |
| Sleep screen small line | "SLEEPING" | `STR_SLEEPING` (unchanged) |
| Boot screen small line | "BOOTING" | `STR_BOOTING` (unchanged) |
| Website header lockup | "Crosshatch Player" | none (website) |
| Short-name lockup | "Crosshatch" | none (website, images) |

The formal name is "Crosshatch Player", capitalised; the short name is "Crosshatch". The device screens use the short name only, never "Crosshatch Player". The small line stays in capitals as it is today.

| Do | Don't |
|---|---|
| "Crosshatch" on the device screens | "Crosshatch Player" on the device screens |
| Capitalise both words of "Crosshatch Player" | "crosshatch player", "CROSSHATCH" |
| Keep SLEEPING and BOOTING as they are | New taglines or a slogan under the name |

## Component Patterns

Behavioural. Visual specs live in `DESIGN.md` Components.

| Component | Use | Behavioural rules |
|---|---|---|
| Brand mark (`{components.brand-mark}`) | Everywhere below | Decorative. Not a tap target on any surface. The device draws it from a pre-baked 1-bit bitmap (no scaling at run time); the website draws the SVG. |
| Lockup, formal (`{components.lockup-formal}`) | Website header | Names the product in full. |
| Lockup, short (`{components.lockup-short}`) | Where the short name is used | Names the product briefly. Not used on the device screens (those draw the title as text). |
| Lockup, stacked (`{components.lockup-stacked}`) | Image lockups | Not used on the device sleep and boot screens. [NOTE FOR UX: place of use undecided.] |
| Sleep and boot screen (`{components.sleep-boot-screen}`) | Boot, default sleep screen | Draws once and stays; no input. Boot also draws the version line at the bottom, unchanged. The sleep screen is inverted (white on black) except when the sleep-screen setting is LIGHT. |
| Default game icon, 64 px (`{components.game-icon-default-64}`) | Launcher row | Shown only for a game with no `icon.bmp` and no usable manifest `icon`. The row's own behaviour (tap, long-press, selection) is the games spine's and is unchanged. |
| Default game icon, 128 px (`{components.game-icon-default-128}`) | Title screen band, hand-off band | Shown only where the games spine's fallback chain reaches the game's icon and the game has none. A tap on the band does nothing, as for any splash. |

## State Patterns

| State | Surface | Treatment |
|---|---|---|
| Game supplies an icon | Launcher row, title screen, hand-off screen | The game's own icon; the default mark is never drawn. |
| Game supplies no icon | Launcher row | The default mark at 64 px. |
| Game supplies no icon and no `title.png` | Title screen | The default mark at 128 px in the band. |
| Game supplies no icon, `title.png` or `handoff.png` | Hand-off screen | The default mark at 128 px in the band. |
| Manifest `icon` names something the library lacks | Launcher row | The default mark (the fallback is the last step in the chain). [ASSUMPTION: the replacement covers this case, as the fallback it replaces did.] |
| Default sleep-screen mode | Sleep | The mark and "Crosshatch", SLEEPING under it. |
| Custom, cover or blank sleep mode | Sleep | No mark; the user's choice is shown. Where a custom or cover mode has no image to show it falls back to the default screen, so the mark appears. |
| LIGHT sleep-screen setting | Sleep | Not inverted: ink on paper. |
| Boot | Boot | The mark, "Crosshatch", BOOTING and the version line. |

## Interaction Primitives

None. The mark and the name are not interactive on any surface in this spine.

- **Banned:** a tap or long-press handler on the mark, a game choosing the default icon by name.

## Accessibility Floor

Behavioural. Visual contrast lives in `DESIGN.md` Colors.

- **X and O are told apart without colour.** On the device and in any greyscale or colour-blind view, a cross and a ring differ by shape and by stroke weight; the mark never relies on red against blue. The colour-blind simulations on the colour page were approximate: under deuteranopia and protanopia the red X goes olive in every red set tried. The shape rule is the guard, not the colours.
- **Non-text graphics are at least 3:1** against the surface they sit on: X 4.38:1 on white and 4.28:1 on `{colors.surface-dark}`; O 3.99:1 on white and 4.70:1 on `{colors.surface-dark}` (`DESIGN.md` Colors). Slate is not used because C2 falls to 3.09:1 and 3.39:1 there.
- **1-bit legibility, from the 1-bit rasters:** the mark is readable at 32 px (the O's hole is about 1 to 2 px, tight). 64 px (the launcher row) was checked and not flagged. 120 px (the sleep screen) was checked in the sleep mocks, clear in both polarities. "Player" at 14 px in Noto Sans Bold holds in 1-bit; a serif did not.
- **The name is text on the device.** "Crosshatch" is drawn as text by the firmware, so it follows the firmware's existing font handling; it is not baked into an image.
- **No flow depends on seeing the mark.** A game's row has its name; a title screen has its header. The mark is identity, never the only cue.

## Responsive & Platform

| Surface | Size and layout |
|---|---|
| x4pro and sticky, 480 x 800 portrait sleep and boot | 120 px mark centred, title at screen height / 2 + 70, small line at screen height / 2 + 95 (`DESIGN.md` Layout & Spacing); the same on both devices. |
| x4pro and sticky, launcher row | 64 px icon (`{spacing.icon-row}`). |
| x4pro and sticky, title and hand-off band | 128 px icon (`{spacing.icon-hero}`) centred in the 480 x 480 band. |
| Website, light | Mark and lockups in colour on `{colors.surface-light}`; grid and wordmark black. |
| Website, dark | The same on `{colors.surface-dark}`; grid and wordmark white. |

The website's layout (header size, breakpoints) is not specified here. [NOTE FOR UX: website layout and the lockup's size in the header are undecided.]

## Key Flows

Both flows are [ASSUMPTION]: the facilitator drafted them for the owner to correct. The protagonists and settings are invented for illustration.

### Flow 1: Tom installs a game with no icon [ASSUMPTION] (Tom, 11, at the kitchen table after school)

Tom's uncle sent him a small paper-and-pencil game as a package. Its developer did not include an icon.

1. Tom installs the game and opens Games from Home.
2. The launcher shows a row for it with the Crosshatch mark at 64 px at the left, the game's name, and its modes line ("Solo").
3. He taps the row.
4. The title screen opens: the game's name in the header, and in the band the Crosshatch mark at 128 px, then Continue or New game below.
5. **Climax:** he sees the same small board of crosses and rings in the row and big on the title screen, so the game does not look broken or half-installed; it looks like a Crosshatch game.
6. He taps New game and plays.

Failure: the game's manifest names an icon the library lacks. The row shows the Crosshatch mark in its place (the same fallback); the game still opens. [ASSUMPTION]

### Flow 2: Maya puts the device to sleep [ASSUMPTION] (Maya, 34, in bed, finishing a chapter before turning the light off)

Maya has left the sleep-screen setting on its default.

1. She presses the power button.
2. The panel refreshes to the sleep screen: white on black, the Crosshatch mark 120 px at the centre.
3. Under it, in the device's own font, "Crosshatch"; under that, in small capitals, SLEEPING.
4. She puts the device on the nightstand.
5. **Climax:** the last thing on the screen is a small game of tic-tac-toe in progress and the name Crosshatch. The panel holds the image with no power, so it is what she sees when she picks the device up in the morning.

Failure: Maya's setting is a custom image, and the image file is missing. The default screen is shown instead, with the mark and name. [ASSUMPTION: relies on the firmware's existing fallback to the default screen.]

## Implementation notes for builders

Repo facts found while distilling; not design decisions.

- `src/activities/boot_sleep/SleepActivity.cpp` (`renderDefaultSleepScreen`, lines 619 to 632) and `src/activities/boot_sleep/BootActivity.cpp` (lines 13 to 19) draw `Logo120` at `((width - 120) / 2, (height - 120) / 2)`, the title `tr(STR_CROSSPOINT)` in `UI_10_FONT_ID` bold at `height / 2 + 70`, and `tr(STR_SLEEPING)` or `tr(STR_BOOTING)` in `SMALL_FONT_ID` at `height / 2 + 95`.
- `SleepActivity.cpp`, `BootActivity.cpp` and `src/images/Logo120.h` are upstream files not listed in `docs/crosshatch/upstream-touches.md`. The `Upstream touch ledger` job will fail a change to them until the ledger has entries (or the change sits behind a guard).
- `STR_CROSSPOINT` is defined in `lib/I18n/translations/english.yaml`. The ledger lists that file only for `STR_GAMES_*` keys (prefix-scoped), so a change to `STR_CROSSPOINT` is outside the existing entry. `STR_CROSSPOINT` and `Logo120` are used only by these two activities.
- `Logo120` is a pre-baked 120 x 120 1-bit bitmap (`static const uint8_t Logo120[]`). The A2 mark needs a new 120 x 120 bitmap; the sleep screen inverts the whole frame unless the setting is LIGHT, so the bitmap's polarity must match what `Logo120` does today.
- `SleepActivity.cpp` also falls back to `renderDefaultSleepScreen` in its custom, cover and no-cover paths (lines 612, 791, 801).
- The launcher row icon is chosen by `GameRowIcon::choose` (`src/games/GameRowIcon.h`): the package's `icon.bmp`; else the manifest `icon` in its `icon_weight`, when the library has the name; else `game-controller` regular. The mark replaces the last step. The same fallback feeds the title screen's 128 px band and the hand-off band per the games spine.
- The game icon library is Phosphor-only (`docs/crosshatch/game-icons.md`): names are Phosphor's own, in two weights, 1-bit at 32 px and 64 px, and `large` (128 px) draws the 64 px bitmap doubled. The library's names are API surface, so the mark is a separate built-in bitmap, not a new name. The cover-grid Home's Games tab also uses `game-controller` (`GameIcons::GAME_CONTROLLER_32`); the memlog does not change that tab.
- Noto Sans Bold UI sizes shipped in the firmware are 12, 14, 16 and 18 pt; a larger wordmark on device would be a pre-rendered bitmap.

## Open Questions

- [NOTE FOR UX] Which placements use the short lockup (`{components.lockup-short}`) is undecided.
- [NOTE FOR UX] Where the stacked lockup (`{components.lockup-stacked}`) is used is undecided now that the device screens use the stock layout.
- [NOTE FOR UX] Whether the mark is the repo and release avatar is undecided (the memlog does not say).
- [NOTE FOR UX] Spacing between mark and text, and clear space, for each lockup are undecided.
- [NOTE FOR UX] Whether the 128 px default icon is a native drawing or the 64 px bitmap doubled is undecided.
- [NOTE FOR UX] Website layout and the header lockup's size are undecided.
- [NOTE FOR UX] Other uses of the CrossPoint name or logo beyond boot and sleep (the memlog says "e.g. power-off screen") are not enumerated; the code shows only those two activities.
- [ASSUMPTION] Flow 1 (Tom installs a game with no icon), including its failure path.
- [ASSUMPTION] Flow 2 (Maya puts the device to sleep), including its failure path.
- [ASSUMPTION] The default mark also replaces `game-controller` when a manifest names an icon the library lacks.
