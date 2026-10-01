---
name: crosshatch-player games
description: The games area of the crosshatch-player e-reader firmware (launcher, title screen, Options, the match's runtime views) on 1-bit e-ink. FreeInkUI is the system; this file specifies only the games area's deltas.
status: draft
created: 2026-10-01
updated: 2026-10-01
sources:
  - _bmad-output/planning-artifacts/ux-designs/ux-crosshatch-player-2026-10-01/.memlog.md
  - docs/contributing/touch-and-ui.md
  - docs/crosshatch/game-canvas.md
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
colors:
  ink: '#000000'
  paper: '#FFFFFF'
typography:
  header-title:
    fontFamily: Ubuntu
    fontWeight: '700'
    fontSize: 24px
    note: 'UI_12_FONT_ID bold, the FreeInkUI header; size measured (cap height 17 px), the font slot is the contract'
  row-label:
    fontFamily: Ubuntu
    fontWeight: '400'
    fontSize: 24px
    note: 'UI_12_FONT_ID regular (the FreeInkUI body font)'
  row-subtitle:
    fontFamily: Ubuntu
    fontWeight: '400'
    fontSize: 20px
    note: 'UI_10_FONT_ID regular (the FreeInkUI small font); one line, ellipsis'
  dialog-caption:
    fontFamily: Ubuntu
    fontWeight: '400'
    fontSize: 20px
    note: 'UI_10_FONT_ID; the game name above a runtime view headline'
  dialog-headline:
    fontFamily: Ubuntu
    fontWeight: '700'
    fontSize: 24px
    note: 'FreeInkUI option-dialog headline; maxLines set where it can wrap'
  dialog-message:
    fontFamily: Ubuntu
    fontWeight: '400'
    note: 'FreeInkUI option-dialog message style, inherited; wraps (the error view to 8 lines)'
  banner-text:
    fontFamily: Ubuntu
    fontWeight: '400'
    fontSize: 24px
    note: 'UI_12_FONT_ID regular, centred'
  handoff-text:
    fontFamily: Ubuntu
    fontWeight: '400'
    fontSize: 24px
    note: '[ASSUMPTION] same as banner-text; not decided'
rounded:
  none: 0px
  sm: 6px
  md: 8px
spacing:
  gutter: 24px
  inset: 16px
  header: 94px
  rule: 3px
  frame: 2px
  row-two-line: 74px
  row-icon: 86px
  option-row: 52px
  splash-band: 360px
  list-dialog-width: 360px
  view-width: 378px
  banner-height: 131px
  banner-bottom: 20px
  icon-row: 64px
  icon-view: 64px
  icon-option: 32px
  icon-hero: 128px
components:
  selection:
    fill: 'dither-25: one {colors.ink} pixel per 2 x 2 cell, the rest {colors.paper}'
    ink: '{colors.ink}'
    radius: '{rounded.sm}'
    inset: '{spacing.gutter}'
  header-bar:
    height: '{spacing.header}'
    title: '{typography.header-title}'
    rule: '{spacing.rule} {colors.ink}'
    leading: 'back arrow'
  list-row:
    label: '{typography.row-label}'
    subtitle: '{typography.row-subtitle}'
    height-two-line: '{spacing.row-two-line}'
    selected: '{components.selection}'
  launcher-game-row:
    icon: '{spacing.icon-row}'
    label: '{typography.row-label}'
    subtitle: '{typography.row-subtitle}'
    height: '{spacing.row-icon}'
    selected: '{components.selection}'
  title-splash:
    band: '{spacing.splash-band}'
    default: 'game icon at {spacing.icon-hero}, {colors.ink}, centred'
    override: 'title.png, at most 480 x 360, centred, clipped'
  title-menu-row:
    base: '{components.list-row}'
  options-row:
    base: '{components.list-row}'
  option-dialog:
    frame: '{spacing.frame} {colors.ink}'
    radius: '{rounded.md}'
    background: '{colors.paper}'
    padding: '{spacing.inset}'
    width-list: '{spacing.list-dialog-width}'
    width-view: '{spacing.view-width}'
    caption: '{typography.dialog-caption}'
    headline: '{typography.dialog-headline}'
    message: '{typography.dialog-message}'
    icon: '{spacing.icon-view}'
    option-height: '{spacing.option-row}'
    option-icon: '{spacing.icon-option}'
    focused: '{components.selection}'
  result-banner:
    frame: '{spacing.frame} {colors.ink}'
    radius: '{rounded.md}'
    background: '{colors.paper}'
    width: '{spacing.view-width}'
    height: '{spacing.banner-height}'
    bottom: '{spacing.banner-bottom}'
    text: '{typography.banner-text}'
  hand-off-screen:
    background: '{colors.paper}'
    icon: 'eye-closed at {spacing.icon-hero}, {colors.ink}, centred'
    text: '{typography.handoff-text}'
  hand-off-text-box:
    frame: '{spacing.frame} {colors.ink}'
    radius: '{rounded.md}'
    background: '{colors.paper}'
    width: '{spacing.view-width}'
    text: '{typography.handoff-text}'
  sleep-blank:
    background: '{colors.paper}'
    icon: 'eye-closed at {spacing.icon-hero}, {colors.ink}, centred'
  pause-menu:
    base: '{components.option-dialog}'
    gap-line: '{typography.dialog-message}, centred'
  error-view:
    base: '{components.option-dialog}'
---

# crosshatch-player games: Design Spine

The spines win over any mock. Mocks: [title screen](mockups/key-title-screen.html), [Options](mockups/key-options.html), [launcher](mockups/key-launcher.html), [hand-off and Result](mockups/key-hand-off.html), [Play-again gap pause menu](mockups/key-gap-pause.html). The behaviour of every component below lives in `EXPERIENCE.md`.

## Brand & Style

The games area is part of an e-reader, and it looks like one: black ink on white paper, the same header, list and dialog that the reader's own settings and library use. It inherits FreeInkUI (`docs/contributing/touch-and-ui.md`) wholesale; this file names only what the games area adds on top of it.

What it adds is one moment of invitation. The owner's review found the built title screen, a plain list, did not draw the player in, so the title screen becomes a splash: the game's own picture large above a short menu. Everywhere else the games area stays plain, so the splash, and a developer's hand-off page, are the only places where a game's own art appears outside its canvas.

## Colors

The panel is 1-bit. There are two colors and no grays.

- **Ink (`#000000`)**: text, icons, rules, frames, and the selection's dither.
- **Paper (`#FFFFFF`)**: every background, the inside of every frame, and the hand-off text box over a developer's art.

Gray does not exist on this panel; the selection is a dither (`{components.selection}`: one ink pixel in each 2 x 2 cell, measured from the built screens), never a tint. Ink on paper is the only text combination, at 21:1. Text on the selection reads ink over a 25 % dither, as built and legible in every screenshot. Never draw text over a denser dither or over a developer image without the paper box.

## Typography

FreeInkUI's two Ubuntu UI fonts, as bound by `UIScale.h`: `UI_12_FONT_ID` for header titles (bold), row labels, headlines and the banner; `UI_10_FONT_ID` for row second lines and the dialog caption. Pixel sizes in the frontmatter are measured from the 480 x 800 screenshots and are approximate; the font slot is the contract.

- One weight change only: bold for the header title and dialog headlines.
- Second lines (`{typography.row-subtitle}`) are one line and end in an ellipsis when long (FreeInkUI's `maxLines` default), which can happen to New game's mode-and-settings line.
- The hand-off text's type is not decided; it is drawn here as `{typography.banner-text}` [ASSUMPTION].

## Layout & Spacing

The X4 Pro screen is 480 x 800, portrait. Measured from the built screens:

- **Header** `{spacing.header}`: the status strip (battery at the top right), the back arrow and title, then a `{spacing.rule}` ink rule across the full width.
- **Lists**: the selection fill is inset `{spacing.gutter}` from the left edge and stops short of the scrollbar lane on the right; a two-line row pitches at `{spacing.row-two-line}`, a launcher row with its 64 px icon at `{spacing.row-icon}`. The first row starts `{spacing.inset}` under the rule.
- **Title screen**: under the header, a splash band of `{spacing.splash-band}` (the most `title.png` may be, AD-15), the image or the game's icon centred in it; the menu rows follow under the band [ASSUMPTION: band height fixed at the image maximum, so the menu does not move between games]. Three two-line rows end near y = 690, inside the screen.
- **Runtime views** (pause, end-of-round, error): one framed dialog `{spacing.view-width}` wide (about 4/5 of the width), centred on the screen; the title screen's own confirm is `{spacing.list-dialog-width}`, centred in the content area under the header, as FreeInkUI builds it in a list screen.
- **Result banner**: `{spacing.view-width}` wide, `{spacing.banner-height}` tall, `{spacing.banner-bottom}` above the bottom edge, centred.
- Padding inside every frame is `{spacing.inset}`.

## Elevation & Depth

None. A 1-bit panel has no shadow or tone, so layers are told apart by a `{spacing.frame}` ink frame on paper. A dialog or banner over a game frame covers it with paper inside its frame and leaves the rest of the frame visible.

## Shapes

- `{rounded.md}` (8 px): every frame: the option dialog, the Result banner, the hand-off text box.
- `{rounded.sm}` (6 px): the selection fill.
- `{rounded.none}`: the header rule and the splash band; a developer's image is drawn as shipped, never masked.

## Components

FreeInkUI's header, list, option dialog, scrollbar, tap flash and tap zones are used as they are. Per component, what the games area specifies:

- **Header bar**: back arrow, then the screen's title in `{typography.header-title}`: "Games" on the launcher, the game's name on its title screen, "Options" on Options. Unchanged from built.
- **List row (one line, two lines, selected)**: label in `{typography.row-label}`, an optional second line in `{typography.row-subtitle}`. Selected: `{components.selection}` behind both lines, ink text unchanged. No icons on title-screen or Options rows.
- **Launcher game row**: the game's 64 px icon (its `icon.png`, or a library icon) at the left, the name, and under it the modes line ("Solo · Pass and play") or an unavailable game's reason, both in `{typography.row-subtitle}`. An unavailable row is not dimmed: the reason is the only mark, so the selection stays visible on it. Built today: one-line rows, a second line only for an unavailable game; the modes line comes in the follow-up story. [Mock](mockups/key-launcher.html).
- **Title-screen splash**: the `{spacing.splash-band}` band under the header. Default: the game's icon at `{spacing.icon-hero}`, ink, centred. Override: the game's `title.png` (at most 480 x 360), centred and clipped. No frame and no text in the band. Built today: no splash. [Mock](mockups/key-title-screen.html).
- **Title-screen menu rows**: two-line list rows. Continue / "Go on with the saved game"; New game / the current mode and each setting's value, joined by " · "; Options, one line [ASSUMPTION: no second line was decided]. [Mock](mockups/key-title-screen.html).
- **Options row (name + current value)**: a two-line list row, the name ("Mode", or the game's setting name) on the first line and its current value on the second. Nothing marks it as cycling: no arrows, no toggle. [Mock](mockups/key-options.html).
- **Option / confirm dialog**: `{components.option-dialog}`: ink frame, `{rounded.md}`, paper inside, `{spacing.inset}` padding; optional caption (`{typography.dialog-caption}`, centred), headline (`{typography.dialog-headline}`), message lines, an optional 64 px library icon centred between text and options, then the options, each `{spacing.option-row}` tall with an optional 32 px icon at its left, the focused one on the dither. The New-over-save confirm (headline, the mode's name, message, Cancel, New game) is as built (`story-sweep-screenshots/new-over-save-cancel-focused.png`).
- **Result banner**: `{components.result-banner}` over the mover's own frame: frame, paper inside, "Tap to pass to player N" centred in `{typography.banner-text}`. No button hints. As built (`story-hand-off-screenshots/3-result-banner.png`). [Mock](mockups/key-hand-off.html), third column.
- **Hand-off screen, default**: a paper screen, no status strip, the `eye-closed` icon at `{spacing.icon-hero}` centred, and "Player N, tap when ready" centred under it in `{typography.handoff-text}`. Built today: the icon alone. [Mock](mockups/key-hand-off.html), first column.
- **Hand-off screen with a developer image**: the game's `handoff.png` (at most 480 x 800) centred and clipped, then `{components.hand-off-text-box}`: a framed paper box holding the same text, at the manifest's `handoff_text` position (top, middle, or bottom; bottom when absent). The box's frame, width and height are drawn as the Result banner's [ASSUMPTION: only "a framed white box" is decided]. [Mock](mockups/key-hand-off.html), second column.
- **Sleep blank**: the `eye-closed` icon alone at `{spacing.icon-hero}`, centred on paper, no text, never the developer's image. Unchanged from built (`story-hand-off-screenshots/1-handoff-new-match.png` shows the same drawing).
- **Pause menu**: an option dialog `{spacing.view-width}` wide over the frame: caption (the game's name), headline "Paused", the `pause` icon, Resume (`play` icon) and Leave (`sign-out` icon). Opened in the Play-again gap, it adds "Starting the next round" under the headline, **centred** like the caption and headline. Built today: the line is left-aligned (`story-hand-off-screenshots/6-gap-pause-menu.png`). [Mock](mockups/key-gap-pause.html).
- **End-of-round menu**: the same dialog: headline "Game over", the `flag-checkered` icon, Play again (`arrows-clockwise`) and Leave. Unchanged.
- **Error view**: the same dialog on a cleared screen: caption, headline ("The game could not start" or "The game stopped with an error"), the reason or Lua's message wrapped to 8 lines, the `warning` icon, one option "« Back" with the `sign-out` icon. Unchanged (`story-title-screen-screenshots/continue-unreadable-error-view.png`).

## Do's and Don'ts

| Do | Don't |
|---|---|
| Ink and paper only; selection by the 25 % dither | Gray text, gray fills, or a denser dither behind text |
| A paper box under any runtime text drawn over a developer's image | Runtime text straight on `handoff.png` or `title.png` |
| The game's picture only in the splash band and the hand-off page | Game art in the launcher beyond the 64 px row icon, or on Options |
| FreeInkUI's header, list rows and option dialog as they are | A custom row or a hand-rolled hit area for any games screen |
| Centre the dialog caption, headline and the gap line | Mix left- and centre-aligned lines above a dialog's icon |
| Mark an unavailable game by its reason line | Dim or hide an unavailable game's row |
| Frames at `{rounded.md}`, selection at `{rounded.sm}` | Shadows, double frames, or square-cornered dialogs |
