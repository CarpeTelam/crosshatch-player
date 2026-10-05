---
title: 'Refactor sweep'
type: 'refactor'
ticket: '4'
created: '2026-10-05'
status: 'built'
baseline_revision: '4137d9478ae0c2677b98fe0f0755d34ef978e9e8'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
followup_review_recommended: true
context:
  - '{project-root}/AGENTS.md'
  - '{project-root}/test/game_script/first_party/README.md'
  - '{project-root}/docs/crosshatch/api-level-1.txt'
warnings: [oversized]
deferred:
  - summary: >-
      The load-nesting probe sees only main's own load, so a module required in a function body (setup, draw, input) that itself requires another at load is not probed.
    evidence: |-
      `probeLoadNesting` wraps `require` for `pcall(require, "main")` only; a lazily loaded chain of the same depth is the author's (documented in first_party/README.md "Module loading" and ScriptVm.h; pinned by the engine test for the lazy row). The device refuses the second nested parse at under 10 KB of the 16 KiB stack free; the device figure is unmeasured (the simulator refused main > layout > board). Reason deferred: probing a function-body require needs the game's own calls run under the wrapper, which the rounds do not expose; entry 5's device run is where a lazy chain would show.
    location: >-
      test/game_script/harness/games_check/ScriptVm.cpp (probeLoadNesting)
    severity: low
  - summary: >-
      The instruction margin of Sudoku's `toggles` round (rules.draw_marks draws about 20 full frames and loops over 81 cells inside one `steps(state)` call) is unmeasured.
    evidence: |-
      The round passes, so it is under the 2,000,000 budget of one `steps` call; the sandbox has no instruction counter a check can read, so the margin is not a figure. A later addition to draw_marks that crosses the budget fails the round with "instruction budget exceeded".
    location: >-
      test/game_script/first_party/sudoku/rules.lua (draw_marks), rounds/toggles.lua
    severity: low
  - summary: >-
      A rejected move after the round is over, or a malformed move, gets "Play in the highlighted board" in Ultimate tic-tac-toe, though no board is highlighted then.
    evidence: |-
      Stays deferred (deferred-work.md `## 8.1`): the device delivers no input once the round is over and `input` builds only well-formed moves, so a player never meets it; checks.lua pins the wording, and a "Round is over" reason is a rule change that is the owner's wording call (Battleship says "The game is over"). The owner confirms at entry 5.
    location: >-
      games/ultimate-tic-tac-toe/main.lua (apply)
    severity: low
  - summary: >-
      Ultimate tic-tac-toe's HOW TO PLAY page is checked for fit only at the 474 x 788 canvas under stand-in text metrics and has no paging for a narrow canvas.
    evidence: |-
      Stays deferred (`## 8.1`): no v1 device has a canvas other than 474 x 788 (X4 Pro and Sticky), a check cannot change `ch.screen`, and only the simulator or a device shows real font fit (the simulator screenshots did). The owner confirms at entry 5.
    location: >-
      games/ultimate-tic-tac-toe/main.lua (help_lines), test/game_script/first_party/ultimate-tic-tac-toe/checks.lua (helpPageFits)
    severity: low
  - summary: >-
      No committed check draws Sudoku's bank-wide worst frame (the bank puzzle with the most visible marks, SHADE PEERS on, after FILL NOTES).
    evidence: |-
      Stays deferred (`## 8.3`): a host script (not committed) counted at most 1,293 commands over the 400 bank puzzles against the 2,048 limit, and rules.frame draws four dealt grids at 987 to 1,150; committing the bank-wide figure needs the 400 grids drawn in batches inside the checks VM, whose heap has 5 to 8 KB left. The bank is fixed by Decision D1; a bank or notes-drawing change reopens it.
    location: >-
      test/game_script/first_party/sudoku/checks.lua, games/sudoku/view.lua
    severity: low
  - summary: >-
      The games check and `ctest -L games-check` are documented only in test/game_script/first_party/README.md and a CI comment, not in AGENTS.md or docs/contributing/.
    evidence: |-
      Stays deferred (`## 8.8`): the fix edits AGENTS.md (the owner's agent-context file) and docs/, both outside this entry's touches; first_party/README.md, which is in touches, already documents the check, the rounds format, the module-nesting rule, and the draw-command patterns. The owner confirms at entry 5.
    location: >-
      AGENTS.md, docs/contributing/
    severity: low
  - summary: >-
      Sudoku's checks.lua VM has about 6.5 KB of heap headroom, so a larger bank or more checks may run it out of memory.
    evidence: |-
      Measured by the implementer on this tree: a live string of n bytes added to checks.lua before its last collection passes at 6,500 B and fails at 6,600 B (entry 3 measured 5 to 8 KB at `e84e5fa3`). Documented in first_party/README.md ("The checks VM heap") and checks.lua's header. The fix (a smaller load, or a larger heap for the check's own VMs) is an arena or bank decision the sweep does not take; the bank is fixed (Decision D1), and entry 5 may only lower the cost cap.
    location: >-
      test/game_script/first_party/sudoku/checks.lua
    severity: low
---

<intent-contract>

## Intent

**Problem:** Entry 4 is cleanup only (R14): the open items the epic met are fixed, documented, or deferred, each deferral with a reason the owner confirms at entry 5. The three build records and `deferred-work.md` `## 8.1`, `## 8.2`, `## 8.3`, `## 8.8` leave these: the games check accepts a module loaded inside another module's load (the simulator refused it for Sudoku, the host passed every check); the check sees only text commands, so a highlight, an icon, a won board's mark, and Battleship's draw-level secrecy are proven by screenshots alone; Battleship's Over labels sit tight against their boards; Sudoku's rounds and checks repeat code.

**Approach:** Close what the check and the companion folders can close, in `touches` only: a generic module-nesting rule in the games check (the double pinned to the sandbox), the recording-`ch.gfx` pattern documented in `first_party/README.md` and used in each game's companion folder, Battleship's labels, and the Sudoku duplication. Document or defer the rest with a reason.

## Boundaries & Constraints

**Always:** Behaviour-preserving for every game (rules, state, moves, snapshots); the rounds format (epic Notes C1) and the `checks.lua` interface (C2) unchanged; each game keeps its own copies (R2, R3); the harness and `scripts/` name no game; a double a story adds or extends names the device behaviour it stands in for, and a test pins the two together, saying where it is more permissive; a rewritten function keeps its guards (Design Notes).

**Never:** `src/**`, `lib/**`, `docs/crosshatch/api-level-1.txt`, `AGENTS.md`, any upstream file; Sudoku's clue look (epic Notes, 2026-10-05); `test/game_script/harness/CMakeLists.txt`; a file or `require` shared between games; `deferred-work.md` outside `## 8.1`, `## 8.2`, `## 8.3`, `## 8.8`.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Flat load | main requires `a` and `b` | `ThePackageInstallsAndLoads` green | none |
| Cached nested | main requires `b`, then `a`, and `a` requires `b` | green (`b` is already loaded) | none |
| Nested load | main requires `a`, `a` requires `b` (b not loaded) | red, names `main > a > b` | the failure says require `b` from main first or from a function body |
| Lazy require | a function body of main requires `a`, `a` requires `b` | green (not probed; documented, pinned) | none |
| Secrecy at draw level | two firing states differing only in where the other seat's intact ships lie | seat 1's and seat 2's frames record the same commands | red naming the first differing command |
| Highlight | tic-tac-toe, forced board 5 | `light` fills exactly on board 5's block; none once the round is over | red names the boards |
| Over labels | Battleship Over frame | label text ends 4 px or more above the boards, boards end above the dialog (y 259) | geometry check red |

</intent-contract>

## Code Map

- `test/game_script/harness/games_check/ScriptVm.{h,cpp}` -- `OwnedVm`, `ScriptVm::runChunk`/`inspect` (read a returned string, as `ScriptVmTest::viaVm` does); add the probe here.
- `test/game_script/harness/games_check/GameCheck.cpp` -- `checkPackage` (after the registry and hash checks), `installAndLoad`, `Installed::assets->sources()/images()`, `CHECKS_SEED`.
- `test/game_script/harness/games_check/GamesCheckEngineTest.cpp` -- scratch-tree idiom: `writeGame`, `write(games()/id/"a.lua", ...)`, `edit`, `round`, `pack`, `expectRed`/`expectGreen`, `padGame`, `soloManifest`, `ONE_TAP`.
- `test/game_script/harness/games_check/ScriptVmTest.cpp` -- the pin idiom: the same snippet through `DirectGame` (LuaGame) and `ScriptVm`; `LuaGameFixture.h` `modelTaskStack`; `SandboxTest.RequireNeedsParserHeadroom` is the sandbox's refusal (`lib/GameScript/Sandbox.cpp` `require`, `CallGuard::PARSE_HEADROOM_BYTES`); `LuaGame::setStackFloor`.
- `test/game_script/first_party/README.md` -- the companion folder's documentation.
- `test/game_script/first_party/sudoku/rules.lua` (`record`, `rules.frame`), `checks.lua` (lines 8-29: the depth wrapper), `taps.lua`, `rounds/{notes-dots,notes-digits,solve-easy,solve-medium,solve-hard,solve-expert}.lua`; `games/sudoku/{view,main}.lua` (what `rules.frame` pins; main's `require` comment).
- `games/ultimate-tic-tac-toe/main.lua` (`draw`: `light` rects under the grid, won-board `white` fill and big icon, `x`/`circle` icons, `question` icon) and `test/game_script/first_party/ultimate-tic-tac-toe/checks.lua`.
- `games/battleship/main.lua` (`drawFiring`, `drawFleet`, `drawTarget`, `drawBoth`, `game.views`), `layout.lua` (`L.over`, `L.over_label_y`), `test/game_script/first_party/battleship/checks.lua` (`build`, `firing`, `stateWith`, the geometry check `eq(L.over[1].y, 66)`).
- Not to change: `RoundPlayer.cpp` (no stack floor: see Design Notes), the games, other than `battleship/layout.lua` and one comment in `sudoku/main.lua`.

## Tasks & Acceptance

**Execution:**
- [ ] `games_check/ScriptVm.h`, `ScriptVm.cpp` -- add `inline constexpr size_t MAX_LOAD_NESTING = 2`, `struct LoadNesting { size_t depth; std::string chain; }` and `LoadNesting probeLoadNesting(OwnedVm&)`: a chunk that wraps the global `require` to count a name the first time it is required (a cache hit never nests), calls `pcall(require, "main")`, restores `require`, and returns the chain at the deepest nesting; a Lua error or fault of main's own load is ignored (the rounds name it) -- the stand-in for the sandbox's parser-headroom refusal
- [ ] `games_check/GameCheck.cpp` -- `checkPackage` runs the probe over the installed sources (an `OwnedVm` with no companion modules, `CHECKS_SEED`) and fails above `MAX_LOAD_NESTING`, naming the chain, the device's message, and the fix -- one rule for every game
- [ ] `games_check/GamesCheckEngineTest.cpp` -- the matrix's four load rows (red names `main > a > b`; a three-deep chain is red; the lazy row is green), over scratch games
- [ ] `games_check/ScriptVmTest.cpp` -- the pin: bisect the byte margin `X` of a modelled stack (`setStackFloor` at the test frame minus `PARSE_HEADROOM_BYTES` minus `X`) at which `DirectGame` loads the flat game, and assert the nested game is refused there ("script recursion too deep to load a module") while the probe flags exactly the nested one
- [ ] `first_party/README.md` -- add "Module loading" (the rule, what the probe sees, what it does not), "Observing draw commands" (the recording `ch.gfx` pattern, the secrecy pattern for a hidden game, the double's limits), and "The checks VM heap" (the sandbox's 256 KB shared by the game's modules, `checks.lua`, and everything it requires; Sudoku's headroom of 5 to 8 KB); update "What the check proves"
- [ ] `first_party/ultimate-tic-tac-toe/trace.lua` (new), `checks.lua` -- the recorder (`trace.record(f, on_call)` returns the commands as text and a count; its functions come from the real `ch.gfx` keys); entries pinning the highlight, won-board fill and big mark, `x`/`circle` icons, the help page's no board, and `draw_grid`'s command count
- [ ] `first_party/battleship/trace.lua` (new), `checks.lua` -- the same recorder; entries for draw-level secrecy (seats 1 and 2, firing and the waiting frames, with a positive control), icon and shape kinds (target: `waves`/`fire`/`boat`/`question` icons only; own fleet: no icons), and seat 0's Over frame holding every ship cell of both fleets
- [ ] `games/battleship/layout.lua`, `checks.lua` -- Over boards at `y = 72` (labels stay at 44), comments and the geometry check updated, a check that the label text box ends 4 px or more above the boards
- [ ] `first_party/sudoku/rules.lua` -- extend `record` with an `on_call` hook (keep its guards) and pin through `rules.frame` and a new `rules.draw_marks`: SHADE PEERS (`light` fills equal the selected cell's peers that are neither clue nor focus ground), SHOW REMAINING (the nine counts), the full refresh (once on the first frame and on each panel change, never on a board tap), clash strokes (rising) and CHECK strokes (falling)
- [ ] `first_party/sudoku/checks.lua`, `games/sudoku/main.lua` -- delete the depth wrapper and its assert (the games check covers it), fix both comments, and say the checks VM's 5 to 8 KB headroom and how it was measured
- [ ] `first_party/sudoku/taps.lua`, the six rounds -- one helper for the three-cell notes loop (`notes-dots`, `notes-digits`) and one for the solve-and-show-best tail (`solve-*`), with identical steps
- [ ] `_bmad-output/implementation-artifacts/deferred-work.md` -- `## 8.1`, `## 8.2`, `## 8.3`: each item this entry closes reads "Resolved by entry 4 of epic-first-party-games (ticket 8.4; ...). It read: <old summary>", as `## e5-close` V3 does; the others and `## 8.8` unchanged

**Acceptance Criteria:**
- Given a scratch game whose main requires a module that requires another, when `ThePackageInstallsAndLoads` runs, then it is red naming the chain; given main requiring both, or the second already loaded, then green.
- Given Battleship with `drawFiring` reading the other fleet directly (a scratch mutant), when the games check runs, then a check is red; given the shipped draw, green.
- Given tic-tac-toe with the `light` fill loop or the won-board mark removed (mutants), then a check is red.
- Given the Battleship Over frame in the simulator, then each label has clear space above its board and the boards clear the dialog.
- Given the three games, when the games check runs, then every test passes and no snapshot or round changes.

## Implementation Notes

Implemented by one subagent (route: full), verified by the build agent: full host suite 1761 of 1761, `-L games-check` 83 (79 plus 4 new tests), every `scripts/*_test.py` and `check_upstream_touches.py` pass.

Deviation from the plan, accepted at verify: Battleship's draw-level pins (`secrecy`, `kinds`, `over`) live in `test/game_script/first_party/battleship/draws.lua`, called from the rounds `won-by-seat-1`, `won-by-seat-2`, and `clear` through `steps(state)`, not in `checks.lua`. Battleship's checks VM holds about 190 KB live of the sandbox's 256 KB and one frame's draw leaves about 47 KB of garbage, so the entries faulted "not enough memory" there; Sudoku's `rules.lua` is the same arrangement (entry 3). A red there fails a round (`EveryRoundPlaysAsItsFileSays`), not a check; the README says so. Tic-tac-toe's entries stay in `checks.lua`.

Other points: the probe is `probeLoadNesting` in `ScriptVm`; Sudoku's depth wrapper is deleted; Sudoku's checks-VM headroom re-measured by the implementer (a live string of n bytes added before the last collection passes at 6,500 B and fails at 6,600 B).

Review patches (implementation subagent, re-engaged): the probe fails on a chunk error or non-string result (`LoadNesting::error`, a test hook `script` argument, an engine test with a main that sets `require = nil`); the advice names the whole chain, deepest first; `ScriptVm.h` comment corrected; Battleship's pins moved to one round `rounds/draw-commands.lua` (the three edited rounds restored), a `draws.recorder()` pin, every positive control asserting the "differs" message, non-empty minimums (70 for firing frames, 30 for placement and waiting; measured 76 and 32 to 53), a control per comparison; Sudoku's `record` takes its names from the real `ch.gfx` keys (a misspelled call raises, scratch mutant red); the inverted label-gap message; `deferred-work.md` `## 8.1`-`8.3` evidence lines rewritten as the resolution (the draw-only item names what stays screenshot-only); README updated.

## Plan Change Log

## Review Triage Log

### 2026-10-05 — Review pass
- verdicts: 26 findings — high 0, medium 6, low 19, false 1, maybe-false 0 (blind-hunter 16, edge-case-hunter 10, verification-gap none, intent-alignment descriptive only, no findings)
- findings:
  - Blind Hunter
  - `[low]` `[patch]` the probe passes silently when its chunk cannot run (`if (ran.ok())`) — real: a chunk Error or a non-string result leaves depth 0 and the gate green; fix: an Error (main's raise is `pcall`ed inside the chunk) or a non-string result fails like a probe VM that cannot start; a guard Fault of main's own load stays ignored (the rounds name it), with an engine test for each.
  - `[low]` `[patch]` the advice names only the last module of a chain, so `main > a > b > c` is still red after requiring `c` first — real (the engine test pins that wording); fix: say to require the chain's modules below the first from main.lua, deepest first, and test a 4-deep chain's message. Same root as the edge-case row on the same advice.
  - `[low]` `[reject]` a mistyped or failing nested require is reported as nesting — the rounds fail the same game with the sandbox's own "module not found" at its load; the probe's message is not wrong about the depth it counted, and a fix adds an existence check to the wrapper.
  - `[low]` `[reject]` cycles and an error caught inside main are untested, and `raises-late` does not assert one failure — the sandbox names a cycle itself; the probe changes nothing for either; a direct correction would add cases for paths no game has.
  - `[low]` `[patch]` Battleship's recorder is untested and its comment names a check (`draw_grid`'s 28 commands) that only tic-tac-toe has — real; fix: the comment says what pins it, and a `draws.recorder()` pin (count, text, an unknown name raises, `ch.gfx` restored after an error) in Battleship's companion folder. Same root as the edge-case claim row.
  - `[low]` `[patch]` three recorders, the README says each raises on an unknown name but Sudoku's `record` accepts any (and a misspelled call passes) — real; fix: Sudoku's `record` takes its names from the real `ch.gfx` like the other two (a mutant calling a misspelled function is red). The two `trace.lua` copies stay byte-identical on purpose (R2), no drift check.
  - `[medium]` `[patch]` Battleship's `draws.*` hang on three unrelated rounds and nothing says all are called — real: dropping or renaming a round silently drops a pin; fix: one round, `rounds/draw-commands.lua`, calls every `draws.*` function (the three existing rounds go back to their original steps; if one VM runs out of memory, two rounds named for what they pin), and `draws.lua` says it.
  - `[low]` `[patch]` the label-gap assert text says "sit tight" for the condition that fails when they do — real, wording inverted in `checks.lua` and `draws.lua`; fix: "must sit at least 4 px above".
  - `[low]` `[reject]` `SMALL_LINE = 24` and the icon pixel sizes are copied constants — the 24 is the game's own wrap step, named in both places, and the engine's sizes are API level 1's fixed 32, 64, 128; no divergence is shown.
  - `[low]` `[reject]` the layout change is mixed into the observability work, with no screenshot and no manifest version bump — the sweep names this change (orchestrator list, `story-battleship-screenshots/over-menu.png`); the screenshot is a step after the review; a package's hash, not its version, is what the installer compares.
  - `[medium]` `[patch]` the resolved `deferred-work.md` entries keep evidence lines that say the opposite (8.2 "Never list", 8.3 "the harness gap stays", the draw-only item "still proven by screenshots alone") — real; entry 5 reads them: fix: each resolved item's whole text becomes the resolution, its old reading kept only as "It read", and the draw-only item names what is still open. Same root as the edge-case row.
  - `[low]` `[defer]` 8.3's nesting item is marked resolved while a module required lazily (a function body) is not probed — true and documented; carried as a new deferred item rather than reopening the entry.
  - `[low]` `[reject]` no regression test shows the real Sudoku fails without its `board` preload after its wrapper was deleted — `GamesCheckTest` runs the probe over the real game, the rule is generic, and the nesting mutant (`layout` before `board`) was run red in `ThePackageInstallsAndLoads`.
  - `[false]` `[reject]` the Sudoku round dedupe is an unrelated refactor — the ticket's description names "duplicated code" in the games' companion folders and the plan lists it; this is the sweep itself.
  - `[low]` `[patch]` brittle or unexplained details: `ch.store.set({})` unexplained, a pointless `events = nil` — fix: a comment, delete the line; the pixel offsets in `watch()` are what the check pins and the headroom figures are dated and measured: rejected.
  - `[low]` `[defer]` the instruction margin of `steps(state)` in the `toggles` round (about 20 Sudoku frames and 81-cell loops) is unreported — the round passes, so it is under 2,000,000; a figure needs a counting hook the sandbox lacks; carried as a deferred item.
  - Edge Case Hunter
  - `[medium]` `[patch]` the positive controls in `draws.secrecy` pass on any error, so a draw that runs out of memory passes them vacuously — real (the checks VM already did run out of memory at 47 KB a frame); fix: the control must fail with the "differs" message (`sameFrames`' text), and the error text is asserted.
  - `[medium]` `[patch]` `count > 50` reads only the last iteration and the placement and waiting comparisons have no non-empty guard, so empty frames compare equal — real; fix: every comparison asserts its commands are non-empty (a minimum for the firing frames) and `sameAcross` returns the least count seen.
  - `[low]` `[patch]` advice for chains deeper than three names only the last module — same root as the blind-hunter advice row; patched there.
  - `[low]` `[reject]` `seen[name]` is set before the load, so a retried load counts as a cache hit — a module whose load raised and was caught and retried is no game's pattern, and the sandbox forgets a half-loaded module so a retry loads again, which the double then misses; carried in the plan's "more permissive" list, not worth wrapper state.
  - `[low]` `[patch]` Battleship's recorder comment claims a check it has — same root as the blind-hunter recorder row; patched there.
  - `[low]` `[patch]` README claims every recorder raises on an unknown name; Sudoku's accepted any — same root; patched there.
  - `[medium]` `[patch]` only the firing and seat-1 placing comparisons have a positive control — real: a waiting or placing draw that records nothing passes; fix: a control for each comparison (a difference the frame may show is found, with the "differs" message).
  - `[low]` `[reject]` the plan's tasks name `checks.lua` for Battleship's draw pins and the delivered files differ — the deviation is recorded in Implementation Notes (accepted at verify); a plan edit is no fix.
  - `[low]` `[patch]` `ScriptVm.h` says a raising main gives an empty chain, but the wrapper records `main` before its load so a raise gives chain "main", depth 1 — real comment error; fix the comment.
  - `[medium]` `[patch]` the `deferred-work.md` evidence lines contradict the Resolved summaries — same root as the blind-hunter row; patched there.

## Design Notes

**Why a depth rule and not a stack floor.** The sandbox refuses a module's parse with under `PARSE_HEADROOM_BYTES` (10 KiB) of the 16 KiB VM stack free (`Sandbox.cpp`); the simulator refused Sudoku's `main > layout > board` (`story-sudoku-plan.md`, review row "found at the simulator step"). The device figure is unmeasured. Prototype, measured 2026-10-05 on this Release host build (reverted): `RoundPlayer` with `lua->setStackFloor(frame - VM_STACK_BYTES)` accepts a module chain five deep and refuses the sixth, so the host's frames are about a third of the simulator's and bytes are no usable bound here; a depth rule, as Sudoku's `checks.lua` already used (`deepest <= 2`, first requires only), gives the simulator's answer on any build. The probe is stricter in one way (depth, not bytes: it can fail a load the device takes) and more permissive in another (only main's own load is probed, so a module required in a function body, `draw` or `input` is not, and lazily loaded nesting is the author's). It is `ScriptVm`'s double of `Sandbox.cpp`'s refusal; the pin test shows a stack margin at which the sandbox itself refuses the nested game and accepts the flat one, and the probe agrees on both.

**Guards kept.** `Sudoku` `record` (rules.lua): restores `ch.gfx` after `pcall` and re-raises the error (a failed draw must not leave the recorder installed), counts one per call, hands image calls to the callback. `checkPackage`: every early return stays; the probe runs after the hash comparison and adds no return. Sudoku's wrapper is deleted, not moved: the harness rule replaces it (`seen` first-require counting and `deepest <= 2` are the probe's).

**Draw-level observation.** `RoundPlayer` keeps text commands only (`## 8.1`, `## 8.2`); a new `shows`-style key would change the owner-approved rounds format (C1), so the check stays as it is. Entry 3's follow-up showed a recording `ch.gfx` called from a round's `steps(state)` or a check observes every command; each companion folder keeps its own recorder (R2). It is a double of the engine's `ch.gfx`: it takes the engine's function names from the real table (a name the engine lacks fails on the recorder too) and counts each call as one command, and it is more permissive: no argument check, no clipping, no frame or image limits, no icon or image lookup. A check pins the recorder against `board.draw_grid`'s documented command count.

**Settled by existing text.** Duplicated code across games (`wrap`, `help` text wrapping, `eq`, `taps.*`, the recorder) stays: R2 and R3. Entry 7's fixture and tests: its rejected rows (a pass branch never run, one keep only) are reasons, not defects; no change. The Sudoku clue look stays (epic Notes, 2026-10-05). Stay deferred, with reasons for the owner at entry 5: the wording of a rejected move after the round is over and the narrow-canvas help page (`## 8.1`: unreachable by a player on a 474 x 788 canvas, wording is the owner's); the bank-wide worst Sudoku frame (`## 8.3`: measured 1,293 of 2,048, the bank fixed by D1); `## 8.8` (AGENTS.md is the owner's; `first_party/README.md` already documents the check); the checks VM's headroom (fix is an arena or bank decision). No firmware file changes, so no `pio run` or `pio check`; the game change is drawing, so `sim.sh build x4pro` and a fresh Over screenshot.

## Verification

**Commands (host tests and fast checks first; locks as AGENTS.md says; work only in `/home/user/epic-first-party-games-lane-a`):**
- `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test'`, then `ctest --test-dir build/test --output-on-failure -j` -- expected: all pass (`-L games-check` was 79 of 79 at `4137d947`).
- Mutants over scratch roots in `build/test/games_check_scratch/` with `-DGAMES_CHECK_GAMES_ROOT=... -DGAMES_CHECK_COMPANION_ROOT=...`, reset with `-U...`: tic-tac-toe without the `light` loop and without the won mark; Battleship `drawFiring` drawing `views` from `state.f[3 - seat]` and `drawTarget` drawing an icon on an intact cell; Sudoku with `refresh` dropped, a shade fill added or removed, the pad counts off, `stroke`'s direction swapped; Sudoku's main loading `layout` before `board` -- expected: each red, the shipped tree green.
- `for t in scripts/*_test.py; do python3 $t; done`, `python3 scripts/check_upstream_touches.py`, `./bin/clang-format-fix` twice -- expected: pass, nothing new.
- After the review, once: `sim.sh build x4pro` under the build lock, pack `battleship` into `fs_/games/`, play two placements and a win, and screenshot Over to `story-refactor-sweep-screenshots/over-menu.png`.
- Fresh tree of the committed work (a `git archive` tree plus every submodule): the `games-check` job's configure, build, and `ctest -L games-check --no-tests=error`.

**Results (build agent, worktree `/home/user/epic-first-party-games-lane-a`, after the review patches):**
- Host: full `ctest --test-dir build/test -j4` 1763 of 1763 pass (`-L games-check` 85: 79 at `4137d947` plus 6 new); every `scripts/*_test.py` passes; `scripts/check_upstream_touches.py` PASS (no ledger edit; the changed files are fork-only); `./bin/clang-format-fix` twice, the second run changed nothing (diff checksum identical before and after).
- Mutants (the implementer, scratch roots in `build/test/games_check_scratch/`, since reset; the shipped tree green): tic-tac-toe without the `light` loop, without the won-board white fill, without the big mark, with the small-icon offset, and with a board drawn on the help page; Battleship `drawFiring` reading the other raw fleet (two variants), an icon on an intact target cell, a waiting-frame leak, Over boards back at y 66, Over missing ships; Sudoku refresh dropped, refresh on a board tap, a shade fill added and removed, pad counts offset and unclamped, strokes swapped, a misspelled `ch.gfx` call, and `main` loading `layout` before `board` (red as `main > layout > board` in `ThePackageInstallsAndLoads`): each red. Not re-run after the review patches except the misspelled-call mutant and the games-check suite (85 of 85).
- Simulator: `sim.sh build x4pro` under the build lock, SUCCESS (15.6 s with the shared cache). `battleship` packed with `scripts/pack_game.py` into `fs_/games/` (hash 63a06f0a7d99d968), played through two placements and a 17-shot win by seat 1 from Games, and the Over frame looked at:
  - `story-refactor-sweep-screenshots/over-menu.png`: seat 0's frame under the Game over menu: "Player 1" and "Player 2" now sit clear above their boards (about 6 px), both fleets and the shot counts show, and the boards end about 6 px above the dialog.
- No firmware file changed (`src/`, `lib/`, `platformio.ini`), so no `pio run` or `pio check`. Flash, RAM, and device timing: unmeasured.
- Fresh tree: a `git archive` tree of code commit `bc3d0d5e` plus every submodule's archive (nested ones included), not a clone, reusing the warm `~/.platformio`: the `games-check` job's configure (`cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release`), build of `GamesCheckTest GamesCheckEngineTest GamesCheckFlowTest` (145 steps, 49 s), and `ctest -L games-check --output-on-failure --no-tests=error`: 85 of 85 pass. The gate reads no git history. The commit was then amended with this plan text only; no other file changed after the measurement.

**Manual checks:** the Over screenshot shows the labels clear of the boards and the boards clear of the dialog.

## Auto Run Result

**Summary.** The sweep closes what the games check and the companion folders can close and defers the rest with reasons.
- The games check gains a generic module-nesting rule: `checkPackage` fails any game whose main's own load nests modules deeper than main plus one level (the simulator refused `main > layout > board` for Sudoku while every host check passed). The probe (`ScriptVm.h` `probeLoadNesting`) is a depth double of the sandbox's parser-headroom refusal, pinned to the sandbox by a bisected stack-margin test and documented as stricter (depth, not bytes) and more permissive (main's own load only). A stack floor was measured and rejected: this Release host accepts a five-deep chain with the device's 16 KiB modelled.
- Draw-level observation, through a recording `ch.gfx` in each companion folder (the rounds format C1 is unchanged): tic-tac-toe pins the highlight, the won-board fill and big mark, the icons, and the help page's no board (`checks.lua`); Battleship pins draw-level secrecy (frames equal across states differing only in the other seat's intact ships, with positive controls), the icon and shape kinds, and seat 0's Over frame, in `draws.lua` run from `rounds/draw-commands.lua` (the checks VM's heap has no room); Sudoku pins refresh, SHADE PEERS, pad counts, and both stroke kinds in `rules.lua`.
- Cleanup: Sudoku's local depth wrapper is deleted (the harness rule replaces it), its notes and solve rounds share `taps.notes_three` and `taps.solve_best`; Battleship's Over boards move 6 px down so the labels clear them.
- Deviation: Battleship's draw pins are a round, not `checks.lua` entries (heap); a red there fails `EveryRoundPlaysAsItsFileSays`.

**Files.** Harness (all in `test/game_script/harness/games_check/`): `ScriptVm.{h,cpp}` (probe), `GameCheck.{h,cpp}` (`checkPackage` runs it), `GamesCheckEngineTest.cpp` and `ScriptVmTest.cpp` (tests and the pin). Docs: `test/game_script/first_party/README.md` (Module loading, Observing draw commands, The checks VM heap). Companions: `ultimate-tic-tac-toe/{trace.lua (new), checks.lua}`; `battleship/{trace.lua, draws.lua, rounds/draw-commands.lua (new), checks.lua}`; `sudoku/{rules.lua, checks.lua, taps.lua, rounds/{notes-dots,notes-digits,solve-easy,solve-medium,solve-hard,solve-expert,toggles}.lua}`. Games: `games/battleship/layout.lua` (Over boards y 72), `games/sudoku/main.lua` (a comment). Records: `deferred-work.md` (`## 8.1` to `## 8.3`, four items resolved with their evidence rewritten), this plan, `story-refactor-sweep-screenshots/over-menu.png`.

**Review.** 26 findings from the four lenses (blind-hunter 16, edge-case-hunter 10, verification-gap none, intent-alignment descriptive): high 0, medium 6, low 19, false 1. Patched (patch entries): 6 mediums (Battleship pins tied to unrelated rounds; positive controls passing on any error; non-empty guards; controls missing per comparison; the stale `deferred-work.md` evidence lines, twice) and 8 lows (probe failing silently, advice for deep chains, the `ScriptVm.h` comment, recorder comment and README claim, Sudoku's permissive recorder, inverted assert text, unexplained lines). Deferred: 7 items in the `deferred` list (two carry new reasons from review: lazy nesting unprobed, the `toggles` round's instruction margin; five are the epic's remaining items, each with the reason the owner confirms at entry 5). Rejected with the reasons in the Review Triage Log: 11 (a mistyped nested require reported as nesting, cycles untested, copied constants, the layout change bundled, no real-Sudoku regression test, the dedupe as unrelated, the retried-load wrapper state, the plan/diff mismatch, and three others). `followup_review_recommended: true`: five medium entries were patched, and the specific unverified risk is that the rewritten Battleship `draws.lua`, `rounds/draw-commands.lua`, the Sudoku recorder change, and the probe's failure branch were reviewed only through the build agent's own host run and the author's mutants.

**Verification.** See Verification Results: host 1763 of 1763 (games-check 85), fast checks green, formatting stable, `sim.sh build x4pro` SUCCESS, one screenshot looked at, the games-check job's commands from a fresh archive tree 85 of 85.

**Formatting.** `./bin/clang-format-fix` changed no file outside this story's paths.

**Residual risks.** The nesting probe is a depth rule calibrated on one simulator observation; the device's depth limit is unmeasured (entry 5's run would show a lazy chain). The recorder doubles check no arguments, clip nothing, and apply no frame limits. Battleship's pins fail as a round. Entry 9 (lane B) edits Sudoku's `view.lua`, layout, and companion checks, so `rules.frame`'s SHADE PEERS and stroke pins will change at that merge; the orchestrator resolves it. Screenshots: `story-refactor-sweep-screenshots/over-menu.png` shows Battleship's Over frame with the labels clear of the boards.
