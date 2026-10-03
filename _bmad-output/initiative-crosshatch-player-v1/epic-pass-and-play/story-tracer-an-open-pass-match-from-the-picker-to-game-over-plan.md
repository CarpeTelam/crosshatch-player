---
title: 'Tracer: an open pass match from the picker to game over'
type: 'feature'
ticket: '1'
created: '2026-10-01'
status: done
route: 'full'
route_source: 'auto'
baseline_revision: 'c1902721a82d6e4b3dabda6337a1e5dc0ccc85c2'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/.skills/control-flow-clarity/SKILL.md'
  - '{project-root}/.skills/scope-discipline/SKILL.md'
  - '{project-root}/.skills/refactor-for-review/SKILL.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Every match runs `Roster::solo()`: the picker's Pass row and a pass-only game start a solo match, `gameHostCaps().pass` is false, and `Session` and `SoloRounds` serve one local seat only, so no pass match exists and entry 4 has no seat-taking interface to build the hand-off on.

**Approach:** The launcher and picker build a `GameCore::Roster` (solo, or pass with the fewest seats a pass match can have) and pass it through `GameMatchActivity` into `GameVM`, which builds the `Session` from it. `Session` takes the seat for `input` and `draw` from its caller, keeps a move only from the turn seat, and delivers `over` once to each local seat; the round driver splits into seat-taking steps that never pick a seat, plus the open-match composition that draws the seat a new pure `GameCore::seatShown` names. `pass` turns on through a shared host-caps constants header that the harness double also reads, and the open fixture `pass-open` (noughts and crosses) is added and packed.

## Boundaries & Constraints

**Always:** Nothing in `GameCore`, `GameScript`, or `src/games` assumes n = 2: host suites run n = 3. A move reaches `apply` only from the turn seat while the round is on. A pass match writes, reads, and deletes no `resume.bin` (one log line). Every existing solo test case keeps its assertions; `SoloRoundsTest`'s existing cases stay byte-for-byte unchanged. Each test double names the device behaviour it stands in for, and a test pins the host-caps double's defaults to the device's values (R13). AGENTS.md memory rules (no new statics, no bare `new`, locals under 256 B).

**Never:** Edit `lib/GameCore/MatchLifecycle.*`, `lib/GameScript/LuaGame.*`, `src/games/FrameReplay.*`, `src/games/GameSaveStore.*`, `english.yaml`, any upstream file, or the epic file. No seat-choice UI (deferred, `## e5-inception`). No `Result`/`HandOff` (entries 2 and 4). No `Manifest::check` change (entry 2). No `api-level-1.txt` entry.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Picker Pass row | `pass-open` (solo+pass, seats 1..2), host `pass` on | Pass match, `Roster{Pass, 2, 0b11}`; `setup` gets `ctx.seats = 2, mode = "pass"`; seat 1 drawn first | — |
| Turns alternate | pass, turn 1, seat 1 taps a free cell | `apply(seat 1)`; then seat 2 drawn; next tap goes to seat 2's `input` | — |
| Off-turn input | driver `play(tap, 2)` while turn is 1 | seat 2's `input` runs; its move is discarded, no `apply` | counted in `discardedMoves()` |
| Rejected move | seat taps a taken cell | `apply` rejects; `rejected` goes to the mover's `input` (its own `ui`) | no new snapshot |
| Round ends | winning or drawing move, n = 2 or 3 | `over` to each local seat once, ascending, each with its own `ui`; answers discarded; then `draw(0)`; Over menu over seat 0's frame | — |
| Timer | pass, timer due, turn 2 | `timer` reaches seat 2's `input` | stale timer dropped with no draw |
| One local seat | a 2-seat roster with `localSeats = 0b10` (nearby's shape) | `seatShown` = 2 in Playing and Over; `over` to seat 2 only | — |
| Pass start that cannot fit | pass with `max(2, seats.min)` > `min(seats.max, host maxSeats)` | no match starts; error logged; screen repainted | — |
| Pass resume.bin | any pass match | no read, write, or delete; one "no resume.bin" log line | — |

</frozen-after-approval>

## Code Map

- `lib/GameCore/Roster.{h,cpp}` -- `Roster` (mode, seats, localSeats mask, api). Add `static Roster pass(uint8_t seats)` (every seat local), `uint8_t localSeatCount() const`, and free `uint8_t passSeats(uint8_t seatsMin, uint8_t seatsMax, int32_t hostMaxSeats)`: `max(2, seatsMin)` if within `min(seatsMax, hostMaxSeats, MAX_SEATS)`, else 0.
- `lib/GameCore/SeatShown.h` (new, pure, header-only) -- `uint8_t seatShown(MatchState, const Roster&, const Status&)`: one local seat → `firstLocalSeat()` in every state; several → 0 when `state == Over || status.over`, else `status.turn`. Includes `MatchLifecycle.h` (read only) for `MatchState`; entry 2 adds Result and HandOff.
- `lib/GameCore/Session.{h,cpp}` -- drop `localSeat`. `handle(event, seat)`: input for `seat`; keep the move only when `!pending && !over && seat == status.turn && roster.isLocal(seat) && !runtimeEvent && fits`. `draw(seat)`. `afterSnapshot` delivers `over` to each local seat 1..n once per round. `Rejected` goes to `moveSeat`. Keep every existing guard (Design Notes).
- `lib/GameScript/SoloRounds.{h,cpp}` -- keep the class name (its suite stays unchanged). Seat-taking steps: `begin(Session&)` / `beginAgain()` (no draw; `beginAgain` cancels the timer), `play(event, seat)` (stale timer dropped; input, pending move; no draw), `draw(seat)` (draws, counts `started` at a round's first frame, then the round end). Composition: `start`, `restart`, `step(event)` = the steps with the seat `seatShown(status.over ? Over : Playing, ...)` names; `step` drops a stale timer before anything, with no draw.
- `src/games/GameVM.{h,cpp}` -- `create(..., const GameCore::Roster& roster)` stores it; `run()` builds the `Session` from it. Keeps calling `rounds.start/restart/step` (an open match). Comments "solo Session" → the match's.
- `src/games/GameHostCaps.{h,cpp}` -- `GameHostCaps.h` gains `namespace HostCapsValues { MAX_SEATS = 2; NEARBY_BUILT = false; PASS = true; }`; `.cpp` uses them (simulator nearby rule unchanged).
- `test/game_script/harness/list_stubs/{GameHostCapsDouble.cpp,HostCapsScript.h}` -- defaults from `HostCapsValues` (`Script::pass = HostCapsValues::PASS`, nearby `NEARBY_BUILT`); comments name the device function they stand in for.
- `src/activities/games/GameMatchActivity.{h,cpp}` -- constructor `(renderer, input, manifest, const GameCore::Roster& roster, Start start = Start::New)`; member `roster`; passes it to `GameVM::create`; in `onEnter`, a pass roster skips `setPackageHash` with `LOG_INF("%s: pass match; no resume.bin until pass saves (epic-pass-and-play entry 9)")`, so `GameSaveStore` reads, writes, and deletes nothing (its `hasPackageHash` guards). Class comment: solo or open pass.
- `src/activities/games/GameModeActivity.cpp` -- `activateIndex`: Solo → `Roster::solo()`; Pass → `passSeats(manifest.seatsMin, manifest.seatsMax, gameHostCaps().maxSeats)`, 0 → `LOG_ERR`, `requestUpdate()`, return; else `Roster::pass(n)` and log `Mode pass picked for %s: %u seats`; Nearby → today's solo fallback log. Includes `games/GameHostCaps.h`.
- `src/activities/games/GamesLauncherActivity.cpp:580-605` -- direct start: a pass-only game gets the pass roster the same way; nearby-only keeps its solo log; Continue stays solo `Start::Resume`.
- Call-site follow-through for the new signatures (test/game_script): `LuaGameFixture.h` `SessionGame` (optional roster), `SessionGameTest.cpp:269-302`, `HostBindingsTest.cpp:184`, harness `ResumeSessionTest.cpp`, `GameVmTest.cpp:62`, `ResumeMatchTest.cpp:267,1146`, `GameMatchTest.cpp:51,961`.
- Harness tests that relied on `pass` off by default: `GamesLauncherTest.cpp` `AGameThisHostCannotStart...` and `ModePickerTest.cpp` `ASoloAndPassGame...WhileTheHostHasNoPass...` set `hostcaps::script().pass = false`; `AGameWhoseOnlyStartableModeIsPass...` and `ATapOnPassStartsTheSoloMatchAndSaysSo` become pass-match tests; `openPickerFor` builds a 1..2-seat manifest.
- `test/game_script/fixtures/pass-open/` (new: `manifest.json`, `main.lua`) and `README.md` -- see Design Notes.
- `scripts/pack_device_run.py` / `_test.py` -- add `('pass-open', 'pass-open')` to `GAMES`, docstring, and the test's expected files (eight, in order).
- Do not change: `LuaGame` (already per-seat `ui`, seat 0 works via `lua_rawgeti`), `MatchLifecycle`, `GameModeActivity.h` (its stale class comment is deferred to entry 7, which rewrites the file).

## Tasks & Acceptance

**Execution:**
- [x] `lib/GameCore/Roster.{h,cpp}`, `lib/GameCore/SeatShown.h` -- add as in Code Map.
- [x] `lib/GameCore/Session.{h,cpp}` -- seat-taking `handle`/`draw`, `over` per local seat, rejection to the mover; update the class comment.
- [x] `lib/GameScript/SoloRounds.{h,cpp}` -- seat-taking steps plus the composition; comment that the name predates pass and the class serves any `localSeats` subset.
- [x] `src/games/GameVM.{h,cpp}`, `src/games/GameHostCaps.{h,cpp}`, `src/activities/games/{GameMatchActivity.*,GameModeActivity.cpp,GamesLauncherActivity.cpp}` -- as in Code Map.
- [x] `test/game_core/SeatShownTest.cpp` (new, in `CMakeLists.txt`) -- table test: pass n = 2 and n = 3 (Playing → turn, Over → 0, Playing with `status.over` → 0), a 2-seat roster with local seat 2 only (2 in both), solo (1); `passSeats` and `Roster::pass` cases.
- [x] `test/game_core/SessionTest.cpp` -- call sites take seat 1; new: n = 3 pass `ctx`, off-turn seat discarded, turn seat applied with its seat, `over` once to each of seats 1..3 in order (and once more after a rematch), one-local-seat roster gets `over` only for its seat, rejection reaches the mover. `FakeRules` records each input's seat.
- [x] `test/game_core/GameHostCapsTest.cpp` -- `PassIsOffUntilPassAndPlay` → pass on: a solo+pass game's check offers both; the real `gameHostCaps()` equals `HostCapsValues` (seats, pass, nearby).
- [x] `test/game_script/SoloRoundsTest.cpp` -- append (existing cases untouched): over `pass-open` with a pass roster: `play(tap, 2)` on turn 1 discards; a step draws seat 2 after seat 1's move; `draw(1)` while turn is 2 publishes seat 1's frame (a seat other than `status.turn`); a timer reaches the turn seat; n = 3 with an inline script; a 2-seat one-local-seat roster draws its seat throughout; `begin` publishes no frame and `started` moves only at the first `draw`.
- [x] `test/game_script/harness/GameMatchTest.cpp` -- a `pass-open` match through `GameMatchActivity` with a pass roster: seats alternate (drawn text), a taken cell is rejected for its mover only, a win delivers `over` once per seat (log), each seat's answer to `over` never reaches `apply` (log), the Over menu shows over seat 0's frame; no `resume.bin` with a `.pkg` present and the skip logged.
- [x] `test/game_script/harness/ModePickerTest.cpp` -- Pass row starts a 2-seat pass match (log, seat 1 drawn); pass-only direct start; a pass that cannot fit starts nothing; `TEST(HostCapsDouble, DefaultsAreTheDevicesValues)` pins the double's `gameHostCaps()` after `hostcaps::reset()` to `HostCapsValues` and `ApiLevel.h`.
- [x] Call-site follow-through and default-`pass` fixes listed in Code Map.
- [x] `test/game_script/fixtures/pass-open/`, `fixtures/README.md` (Games row, packable list, Device-run packages), `scripts/pack_device_run.py`, `scripts/pack_device_run_test.py`.
- [x] `_bmad-output/implementation-artifacts/deferred-work.md` -- `## 5.1` at the end: `## 4.9` item 2 resolved for pass (nearby still solo); `## 4.5`'s double pin resolved; `GameModeActivity.h` class comment stale until entry 7; the `SoloRounds` name, for the sweep (entry 10).

**Acceptance Criteria:**
- Given the X4 Pro simulator with `pass-open` installed, when Games, `pass-open`, and Pass are tapped, then seat 1's board shows, a move shows seat 2's turn, and a finished round shows the Over menu over the "Everyone" (seat 0) frame.
- Given a solo game, when it is played as today, then every pre-existing host test passes unchanged in its assertions and `SoloRoundsTest`'s existing cases are unedited.
- Given the x4pro build, when `check_flash_budget.py compare` runs, then the delta over the base (+233,440 B flash, +784 B static RAM at `c1902721`) is within 11,152 B and 32 B.

## Implementation Notes

- Implemented by a context-free subagent from this plan; host tests 1,415/1,415, every `scripts/*_test.py`, `check_layers.py`, `check_upstream_touches.py`, and `clang-format-fix` clean before review.
- Deviations: `passSeats` takes `int32_t` seats (the manifest's type), so a large `seats.max` is not cut to a byte. `test/game_script/harness/pass_and_play.sources.cmake` (new) adds `Roster.cpp` to the harness core library, since `Session` no longer pulls it in and `SoloRounds` now needs it. `MatchSupport.h` gains `drawnTexts()` (the replay draws a code point per call). `InstallerIconTest`'s two pass cases (real `gameHostCaps()`) now expect pass on; `GamePackageInstallerTest` lists `pass-open` among the README's packable fixtures.
- The subagent's edit to `lib/GameCore/HostCaps.h` (a stale comment) was reverted by the plan's author: the file is outside the entry's touches; the stale comment is deferred to entry 2 in `## 5.1` with `GameModeActivity.h`'s and `Manifest.cpp`'s.

## Plan Change Log

## Review Triage Log

Pass 1 (2026-10-01). Lenses ran as four context-free subagents launched together in one message, and all four returned: blind-hunter, edge-case-hunter, verification-gap, intent-alignment. Verdicts: high 0, medium 5, low 9, false 2, maybe-false 0 (plus the intent auditor's descriptive report).

| # | Lens | Finding | Verdict | Route | Evidence / action |
|---|------|---------|---------|-------|-------------------|
| 1 | blind, edge (x3) | `Manifest::check` offers pass with `seats.max` < 2; the Pass row or pass-only row then does nothing visible (log only), launcher sets `lastOpened` first | medium | defer | Real (`Manifest.cpp:236`), not reachable with any shipped game; the frozen Never excludes `Manifest::check`, which entry 2 changes. Deferred in `## 5.1`. |
| 2 | blind | `Session` no longer validates the seat it is given | low | reject | Callers pass only `seatShown`'s seat; a guard adds a branch for an unshown case. |
| 3 | blind, intent | After a pass round ends, a queued tap's input goes to seat 0 and its move is discarded; untested | medium | patch | R7 behaviour with no test. Added a `PassRoundsTest` case. |
| 4 | blind | Comments claim the loop serves any local subset, but `seatShown` returns a possibly non-local turn seat | low | patch | Direct comment correction: narrowed to all-local and one-local rosters. |
| 5 | blind, edge | `Roster::pass` does not validate `seats` (0, > 16) | low | reject | Only `passSeats`-validated values reach it; guards add complexity for an unreachable input. |
| 6 | blind | The pass-start block is duplicated in picker and launcher | low | reject | Entry 8 removes the launcher's direct start; no named divergence before then. |
| 7 | blind | Pass + `Start::Resume` would go to Error | false | reject | No hash: `loadResume` returns empty with `unreadable` false, so `seedResume` logs and starts new; no caller builds the pair. |
| 8 | blind | Measurements, screenshots, plan bookkeeping missing | false | reject | They run after review (AGENTS.md order); the int32 deviation is now in Implementation Notes. |
| 9 | blind | Stale comments in `HostCaps.h`, `Manifest.cpp`, `GameModeActivity.h` | low | reject | Already deferred in `## 5.1` (files outside touches). |
| 10 | blind | Host-caps double section splits `ModePickerTest`'s ModeCount heading from its tests; Roster tests in `SeatShownTest` | low | patch | Section moved; the Roster tests' file left as is (cosmetic). |
| 11 | blind | Play again in a pass match untested end to end | medium | patch | Added to `PassMatchTest`: Play again redraws seat 1; Leave leaves no `resume.bin`. |
| 12 | blind | `SoloRounds::step` checks the timer twice | low | reject | Deliberate and commented: `step` must skip the draw, `play` must drop it for entry 4's direct callers. |
| 13 | verification-gap | Picker Pass test cannot tell a pass match from solo | medium | patch | Asserts the match's own "pass match; no resume.bin" line and seat 2's frame after a tap. |
| 14 | verification-gap | `afterSnapshot`'s stop on a failing `over` delivery untested | medium | patch | `FakeRules` fails `over` input for one seat; test asserts the outcome and no later delivery. |
| 15 | verification-gap (other) | `## 5.1` evidence cites the picker test as proving the roster reaches the match | low | patch | Holds once #13 lands. |
| 16 | intent | Descriptive: composition uses `seatShown` for input too; `seatShown` fed `status.over`-derived state; steps tested at library level only; launcher direct start checked by logs | low | reject | All match the plan's Code Map (`seatShown` names the seat for draw and input; entry 4 calls the steps from `GameVM`); no defect named. |

## Design Notes

**Guards kept (`git log -L` of `SoloRounds::step`, `startRound`, `countRoundEnd`, `Session::handle`, `afterSnapshot`):** `!timer.accepts(event)` drops a timer re-armed or cancelled after it fired, before `input` and with no draw (b64f455d); the `outcome == Ok` chain stops a step at the first failed call; `started` moves after a round's first frame is published and before `countRoundEnd`, so the match never sees an end without its start (8e233695); `countRoundEnd` counts once per round, after the draw, so the round's last frame is out first; `handle` discards a move while one is pending, after `over`, off-turn, in answer to `rejected`/`over` (a game answering every rejection must not loop inside one step), and over `MOVE_BYTES`; `afterSnapshot` sets `settled` only on an Ok status (d45dd71d) and delivers `over` once per round; `start()` after `restore()` skips setup once (68ec417b). All survive the split: `draw(seat)` now owns the `started` and end counts, so `begin` + `draw` keeps the old order.

**Why `SoloRounds` generalises in place:** one class, no duplicate loop; a sibling would copy the Play-again and count logic. The composition (`start`/`restart`/`step`) is what `GameVM` runs for every open match, so `SessionGame` still drives production code; entry 4 calls the steps with its own seats from `GameVM` and never edits the driver.

**Host-caps pin:** `GameHostCaps.cpp` and the double define the same symbol, so no target links both; both read `HostCapsValues`, `GameHostCapsTest` pins the real function to it, and the mode-picker suite pins the double's defaults to it.

**`pass-open`:** noughts and crosses on a 3 x 3 grid (`ch.screen.w` 474: cell 140 px, left 27, top 200; `x` and `circle` icons, large). `setup` keeps `ctx.seats`; marks alternate by move count; `status` gives `turn = moves % 2 + 1` with two seats and `turn = 1` with one, `over` with `winners = {mark}` (pass) or `{1}` (solo) on a line, `{}` on a full board. `apply` rejects a taken or unknown cell ("That square is taken") and logs `apply seat S cell C`. `input`: tap → `{cell = c}` (any cell, so a taken one is rejected); `rejected` → `ui.message`; `over` → `ui.overs`, `ch.timer.cancel()`, log `over for seat S`, and returns `{cell = 5}`, which the runtime discards; `timer` → `ui.nudges`, log `timer for seat S`, re-arm. `setup` and each tap arm `ch.timer.after(10000)`. `draw` shows "Player S (X|O) to move" or, at seat 0, "Everyone: Player N wins" / "Everyone: a draw", plus that seat's `ui` message and nudges.

## Verification

**Commands:**
- Host tests (AGENTS.md, under `/tmp/crosshatch-hosttest.lock`) -- expected: all pass, incl. `SessionTest`, `SeatShownTest`, `SoloRoundsTest`, `GameMatchTest`, `ModePickerTest`, `GamesLauncherTest`, `GameHostCapsTest`.
- `for t in scripts/*_test.py; do python3 $t; done` and `python3 scripts/check_layers.py` -- expected: pass.
- `python3 scripts/check_upstream_touches.py`; `./bin/clang-format-fix` twice -- expected: clean.
- Under `/tmp/crosshatch-build.lock` with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`: `pio run -e x4pro`, `pio run -e default`, `pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high` -- expected: success.
- `python3 scripts/check_flash_budget.py` `build on`, `build off`, `compare --limit-kib 250 --ram-limit-bytes 1024`, `objects` on the final commit -- expected: flash delta minus 233,440 B ≤ 11,152 B; static RAM minus 784 B ≤ 32 B; record both with the commit.

**Results (2026-10-01, after the review patches, on this entry's commit's tree):**
- Host suites: CMake/Ninja configure, build, and `ctest -j4` under the host-test lock: 1,417 of 1,417 pass (incl. `SessionTest`, `SeatShownTest`, `SoloRoundsTest`'s solo cases unedited plus `PassRoundsTest`, `PassMatchTest`, `PickerTest`, `HostCapsDouble`, `GameHostCapsTest`). `PassMatch*` and the picker's pass tests passed 30 of 30 repeats each (implementer).
- Every `scripts/*_test.py` passes; `check_layers.py`: 480 edges pass, no new edge; `check_upstream_touches.py`: PASS (no upstream file touched); `./bin/clang-format-fix` twice, nothing new.
- `pio run -e x4pro` and `pio run -e default`: success. `pio check -e x4pro` and `pio check` (default), all three defect levels: no defects.
- `check_flash_budget.py` `build on`, `build off`, `compare --limit-kib 250 --ram-limit-bytes 1024`, `objects` (under the build lock, with the shared cache): games on 5,913,664 B, off 5,679,168 B, **+234,496 B** (21,504 B under the gate); static internal RAM **+784 B** (`.dram0.bss` +16, `.iram0.text` +684, `.iram0.text_end` +84); 43 game objects, no static initializer, largest mutable static 4 B. Over the orchestrator's base at `c1902721` (+233,440 B, +784 B, measured the same way): **+1,056 B flash and +0 B static RAM** (bar 11,152 B and 32 B). Measured on the working tree whose sources are this entry's commit (only this plan's text changed after the measurement).
- Simulator (`sim.sh build x4pro`, `pass-open` packed by `pack_game.py` into `fs_/games/`), screenshots in `story-pass-open-screenshots/`:
  - `1-home.png` Home; `2-games-tap1.png` Games (tap 1) with the `pass-open` row; `3-picker-tap2.png` the game (tap 2) opens the picker, Solo and Pass and play; `4-pass-tap3-seat1-turn.png` Pass (tap 3): seat 1, "Player 1 (X) to move".
  - `5-seat2-turn.png` after seat 1's move: seat 2, "Player 2 (O) to move"; `6-seat2-taken-square.png` seat 2 taps the taken square: "That square is taken" on seat 2's turn; `7-seat1-turn.png` after seat 2's move: seat 1 again.
  - `8-over-menu-seat0.png` X completes 1-4-7: the Game over menu (Play again, Leave) over seat 0's "Everyone: Player 1 wins" frame; the log shows `over for seat 1`, `over for seat 2`, then `Playing -> Over`, and `pass-open: pass match; no resume.bin ...`.
- Setup note: the container lacked `xdotool` and ImageMagick (the skill's prerequisites); installed with `apt-get` for the screenshots.

**Manual checks:**
- `sim.sh build x4pro`, install `pass-open`, screenshots: Home → Games → `pass-open` → Pass (3 taps), seat 1's turn, seat 2's turn, Over menu over seat 0; copied to `story-pass-open-screenshots/`.
