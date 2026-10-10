---
title: "Make Sudoku's NOTES AS default to DIGITS"
type: 'feature'
ticket: '17'
created: '2026-10-10'
baseline_revision: 'fced14c2d7eeb858594831cdf2d7a7b55d9d32e7'
status: 'built'
route: 'oneshot'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
followup_review_recommended: false
context: ['{project-root}/test/game_script/first_party/README.md']
warnings: []
deferred:
  - summary: >-
      The device-run packet's steps still assume NOTES AS starts at DOTS: step 122 ("dots first, then MENU, NOTES AS: DIGITS") needs a tap to DOTS first, and step 159 ("set NOTES AS: DIGITS") must drop its tap, which would now switch to DOTS and run the Expert heap step with dot notes.
    evidence: |-
      The packet text at device-run-packet.md lines 122 and 159 (and its "defaults" restore note at line 177) was written for the DOTS default; the brief makes the packet and the .chgame files the orchestrator's, to repack with the package hash below.
    location: >-
      _bmad-output/initiative-crosshatch-player-v1/epic-first-party-games/device-run-packet.md:122,159
    severity: medium
---

<intent-contract>

## Intent

**Problem:** Sudoku's NOTES AS toggle reads DOTS when `ch.store` has no `dots` key (`fresh` takes `ui.dots` as `s.dots ~= false`); the owner decided it should read DIGITS (epic Notes, Decision of 2026-10-10, entry 17).

**Approach:** `fresh` takes `ui.dots` as `s.dots == true`, so only a stored `true` gives DOTS. Nothing else changes: the look of each mode, the toggle (which always writes `true` or `false`), the menu, and every existing round's outcome. Companion checks and rounds that relied on the old default set the store or tap the toggle they need and still cover both modes.

</intent-contract>

## Implementation Notes

- Oneshot: a one-expression change in `games/sudoku/main.lua` (`fresh`), plus about 60 changed lines across the companion checks, well under 100. The review patches were applied here too, as the oneshot route says.

## Plan Change Log

## Review Triage Log

### 2026-10-10 — Review pass
- verdicts: 15 findings — high 0, medium 2, low 12, false 1, maybe-false 0
- lenses ran as four context-free subagents (blind-hunter, edge-case-hunter, verification-gap, intent-alignment), started in one message, in the foreground. The diff file was `git diff` since the baseline with the plan intent-to-add.
- findings:
  - `[medium]` `[patch]` blind 1, `long-expert.lua` lost its FILL NOTES in dot mode (the old default) — patched: it taps NOTES AS to DOTS (shown), FILL NOTES, then NOTES AS back to DIGITS (shown), the old sequence; the header comment says so. Host suites pass after the patch.
  - `[low]` `[reject]` blind 2 and edge 4, `ring-full` and `undo-fill` now draw digit notes without any edit — they pin moves, a full ring and undo, not the notes mode (their outcomes pass); dots keep real-game rounds (`notes-dots`, `long-expert`, `toggles`) and the draw-level checks. Pinning each to DOTS adds taps that change what they test.
  - `[low]` `[reject]` blind 3, the unreadable-store guard has no test — `read_store` is unchanged and returns `{}`, the same table the pinned empty store gives; faulting `ch.store.get` would need a double the harness lacks.
  - `[low]` `[patch]` blind 4, the case loop's label read "the store nil" for two cases — patched: each case has a name ("no key", "true", ..., "another key").
  - `[low]` `[patch]` blind 5, a bare `on_menu(s, ui, 6)` without a comment — patched: the comment says it toggles back to `false`.
  - `[low]` `[reject]` blind 6, no manifest bump, help text or release note for the new default — the intent says change nothing else; the package hash is the record (as entry 16 did), and `help.lua` explains HINT and CHECK only.
  - `[false]` `[reject]` blind 7, the plan claims review and results it does not show — the plan is filled at Finalize, before the commit.
  - `[low]` `[reject]` edge 1, a stored non-boolean `dots` now reads DIGITS — the intent's own expression is `s.dots == true`; a boolean is all the toggle writes, and the case is pinned (`"yes"`, `0`, `"true"`, `1`).
  - `[low]` `[reject]` edge 2, an unreadable store shows DIGITS to a player who chose DOTS — every toggle falls back to its default on an unreadable store (the old code gave DOTS to a DIGITS player); keeping the previous value needs a new guard the intent does not ask for.
  - `[low]` `[patch]` edge 3, `"true"` (string) and `1` were not in the cases — patched: both added.
  - `[low]` `[reject]` edge 5, `notes-digits`' `drawn.frame` and `drawn.look` run on a hand-built `ui` — they pin the drawing; the menu text it shows from an empty store and `interaction.store` pin the default through the real game.
  - `[medium]` `[defer]` edge 6, the device-run packet's steps 122 and 159 assume DOTS — the packet is the orchestrator's (brief); recorded in `deferred` with the lines.
  - `[low]` `[reject]` edge 7, the plan said "about 40 lines" and the diff has about 60 — a plan wording fix; the wording is corrected in Implementation Notes anyway.
  - `[low]` `[reject]` intent-alignment (descriptive): the diff implements the store-to-`ui` reading and pins it on the real path (`interaction.*` and four rounds); the draw-level checks build their own `ui` and never read the store; the screenshots and hash named in verify sit outside the diff, and were taken after the review (below).
  - verification-gap: no gap found (it checked that reverting to `~= false` or to a truthiness test fails a pin).


## Design Notes

- Intent: tickets.toml entry 17 ("A store that holds an explicit choice keeps it, since the toggle always writes `true` or `false`") and the epic Notes Decision of 2026-10-10, entry 17 ("defaults to DIGITS, not DOTS. A store with an explicit choice keeps it"). Entry 16's `timer` toggle in the same `fresh` line is kept as built (`s.timer ~= false`, on by default).
- Reading of a non-boolean `dots` (`"yes"`, `0`): the old line read it as DOTS (`~= false`); `== true` reads it as DIGITS, the new default, as `rem`, `shade` and `timer` read a non-boolean as their default.
- Guards in `fresh` kept as they are: it returns early on the same clue signature (Play again keeps `ui`), so the store is read once per new puzzle; a store that cannot be read (`read_store` pcall) gives `{}`, which now gives DIGITS.
- Rounds: `notes-dots` now taps the toggle to DOTS (as `notes-digits` tapped it to DIGITS) so a real round still plays DOTS through the MENU; `notes-digits` plays the default and shows `NOTES AS: DIGITS` on opening the MENU, which pins "no `dots` key gives DIGITS" through the real game; `toggles` flips DOTS then back to DIGITS; `long-expert` (the heap round, digit notes) drops its toggle tap and shows the default instead.
- The device-run packet (`device-run-packet.md`, `.chgame` files) is the orchestrator's; its steps that "set NOTES AS: DIGITS" no longer need the tap, and its "dots first" step now needs one.

## Verification

**Commands:**
- `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j'` -- expected: all pass, `-L games-check` included (both canvases)
- `python3 scripts/pack_game.py games/sudoku <scratch>` -- expected: packs; record the package hash
- `scripts/*_test.py`, `python3 scripts/check_upstream_touches.py`, `./bin/clang-format-fix` twice -- expected: pass, nothing new
- `flock /tmp/crosshatch-build.lock sh -c 'sim.sh build x4pro'` then screenshots: a fresh puzzle with digit notes from an empty store, and the MENU reading `NOTES AS: DIGITS` -- expected: as named

**Manual checks (if no CLI):**
- Look at each screenshot; copy them to `story-notes-digits-screenshots/`.

**Results (on the final tree, after the review patches):**
- Host: `cmake -S test -B build/test -G Ninja ... && ctest --test-dir build/test -j` under the hosttest lock: 1,853 of 1,853 pass, 129 of them `games-check` (both canvases). A mutation of `fresh` back to `s.dots ~= false` failed four tests (`EveryRoundPlaysAsItsFileSays` and `EveryRoundRestoresFromItsSnapshot`, `sudoku_1`, at 466 and at the default canvas), so the new pins are live. Not a flake claim; no flake fix is made here.
- `scripts/*_test.py`: all pass. `scripts/check_upstream_touches.py`: PASS (no non-fork file touched). `./bin/clang-format-fix` twice: nothing new, no formatting-only change.
- Package: `pack_game.py games/sudoku <scratch>`: 42,695 B, hash `f31b5a02a67a712a` (measured on the final tree; entry 16 recorded `95b7db58b5d45f55`, 42,648 B, from its own tree, so no size delta is claimed here).
- Simulator (`sim.sh build x4pro` under the build lock, 18 s, warm cache; the package installed from `fs_/games/` and played): the simulated card's store held only `timer = false` from entry 16's run and no `dots` key (read with `od`), the nearest thing to an empty store. A new puzzle with notes added (cells with three, two and one notes) draws them as the digit images, and the MENU reads `NOTES AS: DIGITS`. No log error.
- Screenshots (`story-notes-digits-screenshots/`, both viewed): `puzzle-digit-notes-empty-store.png` (a fresh Easy puzzle, notes in four cells drawn as digit images, from a store with no `dots`), `menu-notes-as-digits.png` (the nine-row MENU reading `NOTES AS: DIGITS`; TIMER reads OFF only because the card's store kept it from entry 16's run).
- Not run: `pio run`, `pio check`, the other envs. No firmware source or `lib/` changed (the diff is `games/sudoku/main.lua`, its companion checks and this entry's files); the orchestrator's brief says none is needed.

## Auto Run Result

**Summary.** Sudoku's NOTES AS now reads DIGITS when `ch.store` has no `dots` key: `fresh` takes `ui.dots` as `s.dots == true`. A stored `true` gives DOTS, a stored `false` gives DIGITS, and any other value gives DIGITS. Entry 16's `timer` toggle in the same line is unchanged. The look of each mode, the toggle and the menu are as they were.

**New sudoku package hash:** `f31b5a02a67a712a` (42,695 B).

**Files.**
- `games/sudoku/main.lua` -- `fresh` reads `dots` as `s.dots == true`, with a comment.
- `test/game_script/first_party/sudoku/interaction.lua` -- `store` pins none, `true`, `false`, non-booleans and another key (named cases); `reset` and the MENU row 6 action follow the new default.
- `test/game_script/first_party/sudoku/rounds/{notes-dots,notes-digits,toggles,long-expert}.lua` -- `notes-dots` taps to DOTS; `notes-digits` shows the default; `toggles` flips DOTS then DIGITS; `long-expert` plays FILL NOTES in DOTS, then DIGITS, as before.
- This plan and `story-notes-digits-screenshots/` (two PNGs).

**Review.** Four lenses ran as context-free subagents (15 findings: medium 2, low 12, false 1). Patched: the heap round's dot-mode FILL NOTES (medium), the case labels, a comment, two more non-boolean cases. Deferred: the device-run packet's steps 122 and 159 (medium). Rejected with reasons in the log: ring-full and undo-fill's silent mode change, the unreadable-store guard, no manifest bump or help text, the plan claims, a non-boolean reading, an unreadable store showing DIGITS, the hand-built `ui` checks, the plan wording, and the descriptive audit. Follow-up review recommended: false (one medium patched, no high).

**Verification.** As under Verification, Results.

**Residual risks.**
- A player whose store has no `dots` key (never toggled) sees DIGITS after this package, where they saw DOTS; one who toggled keeps their choice.
- The device-run packet still describes the DOTS default (deferred, the orchestrator's).
- Formatting: `./bin/clang-format-fix` changed nothing, so no formatting-only change outside this entry's paths.
