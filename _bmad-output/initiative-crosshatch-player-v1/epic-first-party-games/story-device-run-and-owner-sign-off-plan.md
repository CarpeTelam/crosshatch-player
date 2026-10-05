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
- [x] this plan -- `## Owner's results` for the owner's answers after the run

**Acceptance Criteria:**
- Given the packet, when the owner follows it from a flashed X4 Pro, then each R12 item has a step, the serial line that carries it, and a place to record it.
- Given the epic Notes and `deferred-work.md`, when the packet is read, then every `Assumption for entry 5:` line and every open item of the named entries appears as an owner question.

## Implementation Notes

- Implemented directly, not by a coding subagent: the plan cannot carry the investigation behind the packet (the epic Notes, six deferral sections, three games' controls, the logs), and a subagent given only the plan would redo it. The firmware build, the packing and the host measurements were done before this plan file was written, because its facts come from them; nothing was skipped.
- The brief gave the inbox as `/games/inbox`; the code's inbox is `/games` (`GamePaths.h` `INBOX_DIR`), so the packet says `/games/`.
- The shipped Sudoku deals a random puzzle and symmetry (`setup`) and logs neither, so HINT and CHECK cannot be timed on `0034ee8363e5` there. The packet's `sudoku-costly` fixes the deal; the costliest of 3,000 symmetries of that puzzle is 302,052 host instructions, against the bank's 224,973 (unsymmetrised): recorded under `deferred`, not acted on (3.3 times under the 1 M cap).
- `make_sudoku_costly.py` is a packet file, not a fork script (`docs/crosshatch/fork-scripts.md`): it lives beside the packet, packs nothing into `games/`, and is not run by CI.
- `firmware-x4pro-daedbeab.bin` (5.9 MB) is not committed; the orchestrator hands it to the owner as a session file (SendUserFile), and its SHA-256 and size are in the packet.
- The release dry run (R13) failed on a bug in the release script's packer path; the orchestrator fixes it as entry 8.12 and fills the packet's R13 section after a re-run, so it stays pending here.
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

## Design Notes

- Where the intent settles each choice: the three `Assumption` lines and the owner Decisions are epic Notes lines 111 to 124; the heap and stack figures are R12 and the epic-api-freeze item (under 512 B free reopens the C-stack decision); HINT's 1.5 s bar is the D2 Decision (half the 3 s watchdog, cap lowered if the device is slower).
- HINT and CHECK are timed by video because the firmware logs no tap and no call time; the log shows only a call over 3 s. A first CHECK, then a HINT, then a HINT after a resume (a new VM drops the cached answer) gives the three figures `count_sudoku_costly.lua` reports: 258,030, 45,186 and 302,708 host instructions.
- B7.6's expected log order and 1.8 s total are estimates from `game-canvas.md`'s figures (a 91 ms resume write, a 1,654 ms half refresh), uncalibrated until Part P runs.

## Verification

**Commands:**
- `git status --short` -- expected: only `_bmad-output/.../device-run-packet.md`, `device-run-packet/`, and this plan
- `python3 scripts/pack_game.py games/<id> <dir>` for the three games, twice -- expected: the same hash and bytes (`3c657cfcb2e461dc`, `a7e63b542144938b`, `10a07de10cb14489`)
- `python3 _bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/device-run-packet/make_sudoku_costly.py <dir>` twice -- expected: `d964af5a1278ea7f` both times
- `python3 scripts/check_upstream_touches.py`, `./bin/clang-format-fix` twice -- expected: pass, nothing new in `git status`
- `flock /tmp/crosshatch-build.lock sh -c 'pio run -e x4pro'` -- expected: SUCCESS; recorded in the packet (5,935,632 B). No `pio check`, `default` build, or `sim.sh`: the commit changes no source.

**Manual checks:**
- Each lens reads the packet and plan as documents; the owner's steps are checked against the games' code.

## Owner's results

Filled in after the device run, per the packet's "What to record": the firmware and package hashes, each step's result, the serial lines, HINT's and CHECK's times, the release dry run's link and table, and the owner's answers to J1 to J6, A1 to A3 and each deferral; `deferred-work.md`'s five entries get their outcomes from this section.
