---
type: epic
title: "Three first-party games ship with each release"
parent: initiative-crosshatch-player-v1
covers: [CAP-10, CAP-1]
after: []
assignee: ""
risk: low
---

# Three first-party games ship with each release

## Description

Writes Sudoku, Ultimate tic-tac-toe, and Battleship as script packages that use only API level 1, which the fork release workflow of AD-25 packs and attaches to each release.

## Outcome

Every release ships three games that together exercise every mode; CAP-10's success check and CAP-1's "no game-specific firmware code" check are the signal.

## Requirements

Completed at inception. This epic owns CAP-10 and closes CAP-1's success check.

## Done when

1. Sudoku (solo), Ultimate tic-tac-toe (pass, nearby), and Battleship (pass, nearby, hidden) are script packages in `games/<id>/`, and no game-specific C++ exists in the firmware.
2. Each packs with `pack_game.py`, installs through the inbox, and plays a round on a device in every mode it declares.
3. All three stay within API level 1 and the 1,400 B snapshot, add nothing to the API, and draw boards and markers from `GameIcons`.
4. A fork release made by the release workflow (AD-25) has the three `.cpgame` files attached, packed byte-for-byte from `games/<id>/`.
5. Merged to `develop` with the five-env build, host suites, whole-tree format check, `pio check`, and the fork-only size and ledger jobs all green.
6. The last ticket sets `API_LEVEL_FROZEN` in `lib/GameCore/ApiLevel.h`, closing v1; the first fork release from that commit is the freezing release (spine AD-19).

## Boundaries

`games/<id>/` and the shared 9×9 board module copied into two packages. Not the release workflow (epic-platform-baseline, AD-25), which already packs every `games/<id>/`; this epic adds no workflow. Not an enlarged-board view (later), nor any API addition.

## References

- parent — _bmad-output/initiative-crosshatch-player-v1/initiative-crosshatch-player-v1.md, Requirements
- spec — _bmad-output/specs/spec-crosshatch-player/first-party-games.md
- architecture — _bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md, AD-8, AD-10, AD-15, AD-22, AD-23, AD-24

## Notes

- Decision: release assets are attached by the fork release workflow of AD-25, owned by epic-platform-baseline, not an edit to upstream `release.yml` (user's decision, 2026-09-26).
- Decision: only the nearby-round entries wait on epic-play-nearby; Sudoku and the pass versions are built without it, and only this epic's closure waits for the second device (user's decision, 2026-09-26).
- hitl: a person plays each game in each mode on a device; a second device for the nearby rounds.
- Waits on epic-icon-library because: the icon set.
- Waits on epic-install-and-launcher because: packing and install through the inbox.
- Waits on epic-pass-and-play because: pass-and-play and the hand-off.
- Decision: the API level 1 freeze moved from epic-game-api-docs to the end of this initiative, in epic-first-party-games's last ticket (owner, spine AD-19 update, 2026-09-27).
- Before `API_LEVEL_FROZEN` flips in this epic's last ticket, settle (epic-icon-library retrospective AI-2, 2026-09-28): the epic-script-runtime retrospective's AI-8 (the surface test checks each `fn`'s arity and return type, or signatures leave the list; the stale-base window closed by up-to-date branches, a merge queue, or a `push: develop` run of the freeze check); retro R4 (int16 coordinate saturation; now in epic-game-api-docs's Notes); the per-frame drawing bound for `ch.gfx.image` and `ch.gfx.icon` (retrospective R1, AI-10) as a level-1 entry; the manifest `icon` grammar against the library's (retrospective R6); and the icon pixel sizes (32/64/128), the ink-only and opaque rules, and the image-name grammar as entries rather than comments (retrospective R9 d), with whether `ch.gfx.image_size` belongs in level 1.
