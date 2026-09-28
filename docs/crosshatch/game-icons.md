# Game icons

The game icon library: the icons a game draws with `ch.gfx.icon(name, x, y, size, color)` and a screen with
`drawGameIcon` (`src/games/GameIconDraw.h`). Each icon is a 1-bit bitmap at 32 px (`small`) and 64 px (`medium`);
`large` (128 px) draws the 64 px bitmap doubled. The names are part of the game API level: `api-level-1.txt` lists
each one as `icon <name>`, and `ApiSurfaceTest.IconsMatchTheList` checks that list against the library both ways.

## Where the icons come from

- **Phosphor 2.1.1.** Every icon but two is a Phosphor Icons SVG from the npm package `@phosphor-icons/core`, version
  2.1.1 (`https://registry.npmjs.org/@phosphor-icons/core/-/core-2.1.1.tgz`, sha256
  `313332be6190b724da24107addd781799b48bf76b13963f24501112ffe1baadd`), copied byte for byte from the package's
  `assets/fill/` and `assets/regular/` into `assets/game-icons/phosphor/fill/` and `phosphor/regular/`. Phosphor Icons
  is Copyright (c) 2023 Phosphor Icons, under the MIT licence; `assets/game-icons/phosphor/LICENSE` is the package's
  licence file, and the generated header repeats the copyright line.
- **Originals.** Phosphor has no chess bishop or pawn, so `piece_bishop` and `piece_pawn` are this project's own
  drawings in Phosphor's fill style, in `assets/game-icons/original/fill/`, under this repository's MIT licence
  (`LICENSE`). Each is one `<svg viewBox="0 0 256 256" fill="currentColor">` of `<path d>` elements, like
  Phosphor's, standing on the rook's (`castle-turret`) base bar; every contour winds clockwise except the bishop's
  mitre slit, which winds the other way so the nonzero fill cuts it.

`assets/game-icons/names.txt` maps each name to its SVG and weight. `python3 scripts/gen_game_icons.py` renders them
into `lib/GameIcons/GameIcons.generated.h`, which is never edited by hand; the `Icons up to date` fork CI job
regenerates it and fails on any byte difference.

## Weights

- **Fill** for game tokens: the suits, dice, pieces, player markers, and the `mark_dot`, `mark_hit`, and `mark_miss`
  marks. Solid shapes read best at 32 px on a 1-bit screen.
- **Regular** (2 px lines at 32 px) for `mark_x` and `mark_o` (the owner's decision, for Ultimate tic-tac-toe), and
  for controls and status icons, which sit beside the device's line-style UI icons.

## Naming rules

- Lowercase `snake_case`, `[a-z][a-z0-9_]{0,31}`, no `__`, no final `_`; the generator refuses any other name, since
  each becomes a C++ identifier (`MARK_X_32`, `MARK_X_64`).
- Game tokens take their category's prefix: `mark_`, `suit_`, `die_`, `piece_`, `marker_`. A control or status icon
  is named for its action where the picture could mean several things (`hint`, `show`, `hide`, `restart`, `rotate`,
  `leave`, `undo`, `redo`), and otherwise for the picture (`pencil`, `trash`, `timer`, `trophy`, `warning`).
- One name per glyph: no alias for an icon already in the set (a `close` would be `mark_x` again).
- Level 1 is a preview until `API_LEVEL_FROZEN`: names may still change. Once a level is frozen, no name in it is
  renamed or removed; a new icon joins the next level.

## The v1 set (62 icons)

The epic's six categories are marks, card suits, dice faces, board pieces, player markers, and common controls; the
table lists the common controls in two groups, Controls and Status. The pieces are solid (fill) only: a game tells a
second side apart by the ink on a contrasting square, and outlined pieces are not in v1.

| Category | Name | Source (weight) | Reason |
| --- | --- | --- | --- |
| Marks | `mark_x`, `mark_o` | `x`, `circle` (regular) | Noughts and crosses, Ultimate tic-tac-toe (owner's decision, tracer) |
| | `mark_dot` | `dot-outline` (fill) | A small solid dot: last move, a legal-move hint, a board point (Phosphor's fill `dot` is a ring) |
| | `mark_hit`, `mark_miss` | `fire`, `waves` (fill) | Battleship's hit (flame) and miss (a water tile); a drop was too like the flame at 32 px |
| Card suits | `suit_club`, `suit_diamond`, `suit_heart`, `suit_spade` | `club`, `diamond`, `heart`, `spade` (fill) | The four suits |
| Dice faces | `die_1` … `die_6` | `dice-one` … `dice-six` (fill) | Every face, for dice games and random choices |
| Board pieces | `piece_king` | `crown-cross` (fill) | The chess king's cross-topped crown |
| | `piece_queen` | `crown` (fill) | A ball-tipped coronet, like the Staunton queen's |
| | `piece_rook`, `piece_knight` | `castle-turret`, `horse` (fill) | The rook's turret and the knight's horse head |
| | `piece_bishop`, `piece_pawn` | original (fill style) | Phosphor has neither; silhouettes on the rook's base bar |
| | `piece_ship` | `boat` (fill) | Battleship's ship |
| Player markers | `marker_circle`, `marker_square`, `marker_triangle`, `marker_star`, `marker_hexagon` | same names (fill) | Distinct solid tokens for seats and counters; shapes, not shades, since the screen is 1-bit |
| | `player`, `players` | `user`, `users` (fill) | One seat (whose turn); the pass-the-device and nearby prompts |
| Controls | `arrow_left`, `arrow_right`, `arrow_up`, `arrow_down` | `arrow-*` (regular) | Paging and direction |
| | `undo`, `redo` | `arrow-u-up-left`, `arrow-u-up-right` (regular) | Take back and replay a move |
| | `restart` | `arrows-clockwise` (regular) | Play again, new round |
| | `rotate` | `arrow-clockwise` (regular) | Battleship's rotate control |
| | `shuffle` | `shuffle` (regular) | Random placement, shuffle a deck |
| | `play`, `pause` | same names (regular) | Resume and pause |
| | `check`, `plus`, `minus` | same names (regular) | Confirm; add and remove, steppers |
| | `info`, `help`, `hint` | `info`, `question`, `lightbulb` (regular) | Rules, help, a hint |
| | `pencil`, `eraser` | `pencil-simple`, `eraser` (regular) | Sudoku's pencil marks and clearing a cell |
| | `show`, `hide` | `eye`, `eye-closed` (regular) | Hidden information: show a hand or board, look away before a pass |
| | `home`, `settings`, `leave`, `trash` | `house`, `gear-six`, `sign-out`, `trash` (regular) | Launcher and menu actions; `leave` matches the runtime's Leave |
| | `timer`, `hourglass` | same names (regular) | `ch.timer` countdowns; waiting for the other player |
| | `game_controller` | `game-controller` (regular) | The Home Games tile; line style like the Home tiles' icons |
| Status | `warning`, `flag_checkered` | `warning`, `flag-checkered` (regular) | The error view and the game-over view (a finish, neutral for a win or a loss) |
| | `trophy`, `smiley`, `smiley_sad` | same names (regular) | A win; a result face either way |

The `icons` fixture (`test/game_script/fixtures/icons/`) pages through the set in this order, every icon at 32, 64,
and 128 px in black and in white.

### Names the runtime screens will use

No screen draws a library icon yet. These are the names the epic's later entries will use:

- Entry 5, the match's views (`GameMatchActivity`), which draw these names: `pause` for the pause view, `warning` for
  the error view, and `flag_checkered` for the game-over view, each in the dialog's content band; `play` (Resume),
  `leave` (Leave, and the error view's Back, which leaves the match), and `restart` (Play again) at their option rows'
  left, when the label leaves room ([game-canvas.md](game-canvas.md)).
- Entry 6, the cover-grid Home's Games tile: `game_controller`.

### Left out

Considered and not in v1: `handshake` (a draw; illegible at 32 px in both weights), `stop`, `lock`, `cards`, and a
`close` that would duplicate `mark_x`. None was cut for size.

## Adding or changing an icon

1. Copy the SVG (a Phosphor 2.1.1 file byte for byte, or an original under `original/<weight>/`) and add or change
   its line in `assets/game-icons/names.txt`.
2. Run `python3 scripts/gen_game_icons.py` and commit the regenerated header.
3. Add or change the `icon` line in `docs/crosshatch/api-level-1.txt` and set `API_SURFACE_CRC` in
   `lib/GameCore/ApiLevel.h` to the value `ApiSurfaceTest.ListLoadsAndMatchesItsCrc` prints.
4. Add the name to `NAMES` in `test/game_script/fixtures/icons/main.lua`.
5. Update this file's table and its Size figures.

## Size

The generated data must stay within 48 KiB (49,152 B) of flash; `GameIconBlitTest.TheIconDataFitsIn48KiB` checks it.
Each icon costs 128 B (32 px) + 512 B (64 px) of bitmap, a 12 B `ICONS` entry, and its name string.

Measured on the x4pro ELF (`.pio/build/x4pro/firmware.elf`, games on) with `xtensa-esp32s3-elf-nm -S -C`, summing the
`GameIcons::` symbols:

| Data | Bytes |
| --- | ---: |
| 124 bitmaps (`*_32`, `*_64`), 62 x (128 + 512) | 39,680 |
| `ICONS`, 62 x 12 | 744 |
| Name strings, the sum of `len(name) + 1` (computed: they are not separate symbols) | 540 |
| Total | 40,964 |
| Limit (48 KiB) | 49,152 |

8,188 B to spare. Re-measured after `mark_dot` and `mark_miss` changed source: the same, since every bitmap has a
fixed size. The tracer's four icons measured 2,608 B the same way (without their strings).

The flash cost of the whole game runtime, the icons included, is measured by `scripts/check_flash_budget.py` (x4pro
`firmware.bin`, games on minus games off):

| x4pro | At `068a9ad0` (four icons) | With this set (62 icons) |
| --- | ---: | ---: |
| `firmware.bin`, games on | 5,833,504 | 5,871,760 |
| `firmware.bin`, games off | 5,675,280 | 5,675,280 |
| Flash difference (limit 256,000) | +158,224 | +196,480 |
| Static internal RAM difference (limit 1,024) | +8 | +8 |

The set adds 38,256 B to the games-on image and no static internal RAM: the data is `inline constexpr`, so it sits
in flash (`.flash.rodata`).
