#!/usr/bin/env python3
"""
Tests for gen_game_icons.py. Standard library only:

    python3 scripts/gen_game_icons_test.py [-v]

The rasterizer cases draw small SVGs and compare pixels; the main() cases build an assets folder in a temp directory
(with a SHA256SUMS for the SVGs its map names, unless a case says otherwise) and assert the exit code of every
outcome, so a regression that makes a bad input pass is caught. SumsTest checks the committed SHA256SUMS against the
committed SVGs and names.txt.
"""

import contextlib
import hashlib
import io
import pathlib
import shutil
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import gen_game_icons as ggi  # noqa: E402

SVG = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {view} {view}" fill="currentColor">{body}</svg>'


def svg(*paths, view=32):
    return SVG.format(view=view, body=''.join(f'<path d="{d}"/>' for d in paths))


def grid_of(text, pixels=32):
    view, paths = ggi.parse_svg(text, 'test.svg')
    contours = []
    for d in paths:
        contours.extend(ggi.contours_of(d, 'test.svg'))
    return ggi.rasterize(contours, view, pixels)


def ink(grid):
    """The set of (x, y) ink pixels."""
    return {(x, y) for y, row in enumerate(grid) for x, value in enumerate(row) if value}


def square(left, top, right, bottom):
    return {(x, y) for x in range(left, right) for y in range(top, bottom)}


class RasterizerTest(unittest.TestCase):

    def test_a_square_is_pixel_exact(self):
        self.assertEqual(ink(grid_of(svg('M8,8H24V24H8Z'))), square(8, 8, 24, 24))
        # At 64 px from a 32-unit viewBox, every unit is two pixels.
        self.assertEqual(ink(grid_of(svg('M8,8H24V24H8Z'), 64)), square(16, 16, 48, 48))

    def test_relative_forms_equal_absolute_ones(self):
        pairs = [
            ('M8,8H24V24H8Z', 'm8,8h16v16h-16z'),
            ('M8,8L24,8L24,24L8,24Z', 'M8,8l16,0l0,16l-16,0z'),
            ('M8,8L24,8 24,24 8,24Z', 'm8 8 16 0 0 16-16 0z'),  # implicit lineto after m, packed signs
            ('M4,16C4,4 28,4 28,16S4,28 4,16Z', 'm4,16c0-12 24-12 24,0s-24,12-24,0z'),
            ('M4,16Q16,0 28,16T4,16Z', 'm4,16q12-16 24,0t-24,0z'),
            ('M6,16A10,10,0,0,1,26,16A10,10,0,0,1,6,16Z', 'm6,16a10,10,0,0,1,20,0a10,10,0,0,1-20,0z'),
        ]
        for absolute, relative in pairs:
            with self.subTest(absolute=absolute):
                self.assertTrue(ink(grid_of(svg(absolute))))
                self.assertEqual(ink(grid_of(svg(absolute))), ink(grid_of(svg(relative))))

    def test_h_and_v_draw_axis_lines(self):
        self.assertEqual(ink(grid_of(svg('M2,2H10V6H2Z'))), square(2, 2, 10, 6))
        self.assertEqual(ink(grid_of(svg('M2,2V10H6V2Z'))), square(2, 2, 6, 10))

    def test_s_reflects_the_previous_control_point(self):
        # S after C mirrors C's second control point; S alone uses the current point, which makes a different shape.
        with_c = ink(grid_of(svg('M4,16C4,4 28,4 28,16S4,28 4,16Z')))
        self.assertEqual(with_c, ink(grid_of(svg('M4,16C4,4 28,4 28,16C28,28 4,28 4,16Z'))))
        self.assertNotEqual(ink(grid_of(svg('M4,16L28,16S4,28 4,16Z'))),
                            ink(grid_of(svg('M4,16L28,16C28,28 4,28 4,16Z'))))

    def test_t_reflects_the_previous_control_point(self):
        with_q = ink(grid_of(svg('M4,16Q16,0 28,16T4,16Z')))
        self.assertEqual(with_q, ink(grid_of(svg('M4,16Q16,0 28,16Q40,32 4,16Z'))))
        # T without a Q before it is a straight line.
        self.assertEqual(ink(grid_of(svg('M4,4L28,4T28,28L4,28Z'))), square(4, 4, 28, 28))

    def test_a_circle_from_two_arcs_is_symmetric(self):
        grid = grid_of(svg('M4,16A12,12,0,0,1,28,16A12,12,0,0,1,4,16Z'))
        pixels = ink(grid)
        self.assertIn((16, 16), pixels)
        self.assertNotIn((0, 0), pixels)
        self.assertEqual(pixels, {(31 - x, y) for x, y in pixels})
        self.assertEqual(pixels, {(x, 31 - y) for x, y in pixels})
        self.assertEqual(pixels, {(y, x) for x, y in pixels})
        # Radii too small to reach the endpoint are scaled up: the same circle.
        self.assertEqual(pixels, ink(grid_of(svg('M4,16A1,1,0,0,1,28,16A1,1,0,0,1,4,16Z'))))
        # A zero radius is a straight line.
        self.assertEqual(ink(grid_of(svg('M4,4H28V28A0,0,0,0,1,4,28Z'))), square(4, 4, 28, 28))

    def test_arc_flags_pick_the_arc(self):
        # From (4,16) to (28,16) on a radius-12 circle: sweep 1 goes through the top half (y < 16), sweep 0 the bottom.
        top = ink(grid_of(svg('M4,16A12,12,0,0,1,28,16Z')))
        bottom = ink(grid_of(svg('M4,16A12,12,0,0,0,28,16Z')))
        self.assertTrue(all(y < 16 for _, y in top))
        self.assertTrue(all(y >= 16 for _, y in bottom))

    def test_a_ring_has_a_hole_and_a_same_direction_inner_contour_stays_filled(self):
        # Nonzero winding: the inner square drawn the other way cuts a hole; drawn the same way it adds nothing.
        ring = ink(grid_of(svg('M4,4H28V28H4Z M10,10V22H22V10Z')))
        self.assertEqual(ring, square(4, 4, 28, 28) - square(10, 10, 22, 22))
        filled = ink(grid_of(svg('M4,4H28V28H4Z M10,10H22V22H10Z')))
        self.assertEqual(filled, square(4, 4, 28, 28))
        # The same in two <path> elements.
        self.assertEqual(ink(grid_of(svg('M4,4H28V28H4Z', 'M10,10V22H22V10Z'))), ring)

    def test_phosphor_circle_is_a_ring(self):
        text = (ggi.REPO / 'assets' / 'game-icons' / 'phosphor' / 'regular' / 'circle.svg').read_text()
        pixels = ink(grid_of(text))
        self.assertNotIn((16, 16), pixels)
        self.assertIn((16, 4), pixels)

    def test_coverage_below_the_threshold_is_not_ink(self):
        # A quarter-pixel-wide sliver covers 25% of each pixel in its column.
        self.assertEqual(ink(grid_of(svg('M8,8H8.25V24H8Z'))), set())
        self.assertEqual(ink(grid_of(svg('M8,8H8.5V24H8Z'))), square(8, 8, 9, 24))

    def test_a_drawn_ink_pixel_lands_at_stored_row_px_minus_1_minus_x_col_y_bit_0(self):
        for pixels in (32, 64):
            grid = [[False] * pixels for _ in range(pixels)]
            x, y = 3, 13
            grid[y][x] = True
            data = ggi.pack(grid)
            row_bytes = pixels // 8
            self.assertEqual(len(data), pixels * row_bytes)
            row, col = pixels - 1 - x, y
            index = row * row_bytes + col // 8
            self.assertEqual(data[index], 0xFF & ~(0x80 >> (col % 8)))
            self.assertEqual(data[:index] + data[index + 1:], b'\xff' * (len(data) - 1))
        # Through the rasterizer: the pixel at x 3, y 13.
        data = ggi.pack(grid_of(svg('M3,13h1v1h-1z')))
        self.assertEqual(data[(31 - 3) * 4 + 13 // 8], 0xFF & ~(0x80 >> (13 % 8)))
        self.assertEqual(sum(bin(0xFF ^ byte).count('1') for byte in data), 1)


class ParseTest(unittest.TestCase):

    def assertRejected(self, text, fragment):
        with self.assertRaises(ggi.Failure) as caught:
            grid_of(text)
        self.assertIn(fragment, str(caught.exception))

    def test_unsupported_content_is_a_failure(self):
        ns = 'xmlns="http://www.w3.org/2000/svg"'
        self.assertRejected(f'<svg {ns} viewBox="0 0 32 32"><rect width="4" height="4"/></svg>', '<rect>')
        self.assertRejected(f'<svg {ns} viewBox="0 0 32 32"><path d="M0,0H4V4Z" transform="scale(2)"/></svg>',
                            'transform=')
        self.assertRejected(f'<svg {ns} viewBox="0 0 32 32"><path d="M0,0H4V4Z" fill-rule="evenodd"/></svg>',
                            'fill-rule=')
        self.assertRejected(f'<svg {ns} viewBox="0 0 32 32" transform="rotate(9)"><path d="M0,0H4V4Z"/></svg>',
                            'transform=')
        self.assertRejected(f'<svg {ns} viewBox="0 0 32 32" fill="red"><path d="M0,0H4V4Z"/></svg>', 'fill=')
        self.assertRejected(f'<svg {ns} viewBox="0 0 32 16"><path d="M0,0H4V4Z"/></svg>', 'viewBox')
        self.assertRejected(f'<svg {ns} viewBox="0 0 32 32"><g><path d="M0,0H4V4Z"/></g></svg>', '<g>')
        self.assertRejected('<svg viewBox="0 0 32 32"><path d="M0,0H4V4Z"/></svg>', 'not an SVG')
        self.assertRejected(f'<svg {ns} viewBox="0 0 32 32"></svg>', 'no <path>')
        self.assertRejected('<svg', 'not well-formed')

    def test_bad_path_data_is_a_failure(self):
        self.assertRejected(svg('M0,0B4,4Z'), "unknown path command 'B'")
        self.assertRejected(svg('L0,0H4V4Z'), 'does not start with M')
        self.assertRejected(svg('M0,0L4'), 'expected a number')
        self.assertRejected(svg('M0,0A4,4,0,2,1,8,8Z'), 'arc flag')
        self.assertRejected(svg('M0,0H4V4Z 5'), 'number follows Z')
        self.assertRejected(svg('M0,0H1e999V4Z'), 'offset 5')
        self.assertRejected(svg('M0,0A1e400,4,0,0,1,8,8Z'), 'out of range')

    def test_an_arc_to_a_nearly_equal_endpoint_is_a_line(self):
        # A subnormal offset leaves no room to place a centre: the arc is a straight line, not a crash.
        self.assertEqual(ggi.arc_points((0.0, 0.0), 5, 5, 0, False, True, (0.0, 1e-200)), [(0.0, 1e-200)])
        self.assertEqual(ggi.arc_points((0.0, 0.0), 1e-200, 1, 0, False, True, (8.0, 8.0)), [(8.0, 8.0)])
        self.assertEqual(ink(grid_of(svg('M4,4H28V28H4Z M0 0a5 5 0 0 1 0 1e-200Z'))),
                         ink(grid_of(svg('M4,4H28V28H4Z'))))

    def test_arc_flags_may_run_into_the_next_number(self):
        self.assertEqual(ink(grid_of(svg('M4,16a12 12 0 0120 0a12 12 0 01-20 0z'))),
                         ink(grid_of(svg('M4,16a12,12,0,0,1,20,0a12,12,0,0,1,-20,0z'))))


PHOSPHOR = ggi.REPO / 'assets' / 'game-icons' / 'phosphor'
# Two names in both weights; dice-six carries a hyphen, and x is listed first but sorts last.
GOOD_MAP = ('x regular phosphor/regular/x.svg\nx fill phosphor/fill/x-fill.svg\n'
            'dice-six regular phosphor/regular/dice-six.svg\ndice-six fill phosphor/fill/dice-six-fill.svg\n')
X_BOTH = 'x regular phosphor/regular/x.svg\nx fill phosphor/fill/x-fill.svg\n'


def sums_text(assets, paths):
    """SHA256SUMS text for the given SVG paths under assets, computed here rather than by the script."""
    rows = sorted(paths, key=str.encode)
    return ''.join(f'{hashlib.sha256((assets / rel).read_bytes()).hexdigest()}  {rel}\n' for rel in rows)


def map_paths(names):
    """The SVG paths a map's three-field lines name, as read without the script's rules."""
    paths = set()
    for line in names.splitlines():
        fields = line.split()
        if len(fields) == 3 and not line.lstrip().startswith('#'):
            paths.add(fields[2])
    return paths


class MainTest(unittest.TestCase):

    def setUp(self):
        self.tmp = pathlib.Path(tempfile.mkdtemp(prefix='gen-game-icons-test-'))
        self.assets = self.tmp / 'assets'
        (self.assets / 'phosphor' / 'regular').mkdir(parents=True)
        (self.assets / 'phosphor' / 'fill').mkdir(parents=True)
        for name in ('x', 'dice-six'):
            shutil.copy(PHOSPHOR / 'regular' / f'{name}.svg', self.assets / 'phosphor' / 'regular' / f'{name}.svg')
            shutil.copy(PHOSPHOR / 'fill' / f'{name}-fill.svg', self.assets / 'phosphor' / 'fill' / f'{name}-fill.svg')
        self.out = self.tmp / 'out.h'

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def run_main(self, names=GOOD_MAP, out=None, sums=True, extra=()):
        """main() on the temp assets folder; with `sums`, first writes a SHA256SUMS for the SVGs `names` names that
        exist (a map left in place when `names` is None)."""
        if names is not None:
            (self.assets / 'names.txt').write_text(names)
            if sums:
                paths = {rel for rel in map_paths(names) if (self.assets / rel).is_file()}
                (self.assets / ggi.SUMS_NAME).write_text(sums_text(self.assets, paths))
        stderr = io.StringIO()
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(stderr):
            code = ggi.main(['--assets', str(self.assets), '--out', str(out or self.out), *extra])
        return code, stderr.getvalue()

    def main_only(self, *args):
        """main() on the temp assets folder as it is, with --assets and `args`."""
        stderr = io.StringIO()
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(stderr):
            code = ggi.main(['--assets', str(self.assets), *args])
        return code, stderr.getvalue()

    def write_svg(self, name, text):
        (self.assets / name).write_text(text)

    def test_a_good_map_writes_the_header_sorted_by_name(self):
        code, err = self.run_main('# comment\n\n' + GOOD_MAP)
        self.assertEqual(code, 0, err)
        text = self.out.read_text()
        self.assertIn('enum class Weight : uint8_t { Regular, Fill };', text)
        self.assertIn('inline constexpr size_t WEIGHT_COUNT = 2;', text)
        self.assertIn('  const uint8_t* small[WEIGHT_COUNT];\n  const uint8_t* medium[WEIGHT_COUNT];\n', text)
        for array in ('DICE_SIX_32[SMALL_BYTES]', 'DICE_SIX_64[MEDIUM_BYTES]', 'DICE_SIX_FILL_32[SMALL_BYTES]',
                      'DICE_SIX_FILL_64[MEDIUM_BYTES]', 'X_32[SMALL_BYTES]', 'X_FILL_64[MEDIUM_BYTES]'):
            self.assertIn(f'inline constexpr uint8_t {array} = {{', text)
        self.assertIn('// dice-six: Phosphor 2.1.1 regular, phosphor/regular/dice-six.svg', text)
        self.assertIn('// dice-six: Phosphor 2.1.1 fill, phosphor/fill/dice-six-fill.svg', text)
        self.assertIn('// x: Phosphor 2.1.1 regular, phosphor/regular/x.svg', text)
        self.assertIn('// x: Phosphor 2.1.1 fill, phosphor/fill/x-fill.svg', text)
        self.assertIn('\n' + ggi.LICENCE_NOTICE + '\n', text)
        dice = '{"dice-six", {DICE_SIX_32, DICE_SIX_FILL_32}, {DICE_SIX_64, DICE_SIX_FILL_64}},'
        x = '{"x", {X_32, X_FILL_32}, {X_64, X_FILL_64}},'
        self.assertLess(text.index(dice), text.index(x))
        # Each name's regular bitmaps come before its fill ones, and the weights differ.
        self.assertLess(text.index('X_32[SMALL_BYTES]'), text.index('X_FILL_32[SMALL_BYTES]'))
        self.assertNotEqual(self.array(text, 'X_32'), self.array(text, 'X_FILL_32'))
        self.assertNotIn('static', text)
        self.assertNotIn('original', text)

    def array(self, text, name):
        start = text.index(f'uint8_t {name}[')
        return text[start:text.index('};', start)].split('{', 1)[1]

    def test_the_committed_map_names_phosphor_icons_only_in_both_weights(self):
        entries = ggi.read_map(ggi.DEFAULT_ASSETS)
        self.assertEqual(len(entries), 55)
        for weights in entries:
            self.assertEqual(tuple(weights), ggi.WEIGHTS)
            for weight, entry in weights.items():
                self.assertEqual(entry.path, ggi.phosphor_path(entry.name, weight))
        self.assertFalse((ggi.DEFAULT_ASSETS / 'original').exists())

    def test_two_runs_are_byte_identical(self):
        second = self.tmp / 'second.h'
        self.assertEqual(self.run_main()[0], 0)
        self.assertEqual(self.run_main(out=second)[0], 0)
        self.assertEqual(self.out.read_bytes(), second.read_bytes())

    def test_broken_rules_exit_1(self):
        def both(name):
            return f'{name} regular phosphor/regular/{name}.svg\n{name} fill phosphor/fill/{name}-fill.svg\n'

        cases = {
            # (map, a fragment the message must hold)
            'bad map line': ('x regular\n' + X_BOTH, 'names.txt:1'),
            'extra field': ('x regular phosphor/regular/x.svg extra\n', 'names.txt:1'),
            'upper case name': (both('X'), 'bad name'),
            'digit first': (both('6x'), 'bad name'),
            'underscore': (both('x_y'), 'bad name'),
            'double hyphen': (both('x--y'), 'bad name'),
            'trailing hyphen': (both('x-'), 'bad name'),
            'leading hyphen': (both('-x'), 'bad name'),
            'ends in -fill': (both('x-fill'), 'bad name'),
            'long name': (both('a' * 33), 'bad name'),
            'weight bold': ('x bold phosphor/bold/x-bold.svg\n' + X_BOTH, 'unknown weight'),
            'weight heavy': ('x heavy phosphor/regular/x.svg\n', 'unknown weight'),
            'not the name\'s path': ('cross regular phosphor/regular/x.svg\n'
                                     'cross fill phosphor/fill/x-fill.svg\n', "not Phosphor's regular cross"),
            'another name\'s fill': ('x regular phosphor/regular/x.svg\nx fill phosphor/fill/heart-fill.svg\n',
                                     "not Phosphor's fill x"),
            'the other weight\'s folder': ('x regular phosphor/fill/x-fill.svg\nx fill phosphor/fill/x-fill.svg\n',
                                          "not Phosphor's regular x"),
            'fill without -fill': ('x regular phosphor/regular/x.svg\nx fill phosphor/fill/x.svg\n',
                                   "not Phosphor's fill x"),
            'original path': ('x regular original/x.svg\nx fill phosphor/fill/x-fill.svg\n', "not Phosphor's"),
            'escaping path': ('x regular ../x.svg\n', "not Phosphor's"),
            'absolute path': ('x regular /phosphor/regular/x.svg\n', "not Phosphor's"),
            'backslash path': ('x regular phosphor\\regular\\x.svg\n', "not Phosphor's"),
            'not an svg path': ('x regular phosphor/regular/x.png\n', "not Phosphor's"),
            'regular only': ('x regular phosphor/regular/x.svg\n', 'x has no fill line'),
            'fill only': ('x fill phosphor/fill/x-fill.svg\n', 'x has no regular line'),
            'fill twice': (X_BOTH + 'x fill phosphor/fill/x-fill.svg\n',
                           'names.txt:3: x fill is already named on line 2'),
            'regular twice': ('x regular phosphor/regular/x.svg\n' + X_BOTH, 'x regular is already named on line 1'),
            'no icons': ('# only a comment\n', 'names no icons'),
        }
        for label, (names, fragment) in cases.items():
            with self.subTest(label):
                code, err = self.run_main(names)
                self.assertEqual(code, 1, err)
                self.assertIn('names', err)
                self.assertIn(fragment, err)
        self.assertFalse(self.out.exists())

    def test_a_map_that_is_not_utf8_exits_1(self):
        (self.assets / 'names.txt').write_bytes(b'x regular phosphor/regular/\xff.svg\n')
        code, err = self.run_main(names=None)
        self.assertEqual(code, 1, err)
        self.assertIn('not UTF-8', err)

    def test_unsupported_svg_content_exits_1_naming_the_file(self):
        ns = 'xmlns="http://www.w3.org/2000/svg"'
        cases = {
            'rect': f'<svg {ns} viewBox="0 0 32 32"><rect width="4" height="4"/></svg>',
            'transform': f'<svg {ns} viewBox="0 0 32 32"><path d="M0,0H4V4Z" transform="scale(2)"/></svg>',
            'fill-rule': f'<svg {ns} viewBox="0 0 32 32"><path d="M0,0H4V4Z" fill-rule="evenodd"/></svg>',
            'command': svg('M0,0B4,4Z'),
            'infinite number': svg('M0,0H1e999V4Z'),
            'empty': svg('M8,8H8.1V24H8Z'),
        }
        for label, text in cases.items():
            with self.subTest(label):
                self.write_svg('phosphor/regular/bad.svg', text)
                shutil.copy(PHOSPHOR / 'fill' / 'x-fill.svg', self.assets / 'phosphor' / 'fill' / 'bad-fill.svg')
                code, err = self.run_main('bad regular phosphor/regular/bad.svg\nbad fill phosphor/fill/bad-fill.svg\n')
                self.assertEqual(code, 1, err)
                self.assertIn('bad.svg', err)
        self.assertIn('renders empty', err)

    def test_missing_inputs_exit_2(self):
        code, err = self.run_main(names=None)
        self.assertEqual(code, 2, err)
        self.assertIn('names.txt', err)
        (self.assets / 'phosphor' / 'fill' / 'x-fill.svg').unlink()
        code, err = self.run_main(X_BOTH)
        self.assertEqual(code, 2, err)
        self.assertIn('x-fill.svg', err)
        self.assertFalse(self.out.exists())

    def test_an_unwritable_out_exits_2(self):
        code, err = self.run_main(out=self.tmp / 'no-such-folder' / 'out.h')
        self.assertEqual(code, 2, err)
        self.assertIn('cannot write', err)

    def test_an_edited_svg_exits_1_naming_it(self):
        # epic-icon-library retrospective R8 (c): an SVG edited after its sum was taken fails, although it still
        # renders.
        self.assertEqual(self.run_main()[0], 0)
        self.out.unlink()
        svg_path = self.assets / 'phosphor' / 'fill' / 'x-fill.svg'
        svg_path.write_bytes(svg_path.read_bytes() + b'\n')
        code, err = self.run_main(names=None)
        self.assertEqual(code, 1, err)
        self.assertIn(f'{svg_path}: its SHA-256', err)
        self.assertIn('SHA256SUMS:2 (', err)
        self.assertFalse(self.out.exists())

    def test_sums_list_drift_exits_1(self):
        sums = sums_text(self.assets, map_paths(GOOD_MAP))
        lines = sums.splitlines(keepends=True)
        shutil.copy(PHOSPHOR / 'regular' / 'heart.svg', self.assets / 'phosphor' / 'regular' / 'heart.svg')
        digest = hashlib.sha256(b'').hexdigest()
        cases = {
            # (SHA256SUMS text, a fragment the message must hold)
            'an SVG unlisted': (''.join(lines[:-1]), 'phosphor/regular/x.svg (x regular) is not listed'),
            'a path not named': (sums + sums_text(self.assets, ['phosphor/regular/heart.svg']),
                                 'SHA256SUMS:5: phosphor/regular/heart.svg is not an SVG names.txt names'),
            'a path twice': (sums + lines[0], f'SHA256SUMS:5: {lines[0][66:].strip()} is already listed on line 1'),
            'one space': (sums.replace('  ', ' ', 1), 'SHA256SUMS:1: expected'),
            'upper-case hex': (lines[0].upper() + ''.join(lines[1:]), 'SHA256SUMS:1: expected'),
            'short digest': (lines[0][1:] + ''.join(lines[1:]), 'SHA256SUMS:1: expected'),
            'no path': (sums + f'{digest}  \n', 'SHA256SUMS:5: expected'),
            'a blank line': (sums + '\n', 'SHA256SUMS:5: expected'),
            'a comment': ('# sums\n' + sums, 'SHA256SUMS:1: expected'),
        }
        for label, (text, fragment) in cases.items():
            with self.subTest(label):
                (self.assets / ggi.SUMS_NAME).write_text(text)
                code, err = self.run_main(sums=False)
                self.assertEqual(code, 1, err)
                self.assertIn(fragment, err)
        (self.assets / ggi.SUMS_NAME).write_bytes(b'\xff\n')
        code, err = self.run_main(sums=False)
        self.assertEqual(code, 1, err)
        self.assertIn('not UTF-8', err)
        self.assertFalse(self.out.exists())

    def test_binary_mode_lines_are_read(self):
        (self.assets / 'names.txt').write_text(GOOD_MAP)
        sums = sums_text(self.assets, map_paths(GOOD_MAP)).replace('  ', ' *')
        (self.assets / ggi.SUMS_NAME).write_text(sums)
        code, err = self.run_main(names=None)
        self.assertEqual(code, 0, err)

    def test_no_sums_file_exits_2(self):
        code, err = self.run_main(sums=False)
        self.assertEqual(code, 2, err)
        self.assertIn('SHA256SUMS', err)
        self.assertIn('--write-sums', err)
        self.assertFalse(self.out.exists())

    def test_write_sums_writes_sorted_sha256sum_lines_and_no_header(self):
        (self.assets / 'names.txt').write_text(GOOD_MAP)
        code, err = self.main_only('--write-sums', '--out', str(self.out))
        self.assertEqual(code, 0, err)
        self.assertFalse(self.out.exists())
        text = (self.assets / ggi.SUMS_NAME).read_bytes().decode()
        self.assertEqual(text, sums_text(self.assets, map_paths(GOOD_MAP)))
        paths = [line.split('  ', 1)[1] for line in text.splitlines()]
        self.assertEqual(paths, ['phosphor/fill/dice-six-fill.svg', 'phosphor/fill/x-fill.svg',
                                 'phosphor/regular/dice-six.svg', 'phosphor/regular/x.svg'])
        # Round trip: the written file passes, and rewriting it after an edit passes again.
        self.assertEqual(self.main_only('--out', str(self.out))[0], 0)
        svg_path = self.assets / 'phosphor' / 'regular' / 'x.svg'
        svg_path.write_bytes(svg_path.read_bytes() + b'\n')
        self.assertEqual(self.main_only('--out', str(self.out))[0], 1)
        self.assertEqual(self.main_only('--write-sums')[0], 0)
        self.assertEqual(self.main_only('--out', str(self.out))[0], 0)

    def test_write_sums_setup_errors_exit_2(self):
        code, err = self.main_only('--write-sums')
        self.assertEqual(code, 2, err)
        self.assertIn('names.txt', err)
        (self.assets / 'names.txt').write_text(GOOD_MAP)
        (self.assets / ggi.SUMS_NAME).mkdir()
        code, err = self.main_only('--write-sums')
        self.assertEqual(code, 2, err)
        self.assertIn('cannot write', err)
        (self.assets / ggi.SUMS_NAME).rmdir()
        (self.assets / 'phosphor' / 'fill' / 'x-fill.svg').unlink()
        code, err = self.main_only('--write-sums')
        self.assertEqual(code, 2, err)
        self.assertIn('x-fill.svg', err)
        self.assertFalse((self.assets / ggi.SUMS_NAME).exists())

    def test_write_sums_rejects_a_broken_map(self):
        (self.assets / 'names.txt').write_text('x regular\n')
        code, err = self.main_only('--write-sums')
        self.assertEqual(code, 1, err)
        self.assertFalse((self.assets / ggi.SUMS_NAME).exists())


class SumsTest(unittest.TestCase):

    def test_the_committed_sums_match_the_committed_svgs(self):
        # Checked here without the script's parser, so a bug in it cannot hide a stale file.
        assets = ggi.DEFAULT_ASSETS
        named = {entry.path for weights in ggi.read_map(assets) for entry in weights.values()}
        self.assertEqual((assets / ggi.SUMS_NAME).read_text(), sums_text(assets, named))


if __name__ == '__main__':
    unittest.main()
