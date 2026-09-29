# Game icons

The game icon library: the icons a game draws with `ch.gfx.icon(name, x, y, size, color, weight?)` and a screen with
`drawGameIcon` (`src/games/GameIconDraw.h`). Every icon is a Phosphor icon under Phosphor's own name, in two weights,
each a 1-bit bitmap at 32 px (`small`) and 64 px (`medium`); `large` (128 px) draws the 64 px bitmap doubled. The
names are part of the game API level: `api-level-1.txt` lists each one as `icon <name>`, and
`ApiSurfaceTest.IconsMatchTheList` checks that list against the library both ways and draws every name at every size
in both weights. The list also holds the drawn sizes (`limit icon_small_side_pixels`,
`icon_medium_side_pixels`, `icon_large_side_pixels`), that an icon draws its ink only (`draw ch.gfx.icon ink`), and the
canvas pixels a frame's icons and images may cover (`limit frame_icon_image_pixels`).

## Where the icons come from

Every icon is a [Phosphor Icons](https://phosphoricons.com) SVG, and the library holds nothing else: no original
drawings and no icons from another set. The SVGs come from the npm package `@phosphor-icons/core`, version 2.1.1
(`https://registry.npmjs.org/@phosphor-icons/core/-/core-2.1.1.tgz`, sha256
`313332be6190b724da24107addd781799b48bf76b13963f24501112ffe1baadd`), copied byte for byte from the package's
`assets/regular/` and `assets/fill/` into `assets/game-icons/phosphor/regular/` and `phosphor/fill/`. Phosphor Icons is
Copyright (c) 2023 Phosphor Icons, under the MIT licence; `assets/game-icons/phosphor/LICENSE` is the package's
licence file, and the generated header repeats the copyright line.

`assets/game-icons/names.txt` lists each name twice, once per weight, with Phosphor's own file for it:
`<name> regular phosphor/regular/<name>.svg` and `<name> fill phosphor/fill/<name>-fill.svg`.
`python3 scripts/gen_game_icons.py` renders them into `lib/GameIcons/GameIcons.generated.h`, which is never edited by
hand, and opens with `// clang-format off` (`lib/GameIcons/GameIcons.h`, beside it, is formatted like any source); the
`Icons up to date` fork CI job regenerates it and fails on any byte difference. The generator refuses a map
whose name is not a Phosphor file stem, whose path is not Phosphor's file for that name and weight, or that gives a
name in only one weight or in one weight twice.

`assets/game-icons/SHA256SUMS` pins each SVG's content: one `<sha256>  <path>` line, in `sha256sum`'s format and sorted
bytewise by path, for exactly the SVGs `names.txt` names. The generator checks every SVG against it before rendering and
exits 3 (`PIN_MISMATCH`) naming an SVG whose bytes differ, an SVG the file does not list, or a line that names a path
`names.txt` does not name; a malformed or repeated line is exit 1, and a missing `SHA256SUMS` exit 2. An edited SVG
therefore fails `Icons up to date` instead of regenerating cleanly, and only exit 3 gets the job's advice to restore the
SVG or re-pin; any other failure is annotated with its exit code. `(cd assets/game-icons && sha256sum -c SHA256SUMS)`
checks the same sums without the generator. After a deliberate change of source, rewrite the file with
`python3 scripts/gen_game_icons.py --write-sums`, which writes no header.

Regenerate on Linux, as CI does. The fill uses no libm call, but arc flattening uses `math.sin`, `cos`, and `atan2`,
whose last bits can differ between platforms' libm; the header was checked byte-identical only on Linux, for Python
3.10 to 3.13. A run on macOS or Windows may differ from CI's bytes, and then `Icons up to date` fails.

## Weights

Every name ships in both of Phosphor's weights:

- **Regular** (`"regular"`, the default): 2 px outlines at 32 px, matching the device's line-style UI icons. The
  runtime's own screens (the match views and the Home Games tab) draw this weight.
- **Fill** (`"fill"`): solid shapes, which read best at 32 px on a 1-bit screen, for game tokens such as suits,
  dice, and player markers.

A game picks the weight per call with `ch.gfx.icon`'s sixth argument: absent or `nil` is `"regular"`, and any value
other than the two names is an ordinary Lua argument error, as a bad size is (`bad argument #6 to 'icon' (invalid
option 'bold')`), which `pcall` catches. The weight rides in the display list's icon command, so a replayed frame draws
the same bitmap. A screen draws the fill weight with `drawGameIcon(..., black, true)`.

## Bitmap layout

Each bitmap in `ICONS` is PackBits-compressed, in drawn orientation, so a draw decodes one row at a time and needs no
buffer beyond that row. A packed bitmap is a 2-byte little-endian length N, then N bytes of PackBits that decode to the
drawn rows from the top, `pixels / 8` bytes each (4 at 32 px, 8 at 64 px), 1 bit per pixel, MSB first, bit 0 = ink,
bit x = drawn column x. A run is a control byte c and its data: c 0 to 127 copies the next c + 1 bytes, c 129 to 255
repeats the next byte 257 - c times (2 to 128), and 128 is malformed (the generator never writes it). A run may cross
from one row into the next, so the bitmap is one stream, and the generator gives it the shortest encoding, which makes
the output deterministic. Encoding each row on its own would cost 14,109 B more over the whole set (59,638 B against
45,529 B, length prefixes included, from the generator's own encoder on the 220 bitmaps), and the reader that crosses
rows needs only a run counter, a repeat value, and a read position.

`GameIconBlit::inkRuns` reads rows through `GameIcons::PackedReader` into an 8-byte buffer on the stack, walks each
into runs of ink, and decodes the next row only when the draw needs it; the rows above a clip are decoded and not
drawn (at most 64), and an icon wholly off the canvas decodes nothing. There is no heap, no static buffer, and no
failure path in the draw: a malformed run (control 128, or a run or copy whose data is past the stored length), which
only a generator bug can produce, ends the icon at the row it breaks, drawing nothing from that row on and reading
nothing past the bitmap's stored length. `GameIcons.h` also `static_assert`s that every bitmap decodes to exactly its
rows, with no run or byte left over, so such a bug fails the build first. The fill counts and pixel budget do not
change: the runs are the pixels' runs, as before.

`GAME_CONTROLLER_32` is the one raw array, in `GfxRenderer::drawIcon`'s layout (square, rows padded to whole bytes,
bit 0 = ink, stored rotated 90 degrees counter-clockwise: stored (row, col) is drawn at (pixels - 1 - row, col)),
because Home's cover-grid Games tab passes it to `drawIcon`. `ICONS` still holds a packed copy of the same bitmap.

`test/game_script/GameIconsRaw.h` holds every bitmap uncompressed, in that same layout, as the generator
computes it (`python3 scripts/gen_game_icons.py --raw-out test/game_script/GameIconsRaw.h`). Only the host
tests include it: `GameIconBlitTest` decodes every icon at every size and weight and compares it, byte for byte and
pixel for pixel, with the raw bitmap, and `gen_game_icons_test.py` checks the committed file against a fresh render.
The `Icons up to date` job regenerates both files and fails on a byte difference in either.

## Naming rules

- A name is Phosphor's own: the SVG file stem, hyphens included, without `-fill` (`dice-six`, `game-controller`,
  `arrow-u-up-left`). The library's names are only the 55 in the table below (the `icon` lines of `api-level-1.txt`),
  not every icon on `https://phosphoricons.com`, which is where to preview a listed icon's drawing. The generator
  enforces `[a-z][a-z0-9-]{0,31}`, no `--`, no final `-`, and no name ending in `-fill`.
- In C++ the header's identifiers are the name in upper case with `-` as `_`, and the fill weight adds `_FILL`; the
  packed arrays add the size and `_PB`: `dice-six` is `DICE_SIX_32_PB` and `DICE_SIX_64_PB` (regular) and
  `DICE_SIX_FILL_32_PB` and `DICE_SIX_FILL_64_PB` (fill). A name holds no `_` and never ends in `-fill`, so no two
  names share an identifier. `ICONS[i].small[weight]` and `.medium[weight]` index the bitmaps by `GameIcons::Weight`
  (`Regular` 0, `Fill` 1).
- One name per glyph: no alias for an icon already in the set.
- A game's own images (`ch.gfx.image`) follow a different rule: the file name without `.bmp`, 1 to 32 characters from
  `[a-z0-9_]`, with no `-`. A chess game that ships `crown-cross.bmp` has it skipped with a log line; name it
  `crown_cross.bmp` (`api-level-1.txt` lists the rule as `name image (?!icon$)[a-z0-9_]{1,32}`, beside the image
  limits).
- Level 1 is a preview until `API_LEVEL_FROZEN`: names may still change. Once a level is frozen, no name in it is
  renamed or removed; a new icon joins the next level.

## The v1 set (55 icons, 110 bitmaps a size)

The set's categories are marks, card suits, dice faces, player markers, game pieces, common controls, and status
icons (the epic's R3, as amended by the owner in entry 8). Each name is listed once, under the category it was chosen
for (`circle` is a mark, and in fill also a round player marker). The owner dropped the chess pieces (entry 8), so the
game pieces are Battleship's `boat` alone; a chess game draws its own pieces with `ch.gfx.image`.

| Category | Name | Why it is in the set |
| --- | --- | --- |
| Marks | `x` | Noughts and crosses and Ultimate tic-tac-toe's cross (the tracer draws it) |
| | `circle` | The nought (regular, a ring); in fill, a round solid player marker |
| | `dot-outline` | A small dot: last move, a legal-move hint, a board point (a small ring in regular, a solid dot in fill) |
| | `fire` | Battleship's hit |
| | `waves` | Battleship's miss, a water tile; a drop was too like the flame at 32 px |
| Card suits | `club` | The clubs suit |
| | `diamond` | The diamonds suit |
| | `heart` | The hearts suit |
| | `spade` | The spades suit |
| Dice faces | `dice-one` | Die face 1, for dice games and random choices |
| | `dice-two` | Die face 2 |
| | `dice-three` | Die face 3 |
| | `dice-four` | Die face 4 |
| | `dice-five` | Die face 5 |
| | `dice-six` | Die face 6 |
| Game pieces | `boat` | Battleship's ship |
| Player markers | `square` | A distinct token for a seat or counter; shapes, not shades, since the screen is 1-bit |
| | `triangle` | A distinct seat or counter token |
| | `star` | A distinct seat or counter token |
| | `hexagon` | A distinct seat or counter token |
| | `user` | One seat: whose turn it is |
| | `users` | Both players: the pass-the-device and nearby prompts |
| Controls | `arrow-left` | Paging and direction |
| | `arrow-right` | Paging and direction |
| | `arrow-up` | Paging and direction |
| | `arrow-down` | Paging and direction |
| | `arrow-u-up-left` | Undo: take back a move |
| | `arrow-u-up-right` | Redo: replay a move |
| | `arrows-clockwise` | Play again, a new round (the end-of-round view's Play again row) |
| | `arrow-clockwise` | Rotate, as in Battleship's ship placement |
| | `shuffle` | Random placement, shuffling a deck |
| | `play` | Resume (the pause view's Resume row) |
| | `pause` | Pause (the pause view) |
| | `check` | Confirm |
| | `plus` | Add; a stepper's up |
| | `minus` | Remove; a stepper's down |
| | `info` | The rules |
| | `question` | Help |
| | `lightbulb` | A hint |
| | `pencil-simple` | Sudoku's pencil marks |
| | `eraser` | Clearing a cell |
| | `eye` | Hidden information: show a hand or a board |
| | `eye-closed` | Hidden information: look away before a pass |
| | `house` | Home, the launcher |
| | `gear-six` | Settings |
| | `sign-out` | Leave the match (the views' Leave and Back rows) |
| | `trash` | Delete, discard |
| | `timer` | `ch.timer` countdowns |
| | `hourglass` | Waiting for the other player |
| | `game-controller` | The Home Games tab; line style like the Home tabs' icons |
| Status | `warning` | The error view |
| | `flag-checkered` | The game-over view: a finish, neutral for a win or a loss |
| | `trophy` | A win |
| | `smiley` | A good result |
| | `smiley-sad` | A bad result |

The `icons` fixture (`test/game_script/fixtures/icons/`) pages through the set in this order, three names a page, every
icon at 32, 64, and 128 px in regular and in fill, in black and in white (21 pages an ink).

### Names the runtime screens use

Two screens draw library icons besides the games, both in the regular weight:

- The match's views (`GameMatchActivity`) draw by name through `drawGameIcon`: `pause` for the pause view, `warning`
  for the error view, and `flag-checkered` for the game-over view, each in the dialog's content band; `play` (Resume),
  `sign-out` (Leave, and the error view's Back, which leaves the match), and `arrows-clockwise` (Play again) at their
  option rows' left, when the label leaves room ([game-canvas.md](game-canvas.md)). `src/games/GameViewIcons.h` holds
  the names, and `GameViewIconsTest` checks each against the library.
- The cover-grid Home's Games tab (`src/components/CoverGridHomeUi.cpp`, ledger row 9 of
  [upstream-touches.md](upstream-touches.md)) draws `game-controller` directly, as the generated symbol
  `GameIcons::GAME_CONTROLLER_32` (regular) passed to `renderer.drawIcon`, beside upstream's own tab icons.

### The launcher's row icons

`GamesLauncherActivity` (`src/activities/games/`) shows one 64 x 64 icon at the left of each game's row, chosen by
`GameRowIcon::choose` (`src/games/GameRowIcon.h`) in this order:

1. the package's own `icon.bmp` (a 64 x 64 file in the converter's 1-bit layout, which the installer wrote from
   `icon.png`), when the launcher read it;
2. else the manifest's `icon` in the manifest's `icon_weight` (`regular` when absent), when the library has that name;
3. else `game-controller` in the regular weight, the fallback for a game with neither (also for an `icon` the library
   lacks, which the installer refuses but a hand-copied `/.games/<id>/` can name).

A library icon is decoded into memory (`renderLibraryIcon`, from the same `GameIconBlit::inkRuns` runs a draw fills,
so the pixels are the ones `drawGameIcon` would make) one row at a time into a single scratch bitmap, because the SDK's
list draws bitmaps, not a `GfxRenderer`. The bitmap is FreeInkUI's `Mask1` (bit 0 = ink, MSB first), which is also how
`icon.bmp` and the library store their pixels. `GameRowIconTest` and `GamesLauncherTest` pin the choice and the pixels.

### Left out

Considered and not in v1: the chess pieces (Phosphor's `crown-cross`, `crown`, `castle-turret`, and `horse`, and the
original bishop and pawn; dropped by the owner in entry 8, the originals also because the library is Phosphor only),
`handshake` (a draw; illegible at 32 px in both weights), `stop`, `lock`, `cards`, and a close icon that would
duplicate `x`. None was cut for size.

## Adding or changing an icon

1. Pick the icon on `https://phosphoricons.com`, where every Phosphor icon can be previewed; its name there becomes its
   library name once the steps below add it (until then a game that draws it stops with an unknown-icon error).
2. Copy `assets/regular/<name>.svg` and `assets/fill/<name>-fill.svg` from the pinned 2.1.1 package, byte for byte,
   into `assets/game-icons/phosphor/regular/` and `phosphor/fill/`, and add the name's two lines (regular, then fill)
   under its category in `assets/game-icons/names.txt`.
3. Run `python3 scripts/gen_game_icons.py --write-sums` and check that `SHA256SUMS` changed only by the new SVGs'
   lines (`git diff`); a changed line for an existing SVG means that file is no longer the package's.
4. On Linux, run `python3 scripts/gen_game_icons.py --raw-out test/game_script/GameIconsRaw.h` and commit
   the regenerated header and the test reference with `SHA256SUMS`.
5. Add or change the `icon` line in `docs/crosshatch/api-level-1.txt` and set `API_SURFACE_CRC` in
   `lib/GameCore/ApiLevel.h` to the value `ApiSurfaceTest.ListLoadsAndMatchesItsCrc` prints.
6. Add the name to `NAMES` in `test/game_script/fixtures/icons/main.lua`.
7. Update this file's table and its Size figures.

Renaming or dropping an icon a screen uses breaks it: `game-controller` fails the x4pro and sticky builds (and the
simulator's), since the cover-grid Home names `GameIcons::GAME_CONTROLLER_32`; one of the match's view icons fails
`GameViewIconsTest`. Change the screen in the same commit.

## Size

The generated data must stay within 96 KiB (98,304 B) of flash; `GameIconBlitTest.TheIconDataFitsIn96KiB` checks it.
That limit is the owner's original budget for the icon data and stays as it was set, although packing (entry 15) left
the data at less than half of it; the packing itself is checked by the same test (packed bytes below raw bytes), and the
flash the icons and everything else games add is bounded by `check_flash_budget.py`'s gate below. Each name costs four
packed bitmaps (45,529 B for all 220, about 830 B a name), a 20 B `ICONS` entry (the name pointer and two bitmap
pointers per size on the ESP32), and its name string; `GAME_CONTROLLER_32` adds its raw 128 B once.

Measured on the x4pro ELF (`.pio/build/x4pro/firmware.elf`, games on) with `xtensa-esp32s3-elf-nm -S -C`, summing the
`GameIcons::` symbols, at the parent of entry 15 (`1a0b7f6d`, whose firmware sources are entry 3's `c47cceaa`) and at
entry 15:

| Data | Before entry 15 | After entry 15 |
| --- | ---: | ---: |
| 220 bitmaps, raw in `drawIcon`'s layout (`*_32`, `*_64`, `*_FILL_32`, `*_FILL_64`), 55 x 2 x (128 + 512) | 70,400 | 0 |
| 220 packed bitmaps (`*_PB`), length prefixes included | 0 | 45,529 |
| `GAME_CONTROLLER_32`, raw | (in the 70,400) | 128 |
| `ICONS`, 55 x 20 | 1,100 | 1,100 |
| `DRAWN_PIXELS` | 12 | 12 |
| Sum of the `GameIcons::` symbols | 71,512 | 46,769 |
| Name strings, the sum of `len(name) + 1` (computed: they are not separate symbols) | 475 | 475 |
| Total | 71,987 | 47,244 |
| Limit (96 KiB) | 98,304 | 98,304 |

51,060 B to spare. (`GameIconBlitTest` counts the same data without `DRAWN_PIXELS`: 47,232 B.) Before entry 9 (62 names,
one weight each) the same sum was 40,964 B of a 48 KiB limit. The tracer's four icons measured 2,608 B the same way
(without their strings).

The flash cost of the whole game runtime, the icons included, is measured by `scripts/check_flash_budget.py` (x4pro
`firmware.bin`, games on minus games off):

| x4pro | At `068a9ad0` (four icons) | 62 icons, one weight | After entry 7 (`8e233695`) | Phosphor names, both weights (entry 9) |
| --- | ---: | ---: | ---: | ---: |
| `firmware.bin`, games on | 5,833,504 | 5,871,760 | 5,871,712 | 5,902,784 |
| `firmware.bin`, games off | 5,675,280 | 5,675,280 | 5,675,376 | 5,675,376 |
| Flash difference (limit 256,000) | +158,224 | +196,480 | +196,336 | +227,408 |
| Static internal RAM difference, `.dram0.*` + `.noinit` | +8 | +8 | +8 | +8 |
| Static internal RAM difference, the gate's sum with `.iram0.*` (limit 1,024) | unmeasured | unmeasured | +776 | +776 |

Entry 9 (Phosphor names, both weights; measured with the flash budget job's four commands on this story's
working tree, from an empty `.pio`, not an archive tree) adds +31,072 B to the games-on image against entry 7: +31,011 B of
icon data (71,975 B against 40,964 B) and 61 B of code for the weight argument, with no static internal RAM change.
The runtime is now 28,592 B under the 250 KiB gate.

Entry 15 (PackBits; the flash budget job's four commands run on this story's working tree, and again on its parent
`1a0b7f6d`, whose firmware sources equal entry 3's `c47cceaa`, whose games-on image measured 5,913,872 B both times)
takes the games-on image from 5,913,872 B to 5,889,312 B and the games-off image stays 5,676,064 B, so the difference
falls from +237,808 B to +213,248 B: -24,560 B of flash and +0 B of static internal RAM (+776 B before and after). By
`size -A` on the two games-on ELFs, `.flash.rodata` falls 24,736 B (3,491,812 B to 3,467,076 B) and `.flash.text` grows
180 B (2,301,628 B to 2,301,808 B). The data: 70,400 B of raw bitmaps become 45,529 B packed (-24,871 B) and the raw
`GAME_CONTROLLER_32` adds 128 B, so the icon data saves 24,743 B, and 7 B of padding round the rest. The decoder costs
180 B of code (`PackedReader` inside the one `inkRuns` instance `drawGameIconAt` uses). Against the epic's base
(`962ae61`, +228,496 B), the runtime is now 15,248 B smaller than then, and 42,752 B under the 250 KiB gate.

After the refactor sweep (entry 7, measured from a fresh archive tree of entry 7's pre-amend commit (the same code as `8e233695`, which only adds the measured figures) with the flash budget job's four
commands) the runtime added +196,336 B of flash, 59,664 B under the 250 KiB gate. The epic's base `1eacdc77`, measured
the same way with entry 7's script copied in, is +151,088 B of flash and +776 B of static internal RAM. So this epic
adds +76,320 B of flash in all (+45,248 B up to entry 7, +31,072 B in entry 9) and no static internal RAM. The RAM
gate counts IRAM since entry 7 (it shares internal SRAM on the S3): +8 B of `.dram0.bss`, +684 B of `.iram0.text` (the
FreeRTOS task functions only games link, which ESP-IDF places in IRAM: `vTaskSuspend` 240 B, `vTaskResume` 216 B,
`eTaskGetState` 154 B, `uxTaskGetStackHighWaterMark` 35 B, `pxTaskGetStackStart` 14 B: 659 B by `objdump -t`, plus
5 B of alignment padding between functions and 20 B more of the literal pool at the section's start, which their 200 B
of `.literal.*` sections grow once the linker merges the literals already there; `objdump -t` and `-h` on the games-on
and games-off ELFs of the cross-story fix commit's tree), and +84 B of `.iram0.text_end` alignment padding: +776 B,
248 B under the 1,024 B gate, all of it already present at the base (epic-script-runtime's gate counted only
`.dram0.*` and `.noinit`, so it recorded +8 B). The icon data is `inline constexpr`, so it sits in flash
(`.flash.rodata`) and adds no static internal RAM.
