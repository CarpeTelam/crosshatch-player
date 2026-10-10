---
title: 'e6pre-15: one mode table in lib/GameCore'
type: 'refactor'
ticket: ''
created: '2026-10-10'
status: 'done'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
baseline_revision: 'ccb748bb5f4892ed1b27cb49077dab0709a905c4'
context: []
deferred:
  - summary: scripts/pack_game.py spells the three mode names again (MODES = ('solo', 'pass', 'nearby')) with no test tying it to GameCore::MODE_TABLE
    location: scripts/pack_game.py:82
    severity: low
followup_review_recommended: false
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** The three game modes are written out by hand in several places (`GameCore::modeName`'s switch, `Manifest.cpp`'s `parseMode`, its inline `modes` parse, its `ALL_MODES` and `{MODE_SOLO, MODE_PASS, MODE_NEARBY}` literals, and `GameModeActivity.cpp`'s `MODE_TEXTS` bit/mode/log columns), and epic-play-nearby adds a fourth way to touch every one of them (deferred-work `## e5-close`, D2's mode part).

**Approach:** Per the owner's dated Decision "`e6pre-15`, one mode table" (epic-play-nearby.md, last Note, 2026-10-10): `lib/GameCore` lists each mode once, in solo, pass, nearby order, with its `GameCore::Mode`, its `Manifest::MODE_*` bit, and its name; small functions over that table replace the hand-written mappings; `MODE_TEXTS` keeps only the screen's `StrId` column and takes bit, order, and log name from GameCore, with a compile-time check that it covers every mode in GameCore's order.

## Boundaries & Constraints

**Always:** Behaviour identical: every existing host test passes unchanged; `ctx.mode` strings and the manifest's `BadModes` / `BadDefaultMode` codes unchanged; `API_SURFACE_CRC` unchanged; the table is `constexpr` (no static initializer, `check_flash_budget.py objects` passes); no new layer edge (`scripts/check_layers.py`); `lib/GameCore` includes nothing from `lib/I18n`.

**Never:** Touch `GameSaveStore`'s `resume.bin` mode bytes and switches; the per-mode behaviour switches in `rosterFor` and `startNew` (this epic replaces their Nearby branches); D2's `peekStartable` / `loadStartable` selection; D3, D4; change the spine's allocations or any generated file.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Round trip | each table row's name, `Mode`, bit | the same row back from name, `Mode`, and bit lookups | No error expected |
| Order | the table | solo, pass, nearby; `MODE_TEXTS` in the same order | a mismatch fails the build |
| Unknown name | "", "Solo", "online", "solo " | no row | `modes` entry: `BadModes`; `default_mode`: `BadDefaultMode` |
| Unknown bit | 0, two bits, 0x08, all bits | no row | `oneMode` false; `startMode` ignores it |
| Stray `Mode` value | `static_cast<Mode>(255)` | `modeName` gives "solo" | none |

</frozen-after-approval>

## Code Map

- `lib/GameCore/ModeTable.h` (new) -- `ModeRow`, `MODE_TABLE`, `MODE_COUNT`, `modeRow`, `modeRowForBit`, `modeRowForName`, `ALL_MODE_BITS`, all `constexpr`/`inline constexpr`.
- `lib/GameCore/Roster.cpp` -- `modeName` becomes a lookup; its declaration stays in `Roster.h` (callers: `LuaGame.cpp:430`, `MatchResume.cpp:51`, tests).
- `lib/GameCore/Manifest.cpp` -- `parseMode`, the `modes` array parse in `onString`, `ALL_MODES`, `oneMode`, `startMode`'s literal loop.
- `src/activities/games/GameModeActivity.cpp` -- `MODE_TEXTS` and its users: `modeName`, `writeModesLine`, `nextMode`, `rosterFor`, `startNew`, `startResume`.
- `test/game_core/ModeTableTest.cpp` (new), `test/game_core/CMakeLists.txt` -- the table's test.
- Not changed: `GameSaveStore.cpp` (RESUME_MODE_*), `test/game_script/harness/games_check/RoundFile.cpp` (round-file reader, not a mode mapping the Decision lists), `Manifest::check`'s per-mode rules.

## Tasks & Acceptance

**Execution:**
- [x] `lib/GameCore/ModeTable.h` -- add the table and its lookups -- the one place a mode is listed
- [x] `lib/GameCore/Roster.cpp`, `lib/GameCore/Manifest.cpp` -- replace the hand-written mappings with the lookups
- [x] `src/activities/games/GameModeActivity.cpp` -- `MODE_TEXTS` keeps `{Mode, StrId}`; static_assert it covers the table in order
- [x] `test/game_core/ModeTableTest.cpp` -- cover the matrix rows; register in CMake

**Acceptance Criteria:**
- Given the existing host suites, when they run unchanged, then they pass.
- Given a mode added to the table without a `MODE_TEXTS` row (or in another order), when `GameModeActivity.cpp` compiles, then the static_assert fails.
- Given `lib/GameCore/ApiLevel.h`, when `ApiLevelTest` runs, then `API_SURFACE_CRC` is unchanged and passes.

## Implementation Notes

Implemented directly in the build session (no implementation subagent): the investigation's edits were small and mechanical, so the code was written before this plan was filed, and the plan was filled from the diff. The host test run and every later check ran after the plan existed.

## Plan Change Log

## Review Triage Log

Pass 1, four lenses (blind-hunter, edge-case-hunter, verification-gap, intent-alignment), each a context-free foreground subagent. Counts: high 0, medium 1 (grouped), low 2, false 6, deferred 1.

| # | Lens | Finding | Verdict | Route | Evidence / action |
|---|------|---------|---------|-------|-------------------|
| 1 | blind, edge, gap (x4 reports) | `CoversEveryEnumeratorOfMode` walks `MODE_COUNT` rows, so it cannot fail for a mode added to the enum without a row; `modeName`'s switch warning is gone | medium | patch | Real: a `Mode` enumerator with no row passed every check and read as "solo". Added `static_assert(Mode::Nearby + 1 == MODE_COUNT)` in `ModeTable.h`; renamed the test `RowsFollowTheEnumOrder` and fixed its comment. `rosterFor` / `startNew` keep their switches, which still warn. |
| 2 | blind, edge | `textOf` subtracts pointers, undefined for a row not from `MODE_TABLE` | low | patch | Direct fix: `textOf` looks the row's `Mode` up in `MODE_TEXTS`; the static_assert makes the lookup total. |
| 3 | edge, intent-alignment | `ModeTable.h`'s comment understates what a new mode needs | low | patch | Comment now lists the enumerator, the bit, the row, the `MODE_TEXTS` row and `english.yaml` key. |
| 4 | blind | `textsCoverTable` does not catch swapped `StrId`s | false | rejected | `ModePickerTest` pins the three strings and the cycle order (verification-gap lens read it); both are unchanged and pass. |
| 5 | blind | `modeName`'s solo fallback is thinly justified | false | rejected | Kept deliberately and justified in Design Notes (`LuaGame` pushes it to Lua; the old switch ended in `return "solo"`); tested. |
| 6 | blind, intent-alignment | `GameModeActivity` changes have no new test | false | rejected | Its functions are pinned by existing `ModePickerTest` / `GamesLauncherTest` cases, unchanged and passing in the 1861-test run. |
| 7 | edge | table over 8 rows loses bits in `allModeBits` | false | rejected | Each row's bit is a `Manifest::Mode` enumerator (uint8_t); `bitsAreDistinctSingles` fails the build before 9 distinct single bits exist. |
| 8 | edge | header cycle between `ModeTable.h` and `Roster.h` | false | rejected | `Roster.h` does not include `ModeTable.h`; only `Roster.cpp` does. |
| 9 | blind | ledger / docs unchecked | false | rejected | `check_upstream_touches.py` passes; all three changed files are fork-only. |
| 10 | intent-alignment | `scripts/pack_game.py:82` repeats the names | low | defer | Pre-existing, outside the Decision's list (C++ sites); recorded in `deferred`. |
| 11 | intent-alignment | `Manifest::check` / `claimsUnseatedMode` and the `GameModeActivity.cpp:131` pass special case still name modes | false | rejected | Per-mode behaviour, which the Decision leaves in place. |

## Design Notes

**Where the table lives.** `ModeTable.h` is a new fork file in `lib/GameCore`, including `Manifest.h` (for the bits) and `Roster.h` (for `Mode`); `Roster.h` keeps `modeName`'s declaration, so no caller changes. All edges are inside `lib/GameCore`, so `check_layers.py` sees no new edge.

**Guards in each replaced function (AGENTS.md's `git log -L` rule).** `git log -L` for these functions returns only the squashed merge commits (`a4212569` for `modeName`, `3d4aec97` and `17d02483` for the `Manifest.cpp` ones, `60b59a4b`, `b5566c1c`, `17d02483` for the screen), none with a dropped targeted fix on record; the guards below are read from the code and kept:
- `modeName`: the trailing `return "solo"` after the exhaustive switch answers a value that is no `Mode` (a stray cast) with solo, never null, since `LuaGame` pushes it into `lua_pushstring` and logs print it with `%s`. Kept as the first-row fallback, with a test.
- `parseMode`: leaves `out` untouched on failure and is exact-match (case and whitespace sensitive); `modeRowForName` compares whole `string_view`s the same way. The `modes` parse keeps `|=` so a repeated name is harmless, and an unknown name fails `BadModes` (not `BadDefaultMode`).
- `oneMode`: refuses 0, several bits, and unknown bits, so `startMode` and `fieldsValid` never accept them (a Manifest not from `parse`, a stale remembered bit). `modeRowForBit` matches one row's exact bit, which is the same set; tests pin 0, two bits, 0x08, and all bits.
- `ALL_MODES` in `fieldsValid`: `(m.modes & ~ALL) == 0` rejects unknown bits; `ALL_MODE_BITS` is the OR of the rows.
- `startMode`: the remembered-then-default-then-first-in-order chain is untouched; only the order source changes (table order = solo, pass, nearby).
- `modeTextOf(bit)` returned null for a non-single or unknown bit; `modeName` answered `""`, `rosterFor` false, `startNew` the "no mode" error. All three now test `modeRowForBit(...)` for null, same results.
- `nextMode`: the wrap-around start index (`COUNT - 1` when `current` is not in `modes`) and the final `return 0` are kept; the loop now indexes `MODE_TABLE`.
- `startResume`: the solo-pass-nearby scan stops at the first mode this host can start; now iterates `MODE_TABLE` (same order).
- `rosterFor`'s `roster = Roster::solo()` reset before the switch, and `startNew`'s three log lines, are kept as they were; only the row they read from changed (`row->name` is the old `log` string).

**MODE_TEXTS.** `{GameCore::Mode, StrId}` per row; `textsCoverTable()` (a constexpr function) checks the count and each row's `Mode` against `MODE_TABLE[i]`, and `textOf(row)` finds the row's `Mode` in `MODE_TEXTS`. `ModeTable.h` also ties `MODE_COUNT` to `Mode::Nearby` with a static_assert, so a `Mode` added without a row fails the build. Adding a mode: one table row, one `MODE_TEXTS` row (the static_assert says so), one `english.yaml` key, plus the per-mode switches the compiler flags.

## Verification

**Commands:**
- host suites: `flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test'` then `ctest --test-dir build/test --output-on-failure -j` -- expected: all pass, `ModeTableTest.*` among them
- `python3 scripts/check_layers.py`, `python3 scripts/check_upstream_touches.py`, every `scripts/*_test.py`, `./bin/clang-format-fix` twice -- expected: clean
- under the build lock: `pio run -e x4pro`, `pio run -e default`, `python3 scripts/check_flash_budget.py build on` then `objects`, `pio check` for `default` and `-e x4pro` with AGENTS.md's flags, `sim.sh build x4pro` -- expected: success; `objects` reports no new static initializer


**Results (all on the working tree that became the commit, after the review patches):**
- Host suites: `cmake --build build/test` and `ctest`: 100% of 1861 tests pass, 8 of them the new `ModeTableTest.*`; every pre-existing test unchanged.
- `scripts/check_layers.py` passed (587 edges), `scripts/check_upstream_touches.py` PASS, all 13 `scripts/*_test.py` pass, `./bin/clang-format-fix` run twice with nothing changed (no formatting-only change outside the paths).
- Under the build lock: `pio run -e x4pro` and `pio run -e default` succeed; `check_flash_budget.py build on` and `objects` pass (49 game objects, largest mutable static 4 B, no static initializer); `pio check` for `default` and `-e x4pro` pass with the three `--fail-on-defect` flags; `sim.sh setup` then `sim.sh build x4pro` succeed. No screenshot: the screen's output is unchanged and the mode strings are pinned by the existing `ModePickerTest`.
- `API_SURFACE_CRC` untouched (`ApiLevelTest` passes). No flash delta measured; the orchestrator measures it.

## Auto Run Result

**Summary:** One `constexpr` `GameCore::MODE_TABLE` (`lib/GameCore/ModeTable.h`: `Mode`, `Manifest::Mode` bit, name) with `modeRow`, `modeRowForBit`, `modeRowForName`, `ALL_MODE_BITS`; `modeName`, `parseMode`, the `modes` array parse, `oneMode`, `fieldsValid`'s mask, and `startMode`'s order now read it; `GameModeActivity.cpp`'s `MODE_TEXTS` is `{Mode, StrId}` with a static_assert that it covers the table in order. Behaviour unchanged.

**Files:** `lib/GameCore/ModeTable.h` (new), `lib/GameCore/Roster.cpp`, `lib/GameCore/Manifest.cpp`, `src/activities/games/GameModeActivity.cpp`, `test/game_core/ModeTableTest.cpp` (new), `test/game_core/CMakeLists.txt`, this plan.

**Review:** four lenses as context-free foreground subagents; 11 rows in the Review Triage Log: 1 medium and 2 low patched, 1 low deferred, 7 false.

**Residual risks:** `scripts/pack_game.py:82` still spells the names (deferred); `GameModeActivity` has no new test of its own, only the unchanged `ModePickerTest` / `GamesLauncherTest`. No formatting-only changes outside my paths.
