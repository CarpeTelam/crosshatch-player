---
title: 'Brand the firmware as Crosshatch: mark bitmaps, boot/sleep swap, default game icon'
type: 'feature'
ticket: ''
created: '2026-10-03'
status: 'draft'
route: 'full'
route_source: 'auto'
review: ''
review_source: ''
lenses_ran: []
review_loop_iteration: 0
context:
  - '{project-root}/_bmad-output/planning-artifacts/ux-designs/ux-crosshatch-player-2026-10-03/DESIGN.md'
  - '{project-root}/_bmad-output/planning-artifacts/ux-designs/ux-crosshatch-player-2026-10-03/EXPERIENCE.md'
  - '{project-root}/docs/crosshatch/upstream-touches.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** The boot and default sleep screens still show the CrossPoint logo and name, and a game with no icon shows the Phosphor controller. The finalized UX spines specify the Crosshatch mark (A2) for all three.

**Approach:** Generate the mark as 120, 64 and 128 px 1-bit bitmaps in new fork-only files and wire them in with the smallest possible hunks in upstream files, all behind `FREEINK_CAP_GAMES`. Boot and sleep keep the stock layout; only the logo and title change.

## Boundaries & Constraints

**Always:** Keep the stock layout (120 px icon centred, title top h/2+70, small line h/2+95; sleep inverted unless LIGHT, boot not inverted). Pixel-check the bitmaps (ring edge 78 vs grid edge 80 units: the O must not fuse into the grid at 64 and 120 px; 128 px is native). New fork code and assets go in new files. Every touched upstream file gets a ledger row; run `scripts/check_upstream_touches.py` and `scripts/check_layers.py`. Follow AGENTS.md verify order.

**Never:** Add the mark to the Phosphor icon library or its names (`names.txt`, `SHA256SUMS`, `GameIcons.generated.h`, `api-level-1.txt`). Edit `Logo120.h`, `FilesPage.html`, generated i18n files, `.skills/`, or the `freeink-sdk` pointer. Reformat or rename upstream code. Brand the website or repo avatar (out of scope).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Boot | S3 env | Mark, "Crosshatch", BOOTING, version; not inverted | n/a |
| Sleep, DARK or LIGHT | default screen | Mark and "Crosshatch", SLEEPING; inverted unless LIGHT | n/a |
| Sleep, custom/cover/transparent with nothing to show | fallback | The same default screen | existing fallback |
| Game with no icon | launcher row | 64 px mark | n/a |
| Game with no icon or title.png | title splash, hand-off band | 128 px mark | n/a |
| Game with its own icon | any | its own icon, unchanged | n/a |
| Non-games env (default C3) | no `FREEINK_CAP_GAMES` | unchanged CrossPoint screens | n/a |

**Decisions (owner, 2026-10-03):**
- Branding applies to the games envs only (x4pro, sticky), selected by `FREEINK_CAP_GAMES`; the default C3 env and the others keep the CrossPoint screens.
- The title is a new `STR_GAMES_PRODUCT_NAME` key ("Crosshatch") in `english.yaml`, inside the existing `STR_GAMES_*` ledger row; no ledger wording change for that file.
- Add ledger rows for `BootActivity.cpp` and `SleepActivity.cpp` and a short architecture spine note, in the same change as the code.

</frozen-after-approval>

## Code Map

- `src/activities/boot_sleep/BootActivity.cpp`, `SleepActivity.cpp` (`renderDefaultSleepScreen`) -- hardcode `Logo120`, `tr(STR_CROSSPOINT)`; no branding hook exists (investigated).
- `src/images/Logo120.h` -- 1 bpp MSB-first, 1 = white, 15-byte stride, physical-panel orientation (`convert_icon.py` rotates 90° CCW). Leave untouched.
- `src/games/GameRowIcon.{h,cpp}` -- `choose` ends in `Source::Fallback` (`game-controller`); `FALLBACK_NAME` stays (library lookups rely on it).
- `src/activities/games/GamesLauncherActivity.cpp` `provideRow` (~436) -- library icon to Mask1 (bit 0 = ink, MSB first, 512 B at 64 px).
- `src/activities/games/GamePicture.cpp` `drawIcon` (~97) -- Fallback goes to `drawGameIcon` at 128; `PackageBmp` draws Mask1 by runs.
- Tests: `GameRowIconTest.cpp:154`, `GameMatchTest.cpp:2831/3339` via `MatchSupport.h iconFills`; harness lists are explicit CMake files.
- Precedent for guarded upstream hunks: `OtaUpdater.cpp` (rows 4-10 of the ledger). No tool rasterises SVG here (no cairosvg); PIL, ImageMagick and Chromium exist.

## Tasks & Acceptance

**Execution:**
- [ ] `scripts/gen_crosshatch_mark.py` + test -- render A2 to the three bitmaps, enforce the fuse check, `--check` mode -- reproducible, pixel-verified
- [ ] `src/images/CrosshatchMark120.h`, `src/games/GameMarkBitmaps.h` -- generated 120 px (Logo120 layout) and 64/128 px Mask1 -- new files only
- [ ] `src/games/GameRowIcon.*`, `GamesLauncherActivity.cpp`, `GamePicture.cpp` -- Fallback draws the mark -- default icon
- [ ] `BootActivity.cpp`, `SleepActivity.cpp` -- guarded hunks -- logo and name swap
- [ ] `docs/crosshatch/upstream-touches.md`, architecture spine -- ledger rows and spine note -- policy
- [ ] tests -- update fallback pins, add mark pixel tests

**Acceptance Criteria:**
- Given an x4pro build, when it boots or sleeps, then the mark and "Crosshatch" show in the stock layout (simulator screenshots).
- Given a game with no icon, when the launcher and title screen draw, then the mark shows at 64 and 128 px.
- Given the diff, when `check_upstream_touches.py` and `check_layers.py` run, then both pass.

## Implementation Notes

## Plan Change Log

## Review Triage Log

## Verification

**Commands:**
- `python3 scripts/check_upstream_touches.py` -- expected: exit 0
- `python3 scripts/check_layers.py` -- expected: exit 0
- `./bin/clang-format-fix` -- expected: no further changes
- host GoogleTest build and ctest (AGENTS.md command) -- expected: all pass
- `pio run -e x4pro`, `pio run -e default`, `pio check` -- expected: success, under the build lock

**Manual checks (if no CLI):**
- Desktop simulator screenshots: boot, sleep (both polarities), launcher row, title screen.
