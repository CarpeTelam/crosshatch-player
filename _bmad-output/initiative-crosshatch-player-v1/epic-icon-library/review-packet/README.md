# epic-icon-library: review packet

Built on the final epic tree, after entries 9 and 10 and the cross-story review fixes (`f27dcefd`). Every image is a
desktop-simulator screenshot (`.claude/skills/run-crosshatch-player/`, headless). The PR is
[CarpeTelam/crosshatch-player#17](https://github.com/CarpeTelam/crosshatch-player/pull/17). The owner's answers to
entry 8 are in `../story-owner-review-of-the-icon-set-and-screens-plan.md` and, dated, in the epic Notes.

## What is here

- **`icons/black-NN-<category>.png`, `icons/white-NN-<category>.png`:** every library icon (55 Phosphor 2.1.1 names,
  each in regular and fill) at small (32 px), medium (64 px, over a dithered band) and large (128 px), from the
  `icons` fixture (`test/game_script/fixtures/icons/`) on x4pro, in black and in white.
- **`icons/sheet-*.png`:** contact sheets of those pages, where present.
- **`views/{x4pro,sticky}-pause.png`:** the pause view, with `pause` in the dialog's content band, `play` on Resume
  and `sign-out` on Leave.
- **`views/{x4pro,sticky}-game-over.png`:** the game-over view, with `flag-checkered`, `arrows-clockwise` on
  Play again and `sign-out` on Leave.
- **`views/{x4pro,sticky}-error.png`:** the error view, with `warning` and `sign-out` on Back.
- **`home/{x4pro,sticky}-cover-grid-home.png`:** the cover-grid Home, with a Games tab drawn from the regular
  `game-controller` 32 px bitmap before Settings.
- **`home/{x4pro,sticky}-games-after-tap.png`:** the Games list after tapping that tab.

The name list, the reason for each name, and the attribution are in `docs/crosshatch/game-icons.md`. The source map is
`assets/game-icons/names.txt`. Every name can be previewed at https://phosphoricons.com.

## Measured deltas

Each figure is a measurement, with its method.

| Figure | Value | Method |
| --- | ---: | --- |
| Flash, games on − off, x4pro, at the epic's base `1eacdc77` | +151,088 B | `scripts/check_flash_budget.py` (entry 7's version, IRAM counted) copied onto the base tree: build on, build off, compare |
| The same at entry 7 (`8e233695`; measured on its pre-amend tree, the same code) | +196,336 B | same, fresh archive tree |
| The same at entry 9 (`9197d046`) | +227,408 B | same, from an empty `.pio` |
| The same after the review fixes (`f27dcefd`) | +227,712 B | same, on the fix worktree |
| **This epic's flash** | **+76,624 B** | +227,712 − 151,088 |
| Flash gate headroom left (250 KiB gate) | 28,288 B | `check_flash_budget.py compare` |
| Generated icon data | 71,975 B of the 96 KiB (98,304 B) cap | `nm -S` on the icon symbols plus the name strings; `GameIconBlitTest` holds the cap |
| Static internal RAM, games on − off (gate total with IRAM) | +776 B at the base and at every entry since | `check_flash_budget.py` |
| **This epic's static internal RAM** | **+0 B** | 248 B of the 1,024 B gate remain, as before the epic |

The epic-script-runtime gate counted only `.dram0.*` and `.noinit`, so it recorded +8 B. Entry 7 (retro F5) added IRAM
to the gate, which showed that epic 2's FreeRTOS task functions already took +768 B there.

Not measured: 3.2's replay time for a dithered full-canvas image, which needs a device (handed to
epic-install-and-launcher).

## Decisions

The owner answered all 27 `Assumption for entry 8:` lines on 2026-09-28. The epic Notes hold the dated
`Decision (entry 8...)` lines, and entry 8's plan summarises them.
- **Changed by the owner:** icons use exact Phosphor names; every icon comes in both weights, chosen by a
  `weight` argument; the chess pieces are dropped; the icon cap rises to 96 KiB.
- **Everything else:** approved as the orchestrator recommended.

## Deferred, with where each went

| Item | Where it went |
| --- | --- |
| Retro R10: the watchdog, timer poll and store flush pause while the light panel is open | epic-install-and-launcher Notes; needs a ledger-row decision |
| Device-side code with no host harness (icon/image replay, image spans, the `BadImage` mapping, the tab order, the R3 render gate and residual, the call sites around `vmFailure`, the image-header read-error path) | Retro AI-2's `src/games` harness, epic-install-and-launcher |
| The image-budget counting rule | epic-install-and-launcher Notes (installer and `pack_game.py`) |
| Dithered-image replay timing | epic-install-and-launcher Notes (device run) |
| An icon field on the SDK's `DialogOption` | An upstream SDK proposal, not fork work |
| Retro R4 (int16 coordinate saturation) and R8 | epic-game-api-docs Notes; settle R4 before the freeze |
| Retro A3 | epic-install-and-launcher (when `pack_game.py` imports the codec) |
| `sim.sh check` has no automated test | `deferred-work.md` `## 3.10` |
