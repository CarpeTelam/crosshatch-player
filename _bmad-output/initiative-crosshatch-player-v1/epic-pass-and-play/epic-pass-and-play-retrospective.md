---
epic: epic-pass-and-play
date: 2026-10-03
verdict: accepted-with-open-items
criteria: declared
headless: false
---

# Retrospective: epic-pass-and-play

Status: complete. Run interactively; the machine verdict is below and a human decision overrides it.

## Epic summary

Epic: `epic-pass-and-play` (id 5), "One device passes between players and hidden information stays hidden". Risk high. Criteria: declared (the epic file has five Done when items).

Tickets (from `tickets.py status epic-pass-and-play`, build order):

| ref | title | status | state |
|-----|-------|--------|-------|
| 5.1 | Tracer: an open pass match from the picker to game over | done | done |
| 5.2 | Pass lifecycle: Result, HandOff, and the seat each state draws | done | done |
| 5.3 | Title screen design and the AD-22 note (owner `bmad-ux` session, no build) | done | done |
| 5.4 | The hidden hand-off in the match | done | done |
| 5.5 | resume.bin for pass matches | done | done |
| 5.6 | The forced exit's blank hand-off | done | done |
| 5.7 | The title screen | done | done |
| 5.8 | One launcher row per game | done | done |
| 5.9 | Continue a pass match | done | done |
| 5.10 | Refactor sweep | done | done |
| 5.11 | Device run and owner sign-off | done | done |
| 5.12 | Build the approved title-screen, Options, and hand-off design | done | done |
| 5.13 | Plain tap targets for the hand-off | **built** | **review** |

`pending_tickets` is empty: 5.13's `status` is `built`, which counts as finished by the build. It is still at `built`, not called done, and its state is `review`. Nothing forces the machine verdict to rejected on completeness grounds.

How the epic was resolved: no epic was named at invocation; the user pointed at the epic the session is on, and `epic-pass-and-play` is the only epic with in-flight work (12 of 13 done, 5.13 in review).

Delivery: the epic landed on `develop` as one squash commit, `17d02483` ("feat: pass-and-play on one device with a hidden hand-off (#24)", parent `192706f0`): 204 files, +17,561 / -2,355.

### Plan ranges

Each plan's `baseline_revision` is the commit its build started from. Ordered oldest first; checked by `git merge-base --is-ancestor` along the chain. The squash means the baselines are not ancestors of `develop`, but their objects are in the clone (except 5.13's).

| tickets | range | commits | source |
|---------|-------|---------|--------|
| 5.1 | `c1902721..7c1bf335` | 5 | recorded baseline to next baseline |
| 5.2 | `7c1bf335..08419987` | 4 | recorded |
| 5.4 | `08419987..dbc964f7` | 4 | recorded |
| 5.5, 5.6 | `dbc964f7..fb315fd8` | 6 | shared baseline `dbc964f7`, so one range covers both and the earlier plan has no commits of its own |
| 5.7 | `fb315fd8..8b7b8f68` | 4 | recorded |
| 5.8, 5.9 | `8b7b8f68..d2b5cd38` | 8 | shared baseline `8b7b8f68` |
| 5.10 | `d2b5cd38..0ebe76a4` | 34 | recorded |
| 5.12 | `0ebe76a4..70276092` | 22 | **cut recorded**: `70276092` is the tip of `origin/claude/tender-rubin-vhrc0p`, the last commit before 5.13; the 5.12 plan's own next baseline is not recorded |
| 5.13 | `70276092..17d02483` | 2 | **inferred**: 5.13's baseline `0438a669` is not in this clone (`git cat-file` fails), and its commits and `64842706` exist only inside the squash. This range's file list also includes upstream merges that `develop` took after the epic branch forked, so it overstates 5.13's own churn |
| 5.3, 5.11 | none | | no `baseline_revision` (owner design session, device run): no commit or diff evidence |

Per-range `git_evidence.py` output is kept in the session scratchpad (`ev-*.json`).

### Evidence inventory

Available: epic file, initiative requirements, all 13 plans, `cross-story-review.md`, `device-run-packet.md`, the previous retrospective (`epic-install-and-launcher-retrospective.md`), per-range git evidence for eleven tickets, the squash commit, the pre-squash branch tip.

Missing: 5.13's baseline commit and its individual commits; any session logs (not present in the repo or session, so process-lesson analysis is skipped and said so below); 5.3 and 5.11 have no diff range.

## Findings

Provenance: aggregate views and the three review lenses ran as read-only subagents over the squash diff `192706f0..17d02483` (fork-owned paths, 87 files). Their reports are unverified until re-checked; each finding below says **verified** (I re-read the cited source) or **unverified** (agent report only). Disposition is for this instance: fix now / defer / accept.

### Aggregate view: spec-to-implementation reconciliation

| id | finding | source | status | disposition |
|----|---------|--------|--------|-------------|
| S1 | Done when 5 met: all 21 check runs on PR #24's head are `success` (five-env builds, unit-tests, clang-format, cppcheck, x4pro static analysis, flash budget, layer check, upstream ledger). The epic file records green only for the earlier head `6cf7f65b` (Notes line 136) and no line records CI on the final head. | GitHub check runs for PR #24; epic Notes :136 | verified | accept; record the final-head CI in the epic file |
| S2 | Done when 2 ("checked on an X4 Pro") is only partly met: hand-off and blank pushes were timed on the device, but B7.6 (sleep with a dirty store) was not run, Part C (unreadable `.pkg`) was not staged, epic 4's R2-R4 and the per-Sleep-Screen-mode and per-state sleeps are "owner's word" with no log. | `story-device-run-and-owner-sign-off-plan.md` :25-27, :47, :52 | verified | accept (owner chose to merge without further hardware tests, Notes :141); open item |
| S3 | The 5.6 sleep blank takes 1,654 ms on the device against the 1,500 ms SD window, so a pending resume write or `ch.store` flush after it is skipped in a hidden pass match. Accepted by the owner with the fix deferred under `deferred-work.md ## 5.6`. | sign-off plan :35, :47; `GameMatchActivity.cpp:398-421` | verified | defer (owner decision); see F6 for the added edge cases |
| S4 | Requirement text no longer matches the build in eight places, each settled by an owner Decision: R4/R5 "blank" hand-off (now icon/title/handoff image plus "Player N's turn" and "I'm ready"); R4 double-tap protection (time guard added then removed, banner and button overlap by choice); R6/R14 sleep blank (plain white push); R9 "New for each mode" (now one New plus Options); R10 "no api-level-1.txt entry" (`ctx.settings`, `default_mode`, `settings`, reserved `title.png`/`handoff.png`); R12 share (11,152 B, then 20,000 B, then 21,000 B, gate 250 to 270 KiB); R1 `ctx` shape; the AD-12 "cannot land on the other" sentence. | epic Notes :121, :125-127, :130, :138-141; spec reconciliation report | partly verified (Notes lines read; code cites unverified) | accept the deviations; propose reconciling the Requirement text |
| S5 | Built with no Requirement: Options screen, `prefs.bin`, manifest `default_mode`/`settings`, reserved images, `GameConfirmDialog`, diagnostic push-time log lines (`64842706`). All came in through owner Decisions, not from a ticket's `covers`. | Notes :121, :140 | verified (Decisions) | accept |
| S6 | R12 flash: final +20,448 B over the base at `c1902721` (Notes :139) against the original 11,152 B share (+9,296 B over) and the final 21,000 B share (within by 552 B). Static RAM +0 B against 32 B. The raised shares were set by owner Decision. | Notes :127, :139, :140 | verified | accept |
| S7 | Ticket 5.13 (plain tap targets) is at status `built`, state `review`. Its device behaviour was checked only in the simulator; the device run that motivated it (28 hand-offs, 50 dropped move-screen touches) was not repeated on the new build. | `tickets.py status`; Notes :138; sign-off plan :52-56 | verified | open item |

### Aggregate view: architecture delta

| id | finding | source | status | disposition |
|----|---------|--------|--------|-------------|
| A1 | `check_layers.py` passes at HEAD (538 include edges, 115 game files). New file `lib/GameCore/SeatShown.h` includes only GameCore headers. | agent ran the script; CI Layer check green | verified (CI) | accept |
| A2 | Ledger row 2 says "`STR_GAMES_*` keys appended, append-only". The epic's diff to `english.yaml` removes four keys (`STR_GAMES_MODE_TITLE`, `_SOLO_DESC`, `_PASS_DESC`, `_NEARBY_DESC`) and adds eleven. `check_upstream_touches.py` still passes, so it does not enforce "append-only". | `git diff 192706f0 17d02483 -- lib/I18n/translations/english.yaml`; `docs/crosshatch/upstream-touches.md:34` | verified | fix now: reword row 2 to "STR_GAMES_* keys added, changed, or removed under that prefix", or make the check enforce append-only |
| A3 | R12's "upstream files change only in ledger row 2" holds: the only upstream file in the diff is `english.yaml`. | agent grep via `git cat-file -e upstream/develop:<path>`; ledger job green | verified (CI) | accept |

### Aggregate view: size growth

| id | finding | source | status | disposition |
|----|---------|--------|--------|-------------|
| G1 | `GameMatchActivity.cpp` grew 703 to 1,092 lines (+389) and `.h` 206 to 358; it now carries match load/resume, lifecycle handling, the playing loop and hand-off gap, canvas render, pause/over view building, snapshot persistence, and exit/abandon. Largest functions `loopPlaying` 100 lines and `GameVM::run` 124 (was 74); none over 150. `GameModeActivity.cpp` grew 108 to 487, `GameSaveStore.cpp` 393 to 762. | `wc -l` at HEAD and `c1902721`; agent brace scan (approximate) | line counts verified; function sizes unverified | defer: split `GameMatchActivity` before epic-play-nearby adds a second local-seat path |

### Aggregate view: duplication map

| id | finding | source | status | disposition |
|----|---------|--------|--------|-------------|
| D1 | The atomic replace-file sequence (tmp rename, write, remove old, rename) is written twice: `replaceFile` and `saveStore`. `replaceFile`'s comment says it does what `saveStore` does; `saveStore` does not call it. | `src/games/GameSaveStore.cpp:121-153`, `:325-366` | verified | defer to the next sweep |
| D2 | Mode-to-string/byte/bit mapping is enumerated in four to five places (`modeName`, `MODE_TEXTS`, `modeOfByte`, two switches in `GameModeActivity`, two in `GameSaveStore`). `peekStartable` and `loadStartable` share one resume.bin-or-tmp selection and classification. | agent report | unverified | defer |
| D3 | Framed-panel style block (border, radius, all states equal) copied three times; confirm-dialog input glue copied between `GameModeActivity` and `GamesLauncherActivity`; `loopPlaying`/`loopView`/`loopHandOff` repeat the same flush preamble. | agent report | unverified | defer |
| D4 | Test data: the same 8-byte package hash is hard-coded in `ModePickerTest.cpp:62`, `ResumeMatchTest.cpp:41-42` and `GameSaveStoreTest.cpp:83-84` plus inline arrays; `resumeBytes`/`resumeFile` builders, `snapshotOf`, `manifestJson`, `addGame`, `dropMatch`, `tapCell`, `holds` and others recur across suites. `LauncherSupport.h` was added for two suites but not these. | the hash lines | hash verified; the rest unverified | defer |

### Aggregate view: pattern divergence

Checked over the 3,577 lines the epic added in product code, and each rule reported clean: bare `new` / `std::make_unique` (none; nothrow forms used), SdFat/FsFile/SDCardManager (comments only), `Serial.print*` (none), raw `HalGPIO::BTN_*` (none), hard-coded user-facing strings (none found), `rowTouch`/`wasTapInRect` (none), upstream files outside the ledger (none; see A2 for the one row-2 deviation). Not checked: stack locals over 256 B (largest array found 192 B; struct locals such as `fui::ListProps` were not measured, no `sizeof` run), `RenderLock` inside a destructor (call sites not each traced to their enclosing function), and test code and docs for the conventions. Status: agent report; the grep results are unverified except where A2 and the symbol greps above were re-run.

### Lens: verification-gap

| id | finding | source | status | disposition |
|----|---------|--------|--------|-------------|
| V1 | The test input double copies the device's `TOUCH_DOWN_SELECT_DELAY_MS = 90` and `TOUCH_LONG_PRESS_MS = 500`, and nothing compares them with the device source (R13 asks that a test pins the two agree). The double's only test checks it against its own comment. | `test/game_script/harness/screen_stubs/MappedInputManager.h:125-126`; grep of `test/` | verified | fix now (action item AI-3) |
| V2 | `CopiedConstantsTest` keeps two tests, `TheSourcesAreRead` and the reader's self-test; it guards no constant, and fails in a clone whose `freeink-sdk` submodule is not initialised. `match.cmake:96-97` still passes the two path definitions. | `CopiedConstantsTest.cpp:76,82`; `match.cmake` | verified (tests listed) | fix now: either use it for V1 or delete it |
| V3 | Pass resume has no test for a hidden-pass `Paused` state or for a snapshot written mid-turn in `Playing` (the `pass-hidden` fixture passes the turn on every move). | agent report | unverified | defer; add when 5.6's fix lands |
| V4 | `FORCED_EXIT_DEADLINE_MS = 1500` has no `static_assert` pinning it, though `STOP_POLL_MS == 5` does; retuning it passes every test while `game-canvas.md` states 1,500. | `GameMatchActivity.h:78`; `GameMatchActivity.cpp:101` | verified | fix now (small) |
| V5 | Not covered: forced exit from the hidden-pass Over state by sleep; the Play-again variant of the first-move-during-push rule; a latched post-transition contact lifting during the first frame's push. | agent report | unverified | defer |
| V6 | No test pins removed 5.12 behaviour: a repo-wide grep for the removed guard symbols finds 0 hits; plain-tap and first-move cases are covered at `GameMatchTest.cpp:2783-2893`. | grep re-run | verified | accept (checked and clean) |

### Lens: adversarial and edge-case (consolidated, overlapping findings merged)

| id | finding | source | status | disposition |
|----|---------|--------|--------|-------------|
| F1 | The lifecycle table still describes the removed 5.12 rule: HandOff to Playing on "a tap on 'I'm ready' begun after the screen was pushed", Result to HandOff on "a tap on the banner begun after it was pushed". The same file's Taps section says one plain tap passes. | `docs/crosshatch/game-canvas.md:169`, `:172` | verified | fix now |
| F2 | Three stale comments pin the removed rule: `GameMatchTest.cpp:2712-2715` (says a pass needs a tap begun after the push; the tests below assert the opposite); `GameMatchActivity.cpp:670-671` (`now` "dates a release", but is used only for `store.flushIfDue`); `GameMatchTest.cpp:1801-1805` (cites superseded row N1). | the cited lines | first two verified; third unverified | fix now |
| F3 | `device-run-packet.md` B6 steps 1-4 and timing T5 still describe the removed guard (home-key 2,000 ms, power-click 1,051 ms, "reopens N1"). | agent report | unverified | fix now if the packet is reused; otherwise mark superseded |
| F4 | `touchDownLatched` is cleared in one place, at the end of `loopPlaying` (`GameMatchActivity.cpp:611`); `loopHandOff`, `loopView` and an early return from `handle()` never clear it, so the first pass back in Playing can reuse a stale `touchDownMs`/`touchDownFrame` and drop a fresh contact as "began before the hand-off". | `GameMatchActivity.cpp:579,603,611,737-740` | verified (single reset); consequence unverified | fix now or defer with a test |
| F5 | A tap after "I'm ready" but before the VM publishes the seat frame (`awaitingRound`) is dropped as "before the frame was on the panel"; the first-move-accepted rule covers only the window after publish. | agent report | unverified | open question for the owner: accepted, or a remaining "several taps" case |
| F6 | The hidden-pass forced-exit blank runs before every SD step and counts against the 1,500 ms window, so on a 1,654 ms push the resume write, store flush and an Over `resume.bin` delete retry are all skipped. Related: a `TurnChanged` pass returns before `flushResume()`, so a move made within one loop pass of the exit is not saved. The blank is also pushed in HandOff, Over and Paused-from-HandOff where nothing private shows (the no-VM path does check `Panel::Seat`; the VM path does not). | `GameMatchActivity.cpp:260-262`, `:398-421` | code verified; the loss on device is unconfirmed (B7.6 not run) | defer per owner (`## 5.6`); add the panel check and the pre-blank flush to that deferral |
| F7 | `Manifest::check` now returns Invalid for any manifest declaring `pass` or `nearby` with `seats.max < 2`. A package installed before the epic that listed `pass` with one seat was inert and becomes Invalid, which hides the whole game, solo included. | `lib/GameCore/Manifest.cpp:278-279` | verified | accept or fix: decide whether to degrade to "no pass" instead of Invalid |
| F8 | `GameModeActivity::startMatch` refuses to start when `settings.count != manifest.settingsCount`, which blocks Continue as well as New, though a resume runs no `setup`. A transient settings read fault makes a good save unreachable, with only a log line and no on-screen reason. | `GameModeActivity.cpp:323-329`, `:143-161` | verified | defer; let Continue through and add a notice |
| F9 | A Timer that falls due after the round is over reaches `input()` with seat 0, which `game-canvas.md:250` calls "never an input seat" (pinned by `GameVmTest.cpp:581-622`). A script that indexes per-seat tables on `ctx.seat` can error out of a finished match. | agent report | unverified | open question: document it or stop delivering |
| F10 | `GameVM::showSeatNow` leaves a request unserved when the turn seat is not local; harmless for pass today, listed as G1 in the deferred work and blocks nearby. | agent report | unverified | defer (already tracked) |
| F11 | A package with `title.bmp` or `handoff.bmp` that was valid before the epic is now skipped silently by the asset loader and fails at `ch.gfx.image` time. | agent report | unverified | defer; log or reject at install |
| F12 | A Nearby choice plays solo but is remembered in `prefs.bin` and named in the confirm dialog. | agent report | unverified | defer until nearby runs |
| F13 | The launcher row shows no sign of a save; New over a save relies on a `peek` cached at screen entry, so a save that appears after it is replaced without the confirm question. | agent report | unverified | defer |
| F14 | A `Continue` whose game has no startable mode on this host fails with only a log line even when the save itself is startable. | agent report | unverified | defer |

## Behavior verification

- **Host suites, run in this session** on the merged tree (HEAD `17d02483`): `cmake --build build/test` (no work to do) then `ctest --test-dir build/test -j 4`: **1,620 of 1,620 passed**, 6.28 s. This covers pass lifecycle, hand-off, forced exit, pass resume, title screen and launcher.
- **CI on PR #24's head:** 21 of 21 check runs green (list under S1). Read through the GitHub check-runs API, not the logs.
- **Simulator:** not run in this retro. Entry 13's builder checked it in the simulator (sign-off plan :52-56).
- **X4 Pro:** not run in this retro. The owner's device run is recorded in `story-device-run-and-owner-sign-off-plan.md` as owner's word plus two or three pasted serial logs I did not see: hand-off pushes 1,678-1,722 ms, sleep blank 1,654 ms, `resume.bin` write 91-92 ms, `peek` 7-9 ms. Not run on the device: B7.6, Part C, epic 4's R2-R4, per-mode and per-state sleeps, and the device behaviour of ticket 5.13.

## Previous-retro follow-through

Previous retro: `epic-install-and-launcher/epic-install-and-launcher-retrospective.md` (exists; its Action items section lists AI-1 to AI-15). Result of checking each item at HEAD; a source was read for each "landed" and each "no evidence". Spot-checked by me: AI-6, AI-13 and AI-10 items 1-2 (verified); the rest is agent-reported.

| item | owner | landed? | source |
|------|-------|---------|--------|
| AI-1 run the device packet R1-R4, record under Behavior verification | owner | partly: R1 ran and passed; R2-R4 "owner's word"; not recorded in the old retro; gating clause deferred by decision | sign-off plan :23-25; epic Notes :87 |
| AI-2 build the e4-r2 story (launcher note, install after remove) | owner, dev loop | landed | commits `b5ce80c0`, `b83dfbe2`; old retro Addendum |
| AI-3 remove the model-name rule from the build brief | owner | landed | `grep` of `orchestrated-epics.md` finds nothing |
| AI-4 fidelity rule, fold `save_store_stubs` | owner, dev loop | partly: rule and fold landed; the `touch-and-ui.md` half deferred by decision (upstream file) | `orchestrated-epics.md:233-238`; Notes :90 |
| AI-5 calibrate timing fixtures or list as uncalibrated | owner | landed and used | packet :50, :55; sign-off plan R1 row |
| AI-6 fix rev-12 concurrency group; two `AGENTS.md` lines | owner | **no evidence found, looked and verified absent**: `AGENTS.md` has no `labeled`/`GameIconsRaw`; the workflow's concurrency group is unchanged. No epic doc records a deferral. | `AGENTS.md`; `.github/workflows/crosshatch-game-packages.yml:19-21` |
| AI-7 measure on the cited commit; flake fix proven at a repeat count | owner | landed | `orchestrated-epics.md:120-122, :239-241` |
| AI-8 after a container restart check worktrees and subagents | owner | landed | `orchestrated-epics.md:99-103` |
| AI-9 record epic 4's delta in the spine; name the next static-RAM lever | owner | landed, with different figures (+4,848 B / +8 B) | spine Flash budget row; Notes :83 |
| AI-10 apply the 11 spec reconciliations | owner | partly: items 3, 7, 11 landed; items 1, 2 verified absent in the spine (no `.removing`, `MAX_GAMES`, `.chgame.installed`); items 4, 5, 6, 8, 10 no evidence found; item 9 unchecked | spine AD-15/AD-16; `formats.md`; `game-canvas.md:152` |
| AI-11 device step: Continue with an unreadable `.pkg` | owner | step added; device run not staged; host test `ResumeMatchTest.AContinueWhosePkgWillNotReadStopsInTheErrorViewAndKeepsTheSave` instead | packet :61, :156-162; sign-off plan :27 |
| AI-12 installer refactor, on the next installer change | dev loop | deferred by decision | Notes :88, :122, :141 |
| AI-13 deferred-work entries A3, rev-7, rev-9 | owner | **no evidence found, looked and verified absent** in `deferred-work.md` | grep for `rev-7`, `rev-9`, `62 bytes`, `game_codec` |
| AI-14 remove `.removing` on a failed delete; rename the test | dev loop | landed | commit `8d9cd309` |
| AI-15 tests: fake logs `exists`; OOM suite | dev loop | landed | commit `8d9cd309` |

Also open from that retro: R10 (watchdog and store flush under the light panel) deferred to the next ledger change (Notes :89). The old retro's own Behavior verification still reads "pending the owner".

## Action items

All are proposed, not applied. Remediation and spec reconciliations await the owner or the dev loop.

| id | item | owner | kind |
|----|------|-------|------|
| AI-1 | Reword ledger row 2 of `docs/crosshatch/upstream-touches.md` (keys added, changed or removed under `STR_GAMES_*`), or make `check_upstream_touches.py` enforce append-only. Findings A2. | owner | spec reconciliation |
| AI-2 | Fix the stale lifecycle table in `docs/crosshatch/game-canvas.md:169,172` and the three stale comments (F1, F2); mark `device-run-packet.md` B6 and T5 superseded (F3). | dev loop | remediation |
| AI-3 | Pin the input double's 90 ms and 500 ms against the device source using the retained reader helper in `CopiedConstantsTest`, or delete that file and its two compile definitions. Add `static_assert(FORCED_EXIT_DEADLINE_MS == 1500)`. V1, V2, V4. | dev loop | remediation |
| AI-4 | Clear `touchDownLatched` on leaving Playing (F4) and add a test for a fresh contact on the first pass back. | dev loop | remediation |
| AI-5 | Fold the 5.6 deferral's follow-up into one story: skip the blank unless `panel == Panel::Seat`, flush the resume write and store before the blank or start the deadline after it, flush on the `TurnChanged` pass. Run B7.6 on the X4 Pro first to confirm the loss. F6, S3. | owner decides, dev loop builds | remediation |
| AI-6 | Device-check ticket 5.13's plain tap targets on the X4 Pro (a fast double tap on the banner, the first move after "I'm ready", a held contact across the transition) before 5.13 moves from review to done. S7, F5. | owner | verification |
| AI-7 | Reconcile the Requirement text with the owner Decisions in the epic file (the eight places in S4), and record CI on the final head under Done when 5 (S1). | owner | spec reconciliation |
| AI-8 | Carry over from the previous retro: AI-6 (concurrency group, two `AGENTS.md` lines), AI-13 (three deferred-work entries) and AI-10 items 1, 2, 4, 5, 6, 8, 10, either doing them or recording an explicit deferral. These were dropped without a recorded decision. | owner | process |
| AI-9 | Process lesson: when an owner Decision supersedes a Requirement, edit the Requirement in the same commit; 20 `Assumption for entry 11` lines and eight stale Requirement lines accumulated in this epic. | owner | process |
| AI-10 | Defer to the next sweep: split `GameMatchActivity` (G1) before epic-play-nearby, share `replaceFile` with `saveStore` (D1), and one test-support header for the duplicated fixtures (D4). | dev loop | remediation |

## Acceptance verdict

**accepted-with-open-items**, criteria **declared** (the epic file has five Done when items). This is the machine verdict; a human decision overrides it.

Evidence, against the Done when:
1. Hidden and open two-player fixtures play a round from Home in at most 3 taps, turns enforced: met by host tests (`ModePickerTest.cpp:567,1559`, `GameMatchTest`, `SoloRoundsTest`), and the owner's device run (logs show `pass-open` and `pass-hidden` matches).
2. Hand-off on every turn change, new or resumed round, and sleep, with a script unable to suppress it, checked on an X4 Pro: **met with deviation and partly on the device.** Host tests cover each transition; the device run timed the pushes, but B7.6 and the per-state sleeps were not run, and the blank is now plain white for sleep and a design screen for hand-offs (owner Decisions).
3. Play again, game over with seat 0, one `over` per local seat: met (`ResumeMatchTest.cpp:1522`, `Session.cpp:73-74`).
4. Sleep and Continue restore a pass match, through the hand-off when hidden: met in host tests; the device part is owner's word.
5. Merged with the five-env build, host suites, format check, `pio check` and the fork-only gates all green: met (21 of 21 check runs on the head of PR #24).

No unfinished tickets (`pending_tickets` is empty), so completeness does not force a rejection. Ticket 5.13 is still at `built` (state `review`) and is an open item, not a blocker. No blocking finding stands unresolved: the sleep-window loss (F6, S3) is a recorded owner deferral, and nothing found contradicts a Done when item.

Open items that qualify the verdict: S2 and S7 (device checks not run), F6 (sleep-window skips), F4 (stale touch latch), A2 (ledger row 2 deviation), F1/F2 (stale docs and comments), and the dropped carry-overs in AI-8.

## Open questions

1. **Ticket 5.13** is at `built`/`review`. Does the owner want it called done after the device check in AI-6, or accepted on the simulator check alone? This decides whether the epic closes as accepted or stays accepted-with-open-items.
2. **F5:** is a tap in the window between "I'm ready" and the VM publishing the seat frame acceptable to drop, now that the owner chose "a plain tap passes"?
3. **F7:** should a pre-epic manifest with `pass` and `seats.max` 1 hide the whole game (Invalid), or degrade to solo-only?
4. **F9:** should a Timer after the round is over reach `input()` with seat 0, or be dropped?
5. **A2:** is ledger row 2 meant to be append-only? The check passes either way.
6. Could not resolve: whether the device-run firmware matches `64842706` or later (the sign-off plan does not record the exact commit and hashes), and the contents of the owner's serial logs, which I did not see.
7. Session logs for the epic's builds were not available, so process lessons beyond what the plans and Notes record are not covered.

## Evidence limits

- Not checked: stack locals over 256 B (structs not measured); the duplication and divergence greps over test code and docs; `scripts/pack_device_run.py` against `pack_game.py` beyond the function names; the spine's layer-table text against `check_layers.py`; `lib/GameIcons` (no diff lines).
- Findings marked **unverified** come only from a subagent's report.
- 5.13's own diff could not be isolated: its baseline commit is absent and its range is a squash that also carries upstream merges.

## Addendum: follow-up fixes applied (2026-10-03)

Ticket 5.13 was marked done on the owner's confirmation (commit `0264a1f4`); its device check (action item AI-6) was not run. Seven follow-up builds then applied the retro's findings, with the owner's answers recorded in the epic Notes: e5-r1 (F4 stale touch latch, V4 window `static_assert`, F2 comments, one V5 gap), e5-r2 (F1, F3, F5 note, A2, V1/V2), e5-r3 (F8, F11), e5-r4 (D1), e5-r5 (F6 cheap fixes, F10), e5-r6 (F9), e5-r7 (F7). Each ran four review lenses as subagents (two for e5-r6 were run by the orchestrator); each plan is `_bmad-output/implementation-artifacts/plan-e5-r*.md`. Merged tree `55a8d9bf`: host suites 1,646/1,646, firmware and `pio check` green, flash +254,480 B (40 B over the epic's 21,000 B share, 22,000 B under the gate).

Resolved: A2, F1, F2, F3, F4, F5 (decided: keep dropping), F7, F8, F9, F10 (the `TurnChanged` flush), F11 (log only), D1, V1, V2, V4, and V5's first gap. The verdict stays accepted-with-open-items. Still open: S2 and S7 (device checks, B7.6), F6's window (reordering, owner choice under `## 5.6`), F12-F14, F3's other packet history, G1 (`GameMatchActivity` split, before epic-play-nearby), D2-D4, V3 and V5's other gaps, AI-7 (Requirement text, including R10's "invalid"), and AI-8 (the previous retro's dropped items).
