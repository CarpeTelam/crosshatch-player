---
title: 'Nearby on the X4 Pro only until a Sticky run'
type: 'feature'
ticket: '20'
created: '2026-10-13'
status: done
route: 'oneshot'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
followup_review_recommended: false
context: []
warnings: []
deferred: []
baseline_revision: 'b56b7645512bea8e3914b38ca75ba8f6feccd72c'
---

<intent-contract>

## Intent

**Problem:** Entry 11 turned `NEARBY_BUILT` on for every games board, Sticky included, and no Sticky has run Play Nearby. The owner's Decision of 2026-10-13 (epic Notes, "Nearby on the X4 Pro only") offers it on the X4 Pro only until a Sticky device run.

**Approach:** `gameHostCaps()` reports nearby on for the X4 Pro device build (`FREEINK_DEVICE_X4PRO`) and for the simulator (`SIMULATOR`, R16, as entry 16 built it), and off on every other games board, so `Manifest::check` keeps a declared `nearby` unavailable there. The device flag stays one `constexpr` expression in `GameHostCaps.cpp`, with a comment naming the Decision and what lifts it. A host test pins the caps per board, each built with that board's define.

</intent-contract>

## Tasks & Acceptance

- `src/games/GameHostCaps.cpp` -- `NEARBY = IS_SIMULATOR || (IS_X4PRO && NEARBY_BUILT)`, `IS_X4PRO` from `FREEINK_DEVICE_X4PRO`; comment names the Decision and the lift (a Sticky device run).
- `src/games/GameHostCaps.h` -- comments only: `NEARBY_BUILT` is the X4 Pro's answer (and the harness double's default).
- `test/game_script/harness/game_host_caps_boards.cmake` + `GameHostCapsBoardsTest.cpp` -- new: one executable per board (X4 Pro, Sticky, no device define, simulator on X4 Pro, simulator on Sticky), each built with that board's defines (platformio.ini; simulator.ini), pinning nearby and what `Manifest::check` offers a three-mode game.
- `test/game_core/GameHostCapsTest.cpp` -- the shared copy has no device define (stands for any other games board): nearby off, a declared `nearby` left out.
- `test/game_script/harness/installer.cmake` -- `game_installer_src` is the X4 Pro host (`FREEINK_DEVICE_X4PRO=1`, PRIVATE), so the installer and games-check suites keep their nearby-on host.
- Comments in `HostCapsScript.h`, `GameHostCapsSimulatorTest.cpp`, `game_host_caps_simulator.cmake` follow.

## Implementation Notes

Oneshot: about 100 changed lines, one expression and its tests, no new surface. Decisions:

- The define naming the X4 Pro is `FREEINK_DEVICE_X4PRO` (platformio.ini `[env:x4pro]` and the `x4pro-gh_release*` envs; `simulator_x4pro` in `.claude/skills/run-crosshatch-player/simulator.ini`). The games boards are `x4pro*` and `sticky*` only (`FREEINK_CAP_GAMES` in platformio.ini). The gate is positive (X4 Pro), so a future games board is off until the owner says otherwise.
- `simulator_sticky` defines both `SIMULATOR` and `FREEINK_DEVICE_STICKY`; the simulator reports nearby on there too ("the simulator's nearby must stay as entry 16 built it"; the orchestrator's brief).
- `NEARBY_BUILT` keeps its name and value (`true`): the harness double (`HostCapsScript.h`) defaults to it as the stand-in for the X4 Pro, and `ModePickerTest.HostCapsDouble` pins that. The X4 Pro board test pins the real function to the same value.
- `test/game_core/CMakeLists.txt` is outside the entry's `touches`, so it is untouched: its copy of `GameHostCaps.cpp` has no device define and its suite now pins "off" there; the on-cases live in the harness suite (inside `touches`).
- The directory-wide `set_source_files_properties` first tried in `installer.cmake` also reached the new board copies (Sticky answered on); a PRIVATE target definition on `game_installer_src` replaced it. Only `GameHostCaps.cpp` among that library's sources reads a `FREEINK_DEVICE_*` define (grep of `src` and `lib`: `FirmwareBoardTag.cpp`, `main.cpp`, and `EspNowLink.cpp` are not in it).
- Guards of `gameHostCaps()` (`git log` over `GameHostCaps.cpp`: `3d4aec97`, `17d02483`, `13a4c60b`, as entry 11's plan recorded): no early return or ordering guard; `NEARBY` stays `constexpr`, so `check_flash_budget.py objects` sees no initializer.

## Plan Change Log

## Review Triage Log

Lenses ran as context-free foreground subagents (blind-hunter, edge-case-hunter, verification-gap, intent-alignment), in one message, over `git diff b56b7645 -- src test` (15,711 B, nine files).

### 2026-10-13 - Review pass
- verdicts: 10 findings - high 0, medium 0, low 7, false 3, maybe-false 0 (plus 2 descriptive intent-alignment rows)
- findings:
  - blind 1 `[false]` `[reject]` other nearby entry points not gated on `HostCaps::nearby` -- `grep` of `src` and `lib` for `.nearby`: `Manifest::check` is the only reader of `host.nearby` (GamePackageInstaller and GameRegistry call it); the lobby and probe are reached only through a mode the check offers, so Sticky has no other route. The radio code stays linked on Sticky (no flash saving claimed).
  - blind 2 `[low]` `[patch]` stale "the device starts nearby" comments and the simulator test's message -- `GamesLauncherTest.cpp`, `GamesCheckEngineTest.cpp`, and `GameHostCapsSimulatorTest.cpp` reworded to say the X4 Pro; `game_host_caps_simulator.cmake`'s over-long line re-wrapped.
  - blind 3 `[low]` `[patch]` `GamesCheckEngineTest`'s `ASSERT_TRUE(nearby)` depends on an indirect define -- the assertion message and the comment now name `installer.cmake`'s X4 Pro build. The PRIVATE define reaches only `GameHostCaps.cpp` in practice (grep for `FREEINK_DEVICE_X4PRO` in `src` and `lib`: `main.cpp`, `FirmwareBoardTag.cpp`, `EspNowLink.cpp`, none built by that library).
  - blind 4 `[low]` `[reject]` `NEARBY_BUILT` name no longer says "every board" -- the header comment now says it is the X4 Pro's answer and the double's default; a rename touches the header, the double, and several unrelated test comments, for a name only. The X4 Pro board test pins the real function to it.
  - blind 5 `[low]` `[patch]` no `#error` guard for the board suite's defines -- added to `GameHostCapsBoardsTest.cpp`. The rest of the row (other boards covered only by the no-board copy; the simulator test now redundant with the two simulator board suites; a long test name) is `[low]` `[reject]`: the no-board copy is the same code path for any non-X4-Pro board, `GameHostCapsSimulatorTest` is entry 16's pin and stays as built, a name is not a defect.
  - blind 6 `[false]` `[reject]` no test for a game declaring only `nearby` on a non-X4-Pro board -- `ManifestTest` pins `Manifest::check`'s `NoHostMode` for a missing host mode; this entry changes only the caps value it reads.
  - blind 7 `[low]` `[reject]` the Decision and its lift condition live only in code comments -- the epic file's Notes Decision records it and the comment in `GameHostCaps.cpp` names the lift; the orchestrator owns `deferred-work.md` and the epic file.
  - edge 1 `[low]` `[patch]` `defined(FREEINK_DEVICE_X4PRO)` against siblings' `#if FREEINK_DEVICE_X4PRO` -- changed to the value test, as `main.cpp`, `FirmwareBoardTag.cpp`, and `EspNowLink.cpp` do.
  - edge 2 `[false]` `[reject]` Sticky with a nearby-only game is unavailable -- that is the intent ("keeps a declared nearby unavailable there, as before entry 11"); `Manifest::check` returns `NoHostMode`, pinned by `ManifestTest`.
  - edge 3 `[low]` `[reject]` the Sticky simulator shows nearby, so the Sticky-off UI path is not seen in simulator screenshots -- the orchestrator's brief and R16 settle it (the simulator's nearby stays as entry 16 built it); the Sticky-off caps are pinned by `GameHostCapsStickyTest` and the mode-picker "no radio" rows already cover the off UI.
  - verification-gap: no findings.
  - intent-alignment `[descriptive]` readings A-D; the diff implements B and C and A through B; D (the simulator on any board is on) follows the brief. The divergence it lists (host tests restate the platformio.ini defines by hand; no Sticky firmware or simulator build runs the off path) is the verification the ticket names (`pio run -e sticky`, `sim.sh build sticky`) plus the Sticky device run the Decision waits for.
  - intent-alignment `[descriptive]` tests that read `NEARBY_BUILT` as "the device's value" model an X4 Pro host -- same as blind 4; the double's default is pinned to the X4 Pro build's answer.

## Verification

Results, on the final tree (after the review patches):

- Host suites (`cmake --build build/test && ctest --test-dir build/test -j8`, hosttest lock): 2,539 tests, 0 failed (2 dump-for-review tests skipped, as before). New: `GameHostCapsX4ProTest`, `GameHostCapsStickyTest`, `GameHostCapsNoBoardTest`, `GameHostCapsSimulatorX4ProTest`, `GameHostCapsSimulatorStickyTest` (3 tests each, one executable per board's defines). Mutants, each restored: dropping `IS_X4PRO` from the expression turned the Sticky, no-board, and `GameHostCapsTest` cases red; dropping `IS_SIMULATOR` turned the two simulator suites red.
- `./bin/clang-format-fix` twice, no change after the second; `python3 scripts/check_upstream_touches.py`: PASS (no upstream file touched).
- `pio run -e x4pro`, `-e sticky`, `-e default`: all exit 0. `sim.sh build x4pro` and `build sticky`: exit 0. `pio check ... -e x4pro` (`GameHostCaps.cpp` is built only with `FREEINK_CAP_GAMES`, so `default` leaves it out): no defects. `check_flash_budget.py objects`: 63 game objects, no static initializer, largest mutable static 4 B.
- x4pro size like for like (two builds of the same tree layout, one at baseline `b56b7645` with the two `src/games/GameHostCaps.*` edits reverted, one with them; `pio run -e x4pro`, `firmware.bin` and `xtensa-esp32s3-elf-size -A` of the ELF): `firmware.bin` 6,255,680 B in both (delta 0 B); every loadable section (`.iram0.*`, `.dram0.*`, `.flash.*`) identical in `size -A`, so static internal RAM (iram0 + dram0 sections, 259,959 B) is also delta 0 B; only `.debug_info` and `.debug_str` differ (+18 B, +9 B), which are not in the image. The two bins differ in 65 bytes from offset 177 (the image's header/digest bytes); a rebuild of the changed tree gave a bin identical to the first. The change costs nothing on the x4pro against the epic's +40,304 B / +80 B over `c28120a7` (those figures were taken by entry 11's method and are not re-measured here). The flash-budget `compare` (games on versus off) was not run: the ticket asks for the x4pro compare unchanged like for like, which the two same-way measurements above give.
- Not run: a Sticky device or simulator screenshot (no Sticky device; the simulator's Sticky reports nearby on by design). No screenshots for this ticket.

## Auto Run Result

**Summary:** `gameHostCaps()` reports Play Nearby on for the X4 Pro device build (`FREEINK_DEVICE_X4PRO`) and the simulator on either board, and off on every other games board (Sticky first), so `Manifest::check` leaves a declared `nearby` out there as before entry 11. The device flag is one `constexpr` expression with a comment naming the owner's Decision of 2026-10-13 and the lift (a Sticky device run). The x4pro firmware is unchanged like for like (0 B flash, 0 B static RAM).

**Files:**
- `src/games/GameHostCaps.cpp` - `NEARBY = IS_SIMULATOR || (IS_X4PRO && NEARBY_BUILT)`, comment with the Decision and the lift.
- `src/games/GameHostCaps.h` - comments: `NEARBY_BUILT` is the X4 Pro's answer and the double's default.
- `test/game_script/harness/GameHostCapsBoardsTest.cpp`, `game_host_caps_boards.cmake` - new: five per-board executables (X4 Pro, Sticky, no device define, simulator on X4 Pro, simulator on Sticky).
- `test/game_core/GameHostCapsTest.cpp` - the no-device-define copy pins nearby off.
- `test/game_script/harness/installer.cmake` - `game_installer_src` builds the X4 Pro caps (PRIVATE `FREEINK_DEVICE_X4PRO=1`), so the installer and games-check suites keep a nearby-on host.
- Comment-only: `HostCapsScript.h`, `GameHostCapsSimulatorTest.cpp` (and its message), `game_host_caps_simulator.cmake`, `GamesLauncherTest.cpp`, `GamesCheckEngineTest.cpp` (assert message names the X4 Pro caps).

**Review breakdown:** four lenses (blind-hunter, edge-case-hunter, verification-gap, intent-alignment) as foreground subagents; 10 findings (7 low, 3 false) plus 2 descriptive rows. Patched (low): stale comments and the simulator test message, the `GamesCheckEngineTest` assertion message, a `#error` guard for the board suite's defines, `#if FREEINK_DEVICE_X4PRO` in place of `defined()`. Deferred: none. Rejected with reasons in the Review Triage Log: the `NEARBY_BUILT` rename (low), the thin-coverage/redundancy row (low), the Decision recorded only in code comments (low; the epic Notes record it), the Sticky simulator showing nearby (low; settled by the brief and R16), and three false rows (other entry points; nearby-only game; nearby-only on Sticky). `followup_review_recommended: false` (no medium or high patched).

**Verification:** see above. **Formatting:** `clang-format-fix` made no change outside this story's files.

**Residual risks:** the host tests restate each board's defines by hand (the cmake comments cite `platformio.ini` and `simulator.ini`), so a changed env flag is caught by a firmware build or device run, not by them; `test/game_core/CMakeLists.txt` (outside `touches`) still builds its copy of `GameHostCaps.cpp` with no device define, so that suite stands for "any other games board" by design; the radio code stays linked on Sticky (no flash saving); the gate lifts by dropping `IS_X4PRO` once a Sticky device run has played a round, a step the owner's Sticky run (SPEC Open Questions) triggers.
