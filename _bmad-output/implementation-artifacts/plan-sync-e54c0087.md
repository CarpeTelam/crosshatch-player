---
title: 'Sync upstream develop at e54c0087'
type: 'chore'
ticket: ''
created: '2026-10-10'
status: 'built'
route: 'full'
route_source: 'auto'
baseline_revision: '39061f4416dc01a8e1df393752252f2ed76144a3'
review: 'thorough'
review_source: 'auto'
lenses_ran: [blind-hunter, edge-case-hunter, verification-gap, intent-alignment]
review_loop_iteration: 0
followup_review_recommended: false
deferred:
  - summary: 'A browser plugin (or a device.json path) can write anywhere on the card except the three credential stores, so it can replace installed game files that the Lua VM runs and the saves, bypassing GamePackageInstaller''s checks. File Transfer''s own file manager can do the same, so this is a plugin-specific widening, not a new capability.'
    location: 'src/network/ProtectedPaths.h (isPluginPath, SENSITIVE_FILES); callers in src/network/CrossPointWebServer.cpp (/api/plugin-fs l.2351, download dest l.2131, l.2424)'
    severity: medium
    options: '(a) add /.games, /.games-data, /.games-tmp to the protected set: edits an upstream file the ledger does not list, so a new ledger row and spine update first; (b) propose upstream a protected-prefix hook the fork fills from src/games; (c) accept: plugins are user-installed SD code, as trusted as the file manager. Recommendation: (b), with (c) until it lands.'
  - summary: 'SDK StreamingJsonParser (moved from lib/JsonParser to the SDK''s JsonSax) now has TOKEN_BUF_SIZE 2048, was 512, so a ManifestReader (heap-allocated everywhere) is about 1.5 KB larger. Stale comments and copies: GameModeActivity.cpp ManifestRead comment ("about 1.4 KB"), pack_game.py / pack_game_test.py and game_codec_test.py (C_READER_TOKEN_LIMIT = 512) and docs/crosshatch/formats.md l.505 state the 511/512-byte token limit, which the packer now enforces more tightly than the device reads.'
    location: 'src/activities/games/GameModeActivity.cpp l.72; scripts/pack_game.py; scripts/pack_game_test.py l.981, l.1038; scripts/game_codec_test.py l.27; docs/crosshatch/formats.md l.505'
    severity: low
  - summary: 'UiAppHost::route now fires haptic_feedback::touchAction on any routed release or swipe event, so game screens built on UiAppHost (launcher, mode picker, match view, dialogs) vibrate in play when the user turns haptics on (default off). Whether a match should vibrate on every touch is a product choice.'
    location: 'src/components/UiAppHost.cpp route(); src/activities/games/GameMatchActivity.cpp, GameMatchView.cpp'
    severity: low
  - summary: 'Checked, no defect: ButtonNavigator/UiListActivity press stepping reaches only GamesLauncherActivity (fixed); the games'' other screens read wasPressed/wasReleased directly. MappedInputManager::wasScreenTapped is false on a HeaderBackTapTarget tap, but the same tap is Button::Back through wasBackGesture (wasPressed/wasReleased(Back)), which the launcher''s note popup already dismisses on; the target is cleared on exit and push. FrontlightPanelActivity has no games code. PluginLocations roots (/.crosspoint/plugins, /plugins, /.plugins) do not meet the games folders (/.games, /.games-data, /.games-tmp, /games). test/protected_paths and test/plugin_manifest_classification pass.'
    location: 'src/activities/games/GamesLauncherActivity.cpp; src/MappedInputManager.cpp l.186, l.373, l.382; src/util/PluginLocations.h'
    severity: info
  - summary: 'No host test runs the real ActivityManager::goHome, so the GamesLauncher/GameMode/GameMatch to GAMES mapping (ledger row 5) and upstream''s PluginCatalog to OPDS_BROWSER mapping are unpinned: deleting either clause fails no test, and Back would land on the Files tab. HomeTabsTest reopens Home with an item directly.'
    location: 'src/activities/ActivityManager.cpp goHome (l.~343-361); test/game_script/harness/screen_stubs/ActivityManager.h'
    severity: low
  - summary: 'GameConfirmDialog::readButtons (remove-game dialog) moves its focus on the release of Up/Down/Left/Right/NavPrevious/NavNext, while lists (launcher, mode picker, options) now step on press and the match pause menu already stepped on press. A two-button dialog, so a product choice, not a defect.'
    location: 'src/activities/games/GameConfirmDialog.h l.30-38'
    severity: low
  - summary: 'In list mode Plugins (upstream, installed plugin) and Games (fork) both draw the Blocks icon (listIconFor maps UIIcon::Plugins to icon_blocks), so the two rows look alike when a plugin is installed; the cover grid shows Blocks for the library slot and the controller for Games, so it is unaffected. Choosing another icon for Games is a product choice (row 7 of the ledger says list mode reuses an existing UIIcon).'
    location: 'src/activities/home/HomeActivity.cpp render() (menuIcons.insert(end() - 1, Blocks)); src/components/UiAppHelpers.h listIconFor'
    severity: low
  - summary: 'The fork release dry run (crosshatch-release.yml, "dry run" on, from the Actions tab on the pushed merge branch) is owed before this merge counts as done: upstream changed OtaUpdater.cpp, HttpDownloader.*, ReleaseJsonParser.h, FirmwareBoardTag.cpp, the release workflows, [base] build_flags and the freeink-sdk pointer (upstream-touches.md, row 10). Not runnable here: it needs a push.'
    location: '.github/workflows/crosshatch-release.yml; docs/crosshatch/upstream-touches.md row 10'
    severity: medium
context:
  - '{project-root}/AGENTS.md'
  - '{project-root}/docs/crosshatch/upstream-touches.md'
  - '{project-root}/docs/crosshatch/orchestrated-epics.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** The fork's `develop` (`39061f44`) is 56 upstream commits behind `e54c0087` (last sync `93e98bb7`, PR #14). The commit is pinned because `upstream/develop` keeps moving. Upstream also moved `.skills/` to `.agents/skills/`, which AGENTS.md still names, and added two envs (`metalio_eink4`, `eego_a4`) to the `ci.yml` matrix.

**Approach:** Merge `e54c0087` into branch `sync-upstream-e54c0087` (AGENTS.md "Sync upstream by merging"; never rebase) as one merge commit titled `chore: merge upstream develop (e54c0087)`. Keep both sides' behaviour in every conflict. Put every fork-side fix the merge needs in that same commit, and list each in Implementation Notes. Check, and record in `deferred`, whether upstream's SD-plugin system and its input changes affect the games.

## Boundaries & Constraints

**Always:**
- `merge.ours.driver` is set, so AGENTS.md keeps our copy. Resolve upstream's `CLAUDE.md` with `git rm CLAUDE.md`.
- Never hand-edit generated files (`lib/I18n/I18nKeys.h`, `I18nStrings.*`, `*.generated.h`).
- Keep both sides in `english.yaml` (all keys), `test/CMakeLists.txt` (both sides' `add_subdirectory` lines plus upstream's Python `add_test`), and `HomeActivity.h`/`.cpp` (upstream's `onPluginsOpen`, `hasPlugins`, `hasLibrarySlot()` and our `#if FREEINK_CAP_GAMES` Games blocks).
- Games keeps its place with Plugins installed and without: list mode just above Settings, cover grid the tab before Settings, one index mapping (`getMenuItemCount`, `menuItemToIndex`, `indexToMenuItem`, `CoverGridHomeUi::drawTabs`). Upstream's Plugins entry shares the library slot (index 2), shown when `hasPlugins || hasOpdsServers`.
- Each ledger block (`docs/crosshatch/upstream-touches.md` rows 1-12) stays intact, guarded, and does what its row says. Edit a row's text only where the merge moved what it describes. `python3 scripts/check_upstream_touches.py` passes.
- Row 10 and the probe re-check list apply: upstream changed `src/network/OtaUpdater.cpp`, `HttpDownloader.*`, `lib/JsonParser/ReleaseJsonParser.h`, `FirmwareBoardTag.cpp`, release workflows, `[base] build_flags` (`FREEINK_NET_WOLFSSL` kept, `HAVE_MAX_FRAGMENT` removed, content-protection wolfSSL defines added) and the `freeink-sdk` pointer. Re-read the four probe values against the new SDK `SecureHttpClient` and `HttpDownloader`, and update the paragraph only if a value moved.
- In AGENTS.md, change only: `.skills/` references to `.agents/skills/<name>/SKILL.md` and the "never edit" rule to `.agents/`; the "five envs" list to the envs `ci.yml`'s matrix now builds. Same env-list change in `docs/crosshatch/orchestrated-epics.md`.

**Never:**
- Push, open a PR, rebase, edit `deferred-work.md`, run `tickets.py mark`/`pull`, move the `freeink-sdk` pointer by hand (the merge brings it), edit `.agents/` or upstream's `ci.yml`.
- Redesign. A finding whose fix needs a design choice goes to the orchestrator as a HALT question with options and a recommendation; only a one-line guard is fixed here.
- Stop and report on: a conflict where both sides' behaviour cannot be kept; a fork block that no longer applies; the x4pro image not fitting its app slot (`partitions.csv`); a failing flash budget gate.

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Plugins installed, no OPDS | `hasPlugins` | Slot 2 present: list Folder, Library, Plugins, Transfer, Games, Settings; grid tabs in that order with Games before Settings; slot opens `goToPlugins` | No error expected |
| OPDS only | `hasOpdsServers` | Same layout; slot opens `goToBrowser` | No error expected |
| Neither | both false | No slot 2; Games still directly before Settings; both mappings inverse over every item | Unknown index maps to `NONE` |

</frozen-after-approval>

## Code Map

- Conflicts (merge-tree trial): `lib/I18n/translations/english.yaml`, `src/activities/home/HomeActivity.{h,cpp}`, `test/CMakeLists.txt`.
- `src/components/CoverGridHomeUi.{h,cpp}` -- upstream did not change it; it takes `hasOpds` and the merged `HomeActivity` passes `hasLibrarySlot()`. The slot's tab icon stays Blocks for OPDS and Plugins alike.
- `src/activities/games/GamesLauncherActivity.cpp` (`navigateButtons`, ~l.213) calls `ButtonNavigator::onNextRelease`, which upstream removed (`onPreviousRelease` stays; `Buttons` is now `std::initializer_list`). The base `UiListActivity::navigateButtons` now steps on press. Align the override to `onNextPress`/`onPreviousPress`; the override exists only to walk `rowCount()` rather than `listCount()`.
- `test/game_script/harness/` -- `HomeTabsTest.cpp` and the `home_stubs/`, `screen_stubs/ActivityManager.h`/`ScreenDoubles.cpp` doubles: add `goToPlugins`, `anyPluginInstalled()`, and a Plugins-installed case; `GamesLauncherTest.cpp`, `ModePickerTest.cpp` drive buttons and may assume release stepping.
- Auto-merged fork blocks to re-read against the ledger: `ActivityManager.{h,cpp}` (rows 4-5; upstream added `goToPlugins`, `goToJoinNetwork`, `prepareForSleep`, a haptic call, and a `PluginCatalog` goHome name), `OtaUpdater.cpp` (row 10), `SleepActivity.cpp` (row 12; upstream added `kSleepClean`, dropped `Txt.h`), `platformio.ini` (row 1; `metalio_eink4`, `eego_a4` and `*-gh_release*` envs get no `FREEINK_CAP_GAMES`), `.gitignore`, `.github/workflows/ci.yml` (never edit; not ledgered, so a diff from upstream's means a wrong merge).
- Live `.skills/` references outside `_bmad-output/`: `AGENTS.md` lines 12 and 24 only. "Five envs" lists: `AGENTS.md` (While testing bullet) and `docs/crosshatch/orchestrated-epics.md` l.189. Merged `ci.yml` matrix: `default`, `sticky`, `x4pro`, `x4c`, `papermono`, `metalio_eink4`, `eego_a4`.
- Findings to investigate after the merge (record in `deferred`): `src/network/ProtectedPaths.h` (only the three credential stores are protected; `isPluginPath` admits `/games/`, `/.games/`, `/.games-data/`), `src/util/PluginLocations.h` (roots `/.crosspoint/plugins`, `/plugins`, `/.plugins`), `test/protected_paths`, `test/plugin_manifest_classification`; `/api/plugin-fs` and the file manager in `CrossPointWebServer.cpp`. Input: `ButtonNavigator`, `UiListActivity`/`UiTabListActivity` press stepping, `MappedInputManager::wasScreenTapped` now false on a `HeaderBackTapTarget` tap (the launcher's note popup uses it at l.188; `HeaderBackTapTarget::clear()` runs only in `BaseTheme::drawHeader`), `UiAppHost::route` haptics, `FrontlightPanelActivity` (touch tile now a text label; slider release requests a clean refresh).

## Tasks & Acceptance

**Execution:**
- [ ] `git merge --no-ff --no-commit e54c0087`; resolve the four conflicts and `CLAUDE.md` as the Intent says -- the merge itself.
- [ ] `src/activities/home/HomeActivity.{h,cpp}` -- keep both sides; Games rows use `hasLibrarySlot()` where the converters take the slot flag -- see the matrix.
- [ ] `src/activities/games/GamesLauncherActivity.cpp` -- press stepping, matching the base -- upstream removed `onNextRelease`.
- [ ] `test/game_script/harness/**` -- doubles and tests for the merged code, with the Plugins case from the matrix -- the games host tests must build and pass.
- [ ] `docs/crosshatch/upstream-touches.md` -- re-read rows 1-12 and the probe paragraph; edit only what moved.
- [ ] `AGENTS.md`, `docs/crosshatch/orchestrated-epics.md` -- the `.agents/` and seven-env edits only.
- [ ] Commit once: `chore: merge upstream develop (e54c0087)`, with the attribution lines the session gives.

**Acceptance Criteria:**
- Given the merge commit, when `git log --oneline --merges -1` and its parents are read, then parent 1 is `39061f44` and parent 2 is `e54c0087`.
- Given the host suites, `scripts/*_test.py`, `check_layers.py` and `check_upstream_touches.py`, when run, then all pass and `./bin/clang-format-fix` twice leaves `git status` unchanged.
- Given each env of the merged `ci.yml` matrix, when built, then it succeeds; x4pro fits its slot; the flash budget gate passes.

## Implementation Notes

The merge is staged and uncommitted (MERGE_HEAD is `e54c0087`); the plan's one commit is not made. `merge.ours.driver` was set, so AGENTS.md merged with our copy; upstream did not change `CLAUDE.md` in this range, so there was nothing to `git rm`. The `freeink-sdk` pointer is upstream's `32a265fa` (brought by the merge; the submodule was checked out to it).

Conflicts, both sides kept: `lib/I18n/translations/english.yaml` (our `STR_GAMES_*` block, then upstream's clipping keys), `test/CMakeLists.txt` (`game_core`, `game_script`, then upstream's four `add_subdirectory` lines and the Python `add_test`), `src/activities/home/HomeActivity.h` / `.cpp` (`onGamesOpen` and `onPluginsOpen`, both guarded as before). The merge's auto-resolved callers already pass `hasLibrarySlot()` to `menuItemToIndex`, `indexToMenuItem`, `getMenuItemCount` and `CoverGridHomeUi::begin`, and the Games row/tab code needed no change: `CoverGridHomeUi.*` is untouched, and the Games index mapping is the same with and without the slot.

Fork-side fixes in the merge commit:
1. `src/activities/games/GamesLauncherActivity.cpp`: `navigateButtons` uses `onNextPress`/`onPreviousPress` (upstream removed `onNextRelease`), matching the base.
2. `test/game_core/CMakeLists.txt`, `test/game_script/CMakeLists.txt`, `harness/CMakeLists.txt`, `installer.cmake`, `resume.sources.cmake`, `games_launcher.cmake`: upstream moved `StreamingJsonParser` from `lib/JsonParser` to the SDK (`freeink-sdk/libs/network/JsonSax`, 2600a79e); the sources and the include path now point there.
3. `scripts/check_layers.py` (+ a test in `check_layers_test.py`): `<StreamingJsonParser.h>` is now an SDK header; `SDK_HEADERS_AS_COMPONENT` maps it to `lib/JsonParser`, so the spine's Domain edge (GameCore may include the parser) still holds and no other component gains it. A design-adjacent choice: the alternative is a new `sdk:` edge in the spine's table.
4. `scripts/pack_game_test.py`: reads `MAX_NESTING` from the SDK header when the submodule is checked out; the Fork script tests job checks out no submodule, so there the shared vectors pin the constant alone (the header assertion is skipped, the rest of the test runs).
5. `docs/crosshatch/formats.md` l.347: the header's new path.
6. Harness doubles: `home_stubs/HapticFeedback.h` (counts `touchAction`), `home_stubs/activities/plugins/PluginCatalogActivity.h` (`pluginsInstalled()` / `anyPluginInstalled()`), `home_stubs/FsHelpers.h` + `HomeStubs.cpp` (`hasReflowableBookExtension` for `hasEpubExtension`), `screen_stubs/components/UITheme.h` (`getContentArea`, the whole screen), `screen_stubs/ActivityManager.h` + `ScreenDoubles.cpp` (`goToPlugins(showOpds)`, `goToJoinNetwork`, `prepareForSleep`), `screen_stubs/MappedInputManager.h`: `hold()` is now the held level (isPressed, getHeldTime) without the press edge (wasPressed), as the device's, because upstream's `ButtonNavigator::onContinuous` returns on a pressed frame; `wasAnyPressed`/`wasAnyReleased` dropped (upstream removed them from the manager). The double was more permissive than the device before (a level `wasPressed` on every held frame).
7. Tests: `HomeTabsTest.cpp` covers plugins only, plugins with OPDS (the slot opens `goToPlugins`, `lastShowOpds` follows), neither, the walks with and without books, the reselected item, both mappings over the slot flag, and the list-mode labels and one haptic call per row tap, in both the games-on and games-off builds; `GamesLauncherTest.cpp` adds a press-steps/release-does-not test.
8. `AGENTS.md` (`.agents/`, seven envs), `docs/crosshatch/orchestrated-epics.md` (seven envs), `docs/crosshatch/upstream-touches.md` (probe paragraph, below).

Ledger re-read: rows 1-12 are intact and guarded (`platformio.ini` has no `FREEINK_CAP_GAMES` on `metalio_eink4`, `eego_a4` or the `*-gh_release*` envs; `OtaUpdater.cpp`, `SleepActivity.cpp`, `BootActivity.cpp`, `ActivityManager.*` fork hunks sit unchanged beside upstream's edits). Only the probe paragraph moved: the SDK commit is `32a265fa...`; `HttpDownloader` now runs through the SDK's `ResumableFetch.h` (its 5-hop limit is `kMaxRedirects` there; 60,000 ms, `setInsecure()` and the user agent are as before), and it reports every non-2xx status as a bare failure (was non-200). `SecureHttpClient` defaults (15,000 ms, 0 redirects, `setReuse`) and `FREEINK_NET_WOLFSSL` in `[base]` are unchanged, so the probe's four values hold. The release workflows, `HttpDownloader.*`, `[base] build_flags` and the SDK pointer all moved, so the fork release dry run (Actions tab) is still owed before this merge counts as done.

Verified so far: the games and all host suites (1999 tests, `games-check` label included) pass; all `scripts/*_test.py`, `check_layers.py`, `check_api_freeze.py` pass; `check_upstream_touches.py` passes on a trial merge commit of the staged tree; `./bin/clang-format-fix` twice leaves `git status` unchanged. No firmware build, `pio check` or simulator build was run.

Fork-side fixes added after the implementer's report (same merge commit):
9. `.claude/skills/run-crosshatch-player/simulator.ini` and `SKILL.md`: `sim.sh build x4pro` failed (`HalHaptics.h` not found) because the pinned crosspoint-simulator (`8699595`) predates upstream's haptics and plugin HAL. The pin moves to `1915ee0a` (crosspoint-simulator HEAD, the SKILL's own gotcha), with the new sample's `skip_device_content_protection.py` pre-script and the `JsonSax` and `CatalogList` lib deps. `scripts/sim_sh_test.py` passes.
10. From the review (triage log items 4, 9, 16): `scripts/pack_game_test.py` fails when the SDK is checked out but its header moved; `GamesLauncherTest.cpp` gains the NavPrevious press test; a stale comment in `screen_stubs/MappedInputManager.h`.

Input findings checked against the merged tree (all recorded in `deferred`): press stepping reaches only the launcher among the games' own overrides (fixed); the mode picker and options screens use the base `navigateButtons`; the match pause menu already stepped on press; `GameConfirmDialog` still steps on release. `wasScreenTapped` is false on a header-back tap, which `wasBackGesture` turns into Back; the match reads gestures through `readGesture`, not `wasScreenTapped`, and `ActivityManager` clears `HeaderBackTapTarget` on exit and push (l.203, l.234), so a match never sees a stale back target. The light panel opens by the down swipe (`wasLightPanelGesture`) in a match; its touch tile only toggles `SETTINGS.touchReaderControls`, which only the EPUB reader reads, and its slider-release clean refresh is the panel's own. `UiAppHost::route` haptics fire for game screens when the user enables haptics (default off; deferred). Plugin system: its roots (`/.crosspoint/plugins`, `/plugins`, `/.plugins`) do not meet `/games`, `/.games`, `/.games-data`, `/.games-tmp`; the one exposure is plugin writes through `isPluginPath` (medium, deferred with options (a) protect the game paths via a new ledger row, (b) an upstream hook, (c) accept; recommendation (b), (c) meanwhile).

## Plan Change Log

## Review Triage Log

Lenses: blind-hunter, edge-case-hunter, verification-gap, intent-alignment, each a context-free foreground subagent, reading `review.diff` (the fork-side diff: upstream-owned files as the difference from `e54c0087`, fork-only files and AGENTS.md from `39061f44`; 34 files, 72 KB) at merge commit `f238c89b`. Pass 1: 0 high, 0 medium, 3 low patched, 2 low deferred, the rest false or rejected.

| # | Lens | Finding | Verdict, route, evidence |
|---|------|---------|--------------------------|
| 1 | BH, EC | `.gitignore` drops upstream's `.claude/*` for `.claude/worktrees/` | false: `39061f44` already had only `.claude/worktrees/`; `93e98bb7` and `e54c0087` have `.claude/*`, so the fork changed it after the last sync and the merge kept that side; `.gitignore` is on the Allowlist |
| 2 | BH | Probe paragraph stale on the timeout source | false: `HttpDownloader.cpp` l.18/35 still sets `HTTP_TIMEOUT_MS = 60000`; I re-read `SecureHttpClient.h` at `32a265fa` (`_timeoutMs = 15000`, `_followRedirects = 0`) and `ResumableFetch.h` (`kMaxRedirects = 5`) |
| 3 | BH | Re-check list item 1 lacks `ResumableFetch.h`; no script ties the pinned SDK commit to the pointer | low, rejected: item 3 of the same list already re-reads on any SDK pointer move; a script is more than a correction |
| 4 | BH, EC | `pack_game_test.py` skips the `MAX_NESTING` header pin silently | low, patch: skip only when `freeink-sdk/libs` is absent, so a later SDK move of the header fails the test where the submodule is checked out |
| 5 | BH | JsonSax path repeated across seven CMake files; stale `lib/JsonParser/StreamingJsonParser` copy may shadow the SDK's | partly false: `git ls-files lib/JsonParser` holds only `ReleaseJsonParser.*`, no stale copy. Repetition is low, rejected: a variable is a refactor beyond a correction |
| 6 | BH | `goHome` compares the literal `"GameMatch"`; ledger says `HomeMenuItem::Games` | low, rejected: both predate this merge (fork code unchanged by it) |
| 7 | BH | `HomeTabsTest` comment claims `goHome` coverage | false: the comment explains why the library slot is found by the OPDS item; it does not claim a test of `goHome` (the untested mapping is row 12) |
| 8 | BH | Haptic double asserted only for list rows | low, rejected: fixing adds tests for paths the diff did not change; `UiAppHost::route` fires the call for every routed release |
| 9 | BH, VG | Previous key stepping on press is untested (the new test drives Next only) | low, patch: added `ListTest.APreviousKeyStepsOnItsPressAndItsReleaseIsNotOneStepMore` (passes; `click(NavPrevious)` and the held test pass either way, as the lens showed) |
| 10 | BH | Other fork list screens may still step on release | false: `git grep onNextRelease\|onPreviousRelease` finds only upstream's `LibraryListActivity`; the mode picker and options screens use the base `navigateButtons`; the remove dialog is row 13 |
| 11 | BH | `STR_GAMES_PRODUCT_NAME` naming, 120/70 literals, `STR_GAMES_*` block placement, "3 seconds" in a string | low, rejected: all predate the merge; the block's place was the conflict, both sides kept |
| 12 | BH | `assetSuffix` unused in games builds | low, rejected: predates the merge (the fork `#if` is unchanged) |
| 13 | BH | AGENTS.md says seven envs but not that `metalio_eink4`/`eego_a4` have no games | false: the task fixes AGENTS.md edits to the `.agents` paths and the env list |
| 14 | VG | List-mode row icons are never asserted | low, rejected: the code reads `menuIcons.insert(end() - 1, Blocks)` beside `begin() + 2`, which cannot misalign today; the list-mode screenshot checks it |
| 15 | VG | No test of the real `goHome` mapping | low, defer (deferred list): predates the merge; needs harness plumbing |
| 16 | VG | `powerConfirmClick` comment names `wasAnyPressed`/`wasAnyReleased`, which the merge removed from the double | low, patch: comment trimmed |
| 17 | EC | Same as 4 and 1 | see 4 and 1 |
| 18 | IA | Diff tests exercise fork-written doubles, not upstream's `MappedInputManager`; other game screens still step on release (`GameMatchActivity` pause menu already on press; `GameConfirmDialog` on release) | descriptive; the double change is recorded in Implementation Notes (it was more permissive than the device); the dialog is a low deferred item (deferred list) |
| 19 | IA | The plugin path and installer are not covered by a test | false as a defect: the plugin roots do not meet the games folders (checked); the one real exposure is the medium deferred item on `isPluginPath`, which needs the owner's choice |

Patches applied in the same merge commit (amend): items 4, 9, 16. Re-run: `pack_game_test.py` OK; `GamesLauncherHarnessTest` 2 new tests pass; full host suite 2000 passed; `./bin/clang-format-fix` twice, nothing new.

## Design Notes

- Why press stepping in `GamesLauncherActivity`: upstream's commit 1f08ce93 moved every list to step on press; the launcher's override copies the base except for its row count, so it follows the base (Intent: keep both sides' behaviour).
- Guards in `HomeActivity::indexToMenuItem`/`menuItemToIndex`: the trailing `return 0` / `NONE` and the `hasOpdsUrl` gate protect an unknown item and an absent slot; keep them as they are. `git log -L` on both is not needed since neither is rewritten, only their callers' argument changes.
- `scripts/check_layers.py` (implementer's call, flagged for the orchestrator): the parser's header moved into the SDK, so `SDK_HEADERS_AS_COMPONENT` maps `StreamingJsonParser.h` to the spine's existing `lib/JsonParser` edge; the alternative is a new `sdk:` edge in the spine's table. The mapping keeps the layer rule unchanged (only GameCore may include it) and needs no spine edit.

## Verification

**Commands:** (all under the locks AGENTS.md names; `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`)
- Host: delete `build/test`, then Ninja configure and build under the host-test lock, `ctest --test-dir build/test --output-on-failure -j` (the `games-check` label included) -- expected: all pass.
- `python3 scripts/<each>_test.py`, `scripts/check_layers.py`, `scripts/check_upstream_touches.py`, `./bin/clang-format-fix` twice -- expected: pass, nothing new in `git status`.
- After the review: `pio pkg install` then `pio run` for `default`, `sticky`, `x4pro`, `x4c`, `papermono`, `metalio_eink4`, `eego_a4`; `scripts/check_flash_budget.py` `build on`, `build off`, `compare`, `objects`; `pio check` and `pio check -e x4pro` as AGENTS.md gives them; `sim.sh build x4pro` -- expected: success; x4pro within `partitions.csv`.
- Sizes: each env's `firmware.bin` bytes at `39061f44` and at the merge commit, measured the same way (`stat -c %s .pio/build/<env>/firmware.bin` after `pio run -e <env>`).

**Manual checks:**
- Simulator screenshots in `_bmad-output/implementation-artifacts/sync-e54c0087-screenshots/`: Home in list mode and in the cover grid (Games above Settings), the Games list, one move of Ultimate tic-tac-toe.

- Fork release dry run (orchestrator, 2026-10-11, after the merge): run 38100498514 (`crosshatch-release.yml`, `dry_run` true) on `claude/sync-upstream-e54c0087` at `0bf1c2d5`, success; it built the release envs, packed the games, uploaded the `fork-release` artifact, and published nothing. `games/`, `scripts/pack_game.py` and `scripts/fork_release.py` are unchanged from `273fa892`, and `pack_game.py` over a `git archive` of `0bf1c2d5` gives the packet's package hashes: `sudoku` `f31b5a02a67a712a`, `ultimate-tic-tac-toe` `cd11a6224228acd9`, `battleship` `d3fecc1e5fc245f6`. The deferred dry-run item is resolved.

## Auto Run Result

**Summary.** Merged upstream `e54c0087` into `sync-upstream-e54c0087` as one merge commit (parents `39061f44`, `e54c0087`), titled `chore: merge upstream develop (e54c0087)`. Both sides kept in the four conflicts; Games sits before Settings with and without Plugins installed (list mode and cover grid, screenshots below). Fork-side fixes are items 1-10 in Implementation Notes. No blocking condition: x4pro fits its slot, the flash budget passes, and every env builds. One medium finding needs the owner (plugin writes to game folders) and one medium task is owed (the fork release dry run); both are in `deferred`.

**Review.** Thorough, four context-free foreground lenses over the fork-side diff (`lenses_ran` above); 0 high, 0 medium, 3 low patched, 2 low deferred, the rest false or rejected (Review Triage Log).

**Verification (evidence).**
- Host: `build/test` deleted and rebuilt (Ninja), `ctest -j4`: 2000 passed, `games-check` label (129 tests) included; after the review patches, rebuilt and re-run: 2000 passed. All 13 `scripts/*_test.py`, `check_layers.py`, `check_api_freeze.py` pass; `check_upstream_touches.py` passes; `./bin/clang-format-fix` twice, nothing new.
- Firmware, under the build lock with `PLATFORMIO_BUILD_CACHE_DIR=/home/user/crosshatch-player/.cache`, on source of commit `b393551a` (before the plan and screenshots were added): `pio pkg install` and `pio run` succeed for `default`, `sticky`, `x4pro`, `x4c`, `papermono`, `metalio_eink4`, `eego_a4`. `check_flash_budget.py` `build on`, `build off`, `compare` (270 KiB, 1,024 B), `objects` all exit 0: flash +258,768 B (+252.7 KiB) with 17,712 B to spare, static RAM +784 B with 240 B to spare. `pio check` for `default` and `-e x4pro`: no problems. `sim.sh build x4pro` succeeds after the simulator pin bump.
- x4pro `firmware.bin` 6,214,000 B in an app slot of 0x640000 = 6,553,600 B: 339,600 B spare.

| env | before `39061f44` (B) | after (B) | delta (B) |
|---|---:|---:|---:|
| default | 5,644,160 | 5,925,696 | +281,536 |
| sticky | 5,826,288 | 6,096,576 | +270,288 |
| x4pro | 5,937,008 | 6,214,000 | +276,992 |
| x4c | 5,658,432 | 5,930,976 | +272,544 |
| papermono | 5,686,608 | 5,959,936 | +273,328 |
| metalio_eink4 | n/a (env absent) | 5,944,592 | n/a |
| eego_a4 | n/a (env absent) | 5,950,672 | n/a |

Method: `pio run -e <env>` on the tree at `39061f44` (the worktree's incremental cache) and at `b393551a`, then `stat -c %s .pio/build/<env>/firmware.bin`, the same way both times.

**Screenshots** (`_bmad-output/implementation-artifacts/sync-e54c0087-screenshots/`, simulator `x4pro`, each looked at):
- `home-list.png`: list-mode Home, no plugin; Games directly above Settings.
- `home-cover-grid.png`: cover-grid Home; the controller tab before Settings.
- `home-list-plugins.png`: list mode with a plugin installed; Plugins in the library slot, Games still above Settings.
- `home-cover-grid-plugins.png`: cover grid with a plugin installed; six tabs, Games before Settings (tapping it opened Games).
- `games-list.png`: the Games list with Ultimate Tic-Tac-Toe.
- `ttt-after-move.png`: Ultimate tic-tac-toe after Player 1's move; Player 2 to move.

**Files.** The merge commit's fork-side set: the conflicted files, the harness doubles and tests under `test/game_script/harness/`, four CMake files, `scripts/check_layers.py`, `scripts/pack_game_test.py`, `docs/crosshatch/{formats,upstream-touches,orchestrated-epics}.md`, `AGENTS.md`, `src/activities/games/GamesLauncherActivity.cpp`, the simulator skill's `simulator.ini` and `SKILL.md`, this plan, the screenshots. `./bin/clang-format-fix` changed no file.

**Residual risks.** The fork release dry run is owed (needs a push). The plugin write exposure of game folders needs the owner's choice. Device behaviour (haptics in matches, SD plugin and game coexistence) is unmeasured on hardware. `check_layers.py`'s `SDK_HEADERS_AS_COMPONENT` mapping (Design Notes) is the implementer's choice of two.
