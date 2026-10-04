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

Writes Sudoku, Ultimate tic-tac-toe, and Battleship as script packages that use only API level 1, which the fork release workflow of AD-25 packs and attaches to each release, and a PR-time check over `games/<id>/`. Their nearby rounds and the API level 1 freeze are epic-api-freeze's (split 2026-10-04).

## Outcome

Every release ships three games that a player installs and plays solo and in pass-and-play, hidden included; CAP-10's success check for those modes and CAP-1's "no game-specific firmware code" check are the signal (the nearby rounds close CAP-10 in epic-api-freeze).

## Requirements

Completed at inception. This epic owns CAP-10 except the nearby rounds (epic-api-freeze) and closes CAP-1's success check ("no game-specific firmware code"); epic-api-freeze closes CAP-1's versioned API by freezing level 1.

## Done when

1. Sudoku (solo), Ultimate tic-tac-toe (pass, nearby), and Battleship (pass, nearby, hidden) are script packages in `games/<id>/`, and no game-specific C++ exists in the firmware.
2. Each packs with `pack_game.py`, installs through the inbox, and plays a round on a device in every mode it declares except `nearby` (epic-api-freeze plays the nearby rounds).
3. All three stay within API level 1 and the 1,400 B snapshot, add nothing to the API, and draw boards and markers from `GameIcons`.
4. A fork release made by the release workflow (AD-25) has the three `.chgame` files attached, packed byte-for-byte from `games/<id>/`.
5. A PR that breaks a game in `games/<id>/` (it fails to pack, to load, or to play a scripted round within the snapshot limit and the instruction budget) fails a check before merge.
6. Merged to `develop` with the five-env build, host suites, whole-tree format check, `pio check`, and the fork-only size and ledger jobs all green.

## Boundaries

`games/<id>/` and the shared 9×9 board module copied into two packages. Not the release workflow (epic-platform-baseline, AD-25), which already packs every `games/<id>/`; this epic adds no release workflow; the PR-time check over `games/<id>/` (Done when 5) is a host test target or a `crosshatch-ci.yml` job, decided at inception. It may touch `test/CMakeLists.txt` (ledger row 1), `test/game_script/`, and `crosshatch-ci.yml`'s `needs`, which epic-play-nearby also changes while both run. Not the nearby rounds, the pre-freeze level-1 entries, or the freeze (epic-api-freeze). Not an enlarged-board view (later), nor any API addition.

## References

- parent — _bmad-output/initiative-crosshatch-player-v1/initiative-crosshatch-player-v1.md, Requirements
- spec — _bmad-output/specs/spec-crosshatch-player/SPEC.md, CAP-1 and CAP-10
- spec — _bmad-output/specs/spec-crosshatch-player/first-party-games.md
- architecture — _bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md, AD-8, AD-10, AD-15, AD-22, AD-23, AD-24, AD-25

## Notes

- Decision: release assets are attached by the fork release workflow of AD-25, owned by epic-platform-baseline, not an edit to upstream `release.yml` (user's decision, 2026-09-26).
- Decision: only the nearby-round entries wait on epic-play-nearby; Sudoku and the pass versions are built without it, and only this epic's closure waits for the second device (user's decision, 2026-09-26). **Superseded 2026-10-04 by the split below:** the nearby rounds are epic-api-freeze's.
- hitl: a person plays each game in solo and pass on a device (the nearby rounds and their second device are epic-api-freeze's).
- Waits on epic-icon-library because: the icon set.
- Waits on epic-install-and-launcher because: packing and install through the inbox.
- Waits on epic-pass-and-play because: pass-and-play and the hand-off.
- Decision: the API level 1 freeze moved from epic-game-api-docs to the end of this initiative, in epic-first-party-games's last ticket (owner, spine AD-19 update, 2026-09-27). **Amended 2026-10-04 (owner):** the freeze is epic-api-freeze's last entry, split from this epic.
- Decision (owner, 2026-10-04): this epic is split. It keeps the three games in solo and pass, the PR-time check over `games/<id>/` (new Done when 5), and the release assets; epic-api-freeze (epic 9) takes the nearby rounds, the pre-freeze checklist, the `pack-games` dry run, and the freeze (the old Done when 6), and waits on epic-play-nearby, epic-game-api-docs, and this epic. This epic's `after` is back to epics 3, 4, and 5, so it runs alongside epic-play-nearby and merges without waiting on it; it changes no firmware and adds nothing to `api-level-1.txt`, so it needs no flash share and does not collide with epic-play-nearby's `API_SURFACE_CRC`. The freeze notes, the gating line, and the pre-freeze checklist that were here moved to `epic-api-freeze.md`.
- For inception (pre-inception audit, 2026-10-04; feasibility read at `9cfcea88`, no API change needed for the games themselves):
  - No PR check packs or runs `games/<id>/`: `crosshatch-game-packages.yml` packs fixtures only and host suites read only `test/game_script/fixtures`, so a broken game first fails at release. Decide a host target over `games/` (load each game, scripted rounds, the snapshot limit and instruction budget) and a pack dry run, and whether that counts as "a workflow".
  - Battleship's two 10 × 10 boards cannot both use icons (32, 64, or 128 px only; two stacked boards need about 37 px cells): toggle one board at a time, or draw the own board with rects, and read Done when 3's "from `GameIcons`" accordingly. Placement runs as phases (seat 1, then seat 2), since `status` names one turn seat.
  - Sudoku's pencil marks do not fit as 3 × 3 small text in a 50 px cell: dots, or package images; there is no `ch.text_height` (adding it is a level-1 change).
  - Snapshots need packed strings (estimates: Ultimate tic-tac-toe 138 B, Sudoku 524 to 640 B, Battleship 530 B, against 1,400 B).
  - Deferred entries whose triggers fire here: `## e5-close` (5.13's device check; V3, which Battleship's placement phase reaches), `## e6pre-13` (B7.6), `## e5-r5` (the ghost after a sleep from Over), `## e5-r2` (the dropped-touch log line), and `## owner-e4-games-cap`; `## e6pre-5` (the concurrency group on a real PR) is epic-play-nearby's.
- Handoff to epic-api-freeze (2026-10-04): it takes the three packages in `games/<id>/` and the PR-time `games/` check from this epic, and its nearby rounds play them. This epic's solo and pass device run records the "VM stopped" stack high-water line the Lua C-stack decision reopens on (under 512 B free).
