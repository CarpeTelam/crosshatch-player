---
type: epic
title: "An AI writes a working game from the docs alone"
parent: initiative-crosshatch-player-v1
covers: [CAP-9]
after: []
assignee: ""
risk: low
---

# An AI writes a working game from the docs alone

## Description

Turns the API seed into the self-contained author docs: `docs/crosshatch/game-api.md`, a LuaLS `ch.d.lua` stub, and the icon catalog. An AI-authoring trial proves them. API level 1 stays a preview here; it freezes in epic-api-freeze (spine AD-19).

## Outcome

The author, or anyone with an AI assistant, writes a game from three files; CAP-9's success check is the signal.

## Requirements

Completed at inception. This epic owns CAP-9.

## Done when

1. `docs/crosshatch/game-api.md`, the LuaLS `ch.d.lua` stub, and the icon catalog describe all of API level 1 and match the runtime; the seed's tic-tac-toe example runs unmodified.
2. Given only those three files, an AI writes a new game that installs through the inbox and plays a round on a device.
3. The docs name no path or file outside themselves, so they move to a starter repo unchanged.
4. Merged to `develop`; `ch.d.lua`, the icon catalog, and `game-api.md` agree with `docs/crosshatch/api-level-1.txt`, which the host test checks.
5. **Added 2026-10-05 (owner):** Sudoku draws all its note digits from one cell sheet through `ch.gfx.image`'s cell (Notes, image cells), replacing its separate same-size note images, and plays a round on a device.

## Boundaries

Author docs in `docs/crosshatch/` only. Not the byte-formats doc (epic-script-runtime) or the ledger doc (epic-platform-baseline). It catalogs the icon set; it does not pick it. **Amended 2026-10-05 (owner):** also the image-cells addition to level 1 (Notes), firmware and packer included.

## References

- parent — _bmad-output/initiative-crosshatch-player-v1/initiative-crosshatch-player-v1.md, Requirements
- architecture — _bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/game-api-seed.md
- architecture — _bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md, AD-8, AD-19, AD-23, AD-24, Consistency Conventions (Docs)

## Notes

- hitl: a person runs the AI-authoring trial and plays the round.
- Waits on epic-icon-library because: the icon set to catalog.
- Waits on epic-install-and-launcher because: install through the inbox for the AI-authoring trial.
- Waits on epic-play-nearby because: the complete pass and nearby behaviour the docs describe (the seed's tic-tac-toe is `pass` and `nearby`).
- Decision: the API level 1 freeze moved from epic-game-api-docs to the end of this initiative, in epic-first-party-games's last ticket (owner, spine AD-19 update, 2026-09-27). **Amended 2026-10-04 (owner):** the freeze is epic-api-freeze's freeze entry, which only its starter repo follows.
- Handoffs from epic-icon-library (owner's entry-8 review, 2026-09-28):
  - The icon catalog lists every library icon by its exact Phosphor 2.1.1 name, in both weights, with `ch.gfx.icon`'s sixth argument `weight` (`"regular"` default, `"fill"`), and links each name to the Phosphor site; the chess pieces are not in the library, so a chess game ships its own images (`ch.gfx.image`).
  - Retro R8: a game cannot force a refresh of an unchanged frame (`ch.gfx.refresh("full")` on an identical frame is skipped); one line in the API docs.
  - Retro R4: coordinates saturate to int16 when appended, before clipping; `api-level-1.txt` documents it as a comment outside the contract. Settle it (clip in wider integers, or a `limit` entry) before level 1 freezes.
- Handoff (pre-inception audit, 2026-10-04; the freeze moved to epic-api-freeze by the owner's split the same day): epic-api-freeze, which holds the freeze, waits on this epic (the initiative's `tickets.toml`), so R4 above and any level-1 entry this epic adds land before `API_LEVEL_FROZEN`; AI-8's alternative (signatures moved to `ch.d.lua`) is this epic's if chosen. R4's single owner, and whether AI-8 takes the `ch.d.lua` route, are settled at this epic's inception, since epic-api-freeze starts only after this epic closes. This epic's docs name no path outside themselves (Done when 3), so they need nothing from `games/`; this epic must not wait on epic-api-freeze, which would make a cycle. Since the owner's 2026-10-04 build order it follows epic-first-party-games through epic-play-nearby, so the first-party games exist while these docs are written; the docs still name no path outside themselves (Done when 3), because epic-api-freeze moves them unchanged into the starter repo (CAP-12).
- Decision (owner, 2026-10-05): image cells join level 1 in this epic, queued for its inception and built before its docs are final, so `game-api.md`, `ch.d.lua`, and the AI-authoring trial (Done when 2) cover them; epic-api-freeze's pre-freeze checklist lists them open until they land (spine AD-24 amendment). A package image may be a sheet of equal cells, and `ch.gfx.image` draws one cell of it, so a game with many same-size pieces (a 52-card deck) fits `MAX_IMAGES` and the member cap. Proposed shape (architect, 2026-10-05), settled at inception:
  - `manifest.json` gains an optional `cells` object mapping an image name to its `[width, height]` cell size in pixels. `pack_game.py` and the installer reject a sheet whose size is not an exact multiple of its cell, and a `cells` name that is not one of the package's game images.
  - `ch.gfx.image(name, x, y, color, cell?)` takes an optional 1-based cell, counted left to right, then top to bottom; without it the whole image draws, as today. A cell given for an image with no `cells` entry, or outside its sheet, is a script error. `frame_icon_image_pixels` charges the cell, not the sheet.
  - Runtime: the cell in `DisplayList`'s image command and its hash; a source origin and size in `GameImageBlit::runs` (it reads pixels through `blackAt`, so a cell needs no byte alignment); the cell size beside `ImageSpan`. Contract: `api-level-1.txt` entries (the `manifest` key, the `fn` signature, the cell limits) with `API_SURFACE_CRC` and the surface tests; `package_vectors.json`; `formats.md`; a fixture in `test/game_script/fixtures/`.
  - Not pixel source rectangles passed by scripts: the install-time check catches a bad sheet before any match, and authors pick cells by number, not by magic pixel offsets (CAP-9).
  - Test case (owner, 2026-10-05): Sudoku's digit notes, 27 bold 12 × 16 px images in three sets of nine (epic-first-party-games, the owner's 2026-10-05 three-sets Decision and its entry 9), become one 108 × 48 px sheet: 9 columns (digits 1 to 9) by 3 rows (G, B, H, in that order), so a note is cell `(set - 1) * 9 + digit`. The G and H tiles carry a 50% checker that must match the screen's dither phase where each lands; a cell's checker is laid out from its own corner, and 12 and 16 are even, so every cell keeps the phase it has as a separate image and the slot positions entry 9 settles carry over unchanged. The sheet removes the `images_count` pressure that made the three-sets Decision reuse H, drawn `"white"`, for a shaded cell's focused digit; a fourth row for that look is the owner's call at inception, not part of this addition. The refactor is this epic's, in `games/sudoku/` (Done when 5), and cuts Sudoku's package by 26 members. It changes Sudoku's package hash, so a player's Sudoku saves from an earlier fork release are discarded (spec Non-goals: no save migration); land it before the freezing release, which epic-api-freeze's gating already ensures.
  - Size: one story, or two (package side, runtime side). Budget: it adds firmware code, so it needs a flash and static-RAM share before it starts; measure the base first (epic-api-freeze's Budget note).
