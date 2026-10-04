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
4. A fork release made by the release workflow (AD-25) has the three `.chgame` files attached, packed byte-for-byte from `games/<id>/`.
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
- Also before the freeze (the follow-up's cross-story review, `_bmad-output/implementation-artifacts/cross-story-review-e3r.md` rows 8 and 9, 2026-09-28): whether `limit frame_icon_image_pixels` charges only the host's own canvas (so the same game costs differently on a host with another canvas) or a canvas-independent area, and whether exact values such as `icon_small_side_pixels 32` need their own entry kind rather than `limit`, whose other entries are maximums.
- Also before the freeze (epic-install-and-launcher's entry-14 device run, owner, 2026-09-30):
  - whether a per-frame budget covers filled `rect`, `clear`, and `circle` commands, as `frame_icon_image_pixels` covers icons and images. On an X4 Pro, 2,048 full-canvas filled rects replayed in 3,142 ms under `RenderLock`; the owner accepted that worst case for now (A16).
  - whether a package's members get a cap on their extracted total, or a free-space check from the directory pass's declared sizes (about 4 MB is possible today; `deferred-work.md` `## 4.13`, A19).
  - the Lua C-stack headroom: decided by the owner at that run. The 16 KB stack and the 2,048 B headroom stay, since the guard stopped with 1,412 B free (1,652 B at a hook). The decision reopens if a device log shows under 512 B free.
- Gating (pre-inception audit, 2026-10-04): the initiative's `tickets.toml` now lists epic-play-nearby and epic-game-api-docs in this epic's `after`. Sudoku and the pass versions still start without them (the 2026-09-26 Decision above). `tickets.py` does not enforce the initiative's `after`, and this file's frontmatter `after` would hold every entry, so at inception each nearby-round entry and the freeze entry carry their own `after` on epic-play-nearby's entries; the freeze is its own last entry, with `after` on epic-game-api-docs's entries and on every open item of the checklist below. The reason: once `API_LEVEL_FROZEN` is set, `check_api_freeze.py` fails any change to `api-level-1.txt` (comments included), and the first release from that commit cannot be undone (`fork_release.py` `freeze_problems`); epic-game-api-docs may still add level-1 entries (its R4, Done when 1 and 4) and epic-play-nearby may change level-1 behaviour.
- Pre-freeze checklist (pre-inception audit, 2026-10-04; the status of each item in the three lines above, plus what they missed):
  - Settled: the per-frame bound for icons and images (`limit frame_icon_image_pixels 1048576` in `api-level-1.txt`, e3r-1 `131fe505`; 1.69 s on an X4 Pro at epic-install-and-launcher entry 14); the icon pixel sizes, the ink and opaque rules, and the image-name grammar as entries (`icon_*_side_pixels`, `draw … ink|opaque`, `name image`); the manifest `icon` grammar (`name manifest_icon`, epic-install-and-launcher inception); AI-8's stale-base half, by the owner's F3 (epic-script-runtime retrospective): the audit read `strict_required_status_checks_policy: true` on develop's ruleset, and nothing else records it; the Lua C-stack headroom (decided at entry 14; it reopens if a device log shows under 512 B free, so this epic's device run records the "VM stopped" high-water line).
  - Open: AI-8's arity half (`ApiSurfaceTest` checks no `fn` arity or return type; or the signatures move to `ch.d.lua`, epic-game-api-docs); `ch.gfx.image_size` and how a game learns the pixels it has left (`deferred-work.md` `## e3r-1`, fourth entry); `cross-story-review-e3r.md` rows 8 and 9 (canvas-relative charging; an exact-value entry kind, since `icon_*_side_pixels` are still `limit` entries); A16(b), a fill budget (the owner chose none for now; `DisplayList.h` charges no fills); A19, a cap on a package's extracted total; R4, int16 coordinate saturation, a comment in `api-level-1.txt` and listed in epic-game-api-docs's Notes (its one owner is set at this epic's inception).
  - Not listed until now: behaviour games rely on that `api-level-1.txt` keeps only as comments (the file's own rule says such a rule becomes an entry before the freeze): the faults `pcall` cannot catch, the sealed string metatable, the 480 × 480 title and hand-off area, and canvas-relative charging; each becomes an entry or is accepted as a comment. The icon set closes at the freeze (AD-24 allows renames and removals only while level 1 is a preview); the audit found every icon the three games need in the set, to confirm at inception. Lua 5.5.1 stays (AD-4: a revert to 5.4.9 is cheapest before the freeze), to confirm. A `fork_release.py pack-games` dry run over the real `games/<id>/` before the freezing release, since that path has never packed a real game.
- For inception (pre-inception audit, 2026-10-04; feasibility read at `9cfcea88`, no API change needed for the games themselves):
  - Done when 3 and the Boundaries ("add nothing to the API", "nor any API addition") conflict with the checklist's new entries; the audit suggests "the games need no addition" plus a pre-freeze story that may change the list, and a flash and static-RAM share for it (11,536 B unallocated; the reserve is 2,000 B / 48 B).
  - No PR check packs or runs `games/<id>/`: `crosshatch-game-packages.yml` packs fixtures only and host suites read only `test/game_script/fixtures`, so a broken game first fails at release. Decide a host target over `games/` (load each game, scripted rounds, the snapshot limit and instruction budget) and a pack dry run, and whether that counts as "a workflow".
  - Battleship's two 10 × 10 boards cannot both use icons (32, 64, or 128 px only; two stacked boards need about 37 px cells): toggle one board at a time, or draw the own board with rects, and read Done when 3's "from `GameIcons`" accordingly. Placement runs as phases (seat 1, then seat 2), since `status` names one turn seat.
  - Sudoku's pencil marks do not fit as 3 × 3 small text in a 50 px cell: dots, or package images; there is no `ch.text_height` (adding it is a level-1 change).
  - Snapshots need packed strings (estimates: Ultimate tic-tac-toe 138 B, Sudoku 524 to 640 B, Battleship 530 B, against 1,400 B).
  - Nearby rounds play only on two X4 Pros running one build (the simulator compiles nearby out, and a level-1 surface CRC mismatch aborts the match, AD-13).
  - Deferred entries whose triggers fire here: `## e5-close` (5.13's device check; V3, which Battleship's placement phase reaches), `## e6pre-13` (B7.6), `## e5-r5` (the ghost after a sleep from Over), `## e5-r2` (the dropped-touch log line), `## e6pre-5` (the concurrency group on a real PR), and `## owner-e4-games-cap`.
