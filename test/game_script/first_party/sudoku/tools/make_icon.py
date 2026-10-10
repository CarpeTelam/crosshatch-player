#!/usr/bin/env python3
"""
Writes Sudoku's launcher icon, `games/sudoku/icon.png`: a 9 x 9 grid with one black cell in every row, column and box.
Standard library only (the PNG encoder is make_note_images.py's, imported from this folder); the output is
deterministic, so the committed PNG regenerates unchanged.

    python3 test/game_script/first_party/sudoku/tools/make_icon.py            # write it
    python3 test/game_script/first_party/sudoku/tools/make_icon.py --check    # compare, change nothing

The icon is SIZE x SIZE (64 x 64), the launcher's icon size (docs/crosshatch/formats.md: `icon.png` becomes a 64 x 64
`icon.bmp`, and a non-square icon makes the package invalid). It is 1-bit, black and white only, so the installer's
converter has nothing to dither. By rule, with pixel (x, y) 0-based from the top left:

  grid lines  Ten vertical and ten horizontal lines, line k centred at 5 + 6 * k (5, 11, ... 59). Every third line
              (k = 0, 3, 6, 9) is 3 px wide, the rest 1 px. A vertical line spans y 5 to 59 and a horizontal one x 5 to
              59, so the four corners (4, 4), (60, 4), (4, 60) and (60, 60) stay white.
  black cells A 5 x 5 square in the grid cell (row r, column c) at x 6 + 6 * c, y 6 + 6 * r, for the nine (r, c) of
              CELLS: one per row, per column and per 3 x 3 box.

--check compares the committed file with what this writes (and fails if it is missing), in CI: the games check registers it
as a ctest labelled `games-check` (harness/games_check.cmake, GamesCheckSudokuIcon). Writing the icon (no flag) is by hand,
whenever the rule above changes. --out names the folder written or checked (default games/sudoku).

Exit codes: 0 done (or --check found no difference), 1 --check found a difference.
"""

import argparse
import pathlib
import sys

# make_note_images.py sits beside this file; the path insert (rather than a package import) is what lets `python3 -I` run it.
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from make_note_images import png_bytes  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[5]
OUT = ROOT / 'games' / 'sudoku'
NAME = 'icon.png'

SIZE = 64
FIRST = 5  # the first line's centre, and where a line starts
PITCH = 6  # one cell and one thin line
LINES = 10
LAST = FIRST + PITCH * (LINES - 1)  # 59
CELL = 5

# The black cells as (row, column), 0-based: one in every row, column and box.
CELLS = [(0, 1), (1, 4), (2, 7), (3, 2), (4, 5), (5, 8), (6, 0), (7, 3), (8, 6)]


def pixels():
    """SIZE rows of SIZE values, 0 black and 1 white."""
    rows = [[1] * SIZE for _ in range(SIZE)]

    def black(x0, y0, w, h):
        for y in range(y0, y0 + h):
            for x in range(x0, x0 + w):
                rows[y][x] = 0

    for k in range(LINES):
        centre = FIRST + PITCH * k
        width = 3 if k % 3 == 0 else 1
        left = centre - width // 2
        black(left, FIRST, width, LAST - FIRST + 1)  # a vertical line
        black(FIRST, left, LAST - FIRST + 1, width)  # a horizontal line
    for r, c in CELLS:
        black(FIRST + 1 + PITCH * c, FIRST + 1 + PITCH * r, CELL, CELL)
    return rows


def image():
    return png_bytes(pixels())


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--check', action='store_true', help='compare with the file on disk and change nothing')
    parser.add_argument('--out', type=pathlib.Path, default=OUT, help='the folder to write (default games/sudoku)')
    args = parser.parse_args(argv)
    path = args.out / NAME
    data = image()
    if args.check:
        if not path.is_file() or path.read_bytes() != data:
            print(f'differs: {NAME}', file=sys.stderr)
            return 1
        return 0
    args.out.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
