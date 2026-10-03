---
title: 'Brand the firmware as Crosshatch: mark bitmaps, boot/sleep swap, default game icon'
type: 'feature'
ticket: ''
created: '2026-10-03'
status: 'built'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
baseline_revision: 'df3e2287065aac88ecd3eeafbf740d6b3cf626aa'
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
- [x] `scripts/gen_crosshatch_mark.py` + test -- render A2 to the three bitmaps, enforce the fuse check, `--check` mode -- reproducible, pixel-verified
- [x] `src/images/CrosshatchMark120.h`, `src/games/GameMarkBitmaps.h` -- generated 120 px (Logo120 layout) and 64/128 px Mask1 -- new files only
- [x] `src/games/GameRowIcon.*`, `GamesLauncherActivity.cpp`, `GamePicture.cpp` -- Fallback draws the mark -- default icon
- [x] `BootActivity.cpp`, `SleepActivity.cpp` -- guarded hunks -- logo and name swap
- [x] `docs/crosshatch/upstream-touches.md`, architecture spine -- ledger rows and spine note -- policy
- [x] tests -- update fallback pins, add mark pixel tests

**Acceptance Criteria:**
- Given an x4pro build, when it boots or sleeps, then the mark and "Crosshatch" show in the stock layout (simulator screenshots).
- Given a game with no icon, when the launcher and title screen draw, then the mark shows at 64 and 128 px.
- Given the diff, when `check_upstream_touches.py` and `check_layers.py` run, then both pass.

## Implementation Notes

Implemented by one subagent from this plan; one review pass (4 lenses) then 4 patches (generator hole check, exact title-band test, test cleanup, `drawMask1` placement). Evidence below was taken on the patched tree.

- **Files:** new `scripts/gen_crosshatch_mark.py` (+ test), `src/images/CrosshatchMark120.h`, `src/games/GameMarkBitmaps.h`; edited `BootActivity.cpp` and `SleepActivity.cpp` (one include and one `#if FREEINK_CAP_GAMES`/`#else` hunk each), `GameRowIcon.{h,cpp}`, `GamePicture.{h,cpp}`, `GamesLauncherActivity.cpp`, `english.yaml` (`STR_GAMES_PRODUCT_NAME`), ledger rows 11 and 12 and the architecture spine note.
- **Bitmaps:** the generator draws A2 with the fuse rule (cleared 8 px at 64, 12 px at 120, 0 at 128); the 120 px header is in Logo120's panel orientation and renders upright in the simulator.
- **Host:** 1649 of 1649 GoogleTest cases pass; every `scripts/*_test.py` passes; `check_layers.py` passes; `gen_crosshatch_mark.py --check` passes; `./bin/clang-format-fix` leaves no change.
- **Firmware:** `pio run -e x4pro` and `-e default` succeed; `pio check` (default and x4pro) finds no defects. x4pro image 5,935,328 B (flash 90.5%).
- **Flash budget gate (CI sequence, run locally):** games on vs off +255,312 B (+249.3 KiB) against 276,480 B, 21,168 B to spare; static internal RAM +784 B against 1,024 B, 240 B to spare; 46 game objects, no static initializer, largest mutable static 4 B. This is the gate's on-versus-off figure, not a before/after of this change: the three bitmaps are 4,360 B (1,800 + 512 + 2,048) of it, and no baseline-commit measurement was taken.
- **Simulator screenshots (x4pro):** [boot](crosshatch-brand-swap-screenshots/boot.png) (mark, "Crosshatch", BOOTING, not inverted), [sleep dark](crosshatch-brand-swap-screenshots/sleep-dark.png), [sleep light](crosshatch-brand-swap-screenshots/sleep-light.png), [launcher row](crosshatch-brand-swap-screenshots/launcher-default-icon.png) (Counter with no icon shows the 64 px mark; Pass art keeps its spade), [title screen](crosshatch-brand-swap-screenshots/title-default-icon.png) (128 px mark).
- **Matrix audit:** Boot and Sleep rows and the non-games-env row have no automated test (no host harness reaches those activities); they are covered by the screenshots above and the `default` build. Logged in `deferred-work.md` with the other deferral.
- **Not re-run after the commit:** `scripts/check_upstream_touches.py` reads committed history, so it is re-run once the work is committed.

## Plan Change Log

## Review Triage Log

Review pass 1 (blind-hunter, edge-case-hunter, verification-gap, intent-alignment). Counts: high 0, medium 3, low 8, false 5, maybe-false 0 (one resolved by the build). Routes: patch 3, defer 2, rejected 12. No intent_gap, no bad_plan.

| Finding | Verdict | Route | Evidence |
|---|---|---|---|
| gen check(): an ink ring-centre pixel makes `_flood_reaches_border` return False, so the generator accepts a filled hole (blind, edge) | low | patch | Real: `check()` never raises for it; only `test_each_ring_has_a_hole...` catches it afterwards. Fix: raise `RuleError` in `check()` when the ring's centre pixel is ink. |
| ModePickerTest no-title-image test asserts only `blackIn(...) > 0`, so it passes for any default icon (verification-gap) | medium | patch | Real: the title-splash 128 px mark is pinned only loosely; GameMatchTest/ResumeMatchTest use exact `markFills()`. Fix: compare the band to `match::markFills()` exactly. |
| Test cell list repeats `(48, 128)` (blind) | low | patch | Real but cosmetic: five distinct empty cells are still listed (the reviewer's "four" is wrong). Remove the duplicate. |
| `drawMask1` is a file-scope static after the file's anonymous namespace (blind) | low | patch | Real style drift; move inside `namespace {`. |
| No test exercises the Boot/Sleep swap, the `#if` direction, or the 120 px orientation (blind, verification-gap, intent) | medium | defer | Real: there is no host harness for BootActivity/SleepActivity. Evidence instead: simulator screenshots of boot and sleep (both polarities) and the default-env build, run in Verification. Automated test left in deferred-work.md. |
| Remaining CrossPoint surfaces (hotspot name, hostname, web pages, USB strings) not listed out of scope in the plan or spine (blind) | medium | defer | Real documentation gap, owner already knows (conversation 2026-10-03); logged as a follow-up. `STR_CROSSPOINT` is used only by Boot/Sleep (investigated). |
| No flash delta recorded for the new bitmaps (blind) | low | patch-by-verification | The ~4.4 KB (1800+512+2048 B) is computed; the x4pro measurement is taken the same way before and after in Verification, not in code. |
| Boot screen "Crosshatch" above `CROSSPOINT_VERSION` inconsistent (blind) | false | rejected | `CROSSPOINT_VERSION` is the version number (`-DCROSSPOINT_VERSION=${crosspoint.version}`), printed alone at h-30. |
| `libraryIcon` size not asserted (edge) | false | rejected | `uint8_t libraryIcon[GameRowIcon::BYTES]` in `GamesLauncherActivity.h`; `renderMark` copies `BYTES`. |
| `STR_GAMES_PRODUCT_NAME` wrong name/scope; i18n generator tolerance (blind) | false | rejected | Name and prefix are the owner's decision (Open Question 2). Generator tolerance is exercised by `pio run -e default` in Verification. |
| UPSTREAM_EDGES / check_layers may need `images/CrosshatchMark120.h` (blind) | false | rejected | `src/images/` is upstream, not game code; `check_layers.py` passes. |
| Plan frontmatter overclaims (blind) | false | rejected | `status`, `lenses_ran`, `baseline_revision` are workflow bookkeeping fields. |
| `FALLBACK_NAME` dead in production (blind, verification-gap) | low | rejected | Harmless; kept for the library lookup docs and tests; deleting churns tests, nobody trips on it. |
| `drawIcon` treats any other source as PackageBmp (blind) | low | rejected | Same semantics as the code it replaced; three-value enum. |
| Duplicate byte-packing loops in the generator (blind) | low | rejected | Cosmetic, fix is a refactor. |
| `.h` not `.generated.h`; `--check` ignores formatting (blind) | low | rejected | AGENTS makes `./bin/clang-format-fix` the last step every time; Logo120.h is also a plain formatted `.h`. |
| Ring-never-touches test is tautological (blind) | low | rejected | `check()` independently raises on touching pixels in `bitmap()`; the test is redundant, not wrong. |
| Hole checked at one truncated pixel; no min ring stroke; `--check` compares bytes only (edge) | low | rejected | Not demonstrated to fail; bitmaps are inspected as pixel art and in the simulator. |
| `renderLibraryIcon(nullptr)` unguarded; old non-null contract on `Choice::name` (edge) | low | rejected | Both callers branch on `source` first (checked by three reviewers); a guard adds a branch for a caller that does not exist. |
| Intent-alignment: readings B and C implemented; boot/sleep and the 120 px bitmap not exercised by tests (intent) | n/a | covered | Descriptive. Boot/sleep and orientation are verified by simulator screenshots below. |

## Verification

**Commands:**
- `python3 scripts/check_upstream_touches.py` -- expected: exit 0
- `python3 scripts/check_layers.py` -- expected: exit 0
- `./bin/clang-format-fix` -- expected: no further changes
- host GoogleTest build and ctest (AGENTS.md command) -- expected: all pass
- `pio run -e x4pro`, `pio run -e default`, `pio check` -- expected: success, under the build lock

**Manual checks (if no CLI):**
- Desktop simulator screenshots: boot, sleep (both polarities), launcher row, title screen.
