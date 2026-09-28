---
epic: epic-script-runtime
date: 2026-09-27
verdict: accepted-with-open-items
criteria: declared
headless: false
---

# Retrospective: epic-script-runtime

## Epic summary

**Epic:** `epic-script-runtime` (epic 2, "A sandboxed Lua game runs solo on the device"), folder `_bmad-output/initiative-crosshatch-player-v1/epic-script-runtime/`. The argument `epic-script-runtime` matched the `epics` row with slug `epic-script-runtime` (id 2) in `tickets.py status`; `find 2.1` gave its `epic_file`.

**Tickets.** There are 17, listed in build order from `tickets.py status`. Every one has `status = done` and `state = done`. `pending_tickets` is empty, and none is left at `built`. Commits are attributed by subject and plan; see "Ranges" below for why.

| Ref | Title | hitl | Covers | Plan baseline | Its commits |
|-----|-------|------|--------|---------------|-------------|
| 2.1 | Tracer: Home to Games to a Lua frame and a tap | yes | R1, R3, R4, R9 | `799d5add` | 97dcf52f, 100d07dd, dca1ddc2 (review fixes), cc87a398 |
| 2.2 | Fork script helper and one test step | no | R12 | `799d5add` | e3969f23 |
| 2.3 | Static RAM and static-initializer gates | no | R11 | `e3969f23` | 01dbb04d, 78c3c2b0 |
| 2.4 | API level, HostCaps, and Manifest::check | no | R9, R10 | `cc87a398` | 3d4aec97, 4779ab69 (format), b4b8631a |
| 2.5 | API freeze job and the level in release notes | yes | R10 | `b4b8631a` | b3f89a32, 67e077dd |
| 2.6 | Codec and golden vectors | no | R5 | `cc87a398` | 03f2ef3c, 6c6f32f0 |
| 2.7 | Sandbox, budgets, and the heap cap | no | R4, R6 | `6c6f32f0` | 601c0718, df9b09b5, 376a4c62 (review fixes), 124ac2cd |
| 2.8 | Game contract and the one-seat Session | no | R1, R5, R6 | `124ac2cd` | a4212569, cda0f3cc, c25a2ff6 (arena split, landed after 2.9) |
| 2.9 | The full ch.gfx and display-list limits | no | R2, R3, R6 | `cda0f3cc` | fcae82b3, f25d7c2f, 38ed65d0, aebea6f4 (format) |
| 2.10 | ch.timer, ch.store, ch.time, ch.log, and ch.api | no | R1, R2 | `aebea6f4` | e8420aaa, 2b65f1ef |
| 2.11 | FrameReplay, GameViewport, and touch input | no | R1, R3 | `2b65f1ef` | 13bcd449, 9592a642 |
| 2.12 | GameSaveStore for store.bin | no | R8 | `9592a642` | 5f8582c9, 6165a741 (format), 169ae291 |
| 2.13 | Solo match lifecycle and the error view | no | R6, R7 | `169ae291` | acf780ea, 43561686, b64f455d (review fixes), 932ef835 |
| 2.14 | Level-1 surface test | no | R10 | `9592a642` | 43c4e09f, aef4cc1c |
| 2.17 | Asset-name capacity and the probe's recorded values | no | R13 | `67e077dd` | ee76d675, 18d80a6e |
| 2.15 | Refactor sweep | no | R1–R13 | `932ef835` | ecba20a9, 213a59f5 |
| 2.16 | Solo fixture game on the X4 Pro | yes | R1–R11 | none (the plan has no `baseline_revision`) | 6634faae, fc4a48db |

**Ranges.**
- **Baseline order.** The baselines, oldest first by ancestry: 799d5add (2.1 and 2.2), e3969f23, cc87a398 (2.4 and 2.6), b4b8631a, 6c6f32f0, 67e077dd, 124ac2cd, cda0f3cc, aebea6f4, 2b65f1ef, 9592a642 (2.12 and 2.14), 169ae291, 932ef835. `git_evidence.py` ran once for each distinct range (scratchpad `ev/*.json`).
- **Why the rule's ranges misattribute commits.**
  - Two lanes ran in parallel in worktrees, and the orchestrator then replayed their commits onto one linear branch.
  - Because 2.1 shares its baseline with 2.2, its range is empty by the rule. All of 2.1's commits instead fall in 2.3's range, `e3969f23..cc87a398`.
  - The same happens elsewhere: 2.5's range holds 2.6's commits, and 2.17's range holds 2.7's.
  - The table above therefore attributes commits by subject and plan, not by range.
- **The epic-wide range** is `799d5add..d0d5d6d0`: 45 commits, 1 merge (PR #13, measured), and 293 files (+20,317 / −290). The aggregate views read this range.
- **2.16** has no baseline and no range of its own. Its two commits sit at the end of the epic-wide range.

**Delivery:** one PR, [CarpeTelam/crosshatch-player#13](https://github.com/CarpeTelam/crosshatch-player/pull/13), 44 commits, merged 2026-09-27T22:48:56Z by the owner. Its 17 check runs were all `success` on the head `fc4a48db`, including `Test Status`, `Crosshatch Test Status`, the five env builds, `unit-tests`, `clang-format`, `cppcheck`, `Upstream touch ledger`, `x4pro flash budget`, `API freeze`, `Fork script tests`, `Simulator build`, and `Title Check`. The merge came 9 s after `Crosshatch Test Status` finished, so the epic PR did not merge red (epic 1's P2 did not recur).

**Evidence inventory**

| Evidence | Status | Source |
|----------|--------|--------|
| Epic file with R1–R13, Done when 1–7, and owner decisions in Notes | present | `epic-script-runtime.md` |
| Initiative requirements (CAP-1, CAP-2, CAP-7) | present | `../initiative-crosshatch-player-v1.md` |
| Ticket entries (description, verify, covers) | present, 17 | `tickets.toml`, `tickets.py find` |
| Story files | none; no ticket was refined (`story_file: null`) | `tickets.py find` |
| Plans | present, 17, all `status: done`. 2.16's plan is a short record with no plan sections | `story-*-plan.md` |
| Per-ticket review triage | present for 15 plans, 9 to 23 rows each. **Missing for 2.1**: its plan has no Review Triage Log, although its independent review drove dca1ddc2 (finding O2). 2.16 had no review, as expected | each plan |
| Independent (orchestrator-run) reviews | 2.1, 2.7, 2.13. Recorded in the 2.7 plan (Plan Change Log, change 2) and the 2.13 plan (triage row 6, pass 2); for 2.1, only in dca1ddc2's message and the epic Notes | plans; commits |
| Simulator screenshots | present, 9 folders | `story-*-screenshots/` |
| Deferred work | 20 entries added during the epic | `_bmad-output/implementation-artifacts/deferred-work.md` |
| Spine amendments | AD-4, AD-5, AD-6 (601c0718, c25a2ff6) | `ARCHITECTURE-SPINE.md` |
| Commit and diff evidence | present; `git_evidence.py` for each range and for the whole epic | scratchpad `ev/` |
| CI | PR #13, 17 check runs green on `fc4a48db` | GitHub |
| Device runs | owner-run: the 2.1 tracer on dca1ddc2, and the 2.16 closing run on 6634faae (29 games) | 2.1 and 2.16 plans |
| Release dry run | owner-run on b3f89a32: "1.6.5-ch.3 · Game API 1 (preview)" | 67e077dd, 2.5 plan |
| Orchestration record | **not in the repo.** The owner supplied it for this retro as `epic-2-retro-input.md` (orchestration, incidents, open gaps) and `orchestrator-rules.md` (the brief every build agent followed). Findings that rest only on these are marked "(orchestration notes)" | attachments to this retro |
| Session logs | **not read.** Commits name the orchestrating session `session_01TdSkqyryGS7GgFNQBNEAVN`; no build-agent transcript was available. Process findings rest on the orchestration notes, plans, and commits | commit trailers |
| Previous retrospective | present | `../epic-platform-baseline/epic-platform-baseline-retrospective.md` |

**Going-in concerns.** The owner's attachments set the focus: the orchestration incidents (formatting drift, disk, sizing, a parallel fix that broke another story's tests, stale fixtures, the false red CI, toolchain setup, and fresh-clone wording), the open gaps, and where the rules file should live permanently (AI-12).

## Findings

Each finding carries its source and two dispositions: **instance** (fix now / defer / accept) and **prevention** (the upstream lesson). Sources, when a finding came from a delegated pass:

| Tag | Pass | Where the claim was re-checked |
|-----|------|--------------------------------|
| [agg] | aggregate views (include-graph and grep scripts) | against the source by this retro |
| [spec] | spec reconciliation | against the source by this retro |
| [rev-ci] | CI-and-scripts review | against the source by this retro |
| [rev-rt] | runtime review | against the source by this retro |
| [orch] | the owner's orchestration notes | none in the repo; each is marked where the repo has no evidence |

Line numbers are at `d0d5d6d0`.

### Process and orchestration

**O1. The build agents' own reviews missed every defect the independent reviews found.** High (process).
- Evidence:
  - The build subagents could not start subagents, so each ran its four review lenses itself, in the same context [orch].
  - The independent reviews of 2.1, 2.7, and 2.13 each found real defects:
    - 2.1: an overflow of the 16 KB VM stack, unclipped rects, an abandon that was unsafe as designed, and a screen-to-GameScript dependency. Fixed in dca1ddc2; the stack decision is in the epic Notes.
    - 2.7: the hook-blind paths, fixed in 376a4c62 (2.7 plan, Plan Change Log, change 2: "H1 … M1 … L1").
    - 2.13: stale taps after Play again, fixed in b64f455d.
  - The self-reviews rated none of these high. 2.13's triage row 6 says so directly: "Corrected after the independent review: the verdict 'false' assumed the queue always drains during Over". The item was first dismissed.
- Effect: the 14 stories without an independent review (2.2–2.6, 2.8–2.12, 2.14, 2.15, 2.17) were reviewed only by the agent that wrote them. The cross-ticket reviews in this retro still found medium defects in that code: R1 and R2 at the 2.9 × 2.10 × 2.11 boundaries, and F1 and F2 in the 2.3, 2.4, and 2.14 gates.
- Instance: **accept** (it is history). The open defects are routed individually.
- Prevention: AI-1. The orchestrator runs the review step with context-free subagents for every story, not only the high-risk ones.

**O2. 2.1's review findings are recorded nowhere a later ticket reads.** Medium.
- Evidence: the 2.1 plan has no Review Triage Log (sections: Intent … Design Notes, Verification). Its findings appear only in dca1ddc2's commit message and, for the stack, in the epic Notes.
- Effect: O3.
- Instance: **accept**. This retro now records them.
- Prevention: AI-1. The review record belongs in the plan, whoever runs the review.

**O3. A layering fix that 2.1's review won was undone two tickets later, and the sweep did not notice.** Medium.
- Evidence:
  - dca1ddc2: "GameMatchActivity reaches the VM only through GameVM … not lib/GameScript headers".
  - e8420aaa (2.10) re-added `#include <StoreSlot.h>` (`src/activities/games/GameMatchActivity.h:7`) and `#include <Codec.h>` (`GameMatchActivity.cpp:6`). The file now also uses `Codec::STORE_LIMIT` (:94), `StoreSlot` (:97, .h:104), `GameScript::Canvas` (:113), and `GameScript::InputEvent` (:298).
  - The spine's layer table gives Screens only `src/games/`, `GameCore`, `GfxRenderer`, and `UiListActivity`/`UiAppHost` (`ARCHITECTURE-SPINE.md:37`).
  - The sweep plan does not mention it. [agg], re-checked.
- Instance: **fix now** (AI-5). Reach the store slot and codec limit through `GameVM` or `GameSaveStore`, or amend the spine's Screens row if the owner prefers.
- Prevention: AI-5 also adds a mechanical include check for the spine's layer table. A rule that lives only in one commit message does not survive the next ticket.

**O4. A contract change left fixtures stale, and the host harness could not see it.** Medium.
- Evidence:
  - 2.8 made `status` required. After that, the `loop_draw`, `loop_in_pcall`, and `loop_input` fault scripts stopped on "game.status is not a function" before reaching their loop. 2.13's review found this; 43561686 fixed it.
  - The host suites stayed green because fault scripts run through `DirectGame`, "a LuaGame driven directly, without a Session" (`test/game_script/LuaGameFixture.h:68-97`, `useFault` at :132).
  - Only the `limits` bands go through the Session composition (`SessionGameTest.EveryLimitsFixtureBandIsAScriptError`, `SessionGameTest.cpp:97`).
  - [orch] and [spec], re-checked.
- Instance: **fix now** (AI-6). Add one host test that runs every `faults/*.lua` and every `loop` band through the Session composition and asserts the error text in `test/game_script/fixtures/README.md`. The README table then becomes the test's oracle.
- Prevention: the same test. A fixture's claim ("ends on X") is checked where the fixture is used.

**O5. A parallel fix broke another story's tests; only a combined-tree run caught it.** Medium.
- Evidence:
  - c25a2ff6 (the 2.8 arena split) landed after 2.9 was marked done (f25d7c2f). 38ed65d0 then had to fix two of 2.9's `GfxBindingsTest` cases: they "kept two LuaGames loaded on one arena", which the new 16 KiB reserve rightly refuses.
  - No plan has a Verification record for c25a2ff6 itself [spec].
  - The orchestrator caught the breakage by re-running `ctest` on the combined tree before pushing [orch].
- Instance: **accept**. Fixed in 38ed65d0.
- Prevention:
  - AI-12 makes "re-run the host suites on the combined tree before every push" a written orchestrator step.
  - A fix that lands outside its own story's session records its verification in the plan of the story it changes.

**O6. Formatting drift, three times.** Low.
- Evidence: 4779ab69, aebea6f4, and 6165a741 are each a `style:` commit, formatting test or source files after a story commit. The agents had reverted `clang-format-fix` edits outside their story's paths, or had run the formatter before their last edit [orch].
- After the rules file said "keep out-of-path formatting changes, run it last, run it twice", it did not recur: there are no `style:` commits after 6165a741.
- Instance: **accept**.
- Prevention: AI-12 moves that rule into AGENTS.md.

**O7. The disk ran out during 2.17's fresh-clone build and half-installed a shared package.** Medium; (orchestration notes only).
- Evidence [orch]: finished worktrees (about 1.5 GB each) and fresh clones (about 1.8 GB each) were never deleted. `~/.platformio/packages/framework-arduinoespressif32-libs` was half-installed for about five minutes, which could have broken a parallel build.
- The repo holds no trace of this.
- Instance: **accept**. The orchestrator cleaned up at the time.
- Prevention: AI-12, an orchestrator step: delete each worktree and scratch clone as soon as its work is merged.

**O8. A size was quoted to the owner before it was measured.** Low; (orchestration notes, supported by the repo).
- Evidence:
  - [orch]: about 320 KiB was quoted for Lua's region.
  - c25a2ff6 measured 448 KiB: "the cap times 1.6 for headers on the smallest objects, plus a measured fragmentation margin".
  - The owner accepted after the fact (epic Notes, "after 2.8").
- Instance: **accept**.
- Prevention: AI-12. A memory or flash figure goes to the owner as a measurement, with its method, or as "unmeasured".

**O9. `Crosshatch Test Status` reports failure on a superseded commit.** Low (noise).
- Evidence: `crosshatch-ci.yml` has `cancel-in-progress: true` (lines 31-33) and a rollup with `if: always()` that fails on `contains(needs.*.result, 'cancelled')` (lines 237-243). A push that cancels the previous run therefore turns the old SHA's required check red. [orch] and [rev-ci], re-checked.
- Caution: swapping in `if: ${{ !cancelled() }}` is unsafe. A skipped required check counts as passing, so cancelling the head run by hand would make the PR mergeable [rev-ci].
- Instance: **fix now** (AI-7). Keep `cancelled` fatal only when the run's SHA is still the PR head.
- Prevention: none beyond the fix.

**O10. Every parallel merge conflicted in `deferred-work.md`.** Low; (orchestration notes).
- Evidence [orch]: each story appended to the same file, and the conflicts were resolved by keeping both sides.
- Instance: **fix now** (AI-12). `.gitattributes`, which the fork owns, gets `_bmad-output/implementation-artifacts/deferred-work.md merge=union`. Keeping both sides becomes automatic.
- Prevention: the same.

**O11. The "fresh clone" wording did not fit the sandbox.** Low.
- Evidence:
  - 2.2 and 2.3 ran their gates on `git archive <commit> | tar -x` trees plus submodule archives, because the worktree guard blocked `git clone`. The plans record the deviation. 2.5 and 2.17 used real clones.
  - [rev-ci] traced every gate: none reads history the archive lacks. The only difference is `git_branch.py`'s version suffix, which is "unknown" in both builds, so the on/off delta is unaffected.
- Instance: **accept**.
- Prevention: AI-12. AGENTS.md accepts an archive tree for a gate that reads no git history.

**O12. Toolchain setup in cloud sessions is only partly in AGENTS.md.** Low.
- Evidence:
  - AGENTS.md records the penv certifi bundle and the `pioarduino==6.1.19` pin.
  - It does not say that the proxy blocks `github.com/.../archive/...` (403), so pioarduino has to come from PyPI. Only the simulator skill says so, and it recommends a git clone of platformio-core instead.
  - It does not say that the CA has to go into the `pio` tool's own certifi bundle as well as the penv's [orch].
  - In this retro's session, `uv tool install pioarduino==6.1.19` from PyPI worked, and the simulator build (native platform) needed no certifi change. The espressif32 download path was not exercised here.
- Instance: **fix now** (AI-12, AGENTS.md lines).
- Prevention: the same.

### Runtime lane: cross-ticket review [rev-rt]

The reviewer ran the adversarial, edge-case, and verification-gap lenses in turn over `lib/GameScript`, `lib/GameCore`, `src/games`, `src/activities/games`, and the ledgered Home and ActivityManager hooks. It weighted the boundaries between tickets and reproduced its suspicions in a scratch host project built from the real sources. This retro re-checked R1, R2, and R5 against the code.

**R1. Two `ch.gfx` faults can be caught with `pcall`, so a game survives them and a truncated frame is published.** Medium.
- Evidence:
  - `frameFull` and `drawTarget` raise with `luaL_error` (`lib/GameScript/ChBindings.cpp:38-47`), which `pcall` catches.
  - `ch.store.set` over its limit uses the guard's uncatchable `context.guard->raise` (:193), and so do the budget and headroom faults.
  - Reproduced: a `draw` that runs `pcall(ch.gfx.rect, …)` 2,100 times returns Ok, and the frame publishes cut at 2,048 commands (`outcome=0 gen=1 cmds=2048`). `pcall(ch.gfx.clear)` in `setup` also returns Ok.
  - The contract says both "stop the game" (`game-api-seed.md:198`), and the 2.9 plan's matrix says "frame not published".
  - `GfxBindingsTest` covers only the uncaught case.
- Boundary: 2.9 (the gfx limits) × 2.7/2.10 (the sticky-fault mechanism, which 2.9 did not use).
- Instance: **fix now** (AI-13). Raise both through `guard->raise`, and add `pcall` cases to `GfxBindingsTest`. Do it before level 1 freezes, or games may come to depend on catching them.
- Prevention: one rule for every contract-stopping fault ("raise through the guard"), and a test that runs each one under `pcall`.

**R2. A timer event can be evicted from the input queue, and the game's clock then stops for good.** Medium.
- Evidence:
  - `GameVM::pollTimer` calls `GameTimer::takeDue`, which clears `armed` (`GameTimer.cpp:23-29`), then posts a `Timer` event (`GameVM.cpp:158-165`).
  - `InputQueue::push` drops the oldest event of any kind when the queue is full (`GameInput.cpp:5-16`).
  - Result: if a queue's worth of touches arrives before the VM pops the timer event, the event is dropped while `pending()` is already false.
- Effect: a game that re-arms only from its `timer` event never ticks again. The `solo` fixture works this way (`main.lua:111-112`), so its 60 s round would never time out. It needs a slow `input` or `draw` and fast touching.
- 2.10's triage covers a timer evicting taps, not taps evicting a timer. No test covers it. Traced end to end; re-checked here.
- Boundary: 2.10 × 2.11.
- Instance: **fix now** (AI-13). Never evict a `Timer` event (evict the oldest touch instead), or disarm the timer only when the VM pops its event. Add a test.

**R3. Play again can still flash one old-round frame.** Low; PLAUSIBLE.
- Evidence: Play again snapshots `shownFrame = frameGen()` (`GameMatchActivity.cpp:163-167`). A step that the VM popped during Over and is still running can publish after that snapshot. The new round then full-refreshes that frame before its own first frame.
- b64f455d's fix (dropping queued taps) covers the queue, not a step already running.
- Instance: **defer** (AI-11).

**R4. Coordinates are clamped to int16 before clipping, which changes what is drawn.** Low.
- Evidence: `DisplayList.cpp:19-23` saturates to int16 at append time, before `FrameReplay` clips (`FrameReplay.cpp:115-140`). So `rect(-40000, 0, 80480, 800, …, true)` becomes a rect that is entirely off-canvas, and circles and far-anchored text move.
- Instance: **defer** (AI-11). Clip in wider integers at append time, or document the ±32,767 range in `api-level-1.txt` before the freeze.

**R5. Two SD failures in a row can delete the only saved copy of `ch.store`.** Low; reproduced.
- Evidence: in `GameSaveStore.cpp:86-105`:
  1. A flush removes `store.bin`, then fails its rename. The tmp file is now the only copy, and `loadStore` reads it (:34-38).
  2. The next flush opens that tmp file for writing, which truncates it. If that write fails, `Storage.remove(tmpPath)` deletes it.
- Reproduced: `store=0 tmp=0`, and a restart restored 0 bytes. The tests cover only single failures (`GameSaveStoreTest.cpp:207-250`).
- Instance: **fix now** (AI-13, small). While `store.bin` is missing, write to a second name, or promote the tmp file first.

**R6–R10. Low items.**
- R6, the error text can end in a broken character: `LuaGame::fail` cuts at 160 B with `snprintf` and can split a UTF-8 character that the error view then shows (`LuaGame.cpp:176-178`); `utf8Cut` exists in ChBindings.
- R7, abandon's wait undercounts: its loop counts only 5 ms per turn and ignores the settle ticks (`GameVM.cpp:219-237`). This is the same as deferred item (d) and AI-4.
- R8, a game cannot force a refresh of an unchanged frame: `ch.gfx.refresh("full")` on an identical frame is skipped, and the hint is reset (`RefreshPolicy.cpp:6`, `FrameBuffers.h:49-53`). That follows AD-7 literally, but it is undocumented for game authors.
- R9, binding tests drive a copy of the production loop: `LuaGameFixture.h:128-157`'s `SessionGame` copies the pre-`SoloRounds` loop, and `HostBindingsTest.cpp:40-55` re-implements `pollTimer`. A regression in `SoloRounds::step` fails only `SoloRoundsTest`.
- R10, the watchdog pauses while the light panel is open over a match (see the deferred-item note).
- Instance: R7 → AI-4. R6, R8, R9, and R10 → **defer** (AI-11). R8 is a line in the API docs (epic-game-api-docs).

**Checked and clean:**
- **No `RenderLock` deadlock.** It is taken only on the loop task (`onEnter` after unlock, `leave()`, `stopStuckVm()`), never in `onExit` or the destructor, and never by the VM; the frame mutex is always taken after it.
- **Render never reads a freed VM**, because every write to `vm` is under `RenderLock`.
- **Only the Session and one codec scratch use the arena reserve.** A `static_assert` bounds them (`LuaGame.cpp:25-28`, 13,720 B + ~1.8 KB < 16 KiB).
- **Both abandon paths are consistent:** delete-in-Lua frees the PSRAM; leak-all keeps the store slot.
- **The VM never touches `Storage`.** Every exit path flushes the store.
- **Stale events are handled:** old-round timers and post-cancel timers are dropped by serial, queued taps are cleared on Play again, and `frameGen` cannot cross matches.
- **No torn frames:** publish runs outside `inLua`, and render reads the front buffer under the mutex.
- **Without `pcall`, display-list overflow** reaches the error view unpublished.
- **All 22 `faults/*.lua` scripts reach the fixtures README's error text** when run through the Session path in a scratch harness that models the device's 16 KiB stack. The 43561686 fix holds, and AI-6's test is cheap to write.

**Sleep's worst case**, traced: `goToSleep` runs one more `loop()` pass, which does at most one `flushIfDue` write or one `stopStuckVm`. Then `onExit` waits up to 500 ms to join, up to about 500 ms more to abandon (more on another core; R7), and does one store flush. That is about 1 s plus up to two SD writes, all under `RenderLock`. 2.13's plan records it as a residual risk (line 133).

### CI and scripts lane: cross-ticket review [rev-ci]

**F1. A frozen function's signature or behaviour can change without tripping either guard.** Medium.
- Evidence:
  - `ApiSurfaceTest.ChTableMatchesTheList` and `GlobalsMatchTheList` compare only `name + " function"` (`test/game_script/ApiSurfaceTest.cpp:131-160`). The parameters, optional markers, and return types in `api-level-1.txt` are never checked.
  - The `API freeze` job diffs the list's bytes only. So changing, say, `ch.gfx.rect`'s `filled?` handling or `ch.text_width`'s return type passes both.
  - Enums, event fields, ctx, and limits *are* probed.
  - While level 1 is a preview, the freeze job always passes ("Nothing is frozen"), so the surface test is the only gate. That matches AD-19.
- Boundary: 2.4 (the list) × 2.5 (the job) × 2.14 (the test).
- Instance: **defer, with a trigger**. Before level 1 freezes (the end of epic-first-party-games), extend the surface test to call each `fn` with its documented arity and check the return type, or reduce the list's signatures to names and move signatures to `ch.d.lua` (epic-game-api-docs). AI-8.
- Prevention: a list entry that a test cannot check is documentation, not contract. Say which it is.

**F2. The static-initializer scan skips COMDAT sections before it checks for guard variables.** Medium.
- Evidence: `scripts/check_flash_budget.py:336-337` runs `if 'G' in section.flags: continue` before the `_ZGV` guard test (:338) and the 64 B test.
- On host g++, `object_problems()` reported nothing for:
  - a 4,096 B Meyers singleton in an `inline` function, with its guard;
  - a 1,024 B class-template static member;
  - a 256 B static in a function template instantiated in a game `.cpp`.
- AD-2 forbids dynamic initialization "at any scope". The 1 KiB RAM step catches only a large DRAM case, not PSRAM or anything under 1 KiB.
- 2.3's triage row 2 accepted inline *variables* only. Its Design Notes claim a static initializer "is still caught", which is false for lazily initialized local statics. [rev-ci], CONFIRMED by experiment; the line order re-checked here.
- Boundary: 2.3 × AD-2.
- Instance: **fix now** (AI-9). Test `_ZGV*` before the COMDAT skip, and stop skipping COMDAT symbols in game namespaces. Add a sidecar test for each escape.
- Prevention: a gate's test suite should include the "clever" escapes, not only the plain case.

**F3. The API freeze check judges a PR against the base as it was when CI ran.** Low–medium; PLAUSIBLE.
- Evidence: `scripts/check_api_freeze.py:56-66`, and `crosshatch-ci.yml` runs only on `pull_request`.
- Scenario:
  1. PR B edits `api-level-1.txt` while level 1 is a preview, and its CI passes.
  2. PR A (the freeze) merges.
  3. B merges without a re-run, cleanly, because the two touch different lines.
  - The frozen level has now changed, and nothing on `develop` re-checks it.
- It depends on "require branches to be up to date" being off; this retro cannot read that setting.
- Instance: **defer** to the freeze (AI-8). Turn on up-to-date branches or a merge queue, or add a `push: develop` run of the check.

**F4. The release preflight blocks every release while a later level is an open preview.** Low; spec question.
- Evidence: `scripts/fork_release.py:325-344` refuses a release when `API_LEVEL_FROZEN` is false. Once level 2 opens as a preview, which AD-19 and the freeze job allow, even a bug-fix OTA release is blocked. This follows the spine's own wording. [rev-ci], traced.
- Instance: **spec reconciliation** (AI-10). Decide whether a release may ship frozen level N while level N+1 is a preview.

**F5–F9. Low hardening items.**
- F5, the RAM gate leaves out IRAM: `.iram0.*` shares internal SRAM on the S3 (`check_flash_budget.py:71`). No game code uses `IRAM_ATTR` today.
- F6, the objects scan has blind spots: it does not scan `lib/lua`, guarded fork code inside ledgered upstream files, or a future game library not named `lib/Game*`. This is consistent with AD-2's wording, so the fix is documentation.
- F7, fork script tests can pass with no tests. A `*_test.py` that forgets `unittest.main()` exits 0 having run nothing (`crosshatch-ci.yml:198-207`); check for `Ran [1-9]`. All six files are fine today; this retro ran them: 10, 63, 18, 26, 71, and 21 tests.
- F8, the preview CRC ignores comment text: behaviour described only in comments in `api-level-1.txt` can drift without changing `API_SURFACE_CRC`, which AD-13 uses to match preview devices. It matters for epic-play-nearby.
- F9, `game_codec.py` leaks an exception: `encode('\ud800')` raises `UnicodeEncodeError`, not `CodecError` (`scripts/game_codec.py:92`). That matters for `pack_game.py`.
- Instance: **defer**, all as AI-11 (one hardening chore).

**Checked and clean** [rev-ci]:
- **C and Python codecs** read the same `test/game_script/codec_vectors.json`. They check the same limits. Their decode checks run in the same order. Vectors cover NaN, ±inf, −0.0, int vs float, the int64 edges, key order, non-UTF-8 bytes, duplicate keys, and depth 16/17.
- **The flash gate fails closed** on a missing tool, a missing section, or an unexpected flag state.
- **The freeze job** fails closed on a malformed header.
- **Wiring:** all five fork jobs are in `needs`. The 2.17 capacity is tied three ways (the `static_assert`, the vectors, and Python). The ledger covers every new path.

### Aggregate views [agg]

**A1. Architecture delta.** Mostly clean.
- Clean:
  - No lib depends on `src/`.
  - `GameCore` has no GameScript, Lua, Arduino, or HAL include.
  - There are no header-level cycles among fork modules.
  - Every `src/games/*.cpp` and `src/activities/games/*.cpp` is whole-file guarded (`ForkReleaseProbe.cpp` also excludes `SIMULATOR`, retro-1 AI-1).
  - Every upstream file the epic touched is a ledger row: `english.yaml`, `ActivityManager.*`, `HomeActivity.*`, `OtaUpdater.cpp`.
- Divergences:
  - O3 (Screens → GameScript).
  - `GameScript` → `lib/Utf8` (`TextMetrics.h:3`), and `src/games` → `EpdFont`/`fontIds.h` (`FrameReplay.cpp:7-15`). The spine's allowed-edge lists name neither.
  - `ActivityManager.h:20` rewraps upstream's one-line `HomeMenuItem` enum so a guard fits. That adds merge surface beyond the ledger row's intent.
- Instance: O3 is **fix now** (AI-5). The edges go into the spine reconciliation (AI-10). The enum rewrap is **accept** (ledger row 4 covers the file).

**A2. The spine's Structural Seed trails the code.**
- It does not name `MatchLifecycle`, `GameEvent.h`, the `IGameLog` port, `BlobHeader`, `CallGuard`, `CanvasClip`, `DisplayList`, `GameInput`, `GameSources`, `GameTimer`, `RefreshPolicy`, `SoloRounds`, `StoreSlot`, `TextMetrics`, `GameVM`, `GameClock`, `GameRandom`, `GameLog`, `GameTouch.h`, or `GameHostCaps`.
- The screen is `GamesListActivity`, not `GamesLauncherActivity`.
- AD-7's escalation policy lives in `lib/GameScript/RefreshPolicy.h`, not FrameReplay.
- The solo round loop is `GameScript::SoloRounds`, not GameCore.
- Instance: **spec reconciliation** (AI-10).

**A3. Size growth.**
- `scripts/game_codec.py` (768 lines) is the one grab-bag: encode, decode, blob header, a roughly 260-line Lua-literal vector-notation parser (333-593), the vector runner, and the CLI.
- `Codec.cpp` (612), `LuaGame.cpp` (483), and `Manifest.cpp` (480) are cohesive.
- `GameMatchActivity.cpp` (516) mixes lifecycle, VM supervision, gesture reading, and view building. AD-20 intentionally makes it the owner of all four.
- `check_flash_budget.py` (468) has a separable `objects` subcommand.
- Instance: **defer** (AI-11). Split the vector notation out of `game_codec.py` when `pack_game.py` first imports the codec.

**A4. Duplication.** Low.
- The cross-language codec is a checked mirror, not a copy (clean).
- Real duplicates:
  - The encode-error message building: `LuaGame.cpp:406-414` and `ChBindings.cpp:183-193`.
  - The recursion-headroom check: `ChBindings.cpp:141-146` and `Sandbox.cpp:56-58`.
  - `"/.games"` and `PATH_BUFFER = 96`, in both `GamesListActivity.cpp:25,27` and `GameAssets.cpp:17,47`.
  - Two unrelated 500 ms constants: `STOP_TIMEOUT_MS`, `GameMatchActivity.h:54`, and `ABANDON_WAIT_MS`, `GameVM.h:43`.
  - Fork scripts return literal codes (`check_flash_budget.py:272,426,453`, `check_api_freeze.py:43`, `fork_release.py:789`) although `fork_common.py:42-44` defines `PASS`/`FAIL`/`COULD_NOT_RUN`, against `docs/crosshatch/fork-scripts.md`.
- Instance: **defer** (AI-11).

**A5. Pattern divergence.** Mostly clean.
- Clean:
  - No bare `new` or `make_unique`; every `makeUniqueNoThrow` and `new (std::nothrow)` is null-checked.
  - No local array of 256 B or more.
  - No `Serial`, SdFat, `rowTouch`/`wasTapInRect`, or `RenderLock` in a destructor.
  - No mutable statics.
  - Screens build on `UiListActivity`/`UiAppHost`.
  - Every `.lua` fixture is under `test/game_script/fixtures/`.
- Divergences:
  - Untranslated English can reach the error view. The host-made strings in `LuaGame.cpp:191,193,214` ("not enough memory" when the scratch or state allocation fails, "game not started") reach it via `GameMatchActivity.cpp:254`, where AD-14 allows only Lua's own message untranslated. The Session out-of-memory case is translated (`STR_GAMES_OUT_OF_MEMORY`, :249-251); this retro narrowed the aggregate pass's broader claim to that.
  - Swipes are read from `gpio.wasSwipe()` (`GameMatchActivity.cpp:370`), bypassing `MappedInputManager`. A comment explains that it drops the start point, and AGENTS.md's rule is about the front buttons.
  - AD-4's `static_assert(sizeof(lua_Integer)==8)` is missing from `ArenaAllocatorTest.cpp`, `SandboxTest.cpp`, and `SessionGameTest.cpp`, which include Lua.
- Instance:
  - The English strings: **defer** (AI-11); first decide whether these count as "the script's own message".
  - The swipe read: **accept**.
  - The `static_assert`s: **defer** (AI-11).

### Spec-to-implementation reconciliation [spec]

| Req / Done when | As built | Evidence |
|---|---|---|
| R1 | Met. All six events are delivered, and `ctx = {seats=1, mode="solo", api}` | `LuaGame.cpp:59-104,419-426`; `SessionTest`, `LuaGameTest`, `HostBindingsTest`, `ApiSurfaceTest.EventsAndDirectionsMatchTheList` |
| R2 | Met. `print` is `ch.log` | `ChBindings.cpp:222-227,283-304`; `Sandbox.cpp:206-207`; `SandboxTest.PrintIsChLogNeverStdout` |
| R3 | Met. 2,048 commands / 32 KiB. The runtime views call `displayBuffer` directly (`GameMatchActivity.cpp:437`); that is outside FrameReplay, but those are system views, not the game canvas | `DisplayList.h:11-12`; `GfxBindingsTest` limit tests |
| R4 / DW2 wording | **Met, with a deviation.** A 256 KiB count cap on Lua's requested bytes, inside a 448 KiB Lua region plus a 16 KiB reserve, in one 464 KiB PSRAM block. This is the owner's decision after 2.8. R4 ("256 KB PSRAM arena"), Done when 2 ("the 256 KB arena"), tickets.toml entry 1 ("one 256 KB block"), and spine AD-6's first bullet (`ARCHITECTURE-SPINE.md:119`) still say 256 KB. Only the epic Notes and the AD-6 amendment say otherwise | `ArenaAllocator.h:12,27,30,33`; `c25a2ff6` |
| R5 / DW3 | Met. One vector file for both codecs; `formats.md` records the bytes | `test/game_script/codec_vectors.json`; `CodecTest`; `game_codec_test.py` |
| R6 | Met: every `ScriptError` kind reaches the error view (`GameMatchActivity.cpp:253`), each with a host test; the view itself is checked in the simulator and on the device. **Wording:** the 3 s watchdog is a cancel that *does* show the error view (the AD-5 amendment), which R6's "`Cancelled` shows nothing" does not mention | [spec] error-kind map; this retro's simulator run |
| R7 / DW4 | **Met, with additions:** a `Starting` state, and Back in Paused resumes. Neither is in AD-21. The canvas exception is in `docs/crosshatch/game-canvas.md` | `MatchLifecycle.h:9`; this retro's simulator run |
| R8 | Met. Only `GameSaveStore` touches `store.bin`; it flushes every 5 s, at Over, at Leave, and in `onExit` | `GameSaveStore.h:21,26`, `.cpp:26-28`; `GameMatchActivity.cpp:138,173,200`; `GameSaveStoreTest` (17) |
| R9 | Met. 16 `STR_GAMES_*` keys; ledger rows 4–7 | `Manifest.h:82,85`; `GamesListActivity.cpp:70-79`; `GameHostCaps.cpp:21` |
| R10 / DW6 | Met. The level-1 surface test passes; the dry run's notes read "1.6.5-ch.3 · Game API 1 (preview)" (F1: the surface test checks names only) | `ApiLevel.h`; `api-level-1.txt`; `ApiSurfaceTest`; 67e077dd |
| R11 / DW6 | Met. `RAM_BUDGET_BYTES: 1024` and the `objects` step (F2: COMDAT escapes). Deltas: flash +150,448 B, RAM +8 B, 35 objects | `crosshatch-ci.yml`; epic Notes |
| R11 wording | **The flash delta's baseline is not stated in the epic.** +150,448 B is the raw games-on minus games-off figure, the same method as epic 1's −15,136 B. So the runtime itself adds about 165,584 B over the pre-runtime state (on +166,960 B, off +1,376 B). The spine's envelope row states the −15,136 B baseline (`ARCHITECTURE-SPINE.md:505`); the epic's Notes do not | refactor-sweep plan; static-gates plan |
| R12 | Met. All five fork scripts import `fork_common.py`; one `Fork script tests` job | `scripts/`; `crosshatch-ci.yml` |
| R13 / DW7 | Met. At-capacity and one-over vectors; a guarded `static_assert`; the probe values beside ledger row 10 | `ForkRelease.h:34`; `OtaUpdater.cpp:57-60`; `upstream-touches.md:51-73` |
| DW1 | Met on the device (owner, 6634faae); host `SoloRoundsTest.TheSoloFixturePlaysToGameOverAndKeepsItsStore`; played again in this retro's simulator run | 2.16 plan |
| DW2 on the device | **Met, with two narrowings.** `faults/binary_chunk.lua` fails on the missing `load`, not on text-mode refusal of a precompiled chunk; that refusal is host-only (`LuaGameTest`, `"\x1bLua"`). Frame overflow on the device exercises only the command-count limit; the 32 KiB byte limit is host-only | fixtures README; `SandboxTest` |
| DW5 | Met. PR #13 was green on every job | check runs |

**Other divergences:**
- Behaviour nobody specified: load failures shown in the error view ("could not start"), `limit reject_reason_bytes 64` (`api-level-1.txt:218`), and the abandon "leak-all" fallback that also keeps the store slot (`GameVM.h:107-116`, `GameMatchActivity.cpp:221-227`). The spine has none of them.
- AD-5 says "signals a join semaphore" and "posts Quit". The code polls `finished()` every 5 ms, and the quit is an atomic plus a task notify (`GameVM.cpp:185-203`).
- The spine's Operational-envelope CI row omits `Simulator build` and `API freeze`.
- All of these go to AI-10.

**Deferred work added in this epic (20 entries).**
- Resolved (8): notes to the job summary, the English error screen, the skeleton libraries, the CanvasClip formatting, the codec stack (×2), C recursion, the heap cap vs the arena, and `text_width` vs kerning.
- Open (12): mostly one gap, below.
- **The `src/games` + `GameMatchActivity` harness gap is real, and is the epic's largest verification debt.**
  - No host test builds `GameMatchActivity`, `GameVM`, `GameAssets`, or `FrameReplay`. Only `GameSaveStore`, `GameTouch`, and `GameHostCaps` from `src/games` are host-built (`test/game_script/CMakeLists.txt:91-135`).
  - Seven deferred entries name that gap: the Games-list filter, `GameVM::run` composition and the watchdog, the `onEnter` Canvas, `pollTimer` and the stale-timer drop, FrameReplay wiring, the store restore and `flushIfDue` call, and the transition actions.
  - These paths have been exercised by hand in the simulator and on the device, never by an automated test.
- **The light-panel Home entry is refuted by the code** (the deferred entry at line 102 says the forced exit runs).
  - Home goes to `currentActivity->handleHomeGesture()` (`ActivityManager.cpp:108-112`), and the light panel's handler closes the panel (`FrontlightPanelActivity.cpp:221-224`). The match's `onExit` does not run. The orchestration notes' simulator observation agrees.
  - Side effect: while the panel is up, the match's `loop()` does not run, so the 3 s watchdog, `pollTimer`, and `flushIfDue` pause until it closes.
- **The device-run evidence for the loop fixture is incomplete.** The owner ran every `loop` band, "Stuck in one C call" included (2.16 plan). The deferred entry at line 68 asked for each band's outcome and the "VM stopped" stack high-water mark. Neither was recorded, so it is unknown which abandon path ran on the device (delete-in-Lua or leak-all).

### Previous-retro findings carried

See Previous-retro follow-through: all ten landed.

### What the evidence confirms went well

- **The epic PR merged green.** All 17 check runs passed on the head, and the owner's closing device run passed. Epic 1's P1/P2 (red merge, per-ticket CI claims) did not recur: verifies were phrased as local evidence, following retro-1 AI-7.
- **Upstream drift stayed within the ledger.** The epic touched six upstream-shared files: `english.yaml` (row 2), `ActivityManager.h`/`.cpp` (rows 4–5), `HomeActivity.h`/`.cpp` (rows 6–7), and `OtaUpdater.cpp` (row 10). All are guarded where the language allows, and the `Upstream touch ledger` job is green.
- **Flash and RAM stayed inside budget.** Flash was +146.9 KiB of 250 KiB and static internal RAM +8 B of 1 KiB, with every runtime buffer in PSRAM.
- **The independent reviews were decisive where they ran.** They produced the owner's three sandbox decisions and the arena split. The owner's decisions are all recorded in the epic Notes and the spine, with dates.
- **The two lanes worked.** The CI-and-scripts lane (2.2 → 2.3 → 2.5 → 2.17) never blocked the runtime lane, and the rules-file fix for formatting drift held after its third occurrence.
- **The fixtures README became the device-run script.** The owner, and this retro's simulator run, followed it step by step.

## Behavior verification

The epic changed runtime behavior throughout. What was exercised end to end:

| Flow | How | Observed |
|------|-----|----------|
| Host suites | this retro, `cmake`/`ctest` on `d0d5d6d0` in scratch | **623/623 pass** |
| Fork script tests | this retro, each `scripts/*_test.py` | all 6 pass: 209 tests (10, 63, 18, 26, 71, 21) |
| CI | PR #13 on `fc4a48db` | 17/17 green |
| Solo game, Done when 1 | this retro, simulator x4pro (built from `d0d5d6d0`), `solo` placed as `fs_/.games/solo/` | Home → Games lists 9 games; all 8 prompts hit with tap, long press, and swipe; the timer box filled; "Round over … Score 120, best 120"; "Over event received"; `solo: saved ch.store (22 bytes)` |
| Play again, pause, leave, sleep, Done when 4 | same session | Play again → Round 2, "Hits 0", empty checklist (no stale taps: the b64f455d fix holds); Back → Paused; Resume; Home → Paused; Leave → Games; reopen, then `key sleep` → `Playing -> Leaving on ForcedExit`, "VM stopped", deep sleep, with no hang |
| `ch.store` across a restart | same session, after the sleep restart | "Round 2 … Rounds finished 1 … Best 120"; `restored ch.store (22 bytes)` |
| Faults, Done when 2 | same session: `binary_chunk`, `gc_loop`, `heap`, `lua_error`, `nested_pcall`, `table_move`; `loop` bands 1–5; `limits` bands 1–6 | each ended in the error view with the README's text, and Back returned to Games. `heap`: "not enough memory" at arena peak 349,328 B. `loop` band 5: "VM did not stop within 500 ms of cancel; abandoning it … the simulator cannot stop its thread, so all of it is leaked … The ch.store slot stays with the leaked VM"; then `solo` reopened normally |
| Device, Done when 1, 2, 4 | the owner, X4 Pro, firmware 6634faae, 29 games | passed (2.16 plan) |

Narrowed:
- The simulator cannot delete a thread, and it reads stack headroom as a constant ("stack high-water 2048 bytes free"; "least at a hook 0 bytes" at `-O0`). So abandon's delete path and the 16 KB stack budget are device-only evidence, and the device run did not record them (see Spec).
- This retro did not rebuild the five firmware envs or run `pio check`; PR #13's CI covers them.
- It did not run the release dry run; 67e077dd records the owner's.

## Previous-retro follow-through

From `epic-platform-baseline-retrospective.md`, Action items AI-1 to AI-10:

| Item (owner) | Landed? | Evidence |
|---|---|---|
| AI-1: a simulator build in fork CI; guard the probe with `!defined(SIMULATOR)` (dev loop) | **landed** | c1d4912c; `crosshatch-ci.yml:133` `simulator-build`, in `needs`; `src/games/ForkReleaseProbe.cpp:3`; green on PR #13 |
| AI-2: decide the remaining 1.6 device rows (owner) | **landed (decided)** | 02acb404; `deferred-work.md:41`: "host tests are enough for the 404 and network-failure rows for now" |
| AI-3: harden `fr build` against clean-on-checksum (dev loop) | **landed** | b3f89a32; `scripts/fork_release.py:16-18,537-542` checks and copies each image right after its own `pio run` |
| AI-4: `HttpDownloader.*` in the row-10 dry-run rule (dev loop) | **landed** | a66bac42; `docs/crosshatch/upstream-touches.md`, "Row 10 and fork releases" |
| AI-5: a fork script helper and conventions before new scripts (dev loop) | **landed** (moved into this epic as 2.2) | e3969f23; `scripts/fork_common.py`; `docs/crosshatch/fork-scripts.md`. The optional composite action was not done; the setup is now copied three times (A4) |
| AI-6: spine reconciliation (probe, seed, AD-2 wording, R4/R5 readings) (owner) | **landed** | 9ca639ca, f09fbf8c, a8d20a14 |
| AI-7: one PR per epic; verifies as local evidence; never merge red (owner) | **landed** | a2d93cf8 (AGENTS.md Policy); PR #13 merged green |
| AI-8: fresh-clone runs for gates (owner) | **landed** | a2d93cf8 (AGENTS.md); runs recorded in the 2.2, 2.3, 2.5, and 2.17 plans (O11 on the wording) |
| AI-9: record the games-on minus games-off difference at the first `lua_*` reference (dev loop) | **landed** | the 2.1 plan's Verification: +120,496 B; the spine envelope records the −15,136 B baseline (`ARCHITECTURE-SPINE.md:505`) |
| AI-10: AGENTS.md pitfalls for the penv certifi and `sim.sh setup` during a build (owner) | **landed** | a2d93cf8; AGENTS.md "Running and verifying" and "Known pitfalls" |

## Action items

All items are **proposed**; this retro applied none of them. *Remediation* goes to the dev loop as story-shaped work. *Spec reconciliation* awaits the owner's application to the spine or epic. *Process* items change AGENTS.md, docs, or the BMAD customization.

| # | Kind | Action | From | Owner |
|---|------|--------|------|-------|
| AI-1 | process, fix now | **Independent review for every story.** The orchestrator runs each story's review step with context-free subagents, since a build subagent cannot start its own. Either the build runs with `review=none` and the orchestrator runs `bmad-review` (or bmad-build's thorough lenses) on the story's commit, or the orchestrator reviews after each build commit. The build agent then triages the findings into the plan's Review Triage Log, so every plan, 2.1-style reviews included, keeps its record. Written into the orchestration doc (AI-12) | O1, O2 | owner (process), orchestrator |
| AI-2 | remediation, fix now | **Close the `src/games` harness gap.** Build a host harness that links `GameVM`, `GameAssets`, `FrameReplay`, and `GameMatchActivity` against the simulator's FreeRTOS shim (or stubs), or add a scripted simulator run to the `Simulator build` job (`sim.sh script …` over the fixtures README's steps, asserting log lines). It covers the seven open deferred entries on that gap | Spec, deferred work | dev loop: the first ticket of the next epic that touches `src/games` (epic-install-and-launcher) |
| AI-3 | deferred check, owner | **Record the device evidence the closing run did not.** On an X4 Pro, run the `loop` fixture and note each band's outcome and the "VM stopped" line's stack high-water mark, including which abandon path "Stuck in one C call" took ("Abandoned the stuck VM" or "leaking all of it"). This closes `deferred-work.md:68` and gives the one device measurement of abandon | Spec, deferred work | owner (hitl) |
| AI-4 | remediation, defer | **Bound sleep's time under `RenderLock`** (also R7). `onExit` runs under ActivityManager's RenderLock: a 500 ms join, then `abandon`, whose loop counts only `STOP_POLL_MS` (5 ms) per turn while `deleteIfStuckInLua` can add up to 10 `vTaskDelay(1)` ticks each turn (`GameVM.cpp:219-250`). The stated worst case of about 1 s can therefore be exceeded. Count real elapsed time (`millis()`) in both loops, and record the bound in `deferred-work.md` and game-canvas.md | Deferred item (d), re-checked | dev loop |
| AI-5 | remediation, fix now | **Restore the Screens layering.** Remove `StoreSlot.h`/`Codec.h` and the `GameScript::` uses from `GameMatchActivity` by routing through `GameVM`/`GameSaveStore`, or amend the spine's Screens row (`ARCHITECTURE-SPINE.md:37`) to allow `lib/GameScript`. Then add a small fork CI check (or a step in the ledger job) that fails an `#include` edge the spine's layer table does not allow | O3, A1 | dev loop; owner picks fix vs amend |
| AI-6 | remediation, fix now | **One test that every fixture reaches its named fault as a game.** A host test that runs each `faults/*.lua` and each `loop` band through the Session composition (as `SessionGameTest.EveryLimitsFixtureBandIsAScriptError` does for `limits`) and asserts the fixtures README's error text | O4 | dev loop |
| AI-7 | remediation, fix now | **Stop the false red on superseded commits.** In `Crosshatch Test Status`, before the fail step, compare the run's SHA with the PR's current head (`gh api …/pulls/N --jq .head.sha`, `pull-requests: read`); if they differ, pass with a notice. Keep `cancelled` fatal for the head, and do not use `!cancelled()` | O9 | dev loop (CI chore; fresh-clone gate rule applies) |
| AI-8 | remediation, deferred with trigger | **Before API level 1 freezes:** make the surface test check each `fn`'s arity and return type (or move signatures out of the list into `ch.d.lua`), and close the stale-base window with required up-to-date branches, a merge queue, or a `push: develop` run of the freeze check | F1, F3 | dev loop: epic-first-party-games, before the freeze; owner for the branch setting |
| AI-9 | remediation, fix now | **Close the static-initializer scan's COMDAT escape.** In `check_flash_budget.py`, test `_ZGV*` before the COMDAT skip and stop skipping COMDAT symbols in game namespaces; add sidecar tests for the inline-function static, the template static member, and the function-template static | F2 | dev loop (gate change: fresh-clone run) |
| AI-10 | spec reconciliation | **Update the spine and epic to the as-built:** AD-6's first bullet and R4, Done when 2, and tickets.toml entry 1 say 256 KB, against the 256 KiB cap in a 448 + 16 KiB block; R6 notes the watchdog's error view; AD-21 gets `Starting` and Paused+Back=Resume; AD-5's join wording (polling, an atomic plus a notify); the envelope's CI row adds `Simulator build` and `API freeze`; the Structural Seed names (A2); the allowed edges (A1); the leak-all fallback and `reject_reason_bytes`; the epic Notes' flash delta names its −15,136 B baseline (≈ +165.6 KB of runtime); F4, whether a release may ship frozen level N while level N+1 is a preview | Spec, A1, A2, F4 | owner, via `bmad-architecture` update |
| AI-11 | remediation, defer | **One hardening chore:** F5 (IRAM in the RAM gate), F6 (document the objects scan's scope), F7 (fail on "Ran 0"), F8 (decide whether the preview CRC should cover comment-level behaviour, before epic-play-nearby), F9 (`CodecError` for lone surrogates); A3 (split the vector notation out of `game_codec.py` when `pack_game.py` imports it); A4 (shared message and headroom helpers; the `/.games` and path-size constants; `fork_common` exit codes); A5 (translate or classify the runtime error strings; AD-4 `static_assert`s in three tests); R3 (skip frames published before the new round's first); R4 (clip before the int16 clamp, or document the range); R6 (`utf8Cut` in `LuaGame::fail`); R8 (document that an unchanged frame never refreshes); R9 (move the `SessionGame` fixture and `HostBindingsTest`'s poll onto `SoloRounds`/`GameVM::pollTimer`); R10 (watchdog while the light panel is open) | F5–F9, A3–A5, R3, R4, R6, R8–R10 | dev loop: the sweep of the next epic; R8 to epic-game-api-docs |
| AI-12 | process, fix now | **Give the rules file a permanent home** (decision below), and fold in the orchestration lessons: formatter last and twice (O6), worktree cleanup (O7), measure before quoting (O8), `merge=union` for `deferred-work.md` (O10), archive trees for fresh-clone gates (O11), the PyPI and two-bundle toolchain facts (O12), and the combined-tree host-test run before each push (O5) | O5–O8, O10–O12; the owner's request | owner, via `bmad-project-context` (AGENTS.md) and a docs commit |
| AI-13 | remediation, fix now | **Three runtime defects at ticket boundaries:** (a) `frameFull` and `drawTarget` raise through `guard->raise`, so `pcall` cannot swallow them, with `pcall` cases in `GfxBindingsTest` (R1); (b) a `Timer` event is never evicted from the input queue, or the timer disarms only when its event is popped, with a test (R2); (c) `GameSaveStore::flush` never truncates the tmp file while it is the only copy (R5), with a two-failure test. All three before API level 1 freezes | R1, R2, R5 | dev loop: one fix story before or at the start of the next epic |

**AI-12, decided placement of `orchestrator-rules.md`.** Its content is split by who needs it, rather than kept in one file:

1. **AGENTS.md: facts true for every agent in this repo, orchestrated or not.** These go in as short lines in the existing sections:
   - **Running and verifying:**
     - Run `./bin/clang-format-fix` as the last step before a commit, after every edit, then run it again and confirm `git status` shows nothing new. Keep any formatting-only change it makes outside your paths, never revert it, and name it.
     - Never run two builds at once against one checkout or one `~/.platformio`. When several agents share a machine, wrap every `pio run`, `pio check`, `pio project metadata`, `sim.sh setup`/`build`, and host-test CMake build in one shared `flock <lock-file>`.
     - A git worktree needs `git submodule update --init --recursive` too.
     - The fresh-clone gate rule accepts `git archive <commit> | tar -x` plus each submodule's archive when `git clone` is unavailable, for a gate that reads no git history.
     - pioarduino comes from PyPI (`pioarduino==6.1.19`), because the proxy returns 403 for GitHub archive URLs. The proxy CA goes into the certifi bundle of the environment that runs `pio` as well as the penv's.
     - Delete a finished worktree or scratch clone once its work is merged, because each takes 1.5–1.8 GB and a full disk half-installs shared `~/.platformio/packages`.
   - **Where things are:** game fixtures live in `test/game_script/fixtures/`, never `games/`, which the release packs.
   - **Policy:** `check_upstream_touches.py` needs an `upstream` remote with `develop` fetched and an unshallowed clone.
2. **`docs/crosshatch/orchestrated-epics.md` (new): how an orchestrated epic runs.** The file has two sections.
   - **Orchestrator procedure:**
     - lanes from the epic Notes, one worktree per lane;
     - the shared build lock;
     - no merges into a tree whose agent is still working;
     - independent review subagents for every story (AI-1);
     - the combined-tree host-test run before each push (O5);
     - worktree cleanup (O7);
     - measure before quoting (O8);
     - owner hand-offs;
     - marking tickets done;
     - the five-env build before the epic PR.
   - **Build-agent brief:** the rules file, generalized.
     - Session paths become placeholders: `{epic-folder}`, `{ref}`, `{lock}` (a lock file in the orchestrator's scratchpad), and `{scratch}/{ref}/`.
     - The hard-coded `Claude-Session` trailer becomes "end the commit with the attribution lines your session's system gives".
     - "Amelia" becomes "the orchestrator".
     - Epic-2 story numbers become "a story that adds or changes a CI gate or workflow".
     - It keeps the gate pre-answers (keep all goals, keep the full plan unless padded, approve checkpoint 1), the open-question rule (resolve from the repo, spine, epic, earlier plans, and git history; otherwise stop and ask, with options and a recommendation), one local commit and no push, never `tickets.py mark`/`pull`, and the 300-word report with blocking questions first.
   - It goes in `docs/crosshatch/`, not `docs/contributing/`. `docs/contributing/` is upstream's contributor documentation, and AGENTS.md's policy keeps fork-only material in fork-owned paths. `docs/crosshatch/` already holds the fork's process docs (`fork-scripts.md`, `upstream-touches.md`).
3. **`_bmad/custom/bmad-build.toml` (new): a one-line pointer, not the rules themselves.**
   - Add one `persistent_facts` entry: "When the prompt says this build runs for an orchestrator, follow `docs/crosshatch/orchestrated-epics.md` § Build-agent brief; its answers replace this workflow's human gates."
   - The gate answers must not go into the customization directly. It applies to every bmad-build run, and pre-approving checkpoint 1 there would remove the human gates from interactive builds too.

**Owner decisions (2026-09-28):** the owner accepted every recommendation below.
- **Order:** the fix-now items land in one follow-up PR before epic 3 (epic-icon-library) is planned, starting with the process items:
  1. AI-12 and AI-1;
  2. AI-13, AI-9, and AI-7;
  3. AI-5 and AI-6;
  4. AI-10.
- **Deferred:** AI-2, AI-4, AI-8, and AI-11 keep their triggers. AI-3 stays the owner's device check.
- **AI-5:** restore the rule (Screens reach GameScript only through `src/games`) and add the include check; the spine keeps its Screens row.
- **AI-2:** a scripted simulator run in CI first, a host harness later. AI-3 stays needed for abandon and the stack budget.
- **AI-10, `tickets.toml`:** entries stay untouched as the record of what each build was given (orchestrator call during the follow-up); only the spine and the epic file are reconciled.
- **AI-13, the `__close` gap:** the follow-up review found that a `__close` metamethod raising while a memory error unwinds could still let a script survive the heap cap under `pcall`. `setmetatable` now refuses `__close` and the string metatable is sealed. A `__close` added to a metatable after `setmetatable` remained open. The owner chose option (a) (2026-09-28): record the memory fault where Lua throws, through a `LUAI_THROW` hook supplied by a fork header and build flag. The Lua sources stay unmodified; this is an AD-4 amendment.

## Acceptance verdict

**Machine verdict: accepted-with-open-items**, criteria **declared** (the epic file's Done when 1–7). `pending_tickets` is empty; all 17 tickets are `done`.

| Done when | Result | Evidence |
|-----------|--------|----------|
| 1. Solo fixture from Home → Games, played to game over on an X4 Pro with every input kind, `ch.timer`, and `ch.store` across a restart | met | owner device run (2.16, 6634faae); this retro's simulator run; `SoloRoundsTest` |
| 2. Each fault ends in the error view with Back, in host tests and on the device, and the device stays responsive | met as worded, with the deviation that the "256 KB arena" is the 256 KiB cap in a 464 KiB block (owner decision), and the narrowings that the binary chunk and the frame byte limit are host-only on their exact path. Beyond the criterion: frame overflow and gfx-outside-draw can be swallowed by `pcall` (R1) | host tests (error-kind map); owner device run; this retro's simulator run |
| 3. The C codec and `game_codec.py` pass the same golden vectors; `formats.md` records the bytes | met | `codec_vectors.json`; `CodecTest`; `game_codec_test.py` |
| 4. Back and Home open pause, Leave, Play again, sleep without deadlock; the canvas exception recorded | met | owner device run; this retro's simulator run; `docs/crosshatch/game-canvas.md` |
| 5. Merged with the five-env build, host suites, format, `pio check`, and every fork job green | met | PR #13, 17/17 green on the head |
| 6. Fork CI fails on RAM growth, static initializers, a frozen-level change; the surface test passes; the dry run names the level | met as specified, with gate holes F1 and F2 (a signature change and COMDAT statics escape) | `crosshatch-ci.yml`; `check_flash_budget_test.py`; `check_api_freeze_test.py`; 67e077dd |
| 7. `ASSET_NAME_CAPACITY` vectors and a guarded `static_assert`; the probe values beside row 10 | met | `ForkReleaseTest`; `OtaUpdater.cpp:57-60`; `upstream-touches.md` |

No finding is blocking. Every Done-when criterion holds in the final state and is supported by CI, host tests, the owner's device run, and this retro's simulator run. The verdict carries open items:
- **Fix-now remediations:** AI-5, AI-6, AI-7, AI-9, and AI-13.
  - R1 and R2 break the game contract (a stopping fault that a game can survive; a timer that can stop for good), but only when a script uses `pcall` on purpose or while touches pile up. The owner's device run did not hit either, and level 1 is still a preview (`API_LEVEL_FROZEN false`), so they are fixable without an API break. For those reasons they are not blocking.
- **The device evidence the closing run did not record:** AI-3.
- **The biggest verification debt:** AI-2, the untested `src/games` wiring.
- **Spec reconciliation:** AI-10.
- **Process changes:** AI-1 and AI-12.
- **Deferred hardening:** AI-4, AI-8, and AI-11.

**Human decision: accepted-with-open-items** (owner, 2026-09-28). R1 and R2 are not blocking; AI-13 fixes them before API level 1 freezes, in the follow-up PR described above.

## Open questions

Answered by the owner on 2026-09-28:
- **R1/R2:** not blocking; fixed first, as AI-13.
- **AI-5:** restore the layering and add the include check; no spine change.
- **F4:** yes. A release needs only the levels below an open preview to be frozen. The spine (AI-10) and `fork_release.py` change to match.
- **F3:** turn on "require branches to be up to date" for `develop`. That is a repository setting the owner applies; the surface-test half of AI-8 stays deferred to the freeze.
- **AI-2:** a scripted simulator run in CI first.
- **Deferred item (b):** closed as refuted by the code, with the watchdog-pause side effect recorded in `deferred-work.md` (R10).

## Addendum: follow-up applied (2026-09-28)

The owner-approved fix-now items landed on this retro's branch before epic 3 was planned. They were built as parallel `/bmad-build` lanes in worktrees, and every lane got an independent context-free review (AI-1 applied). Each lane's plan and Review Triage Log is `_bmad-output/implementation-artifacts/plan-e2r-*.md`.

| Item | Commits | Notes |
|---|---|---|
| AI-12, AI-1 | 762947e4, 88d19c20 | `AGENTS.md`, `docs/crosshatch/orchestrated-epics.md`, `_bmad/custom/bmad-build.toml`, `merge=union` for `deferred-work.md`; deferred item (b) closed with the R10 note |
| AI-10 | 4cace128, 56e99944, 93b1590f | spine and epic reconciled; `tickets.toml` left as history |
| AI-9, AI-7, F4 | b05a21ca, 7324aaaa | COMDAT and template escapes closed; superseded-run pass; release allowed with a later open preview |
| AI-13, AI-6 | fdd82fda, da88f053, 48ceb33e, 79e305e3, c6ef1419 | gfx, heap-cap, table-limit, and headroom faults sticky under `pcall`; the `LUAI_THROW` hook (owner option a); `__close` refused and the string metatable sealed; timer events kept; the only store copy kept; fixtures checked against the README |
| AI-5 | 7a94a979, 8ce75fb8 | `src/games/MatchStore`; the `Layer check` CI job |

**Verified on the combined tree** (c6ef1419):
- the five firmware envs build;
- `pio check` passes on default and x4pro;
- the simulator x4pro build succeeds;
- host tests pass, 641/641;
- all seven `scripts/*_test.py` suites pass;
- `check_layers.py` and `check_upstream_touches.py` pass;
- `clang-format-fix` leaves no diff.

**New deferred items**, under `## e2r-*` headings in `deferred-work.md`:
- free inline functions as game names in the objects scan;
- nothing compares `check_layers.py`'s table with the spine's;
- an owner question on `API_MIN_LEVEL` under AD-19.

Still deferred with their triggers: AI-2, AI-3 (owner), AI-4, AI-8, AI-11.
