---
title: 'Give the orchestration rules a permanent home (retro AI-12, AI-1)'
type: 'chore'
ticket: ''
created: '2026-09-28'
status: 'built'
baseline_revision: '69c04796a1248bd05e494b0de02c93996be657fb'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/_bmad-output/initiative-crosshatch-player-v1/epic-script-runtime/epic-script-runtime-retrospective.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** The rules every epic-2 build agent followed lived only in a session scratchpad file, and the orchestration lessons (O5–O8, O10–O12) and the every-story independent review (AI-1) are written nowhere the next epic reads. One deferred item (Home over the light panel) is still open although the code refutes it.

**Approach:** Split the rules by reader, as the retro's "AI-12, decided placement" says: repo-wide facts as short lines in AGENTS.md's existing sections; a new `docs/crosshatch/orchestrated-epics.md` with the Orchestrator procedure and a generalized Build-agent brief; a one-entry `_bmad/custom/bmad-build.toml` pointer; `merge=union` for `deferred-work.md`; and close the light-panel deferred entry with the R10 side effect.

## Boundaries & Constraints

**Always:** AGENTS.md stays terse and keeps its structure (amend existing lines where one already covers the topic: submodule init, pioarduino, certifi, format, fresh-clone, ledger check). The brief uses the placeholders `{epic-folder}`, `{ref}`, `{lock}`, `{scratch}`, says "the orchestrator", "a story that adds or changes a CI gate or workflow", and "end the commit with the attribution lines your session's system gives". It keeps the gate pre-answers, the open-question rule, one local commit/no push/no `tickets.py mark` or `pull`, and the 300-word report with blocking questions first. The toml pointer applies only when the prompt says the build runs for an orchestrator. `.gitattributes` keeps `AGENTS.md merge=ours`.

**Never:** No gate answers in the customization itself (it applies to interactive builds too). No edits to `.claude/skills/`, `.skills/`, `docs/contributing/`, `ci.yml`, or any C/C++. No session ids, scratchpad paths, or model names in the new docs. Do not rewrite other deferred entries.

</frozen-after-approval>

## Code Map

- `AGENTS.md` -- inside the `bmad:context` block; direct edits have precedent (a2d93cf8). Lines to amend: 11 (ledger check → needs `upstream` remote with `develop` fetched, unshallowed), 28 (submodule init → also every git worktree), 31 (pioarduino → from PyPI, proxy 403s GitHub archive URLs; installed here as `uv tool install pioarduino==6.1.19`), 32 (certifi → also the bundle of the env running `pio`, e.g. `/root/.local/share/uv/tools/pioarduino/bin/python`), 35 (format → last step after every edit, twice, keep and name out-of-path changes), 38 (fresh clone → archive tree accepted for a gate that reads no git history). New lines: one-build lock with `flock <lock-file>` (Running and verifying), delete finished worktrees/scratch clones (Running and verifying), fixtures in `test/game_script/fixtures/` never `games/` (Where things are; `fork_release.py pack-games` packs every `games/<id>/`), link to the new doc (Where things are).
- `docs/crosshatch/orchestrated-epics.md` -- new; style of `docs/crosshatch/fork-scripts.md` (short intro, `##` sections, bullets). Source for the brief: the session's `build-brief.md` (its content, generalized) and the retro's AI-12 placement paragraph; lanes example: epic-script-runtime Notes line 72; `tickets.py mark <ref> done` (repo store).
- `_bmad/custom/bmad-build.toml` -- new. `.claude/skills/bmad-build/customize.toml` merge rules: lists append, so `[workflow] persistent_facts = ["..."]` adds one literal fact; rendered into the workflow's "Load Persistent Facts" step by `_bmad/scripts/render_skill.py`.
- `.gitattributes` -- one line today (`AGENTS.md merge=ours`); allowlisted in `docs/crosshatch/upstream-touches.md`, whose `.gitattributes` bullet names only the `merge=ours` rule (update its wording to name the union rule too).
- `_bmad-output/implementation-artifacts/deferred-work.md:101-103` -- the light-panel entry. Refuting code: `src/activities/ActivityManager.cpp:108-112` (Home → `currentActivity->handleHomeGesture()`), `src/activities/util/FrontlightPanelActivity.cpp:221-224` (close, return true); the panel is pushed over the match (`ActivityManager.cpp:197-206`), so only the panel's `loop()` runs; `GameMatchActivity::loopPlaying` (`GameMatchActivity.cpp:282-310`) holds the 3 s watchdog (`vmHealthy`, :259), `pollTimer` (:302), `flushIfDue` (:303). Closure format: see lines 64-66 ("Resolved by …, closed by …. It read: …").

## Tasks & Acceptance

**Execution:**
- [ ] `AGENTS.md` -- amend and add the lines in the Code Map -- repo-wide facts every agent needs (retro AI-12 item 1).
- [ ] `docs/crosshatch/orchestrated-epics.md` -- write `## Orchestrator procedure` (lanes and worktrees, `{lock}`, hold merges, context-free review for every story and triage into the plan's Review Triage Log, combined-tree host suites before every push, cleanup after merge, measure before quoting, owner hand-offs, mark done and push, five-env build before the epic PR) and `## Build-agent brief` (generalized brief) -- AI-12 item 2, AI-1, O5, O7, O8.
- [ ] `_bmad/custom/bmad-build.toml` -- one `persistent_facts` entry -- AI-12 item 3.
- [ ] `.gitattributes`, `docs/crosshatch/upstream-touches.md` -- add the union line; name it in the allowlist bullet -- O10.
- [ ] `_bmad-output/implementation-artifacts/deferred-work.md` -- close the light-panel entry, record R10 -- retro open question "Deferred item (b)".

**Acceptance Criteria:**
- Given a bmad-build render, when `render_skill.py` runs for bmad-build, then the rendered `workflow.md`'s persistent facts contain the pointer to `docs/crosshatch/orchestrated-epics.md`.
- Given a merge of two branches that each append to `deferred-work.md`, when git merges them, then both appends are kept without a conflict.
- Given the change, when `python3 scripts/check_upstream_touches.py` runs, then it passes.
- Given the brief, when an orchestrator fills its placeholders, then it contains no session id, scratch path, story number, or model name.

## Implementation Notes

- Gates pre-answered by the orchestrator: keep all goals; keep the full plan (about 1,600 tokens, no padding); checkpoint 1 approved. No subagent tool in this session, so implementation ran directly from the plan.

## Plan Change Log

- Change 1 (pass 2, finding 6): the orchestrator directed one edit the frozen **Never** excluded, `.claude/skills/run-crosshatch-player/SKILL.md` (install pioarduino from PyPI), so AGENTS.md and the simulator skill agree. It is a fork-added skill, not `.skills/`. The frozen block is left as the human wrote it; every other exclusion stands. Avoids: two conflicting install instructions. KEEP: the placement split (AGENTS.md facts, the doc, the one-entry toml).

## Review Triage Log

**Pass 1** (self-review: no subagent tool in this session, so each thorough lens ran in turn over the 24.8 kB diff of every changed path but this plan; the orchestrator's independent review follows). Verdicts: high 0, medium 0, low 5, false 2, maybe-false 0.

| # | Lens | Finding | Verdict | Route / evidence |
|---|------|---------|---------|------------------|
| 1 | blind | AGENTS.md certifi amendment named the uv tool's `python`, not the bundle it asks for | low | patch: now names `~/.local/share/uv/tools/pioarduino/lib/python3.11/site-packages/certifi/cacert.pem`, which that interpreter's `certifi.where()` printed here |
| 2 | blind | pioarduino line read as if the proxy 403s `pip install platformio` too | low | patch: the 403 clause now names only GitHub archive URLs |
| 3 | blind | the Orchestrator procedure used `{scratch}` before the brief defines it | low | patch: defined with `{lock}` in "Before the first story" |
| 4 | blind | intro's "O1–O11" range overstated the ids the doc cites | low | patch: "O1, O5, and so on" |
| 5 | edge | brief's submodule archive took the submodule's checked-out `HEAD`, not the gitlink of the commit under test; with an uninitialized submodule `git -C freeink-sdk` falls through to the superproject | low | patch: archives `$(git rev-parse <commit>:freeink-sdk)`, which also fails loudly (unknown object) when the submodule is not initialized |
| 6 | edge | `tickets.py mark <ref> done` could bind `<ref>` to the optional `dir` positional | false | argparse matches `[dir] ref status` as `A?AA`; with two arguments `dir` takes none (`tickets.py:950-965`) |
| 7 | intent | diff also edits `docs/crosshatch/upstream-touches.md`, which the task list does not name | false | the allowlist bullet described `.gitattributes` as holding only the `merge=ours` rule; without the edit the ledger text is wrong. One clause, fork-owned file |

Not a finding, recorded for the orchestrator: the simulator skill (`.claude/skills/run-crosshatch-player/SKILL.md:28-32`) still recommends a git clone of platformio-core; AGENTS.md now says PyPI, which the retro's session confirmed works. The plan forbids skill edits; both routes install 6.1.19.

**Pass 2** (the orchestrator's independent, context-free review of c463b238; its calls in brackets were followed). Verdicts: high 0, medium 2, low 9, false 0, maybe-false 0. All patched in the follow-up commit.

| # | Finding | Verdict | Route / evidence |
|---|---------|---------|------------------|
| 1 | brief's "no model names in commits" contradicts the attribution trailer, which names one | medium | patch: "No model names in code or docs; the session's attribution lines are the only exception" |
| 2 | AGENTS.md pointer lacked the orchestrator condition, so any build agent (or bmad-build's implementation subagent) could self-apply the gate answers | medium | patch: pointer now reads "a build agent whose prompt says it runs for an orchestrator"; the brief says the implementation subagent only implements the plan and never invokes bmad-build |
| 3 | archive recipe did not recurse; `freeink-sdk/.gitmodules` has `libs/assets/Icons/lucide` | low | patch: `mkdir -p`, `git archive`, then `git submodule foreach --recursive 'git archive --prefix="$displaypath/" HEAD \| tar -x -C …'`; AGENTS.md says nested ones included and points to the recipe |
| 4 | `flock <lock> a && b` locks only `a` | low | patch: `flock <lock> sh -c '<commands>'` in AGENTS.md and both places in the doc |
| 5 | unconditional `git fetch --unshallow` fails on a complete clone | low | patch: conditioned on `git rev-parse --is-shallow-repository` printing `true` (doc and AGENTS.md Policy line) |
| 6 | pioarduino line's "(see ci.yml)" read as endorsing CI's GitHub-archive install | low | patch: ci.yml is cited for the pin and the reason only; the simulator skill's Prerequisites and Troubleshooting row now say `uv tool install pioarduino==6.1.19` from PyPI [orchestrator call; fork-added skill] |
| 7 | brief pre-answered only three stops | low | patch: any other HALT (version-control check, intent_gap loopback, review loop limit) becomes a blocking question at the top of the report |
| 8 | `## {ref}` heading rule gave no reason or entry format | low | patch: entries keep the file's `source_plan`/`summary`/`evidence` format; the distinct heading keeps union merges from interleaving |
| 9 | AGENTS.md lock line duplicated the Known-pitfalls line about `sim.sh` during a build | low | patch: folded into that pitfall line, keeping the checksum reason and the `pio project metadata` sentence |
| 10 | "keep any out-of-path formatting change" could keep a change to an unledgered upstream file | low | patch: scoped to fork files; an unledgered upstream file stops and is reported (AGENTS.md and brief) |
| 11 | managed-block provenance line stale after hand edits | low | patch: "hand-edited 2026-09-28 for the epic-script-runtime retro's AI-12", block format kept |

## Verification

**Commands:**
- `uv run --no-cache _bmad/scripts/render_skill.py --project-root <worktree> --skill <worktree>/.claude/skills/bmad-build` -- rendered `…/b21901ac4e355d270391/workflow.md`; `grep -n orchestrated-epics` hit line 49, the only entry under "Step 2: Load Persistent Facts" (before the change that step read `_None._`). The customization merged by the documented rule (lists append).
- Scratch repo with this `.gitattributes`: lanes `a` and `b` each appended a `## <id>` entry to `deferred-work.md`; `git merge lane-a` into `lane-b` printed "Auto-merging" with no conflict, and both entries were present; `git check-attr merge` gave `union` for the file and `ours` for `AGENTS.md`.
- `git remote add upstream …` and `git fetch --no-tags upstream +refs/heads/develop:refs/remotes/upstream/develop` (the clone is not shallow), then `python3 scripts/check_upstream_touches.py` on the commit -- `Result: PASS`; `AGENTS.md` is reported as allowlisted, and `.gitattributes`, `_bmad/`, and `docs/crosshatch/` are not upstream paths.
- `./bin/clang-format-fix` twice, with clang-format 21.1.8 from PyPI in a scratch venv first on `PATH` (the system has 18, which the wrapper refuses) -- exit 0 and no `git status` change either time (no C/C++ in the diff).
- Pass 2: the recursive archive recipe ran on throwaway repos (top → `freeink-sdk` → nested `libs/icons`) and extracted every level's files, the nested submodule's included. `check_upstream_touches.py` and the formatter (twice) were re-run on the follow-up commit.
- No firmware or host-test build: the change has no C/C++ or build input.
