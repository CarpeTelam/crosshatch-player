---
title: 'Refactor sweep'
type: 'refactor'
ticket: '13'
created: '2026-09-29'
status: built
baseline_revision: '7eee0a09a99bd8b455632c81ee6fc1d6d8f43e9b'
route: 'full'
route_source: 'auto'
review: 'thorough'
review_source: 'auto'
lenses_ran: ['blind-hunter', 'edge-case-hunter', 'verification-gap', 'intent-alignment']
review_loop_iteration: 0
context:
  - '{project-root}/AGENTS.md'
  - '{project-root}/docs/crosshatch/orchestrated-epics.md'
---

<frozen-after-approval reason="human-owned intent — do not modify unless human renegotiates">

## Intent

**Problem:** The epic-icon-library retro's AI-11 list, the open `deferred-work.md` items (`## e3r-1`, `## e3r-2`, `## e3r-x`, `## 4.1` to `## 4.12`, `## 4.15`), `cross-story-review-e3r.md` findings 12 and 13, and retro R10 are still open (R14). The epic also needs its final flash and static-RAM figure against the base (R13) and a fixture for entry 14's frame timings.

**Approach:** Take each item as fixed, documented, or deferred with a reason (table below), in one commit. Fix what the ticket's `touches` allows and a host test can pin; defer what needs an upstream file outside ledger rows 2 and 5, a new limit or UX choice (the owner's), a `src/` seam, or a device. Add `test/game_script/fixtures/timing/` and measure the epic's delta with the four `check_flash_budget.py` steps.

## Boundaries & Constraints

**Always:** Only the files each item names, `deferred-work.md` (its `## 4.13` section, and in-place "Resolved" marks on the items it closes, as earlier stories did), `test/game_script/fixtures/timing/**`, and the fixtures README. Keep every guard of a function this touches (Design Notes). Each deferral has a reason and an `Assumption for entry 14:` line below.

**Never:** An upstream file outside ledger rows 2 and 5 (R8 (d) stays deferred), `API_LEVEL_FROZEN`, `.github/workflows/ci.yml`, the `freeink-sdk` pointer, a loosened CI gate, and data on a device put at risk. No new bound for filled area (an API and spine change).

## I/O & Edge-Case Matrix

| Scenario | Input / State | Expected Output / Behavior | Error Handling |
|----------|--------------|---------------------------|----------------|
| Continue over a save the card will not read | `peek` finds the file, the open or read fails | `Unreadable`: the Continue row stays; the tap ends in the error view, `resume.bin` untouched | logged; no new match |
| Continue over no usable save | file read and refused (other package, corrupt) | `None`: no row; a resume that reaches the match starts new | logged |
| Tap during the new round's first frame | after Play again, or before the match's first frame | dropped until `renderCanvas` has drawn and displayed it | none |
| Unknown icon name with invalid UTF-8 | lone lead or continuation byte, or a cut sequence | each such byte is `?` in the error text | none |
| `icons` CI job, stale `GameIconsRaw.h` | reference differs from a fresh run | job fails, annotated for that file | none |

</frozen-after-approval>

## Code Map

- `lib/GameIcons/.clang-format` (deleted), `scripts/gen_game_icons.py`, `lib/GameIcons/GameIcons.generated.h` -- R8 (b): the generated header opens with `// clang-format off` (as `GameIconsRaw.h` does), so `GameIcons.h` is formatted.
- `src/activities/games/GameMatchActivity.cpp/.h`, `src/games/GameViewIcons.h` -- R8 (e), R9 (g) ink rule; the tap gate (`roundsDisplayed`, `roundsStartedAwaited` starts at 1); `seedResume` returns whether the match may go on.
- `lib/GameScript/ChBindings.cpp` -- R9 (g): `unknownName` keeps only well-formed UTF-8.
- `src/games/BlitClip.h` (new), `GameIconBlit.h`, `GameImageBlit.h` -- A2's shared clip.
- `test/game_script/harness/ConverterLayoutTest.cpp` (new, in `GameInstallerTest`) -- A2's link: real `PngToBmpConverter` output against `test/game_core/ConverterBmpLayout.h`.
- `src/games/GameSaveStore.cpp/.h`, `GamesLauncherActivity.cpp/.h`, `english.yaml` -- `peek` answers `SaveState`; Continue row kept for `Unreadable`; `STR_GAMES_RESUME_FAILED`; `REMOVE_HOLD_MS` public.
- `scripts/check_layers.py`, `.claude/skills/run-crosshatch-player/sim.sh`, `scripts/pack_game.py`, `.github/workflows/crosshatch-ci.yml` (job `icons-up-to-date`) -- findings 13 and 12, `## 4.2`, `## 4.15`, each with a sidecar test.
- `test/game_script/harness/screen_stubs/MappedInputManager.h`, `save_store_stubs/HalStorage.h` -- held-button double; read-fault knobs.
- `docs/crosshatch/formats.md`, `game-canvas.md`, `game-icons.md` -- stale line, `GameRowIcon` section, R9 (f) wording, resume rules.
- `test/game_script/fixtures/timing/` (new), fixtures `README.md`, `GfxBindingsTest`, `GamePackageInstallerTest`, `ManifestTest` -- entry 14's frames.

## Tasks & Acceptance

**Execution:**
- [x] Fix the items marked fixed below, each with a test that fails without it.
- [x] Document R9 (f), the R9 (g) view-icon ink, and `## 4.11`'s kept save.
- [x] Defer the rest in `deferred-work.md` `## 4.13`, with the `Assumption for entry 14:` lines below.
- [x] Add the `timing` fixture, its README section, and the host tests that pin its bands.
- [x] Measure with the four `check_flash_budget.py` steps; record the figure (Verification).

**Acceptance Criteria:**
- Given the verify commands, when run, then host suites, every `scripts/*_test.py`, `check_layers.py`, `check_upstream_touches.py`, `pio check`, formatting twice, and the five env builds pass.
- Given `pack_game.py timing`, when run, then it packs, and the timing bands draw exactly 1,048,576 icon and image pixels and 2,048 rects on the host.

## Disposition of every item

**Fixed**

| Item | Fix |
| --- | --- |
| R8 (b) | Generated header opens with `// clang-format off`; `lib/GameIcons/.clang-format` deleted; `gen_game_icons_test` pins both. Formatting run once from a fresh archive tree. |
| R8 (e), R9 (g) ink | `GameViewIcons::labelIsBlack`: a solid paint's colour, else the label style's own (how FreeInkUI colours the text); the 64 px view icon follows the panel's own foreground. `AnIconTakesTheInkItsLabelDrawsIn`. |
| R9 (g) lone lead byte | `unknownName` copies only well-formed sequences, `?` for the rest; icon and image cases in `GfxBindingsTest`. |
| A2 clip, converter link | `BlitClip::visible` used by both blits (`BlitClipTest`); `ConverterLayoutTest` builds the real converter and compares its bytes with `ConverterBmpLayout.h` for 50 size and pattern pairs. |
| `## e3r-2` frame-displayed signal, start-of-match await | See Design Notes. `GameMatchTest`: tap between publish and draw dropped; tap before the first frame dropped. |
| Findings 12, 13 | `sim.sh` ignores trailing blanks and CR on marker lines and drops the dead end-marker check; `check_layers.py` says why an include after a spanning-comment guard is unguarded. |
| `## 4.2` | `pack_game.py` walks PNG chunks (CRC, IEND, IDAT inflating to the exact size) and refuses manifest nesting over 32. |
| `## 4.15` | `Icons up to date` regenerates and compares `GameIconsRaw.h` too. |
| `## 4.10` held buttons | Threshold-aware `hold`, `wasLongPressed`, `consumeSuppressedRelease` in the shared double; `REMOVE_HOLD_MS` public; hold at and one short of it, held next and previous keys. |
| `## 4.12` peek | `SaveState` tri-state, Continue kept for `Unreadable`, the match stops in the error view instead of replacing a save it could not read or the VM refused; other-package log at INF; stale comment. |
| `## 4.8`, `## 4.7` | `formats.md` line, `game-icons.md` `GameRowIcon` section; `iconWeight` was read since entry 8 (marked). |

**Documented:** R9 (f) (a heap error in a callback is the game's error in Lua's words, `game-canvas.md`); `## 4.11`'s kept save (`formats.md`); `## 4.12`'s "bad between listing and tap"; `## e3r-1`'s budget fault (already in seed and spine, checked).

**Deferred** (each in `## 4.13`): `## e3r-x` inert pause menu; `## e3r-1` unbounded fills; R10; `## 4.10` unlisted folder; `## 4.6` extracted sum; `## 4.11` first-call rejection; the two `bool` reason codes (ZipFile, PngToBmpConverter); `## 4.3` first failure only; `goHome` mapping test; the `## 4.4`/`## 4.1` seams; modes and pass-and-play Continue; device measurements; row-height and queued-tap assumptions.

**Assumption for entry 14:** `## e3r-x`'s inert pause menu: deferred because a new string and view state are a UX choice; recommend a one-line "Starting the next round" under the paused headline.
**Assumption for entry 14:** `## e3r-1`'s unbounded fills (`rect`, `clear`, `circle`): deferred to the `timing` fixture's band 3 (2,048 full-canvas rects), since a bound is a new limit before the freeze; the run decides.
**Assumption for entry 14:** Retro R10 (watchdog, timer poll, store flush pause under the light panel): deferred with the trigger "the next ledger change" (`FrontlightPanelActivity.cpp`), as decided at inception; R8 (d) waits for the next row-8 touch.
**Assumption for entry 14:** `## 4.10`'s unlisted folder after a partial remove: deferred because a sweep deletes hand-copied folders; recommend a `.removing` marker written before the `.pkg` goes, and finish only marked folders.
**Assumption for entry 14:** `## 4.6`'s extracted-sum bound: deferred because a sum is a new limit in the contract; recommend a free-space check from the directory pass's declared sizes.
**Assumption for entry 14:** `## 4.11`'s save kept after a rejection at the first call: deferred because a heap error is a script error, so deleting could lose a good save; trigger a distinct run-time out-of-memory failure kind.
**Assumption for entry 14:** `ZipFile` and `PngToBmpConverter` bare-`false` reasons: deferred because both are upstream files without a ledger row; recommend one row and an enum beside the `bool` API.
**Assumption for entry 14:** `## 4.3`'s only-the-first-failure popup: deferred as a UX change; recommend "and N more" from the counted `Report.failed`.
**Assumption for entry 14:** The `goHome` mapping test and the `## 4.4`/`## 4.1` drift guards and layout pins: deferred because each needs an upstream edit beyond row 5 or a `src/` seam; add at the next touch, and use `sim.sh ss` diffs for layout.
**Assumption for entry 14:** Mode passing and pass-and-play Continue: deferred to epic-pass-and-play and epic-play-nearby, unreachable while `pass` and `nearby` are off.
**Assumption for entry 14:** Device measurements (resume write cost, `peek` time at entry, how much of the panel's refresh is behind `displayBuffer`, `GameHash`'s mbedTLS read-back of `package_vector.cpgame` as `v1` then `0530a15766e91bf1`): entry 14's steps; the fixtures README's "Timing run" lists them.
**Assumption for entry 14:** `## 4.10`'s fixed row height and a queued second Remove tap: deferred, not shown at the shipped fonts; trigger a theme change or a device report.

## Implementation Notes

Built directly by the build agent (route `full` by size, no implementation subagent; the orchestrator's brief asks for it to be done in the foreground). Several WIP commits during the session were squashed into the one commit at the end.

## Plan Change Log

None.

## Review Triage Log

Pass 1. The four lenses ran as context-free subagents over `git diff 7eee0a09..HEAD` (the generated icon header and this plan left out of the diff file) and all returned: blind hunter (13 findings), edge-case hunter (5), verification-gap (3 gaps), intent-alignment (descriptive, no findings). Counts: 0 high, 1 medium, 12 low, 4 false or rejected as too costly for a cosmetic gain, 1 deferred; patched in the follow-up edit of this commit.

| Lens | Finding | Verdict | Route and evidence |
| --- | --- | --- | --- |
| blind | The icon header is not regenerated | false | The diff file left the generated header out; HEAD's `GameIcons.generated.h` opens with `// clang-format off` and `CommittedTest` and the CI step's local run pass. |
| blind, edge | `utf8SequenceLength` passes overlong forms, surrogates, and code points past U+10FFFF | low | patch: second-byte ranges for E0, ED, F0, F4; nine cases in `GfxBindingsTest`. |
| blind | New tests split `PlayAgainGapTest`'s comment from its class | low | patch: moved above the comment. |
| blind | `sim.sh` leaves a CRLF block "stale" and writes an LF block among CRLF lines | low | rejected: `setup` then makes it current (the test says so); mixed endings in a scratch settings file harm nothing, and a fix adds an `\r` strip to a comparison. |
| blind | `loadResume`'s `bool*` is optional, so a caller can forget the difference this story adds | medium | patch: the out-parameter is a required reference; the one caller and the tests pass it. |
| blind, verification | The view icon's ink is read from the button text style, and only the pure rule is tested | low | patch: the fallback is the headline's style; `ThePauseViewsIconsTakeTheInkOfTheLabelsBesideThem` checks the wiring on the recording renderer (FreeInkApp's default theme draws a focused row in black, so it pins that the icon reads the label's resolved ink, not a constant). |
| blind | The resume error view: reused headline, detail length, endless "try Continue again" | low | rejected: 111 bytes against `ERROR_CAPACITY` 160; the headline is the one every start failure shows; the text names the way out (the game's own row). |
| blind | `roundsDisplayed` is stored when `drawFront` returns false | low | rejected: the first frame of a round is drawn on a cleared screen after `forceFull`, so replay cannot skip it; a skipped identical frame was displayed already; the comment says so. Storing it is what keeps a return from dropping taps for good. |
| blind, edge | `png_problem` leaves gaps: no PLTE check, bytes after IEND, chunk order | low | patch for the palette-without-PLTE case (`test_a_palette_image_needs_its_plte_first`); rest rejected and listed as "not checked" in its docstring (decoders ignore trailing bytes). |
| blind | `json_nesting` has no negative-depth guard and `MAX_NESTING` is not tied to the C++ constant | low | patch: the test reads `StreamingJsonParser.h`'s constant; the depth guard is rejected (it runs only after a successful parse, so brackets balance). |
| blind | `deferred-work.md` grows and is hard to use | low | rejected: in-place "Resolved by ... It read:" is the file's convention. |
| blind | The CI edit has no fresh-tree run and its `:`-split loop is fragile | low | patch: a `same()` function; fresh-tree runs are in Verification. |
| blind | Held-key tests hard-code 600 ms, a garbled assertion message, one `heldMs` for all buttons | low | patch for the message and a comment on the double; the 600 ms rejected (`ButtonNavigator`'s defaults are 500 and 500). |
| edge | `formats.md` and the `seedResume` comment say `Session::restore` applies the game's own rules | low | patch: both say the VM's refusal cannot happen today. |
| edge | `peek`'s comment says "per game", the launcher asks per startable game | low | patch. |
| edge | `labelIsBlack` falls back to one base text style for every state | false | FreeInkUI has one `buttonText` for all states; a state changes the paint only, and a solid paint's colour replaces the text colour. |
| verification | The chunk-walk size formula is tested only against the helper's own table | low | patch: literal sizes for colour types 0, 2, 3, 4, 6 at several depths, and the timing fixture's real `gray.png`. |
| verification | No test drives the VM-refusal branch of `seedResume` | low | defer: unreachable (`setResume` refuses what `peek` excludes), as `## 4.12`'s marked entry says; a test needs a seam in `GameVM`. |
| intent | (descriptive) R13's figure is not in the diff; the intent's surfaces are the device and CI, the tests run on doubles | none | The figure is in Verification below and the report. The device, CI, and fresh-tree items are entry 14's and the epic PR's; the plan says which. |

## Design Notes

**Tap gate.** `roundsStartedAwaited` starts at 1 (the match's first round) and Play again stores `roundsStarted() + 1`, as before. `renderCanvas` reads `roundsStarted()` once, and after its draw (`displayBuffer` included, or nothing drawn when replay skipped an identical frame) stores it in `roundsDisplayed` with release. `loopPlaying` drops gestures until `roundsDisplayed` reaches the awaited count, and keeps asking for no render only while `roundsStarted()` is below it. Guards kept in the functions changed (checked with `git log -L`, commits `acf780ea`, `d4f889f0`, `bd176acd`, `d45dd71d`): in `renderCanvas` the `!vm` return, the gap skip with `viewOnScreen = true` (e3r-x), the clear and `forceFull` after a view, `forceFull` for a repaint with no new frame, and `renderedFrame` stored before the draw (the loop must not ask again) all stay; `drawFront`'s false still skips `displayBuffer`. In `loopPlaying` the order of Back, `vmHealthy`, `roundsEnded`, the gesture read (which consumes the contact), `pollTimer`, `flushIfDue`, `flushResume`, and the frame request is unchanged. A stuck gate would drop every tap, so every path past the gate stores `roundsDisplayed`; the first frame of a round is never skipped as identical because the screen was cleared for it.

**Save safety.** `GameSaveStore::peek` keeps its guards: the `resume.bin`, else `resume.bin.tmp` choice (a whole tmp is the only copy), the per-call buffer with a null check (`new (std::nothrow)`), and the file staying on every refusal. The buffer failure now answers `Unreadable`, not "no save". `seedResume` keeps "no usable save starts new" (nothing is lost) and stops otherwise; Error never writes `resume.bin` (`resumeWritable` false). `setResume`'s refusal is unreachable today (it refuses what `peek` excludes) and the branch is documented as such.

**Clip helper.** `inkRuns` and `runs` keep the int64 arithmetic, the empty-rectangle early return, and the row-decode-above-the-canvas rule; only the four values move into `BlitClip::visible`.

**Timing fixture.** Canvas-generic: it works out the icon count and one-row strips from `ch.screen`, so band 2 is exactly 1,048,576 pixels on 474 x 788 (2 images, 18 icons, 14 full strips and one of 4 px) and on the test canvases. `gray.png` is 8-bit grayscale 128; the converter's ordered dither makes about half the pixels white with runs of one or two.

## Verification

Firmware and measurement were run on a WIP commit of this session, since squashed into this one; `git diff` between the two shows only this plan, the screenshots, and the fixtures README changed, so the code (and every firmware byte) is this commit's, and the measurement is this commit's (the base is `962ae61`, entry 12's `ee26fa2f`). Every build and check below ran under the shared lock.

**Commands** (all passed):
- Host suites: `cmake -S test -B build/test -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j` -- 1297 of 1297 (1274 at the base `7eee0a09`; this story adds 23 tests). About one full parallel run in eight fails one launcher test that starts a match; the base has the same flake (3 failing runs of 20 at `7eee0a09` from an archive tree, 2 of 12 here) and it passes alone: recorded in `## 4.13`.
- Mutations that the new tests catch (each reverted): `wasLongPressed(Confirm, REMOVE_HOLD_MS / 2)` fails `ConfirmHeldToRemoveHoldMsAsksOnce...` and `ConfirmReleasedBeforeTheHold...`; `awaitingRound` in place of `awaitingDisplay` for gestures fails `ATapBeforeTheFirstFrameIsDrawn...`. Not caught, and shown equivalent: walking `listCount()` in the continuous lambdas (`## 4.10`, marked).
- Every `scripts/*_test.py`: `check_api_freeze` 9, `check_flash_budget` 78, `check_layers` 52, `check_upstream_touches` 18, `fork_common` 27, `fork_release` 76, `game_codec` 22, `gen_game_icons` 49, `pack_game` 80, `sim_sh` 5 -- all OK. `python3 scripts/check_layers.py`: 468 edges in 107 files, passed. `python3 scripts/check_upstream_touches.py`: PASS (trial merge of `upstream/develop` clean).
- `pio run -e x4pro`, `default`, `sticky`, `x4c`, `papermono`: all five SUCCESS on that tree. `sim.sh build x4pro`: SUCCESS.
- `LD_LIBRARY_PATH=<libpcre dir> pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high`: PASSED (default env).
- `./bin/clang-format-fix` twice: `git status` shows nothing new. Formatting-only changes it made were to this story's own files. **Fresh archive tree** of `HEAD` (`git archive`, then `git init` there so the wrapper's `git ls-files` sees it; no submodules, since formatting reads none): the first run changed nothing (R8 (b): `lib/GameIcons/GameIcons.h` is formatted and the generated header is left alone by its own marker).
- **The `Icons up to date` step from the same fresh tree** (its shell run with `RUNNER_TEMP` set, Python 3.11 here, CI uses 3.13): matched both files; and with a byte appended to each committed file it failed with two annotations and diffs. The step's `pack_game_test`, `gen_game_icons_test`, `check_layers_test`, and `sim_sh_test` also passed from that tree.

**Flash and static RAM** (`python3 scripts/check_flash_budget.py build on`, `build off`, `compare --limit-kib 250 --ram-limit-bytes 1024`, `objects`, each under the lock, on that tree; x4pro, games on minus off, both built from that one tree):

| | Games on | Games off | Difference | Base (`962ae61`) | Over the base | Pass bar |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `firmware.bin` | 5,909,040 B | 5,678,752 B | +230,288 B | +228,496 B | +1,792 B | 12,000 B |
| static internal RAM | 187,848 B | 187,064 B | +784 B | +776 B | +8 B | 32 B |

25,712 B under the 256,000 B flash gate (10,208 B under the pass bar's +240,496 B); 240 B under the 1,024 B RAM gate (24 B under +808 B). `.dram0.bss` +16 (base +8), `.iram0.text` +684, `.iram0.text_end` +84. `objects`: 43 game objects, no static initializer, the largest mutable static 4 B (`lastOpened`). Entry 12's final tree `ee26fa2f` measured +229,792 B and +784 B the same way; this story adds +496 B of flash (the tap gate, `SaveState`, the UTF-8 filter, the ink helper, one string) and nothing in RAM. The epic's final delta is these figures.

**Screenshots** (simulator, x4pro, `timing.cpgame` packed by `pack_game.py` and opened from Games; the log line `band 2 charges 1048576 pixels` appeared):
- `_bmad-output/initiative-crosshatch-player-v1/epic-install-and-launcher/story-timing-screenshots/menu.png` -- the fixture's menu, the baseline frame.
- `.../band1-gray-image.png` -- band 1: `gray.png` at the canvas's top-left, dithered to a 50% ordered pattern by the converter.
- `.../band2-1048576-pixels.png` -- band 2: two gray images with eighteen 128 px white fill circles and the bottom strips, exactly 1,048,576 pixels.
- `.../band3-2048-rects.png` -- band 3: 2,048 full-canvas rects, the last `dark`, so the screen ends in the dark dither.

The simulator's replay times (a few milliseconds) say nothing about the device: entry 14's run times them.

**Manual checks:** none beyond the above. Not run here and left to the epic PR's CI and entry 14: `ci.yml`, the fork jobs on a real runner, and the device.
