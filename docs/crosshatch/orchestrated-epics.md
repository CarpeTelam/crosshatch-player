# Orchestrated epics

An orchestrated epic is one where a single agent, the orchestrator, builds the epic's stories by handing each one to a
build agent working in its own git worktree, several at a time. This file has two parts: the **Orchestrator
procedure**, which the orchestrator follows, and the **Build-agent brief**, which it fills in and gives to every build
agent. Both add to AGENTS.md; they never replace it. The rules come from the epic-script-runtime retrospective
(`_bmad-output/initiative-crosshatch-player-v1/epic-script-runtime/epic-script-runtime-retrospective.md`, AI-1 and
AI-12), and the finding ids below (O1, O5, and so on) point there.

Each story runs as one `bmad-build-auto` dispatch: the unattended workflow that plans, implements, reviews with four
lens subagents, and commits one ticket, then writes a final `status` to its plan and stops. It never asks a question;
anything the intent leaves open halts it as `blocked` with a named reason in the plan, and the orchestrator answers by
amending the intent and dispatching again. `_bmad/custom/bmad-build-auto.toml` pins its review to `thorough` and tells
it to read the brief when the prompt says the build runs for an orchestrator; the brief adds this fork's rules and
names where a ticket's settled decisions live, and never replaces a workflow step. Interactive `bmad-build` keeps every
human gate; it builds for an orchestrator only in the two cases under "When a story runs under bmad-build".

## Orchestrator procedure

### Before the first story

- **Lanes.** Take the lanes and their order from the epic file's Notes (epic-script-runtime ran two: CI and scripts,
  2 → 3 → 5 → 17, beside the runtime lane). Stories in one lane touch shared files in order; lanes run in parallel.
  `tickets.py next` shows what is ready. Lanes overlap planning, implementation, and review, not builds: every
  build queues on one build lock, and builds at once barely gain on a 4-core container. Fresh worktrees, 2026-10-01:
  `x4pro` and `default` side by side took 384 s, against 611 s one after the other. But the serial `default` (369.5 s)
  rebuilt the C3 framework and the parallel one did not; with `default` at its cached 160.4 s, serial is an estimated
  400 s. Run two lanes unless the epic's Notes name more; a third mostly waits on the lock.
- **One worktree per lane**, branched from the epic branch, set up one lane at a time. Name the lane branch after the
  epic and the lane (`epic-pass-and-play-lane-a`): the auto workflow's version-control check judges the branch against
  the epic and halts on an obvious mismatch. In the worktree, run `git submodule update --init --recursive`, then
  `flock /tmp/crosshatch-build.lock pio pkg install -e x4pro -e default` (the worktree's own library deps; the setup
  script's install stamp is machine-wide, so it skips them), then
  `PLATFORMIO_BUILD_CACHE_DIR={main}/.cache python3 scripts/dev_setup.py --warm`. `--warm` skips a build whose lock is
  already held, so wait for that lane's warm `x4pro` build to finish (`~/.cache/crosshatch/warm-x4pro.log`) before
  setting up the next lane; a lane whose warm build was skipped builds cold on its first story. Before each dispatch,
  `git status` in the worktree must show a clean tree: the workflow halts on a dirty one.
  Every lane builds with the main checkout's build cache (the brief's Environment says how). Measured 2026-10-01 in
  fresh worktrees with their library deps installed first: `x4pro` took 148.9 s with the shared cache (222 objects
  reused) and 240.7 s without (`plan-orchestration-follow-up.md`).
- **Locks.** AGENTS.md's two fixed locks (Known pitfalls) cover the orchestrator's own builds too. Your scratchpad
  `{scratch}` holds each story's `{scratch}/<ref>/` scratch files and fresh trees.
- **Nested subagents are a precondition.** `.claude/settings.json` sets `CLAUDE_CODE_MAX_SUBAGENT_SPAWN_DEPTH` to 3,
  Claude Code's default, because cloud sessions start it with 1, which keeps a build agent from starting its own
  subagents (the cause of O1). The auto workflow needs depth 2 (its implementation subagent and its four lenses) and
  halts `no subagents` without it. Before the first story, start one subagent on Sonnet (`model: "sonnet"`, as the
  build agents run) that reports whether it has the `Agent` tool. If it does not, fix the setting before dispatching
  anything; if that fails, the epic runs under `bmad-build` as "When a story runs under bmad-build" says, and step 3's
  fallback review applies to every story.
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
- **Settle the intent before the epic runs.** The auto workflow halts on an intent gap instead of asking, and each halt
  costs a round trip through the owner and a re-dispatch. Before the first story, read every entry's description and
  `Verify:` against the epic's Requirements and Notes, and record the API-visible choices later stories build on
  (names, a list's convention, an argument's shape) as owner Decisions in the Notes; the pass-and-play inception did
  this (Notes, 2026-10-01). Review the stories with `bmad-preview-ticketing` first when an entry is thin.

### Each story

1. **Dispatch the build agent** in its lane's worktree, on Sonnet: pass `model: "sonnet"` to the `Agent` tool. The
   orchestrator runs on the stronger model the owner started it with and keeps planning, owner questions, merges, and
   the cross-story review there; a build agent's own subagents inherit its model, and
   `_bmad/custom/bmad-build-auto.toml` pins its implementation subagent to Sonnet as well. The build agent's prompt
   starts with the slash command `/bmad-build-auto ticket {ref}`: the word `ticket` matters, since the workflow takes
   a bare ref or title as free text, not as a ticket. Then say that the build runs for an orchestrator, and give the
   brief below, filled in. The review is already pinned to `thorough` by the same file. Add the sentence
   `Halt after planning.` for a plan checkpoint (step 3a); the run then stops at `ready-for-dev`, and the same dispatch
   later resumes it from the plan. The run's chat output is not its result: read the plan.
2. **Read the result from the plan.** `uv run _bmad/method/scripts/tickets.py --project-root <worktree> find {ref}`
   names the plan (it lives in the lane's worktree until the merge, so point `--project-root` there; `status` on the
   main checkout does not see it). Its frontmatter `status` is the outcome: `built` continues at step 3;
   `ready-for-dev` after a plan checkpoint is step 3a; `blocked` is step 2a. A report whose plan carries no terminal
   status is a run that stalled (a cut-off container, a background subagent): dispatch the same command again, and the
   workflow resumes from the plan's status (`draft` plans, `ready-for-dev` and `in-progress` implement, `in-review`
   reviews).
2a. **Unblock and re-dispatch.** `blocked_reason` in the plan and `## Auto Run Result` say why; `tickets.py status`
    on the worktree lists every blocked ticket with its reason. A blocked plan halts every later dispatch of its ticket
    until the status is reset.
    - `intent gap` or `unclear intent`: a choice the entry, epic file, Notes, and the brief's sources do not settle.
      Put it to the owner (see Owner hand-offs), record the answer as a dated Decision in the epic Notes, which the next
      run reads as intent, then reset and re-dispatch. A gap found at planning: `tickets.py mark {ref} draft`. A gap
      found at review: the run saved the attempted change as a patch under `_bmad-output/implementation-artifacts/`
      (the plan names it) and reverted the code; if the patch reads the intent correctly, `git apply` it in the
      worktree, `tickets.py mark {ref} in-review`, and re-dispatch, else `mark {ref} draft` and start fresh.
    - `implementation verification failed`, `patch verification failed`, or `review repair loop exceeded 5 iterations`:
      read the Auto Run Result and the Review Triage Log. Add what the plan missed to the Notes as an Assumption or
      Decision, `mark {ref} draft`, and re-dispatch; a second halt for the same reason goes to the owner.
    - `no subagents`: fix the depth setting (Before the first story); do not re-dispatch until the probe passes.
    - A dirty tree or a branch mismatch: clean the worktree (the orchestrator's leftovers, never the run's) and
      re-dispatch; `blocked plan supplied` means the reset was skipped.
    Run `mark` in the lane's worktree (`--project-root <worktree>`); it clears `blocked_at` and `blocked_reason`.
3. **Every story is reviewed independently (AI-1).** In epic-script-runtime the build agents could not start subagents,
   so each ran its review lenses in its own context, and the three independent reviews found every defect those
   self-reviews had rated low or dismissed (O1). The auto workflow runs each of its four lenses (blind hunter, edge-case
   hunter, verification gap, intent alignment) as a context-free subagent over the story's diff and halts rather than
   self-review, so the lenses are the story's independent review. Before merging, read the plan's frontmatter:
   `lenses_ran` lists the four ids, and the Review Triage Log has one row per finding. When
   `followup_review_recommended` is `true` (the review patched a high finding, or two or more mediums), dispatch the
   same command once more before merging: a `built` plan gets a fresh review pass, and its follow-up fixes land as a
   further commit on the lane. Fallback, only when the epic runs under `bmad-build` without nested subagents: run
   context-free review subagents yourself over the story's diff (`git diff <plan baseline>..<commit>`) with the plan
   as the intent (`bmad-review`, or the four lenses above), send the findings to the build agent, which triages each
   one into its Review Triage Log and fixes what it accepts in a follow-up commit, so every plan keeps its review
   record (O2).
3a. **Surface API-visible choices early.** When a story fixes an API-visible choice that later stories will build on
    (names, a list's convention, an argument's shape) and the Notes did not settle it, dispatch it with
    `Halt after planning.`, read the plan's Design Notes, and send the owner one question with the choice, the
    options, and your recommendation; the same dispatch then implements the approved plan. When the choice only shows
    in a running screen, send the question with its screenshot as soon as the tracer shows it. Either way keep
    building the lanes that do not depend on the answer. In epic-icon-library the naming convention came back at
    entry 8, and entry 9 rebuilt the set: 158 files, retro O4.
4. **Merge only a finished tree.** Never merge into, or rebase, a worktree whose agent is still working; wait for its
   report, and merge only a plan whose `status` is `built`. The auto workflow runs every subagent in the foreground
   and ends its turn only at a HALT, so an agent that hands back early with subagents still running (the interim
   hand-backs of epic-icon-library, retro O1) is a run that left the workflow: treat it as stalled (step 2), not as
   finished. The lane commit is one commit per run (a follow-up pass adds one more).
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
8. **Carry the deferrals, mark the ticket done, and push.** The plan's frontmatter `deferred` list is the story's
   record of real findings that were not its problem. On the combined tree, append each item to
   `_bmad-output/implementation-artifacts/deferred-work.md` under a heading `## {ref}` at the end of the file, in the
   file's format (`- source_plan:`, `summary:`, `evidence:`, with the item's `location` and `severity` in the
   evidence). Build agents never edit that file or run `tickets.py mark done` or `pull`; the orchestrator runs
   `uv run _bmad/method/scripts/tickets.py --project-root . mark <ref> done` on the combined tree, commits both, and
   pushes the epic branch to `origin`. (`deferred-work.md` keeps `merge=union` in `.gitattributes` for out-of-session
   fixes that still append from a lane, O10; when two branches edited the same existing entry, union keeps both
   versions, so read the result.)
9. **Delete finished trees (O7).** Remove a lane's worktree (`git worktree remove`) once its last story is merged, and
   each `{scratch}/<ref>/` fresh clone or archive tree once its gate has run. A worktree takes about 1.5 GB and a fresh
   clone about 1.8 GB; in epic-script-runtime a full disk half-installed `~/.platformio/packages` mid-build.
10. **After a container restart, resume from the plans (epic-install-and-launcher retro AI-8).** Before dispatching
    anything, look at each lane's worktree (`git status`, `git log`, the plan's `status`). The plan is the run's whole
    state: dispatching the same command resumes it from that status (step 2), so no builder is resumed mid-turn. A
    half-implemented `in-progress` tree resumes as it stands. Record the restart, and what was recovered or redone,
    under the plan's `## Implementation Notes`. In epic-install-and-launcher, 4.10's implementation subagent was cut
    off and the plan's author implemented it from the plan, and no record of either restart reached the repo (retro
    proc-4).

### Owner hand-offs

- **Stories marked `hitl = true`** in `tickets.toml` (device runs, a release dry run) are not dispatched to the auto
  workflow: their Verification needs the owner, and the run would halt `implementation verification failed`. Run them
  under `bmad-build` as "When a story runs under bmad-build" says; they stop where their plan says. Give the owner the
  firmware commit, the steps (for game work, the fixtures README), and what to record; put the result in the plan's
  Verification.
- **Calibrate timing-dependent fixtures before a device-run packet (epic-install-and-launcher retro AI-5).** Before
  building a packet, give every fault fixture whose outcome depends on device timing (a watchdog band, a budget band,
  a replay timing) a tethered calibration run on the device, or list it in the packet as "uncalibrated: expected
  outcome estimated from the host ratio". The `loop` fixture's "Slow C calls forever" band was sized from host
  arithmetic and was abandoned, not cancelled, at its first device run (retro proc-3, e4-z1).
- **Decisions.** Give the owner the options, what each means, and a recommendation. Record the answer, dated, in the
  epic file's Notes, and in the architecture spine when it changes a decision there. The Notes are intent for every
  later run, so a Decision recorded there settles the same question for the stories that follow.
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
  triage table. The accepted findings are built as one `bmad-build-auto` dispatch with the review file as its intent
  (`/bmad-build-auto {epic-folder}/cross-story-review.md`, the brief's `{ref}` an id such as `e5-xr`), and that commit
  gets the same review before the push. A story that lands after the review gets its own combined-diff pass. In
  epic-icon-library this pass found a medium data race that every per-story review missed (retro O2).
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

You are building `{ref}` from `{epic-folder}` for an orchestrator. The orchestrator reads your plan afterwards, merges
your commit, and marks the ticket. Follow AGENTS.md exactly; re-read it in your worktree.

### How to run the build

You work in your own git worktree (your current directory); never touch the main checkout or another agent's worktree.
Your prompt starts with `/bmad-build-auto ticket {ref}`: run that skill and follow every step of its workflow (clarify,
plan, implement, review, finalize), with nothing skipped or condensed. The plan goes where `tickets.py find {ref}` says,
or, for work that is not a ticket, to `_bmad-output/implementation-artifacts/plan-<slug>.md`, with the slug led by
`{ref}`. This brief is workflow input: it adds this fork's rules and names where the intent lives. It never replaces a
workflow step, and a HALT is never a cue to guess.

- **Where the intent lives.** The entry in `tickets.toml`, its epic file and what that file's References name, and its
  story file when it has one are the intent, as the workflow says. The decisions that settle a reading of it live in
  the epic file's Notes (dated Decisions and Assumptions), the architecture spine
  (`_bmad-output/planning-artifacts/architecture/`), the earlier plans in `{epic-folder}`, the retrospectives, and
  git history: read them before calling anything an intent gap, and cite the line that settles each choice in the
  plan's Design Notes. A choice none of those settles, with readings that lead to observably different outcomes, is
  an intent gap: HALT as the workflow says, with the options and your recommendation under `## Auto Run Result`, and
  the orchestrator brings the owner's answer back as a Notes Decision. Never pick a reading.
- **Version-control check.** Your worktree's branch is one lane of the epic's branch, named for the epic; it fits the
  intent. The orchestrator hands you a clean tree; if it is dirty, HALT as the workflow says, since the leftovers are
  not yours to clean.
- **Subagents.** The workflow's rules hold: every subagent a step calls for starts in one message, in the foreground,
  and you wait for all of them; a backgrounded subagent stalls an unattended run. If `bmad-build-auto` hands the plan
  to an implementation subagent, that subagent only implements the plan; it never invokes a build skill or follows
  this brief. The four review lenses are the story's independent review (the orchestrator relies on `lenses_ran` in
  the plan), so never run a lens in your own context.
- **Verification order.** The persistent facts in `_bmad/custom/bmad-build-auto.toml` are AGENTS.md's order: host tests
  and fast checks during implementation; the firmware checks the plan's Verification lists once, under the build lock,
  after the review and its patches and before the commit.
- **Marks and the commit.** The workflow's HALT runs `tickets.py mark … blocked`; that is the only `mark` you run.
  Never `mark` any other status, never run `pull`, never push, and never open a PR; the orchestrator marks the ticket
  done. Finalize makes the run's one commit (a follow-up pass adds one more) and requires a clean worktree afterwards,
  so every file your story adds, screenshots included, is committed with it. End the commit message with the
  attribution lines your session's system gives.

### When a story runs under bmad-build

Only a `hitl` ticket, or an epic the orchestrator runs without nested subagents, is built with `/bmad-build {ref}`
instead. The brief then pre-answers that workflow's human gates, and nothing else changes:

- Multi-goal check: **Keep all goals**; the story is an agreed scope. Token count gate: **Keep full plan**, unless it
  is far over because of padding; then tighten, never split. Checkpoint 1: **Approve and continue**.
- Open questions: the sources under "Where the intent lives" settle them. Only a choice none of those settles and that
  would change the design goes back to the orchestrator: stop and put it, with options and your recommendation, at the
  top of your final report. Any other HALT (a dirty tree, a branch mismatch, an intent_gap loopback, the review loop
  limit) is likewise a blocking question at the top of your report.
- Review step: run each lens as a context-free subagent and say so in the plan's Review Triage Log. Only without an
  `Agent` tool, run each lens yourself, one at a time, reading each lens prompt fresh and judging only the diff, say
  so in the log and at the top of your report, and triage the independent review the orchestrator then sends you into
  the same log. Start every subagent in the foreground and wait for it before you triage or report.
- A `hitl` ticket stops where its plan says and hands the owner's steps to the orchestrator in the report.
- Commit exactly one local commit (a follow-up commit is fine when the orchestrator sends review findings); do not push,
  do not open a PR, and never run `tickets.py mark` or `pull`.

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
  (epic-script-runtime used `story-gfx-screenshots/`), under short file names that say what they show, commit them with
  the story, and list each path with one line on what it shows in the plan's Verification and under
  `## Auto Run Result`. The orchestrator shows them to the owner.
- Upstream files: run `python3 scripts/check_upstream_touches.py` before committing when you touched a non-fork file.
- New fork scripts follow `docs/crosshatch/fork-scripts.md` (sidecar test, exit contract, `fork_common.py`, listed in
  the ledger's Game paths).
- Deferred items: the plan's frontmatter `deferred` list, which the review step writes, is the record; give each item
  the `location` and `severity` fields. Never edit `_bmad-output/implementation-artifacts/deferred-work.md`; the
  orchestrator carries the list there when it marks the ticket done.
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
  your paths under `## Auto Run Result`, and HALT with a blocking condition if it changes an upstream file the ledger
  does not list.
- For C/C++ changes, build and check as AGENTS.md's "While testing" and static-analysis bullets say; for `src/games`
  or screens also `sim.sh build x4pro`.

### Final report

The plan is the record: its `status`, `## Auto Run Result` (summary, files, review breakdown, verification, residual
risks), `lenses_ran`, `deferred`, and `followup_review_recommended`. Your final message, under 150 words, points at it:
the plan path and its final status, the commit hash and branch, the screenshot paths with one line each, and, after a
HALT, the blocking condition first. Do not restate the plan.
