---
type: epic
title: "Games and runtime screens share one icon library"
parent: initiative-crosshatch-player-v1
covers: [CAP-8]
after: []
assignee: ""
risk: low
---

# Games and runtime screens share one icon library

## Description

Vendors the curated v1 icon set from Phosphor's fill and regular weights, generates the 1-bit icon header, and adds `ch.gfx.icon` and `ch.gfx.image`. The runtime views and the Home cover-grid Games tile switch to the library. This epic picks the v1 icon list; it is part of API level 1.

## Outcome

Games and runtime screens look like one product, and a game can ship its own images; CAP-8's success check is the signal, with the launcher part closed by epic-install-and-launcher and first-party games by epic-first-party-games.

## Requirements

This epic owns CAP-8, except the launcher rows (epic-install-and-launcher) and first-party game drawing (epic-first-party-games). Lines without a CAP id cite their source section.

- R1 (CAP-8): `assets/game-icons/` holds the chosen Phosphor 2.1.1 SVGs from `@phosphor-icons/core` in the fill and regular weights, Phosphor's MIT licence, original additions drawn in the style of the weight they join for glyphs Phosphor lacks, and a committed name map from each crosshatch name to its source file and weight; `docs/crosshatch/` records the attribution. (spine AD-24, amended 2026-09-28)
- R2 (spine AD-2, AD-24; deferred-work): `scripts/gen_game_icons.py`, on `scripts/fork_common.py` with a standard-library rasterizer, renders each icon at 32 and 64 px as 1-bit bitmaps in `GfxRenderer::drawIcon`'s layout into the committed `lib/GameIcons/GameIcons.generated.h` (read-only `inline constexpr` or one-`.cpp` data), which is never hand-edited; `.gitignore` un-ignores it, `lib/GameIcons/.clang-format` turns the formatter off, AGENTS.md's generated-files line is narrowed, and a fork CI job regenerates it and fails on any byte difference.
- R3 (CAP-8): the v1 set covers marks, card suits, dice faces, board pieces, player markers, and common controls, with lowercase `snake_case` names fixed by this epic as part of API level 1. (spine AD-24; game-api-seed section 5, Icons)
- R4 (CAP-8): `ch.gfx.icon(name, x, y, size, color)` draws a library icon top-left at `x, y` at `small` (32 px), `medium` (64 px), or `large` (128 px, the 64 px bitmap doubled); `black` or `white` sets the ink and leaves the background as drawn; the icon is clipped to the canvas and is one display-list command within the 2,048-command / 32 KB limits. (spine AD-7, AD-24; game-api-seed section 5)
- R5 (CAP-8): `ch.gfx.image(name, x, y, color)` draws `/.games/<id>/<name>.bmp`, in `PngToBmpConverter`'s 1-bit layout, opaque at its native size, `black` as converted and `white` inverted; `GameAssets` loads the images at match start into PSRAM within AD-15's 128 KB converted-image total, one named constant; `icon.bmp` is the package icon, not an image name. (spine AD-15, AD-16, AD-24; game-api-seed section 5)
- R6 (CAP-8): an unknown icon or image name is a stopping fault raised through the guard's uncatchable path (`CallGuard::raise`), so `pcall` cannot keep the game alive, and the session ends in the error view. (spine AD-14; game-api-seed section 6)
- R7 (spine AD-19): `docs/crosshatch/api-level-1.txt` gains `fn ch.gfx.icon`, `fn ch.gfx.image`, one `icon` line per name, and the image limits, with `API_SURFACE_CRC` updated; `ApiSurfaceTest` checks the icon table against the list in both directions. (epic-script-runtime handoff)
- R8 (CAP-8): the pause, error, and game-over views draw icons from the library, reached through `src/games`, so the spine's Screens row is unchanged. (spine AD-22, AD-24)
- R9 (CAP-8): the cover-grid Home shows a Games tile drawn from a `GameIcons` bitmap that opens Games, guarded by `FREEINK_CAP_GAMES` in ledger rows 6 to 9. (spine AD-3, AD-24)
- R10 (SPEC Constraints; spine AD-2, layer table, Operational envelope): every new include edge, an upstream file's included, is in the spine's layer table and `scripts/check_layers.py` in the same commit; the icons' flash delta (x4pro, games on minus games off) is measured and recorded, with its method and against epic 2's +150,448 B, before any size is quoted; static internal RAM stays within the 1 KiB gate.
- R11 (epic-script-runtime retrospective AI-11): the deferred hardening list is fixed, documented, or deferred in this epic's refactor sweep, each deferral with a reason the owner confirms in entry 8.

## Done when

1. The v1 icon set (marks, card suits, dice faces, board pieces, player markers, common controls) is vendored from Phosphor 2.1.1, fill and regular weights (amended 2026-09-28), with its licence and name map, and `scripts/gen_game_icons.py` regenerates `GameIcons.generated.h` byte-identically in the fork-only workflow, with `lib/GameIcons/.clang-format` keeping the formatter off it.
2. `ch.gfx.icon` draws every library icon at small, medium, and large, and an unknown name ends the session in the error view.
3. `ch.gfx.image` draws an image in the 1-bit layout `PngToBmpConverter` produces at its native size, and an unknown name ends the session in the error view.
4. The pause, error, and game-over views and the Home cover-grid Games tile draw from the library, and the icons' flash cost is measured and recorded.
5. Merged to `develop` with the five-env build, host suites, whole-tree format check, `pio check`, and every fork-only job, the new icon regeneration job included, all green (amended 2026-09-28).

## Boundaries

`assets/game-icons/`, the generator, `lib/GameIcons`, the two bindings, and the image path in GameAssets. Owns ledger rows 8 and 9 (the cover-grid Games tile). Not the installer's PNG conversion (epic-install-and-launcher), which produces the `.bmp` files this epic reads.

Handoffs: epic-install-and-launcher converts `icon.png` and package PNGs to the `.bmp` layout this epic reads; the icon names are part of API level 1, catalogued by epic-game-api-docs and drawn by epic-first-party-games.

## References

- parent — _bmad-output/initiative-crosshatch-player-v1/initiative-crosshatch-player-v1.md, Requirements
- architecture — _bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/ARCHITECTURE-SPINE.md, AD-2, AD-3, AD-7, AD-14, AD-15, AD-16, AD-19, AD-22, AD-24, layer table, Operational envelope
- architecture — _bmad-output/planning-artifacts/architecture/architecture-crosshatch-player-2026-09-26/game-api-seed.md, sections 5 (`ch.gfx`, Icons) and 6
- spec — _bmad-output/specs/spec-crosshatch-player/first-party-games.md, for the icons the three games need
- deferred — _bmad-output/implementation-artifacts/deferred-work.md, spine rules ahead of the code (epic-icon-library)
- retrospective — _bmad-output/initiative-crosshatch-player-v1/epic-script-runtime/epic-script-runtime-retrospective.md, AI-11 and the Addendum
- process — docs/crosshatch/orchestrated-epics.md
- constraint — docs/contributing/touch-and-ui.md

## Notes

- Decision: this epic picks the v1 icon set; epic-game-api-docs only catalogs it. AD-24 and the spec's Open Questions were amended to match (user's decision, 2026-09-26).
- Waits on epic-script-runtime because: the `ch.gfx` display list and binding registry, GameAssets, and the runtime views.
- Handoff from epic-script-runtime (entries 4 and 14): append the icon entries to `docs/crosshatch/api-level-1.txt` (updating `API_SURFACE_CRC`) and add the icon-table check to the level-1 surface test (2026-09-27).
- Pick up from `_bmad-output/implementation-artifacts/deferred-work.md` (spine rules ahead of the code, epic-icon-library): un-ignore `lib/GameIcons/GameIcons.generated.h` in `.gitignore`, add `lib/GameIcons/.clang-format` (`DisableFormat`), and narrow AGENTS.md's "every `pio run` regenerates `*.generated.h`" line (spine AD-2, AD-24; 2026-09-27).
- Source conflict: AD-24, the SPEC's Open Questions, and Done when 1 — Phosphor fill 2.1.1, vendored in `assets/game-icons/` with a new `scripts/gen_game_icons.py` — vs the inception brief naming `freeink-sdk/libs/assets/Icons/tools/gen_icons.py` and its nested Lucide submodule. Lucide is stroke-only (1,714 of 1,735 icons), the SDK generator needs an unpinned `rsvg-convert` and Pillow and writes `static const` header data that AD-2 forbids, and the SDK is not edited for fork-only work.
- Decision: Phosphor stays the one source; the SDK generator is only the reference for the bitmap layout, and the Lucide submodule and the `freeink-sdk` pointer are untouched (owner, 2026-09-28).
- Decision: the library takes Phosphor's fill and regular weights; `mark_x` and `mark_o` are the regular `x` and `circle`, for Ultimate tic-tac-toe; AD-24 and the spine's Stack and Structural Seed are amended to match (owner, 2026-09-28).
- Decision: `scripts/gen_game_icons.py` rasterizes with the Python standard library (every Phosphor 2.1.1 fill and regular SVG is one 256 viewBox of `<path>` elements, no transforms, filled with nonzero winding), so regeneration is byte-identical without native libraries (owner, 2026-09-28).
- Decision: entry 1 is the tracer bullet: four icons (`mark_x`, `mark_o` regular; `suit_heart`, `die_6` fill) go from vendored SVG through the generator, `ch.gfx.icon`, the display list, and the `src/games` blit to a simulator screenshot at small, medium, and large in black and white (owner, 2026-09-28).
- Decision: entry 2, `ch.gfx.image`, follows the tracer as the least certain piece: a new data path from `.bmp` to PSRAM to a GameScript port, where `GameAssets` and `FrameReplay` have no host harness (owner, 2026-09-28).
- Decision: three lanes. Runtime runs 1 → 2 → 4 → 5; CI runs 3 after 1; Home runs 6 after 4; the sweep, 7, follows them, and the owner's review, 8, closes the epic. Entries touch shared files only along a dependency path: 2 and 4 share `api-level-1.txt`, `ApiLevel.h`, and the fixtures README; 2 and 5 share `GameMatchActivity.cpp`; 1 and 6 share `upstream-touches.md` (6 follows 1 through 2 and 4). Entries that can run at the same time share no file other than `deferred-work.md`, which merges with `merge=union` (2026-09-28).
- Decision: the icon bitmaps use `GfxRenderer::drawIcon`'s layout (square, 1 bpp, MSB-first, byte-padded rows, 0 = ink, stored rotated 90° counter-clockwise), so the Home tile draws them with `drawIcon` and the `src/games` blit reads the same mapping (2026-09-28).
- Decision: screens reach icons only through a `src/games` helper, so the Screens row keeps no `lib/GameIcons` edge; the upstream `CoverGridHomeUi.cpp` → `lib/GameIcons` edge is added to the spine's layer table and to `check_layers.py`, which gains a rule that an upstream file includes game headers only from its ledger row (owner, 2026-09-28).
- Decision: retro F8 is settled: `API_SURFACE_CRC` covers entry lines only, and `api-level-1.txt`'s header says behaviour is contract only when it is an entry; the sweep writes that line (owner, 2026-09-28).
- Decision: retro AI-2 (the `src/games` harness) stays with epic-install-and-launcher; this epic's new `src/games` logic is pure functions with host tests (owner, 2026-09-28).
- Decision: the refactor sweep, entry 7, takes retro AI-11 except A3 (its trigger is `pack_game.py` importing the codec, in epic-install-and-launcher) and R8 (epic-game-api-docs); if its plan outgrows one session it is split into a sweep and a hardening story (owner, 2026-09-28).
- Decision: no closing device run; Done when names none, and verification is local evidence per entry with CI on the epic PR. Done when 5 is met by the orchestrator's pre-PR steps and CI (owner, 2026-09-28).
- Handoffs to epic-install-and-launcher: launcher rows draw library icons through entry 1's `src/games` helper and read `icon.bmp` through entry 2's `.bmp` reader and its 128 KB constant; `GameCore` cannot include `lib/GameIcons`, so a check of a manifest `icon` name against the library belongs outside `GameCore` (2026-09-28).
- Decision: the epic runs overnight, so owner input sits at its end. Entry 4 builds the v1 set without waiting for approval, and entry 8, after the refactor sweep, is the owner's review of the set and the screens and applies the changes asked for while level 1 is a preview; entry 8 follows the sweep, against the default that the sweep is last, because it needs the finished screens and may only rename, redraw, add, or remove icons (owner, 2026-09-28).
- Decision: during the run, the orchestrator answers a builder's open question that the repo, spine, this file, and earlier plans do not settle with the builder's recommended option when the choice stays reversible inside this epic's PR (an icon name or drawing, a layout detail, a fixture shape, a threshold), records it as a dated `Assumption for entry 8:` line in these Notes, and continues. The same holds for the build brief's other stops (the review-loop limit, an intent_gap) when the orchestrator can settle them reversibly. It stops and waits for the owner only when the choice cannot be undone inside the PR or leaves the epic's bounds: an upstream file outside the ledger, a spine decision beyond these Notes, `API_LEVEL_FROZEN`, a CI gate loosened, or data on a device (owner, 2026-09-28).
- Decision: entry 4 keeps the generated icon data within 48 KiB of flash (the spine's estimate is about 40 KB for 64 icons; 105,552 B of the 250 KiB gate remained after epic 2, and later epics need part of it) (owner, 2026-09-28).
- Decision: after entry 7, before stopping at entry 8, the orchestrator runs the pre-PR steps and opens the epic PR, so CI is fixed overnight, and builds a review packet on the combined tree, since lane worktrees and their screenshots are deleted as they finish: every icon at small, medium, and large in black and white, the pause, error, and game-over views, the cover-grid Home tile, the measured deltas, and the `Assumption for entry 8:` lines. Entry 8's changes land on the same PR (owner, 2026-09-28).
- Assumption for entry 8: the name map is `assets/game-icons/names.txt`, whitespace columns (name, source file, weight), not JSON or CSV, because a standard-library reader checks it most simply (3.1 builder's recommendation; orchestrator, 2026-09-28).
- Assumption for entry 8: the icon lookup `GameIcons::find()` is hand-written in `GameIcons.h` over generated data only (3.1 builder's recommendation; orchestrator, 2026-09-28).
- Assumption for entry 8: an unknown icon name stops the game with `ch.gfx.icon: unknown icon "<name>"` (3.1 builder's recommendation; orchestrator, 2026-09-28).
- Assumption for entry 8: the `icons` fixture shows one ink per page and a tap switches black and white, because four 128 px icons do not fit one 480 px row (3.1 builder's recommendation; orchestrator, 2026-09-28).
- Assumption for entry 8: the rasterizer's 1-bit coverage threshold is 0.5; every pip, the heart's point and the regular weight's 2 px lines show at 32 px in the 3.1 screenshots (3.1 builder's recommendation; orchestrator, 2026-09-28).
- Assumption for entry 8: the 128 KB converted-image budget (131,072 B, one named constant) counts whole `.bmp` files, headers included and `icon.bmp` left out (62 + ceil(w/32)·4·h bytes each), not decoded pixel bytes, because file bytes are what the installer and `pack_game.py` can measure (3.2 builder's recommendation; orchestrator, 2026-09-28).
- Assumption for entry 8: a game holds at most 32 images, matching AD-15's member cap; both image limits are listed in `api-level-1.txt` (3.2 builder's recommendation; orchestrator, 2026-09-28).
- Assumption for entry 8: an unknown image name stops the game with `ch.gfx.image: unknown image "<name>"` (3.2 builder's recommendation; orchestrator, 2026-09-28).
- Assumption for entry 8: only `PngToBmpConverter`'s exact 1-bit header layout is accepted; a misnamed `.bmp` is skipped with a log line rather than failing the load; and "An image is damaged or too large" (`STR_GAMES_BAD_IMAGE`) also covers more than 32 images (3.2 builder's recommendation; orchestrator, 2026-09-28).
- Assumption for entry 8: a game's sources and images share one PSRAM block, and `lib/GameCore/GameImages` owns the `.bmp` format and the image table so the launcher can reach them later (3.2 builder's recommendation; orchestrator, 2026-09-28).
- Assumption for entry 8: the v1 set is 62 names (`assets/game-icons/names.txt`, reasons in `docs/crosshatch/game-icons.md`); game tokens use Phosphor's fill weight, and controls, status icons and `mark_x`/`mark_o` use regular, the device's line style (3.4 builder's recommendation; orchestrator, 2026-09-28).
- Assumption for entry 8: `piece_king` is Phosphor's `crown-cross` and `piece_queen` is `crown`, so only `piece_pawn` and `piece_bishop` are original drawings, in the fill style (3.4 builder's recommendation; orchestrator, 2026-09-28).
- Assumption for entry 8: names describe the action where a picture could mean several things (`hint`, `show`, `hide`, `restart`, `rotate`, `leave`, `undo`, `redo`) and the picture otherwise (3.4 builder's recommendation; orchestrator, 2026-09-28).
- Assumption for entry 8: `mark_miss` is fill `waves` (fill `drop` looked like `mark_hit`'s `fire` at 32 px) and `mark_dot` is fill `dot-outline` (Phosphor's `dot` is a ring); both settled in 3.4's review (3.4 builder's recommendation; orchestrator, 2026-09-28).
- Assumption for entry 8: board pieces are fill only; outlined pieces for a second chess side are not in v1 (3.4 builder's recommendation; orchestrator, 2026-09-28).
- Assumption for entry 8: the `icons` fixture shows up to six icons a page, one category part per page (3.4 builder's recommendation; orchestrator, 2026-09-28).
- Assumption for entry 8: the runtime views use `pause` (pause view), `warning` (error view) and `flag_checkered` (game-over view), with `play`, `leave` and `restart` for the Resume, Leave and Play-again rows; the Home tile uses `game_controller` (3.4 builder's recommendation, for entries 5 and 6; orchestrator, 2026-09-28).
- Assumption for entry 8: the pause, error and game-over views draw their 64 px icon in the option dialog's content band between the text and the rows, with the SDK's spacing (16 px above, 8 px below); row icons (`play`, `leave`, `restart`) sit at the row's left edge, only when the label leaves room, because the SDK's `DialogOption` has no icon field; the error view's Back row uses `leave` (3.5 builder's recommendation; orchestrator, 2026-09-28).
- Assumption for entry 8: the Games tab sits after Transfer and before Settings in the cover-grid tab bar, where list mode puts its row, so `HomeActivity` keeps one index mapping for both Home modes (`showsGamesItem()` removed; list mode unchanged) (3.6 builder's recommendation; orchestrator, 2026-09-28).
- Assumption for entry 8: the spine's new "Upstream hooks" layer-table row and `check_layers.py`'s `UPSTREAM_EDGES` also name the upstream edges ledger rows 5 and 10 already had from epic 2 (`ActivityManager.cpp` → Games screens, `OtaUpdater.cpp` → `GameCore` and `src/games`), since the new "only from its ledger row" rule would otherwise fail them (3.6 builder's recommendation; orchestrator, 2026-09-28).
- Assumption for entry 8: retro R10 (the match's watchdog, timer poll and store flush pause while the light panel is open over a match) is documented in `docs/crosshatch/game-canvas.md` "Overlays" and deferred, because a fix needs `FrontlightPanelActivity.cpp` or more of `ActivityManager.cpp`, both outside the ledger (3.7 builder's recommended reason; orchestrator, 2026-09-28).
- Assumption for entry 8: retro R3's residual (Pause then Resume inside the Play-again gap redraws the old frame) is deferred until retro AI-2's `src/games` harness exists, because the gap is one `restart()` call (3.7 builder's recommended reason; orchestrator, 2026-09-28).
- Assumption for entry 8: the device-side code the host suites cannot build (3.7's R3 render gate and host-failure wording on the error view, 3.1's `Op::Icon` replay, 3.2's loader wiring and `BadImage` mapping, 3.6's tab order) stays covered by simulator screenshots only, deferred to retro AI-2's harness in epic-install-and-launcher (3.7 builder's recommended reason; orchestrator, 2026-09-28).
- Assumption for entry 8: 3.2's replay cost for a dithered full-canvas image stays unmeasured, because it needs a device and this epic has no device run (3.7 builder's recommended reason; orchestrator, 2026-09-28).
- Assumption for entry 8: the image-budget counting rule stays a handoff to epic-install-and-launcher's installer and `pack_game.py`, and an icon field on the SDK's `DialogOption` (3.5) is an upstream SDK proposal, not fork work (3.7 builder's recommended reason; orchestrator, 2026-09-28).
- Assumption for entry 8: retro R4 (coordinates saturate to int16 before clipping) is documented as a comment in `api-level-1.txt`, outside the contract per F8's header line, rather than fixed by clipping in wider integers or made a `limit` entry, so a later fix stays free (3.7 builder's recommendation; orchestrator, 2026-09-28).
- Assumption for entry 8: the new load-failure key reads `STR_GAMES_NOT_LOADED: "The game did not load"` (3.7 builder's recommendation; orchestrator, 2026-09-28).
- Note (2026-09-28): entry 7 fit one session, so no hardening story was split off. Its F5 fix counts IRAM in the static-RAM gate, which tightens it: games on minus off is +776 B of the 1,024 B gate (+684 B of it IRAM), leaving 248 B for later epics.
- Decision (entry 8): assumptions 1-10, 12, 16-20 are approved as written (owner, 2026-09-28).
- Decision (entry 8, answers 11, 13, 14): every library icon is named exactly as Phosphor names it, kebab-case included (`x`, `circle`, `heart`, `dice-six`, `flag-checkered`, `game-controller`, `waves`, `dot-outline`), with no category prefixes and no action names, so a developer can look each one up on the Phosphor site; games decide what an icon means. This supersedes assumptions 11, 13, 14 and 17's names (owner, 2026-09-28).
- Decision (entry 8, answer 15): every library icon ships in both Phosphor weights, regular and fill, and `ch.gfx.icon(name, x, y, size, color, weight)` takes an optional sixth argument, `"regular"` (the default) or `"fill"`. This supersedes assumption 11's per-icon weight (owner, 2026-09-28).
- Decision (entry 8): the chess pieces leave the library (`crown-cross`, `crown`, `castle-turret`, `horse`, and the originals `pawn` and `bishop`); a chess game ships its own images through `ch.gfx.image`. The library holds Phosphor icons only (owner, 2026-09-28).
- Decision (entry 8, flash option A): the generated icon data's cap rises from 48 KiB to 96 KiB (98,304 B) for the two weights; compression stays a later, API-neutral option if a later epic needs the flash. Supersedes the 48 KiB line above (owner, 2026-09-28).
- Decision (entry 8): the names-and-weights change is built here as entry 9, with bmad-build subagents, before the owner's review closes entry 8 (owner, 2026-09-28).
- Decision (entry 8, answer 7): entry 10 closes four deferred items before epic-install-and-launcher (the stale simulator block, the manifest-key surface check, the spine-vs-LAYERS test, and a pure host-failure wording mapping); the orchestrator carries the open handoffs into the downstream epics' Notes; items needing the AI-2 harness, a device, a ledger row, or an SDK change stay deferred (owner, 2026-09-28).
