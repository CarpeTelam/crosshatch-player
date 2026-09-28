---
title: 'Runtime views draw from the library'
type: 'feature'
ticket: '5'
created: '2026-09-28'
status: done
baseline_revision: '7329a0201859c240bcc0f3aee6829d3328e3b6b3'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/AGENTS.md'
  - '{project-root}/docs/contributing/touch-and-ui.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** The match's own views (pause menu, end-of-round menu, error view) are text-only framed dialogs, so they do not look like the games, which now draw from the shared icon library (CAP-8, R8).

**Approach:** A pure, host-tested `src/games/GameViewIcons.h` names each view's and each menu row's library icon and decides whether a row has room for one. `GameMatchActivity::buildView` reserves the option dialog's content band for the view's 64 px icon and draws it, and the rows' 32 px icons left of each label, through entry 1's `drawGameIcon`, with no new include edge and no new strings.

## Boundaries & Constraints

**Always:**
- AGENTS.md rules; C3-safe (`default` builds: the files are whole-file `#if FREEINK_CAP_GAMES` or header-only), locals under 256 B, no allocation. Every build, simulator build, and host-test CMake step runs under `flock /tmp/claude-0/-home-user-crosshatch-player/122f9ed2-dd81-5ff3-bef7-fad69649b3bf/scratchpad/build.lock sh -c '...'`.
- Names (entry 4's assumption): Paused `pause`, Over `flag_checkered`, Error `warning`; rows Resume `play`, Leave `leave`, Play again `restart`, and the error view's Back `leave` (in that view Back leaves the match).
- The view icon is `MEDIUM` (64 px), black, centred horizontally in the band `fui::optionDialog` returns (`props.contentHeight` = 64). A row icon is 32 px at the row's left, inset by the row's vertical margin `(buttonHeight - 32) / 2`, drawn only when the centred label leaves the inset + 32 px + `props.gap` free on its left; its ink follows the row's resolved button style (`buttonStyles.resolve(frame.stateFor(...)).foreground` white → white ink, else black).
- Screens reach icons only through `src/games` (`GameIconDraw.h`, `GameViewIcons.h`); `python3 scripts/check_layers.py` passes unchanged.

**Never:** No SDK edit (`DialogOption` has no icon field; the icons are drawn over the finished dialog, as `CoverGridHomeUi` draws `drawIcon` inside its screen function), no new `tr()` key, no change to `MatchLifecycle`, the spine, `check_layers.py`, `ci.yml`, or any upstream file. Never commit (the build agent commits).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Pause | Back in play | `pause` above the rows; `play` on Resume, `leave` on Leave | none |
| Game over | round ends | `flag_checkered`; `restart` on Play again, `leave` on Leave | none |
| Error | fault or load failure | `warning` between the (up to 8-line) message and Back; `leave` on Back | none |
| Focus | Down moves focus to row 2 | row 2's icon ink matches its label (focused style foreground) | none |
| No room | a label within 32 px + margins of the row's left | that row draws no icon; the label is untouched | none |
| Name missing | a future rename drops a name | the host test fails; on device `drawGameIcon` logs and draws nothing | `LOG_ERR` in `drawGameIcon` |

</frozen-after-approval>

## Code Map

- Work only in `/home/user/wt-runtime` (branch `epic3/runtime`); scratch under `/tmp/claude-0/-home-user-crosshatch-player/122f9ed2-dd81-5ff3-bef7-fad69649b3bf/scratchpad/3.5/`. Toolchain installed; do not reinstall or touch certificates. Never commit `_bmad/render/`.
- `src/activities/games/GameMatchActivity.{h,cpp}` -- `buildView` builds `dialogProps` (`verticalOptions`, touch-only, framed panel) and calls `fui::optionDialog(screen.frame(), centeredRect(...), props)`, which returns the content band; `optionLabel(MatchEvent)` in the anonymous namespace is the pattern for the row map; `MAX_OPTIONS` = 2. Already includes `games/...` headers (existing Screens → src/games edge).
- `freeink-sdk/libs/ui/FreeInkUI/include/components/overlays/option-dialog.h` (read-only) -- the band sits directly above the buttons: buttons start at `band.bottom() + props.gap`, each `props.buttonHeight` (44) tall, `props.gap` (8) apart, `band.x`/`band.width` wide. `optionDialogHeight` counts `gap + contentHeight`. `components/controls/button.h` -- label centred in `rect.inset({2,4,2,4})`, measured with `target().measureText(text.font, label, text)`.
- `FreeInkUICore.h` -- `Frame::stateFor(action, value, base)`, `StyleSet::resolve(State)`, `Paint::color`, `Rect::bottom()/empty()`.
- `src/games/GameIconDraw.h` -- `drawGameIcon(renderer, name, x, y, pixels, black)`; `lib/GameIcons/GameIcons.h` -- `find(name, len)`, `SMALL_PIXELS`/`MEDIUM_PIXELS`.
- `lib/GameCore/MatchLifecycle.h` -- `MatchState`, `MatchEvent`, `menuFor` (Paused: Resume, Leave; Over: PlayAgain, Leave; Error: Back).
- `test/game_script/CMakeLists.txt` -- `GameScriptTest` source list (add the new test and `lib/GameCore/MatchLifecycle.cpp`); `src/games`, `lib/GameCore`, `lib/GameIcons` are include dirs. `GameTouchTest.cpp` is the pure-header test pattern.
- `docs/crosshatch/game-canvas.md` "The views" table -- add an Icon column.

## Tasks & Acceptance

**Execution:**
- [x] `src/games/GameViewIcons.h` -- header-only, `namespace GameViewIcons`: `VIEW_PIXELS` 64, `ROW_PIXELS` 32, `constexpr forView(MatchState)` and `forOption(MatchEvent)` (null for no icon), `rowIconInset(rowHeight)`, `rowIconFits(rowWidth, rowHeight, labelWidth, gap)` -- pure mapping and room rule, host-testable.
- [x] `src/activities/games/GameMatchActivity.{h,cpp}` -- set `props.contentHeight` to `VIEW_PIXELS` when the view has an icon; keep the band `optionDialog` returns; a private `drawViewIcons(screen, band, state, menu, count)` draws the view icon and each row icon per the rules above -- the views draw from the library.
- [x] `test/game_script/GameViewIconsTest.cpp` + `CMakeLists.txt` -- every view with a menu and every event in each `menuFor` menu has a name that `GameIcons::find` resolves; Starting/Playing/Leaving and non-menu events give null; `rowIconFits` true for a short label, false at the boundary minus one pixel and for a row shorter than 32 px -- guards entry 8's renames.
- [x] `docs/crosshatch/game-canvas.md` -- Icon column (view icon, row icons, "drawn when the label leaves room").

**Acceptance Criteria:**
- Given the simulator on `x4pro` and on `sticky`, when the tracer game is paused, ends a round, and a fault script runs, then the pause, game-over, and error views each show their icon in the band and the row icons, and screenshots are committed.
- Given the tree, when host tests, `x4pro`, `sticky`, and `default` builds, and `check_layers.py` run, then all pass.

## Implementation Notes

- `drawViewIcons` keeps the planned signature and reads each row's label and base state from `dialogProps.options`, which still points at `buildView`'s local options array during the call, so it sees the same focus `buildView` drew (no second read of `selected`).
- `rowIconFits` takes the label's left edge as `(rowWidth - labelWidth) / 2`, which equals the button's `4 + (rowWidth - 8 - labelWidth) / 2` for any label that fits its padded content; a label wider than the row never fits.
- The error view's content band sits between the message and Back, as `optionDialog` places it; the message rect shrinks by `gap + 64` px, and the dialog grows by the same, so the 8-line cap is unchanged.
- Verified: host suite 691/691; `x4pro`, `sticky`, `default` builds; `check_layers.py`, `check_upstream_touches.py` pass; simulator screenshots for both devices in `story-runtime-views-screenshots/` (Down to Leave on x4pro showed Leave's icon on the focused dither with black ink, matching its label).

## Plan Change Log

## Review Triage Log

**Pass 1** (thorough; the four lenses ran as context-free subagents over `git diff 7329a020` of `src test docs`, new files included). Verdicts: high 0, medium 1, low 7, false 6, maybe-false 0. Routes: patch 2 (VG1+B1, IA1) plus the builder's own AD-2 note (header constants `inline constexpr`), defer 1 (B6), the rest rejected. Patches applied by the implementation subagent; re-verified below.

| # | Lens | Finding | Verdict | Route / evidence |
|---|------|---------|---------|------------------|
| VG1 | verification-gap | Row-icon y copies `optionDialog`'s layout; no test pins it against the SDK | medium | patch: pure `GameViewIcons::rowTop` used by `drawViewIcons`; `RowTopMatchesOptionDialogsRows` runs the real `fui::optionDialog` (`contentHeight` 64, vertical options) and checks each row's hit rect against it (GameScriptTest now also compiles the SDK's `FreeInkUI.cpp`, unedited). |
| B1 | blind-hunter | Same as VG1 | medium | grouped with VG1. |
| B2 | blind-hunter | Ink ignores `StateDisabled` and non-solid foreground paints | low | rejected: every `DialogOption` is enabled (never set false in `buildView`) and `theme.button`'s foregrounds are solid; unreachable today, and the fix adds branches. |
| B3 | blind-hunter | `forOption(Back)` = `leave` is right only because Back is a row only in the error menu | low | rejected: the header comment says so and `menuFor` has Back only in `ERROR_MENU`; keying on (state, event) adds surface for no current case. |
| B4 | blind-hunter | Pause and game-over icons have 16 px above the band, 8 px below | low | rejected: cosmetic, set by the SDK's slot layout (headline gap + band gap); an entry-8 look point. |
| B5 | blind-hunter | Screenshots and plan missing from the diff | false | they exist (`story-runtime-views-screenshots/`, this plan); the review diff was staged from `src test docs` only. |
| B6 | blind-hunter | SDK gap (`DialogOption` has no icon) worked around without an upstream proposal | low | defer: `deferred-work.md` `## 3.5` records the upstream proposal (AGENTS.md: SDK changes are proposed upstream). |
| B7 | blind-hunter | `VIEW_PIXELS`/`ROW_PIXELS` duplicate the library's constants | false | deliberate: Screens include `GameViewIcons.h`, which stays free of `lib/GameIcons` (epic Notes: Screens keep no `lib/GameIcons` edge); `SizesAreTheLibrarys` pins the equality. |
| B8 | blind-hunter | The test's state and event lists are hand-kept | low | rejected: `forView`/`forOption` switch over every enumerator with no default, so `-Wswitch` flags a new value at compile time; a count sentinel would change GameCore. |
| B9 | blind-hunter | An empty band also suppresses row icons | false | intended: rows are located from the band, every view with a menu has a view icon, and the header comment says "Nothing for an empty band". |
| E1 | edge-case-hunter | Error view with a 2-line headline, 8-line message and the band could exceed the safe height | false | height = 24 padding + 11 text lines + 4 gaps of 8 + 64 + 44; even at 50 px a line that is 714 px, inside the 480x800 portrait safe rect. |
| E2 | edge-case-hunter | `band.width < 64` gives a negative centring offset | false | the dialog is `safe.width * 4 / 5` (about 380 px) less 32 px padding; unreachable. |
| IA1 | intent-alignment | `game-icons.md` still says the rows are drawn only "should they draw them" and omits Back → `leave` | low | patch: only the entry-5 bullet rewritten (the Home lane edits the same section's other lines; its "No screen draws a library icon yet" line is left for the merge). |
| IA2 | intent-alignment | The error view's Back row shows `leave` beside the label's own `«` | low | rejected: cosmetic, an entry-8 look point (listed under Assumed). |
| IA3 | intent-alignment | The tallest error view is not checked | false | as E1. |

## Design Notes

Row rects are derived from the returned band, not recomputed from the panel: `optionDialog` documents the band as "directly above the buttons", so `rowY(i) = band.bottom() + gap + i * (buttonHeight + gap)` (`GameViewIcons::rowTop`, pinned to the SDK by `RowTopMatchesOptionDialogsRows`). This is the only coupling to the SDK layout; the icons are skipped when the band is empty.

**Assumed (reversible, for entry 8):** the view icon sits in the content band (between the text and the rows), the only caller-drawn slot the dialog has; the error view's Back row uses `leave`; row icons sit at the row's left edge rather than beside the centred label, since the dialog draws the label and has no icon field; the error view's `leave` sits beside `STR_BACK`'s own `«`; the pause and game-over icons have the headline's gap and the band's gap above them (16 px) and one gap below (8 px), as the SDK lays the band out.

## Verification

**Commands:**
- `flock <lock> sh -c 'cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j'` -- all pass.
- `flock <lock> sh -c 'pio run -e x4pro && pio run -e sticky && pio run -e default'` -- all succeed.
- `python3 scripts/check_layers.py` and `python3 scripts/check_upstream_touches.py` -- pass.
- Simulator (`sim.sh build x4pro|sticky`, `SIM_DISPLAY=:78`): `tracer` + `f-lua-error` in `fs_/.games/`; pause (Back), game over (5 taps), error view; copy to `story-runtime-views-screenshots/{x4pro,sticky}-{pause,game-over,error}.png`.

**Results (after the review patches, 2026-09-28, worktree tree before commit):**
- Host suites under the lock: `100% tests passed, 0 tests failed out of 692` (the 9 `GameViewIconsTest` cases included).
- `pio run -e x4pro`, `-e sticky`, `-e default` under the lock: all SUCCESS (logs in the scratchpad `3.5/pio-*.log`); `sim.sh build x4pro` and `sim.sh build sticky`: exit 0.
- `python3 scripts/check_layers.py`: 354 include edges in 89 game files, passed. `python3 scripts/check_upstream_touches.py`: PASS (every touched file is fork-only).
- Screenshots (taken by the implementation subagent before the review patches; the patches only moved the same `rowY` formula into `rowTop`, and a post-patch x4pro pause shot over the `icons` fixture shows the dialog, icon and rows at the same pixels), in `story-runtime-views-screenshots/`:
  - `x4pro-pause.png`, `sticky-pause.png`: Paused, the `pause` icon in the band, `play` on the focused Resume row, `leave` on Leave.
  - `x4pro-game-over.png`, `sticky-game-over.png`: Game over after the tracer's fifth tap, `flag_checkered`, `restart` on Play again, `leave` on Leave.
  - `x4pro-error.png`, `sticky-error.png`: `f-lua-error` ("main.lua:2: boom"), `warning` between the message and Back, `leave` on Back.
  - Each x4pro/sticky pair is byte-identical: both envs render a 480x800 portrait screen with the same theme and no button hints; each came from its own simulator build.
- Row-icon ink on a focused row checked by hand on x4pro (Down to Leave: black ink on the focused dither, like its label).
