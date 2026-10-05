---
title: 'Device run and owner sign-off'
type: 'chore'
ticket: '5'
created: '2026-10-05'
status: 'built'
route: 'full'
route_source: 'auto'
baseline_revision: 'daedbeabf8a9cf7da90ed976401a0fbb73498c8d'
review: 'thorough'
review_source: 'auto'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
followup_review_recommended: true
deferred:
  - summary: The bank tool grades each Expert puzzle in its filed orientation only; the game deals a random symmetry, and the costliest Expert puzzle's worst of 3,000 symmetries (302,052 host instructions) is 34% above the bank's recorded 224,973.
    location: test/game_script/first_party/sudoku/tools/make_bank.py, grade.lua
    severity: low
  - summary: No Sticky firmware is built (the orchestrator decided not to), so J6 and the Sticky halves of 8.9 #2 and #4 are not answerable this run: an owner-facing item for a later Sticky pass.
    location: _bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/device-run-packet.md (J6, 8.9 #2 and #4 Sticky halves)
    severity: low
  - summary: `make_sudoku_costly.py` has no committed test that it still matches the shipped game or that the grid costs what the packet says; it re-checks only that GRID has exactly one solution, SOLUTION.
    location: _bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/device-run-packet/make_sudoku_costly.py
    severity: low
  - summary: The script `make_sudoku_costly.py` and `count_sudoku_costly.lua` write the game's state shape (`l`, `v`, `n`, `u`, `t`) by hand, so a change to `games/sudoku/main.lua`'s `setup` or state would show only on a device or the simulator.
    location: _bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/device-run-packet/make_sudoku_costly.py, count_sudoku_costly.lua
    severity: low
  - summary: The 302,052-instruction figure is the worst found (3,000 symmetries of one puzzle, 40 of each other Expert puzzle, none of Easy, Medium or Hard), not the worst possible.
    location: games/sudoku/puzzles.lua (the bank's cost cap), test/game_script/first_party/sudoku/tools/make_bank.py
    severity: low
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Entry 5 (R11 to R14) needs the owner to run the three first-party games on an X4 Pro and sign off the epic, and nothing hands the owner the firmware, the packages, the steps, the questions and the places to record.

**Approach:** Build the device-run packet `device-run-packet.md` and its folder `device-run-packet/` from the epic head, modelled on epic-pass-and-play's packet, and stop there: this run is `hitl`, so it ends once the packet is built and reviewed, before any device result exists.

## Boundaries & Constraints

**Always:** Build the firmware from the epic head under the build lock with the shared cache; give every figure as a measurement with its method, or "unmeasured"; list each timing-dependent item as calibrated or "uncalibrated: expected outcome estimated from the host ratio" (AI-5); put each `Assumption for entry 5:` line and each deferral in the `## 8.x`, `## e5-close`, `## e6pre-13`, `## e5-r5`, `## e5-r2`, `## owner-e4-games-cap` entries to the owner as a question; leave the release dry run (R13) as a section the orchestrator fills in.

**Never:** Touch `games/**`, `src/**`, `lib/**`, `test/**`, or `scripts/**`; mark the ticket, pull, push, or open a PR; edit `deferred-work.md` (the orchestrator carries the plan's `deferred` list there); invent a device result.

</frozen-after-approval>

## Code Map

- `_bmad-output/initiative-crosshatch-player-v1/epic-pass-and-play/device-run-packet.md` and its sign-off plan -- the model: sections Firmware, Packages, Fixture calibration, per-part steps, serial-line table, answer tables, What to record.
- `epic-first-party-games.md` Notes (lines 112, 113, 120 the `Assumption for entry 5:` lines; the Decisions of 2026-10-05 for the panel judgements) and R11 to R14.
- `scripts/pack_game.py`, `scripts/pack_device_run.py` -- the packages and their hashes (`pass-store` is a derived game of the latter).
- `src/games/GameVM.cpp` (the "VM stopped" line), `src/main.cpp` (the 10 s `[MEM]` line), `src/activities/games/GameMatchActivity.cpp` (the dropped-touch and forced-exit lines), `src/games/GamePaths.h` (`INBOX_DIR` is `/games`).
- `games/sudoku/main.lua`, `puzzles.lua`, `solver.lua` -- HINT and CHECK, and the bank; `test/game_script/first_party/sudoku/tools/grade.lua` -- the instruction counter reused for the host figures.
- `docs/crosshatch/game-canvas.md` -- Taps, the forced exit's worst case (B7.6's expectation).
- `.github/workflows/crosshatch-release.yml`, `scripts/fork_release.py` -- the dry run's package table.

## Tasks & Acceptance

**Execution:**
- [x] firmware -- `pio run -e x4pro` at `daedbeab`, record size and SHA-256 -- the owner flashes this build; entry 10 changed the installer
- [x] `device-run-packet/*.chgame`, `HASHES.txt` -- pack the three games and `pass-store`; add `sudoku-costly` -- hashes the owner compares
- [x] `device-run-packet/make_sudoku_costly.py` -- derive a Sudoku with its first deal fixed to the costliest Expert grid -- the shipped game cannot be forced to a puzzle
- [x] `device-run-packet.md` -- steps, serial lines, panel judgements, answer tables, calibration, dry-run section, What to record
- [x] this plan -- points at the packet, which is the single home of the results

**Acceptance Criteria:**
- Given the packet, when the owner follows it from a flashed X4 Pro, then each R12 item has a step, the serial line that carries it, and a place to record it in the packet's own tables.
- Given the epic Notes and `deferred-work.md`, when the packet is read, then every `Assumption for entry 5:` line and every open item of the named entries appears as an owner question.

## Implementation Notes

- Implemented directly, not by a coding subagent: the plan cannot carry the investigation behind the packet (the epic Notes, six deferral sections, three games' controls, the logs), and a subagent given only the plan would redo it. The firmware build, the packing and the host measurements were done before this plan file was written, because its facts come from them; nothing was skipped.
- The brief gave the inbox as `/games/inbox`; the code's inbox is `/games` (`GamePaths.h` `INBOX_DIR`), so the packet says `/games/`.
- The shipped Sudoku deals a random puzzle and symmetry (`setup`) and logs neither, so HINT and CHECK cannot be timed on `0034ee8363e5` there. The packet's `sudoku-costly` fixes the deal; the costliest of 3,000 symmetries of that puzzle is 302,052 host instructions, against the bank's 224,973 (unsymmetrised): recorded under `deferred`, not acted on (3.3 times under the 1 M cap).
- `make_sudoku_costly.py` is a packet file, not a fork script (`docs/crosshatch/fork-scripts.md`): it lives beside the packet, packs nothing into `games/`, and is not run by CI.
- `firmware-x4pro-daedbeab.bin` (5.9 MB) is not committed; the orchestrator sent it to the owner as a session file, and its SHA-256 and size are in the packet; a rebuild is not promised to be byte-identical, and the packet says so.
- The release dry run (R13) failed on a bug in the release script's packer path; the orchestrator fixes it as entry 8.12 and fills the packet's R13 section after a re-run, so it stays pending here. The dry-run commit rule is the orchestrator's: the epic head at the time of the run, valid when `games/**`, `scripts/pack_game.py`, `src/**`, `lib/**` and the `freeink-sdk` pointer are unchanged since `daedbeab`.
- Review patches (pass 1): see the Review Triage Log. `count_sudoku_costly.lua` was added to the packet folder so the host figures can be re-derived, and `sudoku-costly` was repacked (`d964af5a1278ea7f`) so its `setup` still loads the bank.

## Plan Change Log

## Review Triage Log

Pass 1. The four lenses (blind-hunter, edge-case-hunter, verification-gap, intent-alignment) ran as context-free subagents over the packet and plan, which are documents, not code (the diff file held the packet, plan, `HASHES.txt` and `make_sudoku_costly.py` as new files and the `.chgame` files as size and hash lines). Their reports reached the orchestrator, which forwarded them. Verdicts: 0 high, 4 medium, 14 low, 0 false, and one descriptive report. Every patch was made by hand in the packet (the packet is prose; no implementation subagent applies). `followup_review_recommended` is true: two or more mediums were patched.

| # | Lens | Finding | Verdict, route | Evidence and action |
|---|------|---------|----------------|---------------------|
| 1 | edge-case | Seat 1's deliberate first-shot miss makes seat 2 win first (the turn passes after every shot) | medium, patch | `game.apply` passes `state.t = target` after each shot; seat 2's 17th hit precedes seat 1's 18th. Each seat now misses once at row 10, column 10; the round is 35 shots and seat 1 wins on its 18th. |
| 2 | blind, edge | The 1.5 s bar was applied to a video time that holds the touch and a ~0.67 s refresh | medium, patch | A healthy call at the 670,000/s floor (about 0.45 s) plus a refresh nears 1.2 s. Part T now times a MENU control and applies the bar to the difference. |
| 3 | blind, edge | Part B's natural dropped-touch count shares a span with 5.13's deliberate fast taps | medium, patch | The count stops before step 5, and step 5's drops are counted separately. |
| 4 | edge | `sudoku-costly`'s replaced `setup` dropped `require("puzzles")`, so its arena peak and A2 evidence omit the bank | medium, patch | `setup` is the only caller. The script keeps the call; repacked as `d964af5a1278ea7f` (40,642 B), hash, size and SHA-256 updated everywhere. |
| 5 | blind | Part order sentence said T uses a fifth game and P a fourth | low, patch | Fixed: each installs one game after Part I's three-game reading. |
| 6 | blind | The serial monitor exits at every deep sleep; nothing covers it | low, patch | Both earlier sign-off logs are separate pastes. A reconnect loop and the lost-lines note were added. |
| 7 | blind | Nothing confirms the flashed build; same size as the base Measurement looks stale | low, patch | The size matches the entry-10 Measurement at its own commit as well, and the build was a fresh `pio run`; the packet now says Sudoku's install is the check (a pre-entry-10 firmware refuses 36 members). |
| 8 | blind, intent | The firmware's delivery was a scratch path | low, patch | The orchestrator's route (SendUserFile), the size and the hash are recorded. |
| 9 | blind | The two sets of host figures differ by 0.2% and the counting method is not committed | low, patch | The first counts the whole `input` call (solver module load, menu handling), the second the solver calls. `count_sudoku_costly.lua` was added; its figures (258,030, 45,186, 302,708) replace the first harness's, which read 6 higher through a wrapper. |
| 10 | blind | J6 and the Sticky halves of 8.9 #2 and #4 cannot be answered; "J1 to J6 now" | low, patch | The orchestrator decided against a Sticky build. The packet marks them "not answerable this run", Part S says J1 to J5, and the plan's `deferred` item carries it for the owner. |
| 11 | blind, edge | `make_sudoku_costly.py` raised a traceback on a manifest of another shape, rewrote the manifest beyond the docstring, and used the locale's encoding | low, patch | `.get`, a caught error with exit 1, `ensure_ascii=False`, UTF-8, and a docstring that lists both edits and the ignored Difficulty. |
| 12 | blind | Part B step 6 and step 2 omitted that no blank line is expected on Over | low, patch | `pushForcedExitBlank` pushes only with a seat's frame; both steps now say so. |
| 13 | edge | The Best m:ss path is never seen (Part S uses HINT) | low, patch | Said so, with an optional extra round. |
| 14 | edge | Part B step 5 a and b are timing-dependent and missing from the AI-5 table | low, patch | A row "uncalibrated: no estimate" was added. |
| 15 | edge | "About 1.75 s to 1.9 s" has no source for 1.9 | low, patch | Now 100 ms plus the store write (42 to 96 ms) plus 1,654 ms, about 1.8 s, as the Design Notes say. |
| 16 | verification-gap | No test pins `make_sudoku_costly.py`'s grid, cost or hash | low, defer (partly patched) | A one-off packet file outside `scripts/`; a CI test is not worth it. The script now re-checks uniqueness and SOLUTION; the rest is in `deferred`. |
| 17 | blind | The plan's frontmatter and logs were stale | low, patch | Filled in by this pass. |
| 18 | edge, verification-gap | Step 5(c) "no tap-driven change" names no observable; two host-figure scopes side by side | low, patch | Reworded; covered by #9. |
| 19 | intent-alignment | Descriptive: the content is asserted by reading, not executed; the firmware was outside the folder | no verdict | The lens checked strings against the sources and found no mismatch; the delivery route is #8. |

Pass 2 (the follow-up pass, `followup_review_recommended` from pass 1), over `daedbeab..d379cfdb` with four fresh context-free lenses (blind-hunter, edge-case-hunter, verification-gap, intent-alignment), again over documents. Verdicts: 0 high, 2 medium, 15 low, 0 false, and one descriptive report. Two mediums were patched, so `followup_review_recommended` stays true by the rule; the orchestrator's procedure runs this one follow-up pass only, and every patch is to the packet's text or its two helper scripts.

| # | Lens | Finding | Verdict, route | Evidence and action |
|---|------|---------|----------------|---------------------|
| 20 | edge | Part P's "second move less than 5 s after the first" does not keep the store dirty: the 5 s runs from the store's last write | medium, patch | `GameSaveStore::flushIfDue` returns unless the store is dirty and 5 s (`FLUSH_INTERVAL_MS`) have passed since `lastWriteMs`, which starts at the match's start and moves at each flush. A move after the window is flushed on the next loop pass. Step 2 now gives two routes (power within 5 s of the match's start, or a move within 5 s after a `saved ch.store` line), five tries each, and a "not reachable by hand" outcome. |
| 21 | blind, intent | The results have two homes: the packet's answer columns and the plan's one-paragraph "Owner's results" | medium, patch | The packet is the single home (orchestrator's choice): a "Value recorded" column and a "Step results" table (every step id), and the plan only points there and records the sign-off. |
| 22 | blind, edge | The dry-run commit rule contradicts the branch's state, and entry 8.12 changes `scripts/` | low, patch | Restated as the orchestrator gave it: the epic head at the time of the run, valid when `games/**`, `scripts/pack_game.py`, `src/**`, `lib/**` and the `freeink-sdk` pointer are unchanged since `daedbeab`; the intro no longer calls `daedbeab` the head. |
| 23 | blind, edge, intent | Firmware: no durable path, a rebuild has no hash, size equality and "Sudoku installs" do not show the build is `daedbeab`'s | low, patch | The orchestrator has sent the file and the three packages; the packet says to check the hash before flashing, that a rebuild is not promised byte-identical and is not an equal fallback, and that the install shows only a cap of at least 36; the scratch path and the absolute cache path are gone. |
| 24 | blind | "Costliest" and "the worst single call" overclaim: one puzzle got 3,000 symmetries | low, patch | Reworded to "the worst found", with the search's coverage; added to `deferred`. |
| 25 | blind | Part T's method cannot separate the two device rates | low, patch | The packet says it can show only pass or fail against the 3 s and 1.5 s bars, adds repeats and the frame rate, and keeps the rate uncalibrated. |
| 26 | blind, edge, verification-gap | `count_sudoku_costly.lua` hard-codes 11,023 and the menu rows | low, patch | The table building is derived (first minus second grade call, on a fresh solver); rows are named constants with asserts; the docstring says the hook counts VM instructions only and gives the expected output. Both reproduced the packet's figures. |
| 27 | blind, edge | `make_sudoku_costly.py` read `values` outside its guard, and its schema coupling was unstated | low, patch | Moved inside the guard; the docstring states the coupling; added to `deferred`. |
| 28 | blind | Hashes are copied by hand in several places with no mechanical check | low, patch | A `sha256sum *.chgame` command and the count script were added to Verification. |
| 29 | blind | Part B's shot bookkeeping is under-specified | low, patch | A tally instruction, and seat 2 winning is handled the same. |
| 30 | blind | The heap baseline mixes the Games list's cost into the three games' cost, and 8.10's block is not logged | low, patch | An empty-Games reading was added as the baseline; the packet says `MaxAlloc` at Home shows the install fitted, not by how much. |
| 31 | blind | No end-of-run restore; U and S sleep expectations missing; photos and the "which puzzle" record | low, patch | Part Z added; U and S say no blank line is expected; photos go with the results named by step id; the shipped-Sudoku grid can be matched offline by the orchestrator. |
| 32 | blind | The e5-close leak and F13 rows say "Not run; Confirm" though their trigger is the next device run | low, patch | The rows ask the owner to run them now or keep them deferred. |
| 33 | blind | `deferred-work.md` is never cited by path | low, patch | The deferral section cites `_bmad-output/implementation-artifacts/deferred-work.md`. |
| 34 | edge | The calibration table's labels differ from the AI-5 phrase | low, patch | The `pass-store` row reads "uncalibrated: expected outcome estimated from logged figures, not a host ratio"; the Part B row "uncalibrated: no expected outcome estimated". |
| 35 | blind | The plan's `review_loop_iteration` and status said nothing of pass 1's follow-up | low, patch | This log records both passes. |
| 36 | verification-gap | No finding beyond the 11,023 literal (#26) | no verdict | The lens re-did the arithmetic and grepped every quoted log line in `src/`. |
| 37 | intent-alignment | Descriptive: the flash procedure pointed at an earlier packet, and the per-step results had no form | no verdict | Flash steps are inline now (#23); the form is #21. |

## Design Notes

- Where the intent settles each choice: the three `Assumption` lines and the owner Decisions are epic Notes lines 111 to 124; the heap and stack figures are R12 and the epic-api-freeze item (under 512 B free reopens the C-stack decision); HINT's 1.5 s bar is the D2 Decision (half the 3 s watchdog, cap lowered if the device is slower).
- HINT and CHECK are timed by video because the firmware logs no tap and no call time; the log shows only a call over 3 s. A first CHECK, then a HINT, then a HINT after a resume (a new VM drops the cached answer) gives the three figures `count_sudoku_costly.lua` reports: 258,030, 45,186 and 302,708 host instructions.
- B7.6's expected log order and 1.8 s total are estimates from `game-canvas.md`'s figures (a 91 ms resume write, a 1,654 ms half refresh), uncalibrated until Part P runs.

## Verification

**Commands:**
- `git status --short` -- expected: only `_bmad-output/.../device-run-packet.md`, `device-run-packet/`, and this plan
- `python3 scripts/pack_game.py games/<id> <dir>` for the three games, twice -- expected: the same hash and bytes (`3c657cfcb2e461dc`, `a7e63b542144938b`, `10a07de10cb14489`)
- `python3 _bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/device-run-packet/make_sudoku_costly.py <dir>` twice -- expected: `d964af5a1278ea7f` both times
- `cd device-run-packet && sha256sum *.chgame` -- expected: the SHA-256 column of the packet's package table
- `lua device-run-packet/count_sudoku_costly.lua games/sudoku` (a host Lua 5.5.1) -- expected: 258030, 45186, 302708 and a solver sum of 302052
- `python3 scripts/check_upstream_touches.py`, `./bin/clang-format-fix` twice -- expected: pass, nothing new in `git status`
- `flock /tmp/crosshatch-build.lock sh -c 'pio run -e x4pro'` -- expected: SUCCESS; recorded in the packet (5,935,632 B). No `pio check`, `default` build, or `sim.sh`: the commit changes no source.

**Manual checks:**
- Each lens reads the packet and plan as documents; the owner's steps are checked against the games' code.

## Owner's results

The results are not recorded here. The packet is their single home: the owner's answers in its J, A and deferral tables, and the step results, figures and log excerpts in its "What to record" and "Step results" tables. After the run and sign-off, record here only the outcome:

- Device run date and the owner:
- Firmware flashed (commit and SHA-256) and the dry run's link:
- Sign-off (the owner's words and date):
- Device failures, each as a new story in this epic's PR:

`deferred-work.md`'s `## e5-close` (5.13), `## e6pre-13`, `## e5-r5`, `## e5-r2` and `## owner-e4-games-cap` get their outcomes from the packet's rows; the orchestrator carries them there.
