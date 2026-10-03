# Validation Report — crosshatch brand mark

- **DESIGN.md:** `/home/user/crosshatch-player/_bmad-output/planning-artifacts/ux-designs/ux-crosshatch-player-2026-10-03/DESIGN.md`
- **EXPERIENCE.md:** `/home/user/crosshatch-player/_bmad-output/planning-artifacts/ux-designs/ux-crosshatch-player-2026-10-03/EXPERIENCE.md`
- **Run at:** 2026-10-03

## Overall verdict
The pair is a sound, buildable contract for an identity-only spec. Every memlog decision I checked is present and none is contradicted: C2 final, Noto Sans Bold for the wordmark only, Ubuntu 10 bold device title, stock 120 px layout, 60% "Player", native 128 px icon, spacing rule, short/stacked/avatar placements, and the default icon not being an icon-library name. All contrast figures, luminances and the mark geometry recompute correctly. The weak spots are not in the decisions but in what is left unspecified or stated wrongly: the lockups (the main website deliverable) have no text-to-mark proportion or alignment, the boot-screen polarity is wrong against the firmware, "stroke weight" is claimed as an X/O cue but the geometry uses one stroke for both, and the sleep-mode table omits two real modes. Nothing is critical. One high finding, five medium.

This report was produced at Finalize: every finding below was resolved before the report was written, and each carries a one-line Resolution. Only the rubric walker ran as a reviewer lens; the bmad-review structure and prose polish pass also ran but wrote no file (see Polish pass).

## Category verdicts
- Flow coverage — strong
- Token completeness — adequate
- Component coverage — adequate
- State coverage — adequate
- Visual reference coverage — adequate
- Bloat & overspecification — adequate
- Inheritance discipline — strong
- Shape fit — strong

## Findings by severity

### Critical (0)
None.

### High (1)

**[Component coverage]** — Lockups specified by arrangement only (§ D 156-161, 183)
Missing: size ratio between mark and text, vertical alignment, stacked alignment and Crosshatch/Player gap, minimum size, and any 1-bit/greyscale variant. The lockups are the website deliverable and this is the only visual spec for them.
Fix: See Token completeness first finding; add one dimensioned lockup diagram or table to D Layout & Spacing.
Resolution: High lockup proportions: fixed (text 2/3 of mark height horizontal, 1/3 stacked, Player 60%, minimum mark 32 px, all gaps half a cell, owner-confirmed).

### Medium (6)

**[Token completeness]** — Lockup proportions not tokenised or stated (§ D 31, 165)
No wordmark size relative to the mark, no vertical alignment in horizontal lockups, no alignment or line gap in the stacked lockup, no minimum size. Rated high under Component coverage; listed here because the values belong in spacing/typography (cross-reference to the High finding, same issue).
Fix: Add lockup-text-height (fraction of the mark), alignment rule, stacked-line-gap, and a minimum lockup/mark size.
Resolution: High lockup proportions: fixed (text 2/3 of mark height horizontal, 1/3 stacked, Player 60%, minimum mark 32 px, all gaps half a cell, owner-confirmed).

**[State coverage]** — Sleep-mode rows omit real modes (§ E 103-105)
The enum has DARK, LIGHT, CUSTOM, COVER, COVER_CUSTOM, BLANK, QUICK_RESUME, TRANSPARENT_CUSTOM. QUICK_RESUME and TRANSPARENT_CUSTOM were read as never drawing the default screen, so a builder cannot tell whether they are exempt.
Fix: List every enum value against mark / no mark / fallback.
Resolution: Medium sleep modes omitted: fixed (every setting value mapped to mark / no mark / falls back; verified against onEnter; the reviewer was partly wrong that TRANSPARENT_CUSTOM never draws the default screen: it falls back to it when no valid overlay exists).

**[State coverage]** — Boot polarity wrong (§ D 175, E 106)
D says boot is inverted except when the sleep setting is LIGHT; BootActivity.cpp lines 13-19 never invert. E 90 limits inversion to sleep, E 106 is silent, so the pair disagrees.
Fix: Split the Polarity row: sleep inverted unless LIGHT, boot not inverted; add boot polarity to E 106.
Resolution: Medium boot polarity: fixed (boot is not inverted; sleep inverted unless LIGHT).

**[Visual reference coverage]** — Sleep mock shows the superseded design (§ mockups/sleep-screen.html, D 184, E 57)
Mock shows the L3 lockup, Noto Sans Bold 18 pt title and +51 small line; the decision is stock layout with Ubuntu 10 bold and +25. E 57 gives no caveat and calls it the device layout.
Fix: Put the caveat in E 57, or re-render the 120 px case with the stock values.
Resolution: Medium sleep mock superseded: fixed (banner in mockups/sleep-screen.html, caveat in both spines).

**[Mechanical notes]** — Bitmap derivation / ring-grid fuse (§ Mechanical notes, geometry check)
The spines say 64 px and 120 px were checked and clear (E 120) but never say how those bitmaps are produced from the SVG. The ring's outer edge (78) and the neighbouring grid stroke's edge (80) leave a 2-unit gap, under 1 px at 120 px and about 0.5 px at 64 px in 1-bit; the O can fuse into the grid.
Fix: State the derivation and record the rendered ring-to-grid gap in pixels at 120, 64 and 128.
Resolution: Medium bitmap derivation / ring-grid fuse: fixed as a builder acceptance check in Implementation notes.

**[Mechanical notes]** — Stroke weight cue unsupported by geometry (§ D 194, D 182, E 118)
The 'shape and weight' claim is not supported: X and O share stroke width 16 in the A2 symbol.
Fix: Drop 'weight' from all three places, or specify a weight difference in the geometry.
Resolution: Medium stroke weight cue: fixed (shape only; X and O share stroke 16).

### Low (11)

**[Token completeness]** — Spacing values that are not dimensions; non-spec typography note key (§ D 56-59, 28-43)
mark-cell, lockup-clear-space, lockup-mark-text-gap and stacked-second-line-scale are percentages without a base; typography entries use a non-spec note key and carry no fontSize or lineHeight.
Fix: Give percentages as numbers with a base, or move the rule to prose; use fontSize for device-title.
Resolution: Token completeness lows (percent bases, device-title lineHeight, grid/wordmark contrast, O hole wording): fixed.

**[Token completeness]** — Contrast stated for mark colours only (§ D 129)
Grid-on-surface and wordmark-on-surface are load-bearing. Recomputed: black on white 21.00:1, white on #121212 18.73:1.
Fix: State the two figures.
Resolution: Token completeness lows (percent bases, device-title lineHeight, grid/wordmark contrast, O hole wording): fixed.

**[Token completeness]** — O hole size ambiguous (§ D 185, E 120, M18)
'About 1 to 2 px' at 32 px; the inner diameter is 28 of 256 units, which is 3.5 px at 32 px (1.75 px is the radius).
Fix: Say radius or diameter, or quote the rasterised pixel count.
Resolution: Token completeness lows (percent bases, device-title lineHeight, grid/wordmark contrast, O hole wording): fixed.

**[Component coverage]** — Avatar has no surface, padding or crop rule (§ D 161, E 47)
Corner O rings sit about 113 units from centre plus 30 outer radius (143 of 128), so a circular crop would clip them.
Fix: Give the avatar a square surface with padding, or state it is a square asset.
Resolution: Avatar crop: fixed (square asset with one cell of padding).

**[Component coverage]** — Stacked-lockup gap is the writer's reading, stated as fact (§ D 165, M54, E 181)
The half-cell gap is not an owner decision, yet D states it as fact and E says Open Questions: None.
Fix: Confirm with the owner or list as open.
Resolution: Stacked gap: owner-confirmed.

**[Visual reference coverage]** — Missing inline links at Colors, Typography/Lockups and the import photo (§ D 122, D stock layout section)
D cites the colour page without a link, and links wordmark-variants and the photo nowhere inline.
Fix: Add inline links in D Colors, Layout lockups and the sleep table.
Resolution: Visual reference lows (inline links): fixed.

**[Bloat & overspecification]** — Same numbers stated up to three times (§ D 147-150, D 171-173, E 39-40, E 128, D 126/129, E 119)
Mark geometry, 120/70/95 and contrast figures repeat; drift risk, no added information.
Fix: Keep one canonical place and reference it.
Resolution: Bloat/drift lows: partly fixed (repeats removed; Do's table and decision history kept on purpose).

**[Bloat & overspecification]** — Implementation notes are repo fact with rot-prone line numbers (§ E 165-177)
Line numbers (619-632, 13-19, 612/791/801) were correct when checked, but the section belongs in a ticket.
Fix: Drop the line numbers, or move the section out of the spine.
Resolution: Bloat/drift lows: partly fixed (repeats removed; Do's table and decision history kept on purpose).

**[Bloat & overspecification]** — Decision history and Do's table restate other sections (§ D 178, 118/131, 129, 192-203, 136, E 120)
Superseded sizes, rejected colours and a Do's table that restates Colors and Typography; the 1-bit 'Player' at 14 px check is untied to a surface.
Fix: Trim to the rule plus one line of reason.
Resolution: Bloat/drift lows: partly fixed (repeats removed; Do's table and decision history kept on purpose).

**[Inheritance discipline]** — Screen size wording conflicts with the games spine (§ D 174, E 28)
E calls the devices 800 x 480 panels with a portrait 480 x 800 canvas; the games spine says 480 x 800 portrait throughout; code uses getScreenWidth()/getScreenHeight().
Fix: Use the games spine's wording and say once that the layout is relative to the screen.
Resolution: Screen wording: fixed.

**[Shape fit]** — No Inspiration section (§ M11, M15, M20, M37-39)
Memlog names reference and reject sources (Phosphor, CrossPoint's logo, chalk/pastel); material lives in D Brand & Style and Colors.
Fix: Add a three-line Inspiration section, or note in E Foundation that D carries it.
Resolution: Inspiration section: added.

## Polish pass (bmad-review structure, prose)
- 10 structure recommendations and 19 prose edits reviewed.
- Accepted: S1-S6 and P1-P17.
- Declined: moving State Patterns' icon-fallback rows (state coverage is a rubric requirement), cutting the Do's table or Key Flows, moving Implementation notes out of the spine, and one prose rewording that was imprecise about firmware behaviour.
- Word counts went from 5,404 to 4,928.

## Reviewer files
- `review-rubric.md`
