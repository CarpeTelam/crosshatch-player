# Orchestrated epics

An orchestrated epic is one where a single agent, the orchestrator, builds the epic's stories by handing each one to a
build agent working in its own git worktree, several at a time. This file has two parts: the **Orchestrator
procedure**, which the orchestrator follows, and the **Build-agent brief**, which it fills in and gives to every build
agent. Both add to AGENTS.md; they never replace it. The rules come from the epic-script-runtime retrospective
(`_bmad-output/initiative-crosshatch-player-v1/epic-script-runtime/epic-script-runtime-retrospective.md`, AI-1 and
AI-12), and the finding ids below (O1, O5, and so on) point there.

`bmad-build` reads the brief only when its prompt says the build runs for an orchestrator
(`_bmad/custom/bmad-build.toml`); an interactive build keeps every human gate.

## Orchestrator procedure

### Before the first story

- **Lanes.** Take the lanes and their order from the epic file's Notes (epic-script-runtime ran two: CI and scripts,
  2 → 3 → 5 → 17, beside the runtime lane). Stories in one lane touch shared files in order; lanes run in parallel.
  `tickets.py next` shows what is ready.
- **One worktree per lane**, branched from the epic branch. Each needs `git submodule update --init --recursive`.
- **One build lock.** Create `{lock}`, a lock file in your scratchpad `{scratch}` (which also holds each story's
  `{scratch}/<ref>/` scratch files and fresh trees), and give it to every build agent. Every
  `pio run`, `pio check`, `pio project metadata`, `sim.sh setup`/`build`, and host-test CMake configure and build, the
  orchestrator's own included, runs as `flock {lock} <command>`; two builds at once can wipe a build directory
  mid-build or race on the shared `~/.platformio/packages`.
- **The upstream remote.** Worktrees share one git config, so add `upstream` once, fetch `develop`
  (`git fetch --no-tags upstream +refs/heads/develop:refs/remotes/upstream/develop`), and unshallow the clone
  (`git fetch --unshallow`) before any agent runs `scripts/check_upstream_touches.py`.

### Each story

1. **Start the build agent** in its lane's worktree with the brief below, filled in, and the story's ref. Say in the
   prompt that the build runs for an orchestrator.
2. **Answer blocking questions.** A question the repo does not settle goes to the owner (see Owner hand-offs); send the
   answer back to the same agent.
3. **Review every story independently (AI-1).** A build agent cannot start subagents, so its own review runs every
   lens in one context; in epic-script-runtime the three independent reviews found every defect the self-reviews had
   rated low or dismissed (O1). After each build commit, run context-free review subagents over the story's diff
   (`git diff <plan baseline>..<commit>`) with the plan as the intent: `bmad-review`, or bmad-build's thorough lenses
   (blind hunter, edge-case hunter, verification gap, intent alignment). This applies to every story, not only the
   risky ones. Send the findings to the build agent, which triages each one into its plan's Review Triage Log and fixes
   what it accepts in a follow-up commit, so every plan keeps its review record (O2).
4. **Merge only a finished tree.** Never merge into, or rebase, a worktree whose agent is still working; wait for its
   report. `deferred-work.md` merges with `merge=union` (`.gitattributes`), so appends from two lanes combine without a
   conflict (O10); when two branches edited the same existing entry, union keeps both versions, so read the result.
5. **Record out-of-session fixes.** A fix that lands outside its story's session, such as one that changes an earlier
   story's code, records its verification in the plan of the story it changes (O5).
6. **Re-run the host suites on the combined tree before every push** (under the lock), plus
   `python3 scripts/<name>_test.py` for each fork script. In epic-script-runtime a parallel fix (c25a2ff6) broke another
   story's tests, and only this run caught it (O5).
7. **Mark the ticket done and push.** Build agents never run `tickets.py mark` or `pull`; the orchestrator runs
   `uv run _bmad/method/scripts/tickets.py --project-root . mark <ref> done` on the combined tree, commits it, and pushes
   the epic branch to `origin`.
8. **Delete finished trees (O7).** Remove a lane's worktree (`git worktree remove`) once its last story is merged, and
   each `{scratch}/<ref>/` fresh clone or archive tree once its gate has run. A worktree takes about 1.5 GB and a fresh
   clone about 1.8 GB; in epic-script-runtime a full disk half-installed `~/.platformio/packages` mid-build.

### Owner hand-offs

- **Stories marked `hitl = true`** in `tickets.toml` (device runs, a release dry run) stop where their plan says. Give
  the owner the firmware commit, the steps (for game work, the fixtures README), and what to record; put the result in
  the plan's Verification.
- **Decisions.** Give the owner the options, what each means, and a recommendation. Record the answer, dated, in the
  epic file's Notes, and in the architecture spine when it changes a decision there.
- **Measure before quoting (O8).** A memory, flash, or timing figure goes to the owner as a measurement with its
  method, or labelled "unmeasured". In epic-script-runtime about 320 KiB was quoted for Lua's region; the measurement
  was 448 KiB.

### Before the epic PR

- On the combined tree, under the lock: build all five envs (`default`, `x4pro`, `sticky`, `x4c`, `papermono`), run
  `pio check` as AGENTS.md gives it, the host suites, the fork script tests, `sim.sh build x4pro` when `src/games` or a
  screen changed, and `./bin/clang-format-fix` twice with nothing new in `git status`.
- Open one PR for the epic into `develop` with a Conventional Commit title, and never merge it with a red check
  (AGENTS.md, Policy).

## Build-agent brief

Before handing this section to a build agent, replace:

- `{epic-folder}`: the epic's folder, such as `_bmad-output/initiative-crosshatch-player-v1/epic-script-runtime`;
- `{ref}`: the story's ref, such as `2.7`, or the id of work that is not a ticket;
- `{lock}`: the shared lock file in the orchestrator's scratchpad;
- `{scratch}`: the orchestrator's scratchpad directory.

Everything from here to the end of the file is the brief.

### Your task

You are building `{ref}` from `{epic-folder}` for an orchestrator. The orchestrator reviews your work afterwards and runs
an independent review of your commit. Follow AGENTS.md exactly; re-read it in your worktree.

### How to run the build

You work in your own git worktree (your current directory); never touch the main checkout or another agent's worktree.
Invoke the `bmad-build` skill with `{ref}` and follow its workflow; the plan goes where `tickets.py find {ref}` says,
or, for work that is not a ticket, to `_bmad-output/implementation-artifacts/plan-<slug>.md`, with the slug led by
`{ref}`. The orchestrator pre-answers the workflow's human gates, so do not stop at them:

- Multi-goal check: **Keep all goals**; the story is an agreed scope.
- Token count gate: **Keep full plan**, unless it is far over because of padding; then tighten, never split.
- Checkpoint 1: **Approve and continue**.
- Open questions: resolve each from the repo, the architecture spine (`_bmad-output/planning-artifacts/architecture/`),
  the epic file and its Notes, earlier plans in `{epic-folder}`, retrospectives, and git history. Only a choice none of
  those settles and that would change the design goes back to the orchestrator: stop and put it, with options and your
  recommendation, at the top of your final report.
- Review step: if you cannot spawn subagents, do not HALT; run each lens yourself, one at a time, reading each lens
  prompt fresh and judging only the diff, then triage into the plan's Review Triage Log. The orchestrator also runs an
  independent review and sends you its findings; triage those into the same log.
- Commit: exactly one local commit on your worktree's branch (a follow-up commit is fine when the orchestrator sends
  review findings). Do not push, do not open a PR, and never run `tickets.py mark` or `pull`; the orchestrator marks
  the ticket. End the commit message with the attribution lines your session's system gives.
- No model names in commits, code, or docs.

### Environment

- Run `git submodule update --init --recursive` in your worktree before any firmware, simulator, or host-test build.
- **One build at a time across all agents.** Wrap every `pio run`, `pio check`, `pio project metadata`, `sim.sh setup`,
  `sim.sh build`, and host-test CMake configure and build in the shared lock: `flock {lock} <command>`. Hold it for the
  whole command. Firmware builds take minutes: use a long timeout, or run in the background and wait.
- Host tests build in your worktree's `build/test`, with the commands in AGENTS.md.
- Scratch files, logs, and fresh trees go under `{scratch}/{ref}/`; delete fresh trees when you are done (disk is
  limited).

### Rules

- Verify with local evidence (host tests, builds, simulator screenshots); CI runs later on the epic PR. Record the
  evidence in the plan's Verification.
- A story that adds or changes a CI gate or workflow runs that gate once from a fresh tree of your committed work
  before it counts as built: `git clone <worktree> {scratch}/{ref}/fresh` and check out your commit there, or, when
  cloning is blocked and the gate reads no git history, `git archive <commit> | tar -x -C {scratch}/{ref}/fresh` plus
  each submodule's archive at the commit's gitlink
  (`git -C freeink-sdk archive --prefix=freeink-sdk/ $(git rev-parse <commit>:freeink-sdk) | tar -x -C {scratch}/{ref}/fresh`).
  Run the workflow step's commands
  there, and say in the plan which kind of tree it was. A new fork job goes in `Crosshatch Test Status`'s `needs` in
  `.github/workflows/crosshatch-ci.yml`; never edit `ci.yml`.
- Game fixtures live in `test/game_script/fixtures/`, never `games/`.
- Upstream files change only as `docs/crosshatch/upstream-touches.md` allows; run
  `python3 scripts/check_upstream_touches.py` before committing when you touched a non-fork file.
- New fork scripts follow `docs/crosshatch/fork-scripts.md` (sidecar test, exit contract, `fork_common.py`, listed in
  the ledger's Game paths).
- Deferred items: append to `_bmad-output/implementation-artifacts/deferred-work.md` only under a heading `## {ref}` at
  the end of the file.
- A memory, flash, or timing figure in your plan or report is a measurement with its method, or says "unmeasured".
- Formatting: run `./bin/clang-format-fix` (no arguments) as the very last step before the commit, after every edit
  (review fixes and plan edits included), then run it a second time and confirm `git status` shows nothing new. Keep
  any formatting-only change it makes outside your paths in your commit (never revert it) and name it in your report.
- For C/C++ changes build `x4pro` and `default` (C3) at least; for `src/games` or screens also `sim.sh build x4pro`.
  The orchestrator builds all five envs before the PR.

### Final report

Under 300 words: blocking questions first, then the commit hash and branch, what changed (by path), the verification
evidence (commands and results), anything deferred, and any risk or unfinished item.
