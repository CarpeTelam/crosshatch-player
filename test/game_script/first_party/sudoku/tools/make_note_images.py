#!/usr/bin/env python3
"""
Writes the 27 note-digit images of Sudoku, three sets of nine: `games/sudoku/note_g1.png` .. `note_g9.png` (G, grey
numeral on white), `note_b1.png` .. `note_b9.png` (B, black numeral on white) and `note_h1.png` .. `note_h9.png` (H,
white numeral on a 50% checker). Standard library only; the output is deterministic, so the committed PNGs regenerate
unchanged.

    python3 test/game_script/first_party/sudoku/tools/make_note_images.py            # write them
    python3 test/game_script/first_party/sudoku/tools/make_note_images.py --check    # compare, change nothing

Each image is IMAGE_W x IMAGE_H pixels (12 x 16): a bold hand-made 10 x 14 glyph (strokes about 3 px) at tile offset
(1, 1), inside a one-pixel margin. With tile pixel (i, j), 0-based:

  G  black where the glyph is set and (i + j) is even, else white   (the grey numeral, a 50% checker in the glyph)
  B  black where the glyph is set, else white                       (the solid numeral)
  H  black where (i + j) is even outside the glyph, white where the glyph is set   (a white numeral on 50% grey)

The phase rule: the screen's "dark" ground is black where screen x + y is even, and the canvas origin's x + y is even
on both boards, so a tile whose origin (X0, Y0) has X0 + Y0 even continues the ground's checker with H drawn "black",
and with H drawn "white" (which swaps black and white) a tile whose origin has X0 + Y0 odd does; layout.note_tile in
games/sudoku/layout.lua places the tiles so. G gets the same phase, though it sits on white. Tiles are 16 pixels
tall, so three rows exactly fill the 48 pixels (offsets 2..49) of a 51 pixel cell that the grid lines leave clear.
The images are 1-bit grayscale PNGs holding only black and white, so the installer's converter has nothing to dither.

--check also fails on a note_*.png in the folder that is not one of the 27 (it would count against the package's image
limit), and when games/sudoku/layout.lua's NOTE_W and NOTE_H are not IMAGE_W and IMAGE_H. Nothing in CI runs this tool, so
it is run by hand whenever the glyphs, the phase rule, or layout.note_tile change.

Exit codes: 0 done (or --check found no difference), 1 --check found a difference.
"""

import argparse
import pathlib
import re
import struct
import sys
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[5]
OUT = ROOT / 'games' / 'sudoku'

IMAGE_W = 12
IMAGE_H = 16
GLYPH_W = 10
GLYPH_H = 14

# The glyphs: 10 columns by 14 rows, '#' ink, strokes about 3 pixels.
GLYPHS = {
    1: ['....###...',
        '...####...',
        '..#####...',
        '.######...',
        '....###...',
        '....###...',
        '....###...',
        '....###...',
        '....###...',
        '....###...',
        '....###...',
        '....###...',
        '.########.',
        '.########.'],
    2: ['..######..',
        '.########.',
        '###....###',
        '###....###',
        '.......###',
        '......###.',
        '.....###..',
        '....###...',
        '...###....',
        '..###.....',
        '.###......',
        '###.......',
        '##########',
        '##########'],
    3: ['.########.',
        '##########',
        '###....###',
        '.......###',
        '.......###',
        '....#####.',
        '....#####.',
        '.......###',
        '.......###',
        '.......###',
        '###....###',
        '###....###',
        '##########',
        '.########.'],
    4: ['......###.',
        '.....####.',
        '....#####.',
        '...######.',
        '..###.###.',
        '.###..###.',
        '###...###.',
        '##########',
        '##########',
        '......###.',
        '......###.',
        '......###.',
        '......###.',
        '......###.'],
    5: ['##########',
        '##########',
        '###.......',
        '###.......',
        '###.......',
        '#######...',
        '########..',
        '.......###',
        '.......###',
        '.......###',
        '###....###',
        '###....###',
        '##########',
        '.########.'],
    6: ['...#######',
        '..########',
        '.###......',
        '###.......',
        '###.......',
        '#########.',
        '##########',
        '###....###',
        '###....###',
        '###....###',
        '###....###',
        '###....###',
        '##########',
        '.########.'],
    7: ['##########',
        '##########',
        '.......###',
        '.......###',
        '......###.',
        '......###.',
        '.....###..',
        '.....###..',
        '....###...',
        '....###...',
        '...###....',
        '...###....',
        '...###....',
        '...###....'],
    8: ['.########.',
        '##########',
        '###....###',
        '###....###',
        '###....###',
        '.########.',
        '..######..',
        '.########.',
        '###....###',
        '###....###',
        '###....###',
        '###....###',
        '##########',
        '.########.'],
    9: ['.########.',
        '##########',
        '###....###',
        '###....###',
        '###....###',
        '###....###',
        '.#########',
        '..########',
        '.......###',
        '.......###',
        '.......###',
        '###....###',
        '##########',
        '.########.'],
}

SETS = 'gbh'


def pixels(digit, kind):
    """Rows of IMAGE_W values, 0 black and 1 white, for one digit of set `kind` ('g', 'b' or 'h')."""
    glyph = GLYPHS[digit]
    assert len(glyph) == GLYPH_H and all(len(line) == GLYPH_W for line in glyph), f'glyph {digit} is not {GLYPH_W} x {GLYPH_H}'
    ox, oy = (IMAGE_W - len(glyph[0])) // 2, (IMAGE_H - len(glyph)) // 2
    rows = []
    for j in range(IMAGE_H):
        row = []
        for i in range(IMAGE_W):
            gy, gx = j - oy, i - ox
            ink = 0 <= gy < len(glyph) and 0 <= gx < len(glyph[0]) and glyph[gy][gx] == '#'
            even = (i + j) % 2 == 0
            if kind == 'g':
                black = ink and even
            elif kind == 'b':
                black = ink
            else:
                black = even and not ink
            row.append(0 if black else 1)
        rows.append(row)
    return rows


def png_chunk(kind, data):
    body = kind + data
    return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body) & 0xFFFFFFFF)


def png_bytes(rows):
    """A 1-bit grayscale PNG (colour type 0, depth 1, filter 0 on every row, zlib level 9)."""
    height, width = len(rows), len(rows[0])
    raw = bytearray()
    for row in rows:
        raw.append(0)
        packed = 0
        for x in range(width):
            packed = (packed << 1) | row[x]
            if x % 8 == 7:
                raw.append(packed)
                packed = 0
        if width % 8:
            raw.append(packed << (8 - width % 8))
    return (
        b'\x89PNG\r\n\x1a\n'
        + png_chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 1, 0, 0, 0, 0))
        + png_chunk(b'IDAT', zlib.compress(bytes(raw), 9))
        + png_chunk(b'IEND', b'')
    )


def images():
    """{file name: PNG bytes} for all 27 images."""
    out = {}
    for digit in range(1, 10):
        for kind in SETS:
            out[f'note_{kind}{digit}.png'] = png_bytes(pixels(digit, kind))
    return out


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--check', action='store_true', help='compare with the files on disk and change nothing')
    parser.add_argument('--out', type=pathlib.Path, default=OUT, help='the folder to write (default games/sudoku)')
    args = parser.parse_args(argv)
    different = []
    if args.check:
        wanted = images()
        different += sorted(p.name for p in args.out.glob('note_*.png') if p.name not in wanted)
        layout = args.out / 'layout.lua'
        found = re.search(r'layout\.NOTE_W, layout\.NOTE_H = (\d+), (\d+)', layout.read_text()) if layout.is_file() else None
        if not found or (int(found.group(1)), int(found.group(2))) != (IMAGE_W, IMAGE_H):
            different.append('layout.lua NOTE_W, NOTE_H')
    for name, data in images().items():
        path = args.out / name
        if args.check:
            if not path.is_file() or path.read_bytes() != data:
                different.append(name)
        else:
            path.write_bytes(data)
    if different:
        print('differs: ' + ', '.join(different), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
