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
  `tickets.py next` shows what is ready. Lanes overlap planning, implementation, and review, not builds: every
  build queues on one build lock, and builds at once barely gain on a 4-core container. Fresh worktrees, 2026-10-01:
  `x4pro` and `default` side by side took 384 s, against 611 s one after the other. But the serial `default` (369.5 s)
  rebuilt the C3 framework and the parallel one did not; with `default` at its cached 160.4 s, serial is an estimated
  400 s. Run two lanes unless the epic's Notes name more; a third mostly waits on the lock.
- **One worktree per lane**, branched from the epic branch, set up one lane at a time. In it, run
  `git submodule update --init --recursive`, then
  `flock /tmp/crosshatch-build.lock pio pkg install -e x4pro -e default` (the worktree's own library deps; the setup
  script's install stamp is machine-wide, so it skips them), then
  `PLATFORMIO_BUILD_CACHE_DIR={main}/.cache python3 scripts/dev_setup.py --warm`. `--warm` skips a build whose lock is
  already held, so wait for that lane's warm `x4pro` build to finish (`~/.cache/crosshatch/warm-x4pro.log`) before
  setting up the next lane; a lane whose warm build was skipped builds cold on its first story.
  Every lane builds with the main checkout's build cache (the brief's Environment says how). Measured 2026-10-01 in
  fresh worktrees with their library deps installed first: `x4pro` took 148.9 s with the shared cache (222 objects
  reused) and 240.7 s without (`plan-orchestration-follow-up.md`).
- **Locks.** AGENTS.md's two fixed locks (Known pitfalls) cover the orchestrator's own builds too. Your scratchpad
  `{scratch}` holds each story's `{scratch}/<ref>/` scratch files and fresh trees.
- **Nested review subagents.** `.claude/settings.json` sets `CLAUDE_CODE_MAX_SUBAGENT_SPAWN_DEPTH` to 3, Claude
  Code's default, because cloud sessions start it with 1, which keeps a build agent from starting its own review
  subagents (the cause of O1). The build agents' review lenses need only 2. Before the first story, start
  one subagent that reports whether it has the `Agent` tool; if it does not, the build agents run their lenses in their
  own context, and step 3's fallback applies to every story.
- **The upstream remote.** `scripts/dev_setup.py` (next bullet) adds `upstream`, fetches `develop`, and unshallows the
  clone; worktrees share that git config, so every agent can run `scripts/check_upstream_touches.py`. Confirm its
  `git` step passed; if it failed, its docstring lists the commands to run by hand.
- **Toolchain and base measurement.** Run `python3 scripts/dev_setup.py` (a cloud session's SessionStart hook has
  already run it; its output says whether a step failed). On the epic's base commit, under the build lock, build
  `x4pro` and `default`, and run `scripts/check_flash_budget.py` `build on`, `build off`, and `compare`. Record the flash and
  static-RAM figures, with the commit, as a dated `Measurement` line in the epic Notes. Every delta in the epic
  subtracts this measurement, not a figure an earlier epic recorded. In epic-icon-library, `sticky` and `default`
  stopped mid-story on the certifi step, and the flash cost was first quoted against a figure recorded before the gate
  counted IRAM (retro O3, O5).

### Each story

1. **Start the build agent** in its lane's worktree. Its prompt starts with the slash command `/bmad-build {ref}`, so
   the agent runs the whole bmad-build workflow (plan, implement, review, present) and not a summary of it. Then say
   that the build runs for an orchestrator, and give the brief below, filled in. The brief only pre-answers the
   workflow's human gates and adds this fork's rules; it never replaces a workflow step.
2. **Answer blocking questions.** A question the repo does not settle goes to the owner (see Owner hand-offs); send the
   answer back to the same agent.
3. **Review every story independently (AI-1).** In epic-script-runtime the build agents could not start subagents, so
   each ran its review lenses in its own context, and the three independent reviews found every defect those
   self-reviews had rated low or dismissed (O1). With nested subagents on (Before the first story), bmad-build's review
   step runs each lens as a context-free subagent over the story's diff, and that is the story's independent review:
   before merging, read the plan's Review Triage Log and confirm the lenses ran as subagents. When they did not (no
   `Agent` tool, or the log says the lenses ran in the build agent's context), run context-free review subagents
   yourself over the story's diff (`git diff <plan baseline>..<commit>`) with the plan as the intent: `bmad-review`, or
   bmad-build's thorough lenses (blind hunter, edge-case hunter, verification gap, intent alignment). This applies to
   every story, not only the risky ones. Send those findings to the build agent, which triages each one into its
   plan's Review Triage Log and fixes what it accepts in a follow-up commit, so every plan keeps its review record
   (O2).
3a. **Surface API-visible choices early.** When an epic runs unattended and a story fixes an API-visible choice that
    later stories will build on (names, a list's convention, an argument's shape), send the owner one question with its
    screenshot as soon as the tracer shows it. Keep building the lanes that do not depend on the answer. In
    epic-icon-library the naming convention came back at entry 8, and entry 9 rebuilt the set: 158 files, retro O4.
4. **Merge only a finished tree.** Never merge into, or rebase, a worktree whose agent is still working; wait for its
   report. A report that says a lens, implementation, or other subagent has not returned is an interim hand-back, not a
   finished report: an agent whose subagent runs in the background ends its turn with nothing left to do, and the
   harness then asks it for a report. It resumes by itself when that subagent returns (the notification says it may
   resume on its own); resume it with `SendMessage` only if it does not, and only then read its Review Triage Log or
   merge. In epic-icon-library, and in both lanes of its retrospective's follow-up, agents handed back this way (retro
   O1). `deferred-work.md` merges with `merge=union` (`.gitattributes`), so appends from two lanes combine without a
   conflict (O10); when two branches edited the same existing entry, union keeps both versions, so read the result.
5. **Record out-of-session fixes.** A fix that lands outside its story's session, such as one that changes an earlier
   story's code, records its verification in the plan of the story it changes (O5).
6. **Re-run the host suites on the combined tree before every push** (under the host-test lock), plus
   `python3 scripts/<name>_test.py` for each fork script. In epic-script-runtime a parallel fix (c25a2ff6) broke another
   story's tests, and only this run caught it (O5).
7. **Show the screenshots.** Right after merging a story whose verify names screenshots, send its
   `{epic-folder}/story-<name>-screenshots/` images into this session with `SendUserFile` (`display: render`,
   `status: proactive`), one call per story, captioned with the ref, the title, and one line on what each image shows,
   so the owner can skim the session's results in order before loading a build on a device. A story with no
   screenshots gets no call. Without `SendUserFile`, list the paths in the session instead.
8. **Mark the ticket done and push.** Build agents never run `tickets.py mark` or `pull`; the orchestrator runs
   `uv run _bmad/method/scripts/tickets.py --project-root . mark <ref> done` on the combined tree, commits it, and pushes
   the epic branch to `origin`.
9. **Delete finished trees (O7).** Remove a lane's worktree (`git worktree remove`) once its last story is merged, and
   each `{scratch}/<ref>/` fresh clone or archive tree once its gate has run. A worktree takes about 1.5 GB and a fresh
   clone about 1.8 GB; in epic-script-runtime a full disk half-installed `~/.platformio/packages` mid-build.
10. **After a container restart, check before resuming (epic-install-and-launcher retro AI-8).** Before resuming any
    build agent, look at each lane's worktree (`git status`, `git log`, the plan's `status`) and at which of its
    subagents are still running, and resume only from what is there. Record the restart and what was recovered, or
    redone, in that builder's plan. In epic-install-and-launcher, 4.10's implementation subagent was cut off and the
    plan's author implemented it from the plan, and no record of either restart reached the repo (retro proc-4).

### Owner hand-offs

- **Stories marked `hitl = true`** in `tickets.toml` (device runs, a release dry run) stop where their plan says. Give
  the owner the firmware commit, the steps (for game work, the fixtures README), and what to record; put the result in
  the plan's Verification.
- **Calibrate timing-dependent fixtures before a device-run packet (epic-install-and-launcher retro AI-5).** Before
  building a packet, give every fault fixture whose outcome depends on device timing (a watchdog band, a budget band,
  a replay timing) a tethered calibration run on the device, or list it in the packet as "uncalibrated: expected
  outcome estimated from the host ratio". The `loop` fixture's "Slow C calls forever" band was sized from host
  arithmetic and was abandoned, not cancelled, at its first device run (retro proc-3, e4-z1).
- **Decisions.** Give the owner the options, what each means, and a recommendation. Record the answer, dated, in the
  epic file's Notes, and in the architecture spine when it changes a decision there.
- **Measure before quoting (O8).** A memory, flash, or timing figure goes to the owner as a measurement with its method,
  or labelled "unmeasured". In epic-script-runtime about 320 KiB was quoted for Lua's region; the measurement was 448
  KiB. A delta is two measurements made the same way, one at each commit. The base is the one measured before the first
  story, re-measured when a story changes how a gate measures (epic-icon-library retro O3). A measurement is taken on
  the commit it cites, after the last squash or amend; one taken before a history rewrite is void and is repeated
  (epic-install-and-launcher retro AI-7, proc-7).

### Before the epic PR

- **Cross-story review.** After the last story lands, run context-free review subagents (`bmad-review`'s adversarial,
  edge-case, and verification-gap lenses) over `git diff <epic base>..HEAD`, excluding generated files, vendored assets,
  and screenshots, and weight the boundaries between stories. Record it as `{epic-folder}/cross-story-review.md` with a
  triage table. A build agent fixes what is accepted, and that fix commit gets the same review before the push. A story
  that lands after the review gets its own combined-diff pass. In epic-icon-library this pass found a medium data race
  that every per-story review missed (retro O2).
- On the combined tree, in AGENTS.md's verification order and under the locks: the host suites, the fork script
  tests, and `./bin/clang-format-fix` twice with nothing new in `git status`; then build `x4pro` and `default`, run
  `pio check` as AGENTS.md gives it (with `-e x4pro` too when games code changed), and `sim.sh build x4pro` when
  `src/games` or a screen changed. CI builds all five envs (`default`, `x4pro`, `sticky`, `x4c`, `papermono`) on the
  epic PR.
- Open one PR for the epic into `develop` with a Conventional Commit title, and never merge it with a red check
  (AGENTS.md, Policy).

## Build-agent brief

Before handing this section to a build agent, replace:

- `{epic-folder}`: the epic's folder, such as `_bmad-output/initiative-crosshatch-player-v1/epic-script-runtime`;
- `{ref}`: the story's ref, such as `2.7`, or the id of work that is not a ticket;
- `{scratch}`: the orchestrator's scratchpad directory;
- `{main}`: the main checkout's path, whose `.cache` every lane's builds share.

Everything from here to the end of the file is the brief.

### Your task

You are building `{ref}` from `{epic-folder}` for an orchestrator. The orchestrator reviews your work afterwards and runs
an independent review of your commit. Follow AGENTS.md exactly; re-read it in your worktree.

### How to run the build

You work in your own git worktree (your current directory); never touch the main checkout or another agent's worktree.
Your prompt starts with `/bmad-build {ref}`: run that skill and follow every step of its workflow (clarify, plan,
implement, review, present), with nothing skipped or condensed; the plan goes where `tickets.py find {ref}` says,
or, for work that is not a ticket, to `_bmad-output/implementation-artifacts/plan-<slug>.md`, with the slug led by
`{ref}`. If bmad-build hands the plan to an implementation subagent, that subagent only implements the plan; it never
invokes bmad-build or follows this brief. The orchestrator pre-answers the workflow's human gates, so do not stop at
them:

- Multi-goal check: **Keep all goals**; the story is an agreed scope.
- Token count gate: **Keep full plan**, unless it is far over because of padding; then tighten, never split.
- Checkpoint 1: **Approve and continue**.
- Open questions: resolve each from the repo, the architecture spine (`_bmad-output/planning-artifacts/architecture/`),
  the epic file and its Notes, earlier plans in `{epic-folder}`, retrospectives, and git history. Only a choice none of
  those settles and that would change the design goes back to the orchestrator: stop and put it, with options and your
  recommendation, at the top of your final report.
- Any other HALT (a dirty tree or a branch mismatch at the version-control check, an intent_gap loopback, the review
  loop limit): stop and put it at the top of your final report as a blocking question.
- Review step: run each review lens as a context-free subagent over your diff, as bmad-build's review step does, and
  say in the plan's Review Triage Log that the lenses ran as subagents. Only if you have no `Agent` tool, do not HALT:
  run each lens yourself, one at a time, reading each lens prompt fresh and judging only the diff, say so in the log
  and at the top of your report, and triage the independent review the orchestrator then sends you into the same log.
  Start implementation and lens subagents in the foreground (`run_in_background: false`; lenses in one message so they
  still run in parallel) whenever you have nothing else to do meanwhile: a background subagent lets your turn end, and
  the harness then takes an unfinished report from you. A nested agent's subagents may still run in the background
  despite the flag (the icon epic's follow-up reviewer, 2026-09-28), so if your turn ends early anyway, say at the top
  of the report that it is interim and which subagents are still running; you resume when they return. Wait for every
  lens subagent, and any implementation subagent, to return before you triage or give a final report. The Review Triage
  Log names the lenses that returned. Run the review after the host tests and fast checks and before any firmware
  build or `pio check` (AGENTS.md's verification order), so a review fix costs one round of firmware builds.
- Commit: exactly one local commit on your worktree's branch (a follow-up commit is fine when the orchestrator sends
  review findings). Do not push, do not open a PR, and never run `tickets.py mark` or `pull`; the orchestrator marks
  the ticket. End the commit message with the attribution lines your session's system gives.

### Environment

- The orchestrator has set your worktree up: submodules, library deps, and a warm build when one ran. Every build takes
  AGENTS.md's locks (Known pitfalls). Export `PLATFORMIO_BUILD_CACHE_DIR={main}/.cache` in every shell that builds,
  so `pio` and the scripts that call it (`sim.sh`, `scripts/check_flash_budget.py`) share the lanes' cache. Firmware
  builds take minutes: use a long timeout, or run in the background and wait.
- Host tests build in your worktree's `build/test`, with the commands in AGENTS.md.
- Scratch files, logs, and fresh trees go under `{scratch}/{ref}/`; delete fresh trees when you are done (disk is
  limited).

### Rules

- Verify with local evidence (host tests, builds, simulator screenshots); CI runs later on the epic PR. Record the
  evidence in the plan's Verification.
- A story that adds or changes a CI gate or workflow runs that gate once from a fresh tree of your committed work
  before it counts as built: `git clone <worktree> {scratch}/{ref}/fresh` and check out your commit there, or, when
  cloning is blocked and the gate reads no git history, an archive tree. Run `git submodule update --init --recursive`
  first, so each submodule checkout sits at your commit's gitlink, then:

  ```sh
  mkdir -p {scratch}/{ref}/fresh
  git archive <commit> | tar -x -C {scratch}/{ref}/fresh
  git submodule foreach --recursive 'git archive --prefix="$displaypath/" HEAD | tar -x -C {scratch}/{ref}/fresh'
  ```

  The recursion matters: `freeink-sdk` has nested submodules. Run only that gate's own commands there (never the
  other envs or the other gates), reusing the machine's warm `~/.platformio` (never move it aside), and say in the
  plan which kind of tree it was. A new fork job goes in `Crosshatch Test Status`'s `needs` in
  `.github/workflows/crosshatch-ci.yml`; never edit `ci.yml`.
- Screenshots: when your verify names simulator screenshots, look at each one (`build/sim/shots/`), then copy the ones
  that show the result into `{epic-folder}/story-<name>-screenshots/`, `<name>` a word or two for the story
  (epic-script-runtime used `story-gfx-screenshots/`), under short file names that say what they show, commit them with the story, and list
  each path with one line on what it shows in the plan's Verification and your final report. The orchestrator shows
  them to the owner.
- Upstream files: run `python3 scripts/check_upstream_touches.py` before committing when you touched a non-fork file.
- New fork scripts follow `docs/crosshatch/fork-scripts.md` (sidecar test, exit contract, `fork_common.py`, listed in
  the ledger's Game paths).
- Deferred items: append to `_bmad-output/implementation-artifacts/deferred-work.md` only under a heading `## {ref}` at
  the end of the file, each entry in the file's existing format (`- source_plan:`, `summary:`, `evidence:`). The file
  merges with `merge=union`; a distinct first line per story keeps two lanes' appends from interleaving line by line.
- A memory, flash, or timing figure in your plan or report is a measurement with its method, or says "unmeasured"
  (deltas as AGENTS.md's Known pitfalls say). Take it on the commit it cites, after your last amend.
- Test doubles (epic-install-and-launcher retro AI-4): a double your story adds or extends names, in a comment and in
  the plan, the device behaviour it stands in for, and a test pins that the two agree; where it is more permissive
  than the device, say so. A screen double records raw `text()` calls and never lays text out on `\n` unless the
  renderer does. A host timing bound is derived from the device ratio (about 95x host to device, `plan-e4-z1` M1) and
  labelled an estimate until a device run measures it. In epic-install-and-launcher the doubles were more permissive
  than the device three times, and each time a fix passed every test (retro proc-2).
- A flake fix is proven at a stated repeat count: 20 full `ctest` runs plus `--repeat until-fail:200` on each flaky
  test is the bar (epic-install-and-launcher entry 13; retro AI-7, proc-5). A failing test is never called a flake
  without that proof.
- Before moving or rewriting an existing function, say in Design Notes what each guard or early return in it protects
  (AGENTS.md's `git log -L` pitfall).
- Formatting as AGENTS.md says, review fixes and plan edits included; name any formatting-only change it makes outside
  your paths in your report, and stop with a blocking question if it changes an upstream file the ledger does not list.
- For C/C++ changes, build and check as AGENTS.md's "While testing" and static-analysis bullets say; for `src/games`
  or screens also `sim.sh build x4pro`.

### Final report

Under 300 words: blocking questions first, then the commit hash and branch, what changed (by path), the verification
evidence (commands and results), the screenshot paths with one line each, whether every review lens ran as a subagent
and returned, anything deferred, and any risk or unfinished item.
