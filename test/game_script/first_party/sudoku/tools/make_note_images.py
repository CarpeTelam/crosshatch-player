#!/usr/bin/env python3
"""
Writes the 18 note-digit images of Sudoku, `games/sudoku/note_g1.png` .. `note_g9.png` (grey) and `note_b1.png` ..
`note_b9.png` (black). Standard library only; the output is deterministic, so the committed PNGs regenerate unchanged.

    python3 test/game_script/first_party/sudoku/tools/make_note_images.py            # write them
    python3 test/game_script/first_party/sudoku/tools/make_note_images.py --check    # compare, change nothing

Each image is IMAGE_W x IMAGE_H pixels (10 x 14: the digit is a hand-made 8 x 12 bitmap with a one-pixel white margin,
the "own paper" the game draws opaque, so a mark covers whatever is under its slot). Pencil marks are drawn in a 17
pixel slot of a 51 pixel cell, so 14 rows leave 3 pixels between rows of marks. The grey set has a 50% checkerboard
dither baked in (the ink pixels where x + y is even) and the black set is solid: the game draws the focused digit's
marks black and every other mark grey. The images are 1-bit grayscale PNGs holding only black and white, so the
installer's converter has nothing to dither.

Exit codes: 0 done (or --check found no difference), 1 --check found a difference.
"""

import argparse
import pathlib
import struct
import sys
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[5]
OUT = ROOT / 'games' / 'sudoku'

IMAGE_W = 10
IMAGE_H = 14

# The glyphs: 8 columns by 12 rows, '#' ink, 2 pixel strokes.
GLYPHS = {
    1: ['...##...',
        '..###...',
        '.####...',
        '...##...',
        '...##...',
        '...##...',
        '...##...',
        '...##...',
        '...##...',
        '...##...',
        '.######.',
        '.######.'],
    2: ['.######.',
        '##....##',
        '......##',
        '......##',
        '.....##.',
        '....##..',
        '...##...',
        '..##....',
        '.##.....',
        '##......',
        '########',
        '########'],
    3: ['.######.',
        '##....##',
        '......##',
        '......##',
        '...####.',
        '...####.',
        '......##',
        '......##',
        '......##',
        '##....##',
        '##....##',
        '.######.'],
    4: ['.....##.',
        '....###.',
        '...####.',
        '..##.##.',
        '.##..##.',
        '##...##.',
        '########',
        '########',
        '.....##.',
        '.....##.',
        '.....##.',
        '.....##.'],
    5: ['########',
        '########',
        '##......',
        '##......',
        '######..',
        '#######.',
        '......##',
        '......##',
        '......##',
        '##....##',
        '##....##',
        '.######.'],
    6: ['..#####.',
        '.##.....',
        '##......',
        '##......',
        '######..',
        '#######.',
        '##....##',
        '##....##',
        '##....##',
        '##....##',
        '##....##',
        '.######.'],
    7: ['########',
        '########',
        '......##',
        '.....##.',
        '.....##.',
        '....##..',
        '....##..',
        '...##...',
        '...##...',
        '..##....',
        '..##....',
        '..##....'],
    8: ['.######.',
        '##....##',
        '##....##',
        '##....##',
        '.######.',
        '.######.',
        '##....##',
        '##....##',
        '##....##',
        '##....##',
        '##....##',
        '.######.'],
    9: ['.######.',
        '##....##',
        '##....##',
        '##....##',
        '##....##',
        '.#######',
        '..######',
        '......##',
        '......##',
        '.....##.',
        '....##..',
        '.####...'],
}


def pixels(digit, grey):
    """Rows of IMAGE_W values, 0 black and 1 white, for one digit: the glyph in a one pixel margin of paper."""
    rows = [[1] * IMAGE_W for _ in range(IMAGE_H)]
    glyph = GLYPHS[digit]
    for gy, line in enumerate(glyph):
        for gx, mark in enumerate(line):
            if mark != '#':
                continue
            x, y = gx + (IMAGE_W - len(line)) // 2, gy + (IMAGE_H - len(glyph)) // 2
            if not grey or (x + y) % 2 == 0:
                rows[y][x] = 0
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
    """{file name: PNG bytes} for all 18 images."""
    out = {}
    for digit in range(1, 10):
        out[f'note_g{digit}.png'] = png_bytes(pixels(digit, True))
        out[f'note_b{digit}.png'] = png_bytes(pixels(digit, False))
    return out


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--check', action='store_true', help='compare with the files on disk and change nothing')
    parser.add_argument('--out', type=pathlib.Path, default=OUT, help='the folder to write (default games/sudoku)')
    args = parser.parse_args(argv)
    different = []
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
