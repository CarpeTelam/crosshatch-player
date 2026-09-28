#!/usr/bin/env python3
"""
Generate lib/GameIcons/GameIcons.generated.h, the game icon library's bitmaps, from the SVGs that
assets/game-icons/names.txt names. Standard library only; the output is the same, byte for byte, on every run on one
platform (see "Run it on Linux" below).

    python3 scripts/gen_game_icons.py [--assets DIR] [--out PATH]
        DIR   the folder holding names.txt, SHA256SUMS, and the SVGs (default: <repo>/assets/game-icons)
        PATH  the header to write (default: <repo>/lib/GameIcons/GameIcons.generated.h)
    python3 scripts/gen_game_icons.py [--assets DIR] --write-sums   # rewrite DIR/SHA256SUMS; writes no header
    python3 scripts/gen_game_icons_test.py   # the script's own tests

Run it on Linux, as CI's Icons up to date job does: arcs use libm (below), and the header was checked byte-identical
only on Linux, for Python 3.10 to 3.13, so a run elsewhere may differ from CI's bytes.

names.txt: `#` comment lines and blank lines, then one line per icon and weight, whitespace-separated:
`<name> <weight> <SVG path relative to DIR>`. The library holds Phosphor 2.1.1 icons only, by Phosphor's own names:
a name is Phosphor's SVG file stem without "-fill", so [a-z][a-z0-9-]{0,31} without "--", not ending in "-", and not
ending in "-fill". The weight is regular or fill, and the path is Phosphor's own file for that name and weight:
phosphor/regular/<name>.svg or phosphor/fill/<name>-fill.svg. Every name has exactly one line in each weight. In the
header a name's identifiers are its upper case with "-" as "_", plus "_FILL" for the fill weight (dice-six:
DICE_SIX_32, DICE_SIX_FILL_64); names hold no "_", so no two names share an identifier.

SHA256SUMS pins each SVG's content: one `<sha256>  <path>` line (sha256sum's format, lower-case hex, sorted bytewise
by path) for exactly the SVGs names.txt names, so `sha256sum -c SHA256SUMS` in DIR verifies it too. The generator
checks every SVG against it before rendering, so an edited SVG fails instead of regenerating cleanly. After a
deliberate change of source (a new icon, or a new Phosphor release), run --write-sums and commit the file.

Each SVG must be what Phosphor ships: one <svg viewBox="0 0 N N"> (fill absent or currentColor) whose children are
<path d="..."> elements only. Paths take the commands M L H V C S Q T A Z in both cases. Curves are flattened
(cubics and quadratics to 32 segments, arcs to one segment per pi/32 of sweep), scaled to the bitmap, every vertex
rounded to 1/4096 px, and filled by the nonzero winding rule with 16 sample lines per pixel row; a pixel is ink when
its coverage is at least THRESHOLD. Each icon is drawn in each weight at 32 px (small) and 64 px (medium). The fill
uses no libm call; arcs use math.sin, cos, and atan2, whose last-bit differences between platforms the rounding makes
very unlikely, but not impossible, to move a pixel; hence "run it on Linux" above.

The bitmaps use GfxRenderer::drawIcon's layout: square, 1 bit per pixel, MSB first, rows padded to whole bytes,
bit 0 = ink, stored rotated 90 degrees counter-clockwise, so stored (row, col) is drawn at (pixels - 1 - row, col).

Exit 0: the header (or, with --write-sums, SHA256SUMS) was written. Exit 1: a rule is broken: a malformed or
non-UTF-8 map, a bad name, weight, or path (anything but Phosphor's own file for the name and weight), a name given
twice in one weight or missing a weight, a SHA256SUMS that is not UTF-8 or has a line that is malformed or lists a
path twice, SVG content outside the subset above (a non-finite number included), or an icon that renders empty; the
message names the file, line, or name.
Exit 2: the script could not run: no names.txt, no SHA256SUMS (without --write-sums), a map line naming an SVG that
does not exist, or an unreadable or unwritable file.
Exit 3 (PIN_MISMATCH): the SVGs and SHA256SUMS disagree: an SVG whose SHA-256 differs from its line, an SVG names.txt
names that SHA256SUMS does not list, or a SHA256SUMS line for a path names.txt does not name. Restore the SVG, or,
after a deliberate change of source, run --write-sums; only this code calls for re-pinning.
"""

import argparse
import collections
import hashlib
import math
import pathlib
import re
import sys
import xml.etree.ElementTree as ET

import fork_common
from fork_common import Failure, SetupError


class PinMismatch(Failure):
    """The SVGs and SHA256SUMS disagree (exit PIN_MISMATCH): the one failure that re-pinning may fix."""


# The exit code for a PinMismatch (see the docstring's exit list).
PIN_MISMATCH = 3

REPO = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_ASSETS = REPO / 'assets' / 'game-icons'
DEFAULT_OUT = REPO / 'lib' / 'GameIcons' / 'GameIcons.generated.h'
MAP_NAME = 'names.txt'
SUMS_NAME = 'SHA256SUMS'
# One SHA256SUMS line as sha256sum writes it: the digest, a space, the mode (" " text or "*" binary), the path.
SUM_LINE = re.compile(r'([0-9a-f]{64}) [ *](\S.*)')

# Where every icon comes from, as the header's comments name it ({weight} filled in).
SOURCE = 'Phosphor 2.1.1 {weight}'
LICENCE_NOTICE = ('// Phosphor Icons: Copyright (c) 2023 Phosphor Icons, MIT licence; see '
                  'assets/game-icons/phosphor/LICENSE.')
# The weights every name ships in, in the header's index order (Weight::Regular is 0, Weight::Fill is 1).
WEIGHTS = ('regular', 'fill')
NAME = re.compile(r'[a-z][a-z0-9-]{0,31}')

SMALL_PIXELS = 32
MEDIUM_PIXELS = 64
SIZES = (SMALL_PIXELS, MEDIUM_PIXELS)

# A pixel is ink when at least this fraction of it is covered.
THRESHOLD = 0.5
SAMPLE_LINES = 16
# Vertices are rounded to 1/SUBPIXELS px, so the fill works on exact inputs, and a last-bit libm difference in the
# arc flattening (math.sin, cos, atan2) is very unlikely to move a pixel. The fill itself uses no libm call.
SUBPIXELS = 4096
CURVE_SEGMENTS = 32
ARC_STEP = math.pi / 32

SVG_NS = '{http://www.w3.org/2000/svg}'
NUMBER = re.compile(r'[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?')
COMMANDS = 'MmLlHhVvCcSsQqTtAaZz'
SEPARATORS = ' \t\r\n\f,'
NUMBER_START = '+-.0123456789'

Entry = collections.namedtuple('Entry', 'name weight path line source')


def phosphor_path(name, weight):
    """Phosphor's own SVG for a name in a weight, relative to the assets folder."""
    return f'phosphor/{weight}/{name}.svg' if weight == 'regular' else f'phosphor/{weight}/{name}-{weight}.svg'


def identifier(name, weight, pixels):
    """The header's array name for a name's bitmap in a weight at a size: dice-six fill 64 is DICE_SIX_FILL_64."""
    suffix = '' if weight == 'regular' else f'_{weight.upper()}'
    return f'{name.upper().replace("-", "_")}{suffix}_{pixels}'


def read_map(assets):
    """The map's names, sorted bytewise, each as {weight: Entry} holding every weight in WEIGHTS."""
    map_path = assets / MAP_NAME
    try:
        text = map_path.read_bytes().decode('utf-8')
    except OSError as exc:
        raise SetupError(f'cannot read {map_path}: {exc}')
    except UnicodeDecodeError as exc:
        raise Failure(f'{map_path} is not UTF-8: {exc}')
    entries = {weight: {} for weight in WEIGHTS}
    for number, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith('#'):
            continue
        where = f'{map_path}:{number}'
        fields = line.split()
        if len(fields) != 3:
            raise Failure(f'{where}: expected "<name> <weight> <path>", got {raw!r}')
        name, weight, rel = fields
        if not NAME.fullmatch(name) or '--' in name or name.endswith('-') or name.endswith('-fill'):
            raise Failure(f'{where}: bad name {name!r} (want Phosphor\'s name: [a-z][a-z0-9-]{{0,31}} without "--", '
                          'not ending in "-" or "-fill")')
        if weight not in WEIGHTS:
            raise Failure(f'{where}: unknown weight {weight!r} (want one of {", ".join(WEIGHTS)})')
        want = phosphor_path(name, weight)
        if rel != want:
            raise Failure(f'{where}: the path {rel!r} is not Phosphor\'s {weight} {name} (want {want})')
        if name in entries[weight]:
            raise Failure(f'{where}: {name} {weight} is already named on line {entries[weight][name].line}')
        entries[weight][name] = Entry(name, weight, rel, number, SOURCE.format(weight=weight))
    names = set().union(*entries.values())
    if not names:
        raise Failure(f'{map_path} names no icons')
    for name in sorted(names, key=str.encode):
        for weight in WEIGHTS:
            if name not in entries[weight]:
                raise Failure(f'{map_path}: {name} has no {weight} line (every name ships in {" and ".join(WEIGHTS)})')
    return [{weight: entries[weight][name] for weight in WEIGHTS} for name in sorted(names, key=str.encode)]


def local_name(tag):
    return tag[len(SVG_NS):] if tag.startswith(SVG_NS) else tag


def parse_svg(text, where):
    """(viewBox size N, [path d strings]) of an SVG in the supported subset; anything else is a Failure."""
    try:
        root = ET.fromstring(text)
    except ET.ParseError as exc:
        raise Failure(f'{where}: not well-formed XML ({exc})')
    if root.tag != SVG_NS + 'svg':
        raise Failure(f'{where}: the root is <{local_name(root.tag)}>, not an SVG <svg>')
    for key in root.attrib:
        if key not in ('viewBox', 'fill'):
            raise Failure(f'{where}: unsupported attribute {local_name(key)}= on <svg>')
    if root.get('fill') not in (None, 'currentColor'):
        raise Failure(f'{where}: unsupported fill="{root.get("fill")}" on <svg> (want currentColor)')
    box = [token for token in re.split(r'[\s,]+', root.get('viewBox', '').strip()) if token]
    try:
        values = [float(token) for token in box]
    except ValueError:
        values = []
    if len(values) != 4 or values[0] != 0 or values[1] != 0 or values[2] != values[3] or not values[2] > 0:
        raise Failure(f'{where}: viewBox="{root.get("viewBox")}" is not "0 0 N N"')
    if (root.text or '').strip():
        raise Failure(f'{where}: unsupported text inside <svg>')
    paths = []
    for child in root:
        if child.tag != SVG_NS + 'path':
            raise Failure(f'{where}: unsupported element <{local_name(child.tag)}> (only <path> is)')
        for key in child.attrib:
            if key != 'd':
                raise Failure(f'{where}: unsupported attribute {local_name(key)}= on <path> (only d= is)')
        if 'd' not in child.attrib:
            raise Failure(f'{where}: a <path> has no d=')
        if len(child) or (child.text or '').strip() or (child.tail or '').strip():
            raise Failure(f'{where}: unsupported content inside or after a <path>')
        paths.append(child.get('d'))
    if not paths:
        raise Failure(f'{where}: no <path>')
    return values[2], paths


class PathReader:
    """Reads the numbers and flags of one path's d attribute."""

    def __init__(self, d, where):
        self.d = d
        self.where = where
        self.pos = 0

    def skip(self):
        while self.pos < len(self.d) and self.d[self.pos] in SEPARATORS:
            self.pos += 1

    def done(self):
        self.skip()
        return self.pos >= len(self.d)

    def peek(self):
        self.skip()
        return self.d[self.pos] if self.pos < len(self.d) else ''

    def number(self):
        self.skip()
        match = NUMBER.match(self.d, self.pos)
        if not match:
            raise Failure(f'{self.where}: expected a number at offset {self.pos} of d="{self.d}"')
        value = float(match.group())
        if not math.isfinite(value):
            raise Failure(f'{self.where}: the number at offset {self.pos} of d="{self.d}" is out of range')
        self.pos = match.end()
        return value

    def flag(self):
        self.skip()
        if self.pos >= len(self.d) or self.d[self.pos] not in '01':
            raise Failure(f'{self.where}: expected an arc flag (0 or 1) at offset {self.pos} of d="{self.d}"')
        self.pos += 1
        return self.d[self.pos - 1] == '1'


def cubic_points(p0, p1, p2, p3):
    points = []
    for i in range(1, CURVE_SEGMENTS + 1):
        t = i / CURVE_SEGMENTS
        u = 1 - t
        a, b, c, e = u * u * u, 3 * u * u * t, 3 * u * t * t, t * t * t
        points.append((a * p0[0] + b * p1[0] + c * p2[0] + e * p3[0], a * p0[1] + b * p1[1] + c * p2[1] + e * p3[1]))
    points[-1] = p3
    return points


def quadratic_points(p0, p1, p2):
    points = []
    for i in range(1, CURVE_SEGMENTS + 1):
        t = i / CURVE_SEGMENTS
        u = 1 - t
        a, b, c = u * u, 2 * u * t, t * t
        points.append((a * p0[0] + b * p1[0] + c * p2[0], a * p0[1] + b * p1[1] + c * p2[1]))
    points[-1] = p2
    return points


def arc_points(start, rx, ry, rotation, large, sweep, end):
    """The arc from start to end (SVG's endpoint parameterization, F.6.5 and F.6.6) as points after start."""
    (x1, y1), (x2, y2) = start, end
    if (x1, y1) == (x2, y2):
        return []
    rx, ry = abs(rx), abs(ry)
    if rx * rx == 0 or ry * ry == 0:  # a zero radius, or one so small its square underflows: a straight line
        return [end]
    phi = math.radians(rotation % 360)
    cos_phi, sin_phi = math.cos(phi), math.sin(phi)
    dx, dy = (x1 - x2) / 2, (y1 - y2) / 2
    x1p = cos_phi * dx + sin_phi * dy
    y1p = -sin_phi * dx + cos_phi * dy
    scale = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry)
    if scale > 1:  # radii too small to reach: scaled up until they just do
        rx, ry = rx * math.sqrt(scale), ry * math.sqrt(scale)
    numerator = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
    denominator = rx * rx * y1p * y1p + ry * ry * x1p * x1p
    if denominator == 0:  # the endpoints differ by too little to place a centre (subnormal offsets)
        return [end]
    factor = math.sqrt(max(0.0, numerator / denominator))
    if large == sweep:
        factor = -factor
    cxp, cyp = factor * rx * y1p / ry, -factor * ry * x1p / rx
    cx = cos_phi * cxp - sin_phi * cyp + (x1 + x2) / 2
    cy = sin_phi * cxp + cos_phi * cyp + (y1 + y2) / 2
    ux, uy = (x1p - cxp) / rx, (y1p - cyp) / ry
    vx, vy = (-x1p - cxp) / rx, (-y1p - cyp) / ry
    theta = math.atan2(uy, ux)
    delta = math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
    if not sweep and delta > 0:
        delta -= 2 * math.pi
    elif sweep and delta < 0:
        delta += 2 * math.pi
    # The small allowance keeps a half turn at 32 segments however atan2 rounds pi.
    segments = max(1, math.ceil(abs(delta) / ARC_STEP - 1e-9))
    points = []
    for i in range(1, segments):
        angle = theta + delta * i / segments
        ex, ey = rx * math.cos(angle), ry * math.sin(angle)
        points.append((cx + cos_phi * ex - sin_phi * ey, cy + sin_phi * ex + cos_phi * ey))
    points.append(end)
    return points


def contours_of(d, where):
    """The path's subpaths as point lists in viewBox units; filling closes each one."""
    reader = PathReader(d, where)
    contours = []
    current = None
    x = y = start_x = start_y = 0.0
    command = None
    last_cubic = last_quad = None  # the previous C/S's second control point, the previous Q/T's control point

    def to(points):
        nonlocal current
        if current is None:  # a drawing command right after Z starts a subpath at the last one's start
            current = [(x, y)]
            contours.append(current)
        current.extend(points)

    if not reader.done() and reader.peek() not in 'Mm':
        raise Failure(f'{where}: d="{d}" does not start with M')
    while not reader.done():
        char = reader.peek()
        if char in NUMBER_START:
            if command in ('Z', 'z'):
                raise Failure(f'{where}: a number follows Z at offset {reader.pos} of d="{d}"')
            command = {'M': 'L', 'm': 'l'}.get(command, command)  # coordinates after M's first pair are lines
        elif char in COMMANDS:
            command = char
            reader.pos += 1
        else:
            raise Failure(f'{where}: unknown path command {char!r} at offset {reader.pos} of d="{d}"')
        kind = command.upper()
        dx, dy = (x, y) if command.islower() else (0.0, 0.0)
        cubic = quad = None
        if kind == 'Z':
            current = None
            x, y = start_x, start_y
        elif kind == 'M':
            x, y = reader.number() + dx, reader.number() + dy
            start_x, start_y = x, y
            current = [(x, y)]
            contours.append(current)
        elif kind == 'L':
            nx, ny = reader.number() + dx, reader.number() + dy
            to([(nx, ny)])
            x, y = nx, ny
        elif kind == 'H':
            x = reader.number() + dx
            to([(x, y)])
        elif kind == 'V':
            y = reader.number() + dy
            to([(x, y)])
        elif kind in ('C', 'S'):
            if kind == 'C':
                c1 = (reader.number() + dx, reader.number() + dy)
            else:
                c1 = (2 * x - last_cubic[0], 2 * y - last_cubic[1]) if last_cubic else (x, y)
            c2 = (reader.number() + dx, reader.number() + dy)
            end = (reader.number() + dx, reader.number() + dy)
            to(cubic_points((x, y), c1, c2, end))
            (x, y), cubic = end, c2
        elif kind in ('Q', 'T'):
            if kind == 'Q':
                control = (reader.number() + dx, reader.number() + dy)
            else:
                control = (2 * x - last_quad[0], 2 * y - last_quad[1]) if last_quad else (x, y)
            end = (reader.number() + dx, reader.number() + dy)
            to(quadratic_points((x, y), control, end))
            (x, y), quad = end, control
        else:  # A
            rx, ry, rotation = reader.number(), reader.number(), reader.number()
            large, sweep = reader.flag(), reader.flag()
            end = (reader.number() + dx, reader.number() + dy)
            to(arc_points((x, y), rx, ry, rotation, large, sweep, end))
            x, y = end
        last_cubic, last_quad = cubic, quad
    return contours


def rasterize(contours, view, pixels):
    """The filled contours at pixels x pixels: rows of booleans, True for ink, in drawn orientation ([y][x])."""
    scale = pixels / view
    edges_by_row = [[] for _ in range(pixels)]
    for contour in contours:
        points = [(math.floor(px * scale * SUBPIXELS + 0.5), math.floor(py * scale * SUBPIXELS + 0.5))
                  for px, py in contour]
        for (x0, y0), (x1, y1) in zip(points, points[1:] + points[:1]):
            if y0 == y1:
                continue
            low, high = min(y0, y1), max(y0, y1)
            first = max(0, low // SUBPIXELS)
            last = min(pixels - 1, (high - 1) // SUBPIXELS)
            for row in range(first, last + 1):
                edges_by_row[row].append((x0, y0, x1, y1))
    step = SUBPIXELS // (2 * SAMPLE_LINES)
    grid = []
    for row in range(pixels):
        coverage = [0.0] * pixels
        for k in range(SAMPLE_LINES):
            sample = row * SUBPIXELS + (2 * k + 1) * step
            crossings = []
            for x0, y0, x1, y1 in edges_by_row[row]:
                # Half-open in y, so a vertex shared by two edges is crossed once.
                if y0 <= sample < y1:
                    direction = 1
                elif y1 <= sample < y0:
                    direction = -1
                else:
                    continue
                crossings.append((x0 + (sample - y0) * (x1 - x0) / (y1 - y0), direction))
            crossings.sort()
            winding = 0
            left = 0.0
            for cross, direction in crossings:
                before = winding
                winding += direction
                if before == 0 and winding != 0:
                    left = cross
                elif before != 0 and winding == 0:
                    add_span(coverage, left / SUBPIXELS, cross / SUBPIXELS)
        grid.append([value >= THRESHOLD * SAMPLE_LINES for value in coverage])
    return grid


def add_span(coverage, left, right):
    """Adds one sample line's span [left, right) (pixels) to each column it overlaps."""
    left, right = max(left, 0.0), min(right, float(len(coverage)))
    if left >= right:
        return
    for column in range(int(left), min(len(coverage), math.ceil(right))):
        overlap = min(right, column + 1) - max(left, column)
        if overlap > 0:
            coverage[column] += overlap


def pack(grid):
    """drawIcon's bytes for a square [y][x] ink grid: rotated counter-clockwise, MSB first, bit 0 = ink."""
    pixels = len(grid)
    row_bytes = (pixels + 7) // 8
    out = bytearray(b'\xff' * (pixels * row_bytes))
    for y in range(pixels):
        for x in range(pixels):
            if grid[y][x]:
                row, col = pixels - 1 - x, y
                out[row * row_bytes + col // 8] &= ~(0x80 >> (col % 8)) & 0xFF
    return bytes(out)


def render(text, where):
    """{pixels: packed bitmap} of one SVG at each size; an icon with no ink at a size is a Failure."""
    view, paths = parse_svg(text, where)
    contours = []
    for d in paths:
        contours.extend(contours_of(d, where))
    bitmaps = {}
    for pixels in SIZES:
        grid = rasterize(contours, view, pixels)
        if not any(any(row) for row in grid):
            raise Failure(f'{where}: renders empty at {pixels} px')
        bitmaps[pixels] = pack(grid)
    return bitmaps


def array_lines(name, size_bytes, data):
    lines = [f'inline constexpr uint8_t {name}[{size_bytes}] = {{']
    for i in range(0, len(data), 16):
        lines.append('    ' + ' '.join(f'0x{byte:02X},' for byte in data[i:i + 16]))
    lines.append('};')
    return lines


def header_text(icons):
    """The header for [{weight: (Entry, {pixels: bytes})}], sorted by name, every weight of WEIGHTS in each."""
    small_bytes = SMALL_PIXELS * ((SMALL_PIXELS + 7) // 8)
    medium_bytes = MEDIUM_PIXELS * ((MEDIUM_PIXELS + 7) // 8)
    lines = [
        f'// Generated by scripts/gen_game_icons.py from assets/game-icons/{MAP_NAME}; never edit by hand.',
        '//',
        '// Each bitmap is square, 1 bit per pixel, MSB first, rows padded to whole bytes, bit 0 = ink, and stored',
        '// rotated 90 degrees counter-clockwise: stored (row, col) is drawn at (pixels - 1 - row, col), as',
        '// GfxRenderer::drawIcon draws it. ICONS is sorted by name, bytewise; each name has a bitmap per weight and',
        '// size. Names are Phosphor\'s own; identifiers are the name in upper case with "-" as "_", plus _FILL for',
        '// the fill weight.',
        LICENCE_NOTICE,
        '#pragma once',
        '',
        '#include <cstddef>',
        '#include <cstdint>',
        '',
        'namespace GameIcons {',
        '',
        f'inline constexpr int SMALL_PIXELS = {SMALL_PIXELS};',
        f'inline constexpr int MEDIUM_PIXELS = {MEDIUM_PIXELS};',
        f'inline constexpr size_t SMALL_BYTES = {small_bytes};',
        f'inline constexpr size_t MEDIUM_BYTES = {medium_bytes};',
        '',
        f'enum class Weight : uint8_t {{ {", ".join(weight.capitalize() for weight in WEIGHTS)} }};  '
        '// indexes Icon::small and Icon::medium',
        f'inline constexpr size_t WEIGHT_COUNT = {len(WEIGHTS)};',
        '',
        'struct Icon {',
        '  const char* name;',
        '  const uint8_t* small[WEIGHT_COUNT];',
        '  const uint8_t* medium[WEIGHT_COUNT];',
        '};',
    ]
    for weights in icons:
        for weight in WEIGHTS:
            entry, bitmaps = weights[weight]
            lines.append('')
            lines.append(f'// {entry.name}: {entry.source}, {entry.path}')
            lines.extend(array_lines(identifier(entry.name, weight, SMALL_PIXELS), 'SMALL_BYTES',
                                     bitmaps[SMALL_PIXELS]))
            lines.extend(array_lines(identifier(entry.name, weight, MEDIUM_PIXELS), 'MEDIUM_BYTES',
                                     bitmaps[MEDIUM_PIXELS]))
    lines.append('')
    lines.append('inline constexpr Icon ICONS[] = {')
    for weights in icons:
        name = weights[WEIGHTS[0]][0].name
        small = ', '.join(identifier(name, weight, SMALL_PIXELS) for weight in WEIGHTS)
        medium = ', '.join(identifier(name, weight, MEDIUM_PIXELS) for weight in WEIGHTS)
        lines.append(f'    {{"{name}", {{{small}}}, {{{medium}}}}},')
    lines.append('};')
    lines.append('inline constexpr size_t ICON_COUNT = sizeof(ICONS) / sizeof(ICONS[0]);')
    lines.append('')
    lines.append('}  // namespace GameIcons')
    return '\n'.join(lines) + '\n'


def read_svg(assets, entry):
    """(path, bytes) of a map entry's SVG; a missing or unreadable file is a SetupError."""
    path = assets / entry.path
    if not path.is_file():
        raise SetupError(f'{assets / MAP_NAME}:{entry.line}: {entry.name} {entry.weight}\'s SVG {path} '
                         'does not exist')
    try:
        return path, path.read_bytes()
    except OSError as exc:
        raise SetupError(f'cannot read {path}: {exc}')


def read_sums(assets, named):
    """{path: (sha256 hex, line number)} from SHA256SUMS, whose every line is well formed and names a distinct path in
    `named` (the map's SVG paths); a path in `named` it does not list is found when that SVG is checked."""
    sums_path = assets / SUMS_NAME
    try:
        text = sums_path.read_bytes().decode('utf-8')
    except OSError as exc:
        raise SetupError(f'cannot read {sums_path}: {exc}; write it with --write-sums')
    except UnicodeDecodeError as exc:
        raise Failure(f'{sums_path} is not UTF-8: {exc}')
    sums = {}
    for number, line in enumerate(text.splitlines(), 1):
        where = f'{sums_path}:{number}'
        match = SUM_LINE.fullmatch(line)
        if not match:
            raise Failure(f'{where}: expected "<sha256>  <path>" (sha256sum\'s format, lower-case hex), got {line!r}')
        digest, rel = match.groups()
        if rel in sums:
            raise Failure(f'{where}: {rel} is already listed on line {sums[rel][1]}')
        if rel not in named:
            raise PinMismatch(f'{where}: {rel} is not an SVG {MAP_NAME} names; list exactly the SVGs it names '
                              '(--write-sums rewrites the file)')
        sums[rel] = (digest, number)
    return sums


def check_sum(assets, sums, entry, data):
    """PinMismatch unless SHA256SUMS lists the entry's SVG with the SHA-256 of `data`, its bytes."""
    sums_path = assets / SUMS_NAME
    if entry.path not in sums:
        raise PinMismatch(f'{sums_path}: {entry.path} ({entry.name} {entry.weight}) is not listed; after checking '
                          'the SVG is Phosphor\'s own, run --write-sums')
    expected, number = sums[entry.path]
    actual = hashlib.sha256(data).hexdigest()
    if actual != expected:
        raise PinMismatch(f'{assets / entry.path}: its SHA-256 {actual} differs from {sums_path}:{number} '
                          f'({expected}); restore Phosphor\'s file, or, for a deliberate change of source, run '
                          '--write-sums')


def generate(assets, out):
    entries = read_map(assets)
    sums = read_sums(assets, {entry.path for weights in entries for entry in weights.values()})
    icons = []
    for weights in entries:
        rendered = {}
        for weight, entry in weights.items():
            path, text = read_svg(assets, entry)
            check_sum(assets, sums, entry, text)
            rendered[weight] = (entry, render(text, str(path)))
        icons.append(rendered)
    try:
        with open(out, 'w', encoding='utf-8', newline='\n') as header:
            header.write(header_text(icons))
    except OSError as exc:
        raise SetupError(f'cannot write {out}: {exc}')
    print(f'wrote {out} ({len(icons)} icons, {len(WEIGHTS)} weights each)')


def write_sums(assets):
    """Write SHA256SUMS for the SVGs the map names, sorted bytewise by path; no header."""
    lines = []
    for weights in read_map(assets):
        for entry in weights.values():
            _, data = read_svg(assets, entry)
            lines.append((entry.path, hashlib.sha256(data).hexdigest()))
    lines.sort(key=lambda line: line[0].encode())
    sums_path = assets / SUMS_NAME
    try:
        with open(sums_path, 'w', encoding='utf-8', newline='\n') as sums:
            sums.write(''.join(f'{digest}  {rel}\n' for rel, digest in lines))
    except OSError as exc:
        raise SetupError(f'cannot write {sums_path}: {exc}')
    print(f'wrote {sums_path} ({len(lines)} SVGs)')


def main(argv=None):
    parser = argparse.ArgumentParser(description='Generate the game icon library header from its SVGs.')
    parser.add_argument('--assets', type=pathlib.Path, default=DEFAULT_ASSETS,
                        help='the folder holding names.txt, SHA256SUMS, and the SVGs')
    parser.add_argument('--out', type=pathlib.Path, default=DEFAULT_OUT, help='the header to write')
    parser.add_argument('--write-sums', action='store_true',
                        help=f'rewrite <assets>/{SUMS_NAME} from the SVGs names.txt names, and write no header')
    args = parser.parse_args(argv)
    if args.write_sums:
        return fork_common.exit_code(lambda: write_sums(args.assets))

    def step():
        try:
            generate(args.assets, args.out)
        except PinMismatch as exc:
            print(f'error: {exc}', file=sys.stderr)
            return PIN_MISMATCH
        return None

    return fork_common.exit_code(step)


if __name__ == '__main__':
    sys.exit(main())
