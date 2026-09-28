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

Turns the API seed into the self-contained author docs: `docs/crosshatch/game-api.md`, a LuaLS `ch.d.lua` stub, and the icon catalog. An AI-authoring trial proves them. API level 1 stays a preview here; it freezes in epic-first-party-games (spine AD-19).

## Outcome

The author, or anyone with an AI assistant, writes a game from three files; CAP-9's success check is the signal.

## Requirements

Completed at inception. This epic owns CAP-9.

## Done when

1. `docs/crosshatch/game-api.md`, the LuaLS `ch.d.lua` stub, and the icon catalog describe all of API level 1 and match the runtime; the seed's tic-tac-toe example runs unmodified.
2. Given only those three files, an AI writes a new game that installs through the inbox and plays a round on a device.
3. The docs name no path or file outside themselves, so they move to a starter repo unchanged.
4. Merged to `develop`; `ch.d.lua`, the icon catalog, and `game-api.md` agree with `docs/crosshatch/api-level-1.txt`, which the host test checks.

## Boundaries

Author docs in `docs/crosshatch/` only. Not the byte-formats doc (epic-script-runtime) or the ledger doc (epic-platform-baseline). It catalogs the icon set; it does not pick it.

## References

- parent — _bmad-output/initiative-crosshatch-player-v1/initiative-crosshatch-player-v1.md, Requirements
- architecture — _bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/game-api-seed.md
- architecture — _bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md, AD-8, AD-19, AD-23, AD-24, Consistency Conventions (Docs)

## Notes

- hitl: a person runs the AI-authoring trial and plays the round.
- Waits on epic-icon-library because: the icon set to catalog.
- Waits on epic-install-and-launcher because: install through the inbox for the AI-authoring trial.
- Waits on epic-play-nearby because: the complete pass and nearby behaviour the docs describe (the seed's tic-tac-toe is `pass` and `nearby`).
- Decision: the API level 1 freeze moved from epic-game-api-docs to the end of this initiative, in epic-first-party-games's last ticket (owner, spine AD-19 update, 2026-09-27).
- Handoffs from epic-icon-library (owner's entry-8 review, 2026-09-28):
  - The icon catalog lists every library icon by its exact Phosphor 2.1.1 name, in both weights, with `ch.gfx.icon`'s sixth argument `weight` (`"regular"` default, `"fill"`), and links each name to the Phosphor site; the chess pieces are not in the library, so a chess game ships its own images (`ch.gfx.image`).
  - Retro R8: a game cannot force a refresh of an unchanged frame (`ch.gfx.refresh("full")` on an identical frame is skipped); one line in the API docs.
  - Retro R4: coordinates saturate to int16 when appended, before clipping; `api-level-1.txt` documents it as a comment outside the contract. Settle it (clip in wider integers, or a `limit` entry) before level 1 freezes.
