---
title: 'e6pre-8: the single spine edit before epic 6 (spine and docs pass)'
type: 'chore'
ticket: ''
created: '2026-10-04'
status: 'done'
route: 'full'
route_source: 'pinned'
review: 'thorough'
review_source: 'pinned'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
baseline_revision: '6c6b4ba7'
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** The architecture spine and several docs disagree with owner decisions made on 2026-10-04, with the as-built code, and with epic 4 and 5 retrospective proposals; epic 6 planning would read contradictory text.

**Approach:** One docs pass, the only build allowed to edit `ARCHITECTURE-SPINE.md`: apply items A-G of the orchestrator's brief, each checked against code or its cited source, plus one frozen-list line (`limit manifest_nesting_count 32`, owner-approved) with its host constant, tests and CRC.

## Boundaries & Constraints

**Always:** verify each claim against code or source before writing it; amend with a dated "Amended/Reconciled" note rather than silently rewriting a rule; keep Decision records; stop with a blocking question if level 1 is frozen or the nesting limit cannot be tied to a live constant.

**Never:** design Play Nearby; edit `AGENTS.md`, firmware code, or older `deferred-work.md` entries; touch Done-when CI status (S1); move the SDK pointer.

</frozen-after-approval>

## Code Map

- `ARCHITECTURE-SPINE.md` (AD-12, 13, 15, 16, 17, 18, 19, 20, 21, 23, SD-card seed, CI row, layer diagram) -- A, B, C, D.
- `docs/crosshatch/api-level-1.txt`, `lib/GameCore/ApiLevel.h`, `ApiSurfaceTest.cpp`, `ApiLevelTest.cpp`, `scripts/pack_game_test.py` -- D item 8; `StreamingJsonParser::MAX_NESTING` (32) and `pack_game.MAX_NESTING` are the live constants.
- `game-api-seed.md` section 1, `SPEC.md` CAP-3, `upstream-touches.md` row 5, `formats.md` nesting row -- D.
- `epic-pass-and-play.md` Requirements, the title-screen plan's Always line -- E, A5.
- `epic-play-nearby.md` Notes -- F. `deferred-work.md` `## e6pre-8` -- G.

## Implementation Notes

Level 1 is a preview (`API_LEVEL_FROZEN false`), so `check_api_freeze.py` passes a change to its list; the new limit is tied to the parser's constant in both C++ suites and the packer's in `pack_game_test.py`. CRC moved 0x9B618471 to 0x0401CF0D (recomputed by `ApiLevelTest`). Items 3, 7, 11 of AI-10 had landed (AD-17 text, `formats.md` limits table, `game-canvas.md` 502 ms) and were not repeated. C: the epic-play-nearby file names no spine change beyond A; AD-11/13/18 and the Deferred rows were read against the code and needed none.

## Review Triage Log

The four lenses ran as context-free subagents and all returned. Accepted and fixed: R10 contradicted the e5-r7 Decision (reworded); R12 omitted the +21,040 B measurement; AD-12's base bullet and 2026-10-02 sentence still gave the old blank order (pointed to AD-20); unbuilt targets (AD-17, AD-20, AD-21 gap, AD-23) now say "a target, not built until e6pre-N"; the diagram is marked a target; AD-23 names the F9 carve-out it supersedes; the 1,654 ms figure cites the device run (P4, P10); this log. Rejected or deferred: a behavioural vector for 32/33-deep manifests (already pinned by `pack_game_test.py` nesting cases and `StreamingJsonParserTest`); a firmware build for the `ApiLevel.h` CRC (a macro the host build compiled; the brief forbids firmware builds); the lobby/overlay mitigation and a cap on an open overlay (the owner accepted the risk; epic 6's); the 3-tap exception wording (AD-22 already states the fourth tap); PeerGone attribution (the spine cites e6pre-3, where it was decided; the acceptance is dated 2026-10-04 in both); SoloRounds rename checklist (deferred entry C names the trigger); `game-canvas.md` and `formats.md` prefs text belong to e6pre-10/11. Verified-true: no verification gap in the list line, constant ties, or CRC.

## Design Notes

What the rewritten rules protected. (1) AD-12 and AD-20 put the forced-exit blank before the SD steps so no seat's frame stays on the panel if the exit overruns; the owner reversed it (e6pre-10) because the 1,654 ms half-refresh blank exceeded the 1,500 ms window and cost the resume write and store flush. The old sentences stay, marked superseded, so the privacy reason is not lost: e6pre-10 must still blank within the exit. (2) AD-17's "never from onExit()" protected `onExit()` (run under `RenderLock`) from SD work; the exception is one small file from the title screen only, and the match's three-write rule stands. (3) AD-12's "a double tap cannot land on the other" protected hidden hand-offs from a skipped screen; the time guard that kept it was removed by the owner (2026-10-03), and the sentence now records the accepted risk. (4) "The solo machine" wording protected nothing; it was a name that predated pass.

## Verification

- Host suites: `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` under the hosttest lock -- expected: 1649 of 1649 passed (done).
- `python3 scripts/pack_game_test.py`, `scripts/check_api_freeze_test.py` -- OK.
- `./bin/clang-format-fix` twice, `scripts/check_upstream_touches.py`, `scripts/check_layers.py`, `scripts/check_api_freeze.py --base-ref HEAD~1` after commit.
- No firmware builds (docs and tests only; the one header change is a CRC constant).
