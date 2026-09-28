---
title: 'e3r-3 gate and tool hardening'
type: 'chore'
ticket: ''
created: '2026-09-28'
status: 'built'
baseline_revision: '8bd18e86609d0e41b7bf22d95f9e95319449f228'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/AGENTS.md'
  - '{project-root}/docs/crosshatch/fork-scripts.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Five low findings from the epic-icon-library retrospective (AI-11: R9 b, c, e; R8 c, f) leave fork gates and tools weaker than they claim: the layer check reads only a guard's first physical line; `sim.sh setup`/`check` accept an end marker before the begin marker; the build anchor's comment overstates what games-off builds compile; the vendored Phosphor SVGs are not pinned by content; and regeneration off Linux may differ from CI's bytes without the docs saying so.

**Approach:** Join backslash-continued lines before reading `#if` guards; require `platformio.local.ini`'s markers to be absent or exactly one BEGIN followed by one END in both `setup` and `check`, with a scripted test CI runs; correct the anchor comment; add a committed `SHA256SUMS` that the generator verifies (so `Icons up to date` fails on an edited SVG) and can rewrite with a documented flag; document "regenerate on Linux".

## Boundaries & Constraints

**Always:** The generated header stays byte-identical to the committed one. Exit contract 0/1/2 (fork-scripts.md). Standard library only in Python. Keep every existing guard in the changed functions (`generate`'s missing-SVG and I/O SetupErrors, `cmd_setup`'s refusal before rewriting, `cmd_check`'s three failure branches). Test `sim.sh` only on scratch repositories, never the real `platformio.local.ini`.

**Never:** Edit `lib/GameScript`, `src/games` other than `GamesBuildAnchor.cpp`'s comment, `src/activities/games`, `api-level-1.txt`, `game-canvas.md`, `ci.yml`, `.skills/`, or the SDK pointer. No change to the rasterizer or libm use. No upstream file touched.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Continued guard, always true | `#if FREEINK_CAP_GAMES && \` / `X \|\| 1` / include / `#endif` in a ledgered upstream file | include reported outside a games branch | exit 1, one problem at the include's line |
| Continued guard, valid | `#if FREEINK_CAP_GAMES && \` / `defined(Y)` / include | passes | — |
| sim markers valid | none, or one BEGIN … END | setup replaces the block, keeps other lines; check 0 when current | — |
| sim markers malformed | BEGIN only; END only; END before BEGIN; BEGIN BEGIN END; two blocks; BEGIN END END | setup exits 1, file byte-unchanged; check exits 1 | "restore it by hand" message |
| SVG edited | named SVG's bytes differ from SHA256SUMS | generator exit 1 naming the file; header not written | — |
| Sums list drift | named SVG missing from list; listed path not named; path listed twice; malformed line | exit 1 | — |
| No SHA256SUMS | file absent | exit 2 | — |
| `--write-sums` | valid map | writes `<assets>/SHA256SUMS` (sha256sum format, sorted bytewise), no header | exit 2 on write error |

</frozen-after-approval>

## Code Map

Every path is relative to the git worktree `/home/user/wt-tools`; edit only there (never `/home/user/crosshatch-player`). Do not commit, and do not run `pio`, `sim.sh setup`/`build`, or the host-test CMake build: the build agent runs those under a shared lock. Scratch files go under `/tmp/claude-0/-home-user-crosshatch-player/7e992fcf-41e4-557e-980d-e2c780c5216a/scratchpad/e3r-3/`.

- `scripts/check_layers.py` -- `games_guarded_lines` (~:402) iterates `blank(text, literals=False).splitlines()`; `CONDITIONAL` (:204); `games_branch` (:391) unchanged. Module docstring (:26-37) lists "a directive split by line continuations" as an out-of-scope evasion (meant for `#include`); say that conditionals are joined.
- `scripts/check_layers_test.py` -- `test_unguarded_upstream_edge_fails` cases dict (:219) on `src/components/CoverGridHomeUi.cpp`; `test_upstream_file_beyond_its_ledger_edge_fails` shows a passing-guard shape.
- `.claude/skills/run-crosshatch-player/sim.sh` -- fork-only (not in upstream). `cmd_setup` (:38) guard at :42; `cmd_check` (:63); `managed_block`; `cmd_build` runs `setup` when `check` fails. Script `cd`s to `git -C $SKILL_DIR rev-parse --show-toplevel`, so a test runs a copy inside a scratch git repo.
- `.claude/skills/run-crosshatch-player/SKILL.md` :43-51 -- describes the end-marker case; widen to the new rule.
- `scripts/gen_game_icons.py` -- `generate` (:511) reads each SVG's bytes (:521) after `read_map`; `main` (:534) argparse `--assets`, `--out`. Docstring (:1-37) holds usage, exit codes, and the libm note (:22-24).
- `scripts/gen_game_icons_test.py` -- `MainTest.setUp` copies x and dice-six into a temp assets folder; `run_main(names, out)` writes names.txt and calls `main`.
- `assets/game-icons/` -- `names.txt`, `phosphor/{regular,fill}/*.svg` (110), `phosphor/LICENSE`. New `SHA256SUMS` here (Game path `assets/game-icons` already ledgered).
- `src/games/GamesBuildAnchor.cpp:3-10` -- comment only. `lib/GameScript/ChBindings.cpp:4` includes `<GameIcons.h>` with no `#if`, and `test/game_script/CMakeLists.txt:113` puts lib/GameIcons on host tests' path, so where the sort assert runs must be measured, not assumed.
- `.github/workflows/crosshatch-ci.yml` -- header comments for Fork script tests (:17) and Icons up to date (:27); job bodies unchanged.
- `docs/crosshatch/game-icons.md` -- "Where the icons come from" (:10-25), "Adding or changing an icon" (:146).
- `docs/crosshatch/fork-scripts.md` "CI"; `docs/crosshatch/upstream-touches.md` "Game paths" (add the new test).

## Tasks & Acceptance

**Execution:**
- [x] `scripts/check_layers.py` -- in `games_guarded_lines`, splice backslash-newline continuations (C translation phase 2) into logical lines that keep their physical line numbers; a directive is read from its joined text, and a non-directive logical line marks all its physical lines guarded when in a games branch. Update the docstring. -- R9 (b).
- [x] `scripts/check_layers_test.py` -- add the always-true continued guard to the unguarded cases (problem at line 3), and a passing continued guard (`&& \` then `defined(X)`). -- R9 (b).
- [x] `.claude/skills/run-crosshatch-player/sim.sh` -- one helper that returns non-zero unless the markers are absent or exactly one BEGIN then one END (awk over the file); `cmd_setup` dies with "restore it by hand" before `mktemp`; `cmd_check` reports it with its own message ahead of the no-block branch. -- R9 (c).
- [x] `scripts/sim_sh_test.py` -- new unittest: builds a scratch git repo holding copies of `sim.sh` and `simulator.ini` under `.claude/skills/run-crosshatch-player/`, a seeded `fs_/books/`, and a `platformio.local.ini`; asserts setup/check exit codes and file contents for every matrix row; skips when `bash`/`awk` are absent. -- R9 (c), picked up by Fork script tests.
- [x] `.claude/skills/run-crosshatch-player/SKILL.md` -- state the marker rule. -- keep SKILL.md true.
- [x] `src/games/GamesBuildAnchor.cpp` -- correct the comment to what builds show (Design Notes). -- R9 (e).
- [x] `scripts/gen_game_icons.py` -- `SUMS_NAME = 'SHA256SUMS'`; read and validate it in `generate` before rendering; compare each SVG's bytes' SHA-256; reject unlisted, extra, duplicate, malformed; `--write-sums` writes the file and exits; docstring usage, exit codes, and a "regenerate on Linux" line. -- R8 (c), (f).
- [x] `scripts/gen_game_icons_test.py` -- `run_main` writes sums for the map's SVGs by default; cases for each sums failure (1), missing file (2), `--write-sums` output format and round trip, and the committed sums matching the committed SVGs. -- R8 (c).
- [x] `assets/game-icons/SHA256SUMS` -- generate with `python3 scripts/gen_game_icons.py --write-sums`.
- [x] `docs/crosshatch/game-icons.md` -- the sums file, its command, the step in "Adding or changing an icon", and "regenerate on Linux, as CI does" with the libm reason. -- R8 (c), (f).
- [x] `.github/workflows/crosshatch-ci.yml` (comments only), `docs/crosshatch/fork-scripts.md`, `docs/crosshatch/upstream-touches.md` -- mention the sums check and the sim.sh test; ledger the test path.

**Acceptance Criteria:**
- Given the committed tree, when `python3 scripts/gen_game_icons.py --out <tmp>` runs, then it exits 0 and `cmp` with the committed header is equal.
- Given one committed SVG with a byte changed, when the Icons up to date step runs, then it exits 1 naming that SVG.
- Given a fresh tree of the commit, when the Layer check, Fork script tests, and Icons up to date steps run there, then each passes.

## Implementation Notes

- R9 (e) decision, measured: the games-off C3 `default` build compiles `lib/GameScript/ChBindings.cpp`
  (`.pio/build/default/lib*/GameScript/ChBindings.cpp.o` exists in both the main checkout's `default` build of
  `8bd18e86` and this worktree's; its strings name `GameIcons.h`), while `src/games/GamesBuildAnchor.cpp.o` is an
  empty 1,328 B object there. So `GameIcons.h`'s `static_assert(strictlySorted())` already runs on the C3 through
  ChBindings.cpp's unguarded include; only the anchor's comment was wrong. The comment now says the anchor's own
  checks run only with games on, and GameIcons.h's run wherever lib/GameScript compiles. No include moved.
- check_layers: `logical_lines` splices backslash-newline (trailing blanks after the backslash allowed, as GCC and
  Clang do) on the comment-blanked text; a `//` comment ending in a backslash is blanked first, so it does not
  continue (a deliberate evasion, out of scope). Added a case for an `#if` that continues a `#define` (not a
  directive after splicing) beside the plan's two, and a unit test of the line numbering.
- sim.sh: `markers_ok` (awk) replaces setup's end-marker-only guard with the full rule; check reports it first,
  and its old no-end-marker branch stays (now reached only if `markers_ok` were loosened).
- gen_game_icons: `read_svg` holds generate's missing-SVG and read guards, shared with `--write-sums`. The
  unlisted-SVG check runs after the missing-SVG guard, so a missing SVG stays exit 2. Accepted sums lines are
  sha256sum's text (`  `) or binary (` *`) mode with lower-case hex; blank lines and comments are malformed. Order is
  not enforced on read; `--write-sums` sorts. `--write-sums` ignores `--out`.
- clang-format 21 came from PyPI (`uv tool run --from 'clang-format>=21,<22'`, symlinked as `clang-format-21` on
  PATH); `./bin/clang-format-fix` changed nothing, twice.

## Plan Change Log

## Review Triage Log

Pass 1 (thorough). Lenses ran as context-free subagents and all four returned: blind-hunter, edge-case-hunter, verification-gap, intent-alignment. Verdicts: high 0, medium 0, low 13, false 0, maybe-false 0 (duplicates across lenses counted once). Routes: 6 patch groups, 7 rejected, 0 defer, 0 bad_plan, 0 intent_gap.

| # | Lens | Finding | Verdict | Route | Evidence / action |
|---|------|---------|---------|-------|-------------------|
| 1 | blind, edge, intent | Splicing runs after comment blanking: `// x \` then `#if FREEINK_CAP_GAMES` passes as a guard; `#else \ // x` splices the next line (false fail); the docstring claims phase-2 order | low | patch | Reproduced by two lenses. Splice the raw text first, then blank the logical text; both cases tested. |
| 2 | blind, edge | A directive continued through a multi-line block comment (`#if FREEINK_CAP_GAMES /*` / `*/ \|\| 1`) is read from its first line | low | patch | Reproduced (lines 2-3 guarded). Same class as R9 (b); such a directive now counts as no games branch, docstring says so, case tested. |
| 3 | verification-gap | No test puts an include on a later physical line of a guarded logical line; `guarded.add(first)` passes all tests | low | patch | Mutation shown by the lens. Passing fixture added. |
| 4 | blind, intent | Icons up to date: a generator failure aborts the step with no `::error` annotation; the hint omits Linux | low | patch | Step ran bare `python3 ... --out`; wrapped with an annotation naming SHA256SUMS / --write-sums, hint says Linux. |
| 5 | blind | Generator docstring opening "byte for byte, on every run" contradicts "a run elsewhere may differ" | low | patch | Qualified to one platform. |
| 6 | blind | "retro AI-11" is ambiguous (epic-script-runtime's retro has an AI-11 too) | low | patch | Epic named in the ledger row, test docstring, and test comments. |
| 7 | blind, edge | `cmd_check`'s old no-end-marker branch is unreachable after `markers_ok` | low | reject | True, but the frozen Always keeps `cmd_check`'s three branches; it is a harmless backstop, no user-visible effect. |
| 8 | blind, intent | The anchor comment's claim rests on `ChBindings.cpp`'s unguarded include, unenforced; retro offered including `GameIcons.h` outside the `#if` | low | reject | The generated data is env-independent and its sort assert runs on every games-on env (x4pro, sticky, simulator) and in host tests (`test/game_script`), so losing the C3 run would lose no coverage; the comment states the current fact, verified in the default build (`libafc/GameScript/ChBindings.cpp.o`). |
| 9 | blind | `--write-sums` silently re-pins edited SVGs | low | reject | By design in the intent ("can rewrite with a documented flag"); the SHA256SUMS diff is reviewed, and game-icons.md step 3 says to check it. |
| 10 | blind, edge | CRLF or trailing blanks on marker lines read as no markers; setup appends a second block | low | reject | Pre-existing (the old `grep -qxF` and awk matched whole lines too); setup writes LF markers; unlikely in everyday use and the fix normalises four sites. |
| 11 | edge | An unreadable `platformio.local.ini` (awk exit 2) is reported as malformed markers | low | reject | `setup` just `touch`ed the file; an unreadable file is unlikely and the command still exits non-zero with a hand-fix message. |
| 12 | edge | `write_sums` truncates SHA256SUMS before a failed write | low | reject | Same non-atomic pattern as the header write; the file is committed, so `git checkout` restores it; unlikely. |
| 13 | intent, verification-gap | `sim_sh_test.py` skips (still "Ran 4") without bash/awk/git; `markers_ok`'s two order rules are redundant with the count check | low | reject | ubuntu-latest has all three; the redundancy is no coverage hole (lens: count check and the `cmd_check` call are each pinned). |

## Design Notes

- R9 (b): splice every continuation, not only directive ones: after phase 2 a `#if` on a line following `#define A \` is not a directive, and a continued non-directive line inside a games branch stays guarded on every physical line. `#include` split by a continuation stays out of scope (docstring).
- R9 (c): the rule is strict (one block at most) because `awk`'s skip logic drops the user's lines between two BEGINs; two complete blocks would be survivable, but one rule is easier to state and test. A lone END is refused too.
- R9 (e): decision recorded in Implementation Notes after the default build shows whether `ChBindings.cpp` (which includes `GameIcons.h` unguarded) compiles on the C3. If it does, the sort assert already runs on every env and only the anchor's comment is wrong; including `GameIcons.h` outside the `#if` would add nothing. Correct the comment either way.
- R8 (c): the generator, not only the test, verifies, so the gate the finding names (Icons up to date) fails; `sha256sum -c SHA256SUMS` in `assets/game-icons` also verifies it. The sums cover exactly the SVGs names.txt names.
- Guards kept: `generate`'s is_file (missing SVG, exit 2, before hashing), read OSError, write OSError; `cmd_setup`'s refusal runs before any write; `cmd_check`'s branches keep their order after the new malformed check.

## Verification

**Commands:**
- `for t in scripts/*_test.py; do python3 $t; done` -- expected: all pass, each "Ran n tests" n>0.
- `python3 scripts/check_layers.py` -- expected: exit 0.
- `python3 scripts/gen_game_icons.py --out $S/fresh.h && cmp lib/GameIcons/GameIcons.generated.h $S/fresh.h` -- expected: equal.
- `(cd assets/game-icons && sha256sum -c --quiet SHA256SUMS)` -- expected: exit 0.
- Under the lock: `pio run -e x4pro` and `pio run -e default` -- expected: success; default's objects show whether ChBindings.cpp compiled.
- Fresh tree of the commit (clone, or archive recipe): run the Layer check, Fork script tests loop, and Icons up to date step commands -- expected: pass.
- `python3 scripts/check_upstream_touches.py` -- expected: exit 0.

**Evidence (this build):**
- All 9 `scripts/*_test.py` pass after the review patches, each with "Ran n tests" n>0 (check_layers 51, gen_game_icons 31, sim_sh 4, others unchanged). Against the baseline scripts, the new check_layers cases and 5 of 6 malformed-marker subtests fail.
- `python3 scripts/check_layers.py`: passed. `gen_game_icons.py --out <scratch>` then `cmp` with the committed header: equal. `sha256sum -c --quiet SHA256SUMS`: OK.
- Under the lock, on the final C++ state (only `GamesBuildAnchor.cpp`'s comment changed): `pio run -e x4pro` SUCCESS (4:19) and `pio run -e default` SUCCESS. The default build has `.pio/build/default/libafc/GameScript/ChBindings.cpp.o`, so `GameIcons.h`'s asserts compile on the C3 (R9 e decision above). No firmware size was measured: no code changed.
- Fresh tree: a `git archive` tree of this commit before its plan note was amended in (only this plan differs) plus every submodule's archive, nested ones included (the orchestrated-epics recipe; the gates read no git history). There, Layer check passed; the Fork script tests loop passed all 9 files, each with "Ran n tests" n>0; the Icons up to date step (extracted from crosshatch-ci.yml, run under `bash -e`) printed "matches" and exited 0, and with one byte appended to `phosphor/regular/heart.svg` it printed the generator's SHA-256 error and the `::error` annotation and exited 1.
- `python3 scripts/check_upstream_touches.py` on the same commit: Result PASS, trial merge of upstream/develop clean.
- `./bin/clang-format-fix` (clang-format 21.1.8) twice before the commit: no changes.
