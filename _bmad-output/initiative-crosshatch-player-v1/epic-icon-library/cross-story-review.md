# Cross-story review: epic-icon-library

The epic's cross-story review looked at the epic's whole change at once, after entry 9 and entry 10 had landed, to
catch what a single story's review could not: a later story undoing an earlier one's rule, names and texts that
drifted between docs, code and fixtures, and gaps between the stories.

## How it ran

The lenses ran as context-free subagents over `git diff 1eacdc77..ba2ae079` (the epic's base to entry 9's done
commit), minus the generated header `lib/GameIcons/GameIcons.generated.h`, the vendored SVGs, the screenshots (PNGs)
and the BMAD output under `_bmad-output/`. The orchestrator verified each finding against `2ba81907` and sent the eight
below to the `xreview` fix build, which made them in one commit on `epic3/xfix`. Each story whose code a fix changes
records the fix and its evidence in its plan's Verification.

## Triage

| # | Severity | Story | Finding | Outcome | Evidence |
| --- | --- | --- | --- | --- | --- |
| 1 | medium | 3.10 | `GameVM::failure()` (`src/games/GameVM.h`) passed `sessionOutOfMemory` and `game.hostFailure()`, both written on the VM task, to `vmFailure` on every `vmHealthy()` pass, before `failed()` had acquired `done`: a data race. 3.7's version returned early while `failed()` was false | fixed: `if (!failed()) return Failure::None;` before `vmFailure(true, ...)`, with a comment saying the early return keeps the reads after the acquire of `done` | `VmFailureTest` (6 cases) passes; host tests 707/707 pass; `x4pro` and `default` build |
| 2 | low | 3.9 | Category name: `game-icons.md`'s prose says "game pieces", but its table, `assets/game-icons/names.txt`, `api-level-1.txt` (a comment, outside the CRC) and the `icons` fixture's `main.lua` said "Board pieces" | fixed: "Game pieces" in all four. `API_LEVEL_FROZEN` is `false`, so the level-1 comment may change; `gen_game_icons.py` skips `#` lines, so the header does not change | two generator runs to scratch files are byte-identical to each other and to the committed header (`cmp`); `gen_game_icons_test.py` 23 tests OK |
| 3 | low | 3.2 | `docs/crosshatch/game-canvas.md`'s list of reasons for a failed start left out 3.2's `BadImage` ("An image is damaged or too large") | fixed: added to the list | doc only |
| 4 | low | 3.2 | `GameAssets.cpp`'s `addImage`: an SD read error or short read of an image header gave `Truncated`, so the view said the image is damaged instead of that the files cannot be read | fixed: `addImage` sets `readFailed` when the read returns an error or fewer bytes than `min(file size, 62)`, leaving the budget alone; pass 1 then returns `CannotRead`. A file under the 62-byte header still reads whole and is a `Truncated` image. `HalFile::read` returns `-1` on an error but a short count cannot say why, so a short read of a file at least a header long counts as an I/O failure (the brief's file-size rule). Pass 2 already maps any non-`Ok` check to `CannotRead` | `x4pro` and `default` build; not host-testable (no host test builds `src/games/GameAssets.cpp`), and not exercised on a device |
| 5 | low | 3.9 | No test pinned that a manifest `icon` with `_` still parses, which `Manifest.cpp`'s `validIcon` keeps on purpose | fixed: `ManifestTest.AcceptsUnderscoreInIcon` (`"icon": "old_name"` parses, `m.icon` is `old_name`) | host tests 707/707 pass |
| 6 | low | 3.7 | `docs/crosshatch/upstream-touches.md` named the guard forms as `#if`, `#ifdef`, or `#elif`; `check_layers.py` also accepts `#elifdef` and `&&`-joined conditions | fixed: the doc names `#elifdef`, the accepted conditions and `&&` with no `||`, and points to the script's docstring for the full rule | doc only; `check_layers.py` passes |
| 7 | low | epic docs (`2ba81907`) | `docs/crosshatch/game-icons.md`: the IRAM functions listed sum to 659 B, but the text says +684 B of `.iram0.text` | fixed, measured: the other 25 B are 5 B of alignment padding between functions and 20 B of the literal pool at the section's start, which the five functions' 200 B of `.literal.*` sections grow once the linker merges literals already there. `game-icons.md` now gives the breakdown | games-on and games-off x4pro ELFs of this tree (`check_flash_budget.py build on` / `build off`): the only added `.iram0.text` function symbols are the five (659 B, `objdump -t`); padding between function symbols 6,714 → 6,719 B; section start to first function symbol 4,548 → 4,568 B (`objdump -h`, `-t`); `libfreertos.a`'s `.literal.*` for the five: 0x30 + 0x40 + 0x50 + 4 + 4 = 200 B |
| 8 | low | 3.1 | `src/games/GameIconDraw.h` said screens reach the icon library only through `src/games`, but ledger row 9's cover-grid Home includes the generated header directly | fixed: the comment names that exception and points to `GameIcons.h` for why | comment only; `check_layers.py` passes |

## Verification of the fix commit

- Host tests (`cmake` / `ctest`, under the build lock): 707/707 pass, `ManifestTest.AcceptsUnderscoreInIcon` and the
  six `VmFailureTest` cases included.
- Every `scripts/*_test.py`: 8 files, each "Ran n tests" with n > 0, all OK.
- `python3 scripts/gen_game_icons.py --out` twice to scratch files: `cmp`-identical to each other and to the committed
  `lib/GameIcons/GameIcons.generated.h`.
- `check_layers.py`: 362 edges in 92 game files, passed; `check_upstream_touches.py`: PASS (trial merge clean).
- `pio run -e x4pro`, `pio run -e default`, `sim.sh build x4pro`: SUCCESS.
- Flash budget on this working tree (`check_flash_budget.py build on`, `build off`, `compare`; not a fresh tree):
  +227,712 B of flash, 28,288 B under the 250 KiB gate; +776 B of static internal RAM, 248 B under the 1,024 B gate.
- `./bin/clang-format-fix` run twice; the second run changed nothing.
