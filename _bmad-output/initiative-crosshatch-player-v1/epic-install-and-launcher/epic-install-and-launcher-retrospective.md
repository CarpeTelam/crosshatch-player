---
epic: epic-install-and-launcher
date: 2026-09-30
verdict: accepted-with-open-items
criteria: declared
headless: false
---

# Retrospective: epic-install-and-launcher

## Epic summary

**Epic:** `epic-install-and-launcher` (epic 4, "Players install a game by dropping one file and start it in three taps"), folder `_bmad-output/initiative-crosshatch-player-v1/epic-install-and-launcher/`. It covers CAP-3, the solo half of CAP-4, solo resume through Continue (CAP-7), and the launcher part of CAP-8.

**Tickets.** Fifteen (4.1 to 4.15), in build order from `tickets.py status`. Every one has `status = done` and `state = done`, so `pending_tickets` is empty and none is left at `built`. Five pieces of work sit outside the ticket tree, each with a plan in `_bmad-output/implementation-artifacts/`:
- e4-x: the cross-story review's fixes.
- e4-y: the `.chgame` rename and the label-gated package workflow.
- e4-z1, e4-z2, e4-z3: the three fix-now items from entry 14.

Each range is `<plan baseline>..<the story branch head>`, the second parent of its "Merge story 4.N" or "Merge e4-*" commit on the epic branch. The baselines are not linear: lanes ran in worktrees, and 4.4 merged after 4.6, 4.15, and 4.7. So the rule's baseline-to-baseline ranges would misattribute commits, and every range below is per plan. The raw `git_evidence.py` JSON per range is in the session scratchpad (`ev/<ref>.json`).

| Ref | Title | hitl | Covers | Baseline | Range end (branch head / merge) | Commits |
|-----|-------|------|--------|----------|--------------------------------|---------|
| 4.1 | src/games harness: storage and renderer doubles | no | R12 | `8f389a52` | `3f88747d` / `1db48012` | 2 |
| 4.2 | pack_game.py and the package vectors | no | R2, R3, R4, R6 | `8f389a52` | `b63f031c` / `c02a81d5` | 2 |
| 4.3 | Tracer: one package from the inbox to a started game | no | R1, R3, R4, R5, R13 | `4db0787e` | `c47cceaa` / `fd092a6b` | 3 |
| 4.4 | The match on the host | no | R12 | `34fae3ff` | `e957de64` / `11e76665` | 2 |
| 4.5 | The list and Home on the host | no | R12 | `ba0c67be` | `c800622c` / `b99d461c` | 2 |
| 4.6 | Package hardening | no | R1, R2, R3 | `34fae3ff` | `dc1f90dd` / `9f878fb3` | 2 |
| 4.7 | Manifest icon grammar, library check, and the pass capability | no | R6, R8, R9 | `76e0cc45` | `17a5784e` / `56b7614d` | 2 |
| 4.15 | Icon compression (PackBits) | no | R13 | `1a0b7f6d` | `6e1f4ba1` / `3c5a9722` | 2 |
| 4.8 | The paged launcher | no | R7, R13 | `0880c746` | `d8fced0b` / `fc84fea6` | 2 |
| 4.9 | The mode picker | no | R8, R13 | `000cafe6` | `b5566c1c` / `2f78c867` | 2 |
| 4.10 | Remove a game from the launcher | no | R5, R7 | `764e4908` | `a941ee1d` / `b3d83408` | 2 |
| 4.11 | resume.bin and resuming a solo match | no | R10, R11 | `ba0c67be` | `d45dd71d` / `4311b495` | 2 |
| 4.12 | Continue in the launcher | no | R8, R10, R13 | `b698fbe6` | `ee26fa2f` / `b73e3999` | 2 |
| 4.13 | Refactor sweep | no | R13, R14 | `7eee0a09` | `46774c8b` / `9f6e015c` | 2 |
| 4.14 | Device run and owner sign-off | **yes** | R5, R10, R11, R14, R15 | none (the plan has no `baseline_revision`) | docs only: `e9d139ce`..`5a5fd419` | — |
| e4-x | Cross-story review fixes | — | — | `1faaa432` | `93371981` / `b34aca51` | 3 |
| e4-y | `.chgame` rename and the game packages workflow | — | — | `770116eb` | `9180a698` / `216ccbd3` | 2 |
| e4-z1 | Recalibrate the loop fixture's Slow C calls band | — | — | `07a95af2` | `7afbf46b` / `631a72a6` | 2 |
| e4-z2 | Finish a stopped remove from a `.removing` marker | — | — | `07a95af2` | `06a38b2c` / `7e7bcc48` | 3 |
| e4-z3 | "and N more" in the install-failure note | — | — | `07a95af2` | `dac5e94c` / `135fb684` | 2 |

**The epic-wide range** is `962ae61..d4b30b64`: 111 commits, 19 merges (all 19 measured on the first-parent spine). Non-merge churn outside `_bmad-output` is +35,898 / −7,793. The tree diff over `src lib scripts test .github docs/crosshatch` is 187 files, +33,383 / −5,293:
- `src/` and `lib/`, less `GameIcons.generated.h`: 42 files, +3,819 / −334.
- `test/game_script` and `test/game_core`: 126 files, +23,163 / −82.
- The largest single file is the committed generated `test/game_script/GameIconsRaw.h` (+5,144, entry 15).

**Delivery.** One PR, CarpeTelam/crosshatch-player#20, merged 2026-09-30T06:42:33Z as `fa642c4a`. `git diff --quiet d4b30b64 fa642c4` shows the same tree. All 20 check runs on the head `d4b30b64` passed:
- `Test Status` and `Crosshatch Test Status`.
- The five env builds.
- `unit-tests`, `clang-format`, and `cppcheck`.
- `x4pro flash budget`, `Upstream touch ledger`, `Layer check`, `API freeze`, `Icons up to date`, `Fork script tests`, `Simulator build`, and `Game packages`.
- `Title Check`, twice.

**Evidence inventory**

| Evidence | Status | Source |
|----------|--------|--------|
| Epic file: R1–R15, Done when 1–5, dated owner Decisions, `Assumption for entry 14:` lines A1–A32, three "Assumption for the retro's device recheck" lines, ten `Measurement` lines | present | `epic-install-and-launcher.md` |
| Initiative requirements (CAP-3, CAP-4, CAP-7, CAP-8) | present | `../initiative-crosshatch-player-v1.md` |
| Ticket entries | present, 15 | `tickets.toml`, `tickets.py status` |
| Story files | none; no ticket was refined | `tickets.py status` (`refined: false`) |
| Plans | present: 15 story plans and 5 fix plans, all `status: done`. Entry 14's is a results record with no baseline and no review | `story-*-plan.md`, `../../implementation-artifacts/plan-e4-*.md` |
| Cross-story review | present, fixed as e4-x | `cross-story-review.md`, `plan-e4-x-cross-story-fixes.md` |
| Device run | present: the entry-14 run on an X4 Pro, firmware `216ccbd3`. e4-z1 to e4-z3 postdate it | `story-device-run-and-owner-sign-off-plan.md` |
| Device-run packet | regenerated by this retro at `d4b30b64` for the deferred recheck | `device-run-packet.md` (commits `a1ef430f`, `78651385`) |
| Simulator screenshots | 11 story folders, plus this retro's `retro-screenshots/` | `story-*-screenshots/` |
| Deferred work | `## 4.1`–`## 4.15`, `## e4-x`–`## e4-z3`, `## owner-e4-*` | `_bmad-output/implementation-artifacts/deferred-work.md` |
| CI | PR #20: 20 of 20 green on the head | GitHub Actions; the PR's "Firmware builds" comment (run 36678813150) |
| Commit and diff evidence | present | scratchpad `ev/` |
| Orchestration record | **not in the repo.** Every commit carries `session_011UmUdVd37URxMHZHdguv8w`. Its transcript and the builders' transcripts were not available to this retro. Process findings rest on plans, commits, CI, and the epic Notes. A claim that rests only on the orchestrator's summary is marked "(orchestrator's observation, no repo evidence)" | commit trailers |
| Upstream remote | **absent in this clone**, so `check_upstream_touches.py` was not run here. CI's `Upstream touch ledger` passed on the head, and a proxy diff against the last sync's upstream parent `93e98bb7` finds only ledger rows 2 and 5 | agg-6 below |
| Previous retrospective | present | `../epic-icon-library/epic-icon-library-retrospective.md` |

**Going-in concerns.** The invocation named several items:
- Two "do first" items: the packet regeneration, and the device recheck of e4-z1, e4-z2, e4-z3, and A8.
- An owner question on "and N more" at the 64-game limit.
- Nine process findings from the orchestrator.

Each is answered below, as a finding where it has a source.

## Findings

Dispositions: **fix now** (an action item), **defer** (with its trigger), **accept** (recorded so later retros stop re-flagging it). Each finding also names the upstream lesson that would prevent the next one.

### Process and orchestration [proc]

**proc-1. The trailers comply with the repo's written rule. The premise "never write a model name in commits" is not that rule.**
- Every one of the 111 commits in the range carries a model-naming `Co-Authored-By` trailer: 70 `Claude Opus 5.5` (orchestrator) and 41 `Claude Sonnet 5.5` (builders).
- Earlier fork epics carry 194 more (`1d61100f..962ae61`).
- The rule in the repo is `docs/crosshatch/orchestrated-epics.md:162`: "No model names in code or docs; the session's attribution lines are the only exception." It was added in 88d19c20 to replace an older "no model names in commits" wording, because that wording contradicted the attribution trailer (`plan-e2r-ai-12-orchestration-rules-home.md:85`).
- AGENTS.md, `.claude/`, and `_bmad/` hold no model-name rule.
- The "never in commits" wording the orchestrator cites matches the cloud session's own harness instruction, which comes from outside the repo and conflicts with the same harness's attribution reminder.
- Disposition: **accept** the 111 trailers. The trailer policy is an owner decision (Open question Q1).
- Lesson: the orchestrator should cite the repo's rule, not the harness wording. AI-3.

**proc-2. Host doubles were more permissive than the device twice, and each time a fix passed every test.**
- The e4-z3 text path:
  - The harness screen double lays text out with `layoutText`, which breaks on `\n`.
  - The device and simulator draw through `GfxRenderer::wrappedText`, which splits on spaces only (`plan-e4-z3-and-n-more.md:40`, :59).
  - The simulator, not a test, caught the result: "not sourceand 1 more".
  - The fix, dac5e94c, records raw `text()` calls (`test/game_script/harness/screen_stubs/components/UiAppHost.h:85-89`) and asserts that no call holds `\n` (`GamesLauncherTest.cpp:998`).
- The e4-z1 timing bound:
  - A 50 ms host cancel-latency bound admitted fixture sizes, `(6, 20)` to `(6, 24)`, that the device would abandon (`plan-e4-z1-loop-fixture-calibration.md:66`, M1).
  - It was replaced in 7afbf46b by a host-cost bound derived from the device ratio: under 2 ms on the host, about 190 ms on the device at about 95× (:52 gives 92 to 97, labelled unmeasured).
- A third case was caught in review before it shipped: in 4.4 the fake clock ignored `vTaskDelay`, so a timed join would hang (`story-the-match-on-the-host-plan.md:109`, high).
- Disposition: **fix now**, as a convention. AI-4.
- Lesson: a double's fidelity is part of the story's verification. A double stands in for a device behaviour only where a test pins that the two agree.

**proc-3. The loop fixture reached the entry-14 device run uncalibrated.**
- `backtrack(6, 30) -- about 2 M steps a call` was set in 376a4c62 (epic 2) from host arithmetic.
- The count covered the first start position only, which understated the call about 5×. The host call took 91 to 104 ms (`plan-e4-z1…:43`, :50).
- Epic 2's device run had the fixture on the card, but recorded no band outcome (`epic-script-runtime-retrospective.md:390`). The first band-level device record is entry 14's step 9.
- There, "Slow C calls forever" was abandoned rather than cancelled. So the clean cancel path it exists to show had never run on a device.
- Disposition: **fix now**, as a process step. AI-5.
- Lesson: a fixture whose outcome depends on device timing is calibrated on the device before a packet relies on it.

**proc-4. Builders cut off mid-work: one record in the repo.**
- `story-remove-a-game-from-the-launcher-plan.md:73` says "The implementation subagent ended without changing any file (its session was cut off), so the plan's author implemented it directly, from the plan."
- `plan-e4-z3…:44` records a background review lens returning before the checks finished.
- No repo record exists of either container restart, or of 4.10's builder resuming while its own sub-agent still ran (orchestrator's observation, no repo evidence).
- Disposition: **fix now**, as a process step. AI-8.
- Lesson: a restart is recorded in the plan when it happens, so the next retro can see it.

**proc-5. The launcher-test flake came from test code, and the fix is proven.**
- The rate was "about one full `ctest -j 8` run in eight" (b964078a; removed in 46774c8b).
- The cause is VG1 (high) in `story-refactor-sweep-plan.md:146`: a harness defect, plus the Play-again backoff not cleared. The tests assumed no save after leaving a match.
- The proof, at :171: 20 of 20 full runs at 1,302 of 1,302; each of the four flaky tests plus a fifth passed 200 of 200 under `--repeat until-fail:200`; the base failed 3 of 20 runs.
- This retro's run passed 1,361 of 1,361.
- Disposition: **accept**.
- Lesson: when a test flakes, prove the fix at a stated repeat count, as this one did. The orchestrated-epics doc could name the numbers used here as the bar. AI-7.

**proc-6. A `labeled` trigger in `crosshatch-ci.yml` cancelled runs and turned the required status red.**
- 7af3fe19 added `labeled` to the workflow's `pull_request.types`.
- The workflow's concurrency is `group: crosshatch-ci-${{ github.event.pull_request.number }}`, `cancel-in-progress: true` (`.github/workflows/crosshatch-ci.yml:48-50`). So a label event joined the group and cancelled the run in progress.
- 9180a698 moved the job to `crosshatch-game-packages.yml`, whose header (lines 5-8) says why.
- e4-y's own review foresaw the cancel and accepted it as medium (`plan-e4-y-chgame-rename.md:93`, row 6).
- Neither AGENTS.md nor `docs/` states the rule yet.
- Disposition: **fix now**. AI-6.
- Lesson: a review finding that says a required check can go red is not "accepted" without a run showing it doesn't.

**proc-7. A measurement was taken on a WIP commit that was later squashed.**
- `story-refactor-sweep-plan.md:168` says: "Firmware and measurement were run on a WIP commit of this session, since squashed into this fix commit".
- The orchestrator re-measured at `9f6e015c`, whose tree is identical to `46774c8b`, and got the same figures (epic Notes, `75abfed0`).
- AGENTS.md's pitfall already says "Cite a commit that exists on the branch" (the `965c55d7` example).
- Disposition: **fix now**, with one clause. AI-7.
- Lesson: a measurement is taken on the commit it cites, after the last squash, and never carried across a history rewrite.

**proc-8. Independent reviews caught real issues after the builders' own passes, on every build ticket.**
- The patched findings per plan, from each plan's Review Triage Log (builder-fixed / independently fixed, highest independent finding):
  - 4.1: 13 / 15, medium.
  - 4.2: 4 / 9, **high**: `\u` escapes decoded unlike the device parser.
  - 4.3: 7 / 11, **high**: a non-atomic folder rename could free shared clusters.
  - 4.4: 19 / 11, **high**: the fake clock.
  - 4.5: 16 / 4, medium.
  - 4.6: 10 / 7, medium.
  - 4.7: 8 / 5, medium.
  - 4.15: 8 / 5, low.
  - 4.8: 13 / 7, medium.
  - 4.9: 5 / 5, medium.
  - 4.10: 2 / 8, medium: a tap started a game under the Remove dialog.
  - 4.11: 10 / 9, medium: forced-exit SD work was unbounded.
  - 4.12: 10 / 5, **high**: Continue, Leave, Confirm replaced `resume.bin`.
  - 4.13: 11 / 10, **high**: ADV1 at `story-refactor-sweep-plan.md:145`, where a `.pkg` that would not read on Continue started a New match, and Over deleted the valid save. VG1 (the flake) is the second high.
  - e4-x: 10 / 9. e4-y: 4 / 3. e4-z1: 1 / 7. e4-z2: 4 / 8. e4-z3: 2 / 6.
- Disposition: **accept**. The owner's inception Decision to review on a different model from the builders earned its cost.
- Lesson: keep the independent review. The "high" findings cluster on SD-card state (4.3, 4.12, 4.13) and on a double's fidelity (4.2, 4.4), which aim the next epic's review lenses.

**proc-9. Budget: static RAM has no slack left.**
- At `7e7bcc48`: +232,912 B flash (23,088 B under the 256,000 B gate) and +784 B static RAM (240 B under 1,024 B).
- Against this epic's pass bar (+240,496 B, +808 B), 7,584 B and 24 B remain (epic Notes, the last `Measurement` line). Over the base `962ae61` that is +4,416 B flash and +8 B static RAM, after entry 15's PackBits compression saved 24,560 B.
- The 240 B of static RAM left is exactly epic 4's unspent 24 B plus the later allocations:
  - pass-and-play 32 B (`epic-pass-and-play.md:48`);
  - play-nearby 160 B (`epic-play-nearby.md:50`);
  - reserve 24 B (spine Operational envelope).
- Flash likewise: 23,088 B = 7,584 B + 13,504 B + 2,000 B.
- Of the +784 B static RAM, 768 B is IRAM code: `.iram0.text` +684 and `.iram0.text_end` +84 (Notes, 2026-09-29 final line). Only 16 B is data.
- The spine's budget row does not record epic 4's delta.
- Disposition: **fix now** (record it) and **defer** the lever.
- Lesson: the compression lever is spent. The next lever for static RAM is the games-on IRAM code, not data. AI-9.

### Diff-scope review, across tickets [rev]

The `bmad-review` skill ran over `git diff 962ae61 d4b30b64 -- src lib scripts test .github`, with the adversarial, edge-case, and verification-gap lenses as three parallel reviewers. It weighted the ticket boundaries: installer 3 → 6 → 7 → 10 → e4-x → e4-z2; launcher 8 → 9 → 10 → 12 → e4-z3; match 4 → 11 → 12 → e4-z1. The generated icon headers were left out. The reviewer re-checked each finding against the code and dropped one low-confidence claim.

This retro re-opened the sources of rev-1, rev-3, rev-12, and rev-13 before routing them. Findings already in `deferred-work.md` are listed at the end, not re-filed.

**rev-1. Medium. A remove the launcher reports as failed is finished, silently, on the next visit.** (4.10 × e4-z2)
- `remove` writes `.removing` and then deletes the `.pkg`. When that delete fails it returns `SdCard` and leaves the marker behind (`GamePackageInstaller.cpp:352-361`).
- The registry does not read the marker, so the game stays listed, and the launcher says "Could not remove it. Check the SD card." (`GamesLauncherActivity.cpp:309-314`).
- The person plays the game they were told was kept. On the next visit, which Leave's return to the launcher is, `finishRemovals` deletes it and its Continue row without a word. `/.games-data` is kept, so no save is lost, but the game is.
- `RemoveTest.AMarkerThatWillNotGoLeavesTheGameListedAndWhole` (`GameRemoveTest.cpp:156-174`) fails the `.pkg`, not the marker, and never asserts the marker's state.
- Disposition: **fix now** (AI-14). Remove the marker, best effort, when the `.pkg` delete fails, and add the assertion.
- Lesson: the marker changed what "the remove failed" means, and no test re-read the old message against the new state.

**rev-2. Medium, a verification gap. The `.xlink` probe's step order is not pinned.** (4.3/4.6 × e4-z2)
- `foldersShareClusters` (`:195-205`) makes the probe, checks `exists` in `/.games/<id>`, and then removes it.
- The host tests model a shared chain with a `.xlink` placed in advance (`GamePackageInstallerTest.cpp:720`, `GameRemoveTest.cpp:246`). The fake cannot alias two folders, and its `exists` logs no operation.
- Moving the check after the remove passes every host test. On a card with a real shared chain the probe would then read "not shared", and `removeDir` would free clusters the game still uses.
- Disposition: **fix now** (AI-15). The fake logs `exists`, and a test asserts the create → exists → remove order.
- Lesson: this is proc-2's pattern a third time.

**rev-3. Medium. Following "remove one first" does nothing until Games is reopened.** (e4-x × 4.10)
- `confirmRemove` reloads the listing but never runs the inbox install (`GamesLauncherActivity.cpp:283-316`). `installAll` runs only in `onEnter` (`:123`).
- The person removes a game as the note asked, the waiting package does not appear, and they remove more games or decide the package is broken.
- Disposition: **fix now**, with Q2 (AI-2). After a successful remove, run `installInbox` when the inbox holds a file.

**rev-4. Low. The installer's out-of-memory paths are unverified.** No installer suite overrides nothrow `operator new` (`installer.cmake:41-45`; only `GameSaveStoreTest.cpp:33` and `GamesLauncherTest.cpp:36` do). Dropping `OutOfMemory` from `packageIsInvalid` (`:835-837`) would rename a valid package `.bad` on a low heap, and no test would fail. Disposition: **fix now**, as a test (AI-15).

**rev-5. Low. The forced exit writes `resume.bin` before `ch.store`.** `GameMatchActivity.cpp:196-199` runs `flushResume`, `retryResumeDelete`, then `flushStore`, each gated by the 1,500 ms deadline. A slow join can skip `flushStore`, so Continue resumes a board newer than `ch.store`. The device measured 502 ms with no SD step pending (entry 14 step 10). Disposition: **defer**. Trigger: a device log line `forced exit past 1500 ms; skipped` a `ch.store` flush. Record the order in AD-17 (AI-10).

**rev-6. Low. An out-of-memory registry load reads as "No games found".** `loadGames` drops `load()`'s false (`:318-321`), including on the reload after a remove. The pre-epic list did the same. Disposition: **defer**, with rev-7, to the next launcher change.

**rev-7. Low. An inbox name over 62 bytes is skipped, with nothing on screen and no deferred-work entry.** See `forEachInboxFile` (`:176-182`). Disposition: **defer** (AI-13 adds the entry). Trigger: a device report.

**rev-8. Low. Packages waiting for room have no per-visit bound.** `judged` skips `TooManyGames` (`:950`), so W waiting packages cost W directory and manifest reads on every visit. Disposition: **defer**, to `## owner-e4-games-cap`'s investigation.

**rev-9. Low, worse than recorded. When the aside names run out, an installed file reinstalls every visit and undoes a Remove.** (e4-x × 4.10)
- `ASIDE_NAMES = 5` (`:58`), and `moveAside` (`:725-742`).
- `## e4-x` records only that the `.installed` files are never cleaned up.
- Disposition: **defer**, with its entry widened (AI-13). Trigger as recorded: a card that refuses deletes repeatedly.

**rev-10. Low. An interrupted update removes the old folder with no marker.**
- `commit` (`:697-704`) deletes the `.pkg`, then calls `removeDir`.
- A power loss mid-`removeDir`, after the person has since deleted the inbox file, leaves the unmarked orphan that e4-z2 closed for `remove`.
- Disposition: **defer** to AI-12's split, where `commit` and `remove` share one marked removal.

**rev-11. Low. The install note does not block list navigation.** `handleCustomInput` returns false under the note (`:178-189`), so the list still pages and moves its selection. A Confirm after dismissing it can start a New match over a save. Disposition: **defer**. The title screen (`## owner-e4-title-screen`) makes New an explicit choice.

**rev-12. Low. Another label cancels a running `package-games` run.** The workflow-level concurrency group applies even when the job's `if` skips it (`.github/workflows/crosshatch-game-packages.yml:19-21`). Disposition: **fix now**, with AI-6. The job's concurrency can move into the job, or `cancel-in-progress` can be keyed to the `package-games` label.

**rev-13. Low. `ZipScratch::path` is a `std::string` (`GamePackageInstaller.cpp:97-103`).**
- The inbox path is longer than the small-string buffer, so it allocates with a throwing `new`. Under `-fno-exceptions`, a failed allocation calls `abort()` rather than returning `OutOfMemory`.
- The struct itself is on the heap, created with `nothrow`.
- Disposition: **defer** to AI-12. A fixed `char[]` would do, if `ZipFile`'s upstream constructor allows it.

**rev-14. Low.** `lib/GameIcons/GameIcons.h:13-14` cites "This folder's .clang-format", which the epic deleted. Disposition: **fix now**, as a one-line comment edit at the next touch (AI-12).

**Harness doubles more permissive than the device**, beyond proc-2's two:
- The text measure is 10 px per byte (`screen_stubs/components/UiAppHost.h:74-77`), against the real font's widths, and the real renderer cuts the last line with "…".
  - The longest note fills `NOTE_LINES = 4` with 0 to 3 px spare, so a wider font would cut the reason while `TheLongestReasonWithTheLongestFileNameIsDrawnWholeAboveTheMoreLine` still passes.
  - Low. It goes to AI-4.
- `tapToLogical` is an identity cast in the double (`screen_stubs/GfxRenderer.h:36-39`), while the device scales and rotates (`GfxRenderer.cpp:1941`). The production code predates the epic. Low, AI-4.
- The fake's rename keeps the entry's slot, and freed slots are never reused (`stubs/HalStorage.h:50-54, 399-411`). Nothing depends on it today. Low, AI-4.

**Recorded items the review confirmed still hold:**
- `## e4-z3`: the "remove three" reading, the copied popup sizing, and `noteMore[48]` cutting UTF-8.
- `## e4-z2`: the long-name fallback, the empty folder after the marker's delete, no popup for `finishRemovals`, and `.REMOVING` matching in any case.
- `## e4-x`: the aside files, and the 65th-game count running high.
- `## 4.10`, `## 4.11`, `## 4.6`, `## 4.1`/`## 4.4`, `## e4-z1`'s uncertain ratio, and the cross-story review's row 10.

**Came back clean:**
- `ZipDirectory`'s EOCD and directory bounds, overlap, and duplicate names.
- `installAll`'s `stayed` accounting, and the `MAX_GAMES` recount after a late commit failure.
- Paging and padding rows, and Continue selection.
- The forced exit's per-step deadline.
- `confirmRemove` taking `RenderLock` from `routeTouch`, which cannot deadlock.

### Aggregate views [agg]

**agg-1. God-class candidate: `src/games/GamePackageInstaller.cpp`.**
- It is new, at 982 lines and 33 functions, and 14 commits touched it. It holds about nine jobs:
  - inbox scan (L170);
  - tmp cleanup and the cross-link probe (L195-249);
  - remove, the marker, and finishing removals (L263-396);
  - zip member sort and list (L409-518);
  - manifest reading (L529);
  - PNG header parse and conversion (L560-653);
  - extract and commit (L657-723);
  - the game cap (L748-778);
  - error text (L844).
- The largest functions: `installAll` 63 lines, `convertImage` 56, `install` 52.
- It is not yet unreadable: each job is a named function, and no function passes 80 lines. But epic 5 and epic 6 add no installer work, so a split is cheapest now, before a later epic grows it.
- Disposition: **defer** to epic-pass-and-play's refactor sweep. The trigger is the next installer change. AI-12.

**agg-2. `GamesLauncherActivity.cpp` is the most-touched file.**
- 18 non-merge commits touched it, across 4.3, 4.6-4.10, 4.12, 4.13, e4-x, and e4-z3. It is 609 lines with 25 methods; `buildScreen` is 80 lines.
- The owner's title-screen plan (`## owner-e4-title-screen`) removes the Continue rows and their selection rules from it.
- Disposition: **accept** for now. The title screen is the natural shrink.

**agg-3. Duplication map.**

| What | Sites | Disposition |
|------|-------|-------------|
| Directory-walk loop (open, rewind, `openNextFile`, name, length guard) | five in `GamePackageInstaller.cpp` (L170, L228, L299, L385, L748), two in `GameRegistry.cpp` (L77, L84) | defer (AI-12): one `forEachEntry` helper |
| Game-id rule | `isGameId` (installer L254), `validId` (`lib/GameCore/Manifest.cpp:29`, anonymous namespace), `GAME_ID` (`pack_game.py:81`); not in `package_vectors.json`, unlike every numeric limit | defer (AI-12): export `validId`, and add the grammar to the vectors |
| Member-stem rule | installer L144-160, `GameAssets.cpp:28-38`, `pack_game.py:83-84` | defer, with the id rule |
| Error→text | `reasonText` (launcher L42-87, through `tr()`) and `describe` (installer L844-888, log English), two 20-case switches | accept: one is translated UI text and the other a log line |
| Popup sizing | the launcher L493-514 copies `Screen::popup` (`freeink-sdk/libs/ui/FreeInkUI/include/FreeInkApp.h:655-682`) and hard-codes `3/4` width, where the SDK also honours `maxWidth` | defer: recorded at `## e4-z3`, trigger "`wrappedText` honours `\n`" |
| Path building | 26 `snprintf(…, GamePaths::…)` sites, with no path helper; `GameSaveStore::peek` rebuilds the paths its constructor already built (L269-271 vs L152-153) | defer (AI-12) |
| Two fake SD cards | `save_store_stubs/HalStorage.h` (127 lines, still used by `GameSaveStoreTest`, which grew 404 lines in 4.11) and `harness/stubs/HalStorage.h` (440 lines); three `Logging.h` stubs; `manifestJson()` defined five times across the harness suites | fix now (AI-4): one fake. A second fake is exactly where fidelity drifts (proc-2) |

**agg-4. Architecture delta: clean.**
- `check_layers.py` passed: 469 include edges in 107 game files.
- The new cross-area edges are the ones the spine was amended for:
  - `src/games` → `lib/ZipFile`, `lib/PngToBmpConverter`, and `lib/miniz`. The miniz edge was added to the table in the same commit, 6f2aeb3f.
  - The platform `mbedtls/sha256.h`, and `openssl/evp.h` in the simulator.
- `lib/GameCore` gained no `src` or hardware include.
- Disposition: **accept**.

**agg-5. Pattern divergence: none found in the added lines of `src` and `lib`.**
- No bare `new` or `make_unique`: the one allocation, `GameSaveStore.cpp:276`, is `new (std::nothrow)` with a null check.
- No SdFat or `FsFile`, no `Serial.print`, and no raw `HalGPIO::BTN_*`.
- No `RenderLock` in a destructor.
- No local over 256 B by a static scan. e4-z2's `-fstack-usage` frames top out at 240 B (`plan-e4-z2-removing-marker.md:159`).
- No user text outside `tr()`.
- Disposition: **accept**.

**agg-6. Upstream touch: rows 2 and 5 only.**
- `english.yaml` got an append-only hunk at EOF (ledger row 2).
- `ActivityManager.cpp` changed inside `#if FREEINK_CAP_GAMES` (row 5).
- A nit: row 5's text says "its include", but the file has two (`ActivityManager.cpp:32-33`).
- Disposition: **accept**, with the wording fix in AI-10.

**agg-7. AGENTS.md's generated-files list misses one file.** It does not name `test/game_script/GameIconsRaw.h` (+5,144 lines, committed, and compared by CI at `crosshatch-ci.yml:286-305`). Disposition: **fix now**, a one-line AGENTS.md edit. AI-6.

### Spec-to-implementation reconciliation [spec]

Every R1–R15 and every Done-when item is met, or met with a deviation that an owner Decision or an answered `Assumption for entry 14:` line records. The table:

| R | Status | Verified by | Deviation or gap |
|---|--------|-------------|------------------|
| R1 | met with deviation | host; device step 1 | `.xlink` probe (A5), 64-game cap and the 65th refusal (Decision 2026-09-29, A28, A29), `.chgame.installed` (A30), `.removing` (e4-z2), 32 files per visit (not specified; `formats.md:319`) |
| R2 | met | host only | every hostile-zip rejection is host-only; the limits added beyond R2 are spec-2 |
| R3 | met with deviation | host; device step 2 | a non-square icon is refused (A4); sizes that scale to 63 px are refused (cross-story row 1) |
| R4 | met | host and device step 3 | — |
| R5 | met with deviation | device steps 5 and 7 (the remove from before e4-z2); host for today's remove | the marker and the `.installed` aside are not an index, so R5 holds; the spine omits both (spec-3) |
| R6 | met | host | A3 (the codec split) never triggered, because `pack_game.py` imports no codec; it has no deferred-work entry (spec-5) |
| R7 | met with deviation | host and simulator; device for `icon.bmp` and the fallback only | whole-page paging and the return to the page (Decisions 2026-09-29); Unavailable rows and paging not shown on a device |
| R8 | met | device (no picker at one mode); picker host and simulator only | the picker cannot open while `pass` and `nearby` are off |
| R9 | met | host | — |
| R10 | met with deviation | device steps 4, 5, 6, 6a | "discarded" became "not offered, file kept"; one Continue row per game (A1); Leave also writes; the unreadable-save change is spec-4 |
| R11 | met | device step 10 (502 ms) | AD-17 and AD-20 name the delete retry (A32(a), `5a5fd419`); `game-canvas.md` does not record the 502 ms |
| R12 | met with deviation | host | `notLoaded` unreachable; the joined installer-and-launcher suite and the drift guards deferred (A23, A24) |
| R13 | met | CI: ledger, layers, flash | +4,416 B flash and +8 B static RAM over the base, against a 12,000 B / 32 B share |
| R14 | met | owner sign-off | A15–A27 agreed; A18 and A22 fixed as e4-z2 and e4-z3 |
| R15 | met, with the recheck pending | device | e4-z1, e4-z2, e4-z3, and A8 deferred to this retro (Behavior verification) |

Done when:
- **1–4 are met.** Their hostile-zip rejections, paging, library-icon rows, Unavailable row, codec-version discard, and the mode picker are host or simulator only.
- **5 is met:** 20 of 20 checks on the head, merged into `develop`.

**spec-1. The launcher's page size is recorded wrong.**
- Entry 14's plan (`story-device-run-and-owner-sign-off-plan.md:45`) says "With four games per page".
- The X4 Pro launcher shows eight rows per page: `story-remove-screenshots/page1.png`, and this retro's `retro-screenshots/a8-02-page2.png` (13 games, 8 and 5).
- The earlier packet's A8 recipe rested on four, and this retro's first regeneration did too, corrected in `78651385`.
- Disposition: **accept**. The plan stays as the owner's record; this line is the correction.

**spec-2. Limits beyond R2 are enforced but not all listed.**
- `PackageLimits.h:22-36` adds `LUA_SOURCES_BYTES`, `MEMBER_STEM_BYTES`, `IMAGE_MAX_WIDTH`/`HEIGHT`, and `ICON_PIXELS`.
- The manifest nesting limit of 32 (`PackageLimitsTest.cpp:127`) and `ICON_PIXELS` are missing from `api-level-1.txt`.
- Nesting is missing from `formats.md`'s limits table, and `formats.md` (~:247) still says the `.lua` total "is added in a later entry", though `pack_game.py:475` enforces it.
- Disposition: **fix now**, as a spec reconciliation. AI-10.
- An `api-level-1.txt` addition sits under the API freeze, so the owner confirms it.

**spec-3. The spine lags the as-built installer.**
- AD-16 (:271-276) and the SD-card seed (:527-531) omit:
  - `.removing`;
  - `.chgame.installed`;
  - `.xlink`;
  - the `.tmp` files;
  - the 64-game cap;
  - the 65th package waiting in the inbox.
- Disposition: **fix now**, as a spec reconciliation. AI-10.

**spec-4. An unreadable save changed behaviour after inception with no assumption line.**
- Entry 11's frozen matrix said an invalid or unreadable save lets a new match start.
- The sweep (b964078a; `story-refactor-sweep-plan.md:37, :86, :145`) made it: "Unreadable → the Continue row stays, and the tap ends in the error view, `resume.bin` untouched".
- That is the ADV1 data-loss fix (proc-8), and it is the safer behaviour. But no `Assumption for entry 14:` line put it to the owner, and no device run has shown it.
- Disposition: **accept** the behaviour. **Fix now** the record: the AD-17 wording in AI-10, and a device step in the next closing run (AI-11).

**spec-5. A3 (the codec split) has no home.**
- R6 made it conditional: "when `pack_game.py` first imports the codec".
- Entry 2's plan forbade the import (`story-pack-game-py-and-the-package-vectors-plan.md:31`), so the trigger never fired.
- No `deferred-work.md` entry carries it forward.
- Disposition: **defer**, with a deferred-work entry. AI-13.

### What the evidence confirms went well

- **The tracer and the lanes held.** Entry 3 took the epic's first measurement, as the inception decided. Five lanes merged 19 times with no conflict in a shared file outside the dependency paths the Notes named. `deferred-work.md`'s `merge=union` absorbed every concurrent append.
- **The compression lever was pulled on time.** Entry 6's measurement crossed the 15,504 B trigger (Notes, 2026-09-29), and entry 15 was already running by the owner's decision. It saved 24,560 B, so the epic ended at +4,416 B over its base against a 12,000 B share.
- **The device run was thorough.** Entry 14 covered install two ways, the hash on the device, sleep and Continue, reinstall, the changed package, and remove. It timed the three replay bands from video and from the serial log (3,142 ms for band 3). It measured the forced exit at 502 ms, and a second attempt reached the stuck-VM case the first missed. It closed epic 2's AI-3 (the stack decision) with numbers.
- **Every AGENTS.md convention held in the added code** (agg-5), and the layer table grew only where the spine said it would (agg-4).
- **Every previous-retro action item that was due landed** (Previous-retro follow-through).

## Behavior verification

**Host, at `fa642c4a` (this retro, 2026-09-30):**
- The GoogleTest suites passed 1,361 of 1,361, matching the last recorded figure (log `scratchpad/behavior/host-tests.log`).
- All 11 `scripts/*_test.py` files passed, 447 tests (`behavior/script-tests.log`).

**Simulator, x4pro, at `fa642c4a` (this retro, built cold):**
- **e4-z3 passed.**
  - Two invalid packages: the note read "invalid-2.chgame: A Lua file is compiled, not source", with "and 1 more" on its own line. Both files became `.chgame.bad`, and the note did not return on the next visit (`retro-screenshots/e4z3-01-note-two-invalid.png`).
  - Three invalid packages with different reasons: the first reason, then "and 2 more" (`e4z3-04-note-three-mixed.png`).
  - The file named first follows the host filesystem's directory order. A FAT card may name another.
- **e4-z2 passed.**
  - Removing Counter from its own row: the dialog named the game with Cancel focused (`e4z2-02-remove-dialog.png`). Afterwards `/.games/counter/` was gone with no `.removing` anywhere under `/.games`, and `/.games-data/counter/resume.bin` was unchanged (29 B, same mtime).
  - A hand-placed `.removing` in an installed game and in a half-removed folder was finished on the next visit ("Removed loop", "Removed half-gone"). A hand-copied folder with neither `.removing` nor `.pkg` was left alone, and `/.games-data/loop/` kept its file (`e4z2-06-after-interrupted-remove.png`).
- **A8 passed.**
  - With 13 games, page 2 held Package vector (`a8-02-page2.png`). It opened into the error view (`game.setup is not a function`) and wrote no `/.games-data/`.
  - Back, Back to Home, then Games: page 2 opened with Package vector selected (`a8-07-reentry.png`).
  - Leaving Counter before any tap writes a 29 B `resume.bin` and gives it a Continue row. That is why the packet's A8 check uses Package vector.
- **The owner's question, at 64 games installed** (Q2):
  - Three valid packages over the limit: "extra-57.chgame: Too many games are installed; remove one first" / "and 2 more". All three stay in `/games/` and show again on the next visit (`d-03-note-3-valid-over-64.png`).
  - One valid and two invalid: the same text, "and 2 more" (`d-05-note-1valid-2invalid-over-64.png`). The two invalid files are *not* set aside, and their reasons are never shown: at the limit every package is judged "too many" once its manifest is read (`GamePackageInstaller.cpp:796`, by design; Assumption A29).
  - A non-zip file and one valid package: "and 1 more", and the non-zip file *is* renamed `.bad`, because `NotAPackage` fails before the limit check. Its reason is not shown (`d-06-note-notapackage-plus-valid-over-64.png`).
- **Cosmetic, not filed:** over an empty list, "No games found" shows through the note's bottom border.

**What the simulator cannot stand in for:**
- e4-z1: the watchdog cancel versus the abandon is device timing.
- FAT semantics: directory order, reuse of a freed directory slot (the reason `.removing` is deleted last), case-insensitive names, and the cross-link probe. The simulator's `fs_/` is ext4.
- A power loss partway through a remove: the marker was placed by hand.
- The C3's memory limits.

**Device, X4 Pro: pending the owner.**
- The deferred recheck is `device-run-packet.md` R1–R4, regenerated at `d4b30b64`:
  - R1: e4-z1, tethered.
  - R2: e4-z2, including an optional on-card `.removing` check.
  - R3: e4-z3.
  - R4: A8.
- It uses the CI firmware from run 36678813150 (kept until 2026-12-29) and the `game-packages` artifact from run 36678813162 (kept until 2026-10-30). This retro repacked the set locally and got the same hashes, with `loop` at `c74273851f270beb`.
- Until the owner records the results here, e4-z1's cancel path has never run on a device. e4-z2 and e4-z3 are verified on the host and in the simulator only.

## Previous-retro follow-through

From `../epic-icon-library/epic-icon-library-retrospective.md` (Action items, Proposed edits, and the items its own follow-through recorded as not landed). Its Addendum applied the 13 proposed edits and AI-2, AI-7, AI-9, and AI-12 before this epic began (8bd18e86, 9d53b6db, 4d136782). For those, the check is whether they held.

| Item | Owner | Landed? | Source |
|------|-------|---------|--------|
| AI-1: a `src/games` harness | dev loop | yes, with residuals | 4.1 (6ae85f41, 3f88747d), 4.4 (c73b3a51), 4.5 (9916d1a7); `## 3.1`, `## 3.6`, `## 3.7`, `## 3.9`, `## 3.10`, and `## e3r-2` marked resolved; residuals are `notLoaded`, R10 (overlay), and the `## 4.4`/`## 4.1` seams (A23) |
| AI-2: freeze prerequisites in first-party-games' Notes | owner | yes, and extended | `epic-first-party-games.md:53` (8bd18e86); :56-57 (07a95af2, 5a5fd419: the fill budget and A19) |
| AI-3: no hand-back while a sub-agent runs | owner, orchestrator | mostly held | `orchestrated-epics.md:63-67`; plans record "returned before triage" (4.3 :83, 4.6 :88); lapses at 4.10 :73 and e4-z3 :44 (proc-4) |
| AI-4: cross-story review; read history before moving code | owner, orchestrator | yes | `cross-story-review.md` (1faaa432), its fix reviewed twice (`plan-e4-x` :125, :136); `git log -L` noted (4.13 :162, e4-x :160) |
| AI-5: measure the base before quoting a size | owner, orchestrator | yes | the base at `962ae61` was measured first (Notes; 8f389a52 is the first commit) |
| AI-6: an early owner checkpoint | owner, orchestrator | yes, after the launcher existed | Notes, the launcher-screenshot Decision (bf2c559d); it changed entry 10 |
| AI-7: spec and spine reconciled (S1–S6) | owner | yes, before the epic | 8bd18e86; epic 4's own delta not yet recorded (proc-9) |
| AI-8: AGENTS.md libpcre and clang-format lines | owner | yes, and used | AGENTS.md; 4.13 :176 |
| AI-9: a budget line before the first story | owner, orchestrator | yes | Notes, base Measurement; the trigger crossed at entry 6; entry 15 ran (f2d85ab9) |
| AI-10: bound image and icon replay; time it on a device | dev loop, owner | yes | the timing fixture (b964078a); entry 14 steps 8 and 8a (567 / 1,338 / 3,142 ms); fill budget moved to the freeze list |
| AI-11: the hardening list | dev loop | mostly | 4.13 fixed R8(b), R8(e), R9(g), and the rest of A2, and documented R9(f) (4.13 :77-89); R8(d) deferred to the next row-8 touch; **A3 not done**, since its trigger never fired (spec-5) |
| AI-12: handoff lines | owner | yes, and the as-built holds | Notes :78, :83; every image converted and a `.bmp` member invalid (4.3, 4.6; device step 1) |
| AI-13: bound sleep under `RenderLock` | dev loop | yes | 68ec417b (`GameVM.cpp` counts `millis()`), d45dd71d (`FORCED_EXIT_DEADLINE_MS`), `game-canvas.md:88-112`; 502 ms on the device |
| Proposed edits 1–13 | owner | yes, and present | AGENTS.md; `orchestrated-epics.md` :35, :59, :63-67, :97, :102, :155-158, :207, :208, :218-222 |
| epic 2's AI-3: loop band evidence on the device | owner | yes, with a finding | entry 14 step 9; the stack decision; the Slow C band recalibrated in e4-z1, **its device recheck pending** |
| epic 2's AI-8: surface-test arity and the stale base | owner | no; recorded for the freeze epic only | `epic-first-party-games.md:53`; nothing in epic 4 builds it |

## Action items

Every item is **proposed**. The retrospective applied none of them. The owner decides which to execute, and the dev loop or the orchestrator applies the accepted ones.

| # | Action | Owner | Kind | From |
|---|--------|-------|------|------|
| AI-1 | Run `device-run-packet.md` R1–R4 on the X4 Pro. Record each result, with the firmware, under this file's Behavior verification. A failure becomes a new story before epic-pass-and-play starts | owner | verification | the invocation; R15 |
| AI-2 | Answer Q2 ("and N more" at the 64-game limit). If the recommendation is taken, build it with AI-14 as one small story: the launcher's note text, one `STR_` key, `installInbox` after a successful remove (rev-3), and the host tests that pin both | owner, then dev loop | remediation | Q2, rev-3; Behavior verification |
| AI-3 | Answer Q1 (the trailer policy). Then make the orchestrator's brief quote `orchestrated-epics.md:162` verbatim, so it stops restating the rule as "never in commits" | owner | process | proc-1 |
| AI-4 | Add a fidelity rule to `docs/crosshatch/orchestrated-epics.md`'s Build-agent brief and `docs/contributing/touch-and-ui.md`. A screen double records raw `text()` calls and never lays out on `\n` unless the renderer does. A host timing bound is derived from the device ratio (about 95×, `plan-e4-z1…:52`) and labelled an estimate until a device run measures it. A story that adds a double names the device behaviour it stands in for, and the test that pins the two agree. Fold `save_store_stubs/HalStorage.h` into `harness/stubs/HalStorage.h` in the next refactor sweep | owner (docs); dev loop (stub) | process, remediation | proc-2, agg-3 |
| AI-5 | Add a packet step to `orchestrated-epics.md`: before a device-run packet is built, every fault fixture whose outcome depends on device timing gets a tethered calibration run, or is listed in the packet as "uncalibrated: expected outcome estimated from the host ratio" | owner | process | proc-3 |
| AI-6 | Fix rev-12: move the concurrency group into the job, or key it so that only a `package-games` event cancels a run. Then two lines for AGENTS.md. (a) Under "Running and verifying": "Never add `labeled` or another event type to `crosshatch-ci.yml`; its per-PR `cancel-in-progress` group cancels the running checks and turns the required status red. A label-gated job goes in its own workflow file, as `crosshatch-game-packages.yml` does (9180a698)." (b) Add `test/game_script/GameIconsRaw.h` (regenerated by `scripts/gen_game_icons.py`, compared by CI) to the generated-files list | owner | process | proc-6, agg-7 |
| AI-7 | Two clauses for `orchestrated-epics.md`. (a) A measurement is taken on the commit it cites, after the last squash or amend; one taken before a history rewrite is void and is repeated. (b) A flake fix is proven at a stated repeat count; 20 full runs plus `--repeat until-fail:200` per test is the bar that 4.13 set | owner | process | proc-5, proc-7 |
| AI-8 | Add a line to `orchestrated-epics.md`: after a container restart, the orchestrator checks each builder's worktree and live sub-agents before resuming it, and records the restart and what was recovered in the builder's plan | owner | process | proc-4 |
| AI-9 | Record epic 4's delta in the spine's Operational envelope, Flash budget row: +4,416 B flash and +8 B static RAM over `962ae61`, at `7e7bcc48`, after PackBits saved 24,560 B; the compression lever is spent. At epic-pass-and-play's inception, re-measure the base, and name the next static-RAM lever: the games-on IRAM code (+768 B of the +784 B) | owner | spec reconciliation | proc-9 |
| AI-10 | Apply the spec reconciliations, with the evidence below (Proposed reconciliations) | owner | spec reconciliation | spec-2, spec-3, spec-4, agg-6 |
| AI-11 | Add a device step to epic-pass-and-play's closing run: a Continue whose `.pkg` will not read ends in the error view, and `resume.bin` is untouched | owner | verification | spec-4 |
| AI-12 | For the next refactor sweep, triggered by the next installer change: one directory-walk helper, a shared id-grammar check (export `validId`, and add the grammar to `package_vectors.json`), a path helper in `GamePaths.h`, and a split of `GamePackageInstaller.cpp` along the job boundaries in agg-1 | dev loop | remediation (deferred) | agg-1, agg-3 |
| AI-13 | Add `deferred-work.md` entries: A3 (split the Lua-literal vector notation out of `scripts/game_codec.py`; trigger "`pack_game.py` or any script first imports `game_codec.py`"), rev-7 (inbox names over 62 bytes skipped silently), and widen `## e4-x`'s aside item with rev-9 (the names running out reinstall a removed game) | owner | remediation (deferred) | spec-5, rev-7, rev-9 |
| AI-14 | One small fix story before epic-pass-and-play's first story: when `remove` cannot delete the `.pkg`, it removes its own `.removing` (best effort), so a game reported as kept stays kept. Add the marker assertion to `AMarkerThatWillNotGoLeavesTheGameListedAndWhole`, and rename it to what it tests | dev loop | remediation | rev-1 |
| AI-15 | Tests for the same story. The fake logs `exists`, and a test pins the `.xlink` probe's create → exists → remove order (rev-2). An installer suite overrides nothrow `new`, so `OutOfMemory` never sets a valid package aside (rev-4) | dev loop | remediation | rev-2, rev-4 |

### Proposed reconciliations (AI-10)

Proposals only. Each cites what the code does.

1. **Spine AD-16 (:274):** "The registry lists only … There is no separate index. A `.removing` marker in `/.games/<id>/` marks a remove to finish on the next visit. At most 64 games (`GameRegistry::MAX_GAMES`) are installed; a 65th waits in the inbox with its reason." (`GamePackageInstaller.cpp:333-393`, :748-778)
2. **Spine AD-16 (:275):** "An inbox file is deleted after a successful install, or renamed `<name>.chgame.installed` when the card will not delete it." (A30)
3. **Spine AD-17 (:282):** "A save whose package hash or codec version differs is not offered, and it is kept until the game's next match replaces it. A save that cannot be read keeps its Continue row, and the tap stops in the error view." (spec-4; entry 14 step 6)
4. **Spine AD-15 (:260), and `game-api-seed.md` §1:** "Also: the `.lua` members at most 256 KB together; each `.png` at most 2,048 × 3,072; `icon.png` square and scaling to exactly 64 px; manifest JSON nested at most 32 deep (`PackageLimits.h`, `package_vectors.json`)."
5. **Spine SD-card seed (:527-531):** add `/games/*.chgame.installed`, `/.games/<id>/.removing`, and `/.games-data/<id>/*.tmp`.
6. **Spine CI row (:540):** "The label-gated `Game packages` workflow (`crosshatch-game-packages.yml`) packs the device-run set; it is never required."
7. **`formats.md` (~:247):** replace "(the `.lua` total is added there in a later entry)" with "including the `.lua` total" (`pack_game.py:475`), and add the image-size and manifest-nesting rows to the limits table.
8. **`api-level-1.txt`:** add `limit manifest_nesting_count 32`, since `pack_game.py` refuses deeper manifests. Under the API freeze, the owner confirms it.
9. **SPEC CAP-3 success:** "… An invalid one is renamed `.bad` and its reason is shown once; a valid one that would exceed 64 games waits in `/games/` with its reason."
10. **`upstream-touches.md` row 5:** "its include" → "its includes (`GamesLauncherActivity.h`, `GameModeActivity.h`)".
11. **`game-canvas.md`, "The bound":** append "Measured on an X4 Pro: a forced exit with a stuck VM held `RenderLock` for 502 ms (entry 14)."

## Acceptance verdict

**Machine verdict: accepted-with-open-items. Criteria declared** (the epic file's Done when 1–5).

- **Tickets:** all 15 are `done`. `pending_tickets` is empty, and none is at `built`.
- **Done when 1–5 are met on the evidence** (spec):
  - Entry 14's device run covers the install, the hash, the reinstall and remove keeping `/.games-data`, the three-tap start, and Continue after sleep.
  - The host suites (1,361 of 1,361 at the merge) cover the hostile-zip rejections and paging.
  - CI was 20 of 20 on the merged head.
- **No blocking finding stands open.** Every high the reviews raised was patched in its story (proc-8). This retro's review found three mediums (rev-1 to rev-3). None loses saved data, and each has a fix-now item (AI-2, AI-14, AI-15) for a small story before epic-pass-and-play.
- **Open items, tracked:**
  - The owner's device recheck of e4-z1, e4-z2, e4-z3, and A8 (AI-1). These fixes merged after entry 14's sign-off, and only the host and the simulator have run them.
  - Q1 and Q2.
  - The proposed items AI-2 to AI-15.

If AI-1's recheck fails, the failure becomes a story and this verdict is revisited. The owner's own decision, when given, overrides this machine verdict and is recorded here.

## Open questions

**Q1. Trailers (proc-1).**
- The repo rule (`orchestrated-epics.md:162`) already exempts "the session's attribution lines", so the 111 model-named `Co-Authored-By` trailers comply. The choice:
  - (a) Keep the exception, and have the orchestrator cite the repo rule (AI-3).
  - (b) Strip the model name from future trailers (`Co-Authored-By: Claude <noreply@anthropic.com>`), and change :162 to say so.
- **Recommendation: (b).**
  - It is the harness's own "no model identifier in commits" instruction.
  - It costs nothing in attribution.
  - It removes the contradiction that produced proc-1.
  - This retro's own commits already use that form, pending the answer.
- The cost of (b): the harness's attribution reminder prescribes the model-named trailer, and gives way to the repo's own instruction. So (b) needs the rule written in the repo (AGENTS.md or `orchestrated-epics.md:162`), not only in a brief.
- Rewriting the 111 existing commits is not recommended: they are merged history on `develop`.

**Q2. "and N more" at the 64-game limit (`## e4-z3`).** The simulator shows the problem is sharper than the ticket described:
- At the limit every package after its manifest read is judged "too many". So the note reads "Too many games are installed; remove one first" / "and 2 more" whether the other two are valid packages waiting for room (`d-03`) or broken files (`d-05`).
- The broken ones keep their `.chgame` name and never show their reasons until a game is removed (A29, `GamePackageInstaller.cpp:796`).
- A non-zip file is set aside as `.bad` but counted in the same N (`d-06`).
- Reading "remove 3" is the smaller risk. The larger ones are that following the note does nothing until Games is reopened (rev-3), and that a removed slot can go to a package that then turns out broken.

**Recommendation:**
- When the first failure is `TooManyGames`, count the waiting packages separately from the rest.
- Replace the more-line with "N more are waiting" for the waiting ones, and "and N more not installed" for any others.
- Keep the installer's order unchanged: a package is judged only when there is room.
- After a successful remove, run the inbox install at once, so the waiting package fills the slot the person just freed (rev-3).
- This names a kind, the option `## e4-z3` offered, and adds one `STR_` key. It is a launcher-only change, AI-2.

The alternative is to validate a package fully before the limit check, so a broken one is set aside with its reason even at the limit. It costs an extract per waiting package on every visit, which is why A29 chose otherwise.

**Q3. The recheck's scope.** R2's optional step, a hand-placed `.removing` on the card, is the only check of FAT slot reuse and directory order for e4-z2 on real media. The simulator's ext4 cannot show either. The owner decides whether to run it; it takes one extra file copy.
