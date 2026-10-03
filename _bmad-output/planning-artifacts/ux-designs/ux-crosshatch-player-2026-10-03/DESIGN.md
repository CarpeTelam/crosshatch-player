---
name: crosshatch brand mark
description: The Crosshatch brand mark (a tic-tac-toe game in progress), its colors, its wordmark and lockups, and the default game icon. Device screens are 1-bit; the website is full color, light and dark.
status: final
created: 2026-10-03
updated: 2026-10-03
sources:
  - _bmad-output/planning-artifacts/ux-designs/ux-crosshatch-player-2026-10-03/.memlog.md
  - _bmad-output/planning-artifacts/ux-designs/ux-crosshatch-player-2026-10-03/imports/crosspoint-sleep-screen-photo.jpg
  - _bmad-output/planning-artifacts/ux-designs/ux-crosshatch-player-2026-10-01/DESIGN.md
  - _bmad-output/planning-artifacts/ux-designs/ux-crosshatch-player-2026-10-01/EXPERIENCE.md
  - src/activities/boot_sleep/SleepActivity.cpp (renderDefaultSleepScreen)
  - src/activities/boot_sleep/BootActivity.cpp
  - docs/crosshatch/game-icons.md (The launcher's row icons)
  - docs/crosshatch/upstream-touches.md
colors:
  mark-x: '#C9524D'
  mark-o: '#5A7FC4'
  grid-on-light: '#000000'
  grid-on-dark: '#FFFFFF'
  surface-light: '#FFFFFF'
  surface-dark: '#121212'
  wordmark-on-light: '#000000'
  wordmark-on-dark: '#FFFFFF'
  ink: '#000000'
  paper: '#FFFFFF'
typography:
  wordmark:
    fontFamily: Noto Sans
    fontWeight: '700'
    note: 'Noto Sans Bold, the wordmark face only (website and image lockups). Capitalized: "Crosshatch Player", "Crosshatch". Size is relative to the mark height: {spacing.lockup-text-scale-horizontal} in horizontal lockups, {spacing.lockup-text-scale-stacked} in the stacked lockup.'
  wordmark-second-line:
    fontFamily: Noto Sans
    fontWeight: '700'
    note: '"Player" on the stacked lockup, 60% of the stacked "Crosshatch" text size ({spacing.stacked-second-line-scale})'
  device-title:
    fontFamily: Ubuntu
    fontWeight: '700'
    lineHeight: 24px
    note: 'UI_10_FONT_ID bold, the firmware UI font; the device sleep and boot screen title. lineHeight is the font''s advanceY (24 px); no font size in px is recorded, and the font slot is the contract.'
  device-small:
    fontFamily: Noto Sans
    fontWeight: '400'
    note: 'SMALL_FONT_ID (Noto Sans 8 regular); the SLEEPING / BOOTING line'
spacing:
  mark-viewbox: 256px
  mark-stroke: 16px
  mark-grid-near: 88px
  mark-grid-far: 168px
  mark-extent-min: 24px
  mark-extent-max: 232px
  mark-centre-near: 48px
  mark-centre-mid: 128px
  mark-centre-far: 208px
  mark-o-radius: 22px
  mark-x-half-diagonal: 18px
  stacked-second-line-scale: 60% of the stacked lockup's "Crosshatch" text size
  mark-cell: 33.33% of the mark's width
  lockup-clear-space: 33.33% of the mark's width
  lockup-mark-text-gap: 16.67% of the mark's width
  lockup-text-scale-horizontal: 66.67% of the mark's height
  lockup-text-scale-stacked: 33.33% of the mark's height
  lockup-mark-min: 32px
  icon-row: 64px
  icon-hero: 128px
  icon-device: 120px
  device-title-offset: 70px
  device-small-offset: 95px
components:
  brand-mark:
    viewbox: '{spacing.mark-viewbox}'
    stroke: '{spacing.mark-stroke}'
    caps: 'round'
    joins: 'round'
    grid: '{colors.grid-on-light} on {colors.surface-light}; {colors.grid-on-dark} on {colors.surface-dark}'
    x: '{colors.mark-x}'
    o: '{colors.mark-o}'
    device: '1-bit: {colors.ink} on {colors.paper}; the default sleep screen inverts it, the boot screen does not'
  lockup-formal:
    layout: 'horizontal: {components.brand-mark} then "Crosshatch Player"'
    text: '{typography.wordmark}'
    colour: '{colors.wordmark-on-light} / {colors.wordmark-on-dark}'
  lockup-short:
    layout: 'horizontal: {components.brand-mark} then "Crosshatch"'
    text: '{typography.wordmark}'
    colour: '{colors.wordmark-on-light} / {colors.wordmark-on-dark}'
  lockup-stacked:
    layout: 'three lines: {components.brand-mark}, "Crosshatch", "Player"'
    text: '{typography.wordmark}'
    second-line: '{typography.wordmark-second-line}'
    colour: '{colors.wordmark-on-light} / {colors.wordmark-on-dark}'
  sleep-boot-screen:
    icon: '{components.brand-mark} as a {spacing.icon-device} square 1-bit bitmap, centered on the screen'
    title: '"Crosshatch" in {typography.device-title}, top at screen height / 2 + {spacing.device-title-offset}, centered'
    small-line: 'SLEEPING or BOOTING in {typography.device-small}, at screen height / 2 + {spacing.device-small-offset}, centered'
  game-icon-default-64:
    size: '{spacing.icon-row}'
    artwork: '{components.brand-mark}, 1-bit, {colors.ink}'
  game-icon-default-128:
    size: '{spacing.icon-hero}'
    artwork: '{components.brand-mark}, native 128 px 1-bit drawing, {colors.ink}'
---

# crosshatch brand mark: Design Spine

Where a mock and a spine differ, the spine wins. Mocks: [mark variants](mockups/mark-variants.html), [wordmark variants](mockups/wordmark-variants.html), [sleep screen](mockups/sleep-screen.html), [color variants](mockups/colour-variants.html). The games area's own look (FreeInkUI, ink and paper) stays as the 2026-10-01 spine defines it; this file adds the brand mark and nothing else to it. The behavior of every surface lives in `EXPERIENCE.md`.

## Brand & Style

Crosshatch is named for tic-tac-toe, and its mark is a tic-tac-toe game in progress. The tone is fun and plain: simple pen-and-paper games. The mark stands alone, so the symbol works without any text.

The drawing is Phosphor-matched, because the firmware's icon library is Phosphor and the mark should sit beside it: Phosphor's regular-weight construction (256 grid, 16 stroke, round caps and joins), not a hand-drawn wobble. The grid is plain tic-tac-toe only, with no hatching-technique motif. Crosshatch is a fork of CrossPoint but the mark stands on its own; it does not have to look like CrossPoint's.

The device is 1-bit. X and O must be told apart by shape alone: both are drawn at the same stroke (16), so only a cross against a ring separates them. The mark must therefore read in black and white first; the red and blue exist for the website and only add to a drawing that already works.

## Colors

The website palette is final: **option C2, solid and flat.**

- **X red (`#C9524D`)** and **O blue (`#5A7FC4`)**: the two marks on the board, on the website only.
- **Grid**: black (`#000000`) on a white surface, white (`#FFFFFF`) on the dark surface.
- **Dark surface (`#121212`)**: near-black. It is the dark background on purpose. Slate (`#243038`) was measured and is not used: on it C2 reaches only 3.09:1 (X) and 3.39:1 (O), barely above 3:1.
- **Wordmark text**: black on white, white on `#121212`.
- **Ink / paper (`#000000` / `#FFFFFF`)**: the device. No color reaches the panel.

Measured contrast of each mark against the surface (WCAG, from the [color variants](mockups/colour-variants.html) page):

| Graphic | on white | on `#121212` |
|---|---:|---:|
| X `#C9524D` | 4.38:1 | 4.28:1 |
| O `#5A7FC4` | 3.99:1 | 4.70:1 |

All four are above the 3:1 non-text floor. Relative luminance is X 0.190 and O 0.213, so X against O is 1.10:1: near-equal. That is a known weak point, not a defect to hide. Two things mitigate it. First, the colors are desaturated from the drafted `#E5322D` / `#2B6CFF`, which were equiluminant at 1.03:1 and at risk of chromostereopsis (the red-blue vibration). Second, the grid always sits between any X and any O, so the two colors never touch. Two palettes with a lightness split between X and O (C1, C3) were tried and not chosen. Grid and wordmark against their surface: black on white 21.00:1, white on `#121212` 18.73:1.

## Typography

- **Wordmark:** Noto Sans Bold, capitalized: "Crosshatch Player" (formal) and "Crosshatch" (short). Noto Sans Bold is shipped in the firmware (`lib/EpdFont/builtinFonts/source/NotoSans/NotoSans-Bold.ttf`). A serif was drawn and dropped: its hairlines thin out at 22 to 24 px in 1-bit. See [wordmark variants](mockups/wordmark-variants.html).
- **Stacked lockup:** "Player" sits on its own line under "Crosshatch" at 60% of its size (`{typography.wordmark-second-line}`; sizes in Layout & Spacing). It stays legible in Noto Sans Bold in the wordmark mock's small 1-bit preview (64 px mark, 14 px "Player"); in the full-size stacked lockup (120 px mark, 40 px "Crosshatch"), "Player" is 24 px.
- **Scope of the wordmark face:** Noto Sans Bold is the wordmark face only (website, image lockups). It is not a system default and is not imposed on games; games use the system fonts, and a user who wants Noto Sans changes the font at system level.
- **Device title:** "Crosshatch" on the sleep and boot screens is set in the firmware UI font, Ubuntu 10 bold (`{typography.device-title}`), as CrossPoint's title is today. It is not the Noto Sans Bold wordmark. The small line under it is `{typography.device-small}`.
- **Size limit on device:** the largest shipped Noto Sans Bold UI size is 18 pt. A larger wordmark on the device would be a pre-rendered bitmap, not text.

## Layout & Spacing

**The mark's geometry** (viewBox 256, stroke 16, round caps and joins; the SVG source is the A2 symbol in [mark variants](mockups/mark-variants.html)):

| Part | Geometry |
|---|---|
| Grid | four lines, `{spacing.mark-grid-near}` and `{spacing.mark-grid-far}` on each axis, spanning `{spacing.mark-extent-min}` to `{spacing.mark-extent-max}` (`M88 24V232 M168 24V232 M24 88H232 M24 168H232`) |
| O | a ring of radius `{spacing.mark-o-radius}`, no fill |
| X | two diagonals, each from center minus 18 to center plus 18 on both axes (`{spacing.mark-x-half-diagonal}`), as Phosphor's `x` construction |
| Cell centers | `{spacing.mark-centre-near}`, `{spacing.mark-centre-mid}`, `{spacing.mark-centre-far}` |

**The board (a game in progress):** O in the top-left cell (48, 48); X in the center (128, 128); X in the bottom-right cell (208, 208); O in the top-right cell (208, 48). The other five cells are empty. No won-game variant exists.

**Lockups:**

| Lockup | Arrangement |
|---|---|
| Formal | horizontal: mark, then "Crosshatch Player" |
| Short | horizontal: mark, then "Crosshatch" |
| Stacked | three lines: mark, "Crosshatch", "Player" (60% of the "Crosshatch" size) |
| Mark alone | the symbol only, on a square asset with one cell of padding (`{spacing.lockup-clear-space}`) around the mark |

Placements (which lockup goes where) live in `EXPERIENCE.md` Information Architecture. The device sleep and boot screens use the stock layout below, not a lockup.

**Lockup spacing**, derived from the mark's grid, applies to every lockup: clear space around the lockup is one cell of the mark, one third of its width (`{spacing.lockup-clear-space}`, `{spacing.mark-cell}`); every gap in every lockup is half a cell (`{spacing.lockup-mark-text-gap}`): between mark and text, and in the stacked lockup between the mark and "Crosshatch" and between "Crosshatch" and "Player" (owner-decided: one rule). The avatar's padding is the same clear-space rule, so a circular crop does not clip the corner O rings, which reach 143 units from the mark's center, past the mark's half-width of 128.

**Lockup proportions** (owner-decided, as rendered in the wordmark mock):

| Lockup | Text size | Alignment |
|---|---|---|
| Formal and short (horizontal) | 2/3 of the mark height (`{spacing.lockup-text-scale-horizontal}`): 32 px text beside a 48 px mark | vertically centered on the mark |
| Stacked | 1/3 of the mark height (`{spacing.lockup-text-scale-stacked}`): 40 px text under a 120 px mark; "Player" at 60% of that (`{spacing.stacked-second-line-scale}`) | centered |

The minimum mark size is 32 px (`{spacing.lockup-mark-min}`), the smallest size checked in 1-bit.

**The device sleep and boot screens keep the stock CrossPoint layout** (the [photo of the current screen](imports/crosspoint-sleep-screen-photo.jpg) shows it). Only the icon (the new A2 bitmap) and the title text ("Crosshatch") change. Numbers, as the firmware draws them (`renderDefaultSleepScreen`, `BootActivity`):

| Element | Value |
|---|---|
| Icon | `{spacing.icon-device}` x `{spacing.icon-device}` (120 x 120), 1-bit, centered horizontally and vertically on the screen |
| Title | "Crosshatch", UI_10 bold, centered, top at screen height / 2 + `{spacing.device-title-offset}` (70), which is 10 px under the icon |
| Small line | SLEEPING or BOOTING, SMALL font, centered, at screen height / 2 + `{spacing.device-small-offset}` (95), which is 25 px under the title's top |
| Canvas | 480 x 800 portrait. The layout is relative to the screen (the code centers on `getScreenWidth()` and `getScreenHeight()`), so only the offsets above are fixed. |
| Polarity, sleep | inverted (white on black) except when the sleep-screen setting is LIGHT |
| Polarity, boot | not inverted: `BootActivity` never calls `invertScreen`, so the bitmap draws as designed, ink on paper |
| Boot only | the version string, centered at screen height - 30, unchanged |

The sleep-screen mock is superseded in part: it still shows the Noto Sans Bold 18 pt title, a small line at title top + 51, and icons at 120, 160, 200 and 240 px (larger icons and the 18 pt title were rendered and superseded). The decision is the stock layout above: Ubuntu 10 bold title, small line at h/2 + 95, 120 px icon. The spine wins.

## Components

- **Brand mark** (`{components.brand-mark}`): the A2 board above. On the website it uses the color tokens; on the device it is 1-bit ink on paper, X and O told apart by shape alone (a cross against a ring, same stroke). Never recolored, never given a fill, never hatched.
- **Lockups** (`{components.lockup-formal}`, `{components.lockup-short}`, `{components.lockup-stacked}`): the mark plus Noto Sans Bold text, as in Layout & Spacing. Text color follows `{colors.wordmark-on-light}` / `{colors.wordmark-on-dark}`.
- **Sleep and boot screen** (`{components.sleep-boot-screen}`): the stock layout with the A2 120 px bitmap and the title "Crosshatch". [Mock](mockups/sleep-screen.html): superseded in part; the spine wins; see Layout & Spacing.
- **Default game icon, 64 px** (`{components.game-icon-default-64}`): the mark as a 64 x 64 1-bit icon, ink, in the launcher row where a game has no icon of its own. Readable at 32 px as well (see the 1-bit legibility note in `EXPERIENCE.md`), but the row size is 64 px.
- **Default game icon, 128 px** (`{components.game-icon-default-128}`): a native 128 px drawing of the same geometry (not the 64 px bitmap doubled, as the Phosphor library's `large` size is), in the title screen's splash band and the hand-off screen's band, where a game has no `title.png` / `handoff.png` and no icon of its own.

The default icon is a built-in bitmap. It is not an icon-library name, and games cannot select it.

## Do's and Don'ts

| Do | Don't |
|---|---|
| Tell X from O by shape alone (cross against ring, same stroke); color is extra | Rely on red against blue to separate X from O |
| Keep the grid between every X and O | Let an X and an O touch, or add a fill between them |
| Black grid on white, white grid on `#121212` | Use slate `#243038` (see Colors) |
| X `#C9524D`, O `#5A7FC4`, flat | Chalk texture, the chalk-white grid, the pastel set (pink and sky, peach and mint, pink and lavender), or the drafted `#E5322D` / `#2B6CFF` |
| One game in progress, the A2 board as specified | A won-game variant, a strike line, or a hatching motif |
| Noto Sans Bold, capitalized, for wordmark text | A serif wordmark, lowercase, or Noto Sans Bold as a game or system text face |
| The firmware UI font (Ubuntu 10 bold) for the title on device screens | A larger or bitmap wordmark on the stock sleep and boot layout |
| "Player" at 60% size on the stacked lockup | "Player" at full size, or "Crosshatch Player" on one line in the stacked lockup |
| Ship the default icon as a built-in bitmap | Add the mark to the Phosphor-only icon library as a name games can select |
| Hold the sleep and boot layout at 120 px, title top at h/2 + 70 | Move the icon or resize it on the device default screens |
