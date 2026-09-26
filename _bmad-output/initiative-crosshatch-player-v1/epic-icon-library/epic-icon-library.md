---
type: epic
title: "Games and runtime screens share one icon library"
parent: initiative-crosshatch-player-v1
covers: [CAP-8]
after: []
assignee: ""
risk: low
---

# Games and runtime screens share one icon library

## Description

Vendors the curated v1 icon set from Phosphor's fill weight, generates the 1-bit icon header, and adds `ch.gfx.icon` and `ch.gfx.image`. The runtime views and the Home cover-grid Games tile switch to the library. This epic picks the v1 icon list; it is part of API level 1.

## Outcome

Games and runtime screens look like one product, and a game can ship its own images; CAP-8's success check is the signal, with the launcher part closed by epic-install-and-launcher and first-party games by epic-first-party-games.

## Requirements

Completed at inception. This epic owns CAP-8, except the launcher rows (epic-install-and-launcher) and first-party game drawing (epic-first-party-games).

## Done when

1. The v1 icon set (marks, card suits, dice faces, board pieces, player markers, common controls) is vendored from Phosphor fill 2.1.1 with its licence and name map, and `scripts/gen_game_icons.py` regenerates `GameIcons.generated.h` byte-identically in the fork-only workflow, with `lib/GameIcons/.clang-format` keeping the formatter off it.
2. `ch.gfx.icon` draws every library icon at small, medium, and large, and an unknown name ends the session in the error view.
3. `ch.gfx.image` draws an image in the 1-bit layout `PngToBmpConverter` produces at its native size, and an unknown name ends the session in the error view.
4. The pause, error, and game-over views and the Home cover-grid Games tile draw from the library, and the icons' flash cost is measured and recorded.
5. Merged to `develop` with the five-env build, host suites, whole-tree format check, `pio check`, and the fork-only size and ledger jobs all green.

## Boundaries

`assets/game-icons/`, the generator, `lib/GameIcons`, the two bindings, and the image path in GameAssets. Owns ledger rows 8 and 9 (the cover-grid Games tile). Not the installer's PNG conversion (epic-install-and-launcher), which produces the `.bmp` files this epic reads.

Handoffs: epic-install-and-launcher converts `icon.png` and package PNGs to the `.bmp` layout this epic reads; the icon names are part of API level 1, catalogued by epic-game-api-docs and drawn by epic-first-party-games.

## References

- parent — _bmad-output/initiative-crosshatch-player-v1/initiative-crosshatch-player-v1.md, Requirements
- architecture — _bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md, AD-15, AD-19, AD-24
- architecture — _bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/game-api-seed.md, icons section

## Notes

- Decision: this epic picks the v1 icon set; epic-game-api-docs only catalogs it. AD-24 and the spec's Open Questions were amended to match (user's decision, 2026-09-26).
- Waits on epic-script-runtime because: the `ch.gfx` display list and binding registry, GameAssets, and the runtime views.
