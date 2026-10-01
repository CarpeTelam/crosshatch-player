---
title: 'Pass lifecycle: Result, HandOff, and the seat each state draws'
type: 'feature'
ticket: '2'
created: '2026-10-01'
status: done
route: 'full'
route_source: 'auto'
baseline_revision: '7c1bf3356876e5271b7baee7b1a7f9b9104a7450'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 1
context:
  - '{project-root}/.skills/control-flow-clarity/SKILL.md'
  - '{project-root}/.skills/scope-discipline/SKILL.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** `GameCore::MatchLifecycle` knows only AD-21's solo states, so a hidden pass match has no `Result` or `HandOff` state for entry 4 to drive, and entry 1's `seatShown` has no answer for them; `Manifest::check` and `pack_game.py` still accept a manifest that declares `pass` with `seats.max` below 2, which then lists as startable and whose Pass tap only logs (`deferred-work.md` `## 5.1`).

**Approach:** One `hiddenPass` flag, fixed when the lifecycle is built, switches on AD-21's hand-off transitions over two new states (`Result`, `HandOff`) and two new events (`TurnChanged`, `Tap`), appended to the enums; with the flag off (the default) every transition is today's. Paused remembers the state it was entered from and Resume or Back returns there. `seatShown` gains an optional mover and answers the mover in `Result` and `NO_SEAT` in `HandOff`. `Manifest::check` applies `NearbyNeedsTwoSeats` to pass too, with its `describe` text generalised, and `pack_game.py` refuses the same manifest with one generalised message.

## Boundaries & Constraints

**Always:** The solo/open machine (`next(from, event)`, flag off) is unchanged for every pre-existing state and event; new events are never transitions there and `Result`/`HandOff` are unreachable from `Starting`. Every guard in `next` stays (Design Notes). `NearbyNeedsTwoSeats` keeps its name and position. New enumerators go at the end of their enums. No new include edge. AGENTS.md memory rules.

**Never:** Edit `Session.*`, `Roster.*`, `src/**`, `lib/GameScript/**`, `GamePackageInstaller.*`, the epic file, or `api-level-1.txt`. No `host.maxSeats >= 2` rule for pass in `check` (not asked; `passSeats` already guards it). No menu in `Result` or `HandOff`.

**Decisions (orchestrator, 2026-10-01, out-of-touches edits approved):** (1) rewrite `test/game_script/harness/ModePickerTest.cpp`'s `PickerTest.APassOnlyGameWithOneSeatStartsNothingFromTheLauncher` to check the new behaviour (the tap starts nothing, the log names the unavailable game with the generalised reason, "Cannot start" never appears); (2) edit only two comment lines: `lib/GameCore/Manifest.h`'s `NearbyNeedsTwoSeats` comment and `lib/GameCore/HostCaps.h`'s `pass` comment. Host `-Wswitch` warnings in `GameViewIcons.h` and `GameMatchActivity.cpp` are accepted until entry 4 fills those switches.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Hidden start | hiddenPass, Starting + Started | HandOff (new and resumed alike) | — |
| Turn passes | hiddenPass, Playing + TurnChanged | Result | open/solo: not a transition |
| Taps | Result + Tap → HandOff; HandOff + Tap → Playing | as stated | Tap in Playing/Over/Paused: not a transition |
| Pause and back | Result or HandOff + Back/Home → Paused; + Resume/Back | back to Result / HandOff; from Playing back to Playing | — |
| Play again | Over + PlayAgain | hiddenPass: HandOff; else Playing | — |
| Invalid | Result/HandOff + RoundOver, Started, PlayAgain, Leave, Resume | unchanged | ScriptError → Error; ForcedExit → Leaving |
| Menus | menuFor(Result), menuFor(HandOff) | empty | — |
| Seat shown | pass n=2/3: Result with mover m → m; HandOff → NO_SEAT; one local seat: its seat except HandOff → NO_SEAT | — | Result with no or non-local mover → NO_SEAT |
| Manifest | modes include pass, seats.max 1 | Invalid, NearbyNeedsTwoSeats, "pass and nearby need seats.max 2 or more" | pack_game.py: `error: manifest.json: a pass or nearby game needs seats.max of 2 or more` |

</frozen-after-approval>

## Code Map

- `lib/GameCore/MatchLifecycle.h` -- `enum class MatchState { Starting, Playing, Paused, Over, Error, Leaving, Result, HandOff }`; `MatchEvent` gains `TurnChanged` (an accepted move changed the turn seat) and `Tap` (a tap on the Result banner or the hand-off screen) at the end. `MatchLifecycle() = default` (solo/open), `explicit MatchLifecycle(bool hiddenPass)`; `bool hiddenPass() const`; `MatchState resumesTo() const` (the state Paused returns to; Playing outside Paused, and always Playing with the flag off); `static next(from, event, bool hiddenPass = false, MatchState resumesTo = Playing)`. Class comment: both machines in the ASCII table, and that a move which ends the round raises RoundOver from Playing, never TurnChanged (RoundOver is not a transition from Result or HandOff, AD-21). Members `bool hidden = false; MatchState pausedFrom = Playing;`.
- `lib/GameCore/MatchLifecycle.cpp` -- `next`: keep `Leaving` terminal and `ForcedExit` first; Starting+Started → `hiddenPass ? HandOff : Playing`; Playing+TurnChanged → Result only when hiddenPass; Result: Back/Home → Paused, Tap → HandOff, ScriptError → Error; HandOff: Back/Home → Paused, Tap → Playing, ScriptError → Error -- both rows only when hiddenPass: with the flag off every event but ForcedExit (handled before the switch) leaves Result and HandOff as they are; Paused+Resume/Back → `resumesTo` only when hiddenPass and it is Result or HandOff, else Playing (flag off: always Playing, today's row); Over+PlayAgain → `hiddenPass ? HandOff : Playing`. `apply` records `pausedFrom = current` when entering Paused. `allows`/`apply` call `next(current, event, hidden, pausedFrom)`. `menuFor` lists Result/HandOff with the empty states; `name` covers the new values.
- `lib/GameCore/SeatShown.h` -- `inline constexpr uint8_t NO_SEAT = 0xFF;` `seatShown(state, roster, status, uint8_t mover = NO_SEAT)`: HandOff → NO_SEAT (any roster); one local seat → `firstLocalSeat()`; Over or `status.over` → 0; Result → `roster.isLocal(mover) ? mover : NO_SEAT`; else `status.turn`. `NO_SEAT` is never drawn. `SoloRounds::shownSeat()` (3 args) compiles unchanged. Add the overload `seatShown(const MatchLifecycle& lifecycle, const Roster&, const Status&, uint8_t mover = NO_SEAT)`: the state-taking form with `lifecycle.state()`, except that Paused uses `lifecycle.resumesTo()` (a hidden match paused from Result keeps the mover, from HandOff shows NO_SEAT; open/solo Paused shows the turn seat as today). Comments: a match asks with its lifecycle (this overload); the state form's Paused row is the open/solo answer only.
- `lib/GameCore/HostCaps.h` (approved comment line) -- `pass`: "Pass and Play is available (off by default; gameHostCaps() turns it on from HostCapsValues::PASS)". `lib/GameCore/Manifest.h` (approved comment line) -- `NearbyNeedsTwoSeats`: "Invalid: pass or nearby with seats.max below 2".
- `lib/GameCore/Manifest.cpp` -- `check`: `if ((modes & (MODE_PASS | MODE_NEARBY)) != 0 && seatsMax < 2)` → `NearbyNeedsTwoSeats`; `describe` → "pass and nearby need seats.max 2 or more"; the stale "no match can run it until epic-pass-and-play" comment in `check` corrected.
- `scripts/pack_game.py` -- line 251: one message when `pass` or `nearby` is in modes: `a pass or nearby game needs seats.max of 2 or more`; docstring line 10 names "solo, pass, and nearby seat rules".
- Consumers that must compile unchanged: `GameViewIcons.h` `forView`/`forOption`, `GameMatchActivity.cpp`'s switches (host `-Wswitch` warnings only, no `-Werror=switch` target includes them; firmware has no `-Wall`); entry 4 completes them (its description). `GamesLauncherActivity.cpp:105` switch: the enumerator is unchanged.

## Tasks & Acceptance

**Execution:**
- [x] `lib/GameCore/MatchLifecycle.{h,cpp}` -- as in Code Map.
- [x] `lib/GameCore/SeatShown.h` -- as in Code Map.
- [x] `lib/GameCore/Manifest.cpp` -- rule, text, comment.
- [x] `test/game_core/MatchLifecycleTest.cpp` -- keep the solo table and every existing case as is; its loops gain the new states/events (all eight states, all eleven events, statically; the built-lifecycle part only for states the solo machine reaches), whose solo rows are "no transition" except Result/HandOff + ForcedExit → Leaving (two rows added to the solo table); the static flag-off `next(Paused, Resume|Back, false, Result|HandOff)` is Playing; add a hidden table test over every state and event (each listed transition, everything else unchanged), Paused returning to Result/HandOff/Playing, every event from a Paused entered from each of Playing, Result, and HandOff (only Resume/Back differ), Result/HandOff + RoundOver not a transition, Play again to HandOff, a solo/open reachability test (Result/HandOff never reached from Starting by any event sequence), menus in Result and HandOff empty and their Back leading to the pause menu, names.
- [x] `test/game_core/SeatShownTest.cpp` -- rows for every state (8) for hidden pass n=2 and n=3 (Result with mover, without one, with a non-local one), open pass, and one local seat; existing rows unchanged; the lifecycle overload: hidden pass paused from Playing (turn), Result (mover), HandOff (NO_SEAT), and each unpaused state equal to the state form; an open-pass/solo lifecycle paused shows the turn seat / its seat.
- [x] `test/game_core/ManifestCheckTest.cpp` -- `PassNeedsSeatsMaxTwo` (pass-only and solo+pass with max 1 → Invalid `NearbyNeedsTwoSeats`; with max 2 ok; Invalid wins over a host without pass); the describe text; the stale NO_PASS_HOST comment.
- [x] `scripts/pack_game_test.py` -- pass refusals (pass, solo+pass) in the seat-rule test and, with the exact `error: manifest.json: ...` line, once each in `test_manifest_rules_refuse_the_package` (no case packed twice); existing nearby assertions keep passing and are not duplicated; `test_valid_manifests`' solo+pass manifest gets seats 1..2.
- [x] Per the Decisions: `ModePickerTest.cpp`'s one test (also assert the row draws `tr(STR_GAMES_UNAVAILABLE_INVALID)` when the suite's screen recorder can read a row's second line as `GamesLauncherTest`'s `lineAfter` does; otherwise its comment claims only what the logs show); `Manifest.h`, `HostCaps.h` comment lines as in Code Map.
- [x] `deferred-work.md` `## 5.2` -- resolve `## 5.1`'s check item (the launcher lists the game with the invalid-manifest note: `CheckStatus::Invalid`, not Unavailable); host `-Wswitch` warnings and `GameViewIconsTest`'s `ALL_STATES`/`ALL_EVENTS` lists until entry 4; `PickerTest.APassThatCannotFitStartsNothing` now builds a picker from a 1..1 solo+pass manifest `check` refuses, so its `passSeats == 0` guard is reachable only on a host with `maxSeats` below 2: entry 7 (which rewrites the picker suite) re-bases it on a one-seat host double. The `## 5.1` HostCaps/Manifest comment item is resolved in this plan (not by editing 5.1's entry).

**Acceptance Criteria:**
- Given a default-built lifecycle, when any pre-existing state receives any pre-existing event, then the result equals today's table.
- Given a hidden pass lifecycle paused from Result, when Resume is chosen, then the state is Result.
- Given a pass manifest with `seats.max` 1, when the launcher lists it or `pack_game.py` packs it, then it is invalid with the generalised text.

## Implementation Notes

Pass 1 (reverted by the review loopback, Plan Change Log 1; kept for the record):

- `MatchLifecycleTest.EveryStateAndEventFollowsTheTable` loops the six solo states (`SOLO_STATES`) over every event, the new ones included (never a solo transition); Result and HandOff are not states the solo machine has (`reach()` cannot enter them, and `ForcedExit` leaves them in either machine), so `TheSoloMachineNeverReachesResultOrHandOff` covers them for the solo flag. `next()` does not gate Result's and HandOff's own rows on the flag (the Code Map lists them unconditionally); they are unreachable with it off.
- `pack_game_test.py`'s `test_valid_manifests` declared `solo`+`pass` with the default `seats.max` 1, now refused; it gains `seats: {min 1, max 2}`. `test_solo_and_nearby_seat_rules` is renamed `test_solo_pass_and_nearby_seat_rules`.
- Host build: `-Wswitch` warnings only in `GameViewIcons.h` and `GameMatchActivity.cpp`, each switch falling through to its existing no-view or return path (recorded in `deferred-work.md` `## 5.2`).

Pass 2 (after Plan Change Log 1, and the pass-2 review patches: rows 22, 23, 27, 30; a fresh context-free implementation subagent re-derived it from this plan, starting from the pass-1 patch the KEEP names):

- `next` gates Result's and HandOff's rows and Paused's return on `hiddenPass`; `seatShown(const MatchLifecycle&, ...)` added; the solo table gains Result/HandOff + ForcedExit and `EveryStateAndEventFollowsTheTable` checks `next` statically over all eight states (the built-lifecycle part over the six the solo machine reaches). New tests: `TheSoloMachineResumesPlayWhateverPausedWasEnteredFrom`, `HiddenPausedFromEachStateTakesEveryEventAlike`, `RoundOverIsNoTransitionInResultOrHandOff`, `AHiddenPassMatchPausedShowsWhatItWasPausedFrom`, `AnUnpausedLifecycleShowsWhatItsStateShows`, `AnOpenPassOrSoloMatchPausedShowsItsSeat`.
- Deviation: `HostCaps.h`'s `pass` comment reads "off by default; gameHostCaps() sets it: HostCapsValues::PASS" (the plan's wording ran past 120 columns and clang-format split it).
- The picker test asserts the row's second line is `tr(STR_GAMES_UNAVAILABLE_INVALID)` (the suite's recorder reads it).

## Plan Change Log

1. (2026-10-01, review pass 1, bad_plan) **Trigger:** triage rows 1 and 2: `seatShown(Paused, ...)` returns the turn seat, so a hidden match paused from Result or HandOff shows the next seat's view unless every caller remembers to pass `resumesTo()` (a comment-only contract); and the static `next` honours `resumesTo` and Result's/HandOff's rows with the flag off. **Amended:** Code Map (`next` gates both on `hiddenPass`; header states RoundOver's precedence; `seatShown` gains the lifecycle overload; the two approved comment lines' text), Tasks (solo loops over all states, Paused-from-each exhaustive test, RoundOver in Result/HandOff, the overload's rows, pack_game_test without duplicates, the picker test's row assertion, the deferred wording and two new deferrals). **Known-bad state avoided:** a caller passing `Paused` for a hidden match drawing another seat's private view; a flag-off lifecycle reachable into Result/HandOff through the public static API. **KEEP:** everything else in pass 1 as it was (its diff, uncommitted, is saved at the orchestrator scratchpad `5.2/reverted-pass1.patch` for this session only): the appended enumerators and their names, `hiddenPass()`/`resumesTo()`/`pausedFrom`, the `roundStart` helper in `next`, every guard, `NO_SEAT = 0xFF` and the fail-closed Result rule, the HandOff-first order in `seatShown`, `Manifest::check`'s mask test and `describe` text, `pack_game.py`'s one message, every pass-1 test (`HiddenPassEveryStateAndEventFollowsItsTable`, `PausedReturnsToTheStateItWasEnteredFrom`, `HiddenPassTurnsCycleThroughResultAndHandOff`, `HiddenPassPlayAgainHandsTheDeviceOver`, `TheSoloMachineNeverReachesResultOrHandOff`, the menus and names tests, `EveryStateShowsItsSeatWithTheMover`, `NoSeatIsNoRosterSeat`, `PassNeedsSeatsMaxTwo`, `TheSeatRuleSaysItCoversPassAndNearby`, the rewritten picker test).

## Review Triage Log

Pass 1 (2026-10-01). The four lenses ran as context-free subagents, launched together in one message in the foreground, and all four returned: blind-hunter, edge-case-hunter, verification-gap, intent-alignment. Verdicts: high 0, medium 1, low 9, false 0, maybe-false 0 (plus the intent auditor's descriptive report and verification-gap's "no gaps").

| # | Lens | Finding | Verdict | Route | Evidence / action |
|---|------|---------|---------|-------|-------------------|
| 1 | blind, edge, verification-gap (other), intent | `seatShown(Paused)` returns `status.turn`; a hidden match paused from Result/HandOff shows the next seat's view if the caller passes `Paused` | medium | bad_plan | Real at `SeatShown.h`: the function cannot know `pausedFrom`; the fix (a lifecycle overload) adds public surface. Plan Change Log 1. |
| 2 | blind, edge, intent | Static `next(Paused, Resume, false, Result)` returns Result; Result/HandOff rows not gated on the flag; the solo table loop was narrowed to six states, unlike the plan's claim | low | bad_plan (with 1) | Real for the public static API only; a built flag-off lifecycle never records them. Gated on `hiddenPass` and the loop restored in the amended plan. |
| 3 | blind, edge | A winning move raising TurnChanged before RoundOver would drop the round end | low | bad_plan (with 1) | AD-21 has no Result→Over; the header now states RoundOver's precedence and a test pins RoundOver as no transition in Result/HandOff. |
| 4 | edge | `check` offers pass on a host with `maxSeats` < 2 | low | reject | The frozen Never excludes it; no host has fewer than 2 seats (`HostCapsValues::MAX_SEATS`), and `passSeats` guards the start. |
| 5 | blind | `APassThatCannotFitStartsNothing` now uses a manifest `check` refuses | low | defer | Outside the approved edit; entry 7 rewrites the picker suite. `## 5.2`. |
| 6 | blind | The rewritten launcher test's comment says "its row says why" but asserts logs only; no `lastOpened` check | low | bad_plan (with 1) | Row assertion or a truthful comment added to the task. `lastOpened`: the tap returns before setting it (`GamesLauncherActivity.cpp:573-579`), not this entry's intent. |
| 7 | blind | The `## 5.2` deferred entry calls the game "unavailable"; it is Invalid | low | bad_plan (with 1) | Wording corrected in the task. The launcher's "Unavailable" log prefix for Invalid games is pre-existing `src` code: rejected. |
| 8 | blind | `pack_game_test.py` packs the same manifests twice and refuses nearby 1..1 twice | low | bad_plan (with 1) | Duplicates removed in the task. |
| 9 | blind | The hidden table enters Paused only from Playing | low | bad_plan (with 1) | Exhaustive Paused-from-each test added to the task. |
| 10 | blind | The new `HostCaps.h` comment contradicts the field's `false` default | low | bad_plan (with 1) | Comment text fixed in the Code Map. |
| 11 | verification-gap (other) | `GameViewIconsTest`'s own state/event lists omit the new values | low | defer | Entry 4's description completes `GameViewIconsTest`'s `ALL_STATES`; recorded in the `## 5.2` `-Wswitch` item. |
| 12 | intent | Descriptive: the launcher keeps the row (Invalid with its note) rather than removing it | low | reject | The frozen matrix says Invalid; the launcher lists every registry game with its reason (`loadGames`). |

Pass 2 (2026-10-01, after Plan Change Log 1). The four lenses again ran as context-free subagents, launched together in one message in the foreground, and all four returned: blind-hunter, edge-case-hunter, verification-gap ("No verification gaps found"), intent-alignment (descriptive). Verdicts: high 0, medium 0, low 17, false 2, maybe-false 0. No bad_plan or intent_gap: patches went to the step-3 implementation subagent.

| # | Lens | Finding | Verdict | Route | Evidence / action |
|---|------|---------|---------|-------|-------------------|
| 13 | edge | The render task holds only the atomic `shown` state, so it would call the state form with Paused and leak | false | reject | No render path computes a seat: the render task draws published frames, and `seatShown`'s only caller is the VM's round driver (`SoloRounds::shownSeat`). Entry 4 asks from the loop task that owns the lifecycle. |
| 14 | edge | RoundOver is lost if raised in Result or HandOff | low | reject | carried: row 3; the header states the precedence and `RoundOverIsNoTransitionInResultOrHandOff` pins it (AD-21 has no Result→Over). |
| 15 | edge | `check` offers pass on a host with `maxSeats` < 2 | low | reject | carried: row 4. |
| 16 | edge | An installed solo+pass 1..1 game with a solo save becomes unstartable | low | reject | The owner's Decision (epic Notes, inception) makes the manifest invalid; no fixture or first-party game declares it. |
| 17 | edge (claim) | The plan says Result answers the mover, but one local seat and an over status answer first | false | reject | The Code Map lists that order; the amended rows pin it. |
| 18 | edge (claim) | "exact line" is asserted by substring | low | reject | The asserted text is the whole `error: manifest.json: ...` line; `assertRefused` is the suite's own matcher. |
| 19 | blind, intent | No installer test packs a pass 1..1 manifest | low | reject | The installer branches on `check`'s verdict, which `ManifestCheckTest.PassNeedsSeatsMaxTwo` pins, and its Invalid branch has a case (`GamePackageInstallerTest.cpp:185`); a row would be a further out-of-touches edit. |
| 20 | blind | The spine's AD-21 still calls `MatchLifecycle` "the solo machine" | low | defer | The spine is outside this entry's touches. `## 5.2`. |
| 21 | blind | Static `next(Paused, e, true)` with the default `resumesTo` returns Playing | low | reject | The static form takes the paused-from state as an argument by design; matches use the built lifecycle, which passes it. |
| 22 | blind | No test pauses twice in one match | low | patch | Added a two-pause test (lifecycle and the seat overload). |
| 23 | blind, intent | No row pins Result/HandOff against an over status | low | patch | Rows added. |
| 24 | blind | A hidden match in Starting shows the turn seat | low | reject | Starting draws nothing (the previous screen stays until the first frame), and Started leads to HandOff before any draw (R5); a branch would guard a state not shown to draw. |
| 25 | blind | The picker test pins the launcher's "Unavailable" log prefix for an Invalid game, untracked | low | defer | Pre-existing `src` log line (`GamesLauncherActivity.cpp`, `loadGames`). `## 5.2`. |
| 26 | blind | `installCounter`'s default `seatsMax = 1` now gives an Invalid manifest with pass | low | defer | `ModePickerTest.cpp` outside the approved edit; added to the entry-7 item. |
| 27 | blind | "entry 5.1" in a test comment | low | patch | Now "entry 1". |
| 28 | blind | Raw mask vs `hasMode`, the historical enumerator name, two wordings of one rule | low | reject | The name is the ticket's; each language keeps its own message style (as solo's do); the mask is one expression. |
| 29 | blind | The picker test re-implements `lineAfter` | low | reject | Moving a helper across suites is outside the approved edit. |
| 30 | blind | Deferred evidence cites line numbers entry 4 will move | low | patch | Cites function names. |
| 31 | intent | Descriptive: the lifecycle and seat API are tested in GameCore only; no match drives them yet | low | reject | Entry 4 drives them (its description); this entry's verify names the GameCore suites. |

## Design Notes

**Flag over mode value:** a bool is the whole difference between the machines (open pass equals solo), needs no `Roster.h` include in `MatchLifecycle.h` (no new layer edge), and leaves `GameViewIcons.h`'s mapping compiling as it is. **Guards kept in `next`** (`git log -L`: one commit, acf780ea): `Leaving` is terminal; `ForcedExit` leaves from every other state before the switch; `Error` takes only Back, so a second ScriptError keeps the first error view; unknown events return `from`. **NO_SEAT fails closed:** a `Result` asked without a mover, or with a seat that is not local, draws nothing rather than another seat's private view.

## Verification

**Commands:**
- Host tests (AGENTS.md, under the host-test lock) -- expected: all pass, incl. `MatchLifecycleTest`, `SeatShownTest`, `ManifestCheckTest`, `PickerTest`.
- `for t in scripts/*_test.py; do python3 $t; done`; `python3 scripts/check_layers.py`; `python3 scripts/check_upstream_touches.py`; `./bin/clang-format-fix` twice -- expected: pass, nothing new.
- Under the build lock with the shared cache: `pio run -e x4pro`, `pio run -e default`, `pio check -e x4pro --fail-on-defect low --fail-on-defect medium --fail-on-defect high` -- expected: success, no defects. No flash measurement (orchestrator note).

**Results (2026-10-01, after the pass-2 review patches, on the tree this entry commits):**
- Host suites: CMake/Ninja configure, build, and `ctest -j4` under the host-test lock: 1,438 of 1,438 pass (incl. `MatchLifecycleTest`, `SeatShownTest`, `ManifestCheckTest`, `PickerTest`).
- Host `-Wswitch` warnings, accepted by the orchestrator until entry 4 completes those switches (`deferred-work.md` `## 5.2`): `GameViewIcons.h` `forView` (Result, HandOff) and `forOption` (TurnChanged, Tap), in each of its three including TUs; `GameMatchActivity.cpp` `optionLabel` (TurnChanged, Tap) and four switches over the states (Result, HandOff). No other warning; no `-Werror=switch` target includes them; the firmware builds without `-Wall`.
- Every `scripts/*_test.py` (12) passes; `check_layers.py`: 480 edges pass, no new edge; `check_upstream_touches.py`: PASS; `./bin/clang-format-fix` twice: nothing changed.
- Under `/tmp/crosshatch-build.lock` with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`: `pio run -e x4pro` SUCCESS (no compiler warnings in its log), `pio run -e default` SUCCESS; `pio check -e x4pro` and `pio check` (default), `--fail-on-defect low/medium/high`: PASSED, no defects.
- Flash and static RAM: unmeasured (entry 2 records no measurement, orchestrator note); no fork-only CI gate changed, so no fresh-tree run.
- Out-of-touches edits, each approved by the orchestrator (Decisions above): `test/game_script/harness/ModePickerTest.cpp` (one test), `lib/GameCore/Manifest.h` and `lib/GameCore/HostCaps.h` (one comment line each). The `## 5.1` comment item for `Manifest.cpp`/`HostCaps.h` is resolved here (`GameModeActivity.h`'s stays entry 7's).
