---
type: epic
title: "A sandboxed Lua game runs solo on the device"
parent: initiative-crosshatch-player-v1
covers: [CAP-1, CAP-2, CAP-7]
after: []
assignee: ""
risk: high
---

# A sandboxed Lua game runs solo on the device

## Description

Builds the script runtime: the sandboxed Lua VM on its own task, the one codec and its limits, the `ch.*` bindings except icons and images, the display list and refresh policy, per-game storage, and the match activity for solo play with its pause, error, and game-over views. A minimal Games list on Home reaches games placed on the SD card by hand.

## Outcome

A developer runs a solo script game on an X4 Pro and no script can crash or hang the device; the CAP-1 and CAP-2 success checks are the signal.

## Requirements

This epic owns CAP-1 (the host API), CAP-2 (fault isolation), and the solo part of CAP-7 (pause, leave, play again). CAP-1's "no game-specific firmware code" check is closed by epic-first-party-games. Lines without a CAP id cite their source section.

- R1 (CAP-1): `main.lua` returns the AD-8 table; in solo the runtime calls `setup(ctx)` with `ctx = {seats = 1, mode = "solo", api}`, then `status`, `apply`, `draw`, and `input`, and delivers `tap`, `long_press`, `swipe`, `rejected`, `over`, and `timer` events. (spine AD-8, AD-9, AD-11)
- R2 (CAP-1): `ch` holds `api`, `screen`, `gfx` (`clear`, `rect`, `line`, `circle`, `text`, `refresh`), `text_width`, `timer` (`after`, `cancel`), `store` (`get`, `set`), `time.ms`, and `log`, with `print` mapped to `ch.log`; `ch.gfx.icon` and `ch.gfx.image` belong to epic-icon-library. (spine AD-7, AD-17, AD-19, AD-23; game-api-seed section 5)
- R3 (CAP-1): `ch.gfx` builds a display list of at most 2,048 commands or 32 KB, swapped under a frame mutex; `FrameReplay` is the only refresh policy, and `GameViewport` maps both drawing and touch. (spine AD-7)
- R4 (CAP-2): one `GameVM` task owns the Lua state under a 256 KiB heap cap, in a 448 KiB Lua region beside a 16 KiB reserve, one 464 KiB PSRAM block (amended 2026-09-28, retro AI-10); every entry goes through a `lua_pcall` trampoline under a sticky 2 M instruction budget; libraries and text-only loading follow AD-6; a stop is cooperative, with abandon 500 ms after cancel. (spine AD-5, AD-6)
- R5 (CAP-2): one C codec encodes state, moves, and `ch.store` as codec v1 bytes with limits of 1,400, 256, and 4,096 B in every mode; `scripts/game_codec.py` passes the same golden vectors; `docs/crosshatch/formats.md` records the bytes. (spine AD-10)
- R6 (CAP-2): each `ScriptError` this epic can raise ends the session in the error view with Back; `Cancelled` shows nothing, except the 3 s watchdog's cancel, which shows the error view (amended 2026-09-28, retro AI-10). (spine AD-5, AD-14)
- R7 (CAP-7, solo): the match follows AD-21's solo states Starting, Playing, Paused, Over, Error, and Leaving (Starting amended 2026-09-28, retro AI-10); Back and Home open the pause menu; sleep takes AD-20's forced exit; the canvas exception is recorded in `docs/crosshatch/`. (spine AD-20, AD-21)
- R8 (CAP-1): `GameSaveStore` is the only reader and writer of `/.games-data/<id>/store.bin`, written from the loop task at most every 5 s, at round end, and in `onExit()`. (spine AD-17)
- R9 (CAP-1): `Manifest::parse` and `Manifest::check(hostCaps)` are the one manifest reader; `HostCaps` comes from one `src/games` provider; a minimal Home → Games list (ledger rows 4 to 7, list mode, the first `STR_GAMES_*` keys) opens games placed by hand in `/.games/<id>/`. (spine AD-3, AD-15, AD-22)
- R10 (spine AD-19): `lib/GameCore/ApiLevel.h`, `docs/crosshatch/api-level-1.txt`, a host test of the live `ch` surface against it, a fork CI job that guards frozen levels, and release notes that name the level. (spine AD-19; deferred-work)
- R11 (SPEC Constraints, internal RAM): the flash budget job also fails when the x4pro `.dram0.data` + `.dram0.bss` + `.noinit` grow over 1 KiB with games on; a check fails game objects with a static initializer or a mutable static over 64 B; the epic records its flash and RAM deltas. (spine AD-2, Operational envelope; retro AI-9; deferred-work)
- R12 (spine Consistency Conventions, Fork scripts): `scripts/fork_common.py` and one CI step that runs every `scripts/*_test.py` exist before this epic's first new fork script. (spine Consistency Conventions; retro AI-5)
- R13 (spine AD-25): `ForkRelease::ASSET_NAME_CAPACITY` is mirrored in the version vectors and tied to `OtaUpdater.cpp`'s buffer, and the release probe's timeout, redirect limit, TLS mode, and user agent are recorded beside ledger row 10, before the first fork release after this epic. (spine AD-25; deferred-work, release and update-source chore)

## Done when

1. A solo fixture game, placed by hand as `/.games/<id>/` with no `.pkg`, is reached from Home → Games in the list theme and played to game over on an X4 Pro, using drawing, text, tap, long press, swipe, refresh hints, and `ch.timer`, with `ch.store` surviving a restart.
2. A Lua error, an infinite loop, heap exhaustion of the 256 KiB Lua heap cap (in the 464 KiB arena; amended 2026-09-28, retro AI-10), an oversized state (over 1,400 B), move (over 256 B), or `ch.store` (over 4 KB), a binary chunk at load, frame-buffer overflow, and an invalid `status` each end the session in the error view with Back, in host tests and on the device, and the device stays responsive.
3. The C codec and `scripts/game_codec.py` pass the same golden vectors, and the byte formats are recorded in `docs/crosshatch/formats.md`.
4. Back and Home open the pause menu, Leave returns to Games, Play again restarts a solo round, and sleep takes the forced exit without deadlock; the canvas exception to `touch-and-ui.md` is recorded in `docs/crosshatch/`.
5. Merged to `develop` with the five-env build, host suites, whole-tree format check, `pio check`, and the fork-only size, RAM, static-initializer, ledger, API freeze, and fork-script test jobs all green.
6. The fork CI fails a PR whose games-on x4pro internal RAM grows over 1 KiB, whose game objects carry a static initializer or a mutable static over 64 B, or that changes a frozen API level's list; the level-1 surface test passes; a release dry run's notes name "Game API 1 (preview)".
7. `ForkRelease::ASSET_NAME_CAPACITY` has at-capacity and one-over vectors and a guarded `static_assert` on `OtaUpdater.cpp`'s buffer, and the probe's values are recorded beside ledger row 10.

## Boundaries

`lib/GameScript`, the `GameCore` ports, `Session` for one seat, `Manifest::parse`, and in `src/games` the GameVM task, FrameReplay, GameViewport, the arena backend, the GameAssets loader, and GameSaveStore for `store.bin`. `GameMatchActivity` with the Playing, Paused, Over, Error, and Leaving states. Owns ledger rows 4 to 7 (the Home menu item and `goToGames()`) and the first `STR_GAMES_*` keys (row 2). Not the installer, `.pkg`, paging, Continue, or remove (epic-install-and-launcher); not `ch.gfx.icon` or `ch.gfx.image` (epic-icon-library); not pass or nearby. Also the fork CI and release gates pulled from deferred-work: `scripts/fork_common.py` and the fork-script test step, the static RAM and static-initializer checks, `ApiLevel.h` and the API freeze job, the level in `fork_release.py`'s notes, and the asset-name capacity and probe values of AD-25.

Handoffs: the minimal Games list is replaced by the full launcher in epic-install-and-launcher; `Manifest::parse` is reused by the installer and the Nearby lobby; `Session` is extended to N seats in epic-pass-and-play; GameSaveStore gains `resume.bin` in epic-install-and-launcher; the `ch.gfx` binding registry and runtime views are extended by epic-icon-library.

## References

- parent — _bmad-output/initiative-crosshatch-player-v1/initiative-crosshatch-player-v1.md, Requirements
- architecture — _bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md, AD-1, AD-4 to AD-10, AD-14, AD-17, AD-19 to AD-21, AD-23
- architecture — _bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/game-api-seed.md
- architecture — _bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md, AD-2, AD-15, AD-25, Consistency Conventions, Operational envelope
- deferred — _bmad-output/implementation-artifacts/deferred-work.md, spine rules ahead of the code
- retrospective — _bmad-output/initiative-crosshatch-player-v1/epic-platform-baseline/epic-platform-baseline-retrospective.md, AI-3, AI-5, AI-8, AI-9
- constraint — docs/contributing/touch-and-ui.md
- constraint — docs/activity-manager.md

## Notes

- Decision: E2 reaches games through a minimal Home → Games list of installed directories; epic-install-and-launcher replaces it (user's decision, 2026-09-26).
- Waits on epic-platform-baseline because: build guard, vendored Lua, test subdirectories, and the ledger.
- Decision: inception wrote 16 entries in `tickets.toml`, 17 with entry 17 below; the count is above the typical 8 to 12 because the deferred-work gates (entries 2, 3, 5, 14, 17) run in their own lane beside the runtime, and the epic stays one PR (user's decision, 2026-09-27).
- Decision: entry 1 is the tracer bullet: Home → Games → a hand-placed Lua game drawn by the `GameVM` task through `FrameReplay`, and a tap through `GameViewport` to `input`, in the simulator and on an X4 Pro (2026-09-27).
- Decision: two lanes. CI and scripts run 2 → 3 → 5 → 17; the runtime runs 1 → 6 → 7 → 8 → 9 → 10 → 11 → 12 → 13, with 4 after 1 and 14 after 11. Entries in one lane touch shared files in order (2026-09-27).
- Decision: pulled from deferred-work into this epic: every spine rule it assigns to epic-script-runtime (entries 3, 4, 5, 10, 14), release chore items 1 (entry 5) and 2 and 3 (entry 17), retro AI-3 (entry 5), retro AI-9 (entry 1), and retro AI-5, moved from epic-install-and-launcher because `game_codec.py` and the freeze check are the next fork scripts (entry 2) (user's decision, 2026-09-27).
- Decision: left out: the epic-icon-library items, pointed to from that epic's Notes, and retro AI-2, which the owner settled by host tests (2026-09-27).
- Decision: `HostCaps.nearby` is false on every build until epic-play-nearby turns it on, and always false under `SIMULATOR` (2026-09-27).
- Decision: `api-level-1.txt` lists only what this epic ships; epic-icon-library appends the icon entries and the surface test's icon-table check, and epic-game-api-docs adds the `ch.d.lua` and catalog checks, while level 1 is a preview (user's decision, 2026-09-27).
- Decision: fixture games live in `test/game_script/fixtures/`, never `games/`, which the release workflow packs (2026-09-27).
- Decision: the default refactor sweep is entry 15, and a closing device run is entry 16 (2026-09-27).
- Decision: entry 17 takes release chore items two and three (the asset-name capacity and the probe's recorded values), because the owner makes a fork release once this epic merges; it follows entry 5, and the sweep now waits on it (user's decision, 2026-09-27).
- Decision: a script's C recursion must not overflow the 16 KB `GameVM` stack (a nested `pcall` costs about 784 B, a recursive `__index` about 256 B, against Lua's 200-level limit); entry 7 adds a stack-headroom check to the hook it installs, raising a `ScriptError` below about 2 KB free, with a nested-`pcall` and a recursive-`__index` fixture; the stack stays 16 KB in internal RAM and AD-5 is unchanged (user's decision, 2026-09-27).
- Decision: `Manifest::parse` caps `name` at 64 B, `version` at 32 B, and `icon` at 32 B of `[a-z0-9_]`; entry 4 lists them as level-1 limits in `api-level-1.txt` (user's decision, 2026-09-27).
- Decision: a manifest with a duplicate known key is invalid, so `pack_game.py` rejects duplicates too (user's decision, 2026-09-27).
- Decision: after the 2.7 review, entry 7 also adds a 3 s wall-clock watchdog on each callback (cancel, abandon after 500 ms, error view), Lua build defines `LUAI_MAXCCALLS`, `MAXCCALLS`, and `l_randomizePivot` in `lib/lua/library.json`, `setmetatable` refusing `__gc`, count limits on `table.move`/`insert`/`remove`, and a no-op `print` until entry 10; the spine's AD-4, AD-5, and AD-6 carry the amendments, and `api-level-1.txt` changes `lib setmetatable` (user's decision, 2026-09-27).
- Decision: after 2.8, Lua gets its own arena region so the 256 KiB count cap (`lua_heap_bytes`, counted in the bytes Lua requests) always ends a heap bomb, on the host and the device alike: `LUA_REGION_BYTES` 448 KiB (the cap times 1.6 for 16 B block headers on 40 B objects, plus a fragmentation margin measured over 17 heap bombs at 16 B and 8 B headers), beside a 16 KiB reserve for the Session and codec scratch that `luaAlloc` never touches; both are slices of one 464 KiB `ARENA_BYTES` PSRAM block, freed at once (user's decision, 2026-09-27).
- Decision: the closing device run (entry 16) passed on an X4 Pro with firmware 6634faae: the owner played the `solo` fixture from Home to Games to game over, again after a restart with `ch.store` kept, used the pause menu from Back and Home, Resume, Leave, Play again, and sleep during play, and ran every `loop`, `limits`, and `faults/` fixture to the error view with Back to Games and the device responsive, confirming Done when 1, 2, and 4 (user's report, 2026-09-27).
- Epic flash and RAM deltas (x4pro, games on minus games off, measured by the flash budget job's commands at the refactor sweep, ecba20a9, and green in CI on 6634faae): flash +150,448 B (146.9 KiB of the 250 KiB budget), the raw games-on minus games-off figure, measured from epic 1's −15,136 B, so the runtime itself adds about 165,584 B (baseline stated 2026-09-28, retro AI-10); static internal RAM (`.dram0.data` + `.dram0.bss` + `.noinit`) +8 B of the 1,024 B limit; 35 game objects checked, none with a static initializer or a mutable static over 64 B. At run time a match takes one 464 KiB PSRAM arena (448 KiB Lua region plus a 16 KiB reserve), two 32 KiB frame buffers, the loaded sources, and a 4 KiB store slot, all in PSRAM (2026-09-27).
