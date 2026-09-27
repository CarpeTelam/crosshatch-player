- source_plan: `_bmad-output/initiative-crosshatch-player-v1/epic-platform-baseline/story-games-build-flag-empty-game-libraries-and-host-suites-plan.md`
  summary: `sim.sh build` does not notice that `simulator.ini` changed since `sim.sh setup`, so a clone set up before a flag change builds the simulator envs from a stale `platformio.local.ini` block.
  evidence: `cmd_build` runs `pio run` directly; `cmd_setup` is the only writer of the managed block. After this story, a stale block lacks `-DFREEINK_CAP_GAMES=1`, so `simulator_x4pro` silently builds without the game libraries until `sim.sh setup` is re-run.

- source_plan: `_bmad-output/initiative-crosshatch-player-v1/epic-platform-baseline/story-upstream-touch-ledger-and-its-ci-job-plan.md`
  summary: Add a one-line AGENTS.md Policy pointer that an upstream-file change needs a row in `docs/crosshatch/upstream-touches.md` and that `scripts/check_upstream_touches.py` checks it locally.
  evidence: Review pass 1 (blind hunter): agents learn about the ledger only when the CI job fails; AGENTS.md is agent-context, so the edit is deferred rather than patched.

- source_plan: `_bmad-output/initiative-crosshatch-player-v1/epic-platform-baseline/story-fork-update-source-plan.md`
  summary: Verify on a device that the games-enabled update check maps a 404 from the fork's releases/latest to "no update", a network failure to "failed", and offers then stops offering a `-ch.N` release.
  evidence: Review pass 1 (verification-gap): `OtaUpdater.cpp` and `ForkReleaseProbe` are excluded from the host suite and the simulator, so the guarded wiring and the probe are covered only by builds, `strings` checks, and a live `curl` showing the 404 today; the fork release story's device check is the natural place.

- source_plan: `_bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md` (AD-19 and retro1 update, 2026-09-27; reviews `review-ad19-verify.md`, `review-ad19-adversary.md`)
  summary: Spine rules that are ahead of the code, with owners. epic-script-runtime creates them as part of its breakdown; the rest is one pre-epic chore or the named epic.
  evidence: |
    epic-script-runtime:
    - `lib/GameCore/ApiLevel.h` (`API_LEVEL`, `API_MIN_LEVEL`, `API_LEVEL_FROZEN`, `API_SURFACE_CRC`); `ch.api`; `ctx.api`.
    - `docs/crosshatch/api-level-1.txt` (typed entries) and the host test over the live `ch` table, globals, enums, limits, icon table, `ch.d.lua`, and catalog.
    - `crosshatch-ci.yml` job: fail a PR that changes a frozen level's list against the merge base or flips `API_LEVEL_FROZEN` back.
    - `GameCore::HostCaps {api, minApi, maxSeats, nearby}` from one `src/games` provider; `Manifest::check` statuses per AD-15; `nearby` false under `SIMULATOR`.
    - `fork_release.py`: read `ApiLevel.h` from the released commit; notes print "Game API <n> (preview)"; after v1, preflight refuses `API_LEVEL_FROZEN` false.
    - Flash budget job: static RAM gate on `.dram0.data` + `.dram0.bss` + `.noinit` from the x4pro ELF (1 KiB, tests, fresh-clone run); record each epic's flash and RAM delta.
    - A CI check that game objects define no static initializer (`.init_array`) and no mutable static over 64 B.
    Release and update-source chore (before the next release that matters):
    - `crosshatch-release.yml`/`fork_release.py`: read `fork_version_vectors.json` from the released commit (fix the comment at `crosshatch-release.yml:51-52`).
    - `ForkRelease::ASSET_NAME_CAPACITY` (48) plus a vectors field both tests read, a one-over vector, and a guarded `static_assert` on `OtaUpdater.cpp`'s `assetName`.
    - Record the probe's timeout, redirect limit, TLS mode, and user agent beside row 10 in `docs/crosshatch/upstream-touches.md`, add `[base]` `build_flags` and the `freeink-sdk` pointer to its triggers, and fix the probe comment that says it matches `HttpDownloader` (it uses SDK defaults: 15 s, no redirects).
    epic-icon-library:
    - Un-ignore `lib/GameIcons/GameIcons.generated.h` in `.gitignore`, add `lib/GameIcons/.clang-format` (`DisableFormat`), and narrow AGENTS.md's "every `pio run` regenerates `*.generated.h`" line.
    epic-install-and-launcher (retro AI-5):
    - `pack_game.py` is the only Python manifest reader and each Python-enforced limit is a named constant mirrored in vectors.
    - (moved 2026-09-27) `scripts/fork_common.py` and one CI step that runs every `scripts/*_test.py`: now epic-script-runtime entry 2, because `game_codec.py` lands there first.
  placed: |
    2026-09-27, epic-script-runtime inception (`epic-script-runtime/tickets.toml`):
    - ApiLevel.h, ch.api, ctx.api: entries 4, 8, 10. api-level-1.txt: entry 4; its host test: entry 14 (icon-table check handed to epic-icon-library, ch.d.lua and catalog checks to epic-game-api-docs).
    - Frozen-level CI job and fork_release.py reading the level: entry 5. HostCaps, Manifest::check, nearby under SIMULATOR: entry 4.
    - Static RAM gate and the static-initializer / 64 B check: entry 3; each epic's flash and RAM delta: entries 1 and 16.
    - Release and update-source chore, first item (vectors read from the released commit, the crosshatch-release.yml:51-52 comment): entry 5. Items two and three (ASSET_NAME_CAPACITY; the probe values beside row 10): entry 17, before the first fork release after epic-script-runtime merges.
    - Retro AI-5 (fork_common.py, the all-tests step): entry 2. Retro AI-3 (`fr build` against clean-on-checksum): entry 5.
    - Epic-icon-library items: pointed to from that epic's Notes.
    - The 1.6 device check above (retro AI-2): closed by the owner's decision, 2026-09-27: the host tests are enough for the 404 and network-failure rows for now; no device check is scheduled.

- source_plan: `_bmad-output/initiative-crosshatch-player-v1/epic-script-runtime/story-fork-script-helper-and-one-test-step-plan.md`
  summary: No test asserts that `fork_release.py notes` writes its `## Fork release <tag>` section to the job summary.
  evidence: `PublishTest` in `scripts/fork_release_test.py` covers `render_notes` and the unchecked-firmware error but never sets `GITHUB_STEP_SUMMARY` for `notes`; dropping the `write_step_summary` call would pass every test. Pre-existing; `fork_common_test.py` covers the helper itself.
- source_plan: `_bmad-output/initiative-crosshatch-player-v1/epic-script-runtime/story-tracer-home-to-games-to-a-lua-frame-and-a-tap-plan.md`
  summary: GameVM, GameAssets, GamesListActivity, and GameMatchActivity have no automated test; the task lifecycle (input then draw, quit and join, notify after exit), the dir/id filter, and the error and out-of-memory paths are checked only by simulator runs.
  evidence: src/games needs FreeRTOS and Storage, which no host suite provides; entry 2.1's plan records the simulator runs. A host harness over the simulator's FreeRTOS shim, or a scripted simulator run in CI, would pin them (candidate for the refactor sweep, entry 15).
- source_plan: `_bmad-output/initiative-crosshatch-player-v1/epic-script-runtime/story-tracer-home-to-games-to-a-lua-frame-and-a-tap-plan.md`
  summary: The tracer's minimal error screen shows English detail strings from GameAssets and GameMatchActivity ("out of memory", "game folder missing") and does not wrap a long Lua message.
  evidence: GameMatchActivity::renderError draws errorDetail on one line with drawText; entry 13's AD-14 error view replaces it and should map load failures to tr() text and wrap the message.
- source_plan: `_bmad-output/initiative-crosshatch-player-v1/epic-script-runtime/story-tracer-home-to-games-to-a-lua-frame-and-a-tap-plan.md`
  summary: The skeleton libraryName() functions in lib/GameCore/GameCore.* and lib/GameScript/GameScript.* (and their tests) can go now that both libraries have real sources.
  evidence: Their comments say they exist only until the library has other source files; GamesBuildAnchor.cpp would then include a real header from each library (refactor sweep, entry 15).
- source_plan: `_bmad-output/initiative-crosshatch-player-v1/epic-script-runtime/story-api-level-hostcaps-and-manifest-check-plan.md`
  summary: The Games list's `Manifest::check` filter (keep `Ok` games with solo startable) and `gameHostCaps()` have no automated test; dropping the solo test or turning `nearby` on in the simulator would pass every suite.
  evidence: `GamesListActivity` and `src/games` need Storage and the device build, which no host suite provides; entry 2.4's plan records the simulator runs (an `api = 2` and a pass-only scratch game both skipped with their reasons). Same harness gap as entry 2.1's deferred item.
- source_plan: `_bmad-output/initiative-crosshatch-player-v1/epic-script-runtime/story-api-level-hostcaps-and-manifest-check-plan.md`
  summary: `lib/GameScript/CanvasClip.h` and `test/game_script/CanvasClipTest.cpp` (from dca1ddc2) are not clang-format 21 clean, so CI's whole-tree format check fails until someone runs `./bin/clang-format-fix` on them.
  evidence: `./bin/clang-format-fix` realigns the trailing comments in `outlineEdges` and one `EXPECT_FALSE` line; entry 2.4 reverted those edits because story 2.6 owns `lib/GameScript/` and `test/game_script/`.
- source_plan: `_bmad-output/initiative-crosshatch-player-v1/epic-script-runtime/story-codec-and-golden-vectors-plan.md`
  summary: The codec's C stack use at its depth limit (16 nested tables, encode and decode) on the 16 KB `GameVM` stack is not measured.
  evidence: Unverified, medium if it is too deep: by reading, about 200 B a level plus `std::sort`, so about 3.2 KB at depth 16, against entry 7's rule of raising below about 2 KB free. Entry 8, which first runs the codec on the task, settles it by logging the stack high-water mark after encoding and decoding a depth-16 state.
- source_plan: `_bmad-output/initiative-crosshatch-player-v1/epic-script-runtime/story-sandbox-budgets-and-the-heap-cap-plan.md`
  summary: Owner decision needed on C recursion the entry-7 hook cannot see: the Lua parser, the pattern matcher, and `__gc` finalizers can still overflow the 16 KB `GameVM` stack (or, for `__gc`, escape the budget), so a crafted game can still crash the device.
  evidence: Measured in entry 7 (host x86-64 painted stack; ESP32-S3 `-Os -fstack-usage` frames): 199 nested functions need about 95 KB on the host, 199 `do` blocks 63 KB (S3 frames about 224 B a level), 199 parentheses 45 KB, all below Lua's own 200-level limit; `('a?'):rep(199)` in `string.find` needs about 19 KB on the host (S3 `match` 48 B a level, so about 9.5 KB, which overflows from within 2 KB of the guard); `lgc.c:980` turns hooks off inside `__gc`, so a looping `__gc` escapes the budget (on the device abandon still ends it after Leave) and a recursing one escapes the headroom check. Options: `-DLUAI_MAXCCALLS=<n>` and `-DMAXCCALLS=<m>` in `lib/lua/library.json`, sized from a device run (both are `#if !defined` knobs Lua documents as "must be compatible with the size of the C stack"; an AD-4 spine change, Lua stays byte-for-byte); a `setmetatable` that refuses a metatable with `__gc` (changes `lib setmetatable` in `api-level-1.txt`, which says unmodified); or accept and document. Recommended: both defines and the `__gc` refusal, before API level 1 freezes.
- source_plan: `_bmad-output/initiative-crosshatch-player-v1/epic-script-runtime/story-sandbox-budgets-and-the-heap-cap-plan.md`
  summary: Run the `loop` fixture (`test/game_script/fixtures/loop/`) on an X4 Pro and record each band's outcome and the "VM stopped" line's stack high-water mark, including the "Stuck in one C call" band, which is the only device test of `GameVM::abandon`.
  evidence: `GameVM` and `abandon` are not in the host suite; the simulator cannot suspend or delete a thread, so it leaks the stuck VM instead, and it builds at `-O0`, where one `pcall` level passes the whole 2 KB headroom (its hook low-water line read 0 B), so the device numbers are the evidence for the owner's stack decision.
