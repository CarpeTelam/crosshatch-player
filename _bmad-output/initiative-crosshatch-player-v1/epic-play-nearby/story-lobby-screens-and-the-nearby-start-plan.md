---
title: 'Lobby screens and the Nearby start'
type: 'feature'
ticket: '10'
created: '2026-10-12'
baseline_revision: '5c617c04449ba76c23bdea45efebf20a9f878609'
status: built
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'pinned'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
followup_review_recommended: true
context:
  - '{project-root}/AGENTS.md'
  - '{project-root}/docs/crosshatch/orchestrated-epics.md'
  - '{project-root}/docs/contributing/touch-and-ui.md'
warnings: ['oversized']
deferred:
  - summary: >-
      No lobby test runs a game with more than two seats, so the host's per-seat liveness loop, "Player 3 joined", and "2 of 3 players" are untested; the loop checks every seated guest, and only a 3-seat nearby game reaches it.
    evidence: |-
      Every `NearbyLobbyTest` lobby is built from `nearbyManifest()` (seats 2..2) and `HostCaps.maxSeats` is 2, so a mutation that checks only seat 2 passes. `LobbyTest.GuestHeardMsOfASeatThatLeftIsZeroAndItIsPerSeat` covers the accessor per seat with 3 seats, not the screen. Trigger: the first game declaring nearby with seats.max 3 (or a `maxSeats` above 2).
    location: >-
      test/game_script/harness/NearbyLobbyTest.cpp; src/activities/games/GameLobbyActivity.cpp readHost
    severity: low
  - summary: >-
      With more than two seats, one of two seated guests leaving can be overwritten by the screen's resync, so the host line reads "Player N joined" after a departure.
    evidence: |-
      `readHost` resyncs `HostBody` to Joined whenever `seats >= 2` and the body is not Joined, so a `GuestLeft` event with a guest still seated is lost. Unreachable in v1 (`seatLimit()` is 2: one guest, so a leave always drops `seats` below 2). Fix with the first 3-seat game: keep Left when the last polled event was a leave.
    location: >-
      src/activities/games/GameLobbyActivity.cpp readHost
    severity: low
  - summary: >-
      A host lobby that reaches `HostState::Closed`, or a guest whose link dies mid-lobby without our own cancel, would show "Waiting for a player" or "Looking for games nearby" with no radio and no way out but Back.
    evidence: |-
      The edge-case lens relayed it. `HostState::Closed` and `GuestState::Cancelled` are set only by `cancel()` (after which the session is reset), and `ILink` has no failure signal, so no path reaches it on the host harness; whether `EspNowLink` can drop mid-lobby is unknown until entry 11's device run. If it can, the lobby should `failToChoose()` on it.
    location: >-
      src/activities/games/GameLobbyActivity.cpp readHost, pollGuest
    severity: medium (unverified)
---

<intent-contract>

## Intent

**Problem:** Nearby has a link, lobbies, a session, a match, and endings, but no screen reaches them: the title screen starts a Nearby pick as a solo match, a player cannot host or join, and nothing refuses when Wi-Fi is up or heap is low (R8, R9).

**Approach:** A new `GameLobbyActivity` on `UiListActivity` (Host a game / Join a game, the host's waiting-joined-Start-left screen, the guest's host list and its waiting, host-left, and full screens) over `NearbySession::openHost` and `openGuest`; the title screen runs entry 5's probe decision, refuses with a dialog or pushes the lobby, and loses its solo fallbacks; `goHome`'s games names move into one pure header. The lobby gets its link, clock, random, label, and probe reading from a small radio seam, which the harness fills with `FakeLink`; `NEARBY_BUILT` stays false.

## Boundaries & Constraints

**Always:** Build on `UiListActivity`; never `rowTouch` / `wasTapInRect`. Every string is `tr(STR_GAMES_*)`, new keys in `english.yaml` only. The lobby never takes `RenderLock` in a destructor or `onExit`; it takes it on the loop task when it changes what the render task reads. It never touches `resume.bin`. Allocation is `makeUniqueNoThrow` / `std::nothrow`, locals under 256 B. `ActivityManager.cpp` changes only inside `#if FREEINK_CAP_GAMES`, and ledger row 5's Change text is updated in the same commit. Keep every guard of the code this plan moves or rewrites (Design Notes 9).

**Never:** Edit `lib/GameCore/**`, `src/games/EspNowLink.*`, `src/games/GameHostCaps.*`, `src/games/NearbyProbe.*`, `src/games/NearbySession.*`, `GameMatchActivity.*`, `GameConfirmDialog.h`, `GamesLauncherActivity.*`, `scripts/check_layers.py`, `games/**`, `test/game_script/first_party/**`, or any other upstream file (each is outside this entry's `touches`: a need for one is a HALT). Do not turn `NEARBY_BUILT` on. Do not change the wire, the lobby timing defaults, or the title screen with nearby off (it must be unchanged).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Refused: Wi-Fi up | Nearby picked, New game, probe says Wi-Fi in use | Dialog "Wi-Fi is in use. Close it and try again." on the title screen; no lobby, no radio opened | OK / Back / Confirm closes it; the title screen is as it was |
| Refused: low heap | probe says heap under 102,400 B (102,399 refuses, 102,400 passes) | Dialog "Not enough memory for Nearby. Restart the reader and try again." | as above |
| Refused: both | Wi-Fi up and heap low | The Wi-Fi dialog (Note D1) | as above |
| Host flow | probe passes; Host a game; a guest joins; Start | Host screen: label large, "Waiting for a player", "1 of 2 players", Start greyed; then "Player 2 joined", Start enabled; Start replaces the lobby with the match, session moved in | Start on a guest not heard for 3 s is greyed (D3) |
| Guest leaves before Start | joined guest cancels or goes silent | "Player 2 left" with "Waiting for a player", Start greyed, a new guest can join | a delayed duplicate is the 10 s silence path's |
| Guest flow | Join a game; host list; tap a joinable host | Rows with label and "1 of 2 players"; "Waiting for the host to start"; Start replaces the lobby with the match | tap on a host that expired between draw and tap re-lists, joins nothing |
| Different version | host with another package hash | Row greyed, "Different version", takes no tap | a JOIN that the host still refuses shows "Different version" with Back to the list |
| Host leaves / full | host cancels or goes silent; a third guest | "The host left" + "Back to the list"; "That game is full" + "Back to the list" | Back to the list returns to the host list, hosts re-listed |
| Cancel / Back / Home | any lobby screen | ABORT(left) when seated, session destroyed (radio off), title screen; Home or sleep does the same in `onExit` | teardown is idempotent; no `RenderLock` in `onExit` |
| Radio would not start | `open()` returns no link or `openHost` / `openGuest` returns null | Choose screen stays, "Nearby could not start. Try again." above the rows | logged `LOG_ERR`; no session left |
| Nearby over a pass save | title screen with a `resume.bin`, Nearby picked | No "Start a new game?" question; `resume.bin` bytes unchanged; `prefs.bin` holds Nearby | settings unread: nothing starts, as for any New game |
| F14 | Continue with no startable mode on this host | The Continue row's line says `STR_GAMES_RESUME_NOT_HERE` and the log line stays | save untouched |

</intent-contract>

## Code Map

- `src/activities/games/GameLobbyActivity.{h,cpp}` -- NEW. `UiListActivity` subclass `NAME = "GameLobby"`: the five screens, the poll, the radio seam (`NearbyRadio`, `setRadio`), `lobbyGameFor(manifest, pkgHash)`, teardown. Whole file `#if FREEINK_CAP_GAMES`.
- `src/activities/games/GamesHomeRow.h` -- NEW, pure (std only): `isGamesActivity(std::string_view)` and the five name constants.
- `src/activities/games/GameModeActivity.{h,cpp}` -- `startNew` (probe, refusal, lobby push), `rosterFor` (no Nearby roster), `activateIndex` (no confirm for Nearby), `startResume` (F14 note), prefs write extracted, class comment rewritten. Reuse `showSettingsNotice`'s row-note pattern, `GameConfirmDialog` read-only as the dialog pattern.
- `src/activities/ActivityManager.cpp` (`goHome`, ~354-359, and the includes ~33-35) -- the three-name `if` becomes one call. Upstream file, ledger row 5.
- `docs/crosshatch/upstream-touches.md` -- row 5's Change text.
- `lib/I18n/translations/english.yaml` -- 16 `STR_GAMES_*` keys (Design Notes 2).
- `lib/GameCore/HostLobby.{h,cpp}` -- `guestHeardMs(seat)` only (owner Decision, D3), and, `lib/GameCore/ReliableLink.{h,cpp}` `heardMs(PeerHandle) const`; tests in `test/game_core/{LobbyTest,ReliableLinkTest}.cpp`.
- Read-only: `src/games/NearbySession.h` (`openHost`, `openGuest`, `lobby()`, `startHost`, `beginMatch`, `roster`, `counters`, `leave`), `lib/GameCore/{HostLobby,GuestLobby,Lobby}.h`, `src/games/NearbyProbe.h`, `src/games/GameClock.h`, `GameRandom.h`, `GameMatchActivity.h` (the constructor takes `std::unique_ptr<NearbySession>` last), `UiListActivity.h`, `option-dialog.h`.
- `test/game_script/harness/list_stubs/{HostCapsScript.h,GameHostCapsDouble.cpp}` -- add `nearby` (default `HostCapsValues::NEARBY_BUILT`). NEW `list_stubs/NearbyProbeDouble.cpp` + `NearbyProbeScript.h`: defines `NearbyProbe::read()` (the real one is excluded from the harness by name).
- `test/game_script/harness/` NEW `nearby_lobby.sources.cmake` (excludes `GameLobbyActivity.cpp` from the shared libraries), `nearby_lobby.cmake` (adds it and the probe double to `game_launcher_src` with `target_sources`, and the suite), `NearbyLobbyTest.cpp`, `NearbyLobbySupport.h` (`FakeNearbyRadio`, the rig, reading helpers), `GamesHomeRowTest.cpp`.
- `test/game_script/harness/ModePickerTest.cpp` -- the nearby solo-fallback tests (`NewGameInARememberedNearbyStartsNearbyAsSolo` ~697, `NewGameOfAPassAndNearbyGameStartsPass` ~689, the Continue-passes-nearby's-roster test ~1223, and the `HostCapsDouble` default pin ~2153) updated.
- Reuse, do not edit: `NearbySessionSupport.h` (`Duo`, `ObservedLink`, `FakeRtosClock`, `waitFor`), `NearbyMatchTest.cpp` (rig for two matches), `test/game_core/FakeLink.h`, `fixtures/nearby/`.

## Tasks & Acceptance

**Execution:**
- [ ] `lib/I18n/translations/english.yaml` -- add the 16 keys of Design Notes 2 under the `STR_GAMES_NEARBY_*` names -- strings first, so the build fails on a missing key
- [ ] `src/activities/games/GamesHomeRow.h` -- constants for GamesLauncher, GameMode, GameOptions, GameLobby, GameMatch and `isGamesActivity` -- the testable mapping (`## 4.9`, `## 4.13`, `## 5.10`, `## 5.12`, `## sync-e54c0087`)
- [ ] `src/activities/ActivityManager.cpp`, `docs/crosshatch/upstream-touches.md` -- `goHome` calls `isGamesActivity`; includes become `GamesLauncherActivity.h` and `GamesHomeRow.h`; row 5's Change text names the pure header and the five names
- [ ] `src/activities/games/GameLobbyActivity.{h,cpp}` -- the lobby as Design Notes 1, 4, 5, 6 -- R8
- [ ] `src/activities/games/GameModeActivity.{h,cpp}` -- probe, refusal dialog, lobby push, no fallbacks, no confirm and `resume.bin` untouched for Nearby, prefs write for Nearby, F14 note, comment -- R9
- [ ] harness: host-caps double `nearby`, probe double, `nearby_lobby.*`, `NearbyLobbySupport.h`, `NearbyLobbyTest.cpp`, `GamesHomeRowTest.cpp`, `ModePickerTest.cpp` updates -- verify list
- [ ] `./bin/clang-format-fix`, `python3 scripts/check_upstream_touches.py`, host suites, then the firmware checks of Verification once, after review

**Acceptance Criteria:**
- Given a host with nearby on in the host-caps double and a probe reading Wi-Fi up, when New game is tapped in Nearby, then the Wi-Fi dialog shows, no radio opens, and no lobby is pushed; likewise for low heap, and with both the Wi-Fi dialog shows.
- Given a passing probe, when New game is tapped over a pass save, then no question is asked, the lobby is pushed, `resume.bin` is byte-identical afterwards, and `prefs.bin` holds Nearby.
- Given the host lobby, when a guest joins, leaves, and another joins, then each screen of Design Notes 1 shows in order and Start is enabled only while the guest was heard within the live window.
- Given the guest lobby, when a host with another package hash is heard, then its row is greyed with "Different version" and takes no tap.
- Given a started host and guest lobby over `FakeLink`, when the host taps Start, then both lobbies are replaced by `GameMatchActivity` of the `nearby` fixture with the session moved in, and the match plays.
- Given Cancel, Back, Home, or a forced exit on any lobby screen, then a seated player gets ABORT(left), the link is ended exactly once, and the title screen (or Home) follows, with no `RenderLock` taken in `onExit`.
- Given any activity name in `src/activities/games`, then `isGamesActivity` is true for it and false for a name outside the list.
- Given nearby off (today's host), then the title screen's rows, Options, and New game behave as before.

## Implementation Notes

Implemented from this plan as written (build agent, host tests and fast checks only; the firmware checks of Verification are still to run after the review).

- **D3 accessor.** `ReliableLink::heardMs(PeerHandle) const` and `HostLobby::guestHeardMs(seat) const` (0 for no such guest), tested in `ReliableLinkTest.cpp` (`HeardMs*`) and `LobbyTest.cpp` (`GuestHeardMs*`).
- **Radio seam (D12).** `NearbyRadio` is a concrete class in `GameLobbyActivity.h`: its members are the device wiring (probe = `NearbyProbe::read()`, `{false, SIZE_MAX}` under `SIMULATOR`; `open()` returns no link until entry 11; `clock()` is a `static constexpr GameClock`, `random()` a function-local static `GameRandom`, so both outlive the lobby). A lobby owns a `NearbyRadio deviceRadio` member and uses `GameLobbyActivity::setRadio`'s radio when one is set; the title screen reads `GameLobbyActivity::probe()`. The only file-scope static is the override pointer; `random()`'s local static costs a guard and an exit handler (unmeasured; the budget gate measures it).
- **Stale touch table.** The loop task rebuilds the rows (under `RenderLock`) before the render task builds the table from them, so a tap could land on a row that is no longer at that index (a host appearing or expiring: Cancel's index becoming a host's, or the reverse). `rowSerial` / `builtSerial` make `activateIndex` drop a tap whose table is older than the rows; a test (`ATapOnRowsRebuiltSinceTheyWereDrawn...`) fails without it. A window of milliseconds between a build and its publish remains, as for every list screen.
- **F14 note.** `STR_GAMES_RESUME_NOT_HERE` is a sentence and the title screen's subtitle is one line with an ellipsis, so while the note shows the subtitle style allows 3 lines (`GameModeActivity::buildScreen`); the other rows are unchanged.
- **Doubles.** `list_stubs/HostCapsScript.h` gained `nearby`; `list_stubs/NearbyProbe{Script.h,Double.cpp}` script `NearbyProbe::read()`; `NearbyLobbySupport.h` has `FakeNearbyRadio` (a pin test in `NearbyLobbyTest.cpp`) and the multi-device rig.
- **Tests.** `NearbyLobbyHarnessTest` (`NearbyLobbyTest.*`, `NearbyTitleTest.*`), `GamesHomeRowHarnessTest`, `ModePickerHarnessTest` updated (`NewGameInARememberedNearbyPushesTheLobbyAndStartsNoSoloMatch`, `ANearbyOnlyGameContinueStartsNothingAndTheRowSaysWhy`, the host-caps double's `nearby` default).
- **Not done here:** `pio run` / `pio check` / `sim.sh build` and the flash budget measurement (after review, once, under the build lock); the lens review.

## Plan Change Log

## Review Triage Log

### 2026-10-13 — Review pass
All four lenses ran as context-free subagents (blind-hunter, edge-case-hunter, verification-gap, intent-alignment). The harness backgrounded the edge-case lens, so its report reached the orchestrator, which relayed it verbatim; its rows are triaged below as relayed. The patches were applied by this run's own context, the implementation subagent having already handed back (the workflow's fallback). Reviewed diff: `git diff 426acf57 -- src lib test docs` (the orchestrator's `_bmad-output` commits excluded).
- verdicts: 30 findings — high 0, medium 4, low 13, false 12, maybe-false 1 (five of the false rows are the intent lens's descriptive divergences, which the intent itself excludes or answers)
- findings:
  - `[low]` `patch` BH1 `showGuest(bool)` declared, never defined — dead declaration; removed from `GameLobbyActivity.h`.
  - `[low]` `patch` BH2 `GuestView::gone` copied and never read — removed the field and its copy. (Its other claim, that a silent host or guest reads as "left", is `false`: the Decision of 2026-10-12 (the lobby before Start) says a host that "cancels or goes silent" shows "The host left" and a guest that "cancels or goes silent" shows "Player 2 left".)
  - `[false]` `reject` BH3 `preventAutoSleep()` true on Choose — R8: "The lobby sets `preventAutoSleep` while open"; the test pins it.
  - `[medium]` `patch` BH4 Start fired on a liveness reading up to `POLL_MS` old — real (a guest quiet inside the window could still be started over): `startHostMatch` now re-reads `readHost(now)` and starts nothing when the guest is no longer live. Not staged by a test (the loop always polls first unless under 100 ms have passed, which a fake-clock test cannot cut that fine); the guard is two lines and the existing ghost tests cover the polled path.
  - `[low]` `patch` BH5 greyed different-version row reached `join()` on Confirm — `joinHostRow` returns for a `different` row; the test now asserts no "was not sent" line (the join call is never made).
  - `[low]` `reject` BH6 labels: UTF-8 cut and an empty label — a label is at most 16 bytes from `Protocol` and `copyLabel` copies up to 16, so no cut happens in the lobby; an empty label never comes from `EspNowLink::makeLabel`; a fallback string would cost a key for a case no radio produces.
  - `[low]` `defer` BH7 tests cover two-seat games, under 8 hosts, and no selection clamp on a shrinking list — unreachable in v1 (`maxSeats` 2, one host list); the selection-by-identity test added in this pass covers the shrink; a 3-seat lobby test waits for a 3-seat nearby game.
  - `[false]` `reject` BH8 tests bound by real milliseconds are flaky or vacuous — the 400 ms negative waits can only pass vacuously, never fail spuriously; 9,200 repeats plus 100 repeats of each new test after the patches (this pass) and 20 full `ctest` runs (build agent) passed.
  - `[low]` `patch` BH9 (first part) `goHome`'s mapping read as source text — the test now asserts the whole statement (`if (GamesHomeRow::isGamesActivity(activityName)) initialMenuItem = HomeMenuItem::GAMES;`), so a flipped test or another Home item fails it. The rest (the launcher, Options, and the match read the constants) is `false`: those headers are outside `touches`.
  - `[false]` `reject` BH10 `probe()` builds a throwaway radio, each lobby carries a `deviceRadio` — a stack object of one vtable pointer and a 4-byte `GameRandom`, and the member is 8 bytes; no sizeof budget exists for any activity here.
  - `[false]` `reject` BH11 `startNearby` repeats the settings check and writes `prefs.bin` before the lobby — the second is owner Decision D5 (written when the lobby opens); the repeated three-line check carries the guard `startMatch` has and keeps the New game row's notice in one place.
  - `[low]` `patch` BH12 (first part) `LIVE_WINDOW_MS` not tied to the link defaults — a `static_assert` ties it between three keepalives and the silence limit. The translated-format-string claim is `false`: `tr()` with `%u` is how every `STR_GAMES_*` count string is used in this fork.
  - `[low]` `patch` BH13 `heardMs` comment says 0 for "left" but a lost peer keeps its last reading — the header now says so.
  - `[maybe-false]` `defer` EC1 `HostState::Closed` or a dead link mid-lobby shows "Waiting" forever — maybe real only if a link can die without our own `cancel()`; `ILink` has no failure signal and `Closed` and `Cancelled` are set only by `cancel()`, after which the session is reset, so no path reaches it today. Settle with entry 11's device run (a radio that drops). Severity if true: medium (unverified).
  - `[low]` `defer` EC2 three-seat host: a leave overwritten to Joined by the resync — only with `seatLimit() > 2`; v1's `maxSeats` is 2.
  - `[medium]` `patch` EC3 `labelLength` from `open()` over the buffer made a span past `Opened::label` — real for a misbehaving radio; clamped before the span is built.
  - `[medium]` `patch` EC4 the selection stayed on its index when the host list changed, so a Confirm could join another host — real; the selection now follows the selected host by (handle, session), goes to the first host when the list was empty, else to Cancel; a new test (`TheSelectionFollowsTheHostItWasOnWhenAnEarlierHostLeavesTheList`) fails without it.
  - `[false]` `reject` EC5 `Refused` with `Refusal::None` shown as "full" — `GuestLobby` sets `Refused` only with `Full` or `VersionMismatch` (`GuestLobby.cpp` onAbort); `None` is unreachable.
  - `[low]` `reject` EC6 lobby allocation failure shows only a log line — the same as every other title-screen OOM here (the match and Options allocations); a notice would add a path for a 2.5 KB allocation that fails only when the reader is already out of memory.
  - `[false]` `reject` EC7 `showGuest` — duplicate of BH1.
  - `[low]` `reject` EC8 selection can land on a greyed row — the approved screens grey the row (Decision Q2a); the activate guard (BH5) makes Confirm on it do nothing; keeping greyed rows out of the selection ring would be a list-wide change.
  - `[medium]` `patch` VG1 the `goHome` change was checked only by text — see BH9: the test now asserts the whole statement; a runtime test of `goHome` needs `ActivityManager.cpp` on the host, which no harness builds. The suggested pure `homeItemFor` returning an enum is not taken: ledger row 5's text is "a pure header that `ActivityManager.cpp` calls", and the header is std-only by design.
  - `[low]` `defer` VG2 the per-seat loop in `readHost` is run only with one guest seat — as BH7: unreachable with v1's two seats; add a `nearbyManifest(3)` lobby test with the first 3-seat nearby game.
  - `[false]` `reject` VG3 `showGuest` — duplicate of BH1.
  - `[false]` `reject` IA1 the shipped build leaves the lobby unreachable and `NearbyRadio::open()` returns no link — by intent: "`NEARBY_BUILT` stays false; the harness turns nearby on", entries 16 and 11 wire the links; `TheFakeRadioHandsOutWhatTheDeviceRadioDoes` pins the stub.
  - `[false]` `reject` IA2 "title screen with nearby off unchanged" covered by the harness and the simulator build — `WithNearbyOff...StartsPassAsBefore` and `sim.sh build x4pro` (Verification) are the check.
  - `[low]` `reject` IA3 prefs written at the lobby push, not at Start — owner Decision D5; a refusal test that shows `prefs.bin` untouched after a refusal was not added: the write follows the probe in `startNearby`, and the cost is a new fixture state for a one-line order.
  - `[false]` `reject` IA4 "greyed and takes no tap" holds at `GuestLobby`, not at the row — see BH5, now at the row.
  - `[false]` `reject` IA5 Start replaces the lobby, checked against the manager double — the double is the harness's manager (`## 5.12` style); the real replace is the build's.
  - `[false]` `reject` IA6 "the match ends cleanly" untested here — entry 15's; the entry follows it.

## Design Notes

**Owner checkpoint (answered).** Decision (owner, 2026-10-12, entry 10's plan checkpoint): items 1 and 2 approved as written, D1, D2, D4 to D14 as recommended, D3 as its alternative (a) (rewritten below). Decision (owner, 2026-10-12, the flash budget at entry 10's checkpoint, option 1): the epic's share is now 36,912 B flash and 160 B static RAM (Note 4). Nothing here changes a Decision already recorded: Q2a, Q2b, and 2026-10-12 (the lobby before Start) are followed as written, and every row below that adds to them says so.

### 1. The screens

Common to all five: header = the game's name (`headerTitle` = `manifest.name`, as the title screen); footer hints Cancel / Select / Up / Down (`STR_CANCEL` in place of Back); the status block is drawn as text above the list by `buildScreen` (`screen.takeTop`, `screen.target().text` with `theme().titleText`, `bodyText`, `smallText`), and the list holds only things a player can act on, so the selection never lands on a status line. `UiListActivity` features used: `listCount()`, `buildScreen` with `syncListViewport` right before `screen.list`, `ACTION_ROW` with `inputMask = InputTouch`, `activateIndex`, `ListItem.label / subtitle / value / enabled` (enabled=false is the SDK's greyed, non-touchable row), `ListNav::requestSelection`, `handleCustomInput` (the poll), `onBackButton` (= Cancel), `drawFooter`, `headerTitle`, `drawChrome` default; no long press, no tabs, no custom touch.

```
CHOOSE (opens here)                       HOST (after "Host a game")
+------------------------------+          +------------------------------+
| Ultimate tic-tac-toe         |          | Ultimate tic-tac-toe         |
+------------------------------+          +------------------------------+
| (notice, only after a failed |          |         X4 Pro 3F2A          |  label, titleText, centred
|  start: "Nearby could not    |          |  Waiting for a player        |  body line = state
|  start. Try again.")         |          |  1 of 2 players              |  small line = seats taken
| > Host a game                |          | > Start            (greyed)  |  enabled only for a live guest
|   Join a game                |          |   Cancel                     |
+------------------------------+          +------------------------------+
| Cancel  Select   Up   Down   |          | Cancel  Select   Up   Down   |
```

Host body line over time: `Waiting for a player` (1 of 2) -> `Player 2 joined` (2 of 2, Start enabled and selected) -> on leave or silence `Player 2 left` with the small line `Waiting for a player` (1 of 2, Start greyed; a new join returns to `Player 2 joined`). Start greys again, with the line unchanged, if the guest is not heard for 3 s (D3), and re-enables if it is heard again.

```
GUEST, browsing                           GUEST, joined / ended
+------------------------------+          +------------------------------+
| Ultimate tic-tac-toe         |          | Ultimate tic-tac-toe         |
+------------------------------+          +------------------------------+
| (empty: "Looking for games   |          |  Waiting for the host to     |  joined (Joining or Joined)
|  nearby...")                 |          |  start                       |
| > X4 Pro 3F2A  1 of 2 players|          |  X4 Pro 3F2A                 |  small line = the host's label
|   X4 Pro 91C0  1 of 2 players|          | > Cancel                     |
|   X4 Pro 77AB  1 of 2 players|  greyed  |------------------------------|
|     Different version        |  row     |  The host left      (ended)  |  or "That game is full" or
|   Cancel                     |          | > Back to the list           |  "Different version"
+------------------------------+          +------------------------------+
```

Host rows: label = the host's label (up to 16 bytes, copied and NUL-terminated), value = `"%u of %u players"`; a different version also has the subtitle "Different version", `enabled = false`. Hosts are listed first-heard first (`GuestLobby::host(i)`); the list holds at most 8 hosts (`MAX_HOSTS`) and Cancel last. A tap maps through a snapshot of (handle, session) taken at the last redraw, never the live index; `JoinResult::UnknownHost` or `DifferentVersion` re-lists and joins nothing. The ended screen has one row; hardware Back still leaves the lobby (D6).

**Title-screen refusal dialogs** (built in `GameModeActivity`, a `fui::optionDialog` as `WifiSelectionActivity::buildPromptDialog` does, panel framed as `GameConfirmDialog` does): caption `STR_GAMES_MODE_NEARBY` ("Play nearby", small bold), message the refusal line (small, up to 3 lines), one button `STR_OK_BUTTON` (D4). The list is not built under it, as the New-over-a-save question does. Back, Confirm, or a tap on OK closes it. The Continue and Options rows behave as before afterwards.

### 2. New strings (all `english.yaml`, `STR_GAMES_*`, verbatim)

| Key | English |
|---|---|
| `STR_GAMES_NEARBY_WIFI_IN_USE` | `Wi-Fi is in use. Close it and try again.` (R8, Q2b) |
| `STR_GAMES_NEARBY_LOW_MEMORY` | `Not enough memory for Nearby. Restart the reader and try again.` (R8, Q2b) |
| `STR_GAMES_NEARBY_HOST` | `Host a game` |
| `STR_GAMES_NEARBY_JOIN` | `Join a game` |
| `STR_GAMES_NEARBY_WAITING_FOR_PLAYER` | `Waiting for a player` |
| `STR_GAMES_NEARBY_SEATS` | `%u of %u players` |
| `STR_GAMES_NEARBY_PLAYER_JOINED` | `Player %u joined` (seat 2 prints "Player 2 joined") |
| `STR_GAMES_NEARBY_PLAYER_LEFT` | `Player %u left` |
| `STR_GAMES_NEARBY_START` | `Start` |
| `STR_GAMES_NEARBY_SEARCHING` | `Looking for games nearby...` |
| `STR_GAMES_NEARBY_DIFFERENT_VERSION` | `Different version` |
| `STR_GAMES_NEARBY_WAITING_FOR_START` | `Waiting for the host to start` |
| `STR_GAMES_NEARBY_HOST_LEFT` | `The host left` |
| `STR_GAMES_NEARBY_BACK_TO_LIST` | `Back to the list` |
| `STR_GAMES_NEARBY_GAME_FULL` | `That game is full` |
| `STR_GAMES_NEARBY_COULD_NOT_START` | `Nearby could not start. Try again.` (not in a Decision: see D11) |

Reused, no new key: `STR_CANCEL`, `STR_OK_BUTTON`, `STR_SELECT`, `STR_DIR_UP`, `STR_DIR_DOWN`, `STR_GAMES_MODE_NEARBY` (the dialog caption), `STR_GAMES_RESUME_NOT_HERE` (F14's note, D9). `STR_GAMES_WAITING_FOR_HOST` stays the match's own line.

### 3. Choices among readings (recommendation first; the alternative after it)

- **D1, both refusals.** `NearbyProbe::Verdict` carries both and picks none. Recommend Wi-Fi first: the Wi-Fi radio and web server are the usual cause of low heap, closing it is the cheaper step, and the memory message ("Restart the reader") is the heavier one and is shown when Wi-Fi is off and heap is still low. Alternative: memory first (a restart also clears Wi-Fi, but forces a restart when closing Wi-Fi would do). A third reading, one dialog naming both, costs one more string and message layout; not recommended.
- **D2, host-list refresh on e-ink (the entry's unknown).** Recommend: the loop task polls the lobby at most every 100 ms and redraws only when what is drawn changed (a host appeared, expired, changed seats or verdict; a state change), and at most once per `LIST_REFRESH_MIN_MS` = 1,000 ms for list changes (arrivals in between coalesce into the next redraw); a state change a player waits on (joined, left, host left, full, started) and any touch or button redraw at once; no timer-driven redraw. A stable set of hosts costs no refresh (ADVERTs are every 500 ms and expire at 3 s, so changes are events, not a stream). 1,000 ms is an estimate; the panel's partial-refresh time is unmeasured and entry 11's device run settles it. Alternatives: a fixed 2 s redraw (refreshes with nothing changed), redraw on every change (a burst of hosts flickers), or manual refresh only (hosts stay stale).
- **D3, Start only while the guest is live (decided: owner Decision 2026-10-12, entry 10's plan checkpoint, alternative (a)).** `HostLobby` gains an exact per-guest heard-time accessor, `uint64_t guestHeardMs(uint8_t seat) const` (0 for no such guest): the `IClock` reading at which the seated guest's link peer last sent anything valid (any frame, its 1,000 ms `PING` / `ACK` keepalive included, and its `JOIN`), so it starts at the JOIN. The screen enables Start while a guest is seated and `clock.nowMs() - guestHeardMs(2) < LIVE_WINDOW_MS` = 3,000 ms (three keepalives, inside the 10 s silence limit, which still ends a true ghost with "Player 2 left"); it greys Start again, with the line unchanged, when the guest is not heard for that long, and re-enables it when heard again. Unlike the frame-counter inference this plan first recommended, a third device's refused JOIN cannot keep a ghost live. **Finding, settled by the Scope note (orchestrator, 2026-10-12, entry 10's stop):** `HostLobby` sees only the frames `ReliableLink` hands its sink (`JOIN`, `STATE`, `ABORT`); `PING` and `ACK` are never handed to it (`ReliableLink.h`, Sink). The heard time is `ReliableLink::Peer::lastHeardMs` (private; `ReliableLink.cpp` lines 71, 136, 416, 430, 472), so an exact accessor needs a read-only `ReliableLink::heardMs(PeerHandle) const` (about 5 lines, no behaviour change) that `HostLobby::guestHeardMs` calls through `link_`. `ReliableLink.{h,cpp}` and `ReliableLinkTest.cpp` are in the entry's `touches` for exactly this read-only accessor (no behaviour change; tested: a peer never registered returns 0; registered at JOIN; advanced by a `PING`, an `ACK`, a `STATE`; not advanced by a frame of another session or by a lost peer's frames) and the `HostLobby` tests go in `test/game_core/LobbyTest.cpp` (0 before a guest; the JOIN time; advances with keepalives; a seat that left returns 0; per-seat for n > 2). Alternative if the owner prefers not to touch `ReliableLink`: `HostLobby` alone cannot know a keepalive, so the only remaining readings are the frame-counter inference (recommended in the first draft) or offering Start whenever 2 seats are filled.
- **D4, the refusal dialog's buttons.** Recommend one OK button (a notice, not a question), built in `GameModeActivity` (its `fui::OptionDialogProps` is a member, about 700 B, as `GameConfirmDialog` keeps its own). `GameConfirmDialog.h` is outside `touches` and always has Cancel plus an action. Alternative: reuse `GameConfirmDialog` with Cancel and OK (two buttons that do the same).
- **D5, when `prefs.bin` learns Nearby.** Recommend at the New game tap once the probe passes and the lobby is allocated (the same point `startMatch` writes for any mode, same guards), so "the last started mode" is the last mode picked and started. Alternative: at Start, which needs the choices passed into the lobby (more code) and forgets a Nearby pick whose lobby was cancelled.
- **D6, hardware Back on the guest's ended screens.** Recommend Back leaves the lobby as it does everywhere (Cancel and Back are one action, R8); the row "Back to the list" is the way to the list. Alternative: Back returns to the list there only.
- **D7, seats on a host row.** Recommend the seats in the value column and, for a different version, also the subtitle "Different version" (R8 names both on the greyed row). Alternative: the seats as the subtitle, replaced by "Different version" on a greyed row.
- **D8, status block outside the list.** Recommend text above the list (nothing selectable that is not actionable). Alternative: status as disabled list rows (the selection can land on them).
- **D9, F14's note.** Recommend reusing `STR_GAMES_RESUME_NOT_HERE` ("This saved game cannot be continued on this device. It is kept until you start a new game.") as the Continue row's line, as `showSettingsNotice` does for New game; no new key. Alternative: a shorter new key.
- **D10, "Player 2" text.** Recommend `Player %u` with the guest's seat (2 in v1), so a third seat later needs no string. Alternative: literal "Player 2" strings.
- **D11, a radio that will not start.** Not in any Decision, but a null from `openHost` / `openGuest` or `open()` must show something, or the tap does nothing (F14's own complaint). Recommend one notice line (16th string). Alternative: reuse the low-memory text (wrong for a radio fault) or only log.
- **D12, the radio seam.** The lobby needs a link, clock, random, label, probe reading, and timing from the radio layer; entries 11 and 16 fill them. A link-time double as `gameHostCaps()` uses needs the device definition in a separate `.cpp`, and a new file under `src/activities/games/` or `src/games/` is outside `touches` (a HALT). Recommend `class NearbyRadio` (virtual: `probe()`, `open()` returning the link and label bytes, `clock()`, `random()`, `lobbySettings()`, `sessionOptions()`) declared in `GameLobbyActivity.h`, a default device implementation inside `GameLobbyActivity.cpp` that entries 11 and 16 edit ("the link and label wiring only", "choosing the link under SIMULATOR only"), and `GameLobbyActivity::setRadio(NearbyRadio*)` for the harness. Today's device default: `probe()` = `NearbyProbe::read()` (under `SIMULATOR`, `{false, SIZE_MAX}` because `NearbyProbe.cpp` is device only), `open()` returns no link, `clock()` / `random()` = constant-initialized `GameClock` / `GameRandom` (stateless, so they outlive the lobby that hands the session to the match). Static RAM: one pointer, 4 B. Alternative: none inside `touches`.
- **D13, name-mapping drift.** Recommend the pure header plus a test that pins each activity's `NAME` (or `name` for the match) to it and scans `src/activities/games/*Activity.{h,cpp}` for names not in the list (`## 5.12` was exactly a name missed). Alternative: the NAME checks only.
- **D14, Cancel rows.** Recommend a Cancel row last on the host and guest screens (touch has a visible target; Back and the swipe still work). Alternative: none (saves about 150 B, unmeasured).

### 4. Flash and static RAM (estimate, unmeasured as a build)

Basis: the symbol and object sizes of the x4pro build at `5c617c04` (measured sizes; the sums are mine). Linked today: `GameModeActivity` 5,074 B of code, `GameOptionsActivity` 1,755 B, `GameConfirmDialog` 1,923 B. Not linked today, so reachable once the lobby calls them: `NearbySession::openHost` 389 B and `openGuest` 363 B, `makeUniqueNoThrow<Core>` 221 B, `HostLobby` ctor 333 B and `start` 219 B, `GuestLobby` ctor 187 B, `join` 286 B and `cancel` 138 B, `ReliableLink` ctor 209 B, `GameLink::start` 150 B, plus `HostLobby::cancel`, `startHost`, `lobby()`, `versionVerdict` (about 500 B); the rest of the lobby and the session is already in the +16,016 B of entry 15's measurement (`HostLobby::onJoin`, `GuestLobby::onAdvert`, `ReliableLink` and `Protocol::encode` are linked through the sinks' vtables).

| Part | Estimate (B) |
|---|---|
| `GameLobbyActivity` code (five screens, poll, rows, status block, teardown, start) | 4,500 to 6,000 |
| newly reachable link and lobby code (above) | 2,900 to 3,300 |
| `GameModeActivity` (probe, dialog, nearby start, F14, prefs helper) | 600 to 1,000 |
| radio default, `GamesHomeRow` + `goHome` call | about 100 |
| **Total** | **about 8,100 to 10,400, central 9,000** |

Static RAM: +4 B (the radio pointer), +0 B otherwise (no static objects with constructors; `objects` checks). The 16 strings add about 70 B of offsets plus their text each, in both the games-on and games-off builds, so they should net out of the games-on-minus-off difference (unmeasured).

**It did not fit the share as it stood (answered: the share is now 36,912 B / 160 B, owner Decision 2026-10-12, option 1; the build measures like for like and HALTs with these cuts as options if the projected total, its measurement plus entry 5's driver +11,456 B / +80 B plus a few hundred bytes for entry 11, exceeds it).** About 2,512 B remain for entries 10 and 11 together (epic Notes, Measurement 2026-10-12, entry 15); this entry alone is estimated 5.6 to 7.9 KB over that, before entry 11's wiring (a few hundred bytes). It does fit the gate: the base is +258,048 B, the projection with entry 5's driver +27,472 B, the 290 KiB gate 296,960 B, so 11,440 B of headroom, of which 2,000 B is the reserve; this entry would leave about 1.0 to 3.3 KB of it before entry 11. The share (29,984 B) is a budget, not the gate.

Proposed then, and now moot for (1): (1) the owner raised the share by the 6,928 B unallocated (not the 8,000 B proposed); (2) the cuts that stay inside the Decisions save about 1.5 to 2 KB at most (unmeasured) and would not close the gap alone: the refusal as a note on the New game row instead of a dialog (about 0.7 to 1.0 KB; departs from Q2b's `fui::optionDialog`), no large label band (about 0.2 KB; departs from "large"), no Cancel rows (D14, about 0.15 KB), no throttle and no liveness gate (D2, D3, about 0.3 KB; reopens the ghost-seat Assumption). The orchestrator's checkpoint after entry 10 will measure like for like (entry 15's method) and report against whichever number the owner sets; this plan measures its own cost the same way before the commit.

### 5. How it works

- **Opening.** `startNew` for Nearby, after the unread-settings guard: `radio().probe()` then `NearbyProbe::decide`; refused sets the dialog; otherwise `rememberModeIfChanged()`, then `startActivityForResult(GameLobbyActivity(...), onLobbyClosed)`. A push, so Back lands on the title screen as it was; Start's `replaceActivity` clears the stack (title screen and launcher), as every match start does, and Leave goes to Games.
- **Opening a session** (loop task, may wait): Host tapped -> `radio.open()`, `NearbySession::openHost(link, clock, random, lobbyGameFor(manifest, pkgHash), label, lobbySettings, sessionOptions)`; Join -> `openGuest`. A null at either leaves the Choose screen with the notice. `lobbyGameFor` copies id, package hash (`GamePkg::HASH_BYTES` = `Protocol::PKG_HASH_BYTES`, `static_assert`), `manifest.api`, seats, `API_SURFACE_CRC`, `!API_LEVEL_FROZEN`, and `gameHostCaps()`.
- **Poll** (`handleCustomInput`, returns false; the host also reads `guestHeardMs(2)` for D3): at most every 100 ms read `lobby()` into a small value struct (host: `poll(HostEvent)`, `seatsTaken`, `canStart`; guest: `state`, `hostCount`, entries, `refusal`, `gone`) under the session's lock, compare with what is drawn, and when it differs (D2) take `RenderLock`, copy, rebuild the fixed row arrays (at most 8 hosts + Cancel; label and subtitle `char` arrays), `requestUpdate`.
- **Start.** Host: `startHost()`; `Started` -> allocate `GameMatchActivity(renderer, mappedInput, manifest, session->roster(), Start::New, settings, std::move(session))`, `app.clearTapFlash()`, `replaceActivity`. Guest: `GuestState::Started` -> `beginMatch()`, the same. An allocation failure sends `leave(Left, 0)`, resets the session, and returns to Choose with the notice. `NotEnoughSeats`, `NotWaiting`, `LinkFailed` change nothing (events follow). The settings the match gets are the title screen's chosen values, copied into the lobby.
- **Teardown** (Cancel row, Back, `onExit`; idempotent): host `cancel()` / guest `cancel()` under `lobby()`, then `session.reset()` (joins the task, ends the link: radio off, R7). `onExit` runs with the render mutex held and never takes `RenderLock`; the `GameLink` task never takes it either. `preventAutoSleep()` is true for the lobby's whole life.
- **Name mapping.** `GameLobbyActivity::NAME` and `GameModeActivity::NAME` read the header's constants; the other three names are pinned by tests (their headers are outside `touches`).

### 6. Test doubles (Brief, AI-4)

- `FakeNearbyRadio` stands in for the device radio (`EspNowLink` plus its label, `GameClock` / `GameRandom`, `NearbyProbe::read`, and later `UdpLink`). It wraps a `FakeLink` end of a shared `FakeLinkNet` in `ObservedLink` and hands out `FakeRtosClock` and a `FixedRandom`. A test pins that: the label it makes is `EspNow::makeLabel("X4 Pro", mac)`'s form and at most `MAX_HOST_LABEL_BYTES`; `open()` returns an independent link each call; the device default's `open()` returns no link (flipped on purpose by entry 11). More permissive than the device: `open()` never fails unless a test scripts it, takes no time, and the probe answers instantly; the clock runs faster than real time while tasks sleep (`NearbySessionSupport.h`). A `FakeLink` loses only what a test scripts.
- The `NearbyProbe::read()` double returns scripted readings; `ModePickerTest`'s `HostCapsDouble` test pins that `nearby` defaults to `HostCapsValues::NEARBY_BUILT` as `pass` does. The screen doubles record raw `text()` calls and lay nothing out on `\n`; the tests read the recorded lines and therefore keep status lines to one string each.
- Timing: the lobby suites use fake milliseconds (`fastSettings`, `waitFor`); the 3 s live window and 1 s refresh bound are fake-time values, labelled estimates until a device run. A flake proof (20 full `ctest` runs and `--repeat until-fail:200` on each new test) is part of Verification.

### 7. Intent sources (each choice's line)

R8 and R9 (epic file); Decisions Q2a (host label large, Start once joined, greyed different version, a late JOIN gets "That game is full"), Q2b (probe before the lobby, dialog on the title screen, `GameLobbyActivity` on `UiListActivity`), Q4 (fallbacks, F12, F14 land in entry 10), 2026-10-12 "the lobby before Start" ("Player 2 left", "The host left" with Back to the list), 2026-10-12 deferred scope ("entry 10 takes the `goHome` remedy with `GameOptions`"), the ghost-seat Assumption (D3), the entry-9 Scope note, the flash Measurement and Decision lines; `deferred-work.md` `## 4.9` (nearby solo fallback), `## 4.13`, `## 5.10`, `## 5.12`, `## sync-e54c0087` (`goHome`); the retrospective's F12 and F14.

### 8. Not settled elsewhere and not a choice that changes a Decision

Where `GameLobbyActivity` is pushed (push, from R8's "returns to the title screen"); the 100 ms poll; 8 hosts (`GuestLobby::MAX_HOSTS`); header = game name; none changes an outcome the Decisions fix.

### 9. Guards of code this entry moves or rewrites (`git log -L`)

- `goHome` (`ActivityManager.cpp`, last changed by 0bf1c2d5, eced576b, 60b59a4b, 80b68a92, 97dcf52f): the `initialMenuItem == NONE && currentActivity` guard (an explicit item wins) and the upstream chain stay; the games block stays last and inside `#if FREEINK_CAP_GAMES`; only the comparison changes, and a name outside the list leaves `initialMenuItem` as the upstream chain set it. eced576b's `PluginCatalog` clause is upstream's and untouched.
- `rosterFor` (38b67c6d, 17d02483): `row == null` -> false; the roster starts solo; Pass's `seats == 0` -> false (no seat count fits); the Nearby arm becomes `false` (no roster without a lobby). Callers: `onEnter` asks only Pass; `startNew` asks Solo and Pass after the Nearby branch; `startResume` searches modes in table order and skips what returns false, so a Nearby-only game finds none and shows F14's note.
- `startNew` (60b59a4b, 17d02483): the `!row` guard (no mode) and the `!rosterFor` guard (log and repaint) stay for Solo and Pass; the per-mode `LOG_INF` stays, the Nearby text changes.
- `startMatch`: the unread-settings guard (`!resume && settings.count != manifest.settingsCount`) must run before the lobby opens, or a host would start a match without the `ctx.settings` its manifest declares; the OOM guard and the prefs rule (`prefsState != Unreadable`, `(saved.mode & ~modes) == 0`) move unchanged into `rememberModeIfChanged()`, which `startMatch` and the Nearby path both call.

## Verification

**Commands** (builds under `flock /tmp/crosshatch-build.lock`, host-test configure and build under `flock /tmp/crosshatch-hosttest.lock`; `export PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`):
- `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` -- expected: all pass, including the new `NearbyLobbyHarnessTest` (each screen's rows and transitions; a host and a guest lobby starting the `nearby` fixture's match; a guest leaving, a host leaving, and a ghost guest before Start; both refusals and the pair, with the 102,399 / 102,400 B boundary, leaving the lobby unopened; Cancel, Back, Home, and a forced exit tearing down once; refresh bounded as D2; radio failure; a Nearby start leaving a pass `resume.bin` untouched and Nearby in `prefs.bin`; F14's note), `GamesHomeRowTest` (the five names, a name outside the list, the directory scan), and the updated `ModePickerHarnessTest`.
- Flake proof for the new suites: 20 full `ctest` runs and `ctest --repeat until-fail:200` on each new test.
- Fork script tests (`python3 scripts/<name>_test.py` for each script present), `python3 scripts/check_upstream_touches.py` (row 5's new text), `python3 scripts/check_layers.py`, `./bin/clang-format-fix` twice with a clean `git status`.
- Once, after review: `pio run -e x4pro`, `pio run -e default`, `pio check -e x4pro` and `pio check -e default` with the three `--fail-on-defect` flags, `.claude/skills/run-crosshatch-player/sim.sh build x4pro` (title screen with nearby off unchanged), and `python3 scripts/check_flash_budget.py build on`, `build off`, `compare`, `objects` on the commit, the same method and tree as entry 15's measurement, reported as a delta over `+258,048 B / +784 B` and against whichever share the owner sets.

**Manual checks:**
- `sim.sh` shots of the title screen with nearby off (unchanged) go to `story-lobby-screenshots/` if the simulator can show a lobby (it cannot before entry 16; the harness covers the lobby's drawn text).

## Auto Run Result

Status: **blocked** (blocking condition: the epic's flash budget; the run stops for the owner as the Decision of 2026-10-12 (entry 10's checkpoint, option 1) says). The implementation is complete and verified; it is committed on the lane so it is not lost, and is **not** to be merged until the owner answers.

**Blocking figures (measured).** `scripts/check_flash_budget.py` `build on`, `build off`, `compare`, `objects` (all exit 0), x4pro, one incremental tree under the build lock with the shared cache, on the tree this commit holds (`src`, `lib`, `test`, `docs` unchanged since the build; the plan and the screenshots are the only later files): `firmware.bin` 6,244,176 B games on, 5,957,328 B off, **+286,848 B flash**; static internal RAM 190,248 B on, 189,464 B off, **+784 B**. Over the epic base (+258,048 B / +784 B): **+28,800 B flash, +0 B static RAM**. Over entry 15's like-for-like measurement (+274,064 B reachable): this entry adds **+12,784 B**, against the plan's estimate of 8,100 to 10,400 B. By linked-symbol size (ELF, before and after): `GameLobbyActivity` 6,351 B, `GameModeActivity` +1,308, the link side newly reachable (`NearbySession` +1,378, `HostLobby` +1,231, `GuestLobby` +836, `ReliableLink` +638, `GameLink` +408) 4,491, FreeInkUI's `optionDialog` and its props (the refusal dialog) +1,277, `NearbyRadio` 145, `GamesHomeRow` 95.

Projection (the Decision's rule: this measurement + entry 5's driver +11,456 B / +80 B + a few hundred bytes for entry 11's wiring): flash 28,800 + 11,456 = **40,256 B before entry 11's wiring (about 40.5 KB with it) against the share of 36,912 B: over by at least 3,344 B**. Static RAM 0 + 80 = 80 B against 160 B: fits. Against the 290 KiB gate (296,960 B) the same projection is 286,848 + 11,456 = **298,304 B: over the gate itself by 1,344 B** (the driver's cost is entry 5's measured figure, not re-measured here).

**Options for the owner** (savings are linked-symbol sizes, unmeasured as builds):
1. Raise the gate and the share. The gate needs at least +1,344 B (to 300 KiB = 307,200 B gives 8,896 B of headroom after the projection); the share needs at least +3,344 B (to about 41,000 B flash with entry 11's wiring). The gate lives in `scripts/check_flash_budget.py` and `crosshatch-ci.yml` (entry 17's files; outside this entry). Nothing in this entry changes.
2. Cut inside the Decisions, which together save about 2.0 to 2.4 KB and so do not close the 3.3 KB gap alone: the refusal as a note on the New game row instead of the `fui::optionDialog` (up to about 1.3 KB; departs from Decision Q2b), no large label band (about 0.2 KB; departs from "large"), no Cancel rows (D14, about 0.15 KB), no list throttle (D2, about 0.3 KB), the guest's Joined and Ended screens as one (about 0.3 KB). With these and nothing else the projection is still about 2 KB over the share.
3. Both: raise the gate to 300 KiB and the share by about 1.5 KB, and take the cheaper cuts (the dialog as a note).

Recommendation: option 1 (the gate to 300 KiB, the share to about 41,000 B), because the cuts that fit the Decisions do not reach the share, and each departs from an approved screen.

**Summary of the change.** `GameLobbyActivity` (five screens on `UiListActivity`, the radio seam `NearbyRadio`, polling at 100 ms, redraw only on change at most once a second for the host list, selection that follows the host it was on, Start gated by `HostLobby::guestHeardMs` within 3 s, teardown on Cancel, Back, Home, and sleep), `GamesHomeRow.h` (the five names) and `goHome`'s one call, `GameModeActivity` (probe, one-button refusal dialog, lobby push, no solo fallbacks, no question over a save, F12 and F14), 16 `STR_GAMES_NEARBY_*` strings, ledger row 5, and, by the owner's Decision and the orchestrator's Scope note, `HostLobby::guestHeardMs` and `ReliableLink::heardMs` (read-only, tested).

**Files.** `lib/GameCore/{HostLobby,ReliableLink}.{h,cpp}` (the accessors), `lib/I18n/translations/english.yaml`, `src/activities/ActivityManager.cpp` and `docs/crosshatch/upstream-touches.md` (row 5), `src/activities/games/{GameLobbyActivity.{h,cpp},GamesHomeRow.h,GameModeActivity.{h,cpp}}`, `test/game_core/{LobbyTest,ReliableLinkTest}.cpp`, `test/game_script/harness/` (`NearbyLobbyTest.cpp`, `NearbyLobbySupport.h`, `GamesHomeRowTest.cpp`, `ModePickerTest.cpp`, `nearby_lobby.{cmake,sources.cmake}`, `list_stubs/{HostCapsScript.h,GameHostCapsDouble.cpp,NearbyProbeDouble.cpp,NearbyProbeScript.h}`), and the screenshots below.

**Review.** Four lenses, 30 findings (Review Triage Log): patches applied 9 (BH1, BH2, BH4, BH5, BH9, BH12, BH13, EC3, EC4 and VG1 sharing a fix with BH9: 4 medium, 6 low), deferred 4 (3 in the frontmatter), rejected the rest with their reasons in the log. One pre-review finding fixed by the orchestrator's relay: `NearbyRadio::random()`'s function-local static (AD-2) is now a member. `followup_review_recommended: true`: two or more medium findings were patched (the stale-liveness Start, the label-span overread, the selection identity), and the patches were applied by this run's own context and checked by one new test and mutants only; the unverified risk is the stale-liveness re-read in `startHostMatch`, which no test stages.

**Verification (all on the tree this commit holds).** Host suites: 2,503 tests, all pass (2 skipped by design: the screenshot dumps), including `NearbyLobbyHarnessTest`, `GamesHomeRowHarnessTest`, `ModePickerHarnessTest`, and the new `LobbyTest` / `ReliableLinkTest` accessor tests; flake proof after the patches: each `NearbyLobbyTest`, `NearbyTitleTest`, and `GamesHomeRowTest` test `--repeat until-fail:100`, all pass (the build agent's earlier 20 full runs and 9,200 repeats predate the patches). `python3 scripts/check_upstream_touches.py` PASS, `scripts/check_layers.py` passed, fork script tests passed, `./bin/clang-format-fix` twice clean. Firmware under the build lock: `check_flash_budget.py` `build on` (x4pro) and `build off` succeeded, `compare` and `objects` exit 0 (no static initializer; largest mutable static 4 B); `pio run -e default` succeeded; `pio check -e x4pro` and `-e default` with the three `--fail-on-defect` flags exit 0; `sim.sh build x4pro` succeeded. Not run: the simulator cannot reach the lobby (`NEARBY_BUILT` is false and the real `open()` returns no link until entries 16 and 11), so no simulator screenshot of a lobby exists.

**Screenshots** (`story-lobby-screenshots/`; harness wireframes from the screen double's recorded text positions, not panel pixels: white on black marks the selected row; the dump tests `DumpsEachLobbyScreenForReviewWhenAsked` and `DumpsTheRefusalDialogsForReviewWhenAsked` write them when `LOBBY_SHOTS_DIR` is set):
- `title-nearby-picked.png` the title screen with Nearby picked.
- `lobby-1-choose.png` Host a game / Join a game.
- `lobby-2-host-waiting.png` the host's label, "Waiting for a player", "1 of 2 players", Start, Cancel.
- `lobby-3-host-player-2-joined.png` "Player 2 joined", Start selected.
- `lobby-4-host-player-2-left.png` "Player 2 left", waiting again.
- `lobby-5-guest-browse-different-version.png` the host list with a "Different version" host.
- `lobby-6-guest-waiting-for-host.png` "Waiting for the host to start".
- `lobby-7-guest-host-left.png` "The host left" and Back to the list.
- `lobby-8-guest-game-full.png` "That game is full".
- `refusal-wifi-in-use.png` the title-screen dialog, "Wi-Fi is in use. Close it and try again." with OK.

**Residual risks.** The budget overage above; the device build compiled but never ran (`static constexpr GameClock` in `NearbyRadio::clock()` produced no static initializer, per `objects`); D2's 1 s and D3's 3 s are fake-time estimates until entry 14's device run; the wireframes show positions, not the large label or the greyed state (the ink colour of a greyed row is not recorded by the double); `FakeNearbyRadio` is more permissive than a radio (Design Notes 6). No formatting-only change outside this entry's paths.
