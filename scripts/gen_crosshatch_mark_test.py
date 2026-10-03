#!/usr/bin/env python3
"""Tests for scripts/gen_crosshatch_mark.py: the mark's pixels, the fuse rule, and the committed headers.

    python3 scripts/gen_crosshatch_mark_test.py [-v]      # standard library only
"""

import copy
import unittest

import gen_crosshatch_mark as g


class MarkTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rows = {size: g.bitmap(size) for size in g.SIZES}
        cls.layers = {size: g.layers(size) for size in g.SIZES}

    def test_the_ring_never_touches_the_grid_at_any_size(self):
        for size in g.SIZES:
            grid, rings, crosses, _ = self.layers[size]
            for marks in (rings, crosses):
                for y in range(size):
                    for x in range(size):
                        if marks[y][x]:
                            self.assertFalse(grid[y][x], (size, x, y))
                            for nx, ny in g._neighbours4(x, y, size):
                                self.assertFalse(grid[ny][nx], (size, x, y))

    def test_a_plain_threshold_would_fuse_at_64_and_120_but_not_at_128(self):
        # The reason for the rule: before clearing, the ring's pixels are next to the grid's at 64 and 120 px.
        cleared = {size: self.layers[size][3] for size in g.SIZES}
        self.assertGreater(cleared[64], 0)
        self.assertGreater(cleared[120], 0)
        self.assertEqual(cleared[128], 0)

    def test_the_check_rejects_a_fused_ring(self):
        grid, rings, crosses, _ = copy.deepcopy(self.layers[64])
        # Put a ring pixel right next to the grid's left edge of the top-left vertical line.
        y = 12
        x = next(i for i in range(64) if grid[y][i]) - 1
        rings[y][x] = True
        with self.assertRaises(g.RuleError):
            g.check(64, grid, rings, crosses)

    def test_the_check_rejects_an_open_ring(self):
        grid, rings, crosses, _ = copy.deepcopy(self.layers[64])
        # Cut the top of the top-left ring all the way through: its hole now reaches the border of the bitmap.
        for y in range(0, 14):
            rings[y][12] = False
            rings[y][11] = False
        with self.assertRaises(g.RuleError):
            g.check(64, grid, rings, crosses)

    def test_the_check_rejects_a_filled_ring_hole(self):
        grid, rings, crosses, _ = copy.deepcopy(self.layers[64])
        rings[g._scaled(48, 64)][g._scaled(48, 64)] = True
        with self.assertRaisesRegex(g.RuleError, 'no hole'):
            g.check(64, grid, rings, crosses)

    def test_the_check_rejects_a_missing_mark(self):
        grid, rings, crosses, _ = copy.deepcopy(self.layers[128])
        for row in crosses:
            for x in range(len(row)):
                row[x] = False
        with self.assertRaises(g.RuleError):
            g.check(128, grid, rings, crosses)

    def test_each_ring_has_a_hole_and_each_cross_is_solid_at_its_middle(self):
        for size in g.SIZES:
            for cx, cy in g.RINGS:
                px, py = g._scaled(cx, size), g._scaled(cy, size)
                self.assertFalse(self.rows[size][py][px], (size, cx, cy))
            for cx, cy in g.CROSSES:
                px, py = g._scaled(cx, size), g._scaled(cy, size)
                self.assertTrue(self.rows[size][py][px], (size, cx, cy))

    def test_the_empty_cells_are_empty(self):
        for size in g.SIZES:
            for cx, cy in ((48, 128), (128, 48), (208, 128), (128, 208), (48, 208)):
                px, py = g._scaled(cx, size), g._scaled(cy, size)
                self.assertFalse(self.rows[size][py][px], (size, cx, cy))

    def test_the_128_bitmap_is_native_not_the_64_doubled(self):
        doubled = [[self.rows[64][y // 2][x // 2] for x in range(128)] for y in range(128)]
        self.assertNotEqual(doubled, self.rows[128])

    def test_the_board_is_the_specified_game(self):
        # O top-left, O top-right, X centre, X bottom-right: ink in the four cells and a hole in each O.
        self.assertEqual(g.MARKS, (('ring', (48, 48)), ('ring', (208, 48)), ('cross', (128, 128)), ('cross', (208, 208))))


class LayoutTest(unittest.TestCase):
    def test_mask1_is_top_down_msb_first_with_zero_as_ink(self):
        rows = [[False] * 8 for _ in range(8)]
        rows[0][0] = True
        rows[1][7] = True
        data = g.mask1(rows)
        self.assertEqual(len(data), 8)
        self.assertEqual(data[0], 0x7F)
        self.assertEqual(data[1], 0xFE)
        self.assertEqual(data[2], 0xFF)

    def test_the_sizes_of_the_three_bitmaps(self):
        logo, row64, hero128 = g.generate()
        self.assertEqual(len(logo), 15 * 120)
        self.assertEqual(len(row64), 8 * 64)
        self.assertEqual(len(hero128), 16 * 128)

    def test_the_120_bitmap_is_the_drawn_image_rotated_counterclockwise(self):
        rows = g.bitmap(120)
        logo = g.panel_logo(rows)
        for x, y in ((10, 20), (60, 60), (119, 0), (0, 119), (88 * 120 // 256, 40)):
            stored_row, stored_col = 120 - 1 - x, y  # GfxRenderer::drawIcon's mapping
            bit = (logo[stored_row * 15 + stored_col // 8] >> (7 - stored_col % 8)) & 1
            self.assertEqual(bit == 0, rows[y][x], (x, y))

    def test_the_corners_are_paper(self):
        for size in g.SIZES:
            rows = g.bitmap(size)
            for x, y in ((0, 0), (size - 1, 0), (0, size - 1), (size - 1, size - 1)):
                self.assertFalse(rows[y][x])


class HeadersTest(unittest.TestCase):
    def test_the_committed_headers_are_what_the_script_makes(self):
        self.assertEqual(g.main(['--check']), 0)

    def test_parse_array_ignores_formatting(self):
        self.assertEqual(g.parse_array('x A[] = {\n 0x01,0x02,\n 0xff }; y', 'A'), bytes([1, 2, 255]))
        self.assertIsNone(g.parse_array('nothing', 'A'))

    def test_a_changed_array_is_detected(self):
        logo, _, _ = g.generate()
        changed = bytearray(logo)
        changed[0] ^= 0x01
        text = g.header_120(bytes(changed))
        self.assertNotEqual(g.parse_array(text, 'CrosshatchMark120'), logo)


if __name__ == '__main__':
    unittest.main()
