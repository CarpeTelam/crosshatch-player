# Spine Pair Review (rubric walker, Pass 1 only): crosshatch-player games

Run 2026-10-02, after the owner's 2026-10-02 decisions were applied to `DESIGN.md`, `EXPERIENCE.md` and the mocks. Only the Pass 1 coverage checks ran, as the owner chose (no Pass 2 judgment and no accessibility lens).

## Overall verdict

A consumer can source-extract the pair cleanly: every `{token}` reference resolves, every component has both a visual and a behavioural row, and every mock is linked inline. Two mechanical gaps were fixed in the spines. What remains open is low severity and needs an owner decision or a check against built behaviour, not a spine rewrite.

## 1. Flow coverage: adequate

Checked the epic's requirements named in the sources (R4, R5, R9, R16, R17) against Key Flows 1 to 3.

- R9 (3 taps, Continue, New, mode choice): Flows 1 and 3, with failure paths.
- R4, R5 (Result, hand-off, resumed match): Flow 2, with the sleep failure path.
- R16 (one row per game, title screen, selection after Leave, New over a save): spread across Flows 1 to 3 and their failures.

### Findings
- **low** R17 (the Play-again gap's "Starting the next round") has no Key Flow; it is covered only by Component Patterns (Pause menu) and State Patterns (Play-again gap). R5's hand-off before Play again is likewise not walked in Flow 2. *Fix:* optional: a Flow 2 step after "Game over" (Play again, Back in the gap, the line, then the hand-off). Open: not mechanical, a flow is a narrative choice.

## 2. Token completeness: strong

All 37 distinct `{path}` references in the two spines (frontmatter and prose) resolve to frontmatter tokens (checked by script). Colors carry hex; the only text combination (ink on paper, 21:1, and ink over the 25 % dither) is stated. New tokens: `typography.button-label`, `components.ready-button`, `components.forced-exit-blank`; `spacing.splash-band` is now 480px; `components.hand-off-text-box` and `components.sleep-blank` are gone with no references left.

### Findings
- **low** `components.ready-button` takes its size from existing tokens (`{spacing.view-width}` wide, `{spacing.option-row}` tall, middle at y = 400) and is drawn unfocused (paper inside). The owner decided "framed like a FreeInkUI option/button, in the middle of the screen", not its width, height, or whether it shows the focus dither (Confirm presses it). *Fix:* the owner confirms the size and the focus treatment, or the follow-up story's screenshot review settles them. Open.

## 3. Component coverage: strong (after one fix)

Every component named in either spine has a row in `DESIGN.md` Components and in `EXPERIENCE.md` Component Patterns: Header bar, List row, Launcher game row, Title-screen splash, Title-screen menu rows, Options row, Option / confirm dialog, Result banner, Hand-off screen (default; with a developer image), "I'm ready" button, Forced-exit blank, Pause menu, End-of-round menu, Error view.

### Findings
- **medium, fixed** "I'm ready" button had a `DESIGN.md` row but no `EXPERIENCE.md` Component Patterns row (its rules lived only inside the Hand-off screen row). *Fixed:* row added.
- **low** The game canvas (IA view "Canvas (Playing)") has no component row in either spine; it is the declared input exception owned by `docs/crosshatch/game-canvas.md`. Acceptable by reference; no fix.

## 4. State coverage: adequate (after one fix)

Walked every IA surface: Home, launcher, title screen, Options, New-over-save confirm, match and its views.

### Findings
- **medium, fixed** Launcher out of memory: the built launcher (`story-one-launcher-row-per-game-plan.md`, R16) shows "Not enough memory" when a registry load runs out of memory, never "No games found"; State Patterns had only the empty state. *Fixed:* "Launcher out of memory" state row added, as built.
- **low** Launcher remove progress and failure ("Removing...", "Could not remove it. Check the SD card.", `STR_GAMES_REMOVING`, `STR_GAMES_REMOVE_FAILED`) and the install note ("Installing games...", the note R16 names) have no state rows or Voice and Tone rows. They are built and unchanged by this design. *Fix:* add them as "as built" rows after checking the built behaviour (where the failure shows, what stays selected). Open: not invented here.
- **low** The fallback for an unknown or no-longer-startable remembered value still carries [ASSUMPTION] (memlog entry 29 is typed assumption and not superseded), though spine AD-17's owner amendment states the same rule. *Fix:* the owner confirms, and the tag goes. Open.

## 5. Visual reference coverage: strong

`mockups/`: key-title-screen, key-options, key-launcher, key-hand-off, key-gap-pause; `imports/` is empty; no `wireframes/`. Each is linked in both spines' preamble and inline at its component, naming the column it illustrates; "the spines win over any mock" is stated once per spine and in every mock. No orphans. The forced-exit blank is not drawn (a blank page; a wrapped fourth column did not fit the three-column layout), and `DESIGN.md` says so.

## Mechanical notes

- Removed everywhere: "Player N, tap when ready", the `eye-closed` icon on the hand-off and the sleep blank, the `handoff_text` manifest key, the hand-off text box, the 360 px band and `title.png` at 480 x 360, "Go on with the saved game" (kept only as the Built-today value), and the "Open details (assumptions)" section (folded into Typography, Layout, Components, Component Patterns, State Patterns, Voice and Tone).
- [ASSUMPTION] remains in two places only, both from memlog entries still typed assumption: the launcher modes line's order and startable-only filter (entry 22), and the remembered-value fallback (entry 29).
- Options row behaviour aligned with spine AD-17's amendment: the choice holds at once and is written when Options closes (it read "remembered at once" against State Patterns' "written when Options closes").
- Frontmatter `updated` set to 2026-10-02 in both spines; all mocks' `<div>` tags balance; renders checked at 1600 x 1100 with no overlap or cut-off frame.

## Resolution (owner approval, 2026-10-02)

The owner approved the design ("I approve, make it so!") with no change to the open items, so the facilitator's recommendations stand:

- The "I'm ready" button is `{spacing.view-width}` wide and `{spacing.option-row}` tall, drawn without the focus dither; Confirm presses it. Closed.
- No Key Flow is added for the Play-again gap; Component and State Patterns cover it. Closed.
- The launcher's remove progress and failure and the install note are unchanged from what is built and stay out of this design. Closed.
- The remembered-value fallback is decided (spine AD-17); its [ASSUMPTION] tag and the launcher modes line's are removed. Closed.
