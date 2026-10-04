---
type: epic
title: "The first-party games play nearby and API level 1 freezes"
parent: initiative-crosshatch-player-v1
covers: [CAP-10, CAP-1]
after: []
assignee: ""
risk: high
---

# The first-party games play nearby and API level 1 freezes

## Description

Plays Ultimate tic-tac-toe and Battleship, from epic-first-party-games's `games/<id>/`, in Play Nearby on two X4 Pros; settles every open item of the pre-freeze checklist in Notes; dry-runs the release pack of the real games; and sets `API_LEVEL_FROZEN`, which closes v1. The first fork release from that commit is the freezing release (spine AD-19). Split from epic-first-party-games (owner, 2026-10-04).

## Outcome

The first-party games play in every mode they declare, and API level 1 is frozen for every game written against it; CAP-10's nearby rounds and the freezing release (AD-19) are the signal.

## Requirements

Completed at inception. This epic owns CAP-10's nearby rounds and closes CAP-1's versioned host API by freezing level 1; epic-first-party-games owns the games, their solo and pass rounds, and CAP-1's "no game-specific firmware code" check.

## Done when

1. Ultimate tic-tac-toe and Battleship each finish a round in Play Nearby on two X4 Pros running one build, from the packages in `games/<id>/`, with Battleship's boards hidden from the other player's screen.
2. Every open item of the pre-freeze checklist in Notes is either an entry in `docs/crosshatch/api-level-1.txt` with the test that pins it, or an owner Decision recorded here that leaves it a comment or out of level 1.
3. `fork_release.py pack-games` packs the real `games/<id>/` in a dry run before the freezing release, matching `pack_game.py`'s packages byte for byte.
4. The last entry sets `API_LEVEL_FROZEN` in `lib/GameCore/ApiLevel.h`, the freeze job passes, and the first fork release from that commit is the freezing release, with the three `.chgame` files attached.
5. Merged to `develop` with the five-env build, host suites, whole-tree format check, `pio check`, and the fork-only size and ledger jobs all green.

## Boundaries

The nearby rounds of the first-party games, the level-1 entries the pre-freeze checklist settles, and the freeze. Not the games themselves, their solo and pass rounds, or the PR-time `games/` check (epic-first-party-games); not the nearby runtime (epic-play-nearby); not the author docs (epic-game-api-docs). No API addition beyond the checklist's items; any other addition waits for level 2.

## References

- parent — _bmad-output/initiative-crosshatch-player-v1/initiative-crosshatch-player-v1.md, Requirements
- spec — _bmad-output/specs/spec-crosshatch-player/SPEC.md, CAP-1 and CAP-10
- spec — _bmad-output/specs/spec-crosshatch-player/first-party-games.md
- architecture — _bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md, AD-4, AD-13, AD-19, AD-24, Operational envelope (Flash budget)
- constraint — docs/crosshatch/api-level-1.txt, its rules for entries and comments
- input — _bmad-output/implementation-artifacts/cross-story-review-e3r.md, rows 8 and 9
- input — _bmad-output/initiative-crosshatch-player-v1/epic-script-runtime/epic-script-runtime-retrospective.md, AI-8
- input — _bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/epic-first-party-games.md, Notes

## Notes

- Decision (owner, 2026-10-04): split from epic-first-party-games, which keeps the three games in solo and pass, the PR-time `games/` check, and the release assets, so it can merge alongside epic-play-nearby; this epic takes the nearby rounds, the pre-freeze checklist, the `pack-games` dry run, and the freeze.
- Waits on epic-play-nearby because: nearby play for the rounds, and its level-1 changes land before the freeze.
- Waits on epic-game-api-docs because: R4 and any level-1 entry it adds land before the freeze, and its docs are final against the list that freezes.
- Waits on epic-first-party-games because: the three games in `games/<id>/` that the rounds play and the release attaches.
- hitl: two X4 Pros running one build for the nearby rounds; the owner decides each checklist item and makes the freezing release.
- Budget: the checklist's entries add firmware code, so this epic needs a flash and static-RAM share before its first such entry. Epic-play-nearby holds 9,504 B / 160 B; the spine's Flash budget row (2026-10-02) left 11,536 B / 0 B unallocated and a 2,000 B / 48 B reserve, before epic-pass-and-play's 2026-10-03 raise to 21,000 B and its 40 B overage. Measure the base first.
- Decision: the API level 1 freeze moved from epic-game-api-docs to the end of this initiative, in epic-first-party-games's last ticket; the 2026-10-04 split moved it to this epic's last entry (owner, spine AD-19 update, 2026-09-27).
- Moved unchanged from epic-first-party-games's Notes by the 2026-10-04 split (their "this epic" is epic-first-party-games; read it as this one):
  - Before `API_LEVEL_FROZEN` flips in this epic's last ticket, settle (epic-icon-library retrospective AI-2, 2026-09-28): the epic-script-runtime retrospective's AI-8 (the surface test checks each `fn`'s arity and return type, or signatures leave the list; the stale-base window closed by up-to-date branches, a merge queue, or a `push: develop` run of the freeze check); retro R4 (int16 coordinate saturation; now in epic-game-api-docs's Notes); the per-frame drawing bound for `ch.gfx.image` and `ch.gfx.icon` (retrospective R1, AI-10) as a level-1 entry; the manifest `icon` grammar against the library's (retrospective R6); and the icon pixel sizes (32/64/128), the ink-only and opaque rules, and the image-name grammar as entries rather than comments (retrospective R9 d), with whether `ch.gfx.image_size` belongs in level 1.
  - Also before the freeze (the follow-up's cross-story review, `_bmad-output/implementation-artifacts/cross-story-review-e3r.md` rows 8 and 9, 2026-09-28): whether `limit frame_icon_image_pixels` charges only the host's own canvas (so the same game costs differently on a host with another canvas) or a canvas-independent area, and whether exact values such as `icon_small_side_pixels 32` need their own entry kind rather than `limit`, whose other entries are maximums.
  - Also before the freeze (epic-install-and-launcher's entry-14 device run, owner, 2026-09-30):
    - whether a per-frame budget covers filled `rect`, `clear`, and `circle` commands, as `frame_icon_image_pixels` covers icons and images. On an X4 Pro, 2,048 full-canvas filled rects replayed in 3,142 ms under `RenderLock`; the owner accepted that worst case for now (A16).
    - whether a package's members get a cap on their extracted total, or a free-space check from the directory pass's declared sizes (about 4 MB is possible today; `deferred-work.md` `## 4.13`, A19).
    - the Lua C-stack headroom: decided by the owner at that run. The 16 KB stack and the 2,048 B headroom stay, since the guard stopped with 1,412 B free (1,652 B at a hook). The decision reopens if a device log shows under 512 B free.
- Gating (owner's split, 2026-10-04): this epic waits on epic-play-nearby, epic-game-api-docs, and epic-first-party-games as whole epics (the initiative's `tickets.toml`). The reason: once `API_LEVEL_FROZEN` is set, `check_api_freeze.py` fails any change to `api-level-1.txt` (comments included), and the first release from that commit cannot be undone (`fork_release.py` `freeze_problems`); epic-game-api-docs may still add level-1 entries (its R4, Done when 1 and 4) and epic-play-nearby may change level-1 behaviour. The freeze is this epic's last entry, with `after` on every other entry. This replaces epic-first-party-games's 2026-10-04 gating line, which pinned single entries inside that epic.
- Pre-freeze checklist (pre-inception audit, 2026-10-04; the status of each item in the moved lines above, plus what they missed):
  - Settled: the per-frame bound for icons and images (`limit frame_icon_image_pixels 1048576` in `api-level-1.txt`, e3r-1 `131fe505`; 1.69 s on an X4 Pro at epic-install-and-launcher entry 14); the icon pixel sizes, the ink and opaque rules, and the image-name grammar as entries (`icon_*_side_pixels`, `draw … ink|opaque`, `name image`); the manifest `icon` grammar (`name manifest_icon`, epic-install-and-launcher inception); AI-8's stale-base half, by the owner's F3 (epic-script-runtime retrospective): the audit read `strict_required_status_checks_policy: true` on develop's ruleset, and nothing else records it; the Lua C-stack headroom (decided at entry 14; it reopens if a device log shows under 512 B free, so this epic's device run records the "VM stopped" high-water line).
  - Open: AI-8's arity half (`ApiSurfaceTest` checks no `fn` arity or return type; or the signatures move to `ch.d.lua`, epic-game-api-docs); `ch.gfx.image_size` and how a game learns the pixels it has left (`deferred-work.md` `## e3r-1`, fourth entry); `cross-story-review-e3r.md` rows 8 and 9 (canvas-relative charging; an exact-value entry kind, since `icon_*_side_pixels` are still `limit` entries); A16(b), a fill budget (the owner chose none for now; `DisplayList.h` charges no fills); A19, a cap on a package's extracted total; R4, int16 coordinate saturation, a comment in `api-level-1.txt` and listed in epic-game-api-docs's Notes (its one owner is set at this epic's inception).
  - Not listed until now: behaviour games rely on that `api-level-1.txt` keeps only as comments (the file's own rule says such a rule becomes an entry before the freeze): the faults `pcall` cannot catch, the sealed string metatable, the 480 × 480 title and hand-off area, and canvas-relative charging; each becomes an entry or is accepted as a comment. The icon set closes at the freeze (AD-24 allows renames and removals only while level 1 is a preview); the audit found every icon the three games need in the set, to confirm at inception. Lua 5.5.1 stays (AD-4: a revert to 5.4.9 is cheapest before the freeze), to confirm. A `fork_release.py pack-games` dry run over the real `games/<id>/` before the freezing release, since that path has never packed a real game.
- For inception (pre-inception audit, 2026-10-04):
  - The split settles the conflict between epic-first-party-games's "add nothing to the API" and the checklist's new entries: the entries are this epic's, under Done when 2.
  - Nearby rounds play only on two X4 Pros running one build (the simulator compiles nearby out, and a level-1 surface CRC mismatch aborts the match, AD-13).
  - The nearby rounds may come first, pinned only on epic-play-nearby's work; only the checklist and the freeze need epic-game-api-docs.
  - Battleship in `nearby` carries both fleets in the shared state and only `draw` hides the other seat's ships, which the spec accepts; its placement through the per-seat `ui` is untested until this epic.
