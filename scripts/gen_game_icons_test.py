#!/usr/bin/env python3
"""
Tests for gen_game_icons.py. Standard library only:

    python3 scripts/gen_game_icons_test.py [-v]

The rasterizer cases draw small SVGs and compare pixels; the main() cases build an assets folder in a temp directory
and assert the exit code of every outcome, so a regression that makes a bad input pass is caught.
"""

import contextlib
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
GOOD_MAP = 'x regular phosphor/regular/x.svg\nheart fill phosphor/fill/heart-fill.svg\n'


class MainTest(unittest.TestCase):

    def setUp(self):
        self.tmp = pathlib.Path(tempfile.mkdtemp(prefix='gen-game-icons-test-'))
        self.assets = self.tmp / 'assets'
        (self.assets / 'phosphor' / 'regular').mkdir(parents=True)
        (self.assets / 'phosphor' / 'fill').mkdir(parents=True)
        shutil.copy(PHOSPHOR / 'regular' / 'x.svg', self.assets / 'phosphor' / 'regular' / 'x.svg')
        shutil.copy(PHOSPHOR / 'fill' / 'heart-fill.svg', self.assets / 'phosphor' / 'fill' / 'heart-fill.svg')
        self.out = self.tmp / 'out.h'

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def run_main(self, names=GOOD_MAP, out=None):
        if names is not None:
            (self.assets / 'names.txt').write_text(names)
        stderr = io.StringIO()
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(stderr):
            code = ggi.main(['--assets', str(self.assets), '--out', str(out or self.out)])
        return code, stderr.getvalue()

    def write_svg(self, name, text):
        (self.assets / name).write_text(text)

    def test_a_good_map_writes_the_header_sorted_by_name(self):
        code, err = self.run_main('# comment\n\n' + GOOD_MAP)
        self.assertEqual(code, 0, err)
        text = self.out.read_text()
        self.assertIn('inline constexpr uint8_t HEART_32[SMALL_BYTES] = {', text)
        self.assertIn('inline constexpr uint8_t X_64[MEDIUM_BYTES] = {', text)
        self.assertIn('// heart: Phosphor 2.1.1 fill, phosphor/fill/heart-fill.svg', text)
        self.assertIn('// x: Phosphor 2.1.1 regular, phosphor/regular/x.svg', text)
        self.assertIn('\n' + ggi.LICENCE_NOTICE + '\n', text)
        self.assertLess(text.index('{"heart", HEART_32, HEART_64}'), text.index('{"x", X_32, X_64}'))
        self.assertNotIn('static', text)

    def test_an_original_icon_is_labelled_as_one(self):
        (self.assets / 'original').mkdir()
        shutil.copy(PHOSPHOR / 'regular' / 'x.svg', self.assets / 'original' / 'cross.svg')
        code, err = self.run_main(GOOD_MAP + 'cross regular original/cross.svg\n')
        self.assertEqual(code, 0, err)
        text = self.out.read_text()
        self.assertIn("// cross: original, in Phosphor's regular style, original/cross.svg", text)
        self.assertIn('// x: Phosphor 2.1.1 regular, phosphor/regular/x.svg', text)

    def test_two_runs_are_byte_identical(self):
        second = self.tmp / 'second.h'
        self.assertEqual(self.run_main()[0], 0)
        self.assertEqual(self.run_main(out=second)[0], 0)
        self.assertEqual(self.out.read_bytes(), second.read_bytes())

    def test_broken_rules_exit_1(self):
        cases = {
            'bad map line': 'x regular\n',
            'extra field': 'x regular phosphor/regular/x.svg extra\n',
            'bad name': 'X regular phosphor/regular/x.svg\n',
            'digit first': '6x regular phosphor/regular/x.svg\n',
            'double underscore': 'a__b regular phosphor/regular/x.svg\n',
            'trailing underscore': 'die_ regular phosphor/regular/x.svg\n',
            'long name': 'a' * 33 + ' regular phosphor/regular/x.svg\n',
            'bad weight': 'x heavy phosphor/regular/x.svg\n',
            'escaping path': 'x regular ../x.svg\n',
            'absolute path': 'x regular /phosphor/regular/x.svg\n',
            'backslash path': 'x regular phosphor\\regular\\x.svg\n',
            'unknown source folder': 'x regular other/x.svg\n',
            'no source folder': 'x regular x.svg\n',
            'not an svg path': 'x regular phosphor/regular/x.png\n',
            'duplicate': 'x regular phosphor/regular/x.svg\nx fill phosphor/fill/heart-fill.svg\n',
            'no icons': '# only a comment\n',
        }
        for label, names in cases.items():
            with self.subTest(label):
                code, err = self.run_main(names)
                self.assertEqual(code, 1, err)
                self.assertIn('names', err)
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
                self.write_svg('phosphor/bad.svg', text)
                code, err = self.run_main('bad regular phosphor/bad.svg\n')
                self.assertEqual(code, 1, err)
                self.assertIn('bad.svg', err)
        self.assertIn('renders empty', err)

    def test_missing_inputs_exit_2(self):
        code, err = self.run_main(names=None)
        self.assertEqual(code, 2, err)
        self.assertIn('names.txt', err)
        code, err = self.run_main('x regular phosphor/regular/missing.svg\n')
        self.assertEqual(code, 2, err)
        self.assertIn('missing.svg', err)
        self.assertFalse(self.out.exists())

    def test_an_unwritable_out_exits_2(self):
        code, err = self.run_main(out=self.tmp / 'no-such-folder' / 'out.h')
        self.assertEqual(code, 2, err)
        self.assertIn('cannot write', err)


if __name__ == '__main__':
    unittest.main()
