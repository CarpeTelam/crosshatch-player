---
title: 'Nearby on two X4 Pros: wiring and the smoke run'
type: 'feature'
ticket: '11'
created: '2026-10-13'
baseline_revision: 'c087157a62093514e9ce045e099ad22baa3a5adc'
status: built
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
followup_review_recommended: true
context:
  - '{project-root}/AGENTS.md'
  - '{project-root}/.agents/skills/heap-discipline/SKILL.md'
  - '{project-root}/.agents/skills/hal-and-abstractions/SKILL.md'
  - '{project-root}/.agents/skills/scope-discipline/SKILL.md'
deferred:
  - summary: >-
      `EspNowLink.h`'s comment on `makeEspNowLink()` still says "Nothing calls it yet: the lobby and the GameLink task are later entries".
    evidence: |-
      `NearbyRadio::open()` calls it since entry 11. `src/games/EspNowLink.*` is outside entry 11's `touches`, so the comment is left for the next entry that touches the file (entry 13's sweep).
    location: >-
      src/games/EspNowLink.h:396
    severity: low
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** Everything Play Nearby needs runs over a link interface, but on a device nothing hands the lobby an `EspNowLink`: `NearbyRadio::open()` returns no link, `NEARBY_BUILT` is off, so a device never offers nearby, and nothing has run on two X4 Pros. The flip also leaves host fallout (the games check's skip, the mode-picker and launcher tests), the capture script cannot read the `LINK`, `PeerGone`, and `ABORT` lines of entries 8 and 15, and `pack_device_run.py` does not pack the `nearby` fixture.

**Approach:** This run builds everything of entry 11 except the owner's device run: the lobby's radio opens `makeEspNowLink()` and carries its label, `NEARBY_BUILT` turns on, the host fallout follows, `device_log.py`'s summary learns the nearby lines from a recorded sample, `pack_device_run.py` packs the `nearby` fixture, and the owner's smoke-run steps and the fields to record are written into this plan and a packet beside it. The run stops where the device run begins; the owner's results come back into this plan later.

## Boundaries & Constraints

**Always:**
- Entry 11's `touches` only; `lib/GameCore/**`, `games/**`, and every upstream file stay out (a file outside `touches`, or inside `stays_out`, is a blocking question, never a guess).
- The seam is D12's `NearbyRadio` (epic Notes, entry 10's checkpoint Decision): the link and label wiring only, in `GameLobbyActivity.cpp`'s device `NearbyRadio::open()`; the simulator branch (entry 16) is not changed.
- `NEARBY_BUILT` turns on only because the solo fallbacks are gone (entry 10 removed them; R9). Under `SIMULATOR` nearby is on whatever `NEARBY_BUILT` says.
- The games check keeps skipping a declared `nearby` because it plays only `solo` and `pass` (epic-first-party-games R10; epic Notes, the draft's check), never for want of the host capability.
- A test double names the device behaviour it stands in for, a test pins that they agree, and where it is more permissive than the device the plan says so.
- Measurements are like for like (AGENTS.md Known pitfalls); a total over 42,000 B flash or 160 B static RAM over the epic base stops the run and goes first in the hand-back.

**Never:**
- Run the owner's device run, flash a device, or claim a figure that only the device gives (the `GameLink` stack high-water, the ten-cycle heap, `silentRestart()`): each is a field for the owner to fill.
- Add reconnect, a third seat, or nearby saves (spec Non-goals); change `lib/GameCore`, the protocol, or `ReliableLink`; edit `deferred-work.md`; run `tickets.py mark` or `pull`, push, or open a PR.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Device, radio up | the lobby opens on a device (`!SIMULATOR`) | `open()` returns `makeEspNowLink()`'s link and its label (`X4 Pro 3F2A`), at most `MAX_HOST_LABEL_BYTES` | none |
| Device, radio will not start | `makeEspNowLink()` returns null (PSRAM or a bring-up step failed) | `open()` returns no link and no label; the lobby shows "Nearby could not start" | the factory already logged the step and handed Wi-Fi back |
| Host with no radio (harness) | the lobby opens on the host's own `NearbyRadio` | the harness's `EspIdfDriver` fails its first step, so no link starts; one bring-up attempt is counted | as the row above |
| Device caps | `gameHostCaps()` on a device build | `nearby` true; a two-seat game declaring `nearby` is offered all three modes | none |
| Simulator caps | `gameHostCaps()` with `SIMULATOR` | `nearby` true, as before | none |
| Games check, nearby declared | a game declares `solo`, `pass`, `nearby`; the host starts nearby | one note "declared mode nearby skipped: the games check plays only solo and pass"; never a failure; a round file naming `nearby` fails as "not solo or pass" | none |
| Games check, another mode the host cannot start | a declared mode other than nearby is not startable | the note keeps "this host cannot start it" | none |
| Summary, a nearby log | a raw or merged log with `LINK`, `PeerGone`, and `ABORT` lines | a `nearby:` block per device: radio bring-ups, per session the `STATE`s, adopts, queue-full, resend, silence, `ABORT`s, leaves, stack high-water; the `PeerGone` entries and reasons; `[ERR] [LINK]` lines | an unknown `LINK` line is listed verbatim, never dropped |
| Summary, no nearby line | a log with none | `nearby: none` | none |
| Pack | `pack_device_run.py` | `nearby.chgame` and its hash in `HASHES.txt` | a failed pack is exit 1 and writes no `HASHES.txt`, as before |

</frozen-after-approval>

## Code Map

- `src/games/GameHostCaps.{h,cpp}` -- `HostCapsValues::NEARBY_BUILT` false to true; `NEARBY = NEARBY_BUILT || IS_SIMULATOR` stays `constexpr` (no static initializer: `check_flash_budget.py objects`). Comments only otherwise.
- `src/activities/games/GameLobbyActivity.{h,cpp}` -- `NearbyRadio::open()`'s `#else` (device) branch calls `makeEspNowLink()` and copies `hostLabel()` into `Opened`; the class comment in the header already names entry 11 as the editor of the bodies. `EspNowLink.h` is included on the device only.
- `src/games/EspNowLink.{h,cpp}` -- read-only: `makeEspNowLink()`, `EspNowLinkT::hostLabel()`, the bring-up and `end()` (idempotent; the destructor calls it), the explicit instantiation.
- `src/games/NearbySession.cpp`, `src/activities/games/GameMatchActivity.cpp` -- read-only: the session's destructor ends the link; the lobby's `teardown()` and the match's `leave()` reset the session, which is how the radio goes off after each (host tests pin it over `FakeLink`: `...EndsTheLinkOnce`).
- `test/game_script/harness/list_stubs/EspNowLinkDouble.cpp`, `EspNowLinkScript.h` -- new: the host's `EspIdfDriver` (first step fails) and `makeEspNowLink()` over it, with an attempt counter; added to `game_launcher_src` by `nearby_lobby.cmake`.
- `test/game_script/harness/{NearbyLobbyTest,GamesLauncherTest,ModePickerTest,GameHostCapsSimulatorTest}.cpp`, `test/game_core/GameHostCapsTest.cpp` -- the caps flip's host fallout and its pins.
- `test/game_script/harness/games_check/{GameCheck,GamesCheckEngineTest}.cpp` -- the skip note's reason.
- `scripts/device_log.py`, `scripts/device_log_test.py`, `docs/crosshatch/fork-scripts.md` -- the summary's `nearby:` block, patterns pinned to `GameLinkLog.h`, `EspNowLink.h`, `GameMatchActivity.cpp`, and a recording from two simulator instances.
- `scripts/pack_device_run.py`, `scripts/pack_device_run_test.py` -- `('nearby', 'nearby')` in `GAMES`.
- `story-nearby-on-two-x4-pros-wiring-and-the-smoke-run-packet.md` (beside this plan) -- the owner's steps.

## Tasks & Acceptance

**Execution:**
- [x] `src/games/GameHostCaps.{h,cpp}` -- `NEARBY_BUILT = true` -- a device offers nearby now that the lobby has a link.
- [x] `src/activities/games/GameLobbyActivity.cpp` -- the device `open()` calls `makeEspNowLink()` and carries its label -- the entry's wiring (D12).
- [x] `test/game_script/harness/list_stubs/EspNowLink{Double.cpp,Script.h}`, `nearby_lobby.cmake` -- the host's no-radio double so the lobby links on the host -- the real code in the lobby's device branch is also compiled by the harness.
- [x] Host tests that expected nearby off follow the caps (launcher modes line, title screen's Options cycle, the simulator and device caps pins, `GameHostCapsTest`).
- [x] `test/game_script/harness/games_check/GameCheck.cpp`, `GamesCheckEngineTest.cpp` -- the skip note's reason is the check's rule.
- [x] `scripts/device_log.py`, `scripts/device_log_test.py`, `docs/crosshatch/fork-scripts.md` -- the summary's nearby block and its tests.
- [x] `scripts/pack_device_run.py`, `scripts/pack_device_run_test.py` -- pack the `nearby` fixture.
- [x] The owner's steps and the fields to record, in this plan and the packet -- the run stops here.
- [x] Firmware checks once, after the review: x4pro and default builds, `pio check` for both, `check_flash_budget.py` `build on`, `build off`, `compare`, `objects`, `sim.sh build x4pro`.

**Acceptance Criteria:**
- Given a device build, when the lobby opens, then its radio asks `makeEspNowLink()` for the link and carries the link's label; the simulator's branch is unchanged and its two-instance nearby round still plays.
- Given the host suites, when they run, then they pass with nearby on in the host caps, `GamesCheckEngineTest.ADeclaredNearbyIsSkippedAndLoggedNeverAFailure` included.
- Given a recorded two-instance log, when `device_log.py summary` reads it, then each device's `nearby:` block gives its sessions, `STATE` counts and last `ver`, `ABORT`s, leaves, stack figures, and `PeerGone` entries.
- Given `pack_device_run.py`, when it runs, then `nearby.chgame` is among the packages.
- Given the epic base `c28120a7`, when x4pro games on and off are measured like for like, then the epic total is reported and is at most 42,000 B flash and 160 B static RAM, or the run stops first.

## Implementation Notes

- Route: full. The build agent implemented directly from the entry and the epic Notes before this plan was written to disk (the plan records the work and its evidence; no implementation subagent ran), so the Tasks above are ticked from the diff, and the review lenses (context-free subagents) are the independent check.
- The simulator build and a two-instance nearby run were done once at the implemented tree (before review); the lens patches, if any touch `src/`, rerun the build, as the Verification section records.
- The packet beside this plan holds the owner's steps; it is a draft until the orchestrator fills in the firmware commit and SHA-256 at build time.

## Plan Change Log

## Review Triage Log

Pass 1. Four lenses ran as context-free foreground subagents (blind hunter, edge-case hunter, verification gap, intent alignment) over the diff since `baseline_revision` (64,419 B; the plan itself is not in the diff). 25 rows (some merge several lens findings of one root cause). Verdicts: 0 high, 5 medium, 13 low, 6 false, 1 descriptive (intent alignment reports readings, not findings). Routes: 13 patch, 1 defer, 10 reject (6 false, 4 low), 1 descriptive. No intent_gap, no bad_plan, no loopback. Patched in one pass, then the host suites, script tests, ledger check, and format check were re-run (all pass).

| # | Lens | Finding | Verdict / route | Evidence or action |
|---|------|---------|-----------------|--------------------|
| 1 | verification-gap | The device branch of `NearbyRadio::open()` is run only in its failure path (the double never succeeds), so the label copy and the link hand-off are unpinned | medium / patch | Real: a dropped `labelLength` assignment passed every test. `EspNowLinkScript.h` gained `succeed`, `mac`, and teardown counters; `EspNowLinkDouble.cpp` scripts every driver step; `TheFakeRadioHandsOutWhatTheDeviceRadioDoes` now asserts the success path (link, label `X4 Pro 3F2A`, the teardown counted, Wi-Fi handed back). Mutant (the `labelLength` line removed) turns it red. |
| 2 | edge-case | `GamePackageInstallerTest`'s `nearby` fixture still expects `MODE_PASS` only, "its nearby mode is not startable on this host" | medium / patch | Real and part of the entry's "installer tests follow the new caps": it passed because it checked only the modes it listed. Now `MODE_PASS \| MODE_NEARBY` with the false comment gone. |
| 3 | blind, edge-case (3 rows) | Stale comments: `HostCapsScript.h` ("off ... until epic-play-nearby turns it on"), `game_host_caps_simulator.cmake` ("stays false"), `RoundFileTest.cpp:50` ("solo and pass, not nearby" as what the host starts) | low / patch | Each contradicted the flip; reworded (the last now says what the games check plays). |
| 4 | blind, edge-case | `device_log.py`: a guest's `task started` carries `session=00000000` and its stop the host's id, so the per-session stack line reads `stack -` | medium / patch | Real for the guest's figure the packet asks the owner to record. A session with no start line of its own shows the device's stack; the guest's stack assertion is in the recorded-sample test. |
| 5 | edge-case | A failed bring-up also prints `[ERR] [GAME] Nearby: the radio did not start ...`, which the summary missed | low / patch | The lobby's `[ERR] [GAME] Nearby:` lines are listed with the `[ERR] [LINK]` ones; a test covers one. |
| 6 | blind, edge-case | The packet presents `silence n=1` only as a powered-off peer, but a clean leave also logs it about 10 s after `PeerGone` | medium / patch | Real (the recording shows it): the packet says it is expected after a clean leave. |
| 7 | blind | The packet's pass/fail for the heap step is a "few hundred bytes" with no baseline, `Free` only | medium / patch | Reworded: record `Free`, `Min Free`, `MaxAlloc` before and after, the idle spread as the noise, the tolerance the owner's call, figures written either way. |
| 8 | blind | The packet lacks the `sha256sum` and size commands, and does not say to flash the release firmware back after a diagnostics build | low / patch | Added. |
| 9 | blind | Step 1's second reader can pulse DTR/RTS; the "Wi-Fi in use after closing" refusal is waved off | low / patch | Step 1 is first and says to record a reset; the refusal is a recorded finding. |
| 10 | blind | The packet says the ring overflow is not logged while the summary prints `queue full ... overflows` | low / patch | Clarified: `queue full` is the 4-deep session queue; the receive ring's counter is not logged. |
| 11 | blind | `abort recv` shows `x3` as if three aborts | low / patch | The firmware logs a line per copy heard; the summary label and `fork-scripts.md` say so. |
| 12 | blind | `LINK_LINE` accepts `WRN`, which the `LOG_*` macros do not emit; three blank lines before the recorded block | low / patch | Removed `WRN`; spacing fixed. |
| 13 | blind, edge-case, claim (3 rows) | `NEARBY_BUILT = true` is global to every games board, Sticky included, which no run covers | low / reject | R9 and the epic's Decisions say "true on the device"; Sticky and a mixed pair stay open in the spec (R12) and the packet now says a Sticky offering nearby is expected and untested. Gating on the X4 Pro is an intent choice the epic does not make; residual risk below. |
| 14 | edge-case | `makeEspNowLink()` blocks up to half a second inside `open()` on the loop task with no "starting" screen | low / reject | `NearbyRadio::open()`'s own contract says "Lobby task; may wait", the simulator's `UdpLink` path is the same call, and entry 10 chose no interstitial; a screen is new design, and the smoke run shows the wait on the panel. |
| 15 | edge-case | `EspNowLink.h:396`'s comment says "Nothing calls it yet" | low / defer | Real and stale, but `src/games/EspNowLink.*` is outside entry 11's `touches`; deferred (frontmatter). |
| 16 | blind | The `|| IS_SIMULATOR` term of `NEARBY` is no longer pinned (both terms are true) | low / reject | `NEARBY` is true either way today and no behaviour differs; the term matters only if the device constant is turned off again, and `GameHostCapsSimulatorTest` still pins the simulator's answer. Not worth a second test build of the expression. |
| 17 | blind | The games-check note changed for nearby with no consumer updated | false / reject | Searched `docs/`, `.github/`, `scripts/`, and the tests for the old text: only the one test that asserts it (updated). Entry 12's historical plan is a record, not a consumer. |
| 18 | blind | `GameLobbyActivity.cpp` lacks a direct `<memory>` include | false / reject | The header it includes already includes `<memory>`, and the file includes `<algorithm>`, `<span>`, `<utility>` directly. |
| 19 | verification-gap (other) | `device_log_test.py` pins firmware format wording, a fragile check | false / reject | It is the file's existing mechanism (`FORMATS`) for every pattern, and a reworded line should fail it; the parsers are also run against the recording and the synthetic lines. |
| 20 | blind | Packet has unfilled placeholders (commit, SHA-256, `.chgame` hash) | false / reject | Deliberate: the firmware is built at the merged epic head by the orchestrator, after this run (the task's stated scope). |
| 21 | blind | "Within a second or two", "does the panel look right" give no pass criteria | low / reject | They are observations the owner records (D2's unknown is the panel itself, an estimate until this run); a numeric bar would be invented. |
| 22 | blind, edge-case | `GameCheck.cpp`'s note is now unconditional for nearby | false / reject | Intended: the entry says the check keeps skipping nearby because it plays only solo and pass, not because the host lacks it. |
| 23 | edge-case | `device_log.py` sessions of a guest split across `00000000` and the host id | low / patch | Same cause as row 4. |
| 24 | blind | The flip lands before any device has run | false / reject | The task's scope is the wiring, stopping at the device run; the owner's run is the packet. |
| 25 | intent-alignment | Descriptive: the diff implements the wiring plus host-side verification; the device surface (real `EspNowLink.cpp`, the label from a real MAC, a device build) is not exercised by tests in the diff | descriptive | Addressed by row 1 for the open() branch, and by the firmware checks that follow (a device-env build compiles the real driver); the radio itself is the owner's run. |

## Design Notes

1. **The seam (D12; epic Notes, entry 10's checkpoint, and `story-lobby-screens-and-the-nearby-start-plan.md` D12).** `NearbyRadio::open()` is the one place the device wiring lives, edited here "the link and label wiring only". Its `#else` branch now mirrors the simulator's: take the link from the factory, copy `hostLabel()` (at most `Opened::label`'s size) into `Opened`, return it; null leaves `Opened{}`, which the lobby shows as "Nearby could not start" (the lobby's existing path; the factory has already logged the failed step and handed Wi-Fi back, `EspNowLink.h` `failed()`). `clock()`, `random()`, `lobbySettings()`, and `sessionOptions()` already return the device's values and stay. The label's source is entry 5's plan (the link builds it from the MAC in the radio layer, `EspNow::makeLabel`); no label code is added here.
2. **Existing function guards (AGENTS.md `git log -L` pitfall).** `NearbyRadio::open()`: its one guard is "no link leaves the link null" (the early `Opened{}`); kept. `gameHostCaps()` (history `3d4aec97`, `17d02483`, `13a4c60b`): no early return or ordering guard; `NEARBY` stays `constexpr`, so `check_flash_budget.py objects` sees no initializer. `GameCheck.cpp`'s mode-note loop had one condition (declared and not startable); it is kept for solo and pass and nearby gets its own reason, so a declared mode the host cannot start is still logged and never a failure.
3. **Why the harness needs a double.** `GameLobbyActivity.cpp` is compiled by the host harness (`nearby_lobby.cmake`), neither under `SIMULATOR` nor on a device, so its `#else` branch now links against `makeEspNowLink()` and `EspIdfDriver`, which are device code (`EspNowLink.cpp` is excluded by name, `esp_now_link.sources.cmake`). The double (`EspNowLinkDouble.cpp`) defines `EspIdfDriver` with a first step that fails and `makeEspNowLink()` over it, so the real `EspNowLinkT::create()` runs its failure path on the host (the null the device returns when a step fails) and the explicit instantiation the device file makes is repeated. It stands in for: the device's bring-up. More permissive than the device in one way: it never succeeds, so the host never sees a started `EspNowLink`; `EspNowLinkTest` (the same template over a fake driver) holds the link's logic, and the device run holds the radio. A test (`TheFakeRadioHandsOutWhatTheDeviceRadioDoes`) pins that the device `NearbyRadio::open()` calls the factory once and returns no link and no label when it fails. The label copy and the success path of the device branch are compiled but not run on the host; the smoke run shows the host label on both screens (the same gap the simulator branch has, `## 6.16`).
4. **The skip note.** The games check plays solo and pass only (epic-first-party-games R10, epic Notes "the draft's check", `NEARBY_BUILT` on keeps the games check skipping); with nearby on in the host caps, `check.modes` now holds `MODE_NEARBY`, so the old condition (declared and not startable) would stop logging the skip. The note is now keyed to the check's own rule. `RoundFile.cpp` is untouched: a round file naming `nearby` already fails as "not solo or pass" (an existing test), whatever `hostModes` holds.
5. **Host fallout of the flip.** Found by flipping and running the suites: two failures (`ListTest.TheModesLineNamesTheModesThisHostCanStartInOrder`, `TitleScreenTest.TheModesOptionsCyclesAreTheOnesCheckLeavesForThisHost`) assumed a host with no radio by default. Each keeps its no-radio case by scripting `hostcaps::script().nearby = false` and gains the device's case (all three modes, nearby last), so both hosts stay covered. `GameHostCapsSimulatorTest`'s name and comment said nearby was not built; they now pin that the simulator's answer does not depend on `NEARBY_BUILT`. The installer suites link the real `GameHostCaps.cpp` and passed unchanged.
6. **The summary's `nearby:` block.** The lines come from `GameLinkLog.h` (one function per `LINK` line), `EspNowLink.h` (`radio up`, bring-up failures), and `GameMatchActivity.cpp` (the lifecycle line into `PeerGone` and `the other player is gone: <reason>`). Counter lines (`resend`, `silence`, `abort sent`, `link refused`) are shown as the firmware's running totals (the last `total=`), not a sum of steps. A `LINK` line with no pattern is listed verbatim and an `[ERR] [LINK]` line always is, so a reworded firmware line surfaces. The patterns are pinned to the firmware's format strings by the existing `FORMATS` mechanism. The "recorded sample": the host tests print no log, so the sample is a recording of two simulator instances (same `GameLinkLog.h` and match code, over `UdpLink`) of a lobby, a round with a rejected tap, the host's Leave, and the guest's "player left" and Back, embedded verbatim in `device_log_test.py` with its provenance; it has the device's format and not its timing (the simulator's stack high-water is a constant 2048 B and no `radio up` line is printed, which synthetic lines from `FORMATS` cover). The device's capture replaces it at the smoke run.
7. **Budget.** The epic base is `c28120a7` (+258,048 B flash, +784 B static RAM, games on minus off). Entry 10's tree with `EspNowLink` reachable measured +40,256 B / +88 B (epic Notes, Measurement 2026-10-13); this entry makes it reachable for real, so its own cost is the wiring's few hundred bytes over that figure. The measurement is in Verification.

## Verification

**Commands:**
- `cmake --build build/test && ctest --test-dir build/test --output-on-failure -j4` (host, both locks) -- expected: all pass.
- `for t in scripts/*_test.py; do python3 "$t"; done`, `python3 scripts/check_upstream_touches.py`, `./bin/clang-format-fix` twice -- expected: pass, PASS, no new change.
- Firmware checks (once, after the review): see Results.

**Manual checks (if no CLI):**
- Two simulator instances (`SIM_INSTANCE=1` and `2`), the `nearby` fixture: lobby, join, Start, two moves and a tap on a taken square, host Leave, guest "The other player left" and Back.

### Results

All at the tree that became the commit (its `src/` is byte-identical to what the builds and the two-instance run used; after the review patches only tests, scripts, and docs changed, and each check below was run after them).

- **Host suites** (`cmake --build build/test && ctest --test-dir build/test -j4`, hosttest lock): 2,523 tests, all pass; `NearbyLobbyTest.TheFakeRadioHandsOutWhatTheDeviceRadioDoes`'s success-path assertions turn red with the `labelLength` copy removed (mutant, restored); `GamesCheckEngineTest.ADeclaredNearbyIsSkippedAndLoggedNeverAFailure` passes with nearby on in the host caps (it asserts `gameHostCaps().nearby` first). Two tests failed on the first flip run (`ListTest.TheModesLineNamesTheModesThisHostCanStartInOrder`, `TitleScreenTest.TheModesOptionsCyclesAreTheOnesCheckLeavesForThisHost`) and were updated; the review found a third that passed for the wrong reason (`InstallerTest.TheFixtureGamesTheReadmeListsInstallAndCanStart`) and it was updated too.
- **Fork script tests** (every `scripts/*_test.py`, each in its own process): all pass, each with a `Ran n tests` line. `device_log_test.py` 44 tests, five mutants of `device_log.py` (PeerGone entry, running totals, abort-copy count, high-water, error routing) each turn it red; `pack_device_run_test.py` 30 tests.
- **`scripts/check_upstream_touches.py`**: PASS (no upstream file touched). **`./bin/clang-format-fix`** run twice after the last edit: nothing changed beyond this story's own files, nothing outside its paths, no upstream file.
- **Simulator** (`sim.sh build x4pro`, build lock, shared cache, after the last `src/` change): `simulator_x4pro` SUCCESS. Entry 16's nearby, unchanged: two instances (`SIM_INSTANCE=1` and `2`) with the `nearby` fixture played a lobby (host "Simulator 23872", "Player 2 joined", the guest's "Waiting for the host to start"), Start, two moves, a tap on a taken square (rejected on the guest only), the host's Leave, and "The other player left" on the guest with Back; screenshots looked at, none committed (this entry's verify names none). The log of that run is the recorded sample in `device_log_test.py`.
- **Firmware** (`scripts/check_flash_budget.py`, build lock, shared cache `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`, incremental tree in this worktree `/home/user/wt/epic-play-nearby-lane-a`, 28 compile lines in the on and off builds together, so close to the cache; not a fresh clone), x4pro games on minus off:
  - At this entry's tree: `firmware.bin` 6,255,680 B on, 5,957,328 B off, **+298,352 B flash** (8,848 B under the 307,200 B gate); static internal RAM 190,328 B on, 189,464 B off, **+864 B** (`.dram0.bss` +96 B, `.iram0.text` +760 B, `.iram0.text_end` +8 B; 160 B under 1,024 B). `compare`, `objects` (63 game objects, largest mutable static 4 B, no static initializer), and `touch-line` exit 0.
  - Like for like at this entry's base (`c087157a`, the same worktree and path, `src/` restored to it, `build on`, `build off`, `compare`): 6,244,176 B on, 5,957,328 B off, +286,848 B and +784 B (the same figures entry 16 recorded at that commit). So **this entry's own cost is +11,504 B flash and +80 B static RAM**: entry 5's driver measured +11,456 B / +80 B, so the wiring itself is +48 B.
  - **Epic total over the epic base `c28120a7` (+258,048 B, +784 B): +40,304 B flash and +80 B static RAM**, within the 42,000 B / 160 B share, with 1,696 B flash and 80 B static RAM left, and the gate 300 KiB with 8,848 B left. Entry 13's recorded estimate of about 1.2 KB of flash would leave about 500 B of the share. Caveat: the base figure was taken in the main checkout (`/home/user/crosshatch-player`) and this tree's path is longer; the earlier path concern (+448 B, entry 1) was shown to be code, not path, so no correction is applied. Entry 10's scratch measurement (+40,256 B / +88 B) agrees to within 48 B and 8 B.
  - `pio run -e default` (the C3; games off, so none of this entry's code builds): SUCCESS. `pio check` for `default` and for `-e x4pro` with `--fail-on-defect low --fail-on-defect medium --fail-on-defect high`: both PASSED.
- **Not run, by scope**: the owner's smoke run; `check_flash_budget.py` through CI's fresh tree (no gate or workflow changed here).

### Owner's smoke run: fields to fill

See the packet beside this plan (`story-nearby-on-two-x4-pros-wiring-and-the-smoke-run-packet.md`) for the steps; each field below is the owner's, left blank on purpose.

| Field | Value |
|-------|-------|
| Firmware commit | |
| Firmware SHA-256 | |
| Merged log: the lobby (host label on both screens, "Player 2 joined") | |
| Merged log: the join | |
| Merged log: a round of the `nearby` fixture, with a rejected move | |
| Merged log: Leave and "player left" on the other device | |
| `GameLink` task stack high-water (bytes free, each device, each stop) against the margin entry 8 stated (4,096 B stack; no number was stated, the nearest rule is the VM's 2,048 B headroom, `deferred-work.md` `## owner-e4-entry14`) | |
| Internal heap, one device: before ten bring-ups and after (`[MEM] Free` and `Min Free`), no peer | |
| `silentRestart()` needed after teardown (yes / no, and why) | |
| `## 6.3`: capture with and without `pio device monitor` open | |
| `## 6.3`: connecting does not reset the board | |
| `## 6.3`: each sleep gives a lost and a connected line | |
| `## 6.5`: PSRAM written from the Wi-Fi task's receive callback, the ring's ordering (a clean round with no torn or reordered frame) | |
| Entry 10's D2: the host list's 1,000 ms refresh on the panel | |
| `docs/crosshatch/fork-scripts.md`'s run steps worked as written (or the corrections) | |

## Auto Run Result

**Summary:** A device's lobby now opens a real radio: the device `NearbyRadio::open()` calls `makeEspNowLink()` and carries the link's label (`X4 Pro 3F2A`) into the host's screen and ADVERT, and `HostCapsValues::NEARBY_BUILT` is true, so a device offers Play Nearby (the simulator's UDP path is unchanged and still plays a round). The host fallout follows the flip: the games check skips a declared `nearby` because it plays only `solo` and `pass`, and the launcher, title-screen, installer, and caps tests follow the new caps with a no-radio case kept. `device_log.py`'s summary gains a `nearby:` block (radio bring-ups, each `LINK` session's `STATE`s, adopts, resends, silences, `ABORT`s, leaves and `GameLink` stack, the `PeerGone` entries and reasons, errors), tested against a recording of two simulator instances; `pack_device_run.py` packs the `nearby` fixture. The owner's smoke-run steps and the fields to record are in this plan and the packet beside it; the run stops there. Epic total over the base: **+40,304 B flash, +80 B static RAM**, within 42,000 B / 160 B.

**Files:**
- `src/games/GameHostCaps.{h,cpp}` - `NEARBY_BUILT` true, comments.
- `src/activities/games/GameLobbyActivity.cpp` - the device `open()` branch (link and label).
- `test/game_script/harness/list_stubs/EspNowLink{Double.cpp,Script.h}`, `nearby_lobby.cmake`, `NearbyLobbyTest.cpp` - the host's `EspIdfDriver` double (no radio by default, a scripted success), pinned by the lobby test.
- `test/game_script/harness/{GamesLauncherTest,ModePickerTest,GamePackageInstallerTest,GameHostCapsSimulatorTest,RoundFileTest}.cpp`, `list_stubs/HostCapsScript.h`, `game_host_caps_simulator.cmake`, `test/game_core/GameHostCapsTest.cpp` - the flip's fallout.
- `test/game_script/harness/games_check/{GameCheck,GamesCheckEngineTest}.cpp` - the skip note's reason.
- `scripts/device_log.py`, `scripts/device_log_test.py`, `docs/crosshatch/fork-scripts.md` - the summary's nearby block.
- `scripts/pack_device_run.py`, `scripts/pack_device_run_test.py` - the `nearby` package.
- This plan and `story-nearby-on-two-x4-pros-wiring-and-the-smoke-run-packet.md`.

**Review breakdown:** four context-free lenses ran (blind hunter, edge-case hunter, verification gap, intent alignment). 25 triage rows: 0 high, 5 medium, 13 low, 6 false, 1 descriptive; 13 patched (the device branch's success path and its double, the installer test that passed for the wrong reason, the guest's per-session stack figure, the `Nearby:` error lines, the packet's silence note, heap criteria, and recovery steps, stale comments), 1 deferred (see frontmatter), 10 rejected with reasons in the Review Triage Log. The implementation was done directly by the build agent (no implementation subagent), so the lenses are the only independent check.

**Verification:** see Results above. Screenshots: none committed; this entry's verify names none (the two-instance run was looked at).

**Formatting:** `./bin/clang-format-fix` changed no file outside this story's own paths and no upstream file.

**Follow-up review recommended:** true. Five medium findings were patched in test infrastructure (the scripted `EspIdfDriver` double now has a success mode) and in `device_log.py`, after the lenses ran, and nothing re-reviewed them.

**Residual risks:** the device `open()` success path runs only through the double (the real radio, the MAC-derived label, and teardown are the owner's run); `NEARBY_BUILT` is on for every games board, Sticky included, which no run covers; `open()` blocks up to half a second on the loop task (the packet has the owner watch it); the `|| IS_SIMULATOR` term of `NEARBY` is no longer pinned by a test; the recording in `device_log_test.py` is the simulator's (stack figure 2048 B constant, no `radio up` line); the epic share has about 1.7 KB of flash left, against entry 13's estimated 1.2 KB; the stale comment at `EspNowLink.h:396` is deferred. A timing figure in this plan: none claimed (the 1,000 ms list refresh, the 4 KB stack, and the ten-cycle heap are the owner's fields).

**Owner's steps:** the packet beside this plan. The orchestrator builds the firmware packet from the merged epic head (`PLATFORMIO_BUILD_FLAGS=-DCROSSHATCH_TOUCH_LOG=1 pio run -e x4pro`), fills the packet's firmware commit, SHA-256, and `nearby.chgame` hash, and brings the owner's results back into the "Owner's smoke run: fields to fill" table.
