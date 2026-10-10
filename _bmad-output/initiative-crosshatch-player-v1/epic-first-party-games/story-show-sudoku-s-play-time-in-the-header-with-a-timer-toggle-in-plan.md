---
title: "Show Sudoku's play time in the header, with a TIMER toggle in the menu"
type: 'feature'
ticket: '16'
created: '2026-10-10'
status: done
baseline_revision: '4171f69ce841c6d248444b908716fac2613469e5'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
followup_review_recommended: false
context: ['{project-root}/test/game_script/first_party/README.md', '{project-root}/docs/crosshatch/game-canvas.md']
warnings: ['oversized']
deferred:
  - summary: >-
      A minute timer that fires early after a pause redraws an identical frame, one extra e-ink push.
    evidence: |-
      ch.time.ms leaves out pause time and ch.timer counts raw time (api-level-1.txt), so a timer armed before a pause falls due on Resume with the minute unchanged; the engine draws after every timer event (MatchRounds::step) and the game cannot say "nothing changed". Resume already redraws on a full refresh, so the extra push is rare and cheap. Avoiding it needs an engine-side "no change" answer from input.
    location: >-
      games/sudoku/main.lua (game.draw), lib/GameScript/MatchRounds.cpp (step)
    severity: low
  - summary: >-
      Firing and re-arming of the real minute timer are not pinned on the host, because a first-party round delivers no timer event.
    evidence: |-
      interaction.timer pins the draw side through a ch.timer double and a timer event returning nil; the engine's generic timer path is covered by GameMatchTest (ADueTimerIsPolledByTheLoopAndDeliveredToTheGame, the timer fixture), not by Sudoku. On the simulator (x4pro) an idle board showed "0 min" and then "1 min" with no tap, one frame pushed at 60 s (story-timer-screenshots/puzzle-time-1min-after-timer.png). Settled on a device by entry 5's next run, which checks the header time across a pause.
    location: >-
      test/game_script/first_party/README.md ("No timer event is ever delivered"), test/game_script/first_party/sudoku/interaction.lua
    severity: medium (unverified on the device)
---

<intent-contract>

## Intent

**Problem:** Sudoku shows its time only on the Solved screen; the owner wants the play time visible during a puzzle (epic Notes, Decision of 2026-10-10, entry 16), without an e-ink push every second.

**Approach:** While the board shows, the header's right edge shows whole minutes ("0 min", "12 min"), redrawn when the minute changes, from one `ch.timer` armed for the next minute boundary. A MENU row `TIMER: ON/OFF` (icon `timer`, after NOTES AS), kept in `ch.store`, on by default, turns it off.

## Boundaries & Constraints

**Always:** The shown time is the Solved screen's time: saved `t` plus `ch.time.ms() - ui.last`, capped at `MAX_T` (99:59, so 99 min); `ch.time.ms` is the PauseClock's clock (entry 13), so a pause adds nothing. No timer while TIMER is off, while MENU or HOW TO PLAY shows, or once the round is over (game-canvas.md: none while Paused, none after Over). The left header text (level name, message, note) and the time never overlap: a left text that leaves no room hides the time while it shows. A store without the `timer` key, or a non-boolean one, reads as on. Every MENU row stays at least 44 px and every row's tap reaches its action. `ch.timer.after` refuses under 1,000 ms.

**Never:** A per-second or m:ss tick. Change the Solved screen's Time and Best, the saved state or its shape, `apply`, `status`, or any round's outcome. Show a time on MENU, HOW TO PLAY or Solved. Touch anything outside `games/sudoku/**`, `test/game_script/first_party/sudoku/**`, and the one `timer` row in `docs/crosshatch/game-icons.md`.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Minute text | live 0 / 59,999 / 60,000 / 5,999,000 ms, and `t` + 600,000 past it | "0 min" / "0 min" / "1 min" / "99 min" / "99 min" | none |
| Arming | board, TIMER on, live 0 / 45,000 / 59,500 | `after(60000)` / `after(15000)` / `after(1000)` (clamped) | none |
| No next minute | live at or past 99 min | `ch.timer.cancel()`, no `after` | none |
| Not played | TIMER off, MENU, HOW TO PLAY, Solved | `cancel()`, no `after`, no "min" text | none |
| Pause | the clock stands still between two draws | same text; a timer that fires early re-arms for the boundary | none |
| Toggle | tap row 7 | flips `ui.timer`, writes `timer` to the store; label "TIMER: ON/OFF" | none |
| Store | no key / `timer = false` / `timer = "yes"` | on / off / on | none |
| Crowding | left text too long for the time | no time text, the left text unchanged | none |

</intent-contract>

## Code Map

- `games/sudoku/main.lua` -- `MAX_T`, `fresh` (reads the toggles; `ui.timer = s.timer ~= false`), `TOGGLES` (add `[7] = "timer"`), `menu_tap` (HOW TO PLAY becomes 8, CLOSE 9), `game.draw` (arms or cancels the timer, passes the live time). `game.input` already returns nil for `timer`; keep that.
- `games/sudoku/view.lua` -- `MENU_ICONS` (insert `"timer"` as 7th), `draw_menu` labels, `draw_board` header line (right-aligned minutes, `"medium"`, never `"small"` + `"right"`, which `drawn.lua` reads as a pad count), `view.draw(state, ui, over, ms)`; add `view.minutes(ms)`. `view.time` stays.
- `games/sudoku/layout.lua` -- `layout.ROWS = 9`; `row_h` is then 77 (>= 44), and the rows end at y 765 of 788. Update the comments that say eight/8.
- `test/game_script/first_party/sudoku/` -- `taps.lua` (comment: row list), `interaction.lua` (add `interaction.timer`, extend `reset`/`store`), `marks.lua` (`expect_menu` nine rows with the `timer` icon; `on_menu` 7 -> 8, 8 -> 9), `rounds/{how-to-play,long-expert,notes-digits,toggles}.lua` (rows 7 -> 8, 8 -> 9; `toggles` gains TIMER OFF/ON), new `rounds/timer.lua`.
- `docs/crosshatch/game-icons.md` line `timer` -- add "; Sudoku's TIMER toggle".
- Read-only: `lib/GameCore/PauseClock.h`, `lib/GameScript/ChBindings.cpp` (`timerAfter`), `GameTimer.h`.

## Tasks & Acceptance

**Execution:**
- [x] `games/sudoku/layout.lua` -- `ROWS = 9`, comments -- the ninth row
- [x] `games/sudoku/main.lua` -- `ui.timer` from the store (default on); `TOGGLES[7] = "timer"`; HOW TO PLAY row 8, CLOSE row 9; in `game.draw` a local `live(state, ui)` = `min(t + max(0, ch.time.ms() - ui.last), MAX_T)`; when `ui.timer`, not over and `ui.panel == nil`, pass `live` to the view and arm `ch.timer.after(max(1000, (m+1)*60000 - live))` with `m = live // 60000`, unless `m >= 99`; else `ch.timer.cancel()` -- one place covers resume, a fired timer, and every tap
- [x] `games/sudoku/view.lua` -- row 7 `TIMER: ON|OFF` with icon `timer`; the header time right-aligned at the grid's right edge, drawn only when `ms` is given and `ch.text_width(left) + 16 <= grid width - ch.text_width(time)`
- [x] `test/game_script/first_party/sudoku/**` -- the updates in the Code Map plus `interaction.timer(state)`: a `ch.timer` double (records `after`/`cancel`, refuses < 1000 as `timerAfter` does, has no stale-serial semantics, says so in a comment) pinning every Matrix row, the 44 px rows with each row's tap (centre and far corner) reaching its action, a `timer` event returning nil and keeping `ui`; `rounds/timer.lua` waits 61,000 ms and `shows` "1 min", TIMER off shows no "min"
- [x] `docs/crosshatch/game-icons.md` -- the `timer` row

**Acceptance Criteria:**
- Given a puzzle in play with TIMER on, when it is drawn at 0, 59,999, 60,000 and 5,999,000 ms, then the header ends "0 min", "0 min", "1 min", "99 min".
- Given the same puzzle, when the clock does not advance (a pause), then the text and the armed delay are those of the unmoved clock.
- Given TIMER off, MENU, HOW TO PLAY or Solved, then no "min" text is drawn and no `after` is called.
- Given the MENU, when row 7 is tapped twice, then TIMER flips off then on, `ch.store` holds `timer`, and a fresh ui reads it back; a store without the key reads on.
- Given a long left text, then the time is not drawn.
- Given the host suites and the games check on both canvases, then they pass; Solved Time and Best and every existing round keep their outcomes.

## Implementation Notes

- The route is full: more than 100 lines across game, tests and docs. An implementation subagent built it from this plan; the review patches were sent back to the same subagent.
- The header time is drawn "medium" and "right", never "small" and "right", because drawn.lua reads that pair as a pad count. The seed 32 of rounds/timer.lua is the next free one (11 to 14, 21 to 31, 41 and 61 were in use).
- After review the timer is armed from the result of `view.draw` (true only when the time was drawn), so a hidden time arms nothing.

## Plan Change Log

## Review Triage Log

### 2026-10-10 — Review pass
- verdicts: 14 findings — high 0, medium 1, low 10, false 3, maybe-false 0
- lenses ran as four context-free subagents (blind-hunter, edge-case-hunter, verification-gap, intent-alignment), started in one message, in the foreground.
- findings:
  - `[low]` `[patch]` blind 1, edge 1, verification-gap other 1, intent-alignment (3): rounds/timer.lua's comment says it plays the minute timer on the real ch.timer, but a round delivers no timer event, so its "1 min" comes from the tap's draw — patched: the comment now says what the round pins; firing is shown on the simulator and left to the device run (deferred, medium unverified).
  - `[low]` `[reject]` blind 2, the ch.timer double has no pending/stale semantics — the engine's GameTimer owns them and its host tests cover them; Sudoku only calls after and cancel, which the double records.
  - `[false]` `[reject]` blind 3, `live()` repeats apply's arithmetic without the inner dt clamp — min(t + min(dt, MAX_T), MAX_T) equals min(t + dt, MAX_T) because t <= MAX_T, so the two agree.
  - `[medium]` `[patch]` blind 4, a long left text hides the time but the minute timer still redraws an identical frame each minute — patched: view.draw returns whether the time was drawn, game.draw arms only then, otherwise cancels; the pin now expects cancel with no after, and a new pin shows the time and the timer return when the text is short; every change of left text comes with an input, so a draw re-arms.
  - `[low]` `[defer]` blind 4 (second half), a timer that fires early after a pause redraws an identical frame — engine cannot be told "unchanged"; recorded in `deferred`.
  - `[low]` `[reject]` blind 5, no refresh-class pin for the minute redraw — draw calls no ch.gfx.refresh, so a timer draw takes the same path as a tap's; marks.lua already pins refreshes for taps.
  - `[low]` `[reject]` blind 6, HOW TO PLAY does not explain the header time or TIMER — help.lua explains HINT and CHECK only, not the other toggles; the intent says change nothing else.
  - `[low]` `[reject]` blind 7, manifest version not bumped — the intent says change nothing else; the package hash is the record.
  - `[false]` `[reject]` blind 8, the plan is committed unfinished — finalize fills the plan before the commit.
  - `[low]` `[patch]` blind 9, added lines over 120 columns — patched: every added line rewrapped; the awk check on the diff prints nothing.
  - `[low]` `[patch]` blind 10, the pins match the substring "min" / " min$" — patched: `^%d+ min$` everywhere.
  - `[false]` `[reject]` blind 10 (float), view.minutes would print "1.0 min" for a float — ch.time.ms and t are integers, as the double's own integer check shows.
  - `[low]` `[reject]` edge 1 (the same finding as the round comment above, plus "TIMER off is not seen on the real timer in the round") — `shows` cannot assert absence; interaction.timer, which the round's steps call, pins it, and the simulator shot puzzle-timer-off.png shows it.
  - `[low]` `[reject]` verification-gap, no gap found; intent-alignment, descriptive: it names R2/R3 (end-to-end firing, no identical push) as the readings the host tests do not reach — recorded as the two deferred items.
- Patched rows' fixes were made by the implementation subagent and re-run: `ctest -L games-check` 129/129, then the full host suite 1,853/1,853 on my side.


## Design Notes

- Intent: tickets.toml entry 16 and the epic Notes Decision of 2026-10-10 (whole minutes, redrawn once a minute, never m:ss); the owner set TIMER on by default. Pause: the Decision of 2026-10-09 (entry 13) makes `ch.time.ms` leave out pause time, and `ch.timer` counts raw time (api-level-1.txt), so a timer armed before a pause may fire while the minute has not changed; `draw` re-arms from the play clock, so it needs no special case.
- Why `draw` arms: a resume starts a new VM and calls `draw` first (game.input comment on `fresh`), `ch.timer` works in every callback (`timerAfter`), and arming is idempotent for the same boundary. A timer event returns nil and the engine draws after it.
- Guards kept: `fresh` returns early on the same clue signature (Play again keeps `ui`), so the toggle is read only on a new puzzle; `menu_tap` returns nil for `i == nil` (a tap off the rows); `game.input` returns nil for any other event kind.
- The 1,000 ms floor means a boundary less than a second away shows the old minute for up to a second; the draw after the timer shows the new one.
- A timer that fires early after a pause redraws an identical frame (one extra push); left, as the redraw also follows Resume's full refresh.

## Verification

**Commands:**
- `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j'` -- expected: all pass, `-L games-check` included (both canvases)
- `python3 scripts/pack_game.py games/sudoku <scratch>` -- expected: packs; record the package hash
- `scripts/*_test.py`, `python3 scripts/check_upstream_touches.py`, `./bin/clang-format-fix` twice -- expected: pass, nothing new
- `flock /tmp/crosshatch-build.lock sh -c 'sim.sh build x4pro'` then screenshots: a puzzle with the time in its header, the MENU with TIMER ON, a puzzle with TIMER off -- expected: as named

**Manual checks (if no CLI):**
- Look at each screenshot; copy them to `story-timer-screenshots/`.

**Results (on the final tree, after the review patches):**
- Host: `cmake -S test -B build/test -G Ninja ... && ctest --test-dir build/test -j` under the hosttest lock: 1,853 of 1,853 pass, 129 of them `games-check` (both canvases). A mutation of `view.minutes` (divide by 60,001) failed the Sudoku round tests, so the new pins are live. Not a flake claim; no flake fix is made here.
- `scripts/*_test.py`: all pass. `scripts/check_upstream_touches.py`: PASS (docs/crosshatch/game-icons.md is a fork file). `./bin/clang-format-fix` twice: nothing new, no formatting-only change outside this entry's paths.
- Package: `pack_game.py games/sudoku <scratch>`: 42,648 B, hash `95b7db58b5d45f55` (measured after the review patches; the pre-patch tree gave `bc8660b6716b79ee`). The base package was not re-measured: no size gate applies and no size is claimed.
- Simulator (`sim.sh build x4pro` under the build lock, 31 s, warm cache; the package installed and played; the x4pro simulator runs the 466 canvas): the header shows "0 min"; left idle with no tap the real timer fired and one frame was pushed at 60 s, the header then read "1 min"; MENU shows `TIMER: ON` (icon `timer`) as row 7, and `TIMER: OFF` after a tap, after which the board header shows no time. No log error.
- Screenshots (`story-timer-screenshots/`, all viewed): `puzzle-time-0min.png` (a fresh puzzle, "0 min" on the header's right), `puzzle-time-1min-after-timer.png` (the same board a minute later, with no tap), `menu-timer-on.png` (the nine-row MENU, `TIMER: ON`), `menu-timer-off.png` (`TIMER: OFF`), `puzzle-timer-off.png` (the board with no time).
- Not run: `pio run`, `pio check`, the other envs. No firmware source or `lib/` changed (the diff is `games/sudoku/**`, its companion checks, one docs row and this entry's files); the orchestrator's brief says none is needed.

## Auto Run Result

**Summary.** While a puzzle is played, Sudoku's header shows whole minutes on its right ("0 min" to "99 min", the Solved screen's time, pause time excluded), redrawn when the minute changes from one `ch.timer` armed for the next boundary (at least 1,000 ms). MENU gains `TIMER: ON/OFF` as row 7 (icon `timer`, kept in `ch.store`, on by default); HOW TO PLAY is row 8 and CLOSE row 9. A left text too long for the time hides it and arms nothing; TIMER off, MENU, HOW TO PLAY and Solved draw no time and cancel the timer.

**New sudoku package hash:** `95b7db58b5d45f55` (42,648 B).

**Files.**
- `games/sudoku/main.lua` -- `ui.timer` from the store, `TOGGLES[7]`, menu rows 8 and 9, the live time and the arming in `game.draw`.
- `games/sudoku/view.lua` -- `view.minutes`, the `timer` icon and row label, the right-aligned header time with the crowding rule; `view.draw` returns whether the time was drawn.
- `games/sudoku/layout.lua` -- `ROWS = 9`.
- `docs/crosshatch/game-icons.md` -- the `timer` row.
- `test/game_script/first_party/sudoku/{interaction,marks,taps}.lua`, `rounds/{how-to-play,long-expert,notes-digits,toggles}.lua`, new `rounds/timer.lua` -- the pins and rounds.
- This plan and `story-timer-screenshots/` (five PNGs).

**Review.** Four lenses ran as context-free subagents (14 findings: medium 1, low 10, false 3). Patches applied: the hidden-time timer (medium), the round comment, the line widths, the minute pattern. Deferred: the identical redraw after an early timer, and the unpinned real firing (settled on the simulator, to be seen on the device). Rejected with reasons: the double's semantics, `live()` arithmetic (equivalent), a refresh-class pin, HOW TO PLAY text, the manifest version, the unfinished plan, a float, the TIMER-off round assertion. Patched counts by verdict: medium 1, low 3. Follow-up review recommended: false (no high patched, one medium).

**Verification.** As under Verification, Results.

**Residual risks.**
- Whether the time fits beside the left text on the device's real fonts is untested; the host uses stand-in metrics. The simulator x4pro shows "Sudoku - Easy" and the time with room to spare; the owner judges the longest hint message on the panel.
- The header time across a pause is for entry 5's next device run, as the Decision says.
- Formatting: `./bin/clang-format-fix` changed nothing, so no formatting-only change outside this entry's paths.
