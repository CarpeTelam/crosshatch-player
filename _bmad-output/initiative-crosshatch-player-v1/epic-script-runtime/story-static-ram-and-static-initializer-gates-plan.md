---
title: 'Static RAM and static-initializer gates'
type: 'feature'
ticket: '3'
created: '2026-09-27'
status: done
baseline_revision: 'e3969f232e006178d2912863f3c0ad65a0c5ec0e'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context: ['{project-root}/docs/crosshatch/fork-scripts.md']
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** AD-2 caps game code's internal RAM (x4pro `.dram0.data` + `.dram0.bss` + `.noinit` may grow at most 1 KiB with games on) and forbids dynamically initialized statics and mutable statics over 64 B in `lib/Game*`, `src/games`, and `src/activities/games`, but nothing enforces either; the flash gate only compares `firmware.bin` (R11, deferred-work "spine rules ahead of the code").

**Approach:** Extend `scripts/check_flash_budget.py`: `compare` also sums the three sections from each ELF with the toolchain's `size -A` and fails over the RAM limit; a new `objects` subcommand reads every game object of the games-on build with the toolchain's `readelf` and fails on a static initializer or a mutable static over 64 B. Both print their measurements, and the flash budget job runs both.

## Boundaries & Constraints

**Always:** Standard library plus the x4pro toolchain found from the saved `pio project metadata` (`cc_path`); `fork_common` exit contract (0/1/2) and step summaries. Sidecar tests cover each limit at, under, and one over, with fake tools on `PATH`, no firmware build. The object check passes with zero game objects and picks up new game directories and libraries without edits. Existing flash behaviour, CLI, and exit codes stay.

**Never:** Edit upstream's `ci.yml` or any C/C++ source (scratch files for verification are never committed). No allowlist for existing symbols. No new fork script file.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| RAM within | on − off of the three sections ≤ limit (1,024 B) | exit 0, per-section table in stdout and summary | none |
| RAM over | difference = limit + 1 | exit 1, "Over budget" for RAM even if flash is within | none |
| RAM section missing | `.dram0.data` or `.dram0.bss` absent from an ELF | exit 2 naming it; absent `.noinit` counts 0 | SetupError |
| tool missing | derived `size`/`readelf` not runnable, or `cc_path` not ending in `gcc` | exit 2 | SetupError |
| static initializer | object has a non-empty `.ctors*`, `.init_array*`, or `.preinit_array*` section, or defines a `_ZGV*` guard variable | exit 1, object and section/symbol listed | none |
| mutable static | `OBJECT` symbol in a writable section (or `COM`), size 64 B pass, 65 B fail | exit 1, object, symbol, size | none |
| read-only data | `constexpr`/`inline constexpr` of any size (non-writable section) | not flagged | none |
| no game objects | no game sources and no objects | exit 0, "0 objects" | none |
| sources without objects | a game dir or `lib/Game*` has `.c`/`.cpp` sources but the build dir has no objects for it | exit 2 | SetupError |

</frozen-after-approval>

## Code Map

- `scripts/check_flash_budget.py` -- `load_build` (reads `defines`, `prog_path`) becomes a record that also keeps the ELF path and `cc_path`; `report`/`compare` gain the RAM table and limit; `main` adds `--ram-limit-bytes` to `compare` and an `objects` subcommand; docstring gains both. Keep `build`, `build_environment`, `check_flag`, `pio`, `FLAG`/`UNFLAG`, the flash table text ("Within budget", "Over budget** by N B").
- `scripts/check_flash_budget_test.py` -- `CompareTest.put` writes only metadata and a sparse image; extend it to write a fake ELF plus canned `size -A` output and a `cc_path` whose sibling fake `…-size`/`…-readelf` print canned text, with defaults so the existing cases keep their meaning.
- `.github/workflows/crosshatch-ci.yml` -- `flash-budget` job: env `FLASH_BUDGET_KIB` (a test pins it to the default; do the same for `RAM_BUDGET_BYTES`), steps "Build x4pro with games on/off", "Compare image sizes"; header bullet for the job.
- Real layout (measured on the main tree): toolchain `~/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-{gcc,size,readelf}`; library objects at `.pio/build/x4pro/lib<hash>/<LibDir>/*.o` (e.g. `lib42e/GameCore/GameCore.cpp.o`), src objects at `.pio/build/x4pro/src/games/*.o`. Existing game objects (GameCore, GameIcons, GameScript, GamesBuildAnchor, ForkReleaseProbe) have no writable object symbols; `ForkRelease::LATEST_RELEASE_URL` (74 B, weak, read-only) must stay unflagged. ELF sizes today: `.dram0.data` 29,615, `.noinit` 1, `.dram0.bss` 72,200.
- This toolchain emits static constructors into `.ctors`, not `.init_array` (checked with a scratch object and `src/main.cpp.o`); a dynamically initialized local static emits a `_ZGVZ…` guard in `.bss` and no ctor section.

## Tasks & Acceptance

**Execution:**
- [x] `scripts/check_flash_budget.py` -- toolchain lookup from `cc_path`; `static_ram(size_output)` parser and RAM compare with `DEFAULT_RAM_LIMIT_BYTES = 1024`; `game_objects(build_dir, project_dir)` discovery with the sources-without-objects guard; `object_findings(readelf_output)` parser; `objects` subcommand with its own summary heading; docstring.
- [x] `scripts/check_flash_budget_test.py` -- matrix rows: RAM 1,023/1,024/1,025 B, missing section, per-section table; object check 64/65 B, `.ctors` 0/4 B, `.init_array`, guard variable, read-only 200 B, `COM`, zero objects, sources without objects, new dir picked up, CLI exit codes 0/1/2; parsers fed text captured from the real tools.
- [x] `.github/workflows/crosshatch-ci.yml` -- `RAM_BUDGET_BYTES: 1024`; compare step passes `--ram-limit-bytes`; new "Check game objects" step that runs when the games-on build succeeded, even after a failed compare; header bullet.

**Acceptance Criteria:**
- Given the committed tree, when every `scripts/*_test.py` runs, then all pass.
- Given real x4pro games-on and games-off builds of this commit, when `compare` and `objects` run, then both exit 0 and print the RAM delta, flash delta, objects checked, and largest mutable static; the numbers go in the Verification record.
- Given a scratch 2 KiB mutable global used from `setup()` under `#if FREEINK_CAP_GAMES`, when the games-on build is compared, then the RAM gate exits 1; given a scratch `src/games` object with a static constructor and one with an unreferenced 128 B mutable global, then `objects` exits 1 naming both.
- Given a fresh tree of the commit with submodules, when the job's step commands run, then they exit 0.

## Implementation Notes

- Implemented directly (no coding subagent in this session). Files: `scripts/check_flash_budget.py`, `scripts/check_flash_budget_test.py` (18 -> 63 tests), `.github/workflows/crosshatch-ci.yml`, this plan.
- `load_build` now returns a `Build(defines, elf, image, cc_path)` record; `build` uses `.image`. Tools come from `cc_path` minus `gcc` (`xtensa-esp32s3-elf-size`, `-readelf`); one `run_tool` maps a missing or failing tool to `SetupError`.
- The readelf parser matches the tool's fixed columns (hex address/offset/size/ES, then flags and three numbers), so empty flags and the blank name of section 0 parse; a symbol size of 100,000 or more is printed in hex and is handled. An object whose output yields no section rows is a `SetupError`, so a format change cannot pass silently. `ParseTest` feeds verbatim `size -A` and `readelf -W -S -s` output captured from the real toolchain; on the real `src/main.cpp.o` every one of its 977 section and 1,570 symbol rows parsed.
- Found while validating on real objects: skipping COMDAT symbols (Design Notes) was needed; without it `BoardConfig::ACTIVE` and the `PersistableStore` singletons from `src/main.cpp.o` were reported, and game code using `SETTINGS` would have failed.
- Hidden `--project-dir` option (like `--metadata-dir`) lets the CLI test run `objects` against a fixture project.
- The scratch run left `ScratchGates.cpp.o` and friends in `.pio/build` after their sources were deleted (an incremental build never removes objects), so `game_objects` skips an object whose source no longer exists (library objects drop the library's `src/`).
- Review patch: readelf's announced section and symbol counts must match the parsed rows (exit 2 otherwise).
- Mutation check (scratch script, not committed): eleven single-line mutations of the new logic (COMDAT skip, 64 B bound, writable test, RAM bound, RAM verdict in the exit code, guard detection, empty-section bound, `.noinit` in the sum, sources-without-objects guard, library `src/` mapping, count check) each fail the sidecar tests.
- No C/C++ committed; scratch sources for the failing cases were removed after the run.

## Plan Change Log

## Review Triage Log

Pass 1 (no subagents in this session; each lens run here one at a time, its prompt read fresh, judged against the diff only). Diff 61.6 kB, blind floor 8. Verdicts: high 0, medium 1, low 5, false 6, maybe-false 0.

| # | Lens | Finding | Verdict | Route / evidence |
| --- | --- | --- | --- | --- |
| 1 | edge, blind | A `readelf` row the patterns misread is dropped silently: a symbol whose section row did not parse, or every symbol row after a layout change, would pass the check | medium | Patched: `parse_readelf` also returns the counts readelf announces ("There are N section headers", "contains N entries"), and `inspect_object` fails with exit 2 unless every row parsed; `test_a_row_that_does_not_parse_is_setup_error`, real objects (`main.cpp.o` 977/1,570) still parse. |
| 2 | intent, edge (claim) | Matrix row "OBJECT symbol in a writable section, 65 B fail" is narrowed: COMDAT symbols are skipped, so a mutable inline variable over 64 B in a game header passes | low | Rejected: AD-2 scopes the rule to game code, and without the skip `BoardConfig::ACTIVE` (SDK, 196 B) and upstream's `PersistableStore` singletons fail every game object that includes them; attributing COMDAT copies to their header needs DWARF or a whole-build scan. Documented in the docstring and Design Notes; named in the report. |
| 3 | blind | The spine names `.init_array`; the check also fails `.ctors`, `.preinit_array`, and `_ZGV*` guards | false | Intended: this toolchain emits `.ctors` only (checked with a scratch object and `src/main.cpp.o`), and AD-2 forbids dynamic initialization at any scope, which a guard variable is. |
| 4 | blind | `constexpr` pointer tables or vtables could sit in writable `.data.rel.ro` and be flagged | false | Checked with the x4pro toolchain: `constexpr const char* kNames[40]`, `static constexpr int kTable[100]`, and a vtable land in `.rodata*` (flags `A`/`AG`); the build is not PIC. |
| 5 | blind | `toolchain_tool` needs `cc_path` ending in `gcc` (a clang or `.exe` path fails) | low | Rejected: exit 2 names the value; the x4pro env and CI use the GCC toolchain on Linux. |
| 6 | blind | `compare` now needs the toolchain, so a missing `size` hides the flash result too | low | Rejected: exit 2 with the cause; the same job installs the toolchain for the builds it compares. |
| 7 | blind | `SOURCE_SUFFIXES` omits `.s`, `.cxx`, `.c++` | low | Rejected: no game source uses them; a game directory with only such sources is simply not required to have objects, objects found are still checked. |
| 8 | blind | Stale objects from deleted sources in an incremental tree would fail the check | false | Handled before review (seen in the scratch run): objects whose source is gone are skipped; `test_stale_object_of_a_deleted_source_is_not_checked`. |
| 9 | blind | The RAM table's Limit row leaves two cells blank | low | Rejected: cosmetic. |
| 10 | edge | `lib*/<name>` could match another library's directory of the same name | false | The glob matches only direct children of `lib<hash>/`, which PlatformIO names after the library directory; `test_other_code_is_not_checked`. |
| 11 | verification-gap | The workflow's new `objects` step and `--ram-limit-bytes` are not exercised by a test | false | Source-text assertions would not count; the fresh-tree run of the job's commands is the verification (Verification record); `RAM_BUDGET_BYTES` is pinned to the script default by a test. |
| 12 | edge (deletion) | `load_build` changed from a 2-tuple to a 4-field record | false | Only `build` and `compare` in this script call it (repo grep); both updated. |

## Design Notes

A subcommand, not a new script: the check reuses the saved metadata (build dir, `cc_path`), and the ticket places it in `check_flash_budget.py`'s job. "Writable section" (`W` in `readelf -S`) is the test for mutable: `constexpr` data lands in `.rodata`, while `constinit` mutable data and `DRAM_ATTR`/`.noinit`/`.ext_ram.bss` buffers are writable, which is what AD-2 means. Symbols in a COMDAT group (`G`) are skipped: header inline functions' statics and inline variables are copied into every object that uses them, so upstream's `PersistableStore<T>::getInstance()` instance and the SDK's `BoardConfig::ACTIVE` (196 B) would fail any game object that uses `SETTINGS` or the board header. A mutable inline variable in a game header is therefore not caught here; a static initializer it needs still is, since its constructor call lands in the includer's own `.ctors`. `_ZGV*` guards and `.ctors` are included because AD-2 forbids any dynamic initialization at any scope and this toolchain never emits `.init_array`.

## Verification

**Commands:**
- `for t in scripts/*_test.py; do python3 "$t" || exit 1; done` -- expected: every file OK.
- Under the shared flock: `python3 scripts/check_flash_budget.py build on`, `build off`, `compare --limit-kib 250 --ram-limit-bytes 1024`, `objects` -- expected: exit 0, numbers recorded.
- Scratch sources (uncommitted), rebuild on, `compare` and `objects` -- expected: exit 1 each.
- Fresh tree (`git archive` + submodule init) of the commit: the job's step commands -- expected: exit 0.

**Verification record (2026-09-27, Python 3.11.15, pioarduino 6.1.19, xtensa-esp32s3-elf binutils from `toolchain-xtensa-esp-elf`; every build under the shared flock):**
- Sidecar tests: `check_flash_budget_test.py` 63 pass (was 18); `check_upstream_touches_test.py`, `fork_common_test.py`, `fork_release_test.py` pass; each file exits 0.
- Epic record, games-on minus games-off on this commit (no runtime code yet): **flash −15,136 B** (5,647,488 vs 5,662,624 B `firmware.bin`); **static internal RAM +0 B** (`.dram0.data` 29,615, `.dram0.bss` 72,200, `.noinit` 1, both builds). `objects`: 5 game objects (GameCore, GameIcons, GameScript, GamesBuildAnchor, ForkReleaseProbe), no mutable static, no static initializer. Story 2.1's objects are not in this tree; the check takes them from `lib/Game*`, `src/games`, `src/activities/games` with no edit.
- Failing cases on real builds (worktree, uncommitted scratch, then removed): `src/games/ScratchGates.cpp` with a 2 KiB `volatile char` array touched from `setup()` under `#if FREEINK_CAP_GAMES`, `ScratchCtor.cpp` (`int g = esp_random();`), and `ScratchUnreferenced.cpp` (`char g[128];`, referenced nowhere). Rebuilt games-on: `compare` exit 1, RAM `.dram0.bss` +2,048 B, "Over budget by 1,024 B" (flash within); `objects` exit 1 with three problems: `ScratchCtor.cpp.o` `.ctors holds 4 B`, `g_scratchRam` 2,048 B, `g_scratchUnreferenced` 128 B (the linker dropped the last one, so only the object check sees it).
- Parser against real objects: a scratch object (static ctor, guarded local static, 128 B statics, `constinit` 160 B, `constexpr` 200 B, `inline` 100 B) and `src/main.cpp.o` (977 sections, 1,570 symbols, all parsed) report as expected; `constexpr` pointer tables and vtables sit in `.rodata`.
- Fresh tree (retro AI-8) of commit `a411a637d14405a7536bfc008a22bb49fe463a9e`: the worktree guard refuses git aimed outside the worktree, so the tree came from `git archive` of the commit, of `freeink-sdk` at `111fdcc7`, and of its `lucide` submodule at `c81680e0`, into `<scratch>/s2-3/fresh` (no `.pio`, no `platformio.local.ini`). Ran the job's steps with its env: `build on` (3 min 20 s, exit 0), `build off` (exit 0), `compare --limit-kib 250 --ram-limit-bytes 1024` exit 0, `objects` exit 0, same numbers as above; the Fork script tests step exited 0 with all four files. The final commit differs from `a411a637` only in this record.
- `check_upstream_touches.py` not run end to end (shallow clone, no `upstream` remote): every changed path is a Game path (`scripts/check_flash_budget*.py`, `.github/workflows/crosshatch-*.yml`) or `_bmad-output/`. No C/C++ changed, so no clang-format or C3 build applies.
