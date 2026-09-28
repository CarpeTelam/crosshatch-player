# epic-icon-library: review packet for entry 8

Built on the combined epic tree after entry 7 (`720fe299`), for the owner's review in entry 8 (hitl). Every image
is a desktop-simulator screenshot (`.claude/skills/run-crosshatch-player/`, headless). The PR is
[CarpeTelam/crosshatch-player#17](https://github.com/CarpeTelam/crosshatch-player/pull/17).

## What is here

- `icons/black-01-marks.png` … `icons/black-13-status.png`: all 62 library icons, from the `icons` fixture
  (`test/game_script/fixtures/icons/`) on x4pro. Each page shows one category's icons at small (32 px), medium
  (64 px, over a dithered band) and large (128 px), with their names. Pages: marks, suits, dice, pieces 1–2,
  markers 1–2, controls 1–5, status.
- `icons/white-01-marks.png` … `icons/white-13-status.png`: the same 13 pages in white ink on black.
- `views/{x4pro,sticky}-pause.png`: the pause view. The `pause` icon sits in the dialog's content band, with
  `play` on Resume and `leave` on Leave.
- `views/{x4pro,sticky}-game-over.png`: the game-over view. It shows `flag_checkered`, with `restart` on
  Play again and `leave` on Leave.
- `views/{x4pro,sticky}-error.png`: the error view ("main.lua:2: boom"). `warning` sits between the message and
  Back, with `leave` on Back.
- `home/{x4pro,sticky}-cover-grid-home.png`: the cover-grid Home. The Games tab is the `game_controller` 32 px
  bitmap, before Settings.
- `home/{x4pro,sticky}-games-after-tap.png`: the Games list after tapping that tab.

The name list, with the reason for each name and the attribution, is in `docs/crosshatch/game-icons.md`. The
source map is `assets/game-icons/names.txt`.

## Measured deltas

Each figure is a measurement, with its method.

| Figure | Value | Method |
| --- | ---: | --- |
| Flash, games on − off, x4pro, at entry 7 (`8e233695`) | +196,336 B | `scripts/check_flash_budget.py` (x4pro built twice, `FREEINK_CAP_GAMES` on and off) |
| Flash, the same, at entry 4 (`7e54d5d6`) | +196,480 B | same |
| Flash, the same, at entry 4's base (`068a9ad0`, entries 1–3) | +158,224 B | same |
| Flash, epic 2's figure | +150,448 B | as recorded in epic-script-runtime |
| **This epic's flash**, games on − off (entry 7 minus epic 2) | **+45,888 B** | difference of the two rows above |
| The v1 set alone (entry 4 minus its base) | +38,256 B | same |
| Flash gate headroom left (250 KiB gate) | 59,664 B | `check_flash_budget.py compare` at entry 7 |
| Generated icon data | 40,964 B of the 48 KiB (49,152 B) cap | `nm -S` on the icon symbols (40,424 B) plus the 540 B name strings; `GameIconBlitTest.TheIconDataFitsIn48KiB` |
| Tracer (entry 1) x4pro firmware growth | +3,916 B flash, +0 B static RAM | `pio run -e x4pro` size, `1eacdc77` vs `b562ad3c` |
| `ch.gfx.image` (entry 2) x4pro firmware growth | +3,316 B flash, +0 B static RAM | `pio run -e x4pro` size, entry 1 vs `890c1a69` |
| Static internal RAM, games on − off, DRAM sections only | +8 B | `check_flash_budget.py` |
| Static internal RAM, the gate total now that IRAM counts (entry 7, retro F5) | +776 B of 1,024 B (IRAM +684 B), leaving 248 B | `check_flash_budget.py` at entry 7 |

Not measured: the games on − off figure at the epic's base `1eacdc77`, and 3.2's replay time for a dithered
full-canvas image, which needs a device.

## Assumptions for entry 8

The orchestrator settled each of these overnight with the builder's recommended option, because each stays
reversible inside this PR. They are copied verbatim from the epic file's Notes; please confirm or change each one.

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

## Entry 7's deferrals and their recommended reasons

The same items appear above as Assumption lines. They are listed here with their outcome. Entry 7 fixed or
documented every other AI-11 item (F5, F6, F7, F8, F9, A4, A5, R3, R4 documented, R6, R9); its plan,
`story-refactor-sweep-plan.md`, has one row per item.

| Item | Outcome | Recommended reason |
| --- | --- | --- |
| Retro R10: the watchdog, timer poll and store flush pause while the light panel is open over a match | Documented in `docs/crosshatch/game-canvas.md` "Overlays"; the fix is deferred | A fix needs `FrontlightPanelActivity.cpp` or more of `ActivityManager.cpp`, both outside the ledger |
| Retro R3 residual: Pause then Resume inside the Play-again gap redraws the old frame | Deferred | The gap is one `restart()` call; fix it once retro AI-2's `src/games` harness exists |
| Device-side code the host suites cannot build: entry 7's R3 render gate and host-failure wording, 3.1's `Op::Icon` replay, 3.2's loader wiring and `BadImage` mapping, 3.6's tab order | Covered by simulator screenshots only | Waits for retro AI-2's harness (epic-install-and-launcher) |
| 3.2's replay cost for a dithered full-canvas image | Unmeasured | Needs a device; this epic has no device run |
| 3.2's image-budget counting rule | Handoff | The installer and `pack_game.py` (epic-install-and-launcher) must count the same way |
| 3.5: an icon field on the SDK's `DialogOption` | Upstream proposal | The SDK is not edited for fork-only work (AGENTS.md) |
| Retro R4: coordinates saturate to int16 before clipping | Documented as a comment in `api-level-1.txt`, outside the contract | Keeps a later fix free before the freeze |
| Retro A3 | Left to epic-install-and-launcher | Its trigger is `pack_game.py` importing the codec |
| Retro R8 | Left to epic-game-api-docs | It is a line in the API docs |
