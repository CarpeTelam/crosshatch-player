---
title: 'Build the approved title-screen, Options, and hand-off design'
type: 'feature'
ticket: '12'
created: '2026-10-02'
status: 'built'
baseline_revision: '0ebe76a4661cfb9ced2bc757f7815d55dbef4140'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/docs/contributing/touch-and-ui.md'
  - '{project-root}/docs/crosshatch/game-canvas.md'
  - '{project-root}/_bmad-output/planning-artifacts/ux-designs/ux-crosshatch-player-2026-10-01/DESIGN.md'
  - '{project-root}/_bmad-output/planning-artifacts/ux-designs/ux-crosshatch-player-2026-10-01/EXPERIENCE.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** The owner approved entry 3's design on 2026-10-02 (epic Notes Decision): 14 changes against the built title screen, launcher row, hidden hand-off, forced exit and gap pause menu, plus an Options screen, remembered choices, two manifest keys, two reserved images and `ctx.settings`. None is built.

**Approach:** Build the 14 rows of entry 3's plan "Changes for a follow-up story" as `DESIGN.md`, `EXPERIENCE.md`, `mockups/` and the amended AD-8, AD-12, AD-15, AD-17, AD-22 say, where those and R4, R5, R6, R9, R10, R14 differ, the 2026-10-02 Decision settles it. Keep every cross-story guard (seat gate, frame-tagged and back-dated touches, Unstartable, the blank before every SD step). Measure flash and stop for the owner if over the bar.

## Boundaries & Constraints

**Always:** files in the entry's `touches` only, plus two orchestrator-approved out-of-touches edits (decision, orchestrator, 2026-10-02, option A): `lib/GameCore/GameImages.{h,cpp}` gains a shared `isReservedImage` check so `imageNameOf` rejects `title` and `handoff` as it rejects `icon` (with a `GameImagesTest` case), and `src/games/GameAssets.cpp` skips the two reserved names without the "is not loaded" log. Fork code under `FREEINK_CAP_GAMES`. `makeUniqueNoThrow` or PSRAM buffers, null-checked; locals under 256 B. All SD access through `Storage`/`HalFile`, loop task only, never in `render()` or `onExit()` (prefs.bin included). Every string through `tr()`, keys appended to `english.yaml`; the `STR_GAMES_MODE_*_DESC` keys removed. Screens on `UiListActivity`/`UiAppHost`, never `rowTouch`/`wasTapInRect`. The new manifest keys, image names, limits and `ctx settings` are level-1 preview entries; `API_SURFACE_CRC` follows the list. Each test double added or extended names the device behaviour it stands in for (retro AI-4).

**Never:** `API_LEVEL_FROZEN`, CI workflows, the epic file, the spine's ADs, `freeink-sdk`, `.skills/`, any upstream file but `english.yaml`. No script drawing during Result or HandOff. No `RenderLock` taken in `onExit()` or a destructor. No trimming the owner's design to fit flash.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|---|---|---|---|
| Title, save + title.png | Valid save; `title.bmp` 480x480 | Splash band shows it centred; rows Continue ("Load the previous game", selected), New game ("Solo · Hard"), Options | none |
| Title, no save, no title.png | `icon.bmp` or library icon | Icon at 128 px centred in the band; New game selected | unreadable `title.bmp`: icon, logged |
| Options hidden | one startable mode, no settings | Rows Continue/New game only | none |
| Options cycle | tap Mode row | value moves to next, wraps; Back: title screen, Options row selected, New game line updated, prefs.bin written | write fails: logged, choice kept until title closes |
| New over save | save present, New game | Confirm: "Start a new game?" / current mode name / "This replaces the saved game.", Cancel focused | none |
| prefs.bin stale | mode no longer startable, unknown setting/value | each falls back to the manifest default, value by value | not an error (DBG log) |
| Manifest bad | `default_mode` not in `modes`; >4 settings; dup id/value; 1 or 7 values; default not in values; unknown key inside a setting | parse fails, package Invalid | installer `.bad`, pack_game refuses |
| Reserved image too big | `title.png` 481 wide, `handoff.png` 480x801 | Invalid at install and in pack_game | BadImage, logged |
| ch.gfx.image("title") | package ships title.png | unknown image, as any missing name | ScriptError as today |
| Hand-off default | hidden pass, next seat 2 | white screen, game icon 128 px centred at y=200, "Player 2's turn" under it, "I'm ready" button centred at y=400; full refresh | none |
| Hand-off with handoff.png | `handoff.bmp` loaded | image centred, clipped; only the button over it | unreadable: default screen, logged |
| Tap off a button | tap outside banner / outside "I'm ready" | nothing | none |
| Forced exit, hidden | sleep in any state | plain white HALF push after the VM wait, before the first SD step | as today |
| Gap pause | Paused before the next round's first frame | "Starting the next round" centred under "Paused" | none |

</frozen-after-approval>

## Code Map

- `src/activities/games/GameModeActivity.{h,cpp}` -- title screen. `MODE_TEXTS` (drop `description`), `buildRows` (Continue / New game / Options), `buildScreen` (splash band then list), `activateIndex`, `rosterFor`/`startNew`/`startResume` (keep: Continue's fallback roster = first startable mode in solo, pass, nearby order, cross-story row 3; Unstartable asks before New), `buildConfirmDialog` (second line = current mode's name). Keep `NAME`, the confirm guards (`onRowAction` drop while open, release/acquire on `confirmRow`).
- `src/activities/games/GamesLauncherActivity.cpp` `provideRow` :415 -- add the modes line for `check.ok()` games; buffer written on the render task like `libraryIcon`.
- `src/activities/games/GameMatchActivity.{h,cpp}` -- `renderHandOff` :785, `buildHandOffView` :922 (whole-screen `TapZone` goes), `pushBlank` :389, `buildView` :851 (gap message align), `handle` :277 (stores before `shown`), `loopHandOff` :586 (keep the `passScreenShown` gate), `onEnter` (load hand-off art for a hidden pass roster after `seedResume`). `ACTION_PASS` now only on the banner and the button.
- `src/activities/games/GameConfirmDialog.h` -- reuse as is.
- `src/games/FrameReplay.cpp` `drawBlank` :191 -- plain white (clear + `forceFull`), no icon.
- `src/games/GameViewIcons.h` :17-27 -- `forView(HandOff)` returns null.
- `src/games/GameVM.{h,cpp}` -- `create` gains the settings; hidden branch :149 and `beginAgain` :176 publish the round's first turn seat (`nextSeat`) and count it; `passedTo()` reused.
- `src/games/GameSaveStore.{h,cpp}` -- static `loadPrefs`/`savePrefs` beside `peek`; `.tmp` then rename as `saveResume` does; path `GAMES_DATA_DIR/<id>/prefs.bin` (fits `DATA_PATH_BYTES` 64).
- `src/games/GamePackageInstaller.cpp` `convertImage` :612 -- size check for `title.png` (480x480) and `handoff.png` (480x800) from `readPngSize`, before converting; `BadImage`, logged. Budget counting already includes them (non-icon).
- `src/games/GameRowIcon.h` -- reuse `choose`, `readPackageIcon` (64x64 Mask1) for the 128 px icon (each pixel 2x2); `drawGameIcon(…, 128, …)` for library icons. `src/games/GameImageBlit.h` `runs` + `GameCore::checkImageHeader` to load and draw a reserved `.bmp`. No change to either.
- `src/games/GameRegistry.h` `readGame(dirName, reader, entry)` -- the title screen re-reads the installed manifest to get its settings from the reader. No change.
- `lib/GameCore/Manifest.{h,cpp}` -- `ManifestError::BadDefaultMode`, `BadSettings`; `Manifest::defaultMode` (Mode bit, 0 absent), `settingsCount`; `ManifestSettings` (≤4 × {id[17], name[25], values[6][17], count, defaultIndex}) held by `ManifestReader`, read with `settings()` after `finish`; `ManifestReader::Key::DefaultMode`, `Settings`; `MANIFEST_KEYS` + `default_mode`, `settings`, `settings.id`, `settings.name`, `settings.values`, `settings.default` (a third field or a generalized sub-key; ApiLevelTest checks both ways); pure `Manifest::startMode(remembered, hostModes)`; `SettingValues` (≤4 × {id, value}) for the VM. `default_mode` validated in `finish` (key order free).
- `lib/GameCore/ApiLevel.h` -- `API_SURFACE_CRC`.
- `lib/GameScript/LuaGame.{h,cpp}` `setupEntry` :424 -- `setSettings(const SettingValues&)` before start; `ctx.settings` table (empty when none), every call.
- `lib/I18n/translations/english.yaml` :563-581 -- delete `STR_GAMES_MODE_{SOLO,PASS,NEARBY}_DESC`; `STR_GAMES_CONTINUE_DESC` = "Load the previous game"; append `STR_GAMES_OPTIONS` "Options", `STR_GAMES_MODE` "Mode", `STR_GAMES_PLAYER_TURN` "Player %u's turn", `STR_GAMES_READY` "I'm ready".
- `docs/crosshatch/api-level-1.txt` -- `ctx settings table`; `manifest default_mode string?`, `manifest settings array?`, `settings.id string`, `settings.name string`, `settings.values array`, `settings.default string?`; limits `settings_count 4`, `setting_values_count 6`, `setting_id_bytes 16`, `setting_name_bytes 24`, `setting_value_bytes 16`, `title_image_{width,height}_pixels 480/480`, `handoff_image_{width,height}_pixels 480/800`; `name setting_id [a-z][a-z0-9_]{0,15}`; `name image (?!(icon|title|handoff)$)[a-z0-9_]{1,32}`; comments for the min 2 values, reserved images, fall-backs.
- `docs/crosshatch/formats.md`, `game-canvas.md`; `_bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/game-api-seed.md` ctx and manifest sections.
- `scripts/pack_game.py` (`KNOWN_KEYS` :78, `read_manifest` :164, image checks) and `pack_game_test.py`; `scripts/pack_device_run.py` `GAMES` :49 and its test.
- Tests: `test/game_core/{ManifestTest,ManifestCheckTest,ApiLevelTest,GameImagesTest}.cpp`, `ApiLevelList.h`; `test/game_script/{LuaGameTest,GameSaveStoreTest,GameViewIconsTest,ApiSurfaceTest}.cpp`; `test/game_script/harness/{ModePickerTest,GamesLauncherTest,GameMatchTest,ResumeMatchTest,GamePackageInstallerTest,FrameReplayTest,PackedFixturesTest}.cpp`, `mode_picker.sources.cmake` (new sources), doubles in `HarnessSupport.h`/`MatchSupport.h`/`LauncherSupport.h`.
- Orchestrator-approved out-of-touches (2026-10-02, option A): `lib/GameCore/GameImages.{h,cpp}` `imageNameOf` :102 (shared `isReservedImage`), `src/games/GameAssets.cpp` :141 (skip reserved names silently).

## Tasks & Acceptance

**Execution:**
- [ ] `lib/GameCore/Manifest.{h,cpp}`, `ApiLevel.h`, `docs/crosshatch/api-level-1.txt`, `test/game_core/*` -- manifest keys, settings, `startMode`, `SettingValues`, MANIFEST_KEYS, list entries, CRC; tests per matrix rows "Manifest bad" (each rule, both sides of each limit, key order) -- AD-15 amendment.
- [ ] `lib/GameCore/GameImages.{h,cpp}`, `src/games/GameAssets.cpp`, `GameImagesTest`, `GameAssetsLoadTest` -- reserved-image exclusion (option A) -- AD-15 "never by ch.gfx.image".
- [ ] `src/games/GamePackageInstaller.cpp`, `scripts/pack_game.py`, `pack_game_test.py`, installer harness test -- reserved image size checks; pack_game mirrors every manifest rule above.
- [ ] `src/games/GameSaveStore.{h,cpp}`, `GameSaveStoreTest`, `formats.md` -- prefs.bin (Design Notes): round trip, missing, malformed, stale value by value, failed write, kept on remove.
- [ ] `lib/GameScript/LuaGame.*`, `src/games/GameVM.*`, `LuaGameTest`/`GameVmTest` -- `ctx.settings` present and equal across Play again; empty table without settings; the turn-seat announcement at each hidden round begin (new, Play again, resume).
- [ ] `src/activities/games/GamePicture.{h,cpp}` (new) -- load a reserved `.bmp` (≤ given size, `checkImageHeader`, PSRAM) and draw it centred and clipped; load and draw the game icon at 128 px.
- [ ] `src/activities/games/GameOptionsActivity.{h,cpp}` (new), `GameModeActivity.*`, `english.yaml`, `ModePickerTest`, cmake -- rows 1-6 (Design Notes).
- [ ] `src/activities/games/GamesLauncherActivity.cpp`, `GamesLauncherTest` -- row 9.
- [ ] `GameMatchActivity.*`, `FrameReplay.cpp`, `GameViewIcons.h`, `GameMatchTest`/`ResumeMatchTest`/`FrameReplayTest`/`GameViewIconsTest`, `game-canvas.md` -- rows 10-13; existing hidden-pass tests updated to the new screen, not loosened.
- [ ] `test/game_script/fixtures/pass-art/` (new: hidden pass + solo, `default_mode` pass, two settings, `title.png` 480x480, `handoff.png` 480x800, `main.lua` drawing `ctx.settings`), `fixtures/README.md`, `pack_device_run.py` + test -- row 14; device-run steps in Verification for the orchestrator.
- [ ] `game-api-seed.md` ctx and manifest sections; `deferred-work.md` `## 5.12` if anything is deferred.

**Acceptance Criteria:**
- Given a save and a remembered pass mode with settings, when the title screen opens, then rows are Continue (selected), New game "Pass and play · Hard · …" (ellipsis when long), Options, and the 3-tap counts of AD-22 hold.
- Given Continue, when the match resumes, then the save's roster plays (not the current mode) and the unreadable path ends in the error view with `resume.bin` untouched.
- Given the hidden pass match, when any hand-off shows, then no script call draws and no seat frame is pushed until that seat's frame is ready; the seat gate, frame tags and the blank before every SD step are unchanged.
- Given the flash-budget four steps on the commit, then the delta over `c1902721` (+233,440 B, +784 B) is within +244,592 B and +816 B, or the run stops for the owner.

## Implementation Notes

- Phases (orchestrator-approved sizing, 2026-10-02): implementation runs as three context-free subagents in turn, each finishing its phase with host tests green. Phase 1: Tasks 1-5 (manifest, API list and CRC, reserved images, installer and pack_game, prefs.bin, `ctx.settings` and the VM's turn announcement). Phase 2: Tasks 6-8 (GamePicture, title screen, Options, launcher, strings). Phase 3: Tasks 9-11 (match screens, FrameReplay, GameViewIcons, fixtures, device-run packing, docs, game-api-seed, deferred-work). Each phase appends its notes here.
- Phase 1 (Tasks 1-5, 2026-10-02): built and host-tested; no firmware build, `pio check`, or simulator build run (they follow the review). What later phases call:
  - `GameCore::Manifest::defaultMode` (Mode bit, 0 absent), `settingsCount`, `startMode(remembered, hostModes)`; `ManifestReader::settings()` (a `ManifestSettings`, valid after a `finish` that returned `None`), `ManifestSettings::indexOf` / `valuesAt(chosen)`, `ManifestSetting::indexOf`; `SettingValues` (in `Manifest.h`; `LuaGame.h` now includes `Manifest.h` for it).
  - `GameSaveStore::loadPrefs(gameId, Prefs&)` -> `PrefsState {None, Loaded, Malformed, Unreadable}`, `savePrefs(gameId, prefs)`, `resolvePrefs(saved, manifest, settings, hostModes)` -> `Choices {mode, valueIndex[4]}`, `prefsOf(choices, settings)`. `Prefs::mode` is the raw remembered byte, for "a mode other than the one the file holds".
  - `GameVM::create(..., handOff, const SettingValues& settings = {})`; `GameVM::turnAnnouncements()` counts each hidden round begun (new, resumed, Play again) after storing its first turn seat in `passedTo()`; turn changes still count in `turnsPassed()`. `GameMatchActivity` still calls `create` without settings (Phase 3 passes them).
  - `GameCore::isReservedImage(stem, length)` (case-insensitive `icon`, `title`, `handoff`), `TITLE_IMAGE_{WIDTH,HEIGHT}` 480/480 and `HANDOFF_IMAGE_{WIDTH,HEIGHT}` 480/800 in `GameImages.h`.
  - `API_SURFACE_CRC` 0xB3C1FB30.
- Phase 1 reading of the Design Notes: prefs.bin's mode byte is the `Manifest::Mode` bit (1 solo, 2 pass, 4 nearby, 0 none), not resume.bin's byte, whose 0 is solo and so cannot also mean "none" as the Design Notes ask; a byte that is not one bit, or not startable, falls back like any stale value. Recorded in `formats.md`.
- Phase 1 notes: `default_mode` and an unknown or mis-typed field inside a setting fail as `BadDefaultMode` / `BadSettings`; a known key repeated inside one setting is `DuplicateKey`, as at the top level; `"settings": []` is valid (no settings). As with `modes`, a string over the JSON parser's 511-byte token buffer inside `values` is dropped by `StreamingJsonParser` without a callback, so the device would read that setting with one value fewer where `pack_game.py` refuses the value (any over 16 bytes); a known field's dropped value still fails (`Syntax`). Reserved-page sizes are checked from the PNG header before converting (`it may be at most WxH`, `BadImage`). `loadPrefs` reads the fields straight into the caller's `Prefs`; `savePrefs` encodes into a 143-byte heap buffer (its frame already holds three 64-byte paths).
- Phase 2 (Tasks 6-8, 2026-10-02): built and host-tested (host suites, `scripts/*_test.py`, `check_layers.py`, `check_upstream_touches.py`, `clang-format-fix` twice); no firmware build, `pio check`, or simulator build run. What Phase 3 calls:
  - `GamePicture` (`src/activities/games/GamePicture.{h,cpp}`): `loadPage(gameId, "handoff", HANDOFF_IMAGE_WIDTH, HANDOFF_IMAGE_HEIGHT)` (loop task; false and silent when absent, false and logged "... drawing the icon instead" when unusable, PSRAM), `loadIcon(manifest)` (the manifest must outlive the draws), `hasPage()`, `drawPage(renderer, left, top, w, h)` (ink only, centred, clipped to the rect), `drawIcon(renderer, centreX, centreY)` (128 px). It builds in the shared harness libraries and in `game_match_src` (no CMake change needed for the match).
  - `GameMatchActivity`'s constructor takes `const GameCore::SettingValues& settings = {}` (copied into a member) and passes it to `GameVM::create`; the title screen passes `settings.valuesAt(choices.valueIndex)` for New game and Continue. Phase 3 need not add it.
  - `GameModeActivity::modeName(bit)`, `writeModesLine(modes, out, size)`, `nextMode(current, modes)`, `JOINER` (" · ") are public statics; `english.yaml` already has `STR_GAMES_PLAYER_TURN` and `STR_GAMES_READY`.
  - Harness: `screen_stubs/ActivityManager.h` gained `popForResult(parent, child)` (the real Pop of a screen pushed for a result), and `RecordingTarget::newest()` falls back to the newest live target when the newest one is destroyed (the screen under a popped one draws again).
- Phase 2 readings: the confirmation is drawn without the splash band behind it (as built, "no visual change"); `drawChrome` reads `confirmRow` once per render into `renderedConfirmRow` so the band and the dialog never disagree. The title screen reads the manifest's settings with `GameRegistry::readGame` only when `settingsCount > 0`, and uses none (logged) when the re-read fails or its count or package hash differs from the launcher's entry. "Startable" for Options' visibility and the Mode row is `CheckResult::modes`; New game in pass with no seat count still fails at the tap, logged, as before. Options is named `GameOptions`, which `ActivityManager::goHome` does not map to Home's Games row (upstream file, ledger row 5): a Home gesture on Options lands on Home's default row.
- Orchestrating agent, before Phase 3: 1-bit art for the `pass-art` fixture is ready to copy, `title.png` 480x480 and `handoff.png` 480x800, from `/tmp/claude-0/-home-user-crosshatch-player/3c29fff2-79eb-5b74-aad8-2397ce34acb1/scratchpad/5.12/png/` (made by `gen.py` there). The Home gesture from Options lands on Home's default row (`ActivityManager::goHome` maps only `GameMode`/`GamesLauncher`; upstream, ledger row 5, outside `touches`): Phase 3 records it under `## 5.12` in `deferred-work.md`.
- Phase 3 (Tasks 9-11, 2026-10-02): built and host-tested (all host suites, `scripts/*_test.py`, `check_layers.py`, `check_upstream_touches.py`, `clang-format-fix` twice); no firmware build, `pio check`, or simulator build run (they follow the review).
  - Match: `renderHandOff` pushes nothing until `GameVM::turnAnnouncements()` reaches `announcementAwaited` (1, then Play again's count + 1, stored in `handle()` before `shown` and before `playAgain()`); a held-back render sets `handOffHeldBack`, and `loopHandOff` asks for the render once the count arrives. It then draws `drawBlank`, `handoff.bmp` (`GamePicture::drawPage` over the whole logical screen) or the icon (middle at height / 4), then `renderUi` (`buildHandOffView`: "Player N's turn" through `screen.target().text` under the icon when there is no page, and the "I'm ready" `fui::button`, `ACTION_PASS`, touch only, 4/5 of the safe width, `dialogProps.buttonHeight` tall, middle at height / 2), then `displayBuffer` (FULL, FAST over `Panel::Blank`), then `passScreenShown`. N is `vm->passedTo()`, read once per render into `handOffSeat`. Result's banner is the same `fui::button` with `ACTION_PASS`; the whole-screen `TapZone` is gone. `onEnter` loads the hand-off art (`loadIcon`, `loadPage(id, "handoff", 480, 800)`) for a hidden pass roster after `seedResume`, before `GameVM::create`. The gap line is centred (`messageText.align`). The guards of the Design Notes are unchanged (`pushForcedExitBlank` between the stop and `flushResume`, `onExit`'s no-VM blank, Leave's blank before `goToGames`, `closeRouting` when `shown` moved).
  - `FrameReplay::drawBlank(renderer)` lost its viewport: a cleared screen and `forceFull`, nothing else. `pushBlank` logs "<id>: <when>: blank screen pushed (half refresh)" (was "blank hand-off screen pushed"; tests and `game-canvas.md` follow). `GameViewIcons::forView(HandOff)` is null.
  - Tests: `HiddenPassTest`'s helpers (`tapReady` at (240, 400), `tapBanner` at (240, 740), `tapToPass` by state, `renderHandOff`, `expectHandOff(seat)`, `expectOneBlankPush` now plain white) and new cases (the default screen's fills and layout, the two buttons' places, a tap off either, the wait for the announcement at a new match and at Play again, `handoff.bmp` drawn, an unusable one falling back logged, the pass-art fixture's settings, the gap line centred); `PassResumeTest.passTheBlank(seat)` and `TitleScreenTest.PassHiddenFromItsManifest...` follow the new screen; `FrameReplayTest` and `GameViewIconsTest` pin the plain blank and the null icon; `PackedFixturesTest` packs `pass-art` (reserved pages, `default_mode`, settings) and `InstallerTest.TheFixtureGamesTheReadmeLists...` installs it.
  - Fixture `test/game_script/fixtures/pass-art/` (the orchestrator's PNGs; library icon `spade` fill), `fixtures/README.md` (its row, `pass-hidden`'s row rewritten for the new screens, and "Title screen, Options, and hand-off run", the device-run steps), `pack_device_run.py` packs it (ten files; its test follows).
  - Docs: `game-canvas.md` (Resume, Leaving, the forced exit's step 2, Hidden pass states, The hand-off screen, Taps, the views table), the game-api-seed companion's section 1 (`title.png`, `handoff.png`, `default_mode`, `settings`, the image total) and section 2's `ctx` row; `deferred-work.md` `## 5.12` (the Home gesture from Options, the companion's section 3 wording, the hidden-pass test names).
  - Reading: the hand-off screen's page and icon are drawn on the logical screen (480 x 800), not the canvas viewport, as DESIGN.md places them by screen coordinates; the button's height is the option dialog's row (`OptionDialogProps::buttonHeight`, unscaled by the match), which the device's measured 52 px may differ from: the simulator screenshots check it.

## Plan Change Log

## Review Triage Log

**Pass 1 (2026-10-02).** The four lenses (blind hunter BH, edge-case hunter EC, verification gap VG, intent alignment IA) ran as context-free subagents in one message over `git diff 0ebe76a4..` (the worktree, plan excluded; 434,983 B) and all four returned. Verdicts: high 0, medium 1, low 10, false 1; reject 5 of the low (rule: unlikely in use and more than a direct fix, or by design). No intent_gap or bad_plan. Rows 2-8 are patches, sent to the Phase 2 implementer, which also fixed the three "Games list" wordings left at a solo start; row 1 is deferred (below).

| # | Lens | Finding | Verdict | Route | Evidence / action |
|---|---|---|---|---|---|
| 1 | BH1, EC2, VG-o1 | Options changes are lost when Options is left by a Replace (Home gesture, sleep): the result handler runs only on Pop | medium | defer | `ActivityManager.cpp` :162-197: Replace runs `exitActivity` then each parent's `onExit()`, never the handler. The patch round wrote prefs.bin from the title screen's `onExit()`; that breaks the frozen Always line and AD-17 ("never from the match or `onExit()`"), so it was taken out again: the loss is pinned by `OptionsLeftByAReplaceWritesNothing`, the `openOptions` comment says so, and `deferred-work.md` `## 5.12` takes it to the owner at entry 11. |
| 2 | EC1 | `nextMode(0 or unknown)` starts the search at pass, not solo | low | patch | `(COUNT + 1) % COUNT == 1`. `at = COUNT - 1`; test. |
| 3 | EC4, BH2, VG-o2 | Settings not re-read (OOM, card changed) leave `settings` empty while the manifest declares some; the next write wipes every remembered value | low | patch | `prefsOf(choices, settings)` with count 0. Skip `rememberChoices` while `settings.count != manifest.settingsCount` (logged); test the mismatch path. The empty `ctx.settings` in that case stays (logged); rare, documented in the log line. |
| 4 | EC6, BH3 | An Unreadable prefs.bin (card fault) is overwritten with defaults on the first New game | low | patch | `saved.mode == 0` after Unreadable. Keep the load state; New game does not write after Unreadable (an Options change still does); test. |
| 5 | EC5, BH5 | "Player 0's turn" for a round already over at begin | low | patch | `announceTurnSeat` stores `status().turn` = 0. No line for seat 0; test. |
| 6 | VG1, BH6, IA-3b | After Continue, Play again runs setup with the title screen's current choices, while the docs say "Play again passes the same table"; untested | low | patch | resume.bin records no settings (by design, AD-8: a resume calls no setup). Document it in `api-level-1.txt` (comment only, no CRC change) and `game-api-seed.md`; add a GameVmTest of `setResume` then `playAgain` seeing the given settings. |
| 7 | VG2 | Nothing checks the band is hidden under the New-over-save question | low | patch | Add the pixel assertion with a `title.bmp` and the question open. |
| 8 | BH7 | The held-back hand-off's comments name "the Games list" where the title screen stays; a Back/Resume during the wait keeps the pause menu until the seat arrives | low | patch (comment) | Transient (one setup call); comment and `game-canvas.md` wording corrected, behaviour kept. |
| 9 | EC3 | Home from Options lands on Home's default row | low | reject | Already in `deferred-work.md` `## 5.12` (upstream `ActivityManager.cpp`, ledger row 5). |
| 10 | EC7 | An installed game using `title`/`handoff` as its own ch.gfx.image names now fails | low | reject | By design: AD-15 (amended) reserves the names, level 1 is a preview, and the list's `name image` entry changes the CRC preview devices match on. |
| 11 | BH4 | SD reads in the title screen's `onEnter` run under `RenderLock` | low | reject | Nothing can render before the rows exist anyway; the cost is the read's own time; a staging restructure is more than a direct fix. |
| 12 | BH8 | The "I'm ready" button is 44 px tall (OptionDialogProps' default), DESIGN says 52 | low | reject | It is the dialog option height the built option rows use (the source DESIGN measured); the simulator screenshot is the check. |
| 13 | BH9 | A setting value over the JSON parser's 511-byte token is dropped on the device, which pack_game refuses | low | reject | Same as the existing `modes` behaviour; only a hand-made package reaches it; the fix is in the parser. |
| 14 | BH10 | The 2-value minimum has no `limit` line tying C++ and Python | low | reject | Pinned by behaviour cases on both sides; adding a list entry changes the CRC for no user harm. |
| 15 | IA-3a | `startMode` falls back remembered → default → first startable, not "the first one it can" | false | reject | AD-17 (amended) and EXPERIENCE's Remembered choices row both say a stale remembered mode falls back to the manifest's default; the solo/pass/nearby order is recorded in Design Notes. |
| 16 | IA-3c, 3e | No screenshots and no flash measurement in the diff | -- | -- | Not findings on the code: both run in this step's verification (below). |

## Design Notes

- **Current mode** (`Manifest::startMode`): remembered if this host can start it, else `default_mode` if it can, else the first startable in solo, pass, nearby order (so no default and solo listed gives solo; pass-only gives pass). The bits carry no list order, so "first listed" is read as that order; recorded here.
- **prefs.bin** v1: `"CHPF"`, version u8 = 1, mode u8 (the `Manifest::Mode` bit: 1 solo, 2 pass, 4 nearby; 0 none — resume.bin's byte uses 0 for solo, so it is not reused), count u8, then per setting id-len u8, id, value-len u8, value. At most 143 B, read into a stack-free member buffer. Stored by id and value strings so a manifest change resolves value by value. Written by the title screen (loop task) after Options returns with a change, and when New game starts a mode other than the one the file holds (a missing file holds none). Never from the match or `onExit()`.
- **Options**: pushed with `startActivityForResult`; it edits the title screen's `Choices` (current mode bit, chosen value index per setting) through a pointer the parent outlives, sets `changed`, and `finish()`es on Back; the handler (cancelled or not) writes prefs.bin when changed, rebuilds rows and selects Options. No new `ActivityResult` type (upstream variant untouched). Rows: Mode first only with >1 startable mode, then settings in manifest order; label = name, subtitle = current value; tap/Confirm cycles with wrap.
- **Settings to the match**: the title screen builds `SettingValues` from the manifest's settings and its choices and passes them to `GameMatchActivity` (copied) → `GameVM::create` → `LuaGame::setSettings`. Continue passes the current choices too: a resume calls no `setup` (AD-8), so they reach only a Play again after it.
- **"Player N's turn"**: N is `vm->passedTo()`. The VM stores `nextSeat` at each hidden round begin (after setup or restore) and counts it (`turnAnnouncements()`); the match awaits count 1 at Started and count+1 read before `playAgain()` at Play again (as `roundsStartedAwaited`). `renderHandOff` pushes nothing until the awaited announcement (the prior screen stays; `passScreenShown` stays unset, so nothing passes), and `loopHandOff` requests a render when the count moves. Result → HandOff needs no wait (`nextSeat` is stored before `turnsPassed` counts).
- **Hand-off render**: `drawBlank` (white, `forceFull`), then `handoff.bmp` centred and clipped, or the icon (centre y = 200) and the text under it; then `renderUi` (the "I'm ready" `fui::button` with `ACTION_PASS`, 4/5 width, centre y = 400), then `displayBuffer` FULL (FAST when `panel == Blank`), then `passScreenShown`. The routing table is published before the push, as Result's already is; the `passScreenShown` gate drops a routed tap until the push returns. Result: the banner gets `action = ACTION_PASS`; no full-screen zone.
- **Guards kept** (git log -L on `pushBlank`, `stopVm`, `renderHandOff`, `buildHandOffView`): `pushForcedExitBlank` stays between the stop wait and `flushResume` (AD-12); `onExit`'s no-VM blank when `panel == Seat` (row 1); Leave's blank before `goToGames`; FAST repaint when the blank is already on the panel (row 6); routing closed when `shown` moved during render.
- **Memory**: `title.bmp` ≤ 28,862 B and `handoff.bmp` ≤ 48,062 B, PSRAM, loaded on the loop task (title screen `onEnter`; match `onEnter` for a hidden pass roster after `seedResume`); icon bits 512 B member. No SD read in `render()`.

## Verification

**Commands:**
- `cd /home/user/wt-e12 && flock /tmp/crosshatch-hosttest.lock sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test' && ctest --test-dir build/test --output-on-failure -j` -- expected: all pass, a test per change row.
- `for t in scripts/*_test.py; do python3 $t || echo FAIL $t; done`; `python3 scripts/check_layers.py`; `python3 scripts/check_upstream_touches.py` -- expected: pass.
- `./bin/clang-format-fix` twice -- expected: nothing new.
- Under `flock /tmp/crosshatch-build.lock` with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`: `pio run -e x4pro`, `pio run -e default`, `pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high` and with `-e x4pro`, `sim.sh build x4pro` -- expected: pass.
- `python3 scripts/check_flash_budget.py` `build on`, `build off`, `compare --limit-kib 250 --ram-limit-bytes 1024`, `objects` -- expected: delta over `c1902721` within 11,152 B flash and 32 B static RAM, else stop.

**Manual checks:**
- Simulator screenshots against the mocks into `story-design-build-screenshots/`: title with and without a save, with `title.png` and the icon fallback, Options, launcher modes line, hand-off default and `handoff.png`, Result, gap pause menu.
- Device run (entry 11's packet, for the orchestrator): `python3 scripts/pack_device_run.py <dir>` now packs `pass-art.chgame` too; the steps are `test/game_script/fixtures/README.md`, "Title screen, Options, and hand-off run" (launcher modes lines; `title.png` and the icon fallback; Options cycling and remembered choices; `handoff.png` with only "I'm ready"; the banner and the button apart and each the only target; the plain white push before each of the eight Sleep Screen modes, Transparent and Quick Resume included; Play again keeping the settings; `pass-hidden`'s default hand-off screen; the gap line centred; New over a save in the current mode), each once by touch and once with the front buttons alone.

**Results (2026-10-02, on the final tree of this story's commit, after the review patches; firmware sources unchanged since):**
- Host suites: 1,601/1,601 pass (ctest, Ninja `build/test`, under the host-test lock; 1,531 before this story).
- Every `scripts/*_test.py` passes; `check_layers.py` (531 edges in 113 game files) and `check_upstream_touches.py` pass (`english.yaml`, ledger row 2, is the only upstream file changed).
- `./bin/clang-format-fix` twice: the second run changed nothing; it changed no file outside this story's paths.
- `pio run -e x4pro` and `-e default`: SUCCESS. `pio check` (default) and `pio check -e x4pro` at low, medium and high: PASSED. `sim.sh build x4pro`: SUCCESS.
- **Flash: over this epic's share; the run stops for the owner.** `check_flash_budget.py`'s four steps under the build lock: x4pro `firmware.bin` 5,931,504 B games on, 5,679,840 B off, **+251,664 B** (the 256,000 B gate passes, 4,336 B to spare); static internal RAM +784 B (240 B to spare); `objects`: 45 game objects, no static initializer, largest mutable static 4 B. Over the base at `c1902721` (+233,440 B, +784 B): **+18,224 B flash, +0 B static RAM**, against the share of 11,152 B and 32 B: **7,072 B over** in flash. The epic's pre-PR measurement at `38a8b75d` was +241,712 B, so this story adds about +9,952 B (two differences, each measured the same way on its own tree). An intermediate measurement after Phase 1 (`pio run -e x4pro` only, games on, 5,923,760 B) put the manifest settings, prefs.bin, reserved images, `ctx.settings` and the turn announcement at about +2,256 B of it; the two screens, the picture loader and the match screens are the rest (an estimate: the off build was not repeated then). The design was not trimmed (orchestrator's instruction).
- Screenshots (simulator x4pro, Lyra, looked at against the mocks), in `story-design-build-screenshots/`:
  - `launcher-modes-line.png`: one row per game, modes lines "Solo", "Solo · Pass and play", "Pass and play".
  - `title-title-png-no-save.png`: `pass-art`'s `title.png` in the 480 px band; New game "Pass and play · Hard · Small" (its `default_mode` and defaults), Options.
  - `title-title-png-with-save.png`: the same with a save: Continue "Load the previous game" selected, New game "Pass and play · Hard · Medium" (remembered), Options.
  - `title-icon-fallback-no-save.png`: `pass-hidden`, no `title.png`: its library icon at 128 px centred in the band; New game "Pass and play"; no Options (one mode, no settings).
  - `options.png`: Options headed "Options": Mode / Pass and play, Level / Hard, Board / Small.
  - `title-after-options-selected.png`: back from Options after Board was cycled: New game line updated, Options row selected.
  - `new-over-save-current-mode.png`: "Start a new game?" / "Pass and play" / "This replaces the saved game.", Cancel focused.
  - `handoff-default.png`: `pass-hidden`'s hand-off: icon at 128 px, "Player 1's turn", "I'm ready" in the middle.
  - `handoff-handoff-png.png`: `pass-art`'s `handoff.png` with only the "I'm ready" button over it.
  - `result-banner.png`: seat 1's frame (showing `ctx.settings`: Level Hard, Board Medium) with "Tap to pass to player 2"; a tap off the banner left the screen byte-identical (checked by `cmp`).
  - `gap-pause-centred.png`: `slow-restart`'s Play-again gap: "Starting the next round" centred under "Paused".
- Seen in the screenshots, for the owner: the "I'm ready" button and the banner have DESIGN's 2 px ink frame (measured on the PNGs), and the button is 44 px tall, the dialog option height, where DESIGN's `option-row` token says 52 px (review row 12).
