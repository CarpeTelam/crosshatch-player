---
epic: epic-first-party-games
date: 2026-10-11
verdict: accepted-with-open-items
criteria: declared
headless: false
---

# Retrospective: epic-first-party-games

Status: complete. Run interactively. The owner's decisions are recorded under Action items, and the verdict is under Acceptance verdict.

## Epic summary

Epic: `epic-first-party-games` (id 8), "Three first-party games ship with each release". Risk low. Criteria: declared (the epic file has six Done when items).

Tickets (from `tickets.py status`, build order):

| ref | title | status | state |
|-----|-------|--------|-------|
| 8.8 | The games check | done | done |
| 8.1 | Tracer: the games check and Ultimate tic-tac-toe | done | done |
| 8.7 | Pass resume when a move keeps the turn | done | done |
| 8.2 | Battleship | done | done |
| 8.3 | Sudoku | done | done |
| 8.4 | Refactor sweep | done | done |
| 8.10 | Raise the package file cap to 64 | done | done |
| 8.9 | Sudoku digit notes in three bold sets, and the greys swapped | done | done |
| 8.11 | Check every game at the X4 Pro's 466-pixel canvas | done | done |
| 8.12 | Fix the release script's game-packer path | done | done |
| 8.13 | Stop the game clock while the match is paused | done | done |
| 8.14 | Log each game touch, and give Battleship and Sudoku their launcher icons | done | done |
| 8.15 | Count a tap in the margin beside a game's edge cells as that cell | done | done |
| 8.16 | Show Sudoku's play time in the header, with a TIMER toggle in the menu | done | done |
| 8.17 | Make Sudoku's NOTES AS default to DIGITS | done | done |
| 8.5 | Device run and owner sign-off | done | done |
| 8.6 | Fork release with the three games | done | done (2026-10-11, during this retro) |

`pending_tickets` at the start of the run: **8.6** ("Fork release with the three games", `hitl`, after 8.5). On 2026-10-11 the owner accepted running the retro over it. Ticket 8.6 is the post-merge publishing release (R13, Done when 4), so it could not have run before the merge.

During Phase 4 the owner asked to finish the epic. The owner then confirmed the target, and 8.6 was done in this session: run 38106383670 published release `1.6.5-ch.4` from `develop` at `f84399ed`, the hashes were checked, and `story-fork-release-with-the-three-games-plan.md` was written. `pending_tickets` is now empty, and no tickets are still at `built`.

Delivery: the epic landed on `develop` as merge `39061f44` (CarpeTelam/crosshatch-player#35, branch `claude/stoic-bardeen-0768gs`, head `5cfd4489`). The base was `0bf0b3d0`, which is the epic base `eca7e6c7` plus four `develop` doc commits merged in at `715e67c7`. The diff `0bf0b3d0..5cfd4489` is 269 files, +22,209 / -108.

### Plan ranges

Every plan's `baseline_revision` lies on the epic branch's first-parent spine, in ancestor order (checked with `git merge-base --is-ancestor`). Those baselines are orchestrator commits, though, and the builds ran in two lanes and landed later through merges. So a baseline-to-baseline range holds whatever merged in between, not the ticket's own work. For example, 8.7's baseline range `eca7e6c7..d6aa0d3c` has one docs commit. This retro therefore attributes each ticket by its `Merge entry 8.N …` commit, using the range `M^1..M^2`. The baseline ranges were run too, and their output is kept in the scratchpad (`ev/`).

| ticket | baseline | merge | range | commits | files | +/- |
|--------|----------|-------|-------|---------|-------|-----|
| 8.7 | `eca7e6c7` | `146a657d` | `146a657d^1..^2` | 1 | 8 | +289 / -5 |
| 8.8 | `d6aa0d3c` | `a9e8596f` | `a9e8596f^1..^2` | 2 | 21 | +4,470 / -109 |
| 8.1 | `d3795e88` | `344da6c5` | `344da6c5^1..^2` | 1 | 17 | +974 / -120 |
| 8.2 | `d0d488a2` | `3c61b84b` | `3c61b84b^1..^2` | 1 | 19 | +2,058 / -0 |
| 8.3 | `847e7bc2` | `1f06d3a7` | `1f06d3a7^1..^2` | 2 | 63 | +3,887 / -23 |
| 8.4 | `4137d947` | `068ab322` | `068ab322^1..^2` | 2 | 28 | +1,617 / -104 |
| 8.10 | `71b78fb1` | `bb1efc43` | `bb1efc43^1..^2` | 1 | 12 | +146 / -17 |
| 8.9 | `af31d765` | `cf16fe5c` | `cf16fe5c^1..^2` | 3 (1 merge) | 44 | +778 / -205 (27 PNGs unmeasured) |
| 8.11 | `7699d2e0` | `c1fff07a` | `c1fff07a^1..^2` | 2 | 70 | +2,111 / -896 |
| 8.12 | `ffbb4c7f` | none (on the spine) | `54c2a4ea` | 1 | — | — |
| 8.5 | `daedbeab` | `0f0bb0ca` | `0f0bb0ca^1..^2` | 3 | 10 | +726 / -55 (docs only) |
| e8-xr | — | `14428a56` | `14428a56^1..^2` | 1 | 47 | +2,420 / -336 |
| 8.13 | `9fa5e0c2` | `0c149819` | `0c149819^1..^2` | 2 | 22 | +801 / -60 |
| 8.14 | `1ccef0db` | `4a87ed90` | `4a87ed90^1..^2` | 2 | 31 | +1,362 / -101 |
| 8.15 | `9c9fd3df` | `00699a05` | `00699a05^1..^2` | 1 | 14 | +818 / -10 |
| 8.16 | `4171f69c` | `1ac08331` | `1ac08331^1..^2` | 1 | 18 | +508 / -34 |
| 8.17 | `fced14c2` | `e4940989` | `e4940989^1..^2` | 1 | 9 | +160 / -27 |
| 8.6 | — | — | — | — | — | not started |

Notes on the table:

- The figures come from `git_evidence.py` `files`, which counts non-merge commits only and includes `_bmad-output/`.
- The spine also carries orchestrator docs and chore commits between the merges, such as decisions, packet repacks and dry-run records.

### Evidence inventory

Available:

- the epic file
- the initiative requirements
- 16 plans
- `cross-story-review.md` (over `eca7e6c7..daedbeab`, with the `e8-xr` fix review)
- `device-run-packet.md` and its three device-run records
- `deferred-work.md` sections `## 8.*`, `## e8-xr`, `## e5-run2` and `## e5-run4`
- the previous retrospective (`epic-pass-and-play-retrospective.md`)
- per-merge git evidence
- the merge commit, and CarpeTelam/crosshatch-player#35

Missing:

- Session logs for the build dispatches. The orchestrator's scratchpads (`xr/*.md`, `e8-xr/lenses/`) are not in the repo. So process-lesson analysis is limited to what the plans and Notes record.
- A diff for 8.5 (docs only).

## Findings

### Provenance

Four read-only subagents produced these findings. Their reports are unverified until they are re-checked. Each finding below is marked one of two ways:

- **verified**: I re-read the cited source or ran it.
- **agent**: the finding rests on the subagent's report only.

The four analyses, and what each covered:

- **Aggregate views.** These ran over `0bf0b3d0..5cfd4489`, read at `5cfd4489`. Code analysis excluded `_bmad-output/`, `*.png` and `games/sudoku/puzzles.lua`.
- **Spec reconciliation and CI.** These were checked against the epic file, the plans, the packet, and PR #35's check runs.
- **Diff-scope review** (`bmad-review`). Its scope was narrowed to `daedbeab..5cfd4489`. That is the code that landed after the epic's own cross-story review (`cross-story-review.md`, over `eca7e6c7..daedbeab`, with rows 1–24 and the `e8-xr` fix review), so this retro does not repeat that review. The scope was 86 files and 7,776 diff lines, weighted to the boundaries between tickets 8.12–8.17 and `e8-xr`.
  - **Lenses:** all three reported. The adversarial lens finished after the review handed back, and its report was read from its transcript after a container restart; its findings are A-1 to A-11 below.
  - **Also:** the reviewer checked the named boundaries by hand against the code. Those checks are listed under "Boundaries checked clean". The edge-case lens also checked the claims in eight commit messages against the code (`9c56ee81`, `de1c8aa1`, `d4e03a5d`, `38b9104c`, `e14dd42f`, `8a0b72bf`, `54c2a4ea`, `4fdc3176`) and found none false.
- **Previous-retro follow-through.** This was checked at HEAD `f84399ed`.

Each finding also gets a disposition for this instance: fix now, defer, or accept.

### Aggregate view: spec-to-implementation reconciliation

| id | finding | source | status | disposition |
|----|---------|--------|--------|-------------|
| S1 | **R2 is broken, and no Decision covers it.** R2 and R10 say the repository's tooling "names no game". Yet the games check's CMake hard-codes Sudoku's companion tools and registers two Sudoku-only ctests, `GamesCheckNoteImages` and `GamesCheckSudokuIcon`. Both are guarded by `if(EXISTS)`, so they vanish silently when Sudoku leaves. The epic's own cross-story review prescribed the first (row 8: "Register `make_note_images.py --check` as a ctest … in `games_check.cmake`"). Entry 14 repeated the pattern while its plan claimed "The games check names no game". HEAD still has both. | `test/game_script/harness/games_check.cmake:20-21,122-137` at `5cfd4489` and at HEAD; `4fdc3176` (e8-xr); `8a0b72bf` (8.14); `cross-story-review.md` row 8 | verified | fix now: discover any `first_party/<id>/tools/*` that supports `--check` generically, or amend R2 by owner Decision |
| S2 | **Done when 4 and R13's publishing half were not met at the start of the run** (since met: see Acceptance verdict). Entry 8.6 is `planned` and no release has been published since 1.6.5-ch.3 (2026-09-27, no `.chgame`). The dry-run half is met: run 38025954968 on `273fa892` packed all three games, and the package hashes and SHA-256 equal the packet's. `games/` and `pack_game.py` are unchanged from `273fa892` to HEAD. | `tickets.py status`; `device-run-packet.md` "Release dry run"; GitHub releases | verified (status, packet); agent (releases list) | fixed: 8.6 done in this session |
| S3 | **R12 and R14 are partly met.** The owner has not answered any row of the packet's deferral table ("their answers are still owed"). The sign-off was "Go ahead and merge", which the orchestrator recorded as the sign-off, and the Correction at Notes L168 says the explicit confirmation is owed at this retro. Several R12 items were never recorded: the "at most 3 taps from Home" counts, B6 (sleep from Over), video timings for HINT and CHECK (log bounds only: ≤699, ≤1,658 and ≤1,166 ms), and the per-try results of 5.13's B5. | `device-run-packet.md` "Owner answers: this epic's deferrals" (preamble), "What to record", Step results U1/B1/S1/B5/B6, "Sign-off"; epic Notes L168 | verified | owner answered in this retro: sign-off and deferrals confirmed (Action items); the unrecorded items stay unrecorded |
| S4 | **Superseded Requirement text was never edited.** Fifteen places in R2–R14, the C1/C3 Decisions and Boundaries no longer describe the build. Examples: R4 still says "18 package images of about 10 × 14 px" (now 27 at 12 × 16, L115); SHADE PEERS is still `"light"` (now `"dark"`, L116); "No clock is drawn while solving" (now header minutes and TIMER, L157); dot notes as the default (now DIGITS, L161). R2/R14 and Done when 3 say "adds nothing to the API" against entries 10 and 13. R3 says "computed from `ch.screen`" against the fixed 466 box (L123). C3's 51 px formula is stale too. No commit that superseded a line edited the Requirement it superseded. | epic file L25–38, L101, L103; `git show <c> -- epic-first-party-games.md` for `c93574d6`, `11825408`, `030db0e8`, `4171f69c`, `3b4c2a3f` | verified (R4 lines grep) / agent (rest) | spec reconciliation; this is the same lesson as the previous retro's AI-9 |
| S5 | **The engine changed despite "no firmware change", under owner Decisions.** The package member cap went from 32 to 64 (L118, entry 10). `ch.time.ms` now reports play time (L128, entry 13), and its meaning changed with no `API_SURFACE_CRC` change, by design (`api-level-1.txt:87-97`). Six `src/`/`lib/` commits cost +1,376 B of flash and +0 B of static RAM, measured like for like at `0bf0b3d0` and `39061f44` (Notes L165-166). Notes L75 and L106 ("every delta … should be zero") were never annotated. | Notes L118, L128, L165-167; `lib/GameCore/ApiLevel.h:20` | verified (Notes) | accept the deviations. Who pays the 1,376 B is still the owner's call (`epic-play-nearby.md` e6pre-14). The freeze epic should note that firmware from before entry 13 gives games a different clock. |
| S6 | **Built with no Requirement, each through an owner Decision or a device finding:** touch logging (`GameTouchLog.h`), launcher icons (Battleship's `boat`, Sudoku's `icon.png` and its tool), edge-margin snapping, the fixed 466 × 788 design box with `docs/crosshatch/game-canvas.md`, `PauseClock`, the release-script fix (outside the Boundaries, under Finding L125), and the games check's extensions from `e8-xr` (resume replay, 16 KiB heap margin, check-VM limits). | Notes L122-130, L147, L157; agent report C | agent | accept |
| S7 | **Done when 1, 3, 5 and 6 are met.** Specifically: no game-specific C++ (the only `src/`/`lib/` hit is a comment at `PackageLimits.h:17`); the 700 B snapshot ceiling asserted at every step (`RoundPlayer.h:51`, pinned 700 green and 701 red); every icon a game names is in `api-level-1.txt`; the `games-check` job is in `Crosshatch Test Status`'s `needs` (`crosshatch-ci.yml:372-416`); and all 22 check runs on PR #35's head `5cfd4489` passed, except the label-gated Game packages run, which was skipped. That covers the five build envs `ci.yml` had then, unit-tests, cppcheck, x4pro static analysis, clang-format, the flash budget, the ledger, games-check, and both status jobs. Done when 2 is met with a deviation: tap counts were not recorded. | PR #35 check runs (GitHub MCP); `crosshatch-ci.yml`; `RoundPlayer.cpp:115-121` | agent (CI read through the GitHub MCP); verified (`games_check` labels, host run below) | accept |

### Aggregate view: architecture delta

| id | finding | source | status | disposition |
|----|---------|--------|--------|-------------|
| A1 | The epic touched no upstream file. Its 269 files do not overlap `upstream/develop`'s tree, and `check_upstream_touches.py --ref 5cfd4489` passes. `freeink-sdk` and `.agents/` are unchanged. | agent commands | agent | accept (clean) |
| A2 | The games stay self-contained. No game shares a file or a `require` with another, and none names a repository path. The two `board.lua` copies are byte-identical (88 lines), and entries 8.11 and 8.15 edited both the same way. Nothing tests that the copies stay identical; R3 intends that. Sudoku's copy carries three functions it never calls (`origin`, `block_rect`, `tap_cell`), and its `layout.snap_cell` re-implements `board.tap_cell`. | `games/*/board.lua`; `games/sudoku/layout.lua:91` | agent | accept |
| A3 | The harness gained links to the SDK (`BoardInsetsTest` compiles `BoardConfig.h`) and to `src/games` (`GameRowIcon.cpp`, `Session`, the installer). `HostBounds.h` hand-copies the host's dialog and banner rows, measured from screenshots. Only a test that asserts the same literals back pins those rows (see V3). | `games_check.cmake:75-106`; `HostBounds.h` | agent | see V3 |

### Aggregate view: duplication map

| id | finding | source | status | disposition |
|----|---------|--------|--------|-------------|
| D1 | The four draw recorders that cross-story row 24 deferred have started to drift. The UTTT and Battleship `trace.lua` files are no longer byte-identical: their headers differ by 6/5 lines from separate `e8-xr` edits. Sudoku's folder has two recorders of its own, `drawn.lua`'s `record` and `canvases.lua`'s `record`. | `git diff --no-index` of the two `trace.lua` files at `5cfd4489`; `sudoku/canvases.lua:32` | verified (trace drift) | defer, as row 24 already did; start with the duplicate inside Sudoku's folder, which R2 does not protect |
| D2 | Entry 8.15's margin snap exists twice: `board.snap` (both copies) and `battleship/layout.lua` `layout.snap`. The logic is the same; only the signature differs. Its tests are written three times, about 460 lines checking one rule. | `games/*/board.lua`, `battleship/layout.lua:39`; the three companion folders | agent | accept under R2; a candidate snippet for epic-api-freeze's starter repo |
| D3 | Check helpers are copied within and across companion folders. `eq` is defined six times. Sudoku's `pins.lua` already exports one, yet `canvases.lua` and `checks.lua` each define their own. `withCanvas` is identical three times, twice inside Battleship's folder. HOW TO PLAY's `wrap` is identical in UTTT and Battleship `main.lua`. | agent md5 sums | agent | defer: tidy duplicates inside each folder |

### Aggregate view: size growth

| id | finding | source | status | disposition |
|----|---------|--------|--------|-------------|
| G1 | `games/battleship/main.lua` (439 lines) holds rules, views, input, HOW TO PLAY and about 210 lines of drawing. Sudoku split its drawing into `view.lua`. | `5cfd4489:games/battleship/main.lua` (`wc -l` 439) | verified (size) / agent (structure) | **fixed** by AI-8 (`plan-e8-ai8-sweep.md`) |
| G2 | `GamesCheckEngineTest.cpp` grew to 1,610 lines (68 `TEST_F`). One 345-line section is grouped by ticket ("the cross-story review's fixes (e8-xr)"), not by behaviour. `battleship/checks.lua` (1,176 lines) and `ultimate-tic-tac-toe/checks.lua` (775) are single files, where Sudoku's folder splits the same concerns into six modules. | `wc -l` at `5cfd4489` | verified (sizes) / agent (structure) | defer: refactor in a later sweep |
| G3 | `games/sudoku/solver.lua` (687 lines, one function per rung of the technique ladder) and `games/sudoku/main.lua` (363) are large but cohesive. | agent | agent | accept |

### Aggregate view: pattern divergence

| id | finding | source | status | disposition |
|----|---------|--------|--------|-------------|
| P1 | The epic's C++ follows AGENTS.md: `LOG_*` only, `new (std::nothrow)` in the harness, no device allocation, the new device code inside `FREEINK_CAP_GAMES` files. `GameTouchLog::Line` is 128 B on the stack in `loopPlaying`, under the 256 B rule but half of it. | agent read of the `src/`/`lib/` diff | agent | accept (clean) |
| P2 | Lua style differs between the games. UTTT and Battleship use camelCase locals; Sudoku is all snake_case, and so are the companion folders. Sudoku's `ui` uses `panel`/`note`/`msg` where the others use `help`/`message`. Sudoku mixes named reject reasons with inline literals. | agent counts; `sudoku/main.lua:36,105-137` | agent | defer: a style line in the starter repo (epic-api-freeze) |
| P3 | The header of `sudoku/rules.lua` still justifies its layout with "the 256 KB the sandbox allows … had no room left". Since `e8-xr` row 4, check VMs have their own larger limits. | `5cfd4489:test/game_script/first_party/sudoku/rules.lua:1-7` | verified | **fixed** by AI-8 (`plan-e8-ai8-sweep.md`) |

### Diff-scope review (`daedbeab..5cfd4489`)

| id | lens | boundary | finding | source | status | disposition |
|----|------|----------|---------|--------|--------|-------------|
| V1 | verification gap (medium) | 8.13 × 8.16 | Sudoku arms a `ch.timer` on every board draw, so one is pending whenever a match is paused. Only `loopView`'s lack of a `vm->pollTimer()` call keeps it from waking the game and pushing frames while paused; `loopPlaying` and `loopHandOff` both poll. No test pins this. Adding the poll to `loopView` in a scratch tree left all 504 Match, Pause, Resume, Pass, GameVm and Touch ctests green. | `5cfd4489:src/activities/games/GameMatchActivity.cpp:557,572,627`; `games/sudoku/main.lua:346-356` (`d4e03a5d`) | verified (source); agent (scratch mutant) | fix now: add a GameMatchTest with the timer fixture: pause, advance past due, assert no tick and no frame; resume, assert exactly one tick |
| V2 | verification gap and edge case (low) | 8.16 × sleep/Continue | After Continue, the header's minutes can go backwards. The new VM starts its clock at 0 and `state.t` counts only the gaps between moves, so idle time after the last move is lost. Before entry 16 this showed only in the Solved time. It follows from the row-20 Decision ("a sleep is unchanged"), but neither the Decision nor entry 16's plan mentions the header. | `games/sudoku/main.lua:153-161,337`; Notes L128 | agent | accept; record it as a known effect |
| V3 | verification gap (low) | e8-xr × later rounds | `HostBounds.h`'s `DIALOG_TOP` 259, `DIALOG_BOTTOM` 528 and `BANNER_TOP` 640 are only asserted back as literals (`GamesCheckEngineTest.cpp:1528`). Entries 8.13–8.17 do not touch the host dialog, so the values are not stale, just unguarded. The `e8-xr` review already deferred this as medium (`deferred-work.md` `## e8-xr`). | `HostBounds.h` (`14428a56`) | agent | defer (already carried) |
| V4 | edge case (low) | 8.16 × Play again | `fresh` returns early when Play again deals a grid with the same clue signature as the last one. The stale `ui.last` then counts the time spent in the Over menu, both in the header and in the first move's gap. That needs the same bank puzzle and the same symmetry draw, so it is very unlikely. | `games/sudoku/main.lua:152-162,337` | agent | accept |

**Adversarial lens** (`daedbeab..5cfd4489`; I re-read the source for the four marked verified):

| id | boundary | finding | source | status | disposition |
|----|----------|---------|--------|--------|-------------|
| A-1 | 8.16 × 8.13 × resume | V2 at its real size: auto-sleep (default 10 min of no input) is the usual sleep, so every auto-sleep in a puzzle drops the whole idle gap from the header and the Solved time, e.g. "25 min" to "15 min". Entry 16 promised "the same play time the Solved screen shows", which holds only within one VM. A guard would be a time-only move on each minute timer, but row 20's Decision says "a sleep is unchanged". | `games/sudoku/main.lua:159,337`; `src/main.cpp` inactivity sleep | agent | owner: see Open questions |
| A-2 | 8.13 | A pause made before the game has loaded under-counts play time for good. `PauseClock::playMs` subtracts every pause since clock 0, while `startMs` is set at `LuaGame::load`. Back is handled before the load, so a pause during load is subtracted from time that never started. No test pauses before the first frame. | `lib/GameCore/PauseClock.h:55-75`; `lib/GameScript/LuaGame.cpp:207` | verified | **fixed** by AI-8 (`plan-e8-ai8-sweep.md`) |
| A-3 | 8.13 | The `slow-restart` fixture's header still says to pause within the gap. Since entry 13 its busy-wait on `ch.time.ms` freezes while paused, so the watchdog trips. `fixtures/README.md` step 9 was updated; the fixture comment was not. | `test/game_script/fixtures/slow-restart/main.lua:1-7` | agent | **fixed** by AI-8 (`plan-e8-ai8-sweep.md`) |
| A-4 | 8.14 | In `LOG_LEVEL=2` builds, every tap's DBG line enters the 16-line crash ring buffer, which pushes the lifecycle lines out of a panic report and records hidden seats' Battleship taps. Release builds set `LOG_LEVEL=1`, which compiles the lines out. | `GameMatchActivity.cpp:511-513`; `lib/Logging/Logging.cpp`; `platformio.ini` (`LOG_LEVEL` 2 for dev envs, 1 for release) | verified (levels) | defer (dev builds only): log only gestures that were not sent |
| A-5 | 8.14 | The SDK gesture thresholds the touch log is read by (`TOUCH_SWIPE_MIN_PX` 60, `TOUCH_SWIPE_MAX_MS` 700, `TOUCH_TAP_RELEASE_SLOP_PX`) are copied as prose in `GameTouch.h` and `game-canvas.md`, and `CopiedConstantsTest` does not pin them. | `src/games/GameTouch.h:15-19`; `CopiedConstantsTest.cpp:79-119` | agent | defer |
| A-6 | 8.15 × 8.14 | The margin snap stops at the canvas edge. On the X4 Pro the canvas ends at screen x 472 (right inset 7), and `classify` drops a tap whose centroid lands at x 473–479 as off the canvas before any game snaps. The device put right-column taps at canvas x 450–460. | `src/games/GameTouch.h:100`; Notes L137 | agent | defer: add a step to `## e5-run4` that looks for right-edge `off the canvas` lines |
| A-7 | 8.13 | `api-level-1.txt:91-93`, "a timer set before a pause may fall due while the match is paused", reads as if the game may get `input` during the pause. The host delivers it only at Resume, which V1's new test pins. | `docs/crosshatch/api-level-1.txt:87-98` | agent | **fixed** by AI-8 (`plan-e8-ai8-sweep.md`) |
| A-8 | 8.15 | Battleship's `layout.snap` uses `L.W` for the right edge and `layout.H` for the bottom. `L.W` is `layout.W` whenever `L` comes from `compute`, so it is latent: an `L` built any other way would raise inside `input` and end the round. | `games/battleship/layout.lua:41,73` | verified | **fixed** by AI-8 (`plan-e8-ai8-sweep.md`) |
| A-9 | 8.15 | The snap rule is now in two byte-identical `board.lua` copies and a third variant. Nothing pins the copies equal (A2, D2). | `games/*/board.lua:49-73` | agent | accept under R3/R2; a starter-repo note |
| A-10 | 8.15 × 8.16 | Entry 15's deferred MENU-snap question cites "80 px rows" and a "76 px bottom gap". Entry 16's ninth row made them 77 px and 23 px, with CLOSE moved to row 9. | `games/sudoku/layout.lua:17,39`; 8.15 plan :21, :146 | agent | **fixed** by AI-8 (`plan-e8-ai8-sweep.md`) |
| A-11 | e8-xr | A 171-column comment line in `fixtures/pass-keep/main.lua:3`. | as named | agent | accept (style) |

**Boundaries checked clean** (by the reviewer at `5cfd4489`):

- Pause × header: nothing wakes while paused, and Resume fires once, an extra push entry 16's plan accepted.
- `PauseClock` has a single writer and a seqlock reader, and keeps its ledger across Play again.
- Touch logging runs only in `loopPlaying`. `LOG_DBG` is compiled out at `LOG_LEVEL` 1.
- The margin snap uses the 466 box origin, so the Sticky's extra 4 px never snap.
- Battleship's header and lower gaps exceed its snap size.
- The shift-back rule runs after the snap.
- Sudoku's menu and help panels are routed before any snap.
- Entries 8.16 and 8.17 both edit `fresh`, and a store with no key gives DIGITS and TIMER on.
- With 9 menu rows, each is 77 px and the menu ends at y 765.
- Entry 8.12's absolute paths hold.

## Behavior verification

Exercised in this session, on the current `develop` HEAD `f84399ed`. That is the epic plus 92 later commits; of those, 28 files under `games/` and `test/game_script/` changed after the epic, through the upstream sync and the e6pre tickets.

- **Environment fix first.** The `freeink-sdk` working tree was checked out at `111fdcc7`, not the recorded `32a265fa`, which failed the session-start warm builds. I synced it with `git submodule update`; the pointer did not move.
- **Host suites:** 2,046 of 2,046 tests passed, including 129 `games-check` tests. The reviewer separately ran `ctest -L games-check` 129/129 in a scratch tree at the epic head `5cfd4489`.
- **Simulator** (`simulator_x4pro`, 466 × 788, Sudoku packed from `games/sudoku` into `fs_/games/`):
  - The launcher shows Sudoku's grid icon. The title reads "Solo · Easy". A new game shows "0 min" in the header.
  - **Pause across the header (entries 13 and 16, unrun on the device; `## e5-run4`):**
    - The game played 10.3 s, then paused for 87 s (`[36865] Playing -> Paused`, `[123988] Paused -> Playing on Resume`).
    - The header still read "0 min" after resume (`build/sim/shots/resumed.png`).
    - It turned "1 min" at the next push, `[173831]`, 49.8 s after resume (`after70.png`). That is 60 s of play time, so the pause was not counted.
  - **NOTES AS default (entry 17, unrun on the device):** the store was empty. With NOTES on, tapping 1, 2 and 3 in an empty cell drew a small digit "3" in its pad slot, not a dot. The 1 and 2 were hidden because a peer rules them out (`notes.png`).
- **Not exercised here:**
  - Ultimate tic-tac-toe and Battleship in the simulator.
  - B6, a sleep from Battleship's Over.
  - The TIMER toggle off.
  - Any check on a device.

  The device evidence is the owner's three X4 Pro runs as `device-run-packet.md` records them: installs, heap, every resume, B7.6, HINT and CHECK log bounds, icons, and touch logging. The simulator results above do not replace the device checks `## e5-run4` still owes.

## Previous-retro follow-through

From `epic-pass-and-play-retrospective.md`, Action items AI-1 to AI-10, checked at HEAD `f84399ed`. The evidence is the subagent's report; I re-checked the items marked verified.

| item | owner | landed? | evidence |
|------|-------|---------|----------|
| AI-1 Reword ledger row 2 or enforce append-only | owner | landed | `c2954f7f` (e5-r2); `docs/crosshatch/upstream-touches.md:34` |
| AI-2 Stale lifecycle table, three comments, packet B6/T5 superseded | dev loop | landed | `c2954f7f`; `game-canvas.md:252,255`; epic-5 packet :133, :172 |
| AI-3 Pin 90 ms and 500 ms; `static_assert(FORCED_EXIT_DEADLINE_MS == 1500)` | dev loop | landed | `CopiedConstantsTest.cpp:86,94`; `GameMatchActivity.cpp:94` (verified) |
| AI-4 Clear the latch on leaving Playing, plus a test | dev loop | landed | `c2954f7f`; `GameMatchTest.cpp:3504`; the latch moved to `src/games/TouchLatch.h` (`87fe332b`) |
| AI-5 The 5.6 follow-up story; run B7.6 first | owner / dev loop | landed, but out of order | `c2954f7f`, `71cacd39`; B7.6 ran after the fix, in epic 8's device run (Part P: store at 40 ms, blank at 1,690 ms, nothing skipped) |
| AI-6 Device-check 5.13's tap targets | owner | partly; the rest deferred | epic-8 packet B5a–c "partial", per-try results not recorded; `deferred-work.md` `## e5-close` stays open |
| AI-7 Reconcile epic 5's Requirements; record CI on the final head | owner | landed | `d8feb90c`, `f5eafc66` |
| AI-8 Carry the earlier retro's dropped items | owner | landed, except one sub-check deferred | concurrency group `9180a698`, `9840ab0a`; AGENTS.md lines `f374d6f6`; deferred entries `ba1801d3`; AI-10 items in `d8feb90c`. Running the concurrency group on a real PR stays in `## e6pre-5`, assigned to epic-play-nearby. |
| AI-9 Edit a superseded Requirement in the same commit | owner | **not landed** | No rule in `orchestrated-epics.md`, AGENTS.md or `.agents/`. Epic 8 repeated the pattern (S4; R4's stale text verified by grep). |
| AI-10 Split `GameMatchActivity`; share `replaceFile`; one test-support header | dev loop | landed (D4 partly) | `eed14f07`, `87fe332b`; `GameSaveStore.cpp:337`; `401b28e7` (`SaveSupport.h`), with the rest deferred in `## e5-close` |

Epic-5 items that epic 8 took up: V3 (R11, `ResumeMatchTest.cpp` with the `pass-keep` cases) and B7.6 are resolved. The `e5-r2` dropped-touch line appeared 0 times. `e5-r5` (2), the ghost after a sleep from Over, and the `e5-close` leak and F13 were not run and remain open.

## Action items

Owner decisions taken in this retro (2026-10-11):

- The owner **confirmed the device-run sign-off**, which was owed since "Go ahead and merge" (packet "Sign-off"; Notes L168). The unrun device checks stay in `deferred-work.md` `## e5-run4`.
- The owner **confirmed every row of the packet's deferral table as written**. Each stays deferred with its recorded reason and trigger. This answers R14's "each deferral with a reason the owner confirms".
- The owner chose to **fix S1 by generic discovery**, not to amend R2.
- The owner chose to **finish the epic now**: publish ticket 8.6, and build the two fix-now items in one PR.

| id | item | owner | kind | status |
|----|------|-------|------|--------|
| AI-1 | Publish the fork release from `develop` (8.6, R13, Done when 4), check the three `.chgame` hashes against `pack_game.py` at the released commit, and record them in 8.6's plan. | owner (dispatch), orchestrator (check) | remediation | **done**: `1.6.5-ch.4`, run 38106383670 |
| AI-2 | S1: `games_check.cmake` finds companion `--check` tools by their argparse flag (`GamesCheckTool.<id>.<tool>`), so the harness names no game. | dev loop | remediation | done in this branch (`0b0536fb`); PR pending |
| AI-3 | V1: pin that a paused match polls no timer and that Resume delivers the overdue one once (`MatchTest.APausedMatchPollsNoTimerAndResumeDeliversTheDueOneOnce`; red with a `pollTimer()` in `loopView`). | dev loop | remediation | done in this branch (`0b0536fb`); PR pending |
| AI-4 | Rewrite the superseded Requirement text in the epic file: the 15 places in S4, and the stale Notes L75 and L106. Do it as one reconciliation commit, since the epic is closed. | owner | spec reconciliation | **done** at the owner's request: R2, R3, R4, R5, R7–R10, R12–R14, Done when 3 and the Boundaries rewritten; the superseded Notes marked (the epic file's Decision of 2026-10-11) |
| AI-5 | Process: AI-9 from the previous retro did not land, and epic 8 repeated it. Put the rule ("a Decision that supersedes a Requirement edits that Requirement in the same commit") in `docs/crosshatch/orchestrated-epics.md` under the orchestrator's duties, and have the cross-story review check it. | owner | process | proposed |
| AI-6 | Process: a cross-story review's own fix can break an epic rule. Row 8 prescribed naming Sudoku in the harness (S1), and entry 14 copied the pattern while its plan claimed "names no game". The cross-story triage should check each prescribed fix against the epic's Requirements before accepting it. | owner | process | proposed |
| AI-7 | Add to `## e5-run4` for the next device run: a sleep (auto-sleep included) and Continue mid-puzzle, reading Sudoku's header (A-1); right-edge `off the canvas` lines on the X4 Pro (A-6); a pause before the game's first frame (A-2). | orchestrator | verification | **done**: added to `deferred-work.md` `## e5-run4` |
| AI-8 | Next sweep (before epic-play-nearby changes Battleship): split Battleship's drawing into `view.lua` (G1); fix `layout.snap`'s `L.W` (A-8) in the same package change; count only pause time after `startMs` (A-2); restate the MENU-snap deferral with entry 16's geometry (A-10). Doc and comment fixes: P3, A-3, A-7. | dev loop | remediation | done in this branch (`plan-e8-ai8-sweep.md`, commit pending); PR pending |
| AI-9 | Later refactor of the companion folders: merge the duplicated recorders inside Sudoku's folder first (D1, cross-story row 24), and the duplicated `eq` and `withCanvas` helpers (D3); split `GamesCheckEngineTest.cpp`'s e8-xr section by behaviour (G2). | dev loop | remediation | deferred |
| AI-10 | For epic-api-freeze: record that `ch.time.ms`'s meaning changed within level 1 without a CRC change, twice: play time (S5, entry 13), then pauses before the load no longer subtracted (A-2, AI-8), and give the starter repo the margin-snap and Lua style notes (D2, P2). | owner | spec reconciliation | proposed |

## Acceptance verdict

**accepted-with-open-items**, against declared criteria (Done when 1–6).

- **Done when 1** (three packages, no game-specific C++, manifests only `solo` or `pass`): met (S7).
- **Done when 2** (packs, installs through the inbox, plays a round on a device in every declared mode): met, from the owner's three X4 Pro runs (packet Step results I2, U1–U5, B1–B7, S1–S5). The owner confirmed the sign-off in this retro. Not recorded: tap counts.
- **Done when 3** (level 1, the 1,400 B snapshot, `GameIcons`, `ch.gfx`): met. The games check asserts 700 B at every step. The owner's Decisions (entries 10 and 13) changed the engine within the unfrozen level 1 (S5).
- **Done when 4** (a fork release with the three `.chgame` files, packed byte for byte from `games/<id>/`): **met on 2026-10-11**. Release `1.6.5-ch.4` (https://github.com/CarpeTelam/crosshatch-player/releases/tag/1.6.5-ch.4, run 38106383670, tag at `f84399ed`) attaches all three. Their package hashes and SHA-256 equal `pack_game.py`'s output at the tag, the device-run packet's, and the dry run's (`story-fork-release-with-the-three-games-plan.md`).
- **Done when 5** (a PR that breaks a game fails a check): met. The `games-check` job is in `Crosshatch Test Status`'s `needs`, and its red cases are in `GamesCheckEngineTest`.
- **Done when 6** (merged with every gate green): met. All 22 check runs on CarpeTelam/crosshatch-player#35's head passed or were skipped by label (S7).

How the verdict was reached: the machine verdict at the start of the run was **rejected**, because 8.6 was unfinished. The owner chose to finish the epic rather than override, and with 8.6 done every Done when item is met.

The open items keep this from plain *accepted*. None blocks:

- S1 and V1 are fixed on `claude/intelligent-cannon-1ial41` but not yet merged.
- The process lessons AI-5 and AI-6 are open. The Requirement text was reconciled after the verdict, at the owner's request (AI-4).
- Device checks are still owed in `## e5-run4`, now including AI-7.
- Deferred findings: A-4, A-5, D1, D3, V3. G1, A-2, A-8 and A-10, with P3, A-3 and A-7, were fixed after the verdict by AI-8.

The epic file now carries `status: done` and a Closure line in its Notes.

## Open questions

- **A-1, Sudoku's clock across a sleep.** Row 20's Decision says "a sleep is unchanged", so an auto-sleep mid-puzzle drops the idle time since the last move from both the header and the Solved time. Should Sudoku commit its time on each minute timer, a time-only move costing one `resume.bin` write a minute, so a sleep loses less than a minute? Or does the owner accept the drop? The next device run's sleep-and-Continue step (AI-7) will show how large it is.
- **Who pays the epic's +1,376 B of flash** (S5)? This is still open from `epic-play-nearby.md`'s e6pre-14 Decision.
- **The MENU snap (A-10).** The owner has not ruled on whether Sudoku's menu rows snap. AI-8 restated the deferral (`deferred-work.md` `## 8.15`) with entry 16's geometry, so the owner can decide on today's layout.
- **What this retro could not check:** session logs for the build dispatches, which hold no repo copy, so the process lessons rest on the plans and Notes only. Ultimate tic-tac-toe and Battleship were not exercised in the simulator in this session. Nothing was run on a device.
