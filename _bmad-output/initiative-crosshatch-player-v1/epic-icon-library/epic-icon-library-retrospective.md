---
epic: epic-icon-library
date: 2026-09-28
verdict: accepted-with-open-items
criteria: declared
headless: false
---

# Retrospective: epic-icon-library

## Epic summary

**Epic:** `epic-icon-library` (epic 3, "Games and runtime screens share one icon library"), folder `_bmad-output/initiative-crosshatch-player-v1/epic-icon-library/`, covering CAP-8 except the launcher rows and first-party drawing.

**Tickets.** Ten, in build order from `tickets.py status`. Every one has `status = done` and `state = done`. `pending_tickets` is empty and none is left at `built`. Entries 9 and 10 were added during the owner's entry-8 review (87dae8ea, ba4cf74b). Commits are attributed by subject and plan, because the ranges below misattribute them.

| Ref | Title | hitl | Covers | Plan baseline | Its commits |
|-----|-------|------|--------|---------------|-------------|
| 3.1 | Tracer: a Phosphor icon from SVG to the game canvas | no | R1, R2, R4, R6, R7 | `1eacdc77` | b562ad3c, 4018b825, bc6adc54 (mark) |
| 3.2 | ch.gfx.image and package images | no | R5, R6, R7 | `bc6adc54` | 890c1a69, 068a9ad0 (mark) |
| 3.3 | Icon regeneration job | no | R2 | `bc6adc54` | 3cd86dfa, 3fed681a (mark) |
| 3.4 | The v1 icon set | no | R1, R3, R7, R10 | `068a9ad0` | 7e54d5d6, 7329a020 (mark) |
| 3.5 | Runtime views draw from the library | no | R8 | `7329a020` | 491486bf, 0b6612ea (mark) |
| 3.6 | Cover-grid Home Games tile | no | R9, R10 | `7329a020` | 39467f07, 201b7c55 (mark) |
| 3.7 | Refactor sweep | no | R10, R11 | `201b7c55` | 8e233695, 720fe299 (mark) |
| 3.8 | Owner review of the icon set and screens | **yes** | R1–R3, R7–R11 | none (the plan has no `baseline_revision`) | d252cbef, 38000ed2 (packet), 87dae8ea (answers; adds 3.9), ba4cf74b (adds 3.10), e39c29ab, f7be3b4a, dc167d9c, c82ab12c (sign-off), 0ef5a900 |
| 3.9 | Phosphor names and both weights | no | R1–R4, R7–R10 | `87dae8ea` | 9197d046, ba2ae079 (mark), 2ba81907 |
| 3.10 | Deferred cleanup before epic-install-and-launcher | no | R11 | `ba4cf74b` | 0520b146, 0409fafd (mark) |
| — | Cross-story review (not a ticket) | no | — | `ba2ae079` (reviewed `1eacdc77..ba2ae079`) | f27dcefd |

**Ranges.**
- **Baseline order**, oldest first by ancestry: `1eacdc77` (3.1), `bc6adc54` (3.2, 3.3), `068a9ad0` (3.4), `7329a020` (3.5, 3.6), `201b7c55` (3.7), `87dae8ea` (3.9), `ba4cf74b` (3.10). `git_evidence.py` ran once per distinct range and once for the whole epic (scratchpad `ev/*.json`).
- **Why the rule's ranges misattribute.** Lanes ran in worktrees and were merged into the epic branch. 3.10's baseline (`ba4cf74b`) is newer than 3.9's (`87dae8ea`), but 3.10's commit (0520b146) landed before 3.9's (9197d046), so 3.9's range holds only 3.8's docs commits and 3.10's range holds 3.10, 3.9, and the cross-story fix. The table attributes by subject and plan.
- **The epic-wide range** is `1eacdc77..d719a379`: 36 commits, 6 merges (1 measured on the first-parent spine: PR #17), 424 files, +15,693 / −3,206 in non-merge commits. 7,246 / 2,097 of that is the generated header `lib/GameIcons/GameIcons.generated.h`.

**Delivery.** One PR, [CarpeTelam/crosshatch-player#17](https://github.com/CarpeTelam/crosshatch-player/pull/17), merged 2026-09-28T15:32:49Z by the owner. Its 19 check runs were all `success` on the head `e6bed5f7`, including `Test Status`, `Crosshatch Test Status`, the five env builds, `unit-tests`, `clang-format`, `cppcheck`, `Upstream touch ledger`, `x4pro flash budget`, `Layer check`, `Icons up to date`, `API freeze`, `Fork script tests`, `Simulator build`, and `Title Check`. `Crosshatch Test Status` finished at 15:31:41Z, so the PR did not merge red.

**Evidence inventory**

| Evidence | Status | Source |
|----------|--------|--------|
| Epic file with R1–R11 (amended 2026-09-28), Done when 1–5, dated owner decisions, 27 `Assumption for entry 8:` lines and their answers, two `Measurement` lines | present | `epic-icon-library.md` |
| Initiative requirements (CAP-8) | present | `../initiative-crosshatch-player-v1.md` |
| Ticket entries | present, 10 | `tickets.toml`, `tickets.py status` |
| Story files | none; no ticket was refined | `tickets.py status` (`refined: false`) |
| Plans | present, 10, all `status: done`; 3.8's is a short record with no baseline and no review | `story-*-plan.md` |
| Cross-story review | present: 8 findings, fixed in f27dcefd | `cross-story-review.md` |
| Review packet and measured deltas | present | `review-packet/README.md`, `review-packet/{icons,views,home}/` |
| Simulator screenshots | present, 6 story folders plus the packet | `story-*-screenshots/` |
| Deferred work | `## 3.1`–`## 3.10` sections, and entries marked resolved | `_bmad-output/implementation-artifacts/deferred-work.md` |
| Handoffs to later epics | present | `../epic-install-and-launcher/epic-install-and-launcher.md:50-57`; `../epic-game-api-docs/epic-game-api-docs.md` Notes |
| CI | PR #17: 19/19 green on the head; earlier runs include one `cppcheck` failure on `f7be3b4a` (attempt 1), passed on re-run | GitHub Actions run 36423204077 |
| Commit and diff evidence | present | scratchpad `ev/` |
| Device runs | none, by the owner's decision ("no closing device run", epic Notes) | epic Notes |
| Orchestration record | **not in the repo.** 30 of 36 commits name the orchestrating session `session_01DhiSSwkAq51abArzAsjThg`; its transcript and the build agents' transcripts were not available to this retro. Process findings rest on plans, commits, CI, and the epic Notes; a claim resting only on the owner's prompt is marked "(owner's observation, no repo evidence)" | commit trailers |
| Previous retrospective | present | `../epic-script-runtime/epic-script-runtime-retrospective.md` |

**Going-in concerns.** The owner named six orchestration observations to test against the evidence (the invocation of this retro): build agents handing back early, the cross-story race, measurement against a recorded figure, the late entry-8 scope change, environment friction, and the flash headroom left for epic-install-and-launcher. Each is answered under Process and orchestration, as a finding where it has a source.

## Findings

Each finding carries its source and two dispositions: **instance** (fix now / defer / accept) and **prevention** (the upstream lesson). Finding ids are local to this retro. The previous retro's items are written `e2 AI-1`, `e2 R3`, and so on. Line numbers are at `d719a379` unless a commit is named.

The passes and where each claim was re-checked:

| Tag | Pass | Re-checked by this retro |
|-----|------|--------------------------|
| [plans] | plans, Notes, commits, and `deferred-work.md`, read by a context-free subagent | the race diff (0520b146, f27dcefd), the missing hashes (`git cat-file`), the measurement lines |
| [rev] | diff-scope review (`bmad-review`: edge-case, verification-gap, and adversarial lenses as subagents) over `git diff 1eacdc77..d719a379`, minus the generated header, SVGs, PNGs, and `_bmad-output` | R1 (`FrameReplay.cpp:166-180`), R4, R5 against the source |
| [agg] | aggregate views (include graphs from `git archive` trees, duplication, size, patterns, spec) | the manifest grammar, the `/.games-data` literal, the spine rows |
| [ci] | this retro, from PR #17's GitHub Actions runs | — |

### Process and orchestration

The owner asked this retro to test six observations. O1 to O6 answer them in order.

**O1. Build agents that hand back before their subagents finish: no trace in the repo, but it happened in this retro.** Medium (process).
- The repo holds nothing about 3.10's or the cross-story reviewer's early hand-back.
  - No plan, commit body, `cross-story-review.md`, or `deferred-work.md` entry mentions a resume, a second pass, or an unfinished lens [plans].
  - The timing is unremarkable. 3.10 took about 80 min from `ba4cf74b` (12:36) to 0520b146 (13:55), while other stories took 44–110 min. The cross-story fix f27dcefd came 43 min after the reviewed commit `2ba81907`.
  - The session transcript (`session_01DhiSSwkAq51abArzAsjThg`) was not available.
  - For those two agents, the observation stands as the **owner's observation, without repo evidence**.
- It recurred in this retro's own session, so the failure mode is real.
  - The diff-scope reviewer this retro started reported while its adversarial-lens subagent was still running. Its report said: "The adversarial lens subagent had not returned when this report was forced out".
  - This retro resumed the reviewer with `SendMessage`. The resumed agent reported that "the first adversarial run did finish, about 8.5 minutes in, after my first report had already gone out".
  - It had also started a backup re-run that it could not stop ("TaskStop refused, 'owned by' that agent").
  - That orphaned re-run reported later still, with eleven items, eight of them new to this retro.
  - The late lens produced this retro's R7 and R8. The orphaned re-run produced R9.
  - A lens is not deterministic. Two runs of the same adversarial lens over the same diff overlapped on only two items (the `NotLoaded` headline and the icon ink in the views), so one missing lens run is a real loss of coverage.
  - Without the resume, one of the three lenses would have been missing. The first report looked finished apart from one sentence.
- Nothing in the process catches this.
  - The brief's Final report asks for "whether the review lenses ran as subagents", not whether each one *returned*.
  - Orchestrator step 3 checks that the Triage Log says the lenses ran as subagents. A log can say that while one lens's rows are missing.
  - 3.9 and 3.5 also ran their implementation as a subagent (3.9 plan :176; 3.5 plan :84). So a hand-back can also leave code half-applied, not only a review short.
- **Mechanism, found while applying this retro's AI-3** (the follow-up's own session):
  - The e3r-1 and e3r-2 build agents each handed back "NOT FINISHED" while an implementation subagent they had started in the background was still running.
  - The e3r-2 agent wrote that "the harness requires a handback now", and the harness's notice said the agent "may resume on its own when that work completes".
  - So an agent with only background work left ends its turn, and the harness takes an interim report. A "wait before reporting" rule cannot prevent that; starting the subagent in the foreground can.
  - AI-3's edits were amended to say this.
- Instance: **accept** (the orchestrator resumed both at the time, per the owner).
- Prevention: AI-3, one Build-agent brief rule and one orchestrator check (exact edits under Proposed edits).

**O2. The cross-story review caught a seam defect every per-story review missed, and this retro found another. The race came from a refactor dropping an earlier guard, the pitfall AGENTS.md already names.** Medium.
- e2 AI-1 held: all nine reviewed plans say their lenses ran as context-free subagents [plans]. 3.8 has no review, as a `hitl` record.
- They still missed seam defects:
  - Cross-story finding 1: 3.7 (8e233695) wrote `GameVM::failure()` with `if (!failed()) return Failure::None;` first. The line carried no comment saying it also orders the reads after the acquire of `done`.
  - 3.10 (0520b146) moved the wording into the pure `GameScript::vmFailure(failed(), sessionOutOfMemory, game.hostFailure())` (`GameVM.h`). Arguments are evaluated before the callee's `if (!failed)`, so the guard was lost and VM-task fields were read on every `vmHealthy()` pass.
  - 3.10's intent said "behaviour identical" (plan :36). Its only triage row on `failure()` (#1, :124) defers the missing tests to e2 AI-2. The race was **missed, not dismissed**.
  - f27dcefd restored the guard with an ordering comment.
- This is the AGENTS.md Known pitfall ("Refactors of shared helpers have silently dropped earlier targeted fixes … read its history with `git log -L`"). `git log -L` on `failure()` shows exactly 8e233695 → 0520b146 → f27dcefd. 3.10's plan records no history read.
- Of the other seven cross-story findings, three were partly seen by their story and not finished [plans]:
  - finding 5 (3.9 #2: a comment, no test);
  - finding 6 (3.7 #12: the script fixed, the doc not);
  - finding 7 (3.7's plan noted "plus alignment padding").
  - The other four were not raised at all.
- This retro's diff-scope review found one more seam defect that only the combined diff shows: R1 (3.2's image op × 3.1's display-list limits). It also found three low items visible in single stories (R3, R5, R6).
- f27dcefd itself had no independent review. Its fix 4 (`addImage`'s read-error path) is build-verified only (`cross-story-review.md`, finding 4).
- The cross-story review is not a written step in `docs/crosshatch/orchestrated-epics.md`. The orchestrator ran it on its own initiative, after entries 9 and 10 had landed.
- Instance: **accept** (fixed in f27dcefd). R1 is routed below.
- Prevention: AI-4. Make the cross-story review a written pre-PR step, with the review of its own fix commit. Add to the brief: a story that moves or rewrites an existing function runs `git log -L` on it and states in Design Notes what each guard in it is for.

**O3. The flash cost was quoted against a recorded figure from a different gate method. "Measure before quoting" was obeyed and still let a wrong figure reach the owner.** Medium (process).
- Every figure was a measurement with its method, as the rule asks. The error was in what it was subtracted from:
  - Entry 4's verify said to measure "at the entry's base commit and at its commit … beside epic 2's +150,448 B" (`tickets.toml:46`). R10 says "against epic 2's +150,448 B" (`epic-icon-library.md:34`).
  - 3.7 changed the gate's method in the same story: F5 now counts IRAM (`check_flash_budget.py:86-91`). It then subtracted: "+196,336 B … epic 2 closed at +150,448 B, so this epic adds +45,888 B; the base `1eacdc77` was not re-measured" (3.7 plan :223-225).
  - That figure went into `docs/crosshatch/game-icons.md` (8e233695) and into the owner's review packet (38000ed2, README :36-37).
  - The Notes told the owner "+776 B of the 1,024 B gate … leaving 248 B for later epics" as this epic's cost (:113).
- The correction:
  - f7be3b4a re-measured the base with entry 7's gate: +151,088 B flash and +776 B static RAM. Like for like, this epic added +45,248 B up to entry 7, not +45,888 B, and +0 B of static RAM (Notes :121).
  - The final figure is +76,624 B flash (+227,712 − 151,088; README :32-36).
- The RAM gate had under-reported epic 2 since epic-script-runtime. It recorded +8 B, counting `.dram0.*` and `.noinit` only. With IRAM counted, epic 2's FreeRTOS task functions took +768 B (README :42-44; `game-icons.md`, the 659 + 25 B breakdown in f27dcefd).
- Two provenance defects remain:
  - The measurement tree cited for entry 7, `965c55d7`, does not exist: it was amended into 8e233695. It is still cited at `review-packet/README.md:33`, `docs/crosshatch/game-icons.md:186,199`, and the 3.9 plan :111.
  - Entry 9's verify again measured "against entry 7's +196,336 B" (`tickets.toml:110`), a recorded figure. That one happened to use the same gate.
- Is the rule strong enough? **No.** "A measurement with its method" does not say that both sides of a delta must be measured the same way.
- Instance: **fix now** (AI-7: R10's wording and the dead hash; the spine row, S2).
- Prevention: AI-5. A delta is two measurements by the same tool version, one at each commit, never a measurement minus a recorded figure. A story that changes a gate's method re-measures the epic base under the new method. The orchestrator measures the epic base before the first story. A measurement cites a commit that exists on the branch.

**O4. The entry-8 scope change turned into one extra story that rebuilt most of the icon set. The spec, not the builders, fixed the names that were changed. An early owner checkpoint on the API names would have avoided most of the rework.** Medium (process).
- What 3.9 redid [plans]. 9197d046 touches 158 files (+5,442 / −2,533):
  - all 62 of 3.4's prefixed names renamed;
  - per-icon weights replaced by both weights;
  - the chess pieces and 3.4's two original drawings removed (`assets/game-icons`: 54 files added, 6 deleted);
  - 3.1's name grammar and generator changed;
  - 3.5's and 3.6's names updated;
  - the review packet cleared and rebuilt (dc167d9c, 0ef5a900).
  - Flash rose +31,072 B, and the owner raised the icon cap from 48 KiB to 96 KiB (Notes, flash option A).
- Where the replaced names came from: the owner-approved sources.
  - `game-api-seed.md` §5 at the base: "Names are lowercase with `_`, such as `mark_x`, `suit_spade`, `die_6`, `piece_king`" (:163).
  - AD-24: "Icon names are lowercase `snake_case`" (spine :366).
  - Entry 1's description named `mark_x`, `suit_heart`, and `die_6` (`tickets.toml:7`).
  - The owner's own dated Notes decisions used those names (:70, :72).
  - A builder had nothing to ask: the naming question was settled until the owner saw the result. 3.4's only open points were weight style and outlined pieces (plan :142, :162).
- Why the answer came late:
  - The owner decided "the epic runs overnight, so owner input sits at its end. Entry 4 builds the v1 set without waiting for approval" (Notes :82).
  - Icon names were listed as reversible choices the orchestrator could settle (:83).
  - 3.4 finished at 07:34; the answers came at 12:28 (87dae8ea). Everything from 3.5 onward was built on names that were then replaced.
- Would asking earlier have avoided the rework? **Mostly yes, if the question had been asked.**
  - After the tracer (3.1, 05:25), four names and one weight per icon were visible in a screenshot.
  - A one-line question then — "Phosphor's own names, or category names? One weight or both?" — would have let 3.4 build the set once.
  - It would not have avoided the cap increase: both weights cost the flash either way.
- "Reversible inside the PR" (Notes :83) was true, but the cost of reversing grew with every story built on the names.
- Instance: **accept**. The owner chose it knowingly, and 3.9 was clean: 16 triage rows, green CI.
- Prevention: AI-6. In an unattended epic, an API-visible choice that later stories will multiply (names, a list, an argument shape) gets an asynchronous owner checkpoint right after the tracer. Other lanes keep running.

**O5. Environment friction: the certifi step existed in AGENTS.md but was not applied before the first story; one CI flake; and two local-tool gaps AGENTS.md does not mention.** Low.
- (a) 3.1: "`sticky` and `default` first stopped before compiling (the ESP-IDF venv setup's `uv` rejected pypi.org, `UnknownIssuer` …); after the orchestrator fixed the penv certifi bundle, both built" (3.1 plan :191; 4018b825).
  - AGENTS.md already had this step since e2 AI-12.
  - `x4pro` built first, so the gap surfaced mid-story.
  - Every later brief said "Toolchain installed; do not reinstall or touch certificates" (3.2 :55, 3.5 :52, 3.7 :57, 3.9 :81, 3.10 :61), and it did not recur.
- (b) CI, run 36423204077, job 108930737455 (`cppcheck`, attempt 1, on `f7be3b4a`) [ci].
  - Step "Install PlatformIO Core" failed: `Request failed after 3 retries in 132.5s … https://pypi.org/simple/click/ … operation timed out`.
  - It died before any check ran; attempt 2 passed.
  - The step is upstream's `ci.yml` (:114-123), which the fork never edits.
  - The one re-run was within policy (a job that died before any test body ran).
- (c) Local `pio check`: "The packaged cppcheck needs `libpcre.so.3`, which the container lacks; it ran with `LD_LIBRARY_PATH` at a copy extracted (`apt-get download libpcre3`, `dpkg-deb -x`)" (3.7 plan :210-212). AGENTS.md does not say this.
- (d) `./bin/clang-format-fix` formats only files `git ls-files` lists (`bin/clang-format-fix:45`), so an untracked new file is skipped in both modes. 3.10 needed a temporary `git add -N` (plan :112-113). AGENTS.md says only "`-g` skips staged and new files", which implies the no-argument run covers them.
- Instance: **accept** (a), (b). **Fix now** (c), (d): AGENTS.md lines (AI-8).
- Prevention: AI-5's base-measurement step builds all five envs on the epic base before the first story, which also proves the toolchain.

**O6. Flash headroom: 28,288 B of the 250 KiB gate and 248 B of the static-RAM gate remain for five epics. epic-install-and-launcher has no budget line.** Medium (planning).
- Measured: +227,712 B games on minus off at f27dcefd, leaving 28,288 B of 256,000 B. Static RAM is +776 B of 1,024 B (`cross-story-review.md`, Verification; README :37-40). CI's `x4pro flash budget` passed on the head.
- Nothing downstream plans for it:
  - `epic-install-and-launcher.md` has no flash or KiB line [plans]. Its scope — installer, registry, `.pkg`, SHA-256 helper, launcher, mode picker, `resume.bin` — is fork code of **unmeasured** size.
  - The upstream libraries it consumes are already referenced by upstream code: `ZipFile` and `PngToBmpConverter` by `lib/Epub`, and `src/network/FirmwareFlasher.cpp`. So their own cost should mostly not count, **unmeasured**.
  - pass-and-play and play-nearby follow, and play-nearby adds ESP-NOW. The spine's RAM row puts its `GameLink` stack in internal RAM (spine :537).
- The one lever the owner reserved is compression, "a later, API-neutral option" (Notes :118). Measured on the committed header's 220 bitmap arrays (70,400 B; a data-size measurement, not a firmware delta):

  | Encoding | Data bytes | Saving | Note |
  |---|---:|---:|---|
  | as committed | 70,400 | — | 14,080 B of 32 px + 56,320 B of 64 px |
  | PackBits, per bitmap | 51,776 | 18,624 | tiny decoder |
  | zlib level 9, per bitmap | 21,315 | 49,085 | needs an inflater. Upstream vendors `lib/uzlib` and `lib/miniz`; whether either is already linked into x4pro is unmeasured |
  | dropping the 32 px bitmaps | 56,320 | 14,080 | changes rendering; not API-neutral in appearance |

  Method: a Python pass over `lib/GameIcons/GameIcons.generated.h` at `d719a379` (scratchpad).
- What epic-install-and-launcher needs:
  - (1) Measure its own delta at its tracer, games on minus off, both at the tracer's base and at its commit, per AI-5.
  - (2) A budget line in its Notes that splits the 28,288 B among it, pass-and-play, and play-nearby.
  - (3) A trigger for the compression story, taken from the measured need rather than quoted.
- Instance: **fix now** (AI-9, at that epic's inception).
- Prevention: the spine's Flash budget row records each epic's figure, as it says it should ("each epic records its delta"). Today it records none past epic 1 (S2).

### Diff-scope review, across tickets [rev]

**R1. A frame can hold 2,048 full-canvas `ch.gfx.image` commands, and replay has no cost bound.** Medium.
- Evidence:
  - An `Image` command is 8 B (`DisplayList`), so the 32 KB / 2,048-command limits allow 2,048 of them.
  - `FrameReplay` fills each image with one 1-px `fillRect` per colour run (`FrameReplay.cpp:166-180`; `GameImageBlit.h`).
  - A 480×800 checkerboard is a legal image: 62 + 15·4·800 = 48,062 B, under the 131,072 B budget. It gives 384,000 runs per command. Reproduced on host: 38.4 M runs for 100 commands (`scratchpad/review/cost/`).
  - One frame of such commands is about 786 M `fillRect` calls on the render task, under `RenderLock`. The 3 s watchdog covers Lua, not replay. Changing one byte a frame defeats the identical-frame skip.
  - The device time is PLAUSIBLE, not measured.
- The same holds for icons, on their own code path. A 10 B `Icon` op allows 2,048 large icons a frame, each walking 128×128 = 16,384 `inkAt` tests plus its run fills (`FrameReplay.cpp:161-164`, `GameIconBlit.h:59-85`). That is about 33.5 M pixel tests a frame, estimated [rev, second adversarial run].
- Boundary: 3.2 (the op) × 3.1 (the display-list limits).
- 3.2's deferral (`deferred-work.md`, `## 3.2`, "Review finding 10, unverified; medium if true") and the epic-install-and-launcher Notes :57 cover only one dithered image, "and a frame of several". Neither names the 2,048× multiplier.
- Instance: **fix now, before API level 1 freezes** (AI-10). A per-frame limit on image and icon pixels, raised through the guard like a full frame, is new API surface. Alternatively, a row-blit path that makes replay cost proportional to bytes.
- Prevention: a new display-list op states its worst-case replay cost against the frame limits in its plan.

**R2. Play again can still show the last round's frame through Pause or Resume.** Low.
- Only `loopPlaying` checks `roundsStartedAwaited` (`GameMatchActivity.cpp:304-306`). `renderView` and `renderCanvas` draw `vm->drawFront` without it.
- This is 3.7's deferred R3 residual (`deferred-work.md` `## 3.7`, (b)), confirmed by reading. The reviewer adds overlay repaints to the paths.
- Instance: **accept** the existing deferral, which goes to e2 AI-2's harness (epic-install-and-launcher Notes :52).

**R3. `failedToStart`'s contract text misstates which failures come "before any game code ran".** Low.
- `VmFailure.h:22-24` says NoSession only. A `LuaGame::load` scratch or `lua_newstate` out-of-memory also fails before game code runs, yet takes the ordinary error headline.
- The behaviour is unchanged from the base: the old `failedOutOfMemory` was session-only. The comment and `VmFailureTest` pin a contract the prose misstates.
- The adversarial lens adds `NotLoaded` ("game not started"), which is defensive and unreachable today (`VmFailure.cpp:57`).
- Instance: **defer** to the next sweep (AI-11): reword, or widen `failedToStart` if the owner wants "could not start" for those.

**R4. `GameVM::errorMessage()` and `failureDetail()` still read VM-task state without the `failed()` gate.** Low, latent.
- `errorMessage()` reads `sessionOutOfMemory` directly (`GameVM.h:113`).
- Every current caller runs it only after `failed()` (`GameMatchActivity.cpp:241, 253`), so there is no race today. f27dcefd guarded `failure()` only.
- Instance: **defer** (AI-11): gate it the same way, or document "only after `failed()`".

**R5. An over-long `.bmp` name is skipped with no log line.** Low; PLAUSIBLE.
- A name that does not fit the buffer (`fits == false`, `GameAssets.cpp:96`) skips the module and image branches. Its truncated name then fails `looksLikeImage`, so nothing is logged. This contradicts the approved rule that a misnamed `.bmp` "is skipped with a log line".
- Instance: **defer** (AI-11).

**R6. A manifest `icon` accepts names the library can never hold, and nothing reads it yet.** Low.
- `validIcon` accepts `[a-z0-9_-]{1,32}` (`Manifest.cpp:37-48`), including a leading digit, `_`, and `--`. The generator's grammar is `[a-z][a-z0-9-]{0,31}` with no `--` and no trailing `-` (`gen_game_icons.py:60,114`).
- No code under `src/` reads `Manifest::icon` [rev], so a bad name is found only when the launcher draws it.
- `_` is kept on purpose (`ManifestTest.AcceptsUnderscoreInIcon`).
- Instance: **defer** to epic-install-and-launcher, whose Notes already put the library check outside `GameCore` (:50, :56). Decide the manifest grammar before the freeze.

**R7. No producer yet writes the only image layout the loader accepts.** Low, by design; a handoff gap.
- `checkImageHeader` (`GameImages.cpp:62-85`) accepts only `PngToBmpConverter`'s exact layout: top-down negative height, `colorsUsed == 2`, `imageSize` filled in, and a black-then-white palette. That is what R5 specifies.
- Nothing in `src/` or `scripts/` writes it for a game yet. The installer is epic-install-and-launcher's.
- Until then, only the hand-built fixtures draw. An ordinary 1-bit BMP from a common tool, which writes bottom-up rows with `colorsUsed 0`, would start the game as `BadImage`. That tools do this is PLAUSIBLE; no such file was generated.
- The handoff (`epic-install-and-launcher.md:50,53`) covers reading `icon.bmp` and counting the budget. It does not say that the installer and `pack_game.py` must pass *every* package image through the converter, including a supplied `.bmp`.
- Instance: **defer** to that epic, with the handoff made explicit (AI-12).

**R8. Other low items from the adversarial lens.**
- (a) **`GameAssets::load`'s frame is probably over AGENTS.md's 256 B locals rule.** The named locals come to about 290 B on 32-bit: `path` 96, `name` 48, `stem` 33, two `ImageBudget`s, an `ImageHeader`, size_t counters, and three `HalFile` objects (`GameAssets.cpp:63-228`). The same function was about 220 B at the base. PLAUSIBLE by arithmetic; no `-fstack-usage` build. It overlaps A3.
- (b) **`lib/GameIcons/.clang-format` (`DisableFormat: true`) covers the hand-written `GameIcons.h` too**, not only the generated header. R2 asked for the formatter to be off for the generated file.
- (c) **The vendored SVGs are not pinned by content.** There is no checksum list. An edited file at a Phosphor path regenerates cleanly and passes `Icons up to date` (`gen_game_icons.py:119` checks paths only).
- (d) **The Games tab duplicates upstream's `for (int i = 0; i < 5; ++i)` inside an `#if`/`#else`** (`CoverGridHomeUi.cpp:283-287`, confirmed in the diff). Guarding only the extra `ICONS` entry and deriving the count would leave the upstream line untouched (AGENTS.md: minimal upstream diff).
- (e) **Row-icon ink follows `foreground.color != White` and ignores the paint type** (`GameMatchActivity.cpp`, `drawViewIcons`). A theme with a dithered focused row may hide the icon. PLAUSIBLE.
- (f) **Regeneration off Linux may differ** by libm `sin`/`cos`/`atan2` in arc flattening (`gen_game_icons.py:239`). It was byte-identical on Linux for Python 3.10–3.13 only. PLAUSIBLE.
- Rejected:
  - "Too many images shows the damaged-or-too-large text": an approved 3.2 assumption.
  - "`#ifdef` accepted by the layer check": off builds leave the flag undefined, which `check_flash_budget.py` enforces.
- Instance: (a) **measure, then fix if over** (AI-11); (b)–(f) **defer** (AI-11). (d) is also a ledger-hygiene item for the next touch of row 8.

**R9. From the second adversarial run** (the reviewer's orphaned backup lens). Each item was re-checked by this retro where marked.
- (a) **Taps during the Play-again gap reach the new round as game input.** Low to medium; PLAUSIBLE by reading, re-checked.
  - `loopPlaying` reads a gesture and calls `vm->postInput` (`GameMatchActivity.cpp:297-300`) *before* the `roundsStarted() < roundsStartedAwaited` gate (:306).
  - `requestPlayAgain()` clears the queue only once, at the tap. A second tap while the e-ink screen still shows the Over dialog is posted after the clear and popped by the new round, at the button's coordinates.
  - This is the case e2's b64f455d fixed for taps queued *before* Play again, now reopened for taps *after* it, across 3.7's gate (8e233695) and 2.13's clear.
  - Fix: drop gestures while the gate holds; keep `pollTimer()`.
  - Not reproduced in the simulator.
- (b) **The layer check accepts an unguarded include behind a backslash-continued `#if … && \` / `… || 1`.** Low; reproduced by the reviewer on a host copy of `games_guarded_lines`. `games_branch()` reads only the directive's first physical line (`check_layers.py:391-421`). The script's docstring promises an unusual guard "fails visibly".
- (c) **`sim.sh setup` can still drop the user's settings.** Low; re-checked. The guard tests only that the end marker exists somewhere (`sim.sh:42`), not that it follows the begin marker. With `END … BEGIN …`, the `awk` drops everything after `BEGIN`. The reviewer reproduced this with the same `awk`. It is 3.10's own check, and SKILL.md claims setup stops instead.
- (d) **Level 1 does not state the icon sizes or the ink rule.** Low now; it matters at the freeze. Re-checked.
  - `api-level-1.txt` has `enum size small|medium|large` (:195-197) but no pixel sizes (32/64/128), and no "icons draw ink only, images draw opaque".
  - The image-name grammar exists only as a comment.
  - `api-level-1.txt:46` itself says a rule games rely on must become an entry before the freeze.
  - A related gap, speculative until the installer exists: there is no `ch.gfx.image_size(name)`.
- (e) **The build anchor's comment overstates what off builds compile.** Low; re-checked. `GamesBuildAnchor.cpp:3-8` says every env compiles "header-only lib/GameIcons's headers here", but the file is inside `#if FREEINK_CAP_GAMES`. So `GameIcons.h`'s `static_assert(strictlySorted())` never runs on the C3, x4c, or papermono builds.
- (f) **A Lua heap exhaustion is not worded like a load-time one.** Low. A `LUA_ERRMEM` or heap-cap fault during a callback is a `Script` failure with Lua's English "not enough memory" (`LuaGame.cpp:327,332`). The same condition in `load()` shows the translated `STR_GAMES_OUT_OF_MEMORY`. AD-14 allows Lua's own message untranslated, so this may be by design; `game-canvas.md` does not say so.
- (g) Speculative, not re-checked:
  - the 64 px view icon is always black, while row icons follow the style (with R8 e);
  - an unknown-name error can pass a lone UTF-8 lead byte into the error view (`ChBindings.cpp:147-158`).
- Instance: (a), (b), (c), (e), (f), (g) → **defer** (AI-11); (a) first, since it is a contract-visible input bug. (d) → the freeze prerequisites (AI-2).

**Verification gaps.** No host test compiles `GameAssets.cpp`, `FrameReplay.cpp`, `GameIconDraw.cpp`, `GameVM.cpp`, `GameMatchActivity.cpp`, `HomeActivity.cpp`, or `CoverGridHomeUi.cpp` [rev]. So the following are checked only by simulator screenshots:
- the image loader's two passes and f27dcefd's `readFailed` path (`LuaGameFixture::useImages` reimplements the loader);
- `FrameReplay`'s `Icon`/`Image` dispatch, including the fill weight and the 128 px index;
- the Play-again gate;
- `vmHealthy`'s composition (`VmFailureTest`'s header claims it; the test calls only the pure functions);
- `drawViewIcons`;
- the cover-grid tab order (the `static_assert` checks only the count);
- `sim.sh check`'s failure branches.

The e2 AI-2 debt grew in this epic. The handoff at `epic-install-and-launcher.md:52` omits three of its items [plans]: the `vmFailure` call sites (`## 3.10`), the image-header read-error path (f27dcefd), and 3.9's weight replay (`## 3.9`). AI-12 fixes the handoff.

**Checked and clean** [rev]:
- **VM ordering after f27dcefd:** `busy()` and the round counters are atomics; `LuaGame::close()` does not reset `error` or `hostFailed`.
- **The `Icon` and `Image` ops count toward both frame limits through `reserve()`.**
- **Weight:** the default is `regular`. A bad weight is an argument error, like a bad size. An unknown name goes through `guard->raise`.
- **Icon and image runs** never leave the canvas, at x, y ∈ {−32768 … 32767} with the 128 px fill icon, under ASan/UBSan on host.
- **The image header parser:**
  - int64 dimensions, including a height of `INT32_MIN`;
  - an exact size match and a palette `memcmp`;
  - no budget wrap, and no off-by-one in the 32-image cap;
  - 4-byte alignment of spans;
  - `icon.bmp` excluded, and a 0-byte file is `Truncated`.
- **The Home tab order** matches `indexToMenuItem`, with and without OPDS. Off builds keep five tabs.
- **The name → identifier mapping is injective.** The `str.encode` sort matches the binary search's byte compare, and `strictlySorted` is a `static_assert`.
- **`Icons up to date` fails closed**, and the generator is byte-identical under Python 3.10–3.13.
- **Every f27dcefd fix is complete and correct.** Only fix 1 has a residual (R4).

### Aggregate views [agg]

**A1. Architecture delta: clean.**
- There are two new component edges:
  - `lib/GameScript → lib/GameIcons` (`ChBindings.cpp`, b562ad3c), already allowed at the base;
  - `src/components/CoverGridHomeUi.cpp → lib/GameIcons` (39467f07). It is added to the spine's new "Upstream hooks" row and to `UPSTREAM_EDGES` in the same commit, as R10 requires.
- `check_layers.py` passes: 362 edges in 92 files.
- There are no file-level cycles, at the base or the head.
- `GameCore` still reaches only `JsonParser`, `Memory`, and the standard library. `GameImages` added none.
- No file under `src/activities` names `GameIcons`.
- Divergence: the spine's layer table calls the icon data "nothing (generated data only)" (spine :33), and the Structural Seed describes `GameIcons.generated.h` alone (:482). `GameIcons.h` holds hand-written `compare`, `find`, and `static_assert` code (:22-72), an approved 3.1 assumption. → S5.

**A2. Duplication.** Low.
- Two copies of the clip computation:
  - `GameIconBlit::inkRuns` (`GameIconBlit.h:56-84`) and `GameImageBlit::runs` (`GameImageBlit.h:30-57`) compute the same int64 clip;
  - their bit readers are near copies.
- The 1-bit BMP layout lives in three places:
  - the writer, `PngToBmpConverter.cpp:125-150`;
  - the reader, `GameImages.cpp:9-25`;
  - a test mirror, `test/game_core/ConverterBmpLayout.h`, marked "keep it in step" but with no automated link to the converter.
- Three name grammars:
  - icons, `[a-z][a-z0-9-]`;
  - the manifest `icon` (R6);
  - image and module stems, where `imageNameOf` and `moduleNameOf` duplicate one stem loop (`GameAssets.cpp:25-42`, `GameImages.cpp:102-121`).
- e2 A4 is only partly fixed:
  - `src/games/GamePaths.h` now holds `/.games` and its 96-byte path size (8e233695);
  - `/.games-data/` is still a literal with its own `PATH_BYTES = 64` (`GameSaveStore.cpp:26-28`, `GameSaveStore.h:53`).
- The icon sizes are repeated:
  - `GameViewIcons.h:12,14` repeats 64 and 32, pinned by a test;
  - `CoverGridHomeUi.cpp:300` sets `tabs.iconSize = 32` with no check against the `_32` bitmap.
- Instance: **defer** (AI-11). Share the clip helper. Link `ConverterBmpLayout.h` to the converter with a host test that runs `PngToBmpConverter`, if it builds on the host (3.2's unknown). Move `/.games-data` into `GamePaths.h`.

**A3. Size growth.** Low.
- `scripts/gen_game_icons.py` (544 lines) is one pipeline with six concerns: the map reader, SVG and path parsing, curve flattening, the scanline rasterizer, the packer, and the header writer.
- `GameAssets::load` is one function of about 165 lines (`GameAssets.cpp:63-227`). Its arrays and structs come to about 237 B before scalars. That is close to AGENTS.md's 256 B locals rule; the 62-byte header was already moved into a `noinline` `addImage` to stay under it.
- `GameMatchActivity.cpp` grew to 546 lines and still owns lifecycle, supervision, gestures, and views (e2 A3).
- Instance: **defer** (AI-11). Split `GameAssets::load`'s two passes before the installer adds to it.

**A4. Pattern divergence: clean.**
- No bare `new` or `make_unique`, `Serial`, SdFat, `HalGPIO::BTN_`, `RenderLock` in a destructor, or mutable statics.
- The one allocation (`GameAssets.cpp:151`) is null-checked.
- Both new strings go through `tr()`.
- Every fork script uses `fork_common`'s exit codes (e2 A4 fixed).
- New files are guarded like their neighbours.
- Every upstream-shared file touched is a ledger row:
  - `.gitignore` and `AGENTS.md`;
  - `english.yaml` (row 2);
  - `HomeActivity.*` (rows 6–7);
  - `CoverGridHomeUi.*` (rows 8–9).

### Spec-to-implementation reconciliation [agg]

| Req / Done when | As built | Evidence |
|---|---|---|
| R1 | Met: 110 SVGs (55 names × regular and fill), Phosphor's licence, `names.txt` with 110 lines, attribution | `assets/game-icons/`; `docs/crosshatch/game-icons.md` |
| R2 | Met: standard-library rasterizer, `inline constexpr` header, `.gitignore`, `.clang-format`, AGENTS.md narrowed, `Icons up to date` | `gen_game_icons.py:62-67`; `crosshatch-ci.yml:260-287` |
| R3 | **Met, with drift (S3):** 55 Phosphor names. "Game pieces" is now only `boat`. The spine and game-api-seed still say "board pieces" and name no status category | `game-icons.md:67-121`; spine :372; seed :163 |
| R4 | Met: small, medium, and large (the 64 px bitmap at scale 2); optional weight; ink only; clipped; one command | `GameIconBlit.h`; `ChBindings.cpp:163-176`; `GameIconBlitTest`, `GfxBindingsTest` |
| R5 | Met: `IMAGES_BYTES = 131,072` counts whole files, `icon.bmp` excluded. Also `MAX_IMAGES = 32`, which R5 does not name (an approved assumption). Beyond R5: R1 | `GameImages.h:15,17` |
| R6 | Met: `guard->raise` for both. The error view itself is screenshot-only | `ChBindings.cpp:157`; `…StopsTheGameEvenUnderPcall` tests |
| R7 | Met: both `fn` lines, `enum weight`, two `limit images_*` lines, 55 `icon` lines; `API_SURFACE_CRC 0xCF6FE64E`; `IconsMatchTheList` both ways | `api-level-1.txt`; `ApiLevel.h:20`; `ApiSurfaceTest.cpp:365` |
| R8 | Met: `pause`, `flag-checkered`, `warning`; rows `play`, `sign-out`, `arrows-clockwise` | `GameViewIcons.h:17-51` |
| R9 | Met, as a cover-grid **tab** (R9 says "tile"); an approved 3.6 assumption | `CoverGridHomeUi.cpp:273-300` |
| R10 | Edge rule met; RAM +0 B from this epic. **Wording stale (S1):** "against epic 2's +150,448 B" | epic :34 vs Notes :121 |
| R11 | Met: each e2 AI-11 item fixed, documented, or deferred with the owner's sign-off | 3.7 plan; Notes; Previous-retro follow-through |
| Done when 1–5 | See Acceptance verdict | — |

**Spec drift to reconcile** (AI-7):
- **S1.** R10 and the Notes still name epic 2's +150,448 B as the comparison. The like-for-like base is +151,088 B.
- **S2.** The spine's Flash budget row still says "icons about 40 KB for 64 icons at two sizes" (:536). The measurement is 71,975 B for 55 names × 2 weights × 2 sizes. The row records no epic's figure past epic 1.
- **S3.** The spine and game-api-seed name the categories "board pieces" with no "status". R3, Done when 1, and `game-icons.md` say "game pieces … status icons", and the category holds only `boat`.
- **S4.** R9 says "tile"; it is a tab.
- **S5.** The spine's layer table and Structural Seed say `lib/GameIcons` is generated data only.
- **S6.** `965c55d7` is cited as entry 7's measured tree in `game-icons.md` and the review packet. It does not exist; entry 7 landed as 8e233695.
- **S7.** Behaviour added without a requirement, each recorded as an approved assumption:
  - a manifest `icon` accepts `_` and `-`;
  - a misnamed `.bmp` is skipped with a log line;
  - row icons are drawn only when the label fits;
  - `STR_GAMES_BAD_IMAGE` also covers more than 32 images;
  - one Home index mapping serves both modes.
  - Record, no change.

### What the evidence confirms went well

- **The epic PR merged green**: 19/19 check runs on the head, including the new `Icons up to date` job. Epic 1's P1/P2 (merging red) has not recurred for two epics.
- **e2 AI-1 held.** Every non-`hitl` story ran its lenses as context-free subagents. The cross-story review on top of that caught a medium race before merge.
- **Upstream drift stayed inside the ledger.** The only new upstream edge (row 9) landed with its layer-table row and check-script rule in one commit. `Layer check` and the ledger job were green.
- **The owner's overnight design worked for everything reversible.**
  - 27 assumptions were recorded as dated Notes lines, and the owner answered all of them (review-packet README).
  - The dated Decision lines change four (11, 13, 14, 15) and supersede 17's names. The rest stood.
  - Entries 9 and 10 were added and built the same day.
  - The review packet let the owner review 55 names × 2 weights × 3 sizes × 2 inks from screenshots alone.
- **The generator is deterministic across Python versions**, and the byte-identical CI check makes a hand edit of the header impossible to merge.
- **The previous retro's hardening list was cleared in one sweep story** (3.7), as planned, with each item's disposition recorded.

## Behavior verification

The epic changed runtime behaviour: two bindings, the image loader, three views, and the Home tab. This retro exercised the flows below on `d719a379` (a subagent ran them; this retro looked at the key screenshots itself). Artifacts are in the scratchpad `behave/`.

| Flow | How | Observed |
|------|-----|----------|
| Host suites | `cmake`/`ctest` as AGENTS.md gives them | **707/707 pass**, including `VmFailureTest` (6), `GameIconBlitTest` (9), `GameImageBlitTest` (6), `GameImagesTest` (14), `GameViewIconsTest` (9), `ApiSurfaceTest.IconsMatchTheList`, and `ManifestTest.AcceptsUnderscoreInIcon` |
| Fork script tests | each `python3 scripts/*_test.py` | all 8 pass: 9, 78, 49, 18, 27, 76, 22, 23 tests |
| Done when 1: regeneration | `gen_game_icons.py --out` twice, `cmp` against the committed header; again under Python 3.10, 3.12, and 3.13 | byte-identical every time (466,256 B, "55 icons, 2 weights each") |
| Counts | `names.txt`, `api-level-1.txt`, the SVG folders | 55 names; 110 map lines; 55 `icon` lines; 55 + 55 SVGs; the three sets agree |
| Layers | `check_layers.py` | pass, 362 edges in 92 files |
| Done when 4: cover-grid Home | simulator x4pro (`sim.sh build x4pro`, SUCCESS). Settings → UI Theme → Cover Grid | the tab bar reads folder, library, transfer, **game-controller**, settings. A tap opens the Games list |
| Done when 2: icons | `icons` fixture, pages 1, 2, and 21 in black; page 1 in white | each name in regular and fill at 32 px, at 64 px over a dithered band, and at 128 px; labels are Phosphor names (`x`, `circle`, `dot-outline`). White inverts. Screenshot `07-icons-p1-black.png` checked by this retro |
| Done when 3: images | `images` fixture | the badge and dot are opaque at native size in black; the inverse in white; clipped at the right edge |
| Done when 4: runtime views | Back in a game, then Leave | Paused shows the 64 px `pause` icon between the text and the rows, `play` on Resume, and `sign-out` on Leave. Leave returns to Games |
| Done when 2, 3: unknown names | `faults/unknown_icon.lua` and `unknown_image.lua` as games | the error view shows `main.lua:10: ch.gfx.icon: unknown icon "no_such_icon"` (and the image form), exactly as the fixtures README says, with the `warning` icon. No frame was published; Back returns to Games. Screenshot `15-unknown-icon.png` checked by this retro |
| Log | `sim.sh log` | the only ERR lines are the four expected script errors; no assert or panic |
| CI, Done when 5 | PR #17 | 19/19 green on `e6bed5f7` |

Narrowed:
- **No device run.** That was the owner's decision (Notes, "no closing device run"). R1's replay time and 3.2's dithered-image time are device-only and unmeasured.
- **Firmware builds and `pio check` were not re-run.** This retro did not rebuild the five firmware envs or run `pio check`. PR #17's CI covers them, as does the cross-story fix's local run (`cross-story-review.md`, Verification).
- **The flash figures are not independently re-measured.** This retro did not re-run `check_flash_budget.py`. It relies on CI's green `x4pro flash budget` and the orchestrator's recorded runs; the two provenance defects are in O3.
- **Cosmetic items, not findings:**
  - the error view's Back row shows both the `sign-out` icon and the label's own "«";
  - white icons are low-contrast on the fixture's medium band, which the fixtures README anticipates.

## Previous-retro follow-through

From `epic-script-runtime-retrospective.md`, Action items AI-1 to AI-13 and its Addendum. The Addendum records that AI-1, AI-5, AI-6, AI-7, AI-9, AI-10, AI-12, and AI-13 landed before this epic was planned. The rows below cover what this epic did with the items the Addendum left open, and check that the landed process items held.

| Item (owner) | Landed? | Evidence |
|---|---|---|
| AI-1: independent review for every story (owner, orchestrator) | **landed and held** | Nine plans record their lenses as context-free subagents (e.g. 3.1 :122-123, 3.10 :119); 3.8 is `hitl`. `docs/crosshatch/orchestrated-epics.md` step 3. A cross-story review ran as well. Gaps: f27dcefd and the orchestrator's doc commits had no review (O2); a lens can be missing while the log says "subagents" (O1) |
| AI-2: the `src/games` harness (dev loop, epic-install-and-launcher) | **not landed, by decision; the debt grew** | Owner decision, Notes :78: "stays with epic-install-and-launcher; this epic's new `src/games` logic is pure functions with host tests" (`ImageBudget`, `GameViewIcons`, `VmFailure`). New deferrals to it: `## 3.1`, `## 3.2`, `## 3.6`, `## 3.7`, `## 3.9`, `## 3.10`. The handoff at `epic-install-and-launcher.md:52` omits three of them (AI-12) |
| AI-3: device evidence for `loop` bands and abandon (owner) | **no evidence found** | No device run in this epic, by decision (Notes :80) |
| AI-4: bound sleep's time under `RenderLock` (dev loop) | **not landed** | Only documented: `GameMatchActivity.h:53-56` "bounding it is deferred (the epic-script-runtime retro's AI-4)"; 3.7 plan :100, #16 |
| AI-8: surface-test arity and the stale-base window, before the freeze (dev loop, epic-first-party-games; owner for the branch setting) | **no evidence found** | Not named in this epic or in the Notes of epic-install-and-launcher, epic-game-api-docs, or epic-first-party-games. Its trigger (the freeze, now in epic-first-party-games's last ticket) is recorded only in the previous retro (AI-13 here) |
| AI-11: one hardening chore (dev loop, "the sweep of the next epic") | **landed** in 3.7 (8e233695) | F5 fixed (`check_flash_budget.py:86-91`); F6 documented (:37); F7 fixed (`crosshatch-ci.yml:220-222`); F8 fixed (`api-level-1.txt:46`, the owner's decision); F9 fixed (`game_codec.py:93`); A3 → epic-install-and-launcher Notes :54; A4 fixed (`GamePaths.h`, one encode message, `fork_common` exits), **except `/.games-data`** (A2 here); A5 fixed (`STR_GAMES_NOT_LOADED`, the `static_assert`s); R3 fixed (`GameMatchActivity.cpp:168,306`), with the residual deferred (`## 3.7`, R2 here); R4 documented (`api-level-1.txt:58`) → epic-game-api-docs :52; R6 fixed (`LuaGame.cpp:163,178`); R8 → epic-game-api-docs :51; R9 fixed (`GameTimer.h:30`, `GameVM.cpp:163`); R10 documented (`game-canvas.md` "Overlays"), fix deferred → epic-install-and-launcher :55 |
| AI-12: rules file home and orchestration lessons (owner) | **landed and held** | Formatter last and twice: no `style:` commit in this epic. Worktree cleanup: no disk incident recorded. `merge=union`: `deferred-work.md` merged without a conflict commit. Archive trees for gates: 3.7 :216. Combined-tree host run: `cross-story-review.md` Verification. "Measure before quoting" was followed but was not strong enough (O3) |
| AI-5, AI-6, AI-7, AI-9, AI-10, AI-13 | **landed before this epic** (Addendum) and held | `Layer check` green and extended by 3.6 and 3.10. The every-fault README check reached `unknown_icon` and `unknown_image` (3.1, 3.2). The superseded-run pass: six `Crosshatch CI` runs on superseded heads show `cancelled` without turning the head red |

## Action items

All items are **proposed**; this retro applied none of them. The **Epic** column names where each is done:
- *remediation* goes to the dev loop as story-shaped work in that epic;
- *spec reconciliation* awaits the owner's application;
- *process* items are edits to AGENTS.md or `docs/crosshatch/orchestrated-epics.md`, given exactly under Proposed edits, and apply from the next orchestrated epic on.

| # | Kind | Action | From | Owner | Epic |
|---|------|--------|------|-------|------|
| AI-1 | remediation, carried | **Build e2 AI-2's `src/games` harness as that epic's first `src/games` ticket.** Scope: `deferred-work.md` `## 3.1`, `## 3.2`, `## 3.6`, `## 3.7`, `## 3.9`, and `## 3.10`, plus f27dcefd's image-header read-error path. The owner's decision to keep it there stands (Notes :78); this item fixes its scope | e2 AI-2; verification gaps | dev loop | epic-install-and-launcher |
| AI-2 | process, owner | **Write the level-1 freeze prerequisites where the freeze happens.** Add one Notes line to `epic-first-party-games.md` listing what must be settled before `API_LEVEL_FROZEN` flips: e2 AI-8 (surface-test arity, the stale-base window), e2 R4 (int16 coordinates; now in epic-game-api-docs Notes :52, which is not the freezing epic), R1's per-frame drawing limit, R6's manifest `icon` grammar, and R9 (d)'s icon pixel sizes, ink rule, and image-name grammar as entries (and whether `ch.gfx.image_size` belongs in level 1) | follow-through; R1; R6; R9 (d) | owner (Notes edit) | epic-first-party-games |
| AI-3 | process | **A hand-back with a subagent still running is not a finished report.** Add a Build-agent brief rule (wait for every lens and implementation subagent; list which lenses returned) and an orchestrator check (resume the same agent; never merge on it) | O1 | owner (doc edit), orchestrator | process, from epic-install-and-launcher |
| AI-4 | process | **Make the cross-story review a written pre-PR step**, including a review of its own fix commit. **Require a history read before moving or rewriting a function**, with each guard's purpose stated. Add the `failure()` case to AGENTS.md's refactor pitfall | O2 | owner (doc edit), orchestrator | process |
| AI-5 | process | **Strengthen "measure before quoting".** A delta is two measurements made the same way. A gate-method change re-measures the base. The orchestrator measures and records the epic base, and builds all five envs, before the first story. Cite only commits that exist | O3, O5 | owner (doc edit), orchestrator | process |
| AI-6 | process | **An early, asynchronous owner checkpoint for API-visible choices that later stories multiply**, right after the tracer | O4 | owner (doc edit), orchestrator | process |
| AI-7 | spec reconciliation | **Reconcile the epic and spine with the as-built:** S1 (R10's comparison figure), S2 (the spine's Flash budget row: the measured 71,975 B icon data, and each epic's recorded delta, epic 2 at +151,088 B like for like, epic 3 at +76,624 B), S3 (category names in AD-24 and game-api-seed §5), S4 (R9 "tab"), S5 (the GameIcons layer-table row and Structural Seed: a hand-written lookup over generated data), and S6 (replace `965c55d7` with the tree that exists: 8e233695's code, or say "measured before amend" at `game-icons.md:186,199`, README :33, and the 3.9 plan :111) | O3, S1–S6 | owner, via `bmad-architecture` update and an epic-file edit | reconciliation, before epic-install-and-launcher starts |
| AI-8 | process | **AGENTS.md environment lines:** the `libpcre.so.3` workaround for local `pio check`, and that `clang-format-fix` skips untracked files in both modes | O5 (c), (d) | owner (AGENTS.md edit) | process |
| AI-9 | remediation, planning | **Give epic-install-and-launcher a flash and static-RAM budget line before its first story.** State the 28,288 B and 248 B left (measured at f27dcefd) and the share each remaining epic may use. Its tracer measures its own games-on-minus-off delta per AI-5. Name the trigger for the icon-compression story (the owner's reserved lever; PackBits −18,624 B and zlib −49,085 B of data, measured, firmware cost unmeasured) | O6 | owner (Notes), orchestrator | epic-install-and-launcher inception |
| AI-10 | remediation, fix before the freeze | **Bound the replay cost of `ch.gfx.image` (and `icon`) per frame.** Either add a per-frame drawn-pixel limit raised through the guard like a full frame (a new `limit` line in `api-level-1.txt`), or a row-blit path that makes cost proportional to bytes. Then time a worst-case frame on an X4 Pro alongside the existing dithered-image device run | R1 | dev loop; owner for the device run | epic-install-and-launcher (with its image device run), and in any case before the freeze in epic-first-party-games |
| AI-11 | remediation, defer | **The next sweep's hardening list:** R9 (a) first (drop gestures while the Play-again gate holds, with a test once AI-1's harness exists); R9 (b) (join continued `#if` lines in `check_layers.py`, with a sidecar case); R9 (c) (require `END` after `BEGIN` in `sim.sh check`/`setup`); R9 (e) (correct the anchor comment, or include `GameIcons.h` outside the `#if`); R9 (f) (decide and document heap-exhaustion wording); R9 (g); R3 (`failedToStart` wording, `NotLoaded`); R4 (gate `errorMessage()` on `failed()`); R5 (log over-long `.bmp` names); R8 (a) (measure `GameAssets::load` with `-fstack-usage`, split its passes if over 256 B, with A3); R8 (b) (narrow `DisableFormat` to the generated header); R8 (c) (a checksum list for the vendored SVGs, checked by `gen_game_icons_test.py`); R8 (d) (restore upstream's loop line in `CoverGridHomeUi.cpp` at the next row-8 touch); R8 (e); R8 (f) (document "regenerate on Linux", or remove libm from arc flattening); A2 (one clip helper; `/.games-data` into `GamePaths.h`; tie `ConverterBmpLayout.h` to the converter) | R3–R5, R8, R9, A2, A3 | dev loop | the sweep of epic-install-and-launcher |
| AI-12 | process, owner | **Complete the handoff lines in `epic-install-and-launcher.md`:** add to :52 the `vmFailure` call sites (`## 3.10`), the image-header read-error path (f27dcefd), and 3.9's weight replay (`## 3.9`). Add to :53 that the installer and `pack_game.py` pass *every* package image through `PngToBmpConverter`, a supplied `.bmp` included, since the loader accepts only that layout (R7). Add R1's multiplier to :57's device run | R7, R1, verification gaps | owner (Notes edit) | epic-install-and-launcher |
| AI-13 | remediation, carried | **Carry e2 AI-4 (bound sleep's time under `RenderLock`) into epic-install-and-launcher.** Its Done when 4 ("sleeping during a solo match and choosing Continue restores it") changes the same `onExit` path. e2 AI-3 stays the owner's device check | follow-through | dev loop; owner (AI-3) | epic-install-and-launcher |

### Proposed edits

These are exact (quoted text is unwrapped; `orchestrated-epics.md` wraps at 120 columns), and none is applied. Every one is outside the epic folder, so each waits for the owner's approval.

**AGENTS.md** (AI-4, AI-5, AI-8):

1. *Running and verifying*, the static-analysis line. Replace:
   > - Static analysis matching CI: `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`.

   with:
   > - Static analysis matching CI: `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`. In a container without `libpcre.so.3`, the packaged cppcheck will not start: extract the library into scratch (`apt-get download libpcre3 && dpkg-deb -x libpcre3_*.deb <dir>`) and run `pio check` with `LD_LIBRARY_PATH` at its library directory, changing nothing system-wide.

2. *Running and verifying*, the format line. Replace the sentence:
   > `-g` skips staged and new files.

   with:
   > It formats only files git tracks (`git ls-files`), so `git add` a new file before the final run; `-g` also skips staged changes.

3. *Known pitfalls*, the refactor line. Replace:
   > - Refactors of shared helpers have silently dropped earlier targeted fixes: NFC filename normalization (#3600, fixed in #3630) and Hangul line breaking (#2288, fixed in #3700). Before rewriting a function, read its history with `git log -L`; cloud clones are shallow, so run `git fetch --unshallow` first.

   with:
   > - Refactors of shared helpers have silently dropped earlier targeted fixes: NFC filename normalization (#3600, fixed in #3630), Hangul line breaking (#2288, fixed in #3700), and `GameVM::failure()`'s early return, which also ordered its reads after an atomic acquire (8e233695, dropped by 0520b146's move into a pure function, restored in f27dcefd). Before rewriting or moving a function, read its history with `git log -L`, and keep every guard in it, with a comment when its reason is ordering or safety; cloud clones are shallow, so run `git fetch --unshallow` first.

4. *Known pitfalls*, a new last line:
   > - A size delta is two measurements taken the same way, one at each commit: never a new measurement minus a figure recorded earlier, and a change to how a gate measures means re-measuring the base. The icon epic quoted +45,888 B against epic 2's recorded +150,448 B after its gate began counting IRAM; like for like it was +45,248 B (f7be3b4a). Cite a commit that exists on the branch, not a pre-amend hash (`965c55d7`).

**`docs/crosshatch/orchestrated-epics.md`** (AI-3, AI-4, AI-5, AI-6):

5. *Before the first story*, a new bullet after "The upstream remote.":
   > - **Toolchain and base measurement.** On the epic's base commit, under the lock, apply AGENTS.md's certifi steps, build all five envs, and run `scripts/check_flash_budget.py` `build on`, `build off`, and `compare`. Record the flash and static-RAM figures, with the commit, as a dated `Measurement` line in the epic Notes. Every delta in the epic subtracts this measurement, not a figure an earlier epic recorded. In epic-icon-library, `sticky` and `default` stopped mid-story on the certifi step, and the flash cost was first quoted against a figure recorded before the gate counted IRAM (retro O3, O5).

6. *Each story*, step 4. Replace:
   > 4. **Merge only a finished tree.** Never merge into, or rebase, a worktree whose agent is still working; wait for its report.

   with:
   > 4. **Merge only a finished tree.** Never merge into, or rebase, a worktree whose agent is still working; wait for its report. A report that says a lens, implementation, or other subagent has not returned is not finished: resume the same agent (`SendMessage`), wait for its final report, and only then read its Review Triage Log or merge. In epic-icon-library a reviewer reported 8 minutes before its adversarial lens returned (retro O1).

7. *Each story*, a new step after step 3:
   > 3a. **Surface API-visible choices early.** When an epic runs unattended and a story fixes an API-visible choice that later stories will build on (names, a list's convention, an argument's shape), send the owner one question with its screenshot as soon as the tracer shows it. Keep building the lanes that do not depend on the answer. In epic-icon-library the naming convention came back at entry 8, and entry 9 rebuilt the set: 158 files, retro O4.

8. *Owner hand-offs*, the measurement bullet. Replace:
   > - **Measure before quoting (O8).** A memory, flash, or timing figure goes to the owner as a measurement with its method, or labelled "unmeasured". In epic-script-runtime about 320 KiB was quoted for Lua's region; the measurement was 448 KiB.

   with:
   > - **Measure before quoting (O8).** A memory, flash, or timing figure goes to the owner as a measurement with its method, or labelled "unmeasured". In epic-script-runtime about 320 KiB was quoted for Lua's region; the measurement was 448 KiB. A delta is two measurements made the same way, one at each commit. The base is the one measured before the first story, re-measured when a story changes how a gate measures (epic-icon-library retro O3).

9. *Before the epic PR*, a new first bullet:
   > - **Cross-story review.** After the last story lands, run context-free review subagents (`bmad-review`'s adversarial, edge-case, and verification-gap lenses) over `git diff <epic base>..HEAD`, excluding generated files, vendored assets, and screenshots, and weight the boundaries between stories. Record it as `{epic-folder}/cross-story-review.md` with a triage table. A build agent fixes what is accepted, and that fix commit gets the same review before the push. A story that lands after the review gets its own combined-diff pass. In epic-icon-library this pass found a medium data race that every per-story review missed (retro O2).

10. *Build-agent brief*, *How to run the build*, the Review step bullet. Append:
    > Wait for every lens subagent, and any implementation subagent, to return before you triage or report. If one has not returned, wait for it instead of reporting without it. The Review Triage Log names the lenses that returned.

11. *Build-agent brief*, *Rules*, a new bullet after the measurement bullet:
    > - Before moving or rewriting an existing function, run `git log -L` on it and say in Design Notes what each guard or early return in it protects. Keep each one, with a comment when its reason is ordering or safety (AGENTS.md, Known pitfalls).

12. *Build-agent brief*, *Rules*, the measurement bullet. Replace:
    > - A memory, flash, or timing figure in your plan or report is a measurement with its method, or says "unmeasured".

    with:
    > - A memory, flash, or timing figure in your plan or report is a measurement with its method, or says "unmeasured". A delta subtracts two measurements made the same way, never a recorded figure, and names commits that exist.

13. *Build-agent brief*, *Final report*. Replace "whether the review lenses ran as subagents" with "whether every review lens ran as a subagent and returned".

## Acceptance verdict

**Machine verdict: accepted-with-open-items**, criteria **declared** (the epic file's Done when 1–5, as amended 2026-09-28). `pending_tickets` is empty; all 10 tickets are `done`.

| Done when | Result | Evidence |
|-----------|--------|----------|
| 1. The v1 set vendored from Phosphor 2.1.1 in both weights under Phosphor's names, with licence and name map; `gen_game_icons.py` regenerates the header byte-identically in the fork-only workflow; `.clang-format` keeps the formatter off it | **met** | 55 names × 2 weights, 110 SVGs, `names.txt`, licence; byte-identical on four Python versions (this retro); `Icons up to date` green on PR #17. Beyond the criterion: `DisableFormat` also covers hand-written `GameIcons.h` (R8 b), and the category naming drifted from the spine (S3) |
| 2. `ch.gfx.icon` draws every icon at small, medium, and large; an unknown name ends in the error view | **met** | `IconsMatchTheList` draws every name × size × weight; the `icons` fixture in the simulator; `unknown_icon` in the error view with the README's text, under `pcall` too (`GfxBindingsTest`) |
| 3. `ch.gfx.image` draws a converter-layout image at native size; an unknown name ends in the error view | **met** | `GameImagesTest`, `GameImageBlitTest`; the `images` fixture in the simulator; `unknown_image` in the error view. Open beyond the criterion: R1 (replay cost unbounded) and R7 (no producer yet) |
| 4. The three views and the cover-grid Games tile draw from the library; the icons' flash cost is measured and recorded | **met, with a correction** | the views and tab in the simulator and in the review packet; flash measured and recorded (+76,624 B for the epic; 71,975 B of icon data). The first recorded figure was against a figure recorded under a different gate method, corrected in f7be3b4a and 2ba81907 (O3); one cited tree hash does not exist (S6) |
| 5. Merged with the five-env build, host suites, whole-tree format check, `pio check`, and every fork-only job, `Icons up to date` included, all green | **met** | PR #17: 19/19 green on `e6bed5f7`; merged 68 s after `Crosshatch Test Status` passed. One earlier `cppcheck` failure was an infrastructure timeout, re-run once, and green (O5 b) |

No finding blocks acceptance. Every Done-when criterion holds in the final state. The evidence is CI, host tests (707/707), the fork script suites, this retro's simulator run, and the review packet. The open items:
- **Fix before API level 1 freezes:** AI-10 (R1).
  - A game can queue about 786 M one-pixel fills in one frame. The time on a device is unmeasured.
  - It is not blocking for these reasons: level 1 is a preview (`API_LEVEL_FROZEN false`); no shipped game draws images yet; nothing writes the image layout outside the fixtures (R7); and the fix is additive (a new limit).
- **Next-epic prerequisites:** AI-1, AI-9, AI-12, and AI-13.
- **Spec reconciliation:** AI-7.
- **Process changes:** AI-3, AI-4, AI-5, AI-6, AI-8, and AI-2.
- **Deferred hardening:** AI-11.

**Human decision: accepted-with-open-items** (owner, 2026-09-28). Every should-have-and-above item landed in PR #18 (the Addendum lists them). The open items are carried: AI-1, AI-9, AI-12, and AI-13 to epic-install-and-launcher's inception, AI-11's remaining items to its refactor sweep, and AI-2 and AI-10 to the level-1 freeze.

## Open questions

Answered by the owner on 2026-09-28:
- **R3:** widen `failedToStart` to every host failure that precedes game code (NoSession, load out-of-memory, `NotLoaded`), so each shows "The game could not start"; AD-14 amended to match (built in the follow-up lane e3r-2).
- **The proposed edits:** apply them all (8bd18e86, 9d53b6db); build the should-have and higher items as follow-up lanes e3r-1 to e3r-3 before the next epic is planned.

Still open; each would change an action item:

1. **R1's fix shape.** A per-frame drawn-pixel limit (a new `limit` in level 1, simple, API-visible), or a row-blit replay path (no API change; its cost is still proportional to image bytes times commands)? And should it land in epic-install-and-launcher or wait for the freeze epic?
2. **AI-6's checkpoint.** Is an asynchronous owner question during an overnight run acceptable, given that the epic was designed to need no owner input until its end (Notes :82)? If not, the alternative is to pin API-visible naming at inception, in the epic's Requirements.
3. **AI-9's split of the 28,288 B**, and whether icon compression is done pre-emptively in epic-install-and-launcher or only when a measured need crosses a trigger.
4. **O1's source.** The repo has no trace of 3.10's or the cross-story reviewer's early hand-back. If the orchestrating session's transcript (`session_01DhiSSwkAq51abArzAsjThg`) can be shared, O1 can cite it; otherwise it rests on the owner's account plus this retro's own recurrence.
5. **S3.** Should the category be renamed back to "board pieces" (spine, seed), or should the spine adopt "game pieces" and add "status"?

## Addendum: follow-up applied (2026-09-28)

The owner asked for every proposed edit to be applied, and for the should-have and higher items to be built before the next epic is planned. The follow-up ran under this retro's own process changes.

- **Edits applied:**
  - 8bd18e86: the 13 AGENTS.md and `orchestrated-epics.md` edits; S1–S6 in the spine, game-api-seed, the epic file, `game-icons.md`, and the packet README; AI-2, AI-9, AI-12, and AI-13 in the downstream Notes.
  - 9d53b6db: O1's mechanism. Subagents start in the foreground, and a report with a subagent still running is an interim hand-back.
  - 0048e35b: AD-14 amended for the owner's R3 answer.
- **Base measurement.** The new "Toolchain and base measurement" step ran at 8bd18e86:
  - all five envs build;
  - x4pro games on minus off is +227,712 B flash and +776 B static internal RAM (`.iram0.text` +684, `.iram0.text_end` +84, `.dram0.bss` +8), from `check_flash_budget.py` `build on`, `build off`, `compare --limit-kib 250 --ram-limit-bytes 1024`, and `objects` (38 objects, no problems);
  - that is the figure recorded at f27dcefd; the code is unchanged.
  - The toolchain step showed that the proxy error can read `CERTIFICATE_VERIFY_FAILED` and that the penv exists only after a first install. AGENTS.md now says both.
- **Lanes:**
  - e3r-1: R1 and R9 (d), `wt-gfx`.
  - e3r-2: R9 (a), R4, R3 (widened, per the owner), R5, R8 (a), A2, `wt-match`.
  - e3r-3: R9 (b), (c), (e), R8 (c), (f), `wt-tools`.
  - Each lane's plan is `_bmad-output/implementation-artifacts/plan-e3r-*.md`.
- **e3r-3** landed as 6973ec8e (merged fa0d8e72). All four lenses ran as subagents and returned: 13 triage rows, 6 patched, 7 rejected. Its changed gates ran from an archive tree.
- **e3r-1** landed as 131fe505 (merged 0c600666). All four lenses returned: 11 low findings; 7 patch groups, 5 rejected, 4 deferred under `## e3r-1`.
  - R1: `limit frame_icon_image_pixels 1048576` (owner-confirmable), charged when each `ch.gfx.icon` or `ch.gfx.image` command is added and raised through the guard. A worst-case frame at the budget makes at most 1,048,576 fills (3.33 ms on the host; not a device time).
  - R9 (d): `icon_{small,medium,large}_side_pixels`, `draw ch.gfx.icon ink`, `draw ch.gfx.image opaque`, and `name image`; `API_SURFACE_CRC 0x52E8D03D`.
  - Flash: +608 B (x4pro games-on `firmware.bin`, 5,903,088 → 5,903,696 B, both from archive trees).
  - On the combined tree after the merge: host tests 716/716, all 9 `scripts/*_test.py` suites pass.
- **e3r-2** landed as d4f889f0 (merged 11402d1e). All four lenses returned: 11 low findings, 6 patch groups.
  - It fixes R9 (a), the Play-again gap drop, checked in the simulator with the new `slow-restart` fixture; R4; R3, widened per the owner, with AD-14 amended in 0048e35b; R5; and A2.
  - R8 (a): `GameAssets::load` measured 368 B with `-fstack-usage` and was split.
- **Cross-story review** of e3r-1 to e3r-3 (`_bmad-output/implementation-artifacts/cross-story-review-e3r.md`): 13 findings, all low or low–medium.
  - 7 fixed in **e3r-x** (bd176acd). Its four lenses returned.
    - The Resume render in the Play-again gap no longer shows the last round's board, which closes `## 3.7`'s R3 residual.
    - One heap-held scratch for the loader brings the deepest stack chain to 304 B, from 576 B after the split and 480 B before it.
    - A host test covers every fixture's manifest.
    - The budget's comments are reworded.
    - `Icons up to date` exits 3 for a checksum mismatch.
    - `assets/game-icons/** -text` is set.
    - `sim_sh_test.py` fails when every test is skipped.
  - 2 are recorded as freeze decisions in epic-first-party-games's Notes; 2 accepted; 2 deferred under `## e3r-x`.
- **Process observed in the follow-up itself:**
  - Agents again handed back with subagents running: e3r-1 three times, e3r-2 twice, and the cross-story reviewer once.
  - `run_in_background: false` did not keep a nested agent's lenses in the foreground.
  - `orchestrated-epics.md` now treats such a report as interim (9d53b6db, 4d136782).
- **Final checks on the combined tree:**
  - Host tests 719/719, all 9 `scripts/*_test.py` suites (each `Ran` > 0).
  - `default`, `x4pro`, and `sticky` build. `x4c` and `papermono` built on 11402d1e, and e3r-x changes no code they compile.
  - `pio check` passes on `default` and `x4pro`; `sim.sh build x4pro`, `check_layers.py`, and `check_upstream_touches.py` pass.
  - `clang-format-fix` (21.1.8) run twice leaves the tree clean.
  - x4pro games on minus off is **+228,496 B flash** (+784 B over the 8bd18e86 base; 27,504 B under the gate) and **+776 B static RAM** (+0 B; 248 B left), all four `check_flash_budget.py` steps.
