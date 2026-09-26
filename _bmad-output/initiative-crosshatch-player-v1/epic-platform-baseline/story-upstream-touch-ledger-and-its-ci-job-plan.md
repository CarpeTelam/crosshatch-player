---
title: 'Upstream-touch ledger and its CI job'
type: 'chore'
ticket: '2'
created: '2026-09-26'
status: 'built'
baseline_revision: '234d1c084514a39880ed67d65f1ad0e1256e97b7'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 1
context:
  - '{project-root}/_bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Nothing enforces AD-3's cap on upstream changes: a PR can edit any upstream file, move the `freeink-sdk` pointer, or add a fork-only file upstream also has, and the damage only shows at the next upstream merge.

**Approach:** Write the ledger (`docs/crosshatch/upstream-touches.md`: the 10 AD-3 rows, the baseline allowlist, the fork-only game paths) as the single source of truth, a stdlib Python script that reads it from the checked ref and enforces it, and a new fork-only workflow that runs the script with full history on every PR to `develop`.

## Boundaries & Constraints

**Always:**
- The script reads all three lists from `docs/crosshatch/upstream-touches.md` at the checked ref (`git show <ref>:…`); no path list is duplicated in code or workflow. Missing file or an empty section is a setup error (exit 2), never a silent pass.
- Changed paths: `git diff --no-renames --name-only -z merge-base(ref, upstream) ref`, filtered to paths in `git ls-tree -r upstream`. Each must be a ledger row or allowlist entry (exact path match). Exit 1 on any miss.
- `freeink-sdk` gitlink at ref must equal the one at the merge-base, checked on its own and regardless of the lists.
- Trial merge only via `git merge-tree --write-tree` (no worktree, no ref or index change). Conflicts in a game path fail; other conflicts are printed as information only (ledgered files may legitimately conflict and are resolved at merge time).
- Game paths match as a glob on the path or any leading directory of it (`lib/Game*`, `lib/lua` covers `lib/lua/x.c`).
- Fork-only workflows are named `.github/workflows/crosshatch-*.yml`; the doc states this so later workflows are covered without script edits. The script's own path is added to the game-path list.
- Shallow repo, missing upstream ref, or git older than 2.38 → exit 2 with a hint.
- No planning references (AD numbers, epic, story) in the script or workflow; the doc may cite AD-3.

**Never:**
- Editing `ci.yml`, any upstream file, the `freeink-sdk` pointer, or `.skills/`.
- Making the job required (owner step) or touching any remote.
- Non-stdlib Python dependencies or `actions/setup-python`.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Real branch | HEAD (rows 1, 3 + allowlist + fork-only files) | exit 0; lists the upstream paths it accepted | — |
| Unledgered edit | branch editing e.g. `README.md` | exit 1 naming `README.md` | — |
| Pointer move | branch changing the `freeink-sdk` gitlink | exit 1 naming old and new commit | — |
| Ledgered/fork-only only | branch editing `src/network/OtaUpdater.cpp` and adding `src/games/X.cpp` | exit 0 | — |
| Game-path conflict | ref and upstream both add different `scripts/pack_game.py` | exit 1 naming the path | — |
| Setup | shallow clone / no `upstream/develop` / no ledger | exit 2 with hint | — |

</frozen-after-approval>

## Code Map

- `docs/crosshatch/` -- does not exist yet; this story creates it with `upstream-touches.md`.
- AD-3 in the spine -- the 10-row table and allowlist to copy verbatim.
- Current HEAD vs `upstream/develop`: upstream-present changes are exactly `CLAUDE.md` (deleted), `.github/PULL_REQUEST_TEMPLATE.md`, `.gitignore`, `AGENTS.md`, `platformio.ini`, `test/CMakeLists.txt`; `freeink-sdk` unchanged (`111fdcc7`); `git merge-tree --write-tree HEAD upstream/develop` is clean today.
- `.github/workflows/ci.yml` -- style reference only (`actions/checkout@v6`, `permissions: contents: read`); not edited.
- `.gitattributes` -- `AGENTS.md merge=ours`; the workflow sets `merge.ours.driver true` so the trial merge mirrors the documented upstream merge.
- `scripts/git_branch.py` -- upstream Python style reference (module docstring, snake_case, `subprocess`).
- Local git 2.43, python 3.11; GitHub `ubuntu-latest` has newer of both.

## Tasks & Acceptance

**Execution:**
- [x] `docs/crosshatch/upstream-touches.md` -- purpose, the rule, sections `## Ledger` (AD-3 table verbatim), `## Allowlist` (5 entries, each with one-line reason), `## Game paths` (ticket list + `scripts/check_upstream_touches.py` + `.github/workflows/crosshatch-*.yml`), and how to run the check locally (the unshallow step conditional, `merge.ours.driver` set); state that only paths are enforced (Change/Guarded columns are for review) and that entries are shell-style globs whose `*` also matches `/` -- single source of truth.
- [x] `scripts/check_upstream_touches.py` -- stdlib CLI (`--ref`, default `HEAD`; `--upstream`, default `upstream/develop`) implementing the constraints above; exit 0/1/2. Also: fail when `upstream` itself has any path matching a Game path (a clean merge would otherwise pull upstream files into fork-only paths), and leave game paths out of the unledgered list so that failure names the real cause; decode git output with `surrogateescape` so odd paths never raise; warn on Ledger rows missing upstream -- the enforcement.
- [x] `scripts/check_upstream_touches_test.py` -- stdlib `unittest`: `parse_ledger` (first code span per row/bullet, ``` and ~~~ fences skipped, empty section raises), the committed doc parses to 10 Ledger rows and 5 Allowlist entries, `is_game_path` (leading-directory and glob cases, `src/main.cpp` rejected), and one temp-repo integration suite (`git init`, tiny upstream and fork branches, fixture ledger) asserting the exit code of every I/O-matrix row plus an upstream-only game-path file -- a committed regression guard for the gate's failure branches; listed under Game paths.
- [x] `.github/workflows/crosshatch-upstream-ledger.yml` -- `pull_request` to `develop`, `contents: read`, job `upstream-ledger` named `Upstream touch ledger`, checkout `fetch-depth: 0`, add `upstream` remote, fetch `develop` with full history and print elapsed seconds, set `merge.ours.driver`, run the unit test file, then the script -- the CI job the owner marks required.

**Acceptance Criteria:**
- Given the throwaway refs of the I/O matrix built in the scratchpad (never on the working branch), when the script runs with `--ref` on each, then exit codes and messages match the matrix.
- Given the real branch HEAD, when the script runs, then it exits 0.
- Given the workflow file, when parsed with `yaml.safe_load` and its steps are replayed in a fresh full clone, then it parses and the steps succeed; the observed upstream fetch time is recorded.
- Given the tree, when `./bin/clang-format-fix` runs, then `git diff` is empty.

## Implementation Notes

- Implemented directly (no subagent tool in this session). Files: `docs/crosshatch/upstream-touches.md`, `scripts/check_upstream_touches.py`, `scripts/check_upstream_touches_test.py`, `.github/workflows/crosshatch-upstream-ledger.yml`.
- Scratchpad harness (throwaway branches `tmp-ledger/*` in a throwaway worktree, both removed afterwards): real branch + ledger files → 0; `README.md` edit → 1; `freeink-sdk` gitlink moved to its parent commit → 1; edits to `OtaUpdater.cpp`, `english.yaml`, new `src/games/*.cpp` and `crosshatch-*.yml` → 0; fake upstream adding `scripts/pack_game.py` against a fork adding it → 1 (upstream game path + game-path conflict); shallow clone → 2.
- `scripts/check_upstream_touches_test.py`: 18 tests pass (15 before pass-2 patches); each of five mutations (unledgered list emptied, leading-directory match removed, merge-tree slice off by one, SDK check disabled, upstream game-path check disabled) makes at least one test fail.
- Ticket `unknown` (upstream fetch time): a fresh full clone of the fork from GitHub took 4.1 s (98 MiB pack); `git fetch --no-tags upstream develop` on top of it took 1.0 s, since the fork already holds almost all of upstream's history. The workflow prints its own fetch time for the first CI run.
- Today no `upstream/develop` path matches a Game path, and every Ledger row exists upstream (no warnings).
- Pass-1 patch attempt on stale Allowlist warnings was dropped: `.gitattributes` is fork-added, so it warned on every run (triage row 6).

## Plan Change Log

- Loop 1 (review pass 1). Trigger: verification-gap finding that the gate's failure branches are verified only by a throwaway scratchpad harness, so a regression that makes the script always pass would ship unnoticed (every compliant PR still exits 0). Amended: Tasks add `scripts/check_upstream_touches_test.py` (unit + temp-repo integration) run by the workflow, and fold in the pass-1 patch findings (upstream-only game-path check, game paths out of the unledgered list, `surrogateescape`, both fence styles, doc wording on path-only enforcement and glob semantics); Verification runs the test file. Known-bad state avoided: an enforcement gate with no committed test of its failure paths. KEEP: the ledger doc's structure and verbatim AD-3 table, reading the lists from the checked ref, `git merge-tree --write-tree` trial merge with non-game conflicts as notes, the separate `freeink-sdk` gitlink comparison, the exit 0/1/2 contract and setup-error messages, the workflow's trigger, permissions, job name, `fetch-depth: 0`, timed upstream fetch, and `merge.ours.driver` step.

## Review Triage Log

Pass 1 (lenses: blind-hunter, edge-case-hunter, verification-gap, intent-alignment). Counts: high 0, medium 1, low 8, false 1, maybe-false 0, rejected-by-intent 7. Route: one bad_plan (loopback 1); patches folded into the re-derivation; 1 deferred.

| # | Lens | Finding | Verdict | Route | Evidence |
|---|------|---------|---------|-------|----------|
| 1 | gap | No committed test of the script's failure branches; CI only ever exercises the pass path | medium | bad_plan | Repo search finds no test of `parse_ledger`/`is_game_path`/exit codes; plan chose a scratchpad harness. Loopback 1 adds `scripts/check_upstream_touches_test.py` run by the workflow. |
| 2 | blind, edge | Upstream adding a non-conflicting file under a game path passes, though the doc says game paths never exist upstream | low | patch | Verified: merge-tree is clean for a pure upstream add. Fix is one set comprehension over `upstream_paths`; today no upstream path matches (checked with `ls-tree`). |
| 3 | edge | Fork game-path file that upstream later adds is reported as "unledgered" (misleading) | low | patch | True with rule 1 alone; excluding game paths from the unledgered list lets #2's message name the cause. |
| 4 | blind, edge | Non-UTF-8 path raises `UnicodeDecodeError` → traceback, exit 1 not 2 | low | patch | `nul_list` used strict decode; `surrogateescape` is a direct correction. |
| 5 | edge | `~~~` fences not skipped by `parse_ledger` | low | patch | Direct correction (accept both markers). |
| 6 | blind | Stale Allowlist entries never warned about | false | reject | Tried the patch: `.gitattributes` is fork-added and absent upstream by design, so the warning fired on every run. An allowlisted path that reappears upstream is meant to stay exempt. |
| 7 | blind, intent | Change/Guarded columns not enforced; doc does not say so | low | patch | Doc sentence: paths enforced, columns reviewed. Enforcing columns is outside the intent (AD-3 defines the job by path). |
| 8 | blind | Glob semantics looser than "`*` is a wildcard" (`*` crosses `/`, `?`/`[]`) | low | patch | Doc wording fix. |
| 9 | blind | Local instructions: `--unshallow` errors on a full clone; merge driver missing | low | patch | Both fixed in the doc before triage completed. |
| 10 | blind, intent | A PR can add its own ledger row and pass | low | reject | Frozen intent reads the ledger from the checked ref; the row is a reviewed doc change (Design Notes). Owner may add CODEOWNERS; listed in the final report. |
| 11 | blind | Pushes to `develop` and stacked PRs are not checked | low | reject | Intent: "every PR to `develop`"; AGENTS.md routes all work, upstream syncs included, through PRs into `develop`. |
| 12 | blind | `AGENTS.md` does not point at the ledger | low | defer | Agent-context file edit. |
| 13 | edge | Emptied Allowlist would exit 2 | low | reject | The five baseline entries are permanent fork files; error is explicit; unlikely. |
| 14 | gap-other | Fork edit to a file upstream deleted since the merge-base is not flagged | low | reject | Intent/AD-3 define the rule over paths present in `upstream/develop`; the trial merge prints the modify/delete conflict as a note. |
| 15 | intent | Measures whole-fork drift, not the PR's own changes | low | reject | AD-3 and the ticket specify merge-base(HEAD, upstream/develop) to HEAD. |
| 16 | intent | Row count (10) not enforced | low | reject | Fixed count would force script edits; AD-3 guards additions by spine review. |
| 17 | intent | "Game paths" includes non-game fork infrastructure | low | reject | Ticket's own list includes `docs/crosshatch` and the workflow files; heading follows the ticket. |
| 18 | intent, blind | Script cannot check uncommitted work / was not run on its own commit | low | reject | Reading the checked ref is intended; the committed HEAD run is in Verification. |

Pass 2 (same lenses, after loopback 1). Counts: high 0, medium 0, low 13, false 1, maybe-false 0. Patches 6, deferred 0 new (row 12 carried).

| # | Lens | Finding | Verdict | Route | Evidence |
|---|------|---------|---------|-------|----------|
| 1 | blind, intent | A PR can add its own ledger row or weaken the check | low | reject | carried: pass 1 row 10. |
| 2 | blind | `AGENTS.md` does not mention the ledger | low | defer | carried: pass 1 row 12 (already in deferred-work). |
| 3 | blind | No `push`/`schedule` trigger for syncs and direct pushes | low | reject | carried: pass 1 row 11. |
| 4 | blind | Fork edits to files upstream deleted since the merge-base go uncounted | low | reject | carried: pass 1 row 14; now covered by a test asserting the warning and exit 0. |
| 5 | blind | `merge-tree` reads `.gitattributes` from the index, not `--ref`, so the `merge=ours` claim is untested | low | reject | In CI the checked ref is the checkout; locally the only effect is an `AGENTS.md` conflict note, which never fails (not a game path). |
| 6 | blind, gap | Untested: deletion/rename handling, ledger-row warning | low | patch | Added `fork-rm`, `fork-rename`, `up-rm-ini` fixtures and asserts, plus "no warning" on the pass case. |
| 7 | blind | Broad globs (`lib/Game*`) plus the upstream-overlap failure could block every PR on an unrelated upstream name | low | patch | Real but loud and fixable in a doc edit; failure message now says to narrow the entry when the match is only a shared prefix. The pattern list is the ticket's. |
| 8 | blind | Ledgered rows not currently changed are not flagged stale | false | reject | Rows 2 and 4 to 10 are reserved for later work by design; warning on them would be permanent noise. |
| 9 | blind | Unpinned action tag, no fetch retry, no step summary | low | reject | Matches `ci.yml`'s `actions/checkout@v6`; retries and summaries add surface for rare cases. |
| 10 | edge | Printing a surrogate-escaped path raises `UnicodeEncodeError` | low | patch | Reproduced by reasoning; `sys.stdout.reconfigure(errors='backslashreplace')` in `main()`; manual run with a `\xff` filename prints `bad\udcff.txt`, exit 1. |
| 11 | edge | Non-UTF-8 ledger bytes raise on decode | low | patch | Ledger decoded with `errors='replace'`. |
| 12 | edge | Trailing slash in a Game path entry never matches | low | patch | Entries stripped of a trailing `/`; tested. |
| 13 | edge | `*`/`+` bullets silently dropped | low | patch | `parse_ledger` accepts `- `, `* `, `+ `; tested. |
| 14 | edge | Emptied Allowlist exits 2 | low | reject | carried: pass 1 row 13. |

## Design Notes

Reading the ledger from the checked ref (not the working tree) keeps CI and local runs identical and lets verification use plain throwaway branches. A PR that adds a ledger row passes the job by design; the row is then a reviewed doc change, and AD-3 requires a spine update first.

The game-path list is also where the `crosshatch-*.yml` naming convention lives, so the flash-budget and release workflows are covered as soon as they are named that way. Ledger rows whose path is missing from upstream are printed as warnings, not failures, so an upstream rename surfaces without blocking unrelated PRs.

Decision (ticket `unknown`): measure a fresh full clone of the fork plus `git fetch upstream develop` locally, record it, and have the workflow print its own fetch time for the first CI run.

## Verification

**Commands:**
- scratchpad harness: create throwaway branches for the matrix rows, run `python3 scripts/check_upstream_touches.py --ref <branch>`, record exit codes, delete the branches -- expected: 1, 1, 0, 1 (conflict), and 0 for HEAD.
- `python3 scripts/check_upstream_touches_test.py -v` -- expected: all tests pass.
- `python3 scripts/check_upstream_touches.py` (after the commit) -- expected: exit 0.
- `python3 -c 'import yaml,sys; yaml.safe_load(open(sys.argv[1]))' .github/workflows/crosshatch-upstream-ledger.yml` -- expected: no error.
- fresh `git clone` of the local repo in the scratchpad, replay the workflow's fetch and script steps, `time` the upstream fetch -- expected: exit 0.
- `./bin/clang-format-fix && git diff --exit-code` -- expected: clean.
