---
type: epic
title: "API level 1 freezes and game authors get a starter repo"
parent: initiative-crosshatch-player-v1
covers: [CAP-1, CAP-12]
after: []
assignee: ""
risk: high
---

# API level 1 freezes and game authors get a starter repo

## Description

Settles every open item of the pre-freeze checklist in Notes, dry-runs the release pack of the real games, and sets `API_LEVEL_FROZEN`, which closes level 1; the first fork release from that commit is the freezing release (spine AD-19). Then it publishes a game starter repo against the frozen level: epic-game-api-docs's docs moved unchanged, a standalone packer, the first-party games as examples, and instructions an AI assistant follows, so a developer with an AI has their own game on their device the same day (spec CAP-12). Split from epic-first-party-games on 2026-10-04 (owner); the same day the nearby rounds moved to epic-play-nearby and the starter repo moved into v1 here.

## Outcome

Game authors outside this repository write against a frozen level 1 from a starter repo; the freezing release (AD-19) and CAP-12's success check are the signal.

## Requirements

Completed at inception. This epic closes CAP-1's versioned host API by freezing level 1 (epic-script-runtime built the host API itself) and owns CAP-12, the starter repo; epic-first-party-games owns CAP-1's "no game-specific firmware code" check, and epic-game-api-docs owns the docs the starter repo carries (CAP-9).

## Done when

1. Every open item of the pre-freeze checklist in Notes is either an entry in `docs/crosshatch/api-level-1.txt` with the test that pins it, or an owner Decision recorded here that leaves it a comment or out of level 1.
2. On the freezing commit, before the release, a `fork_release.py pack-games` dry run packs the real `games/<id>/`, including the `nearby` manifests epic-play-nearby added, matching `pack_game.py`'s packages byte for byte.
3. The freeze entry sets `API_LEVEL_FROZEN` in `lib/GameCore/ApiLevel.h`, the freeze job passes, and the first fork release from that commit is the freezing release, with the three `.chgame` files attached.
4. A public starter repo carries `game-api.md`, `ch.d.lua`, and the icon catalog byte-identical to `docs/crosshatch/` at the freezing commit, a packer whose packages match `pack_game.py`'s on `package_vectors.json`, at least one first-party game as an example, and instructions for an AI assistant, and names the freezing release as the firmware it targets.
5. Pointed only at the starter repo, an AI assistant writes a new game that packs with the repo's packer, installs through the inbox, and plays a round on an X4 Pro running the freezing release.
6. This repository's changes are merged to `develop` with the five-env build, host suites, whole-tree format check, `pio check`, and the fork-only size and ledger jobs all green.

## Boundaries

The level-1 entries the pre-freeze checklist settles, the freeze, and the starter repo with what it needs from this repository (a standalone packer, and a check that the repo's docs match `docs/crosshatch/`). Not the games or their rounds (epic-first-party-games, epic-play-nearby), though a fix in `games/<id>/` that the freeze or the starter repo needs is this epic's; not the nearby runtime (epic-play-nearby); not writing the author docs (epic-game-api-docs), which move unchanged. No API addition beyond the checklist's items; any other addition waits for level 2. Not a gallery, sharing between players, a browser playground, or a simulated radio link (spec Non-goals).

## References

- parent — _bmad-output/initiative-crosshatch-player-v1/initiative-crosshatch-player-v1.md, Requirements
- spec — _bmad-output/specs/spec-crosshatch-player/SPEC.md, CAP-1, CAP-9, CAP-12, and Non-goals
- spec — _bmad-output/specs/spec-crosshatch-player/first-party-games.md
- architecture — _bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md, AD-4, AD-13, AD-19, AD-24, Operational envelope (Flash budget), Deferred (Starter repo)
- constraint — docs/crosshatch/api-level-1.txt, its rules for entries and comments
- brief — _bmad-output/planning-artifacts/briefs/brief-crosshatch-player-2026-09-26/brief.md, Who This Serves (Later) and Vision, for history only (it put the starter repo out of v1 scope; SPEC CAP-12 supersedes it)
- input — _bmad-output/implementation-artifacts/cross-story-review-e3r.md, rows 8 and 9
- input — _bmad-output/initiative-crosshatch-player-v1/epic-script-runtime/epic-script-runtime-retrospective.md, AI-8
- input — _bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/epic-first-party-games.md, Notes, the 2026-10-04 split Decision and the handoff

## Notes

- Decision (owner, 2026-10-04): split from epic-first-party-games, which keeps the three games in solo and pass, the PR-time `games/` check, and the release assets, so it can merge alongside epic-play-nearby; this epic takes the nearby rounds, the pre-freeze checklist, the `pack-games` dry run, and the freeze. **Superseded in part 2026-10-04 by the build-order Decision below:** the nearby rounds are epic-play-nearby's.
- Waits on epic-play-nearby because: its level-1 changes land before the freeze, and the first-party games' `nearby` manifests are in `games/<id>/` before the dry run.
- Waits on epic-game-api-docs because: R4 and any level-1 entry it adds land before the freeze, and its docs are final against the list that freezes; the starter repo carries them unchanged.
- Waits on epic-first-party-games because: the three games in `games/<id>/` that the release attaches and the starter repo uses as examples, and the PR-time `games/` check that guards them.
- hitl: the owner decides each checklist item, makes the freezing release, and creates the starter repo on GitHub; a person runs Done when 5 on an X4 Pro.
- Budget: the checklist's entries add firmware code, so this epic needs a flash and static-RAM share before its first such entry. Epic-play-nearby holds 9,504 B / 160 B; the spine's Flash budget row (2026-10-02) left 11,536 B / 0 B unallocated and a 2,000 B / 48 B reserve, before epic-pass-and-play's 2026-10-03 raise to 21,000 B and its 40 B overage. Measure the base first.
- Decision: the API level 1 freeze moved from epic-game-api-docs to the end of this initiative, in epic-first-party-games's last ticket (owner, spine AD-19 update, 2026-09-27); the owner's 2026-10-04 split moved it here, to the freeze entry, which only the starter repo follows.
- Moved unchanged from epic-first-party-games's Notes by the 2026-10-04 split (their "this epic" is epic-first-party-games; read it as this one, and its "last ticket" as the freeze entry):
  - Before `API_LEVEL_FROZEN` flips in this epic's last ticket, settle (epic-icon-library retrospective AI-2, 2026-09-28): the epic-script-runtime retrospective's AI-8 (the surface test checks each `fn`'s arity and return type, or signatures leave the list; the stale-base window closed by up-to-date branches, a merge queue, or a `push: develop` run of the freeze check); retro R4 (int16 coordinate saturation; now in epic-game-api-docs's Notes); the per-frame drawing bound for `ch.gfx.image` and `ch.gfx.icon` (retrospective R1, AI-10) as a level-1 entry; the manifest `icon` grammar against the library's (retrospective R6); and the icon pixel sizes (32/64/128), the ink-only and opaque rules, and the image-name grammar as entries rather than comments (retrospective R9 d), with whether `ch.gfx.image_size` belongs in level 1.
  - Also before the freeze (the follow-up's cross-story review, `_bmad-output/implementation-artifacts/cross-story-review-e3r.md` rows 8 and 9, 2026-09-28): whether `limit frame_icon_image_pixels` charges only the host's own canvas (so the same game costs differently on a host with another canvas) or a canvas-independent area, and whether exact values such as `icon_small_side_pixels 32` need their own entry kind rather than `limit`, whose other entries are maximums.
  - Also before the freeze (epic-install-and-launcher's entry-14 device run, owner, 2026-09-30):
    - whether a per-frame budget covers filled `rect`, `clear`, and `circle` commands, as `frame_icon_image_pixels` covers icons and images. On an X4 Pro, 2,048 full-canvas filled rects replayed in 3,142 ms under `RenderLock`; the owner accepted that worst case for now (A16).
    - whether a package's members get a cap on their extracted total, or a free-space check from the directory pass's declared sizes (about 4 MB is possible today; `deferred-work.md` `## 4.13`, A19).
    - the Lua C-stack headroom: decided by the owner at that run. The 16 KB stack and the 2,048 B headroom stay, since the guard stopped with 1,412 B free (1,652 B at a hook). The decision reopens if a device log shows under 512 B free.
- Gating (owner's split, 2026-10-04): this epic waits on epic-play-nearby, epic-game-api-docs, and epic-first-party-games as whole epics (the initiative's `tickets.toml`). The reason: once `API_LEVEL_FROZEN` is set, `check_api_freeze.py` fails any change to `api-level-1.txt` (comments included), and the first release from that commit cannot be undone (`fork_release.py` `freeze_problems`); epic-game-api-docs may still add level-1 entries (its R4, Done when 1 and 4) and epic-play-nearby may change level-1 behaviour. The freeze entry follows every checklist entry, and the starter repo's publish entry follows the freezing release, so the repo targets a frozen level. This replaces epic-first-party-games's 2026-10-04 gating line, which pinned single entries inside that epic.
- Pre-freeze checklist (pre-inception audit, 2026-10-04; the status of each item in the moved lines above, plus what they missed):
  - Settled: the per-frame bound for icons and images (`limit frame_icon_image_pixels 1048576` in `api-level-1.txt`, e3r-1 `131fe505`; 1.69 s on an X4 Pro at epic-install-and-launcher entry 14); the icon pixel sizes, the ink and opaque rules, and the image-name grammar as entries (`icon_*_side_pixels`, `draw … ink|opaque`, `name image`); the manifest `icon` grammar (`name manifest_icon`, epic-install-and-launcher inception); AI-8's stale-base half, by the owner's F3 (epic-script-runtime retrospective): the audit read `strict_required_status_checks_policy: true` on develop's ruleset, and nothing else records it; the Lua C-stack headroom (decided at entry 14; it reopens if a device log shows under 512 B free, so a device run records the "VM stopped" high-water line: epic-first-party-games's solo and pass run first, epic-play-nearby's nearby rounds again).
  - Open: AI-8's arity half (`ApiSurfaceTest` checks no `fn` arity or return type; or the signatures move to `ch.d.lua`, epic-game-api-docs); `ch.gfx.image_size` and how a game learns the pixels it has left (`deferred-work.md` `## e3r-1`, fourth entry); `cross-story-review-e3r.md` rows 8 and 9 (canvas-relative charging; an exact-value entry kind, since `icon_*_side_pixels` are still `limit` entries); A16(b), a fill budget (the owner chose none for now; `DisplayList.h` charges no fills); A19, a cap on a package's extracted total; R4, int16 coordinate saturation, a comment in `api-level-1.txt` and listed in epic-game-api-docs's Notes (its one owner is settled at epic-game-api-docs's inception).
  - Added (owner, 2026-10-05): image cells (`ch.gfx.image`'s optional cell and the manifest `cells` key), a level-1 addition built in epic-game-api-docs (its Notes); open until its entries land there.
  - Not listed until now: behaviour games rely on that `api-level-1.txt` keeps only as comments (the file's own rule says such a rule becomes an entry before the freeze): the faults `pcall` cannot catch, the sealed string metatable, the 480 × 480 title and hand-off area, and canvas-relative charging; each becomes an entry or is accepted as a comment. The icon set closes at the freeze (AD-24 allows renames and removals only while level 1 is a preview); the audit found every icon the three games need in the set, to confirm at inception. Lua 5.5.1 stays (AD-4: a revert to 5.4.9 is cheapest before the freeze), to confirm. A `fork_release.py pack-games` dry run over the real `games/<id>/` on the freezing commit (Done when 2); epic-first-party-games's release (its Done when 4) is the first to pack the real games, so this run checks the games' nearby changes and the frozen commit.
- For inception (pre-inception audit, 2026-10-04):
  - The split settles the conflict between epic-first-party-games's "add nothing to the API" and the checklist's new entries: the entries are this epic's, under Done when 1.
- Decision (owner, 2026-10-04): the build order is epic-first-party-games, epic-play-nearby, epic-game-api-docs, then this epic. The first-party games' nearby rounds moved to epic-play-nearby, which adds `nearby` to their manifests, so this epic plays no nearby round; the starter repo (spec Non-goal until today, now CAP-12; spine Deferred row amended) moved into v1 here. This supersedes the split Decision above where they differ.
- Starter repo, for inception (owner's addition, 2026-10-04):
  - Open question: the repository's name and account, whether it is a GitHub template repository, and its license, which must cover the docs, the packer, the example games, and the Phosphor icons' MIT attribution in the catalog.
  - Open question: one source of truth for the docs. They stay in `docs/crosshatch/` (epic-game-api-docs's Done when 3 keeps them free of outside paths) and are copied at the freezing release and each later release; a check in this repository or the starter repo fails when the copies differ.
  - Open question: the packer. `scripts/pack_game.py` imports `fork_common`; make it standalone here so both repositories run the same file against `package_vectors.json`, or vendor a copy with that vectors test.
  - Unknown: how an author tests a game without a device. The desktop simulator needs a firmware build; a host runner (Lua 5.5.1 with a `ch` stub, the codec limits, and the instruction budget, like this repository's `LuaGameFixture`) is new work. Inception decides whether the starter repo ships one or points at the simulator.
  - Which first-party games go in as examples (all three, or Sudoku plus one two-player game), and the AI instructions file (`AGENTS.md`) the repo ships.
  - Access: agent sessions here are scoped to `crosshatch-player`, so building in the starter repo needs it attached to the session (or created by the owner first). Done when 5 is CAP-12's success check: another developer with an AI has a game on their device the same day.
- Pre-freeze item (owner, 2026-10-05, from epic-first-party-games): 466 × 788 is the default game canvas going forward (spine AD-7, amended 2026-10-05). Before the freeze, decide whether level 1 promises it: a minimum-canvas entry in `docs/crosshatch/api-level-1.txt` (pinned by a test against every games env's `GameViewport::forRenderer`), or a comment that leaves it a convention; and the starter repo's template lays out a 466 × 788 box centred in `ch.screen`, as the first-party games do.
