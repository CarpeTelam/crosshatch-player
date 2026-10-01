---
title: 'Orchestration follow-up: lanes, warm worktrees, a shared build cache, and binding verification order'
type: 'chore'
ticket: ''
created: '2026-10-01'
status: 'built'
route: 'oneshot'
route_source: 'auto'
baseline_revision: 'c7ff55b6c102e5844d18892a1c82b8ca1ba4cc46'
review: 'quick'
review_source: 'auto'
lenses_ran: ['quick']
review_loop_iteration: 0
context: []
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Measurements from the dev-environment session (`plan-dev-env-cold-start.md`) left the orchestration doc behind them:
- Lanes share one build lock, so builds do not parallelize: two at once on 4 cores saved about 4%.
- A new lane worktree pays cold builds: `build_cache_dir = .cache` is per checkout.
- Fresh-tree gate runs have no rule against rebuilding everything.
- The build-agent brief repeats AGENTS.md.
- Three entries are deferred under `## dev-env-cold-start`:
  - the lock `pio run -t unit-tests` takes;
  - the warm-log paths and a fallback when setup fails;
  - review-before-firmware is prose only in a plain `/bmad-build`.

**Approach:**
- In `docs/crosshatch/orchestrated-epics.md`:
  - lanes parallelize work, not builds, and two is the default;
  - every lane worktree runs `scripts/dev_setup.py --warm` and builds with `PLATFORMIO_BUILD_CACHE_DIR` at the main checkout's `.cache`, adopted on the measurement below;
  - fresh-tree gates reuse `~/.platformio` and run only the gate's commands;
  - the brief points to AGENTS.md instead of restating it.
- Close the three deferrals: AGENTS.md lines, plus a `_bmad/custom/bmad-build.toml` override. Under it the implementer runs only host tests and fast checks, and the firmware checks run after review, before the commit.

</frozen-after-approval>

## Implementation Notes

Oneshot: about 90 lines across `docs/crosshatch/orchestrated-epics.md`, `AGENTS.md`, `_bmad/custom/bmad-build.toml` (fork-only, not in upstream), and `deferred-work.md` (marking the three entries resolved).

Shared-cache measurement, 2026-10-01 at c7ff55b6:
- **x4pro:** a fresh worktree with its library deps preinstalled (`pio pkg install -e x4pro -e default`, untimed) built `x4pro` in 148.9 s with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`, reusing 222 objects. The fresh-worktree cold build without it was 240.7 s (worktree `wt-ser`, the same day, with its library deps preinstalled the same way).
- **default:** 163.7 s with the shared cache, but only 2 objects were reused, and the C3 framework came from its cache ("Restored cached Arduino framework"). The earlier 369.5 s fresh-tree `default` had rebuilt that framework once, and that did not recur, so no `default` gain is claimed.
- **Parallel lanes:** fresh worktrees with preinstalled deps, 2026-10-01.
  - Serial in one tree: `x4pro` 240.7 s, then `default` 369.5 s, which rebuilt the C3 framework (2,395 compile lines); 611 s in all.
  - Parallel in two trees: `x4pro` 384.2 s beside `default` 314.0 s, which restored the cached framework (596 compile lines); 384 s wall.
  - The serial-equivalent of about 400 s (240.7 s plus the cached `default`'s 160.4 s) is an estimate, not a run.

User request during review: the handoff to each build agent starts with the slash command `/bmad-build {ref}`, so the agent runs the whole workflow. Orchestrator step 1 and the brief's "How to run the build" say so, and add that the brief only pre-answers gates and adds fork rules, never replacing a workflow step. The brief's trims removed only restatements of AGENTS.md; every workflow step and gate answer is still there.

## Review Triage Log

Pass 1 (quick lens, context-free subagent): 3 medium, 4 low, all patched.
- medium, patch: `--warm` skips a held lock, so lanes set up together got no warm build. Lanes are now set up one at a time, waiting for each warm `x4pro` build, and a skipped lane builds cold.
- medium, patch: the persistent fact could repeat the firmware round after step 4's patch re-run. Firmware now runs once, and step 4's re-run counts.
- medium, patch: the fact did not rerun `./bin/clang-format-fix` after review patches. Added.
- low, patch: the cache figures dropped their method, and a real lane worktree has no library deps installed (the install stamp is machine-wide). The method is stated, and lane setup installs the deps.
- low, patch: "384 s against about 400 s" mixed a run with an estimate. Both the measured runs and the estimate are now stated as such.
- low, patch: "every `pio` command" missed `sim.sh` and the flash-budget script. Now export it in every shell that builds.
- low, patch: the brief's C/C++ bullet still restated AGENTS.md. It now points to AGENTS.md, keeping only the simulator rule.
- low, patch: "The upstream remote" lost its manual fallback. It now says to confirm the git step, or follow the docstring.

## Verification

- Every `scripts/*_test.py` passes; `scripts/check_upstream_touches.py` PASS; `./bin/clang-format-fix` twice, nothing new.
- `_bmad/scripts/render_skill.py` renders bmad-build with both persistent facts in `workflow.md` and the new handoff in `step-03-implement.md`.
- The docs-only change runs no firmware checks.
