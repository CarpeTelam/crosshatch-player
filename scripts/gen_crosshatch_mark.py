#!/usr/bin/env python3
"""
Generate the Crosshatch brand mark (the A2 board of DESIGN.md in _bmad-output/planning-artifacts/ux-designs/
ux-crosshatch-player-2026-10-03) as three 1-bit bitmaps, in two new fork-only headers. Standard library only; the
output is the same on every run.

    python3 scripts/gen_crosshatch_mark.py             # write both headers
    python3 scripts/gen_crosshatch_mark.py --check     # write nothing; exit 1 if a header's bytes differ from this script's
    python3 scripts/gen_crosshatch_mark.py --ascii N   # print the N px bitmap (N = 64, 120 or 128) as text, and write nothing
    python3 scripts/gen_crosshatch_mark_test.py        # the script's own tests

    src/images/CrosshatchMark120.h   the 120 px device mark, in Logo120's layout: 1 bpp, MSB first, 1 = white, rows of
                                     15 bytes, stored in panel orientation (the drawn image rotated 90 degrees counter-
                                     clockwise, as scripts/convert_icon.py does: stored (row, col) is drawn at
                                     (120 - 1 - row, col)). BootActivity and SleepActivity draw it with drawImage.
    src/games/GameMarkBitmaps.h      the 64 px launcher row icon and the 128 px title and hand-off icon, as
                                     GameRowIcon's Mask1: top-down rows, MSB first, bit 0 = ink, drawn orientation.

The geometry is the spine's (viewBox 256, stroke 16, round caps and joins): grid lines at 88 and 168 on each axis from
24 to 232; rings of radius 22 (outer 30, inner 14) at the cells (48, 48) and (208, 48); crosses of half diagonal 18 at
the cells (128, 128) and (208, 208). Each shape is rasterised by exact distance tests on a SUPERSAMPLE x SUPERSAMPLE
grid per pixel, and a pixel is ink when at least half of its samples are inside one layer (the grid, the rings, the
crosses). The 128 px bitmap is this native drawing, not the 64 px one doubled; the 120 and 64 px ones are drawn the same
way at their own size.

The fuse rule (EXPERIENCE.md, builder acceptance check): a ring's outer edge is at 78 units and the neighbouring grid
line's edge at 80, 0.94 px at 120 px and 0.5 px at 64 px, so a plain threshold can join the O to the grid. After
thresholding, a ring or cross pixel that is, or is 4-adjacent to, a grid pixel is cleared (the grid keeps its width and
the mark gives up the pixel). Then the script checks, and fails (exit 1) unless: no ring or cross pixel is, or is 4-adjacent
to, a grid pixel; each ring still encloses its hole (a 4-connected flood of white from the ring's centre never reaches the
border of the bitmap); and each of the four marks has ink. At 128 px the gap is exactly one pixel, so nothing is cleared.

Exit 0: written, or (--check) the headers match. Exit 1: a rule is broken, or --check found a difference. Exit 2: the
script could not run (an unreadable or unwritable file).
"""

import argparse
import functools
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
HEADER_120 = ROOT / 'src' / 'images' / 'CrosshatchMark120.h'
HEADER_GAMES = ROOT / 'src' / 'games' / 'GameMarkBitmaps.h'

VIEWBOX = 256
STROKE = 16
SUPERSAMPLE = 8
SIZES = (64, 120, 128)

GRID = ((88, 24, 88, 232), (168, 24, 168, 232), (24, 88, 232, 88), (24, 168, 232, 168))
RINGS = ((48, 48), (208, 48))
RING_RADIUS = 22
CROSSES = ((128, 128), (208, 208))
CROSS_HALF = 18

# The board's four marks, in the order the checks report them: (kind, centre).
MARKS = tuple(('ring', c) for c in RINGS) + tuple(('cross', c) for c in CROSSES)


class RuleError(Exception):
    pass


def _segment_distance_squared(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    length_squared = dx * dx + dy * dy
    t = ((px - ax) * dx + (py - ay) * dy) / length_squared if length_squared else 0.0
    t = 0.0 if t < 0.0 else 1.0 if t > 1.0 else t
    qx, qy = ax + t * dx, ay + t * dy
    return (px - qx) ** 2 + (py - qy) ** 2


def _capsules():
    """The grid's four strokes and the crosses' four diagonals, as (layer, ax, ay, bx, by)."""
    shapes = [('grid',) + line for line in GRID]
    for cx, cy in CROSSES:
        shapes.append(('cross', cx - CROSS_HALF, cy - CROSS_HALF, cx + CROSS_HALF, cy + CROSS_HALF))
        shapes.append(('cross', cx - CROSS_HALF, cy + CROSS_HALF, cx + CROSS_HALF, cy - CROSS_HALF))
    return shapes


def _inside(layer_shapes, rings, x, y):
    half = STROKE / 2.0
    for ax, ay, bx, by in layer_shapes:
        if _segment_distance_squared(x, y, ax, ay, bx, by) <= half * half:
            return True
    for cx, cy in rings:
        d = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
        if abs(d - RING_RADIUS) <= half:
            return True
    return False


def coverage(size, layer):
    """Per-pixel fraction of samples inside `layer` ('grid', 'ring' or 'cross'), as rows of lists."""
    shapes = [s[1:] for s in _capsules() if s[0] == layer]
    rings = RINGS if layer == 'ring' else ()
    scale = VIEWBOX / (size * SUPERSAMPLE)
    samples = SUPERSAMPLE * SUPERSAMPLE
    rows = []
    for py in range(size):
        row = []
        for px in range(size):
            hit = 0
            for sy in range(SUPERSAMPLE):
                y = (py * SUPERSAMPLE + sy + 0.5) * scale
                for sx in range(SUPERSAMPLE):
                    x = (px * SUPERSAMPLE + sx + 0.5) * scale
                    if _inside(shapes, rings, x, y):
                        hit += 1
            row.append(hit / samples)
        rows.append(row)
    return rows


def _ink(cov):
    return [[c >= 0.5 for c in row] for row in cov]


def _neighbours4(x, y, size):
    for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
        if 0 <= nx < size and 0 <= ny < size:
            yield nx, ny


def layers(size):
    """The ink of the grid, rings and crosses at `size`, after the fuse rule cleared the marks' touching pixels.

    Returns (grid, rings, crosses, cleared): three size x size lists of booleans, and how many pixels were cleared."""
    grid = _ink(coverage(size, 'grid'))
    rings = _ink(coverage(size, 'ring'))
    crosses = _ink(coverage(size, 'cross'))
    cleared = 0
    for marks in (rings, crosses):
        for y in range(size):
            for x in range(size):
                if marks[y][x] and (grid[y][x] or any(grid[ny][nx] for nx, ny in _neighbours4(x, y, size))):
                    marks[y][x] = False
                    cleared += 1
    return grid, rings, crosses, cleared


def _scaled(centre, size):
    return int(centre * size / VIEWBOX)


def check(size, grid, rings, crosses):
    """Raise RuleError unless the fuse rule holds (module docstring)."""
    for name, marks in (('ring', rings), ('cross', crosses)):
        for y in range(size):
            for x in range(size):
                if marks[y][x] and (grid[y][x] or any(grid[ny][nx] for nx, ny in _neighbours4(x, y, size))):
                    raise RuleError(f'{size} px: a {name} pixel at ({x}, {y}) touches the grid')
    both = [[grid[y][x] or rings[y][x] or crosses[y][x] for x in range(size)] for y in range(size)]
    for kind, (cx, cy) in MARKS:
        px, py = _scaled(cx, size), _scaled(cy, size)
        # Ink of this mark: anything within its box (the mark never reaches another cell's centre).
        reach = int((RING_RADIUS + STROKE / 2 if kind == 'ring' else CROSS_HALF + STROKE / 2) * size / VIEWBOX) + 2
        layer = rings if kind == 'ring' else crosses
        ink = sum(layer[y][x] for y in range(max(0, py - reach), min(size, py + reach + 1))
                  for x in range(max(0, px - reach), min(size, px + reach + 1)))
        if ink == 0:
            raise RuleError(f'{size} px: the {kind} at {(cx, cy)} has no ink')
        if kind == 'ring' and both[py][px]:
            raise RuleError(f'{size} px: the ring at {(cx, cy)} has no hole: its centre is ink')
        if kind == 'ring' and _flood_reaches_border(both, size, px, py):
            raise RuleError(f'{size} px: the ring at {(cx, cy)} is open: its hole joins the outside')


def _flood_reaches_border(ink, size, sx, sy):
    if ink[sy][sx]:
        return False  # the hole's middle is ink: caught by the ring test as a non-hole elsewhere
    seen = {(sx, sy)}
    stack = [(sx, sy)]
    while stack:
        x, y = stack.pop()
        if x in (0, size - 1) or y in (0, size - 1):
            return True
        for n in _neighbours4(x, y, size):
            if n not in seen and not ink[n[1]][n[0]]:
                seen.add(n)
                stack.append(n)
    return False


@functools.lru_cache(maxsize=None)
def bitmap(size):
    """The mark at `size` as rows of booleans (True = ink), checked against the fuse rule."""
    grid, rings, crosses, _ = layers(size)
    check(size, grid, rings, crosses)
    return tuple(tuple(grid[y][x] or rings[y][x] or crosses[y][x] for x in range(size)) for y in range(size))


def mask1(rows):
    """Top-down rows, MSB first, bit 0 = ink (GameRowIcon's Mask1)."""
    size = len(rows)
    out = bytearray()
    for row in rows:
        for x0 in range(0, size, 8):
            byte = 0xFF
            for b in range(8):
                if x0 + b < size and row[x0 + b]:
                    byte &= ~(0x80 >> b) & 0xFF
            out.append(byte)
    return bytes(out)


def panel_logo(rows):
    """Logo120's layout: 1 = white, stored (row, col) is drawn at (size - 1 - row, col)."""
    size = len(rows)
    stored = [[rows[col][size - 1 - row] for col in range(size)] for row in range(size)]
    out = bytearray()
    for row in stored:
        for x0 in range(0, size, 8):
            byte = 0xFF
            for b in range(8):
                if x0 + b < size and row[x0 + b]:
                    byte &= ~(0x80 >> b) & 0xFF
            out.append(byte)
    return bytes(out)


def _array(name, data, per_line=16):
    lines = []
    for i in range(0, len(data), per_line):
        lines.append('    ' + ', '.join(f'0x{b:02x}' for b in data[i:i + per_line]) + ',')
    return f'inline constexpr uint8_t {name}[] = {{\n' + '\n'.join(lines) + '\n};\n'


def header_120(data):
    return ('#pragma once\n#include <cstdint>\n\n'
            '// The Crosshatch mark, 120x120, in Logo120\'s layout (1 bpp, MSB first, 1 = white, 15-byte rows, panel\n'
            '// orientation). Generated by scripts/gen_crosshatch_mark.py; never hand-edited.\n'
            + _array('CrosshatchMark120', data))


def header_games(data64, data128):
    return ('#pragma once\n#include <cstdint>\n\n'
            '// The Crosshatch mark as the default game icon: GameRowIcon\'s Mask1 (top-down rows, MSB first,\n'
            '// bit 0 = ink). Generated by scripts/gen_crosshatch_mark.py; never hand-edited.\n'
            'namespace GameMark {\n\n'
            '// The launcher row icon: 64x64, rows of 8 bytes.\n' + _array('ROW_64', data64) +
            '\n// The title screen and hand-off band icon: a native 128x128 drawing, rows of 16 bytes.\n'
            + _array('HERO_128', data128) + '\n}  // namespace GameMark\n')


def generate():
    """The bytes of the three bitmaps: (logo120, row64, hero128)."""
    return panel_logo(bitmap(120)), mask1(bitmap(64)), mask1(bitmap(128))


def parse_array(text, name):
    """The bytes of `inline constexpr uint8_t name[] = {...};` in a header, whatever its formatting; None if absent."""
    match = re.search(r'\b' + re.escape(name) + r'\[\]\s*=\s*\{([^}]*)\}', text)
    if not match:
        return None
    return bytes(int(h, 16) for h in re.findall(r'0x([0-9a-fA-F]{2})', match.group(1)))


def ascii_art(rows):
    return '\n'.join(''.join('#' if v else '.' for v in row) for row in rows)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[1])
    parser.add_argument('--check', action='store_true', help='write nothing; fail when a header differs')
    parser.add_argument('--ascii', type=int, choices=SIZES, help='print one size as text and write nothing')
    args = parser.parse_args(argv)
    try:
        if args.ascii:
            print(ascii_art(bitmap(args.ascii)))
            return 0
        logo, row64, hero128 = generate()
        if args.check:
            bad = []
            for path, name, want in ((HEADER_120, 'CrosshatchMark120', logo), (HEADER_GAMES, 'ROW_64', row64),
                                     (HEADER_GAMES, 'HERO_128', hero128)):
                try:
                    have = parse_array(path.read_text(), name)
                except OSError as e:
                    print(f'error: {path}: {e}', file=sys.stderr)
                    return 2
                if have != want:
                    bad.append(f'{path.relative_to(ROOT)}: {name} differs from the script\'s output')
            for line in bad:
                print(line, file=sys.stderr)
            return 1 if bad else 0
        HEADER_120.write_text(header_120(logo))
        HEADER_GAMES.write_text(header_games(row64, hero128))
        return 0
    except RuleError as e:
        print(f'error: {e}', file=sys.stderr)
        return 1
    except OSError as e:
        print(f'error: {e}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
