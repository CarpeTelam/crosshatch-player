---
title: 'e6pre-5 AI-6: game-packages concurrency group and AGENTS.md lines'
type: 'chore'
ticket: ''
created: '2026-10-04'
status: 'done'
route: 'oneshot'
route_source: 'auto'
review: 'quick'
review_source: 'auto'
lenses_ran: [quick]
review_loop_iteration: 0
baseline_revision: '1a094c94f4ccad7f35f80515ddfc02e63fc20a17'
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** `crosshatch-game-packages.yml` has a workflow-level concurrency group with `cancel-in-progress`, which applies even when the job's `if` skips the run, so another label's `labeled` event cancels a running `package-games` pack (epic 4 retro rev-12). AGENTS.md also lacks the rule against adding event types to `crosshatch-ci.yml` (proc-6) and omits `test/game_script/GameIconsRaw.h` from the generated files (agg-7).

**Approach:** Give a `labeled` event for any other label a group of its own (run id), so only a push or the `package-games` label can cancel a run; keep the workflow out of Crosshatch Test Status's needs. Add the two AGENTS.md lines (AI-6).

</frozen-after-approval>

## Implementation Notes

Oneshot: three small text edits. Chose a keyed group over job-level `concurrency` because whether a skipped job enters its concurrency group is not documented; a unique group cannot cancel anything whichever way it behaves. Commit 9180a698 exists (`ci: run the Game packages job from its own workflow`). `GameIconsRaw.h` is regenerated with `--raw-out` and compared at `crosshatch-ci.yml:343-366` (the retro's :286-305 had drifted). No `deferred-work.md` entry for AI-6/rev-12 existed, so `## e6pre-5` records the PR-run verification.

## Verification

**Commands:**
- YAML parse of the workflow -- ok (actionlint not installed).
- `./bin/clang-format-fix`, `python3 scripts/check_upstream_touches.py` -- clang-format-fix exit 0 (rerun: git status shows only the four intended paths); check_upstream_touches PASS.

**Manual checks:**
- Expression: `labeled` + other label gives `...-<pr>-other-<run_id>` (unique); `labeled` + `package-games`, `synchronize`, `opened`, `reopened` give `...-<pr>`. A real label-event run needs the epic PR (deferred).
- A \`synchronize\`/\`opened\`/\`reopened\` run on an unlabeled PR still shares the per-PR group and may cancel a running pack; intended (a newer commit supersedes).

## Review Triage Log

Quick lens ran as a context-free subagent and returned. Verdict counts: 0 high, 0 medium, 3 low, 2 false.
- low: AGENTS.md managed-block header not updated -- patched (header now names AI-6).
- low: deferred-work evidence held process narration -- patched.
- low: Verification lacked check results -- patched.
- false: expression correctness, commit hash, ledger line refs -- reviewer confirmed correct.
- false: unlabeled synchronize case -- intended, noted in Manual checks.
