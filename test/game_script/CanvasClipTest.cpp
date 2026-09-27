#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <vector>

#include "CanvasClip.h"

using GameScript::CanvasRect;
using GameScript::circleRows;
using GameScript::clipLine;
using GameScript::clipToCanvas;
using GameScript::outlineEdges;

namespace {

constexpr int32_t W = 480;
constexpr int32_t H = 800;

bool same(const CanvasRect& a, const CanvasRect& b) { return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h; }

TEST(CanvasClipTest, KeepsInsideRectsAndClampsOverhangs) {
  CanvasRect out;
  ASSERT_TRUE(clipToCanvas({10, 20, 30, 40}, W, H, out));
  EXPECT_TRUE(same(out, {10, 20, 30, 40}));
  ASSERT_TRUE(clipToCanvas({-5, -5, 20, 20}, W, H, out));
  EXPECT_TRUE(same(out, {0, 0, 15, 15}));
  ASSERT_TRUE(clipToCanvas({470, 790, 100, 100}, W, H, out));
  EXPECT_TRUE(same(out, {470, 790, 10, 10}));
  ASSERT_TRUE(clipToCanvas({-1000, -1000, INT16_MAX, INT16_MAX}, W, H, out));
  EXPECT_TRUE(same(out, {0, 0, W, H}));
}

TEST(CanvasClipTest, DropsEmptyAndOffCanvasRects) {
  CanvasRect out;
  EXPECT_FALSE(clipToCanvas({10, 10, 0, 5}, W, H, out));
  EXPECT_FALSE(clipToCanvas({10, 10, 5, -1}, W, H, out));
  EXPECT_FALSE(clipToCanvas({W, 0, 10, 10}, W, H, out));
  EXPECT_FALSE(clipToCanvas({0, H, 10, 10}, W, H, out));
  EXPECT_FALSE(clipToCanvas({-20, 0, 20, 10}, W, H, out));                              // ends exactly at x = 0
  EXPECT_FALSE(clipToCanvas({INT16_MIN, INT16_MIN, INT16_MAX, INT16_MAX}, W, H, out));  // ends at -1
}

TEST(CanvasClipTest, OutlineEdgesInsideTheCanvas) {
  CanvasRect edges[4];
  ASSERT_EQ(outlineEdges({10, 20, 30, 40}, W, H, edges), 4);
  EXPECT_TRUE(same(edges[0], {10, 20, 30, 1}));  // top
  EXPECT_TRUE(same(edges[1], {10, 59, 30, 1}));  // bottom
  EXPECT_TRUE(same(edges[2], {10, 20, 1, 40}));  // left
  EXPECT_TRUE(same(edges[3], {39, 20, 1, 40}));  // right
}

TEST(CanvasClipTest, OutlineEdgesOffTheCanvasAreSkipped) {
  CanvasRect edges[4];
  // The reviewed worst case: every edge is off the canvas, so nothing is drawn.
  EXPECT_EQ(outlineEdges({INT16_MIN, INT16_MIN, INT16_MAX, INT16_MAX}, W, H, edges), 0);
  // A rect around the whole canvas: its edges lie outside, so nothing is drawn either.
  EXPECT_EQ(outlineEdges({-1000, -1000, INT16_MAX, INT16_MAX}, W, H, edges), 0);
  // Only the right and bottom edges cross the canvas, each clipped to it.
  ASSERT_EQ(outlineEdges({-100, -100, 200, 300}, W, H, edges), 2);
  EXPECT_TRUE(same(edges[0], {0, 199, 100, 1}));
  EXPECT_TRUE(same(edges[1], {99, 0, 1, 200}));
  EXPECT_EQ(outlineEdges({10, 10, 0, 10}, W, H, edges), 0);
}

struct Span {
  int32_t y, x, w;
};

std::vector<Span> spansOf(const int32_t cx, const int32_t cy, const int32_t r, const bool filled, const int32_t w = W,
                          const int32_t h = H) {
  std::vector<Span> spans;
  circleRows(cx, cy, r, filled, w, h,
             [&](const int32_t y, const int32_t x, const int32_t len) { spans.push_back(Span{y, x, len}); });
  return spans;
}

bool onCanvas(const int32_t x, const int32_t y) { return x >= 0 && y >= 0 && x < W && y < H; }

TEST(CanvasClipTest, LinesOnTheCanvasAreKept) {
  int32_t x1 = 10, y1 = 20, x2 = 300, y2 = 700;
  ASSERT_TRUE(clipLine(x1, y1, x2, y2, W, H));
  EXPECT_EQ(x1, 10);
  EXPECT_EQ(y1, 20);
  EXPECT_EQ(x2, 300);
  EXPECT_EQ(y2, 700);
  int32_t px1 = 5, py1 = 5, px2 = 5, py2 = 5;  // a point
  EXPECT_TRUE(clipLine(px1, py1, px2, py2, W, H));
}

TEST(CanvasClipTest, LinesOffTheCanvasAreDropped) {
  int32_t x1 = -100, y1 = -5, x2 = 600, y2 = -1;  // above
  EXPECT_FALSE(clipLine(x1, y1, x2, y2, W, H));
  EXPECT_EQ(x1, -100);                          // untouched
  int32_t a1 = W, b1 = 0, a2 = W + 50, b2 = H;  // right of it
  EXPECT_FALSE(clipLine(a1, b1, a2, b2, W, H));
  int32_t c1 = -10, d1 = 5, c2 = 5, d2 = -10;  // passes by the corner
  EXPECT_FALSE(clipLine(c1, d1, c2, d2, W, H));
}

TEST(CanvasClipTest, CrossingLinesEndOnTheCanvasOnTheSameLine) {
  // Extreme int16_t coordinates: the diagonal through the canvas corner.
  int32_t x1 = INT16_MIN, y1 = INT16_MIN, x2 = INT16_MAX, y2 = INT16_MAX;
  ASSERT_TRUE(clipLine(x1, y1, x2, y2, W, H));
  EXPECT_TRUE(onCanvas(x1, y1));
  EXPECT_TRUE(onCanvas(x2, y2));
  EXPECT_EQ(x1, 0);
  EXPECT_EQ(y1, 0);
  EXPECT_EQ(x2, H > W ? W - 1 : H - 1);
  EXPECT_EQ(x2, y2);
  // A horizontal and a vertical line span the canvas exactly.
  int32_t h1 = -30000, hy1 = 400, h2 = 30000, hy2 = 400;
  ASSERT_TRUE(clipLine(h1, hy1, h2, hy2, W, H));
  EXPECT_EQ(h1, 0);
  EXPECT_EQ(h2, W - 1);
  EXPECT_EQ(hy1, 400);
  int32_t v1 = 7, vy1 = 30000, v2 = 7, vy2 = -30000;
  ASSERT_TRUE(clipLine(v1, vy1, v2, vy2, W, H));
  EXPECT_EQ(vy1, H - 1);
  EXPECT_EQ(vy2, 0);
  // A steep line from far away: both ends on the canvas, near the true line.
  int32_t s1 = -20000, t1 = -1000, s2 = 20000, t2 = 1000;  // y = x / 20
  ASSERT_TRUE(clipLine(s1, t1, s2, t2, W, H));
  EXPECT_TRUE(onCanvas(s1, t1));
  EXPECT_TRUE(onCanvas(s2, t2));
  EXPECT_LE(std::abs(t1 - s1 / 20), 1);
  EXPECT_LE(std::abs(t2 - s2 / 20), 1);
}

TEST(CanvasClipTest, SmallCirclesHaveTheirExpectedPixels) {
  // r = 0 is one pixel.
  auto spans = spansOf(10, 10, 0, true);
  ASSERT_EQ(spans.size(), 1u);
  EXPECT_EQ(spans[0].x, 10);
  EXPECT_EQ(spans[0].w, 1);
  EXPECT_EQ(spansOf(10, 10, 0, false).size(), 1u);
  // r = 1: a 3 x 3 block, or that block's ring (no centre).
  spans = spansOf(10, 10, 1, true);
  ASSERT_EQ(spans.size(), 3u);
  for (const Span& s : spans) EXPECT_EQ(s.w, 3);
  spans = spansOf(10, 10, 1, false);
  ASSERT_EQ(spans.size(), 4u);  // full top row, two single pixels, full bottom row
  EXPECT_EQ(spans[0].w, 3);
  EXPECT_EQ(spans[1].x, 9);
  EXPECT_EQ(spans[1].w, 1);
  EXPECT_EQ(spans[2].x, 11);
  EXPECT_EQ(spans[3].w, 3);
  // Negative radius draws nothing.
  EXPECT_TRUE(spansOf(10, 10, -1, true).empty());
}

TEST(CanvasClipTest, CirclesAreSymmetricAndRingsSitInsideTheirDisc) {
  const auto disc = spansOf(200, 300, 40, true);
  ASSERT_EQ(disc.size(), 81u);
  for (const Span& s : disc) {
    EXPECT_EQ(s.x + s.w - 1 - 200, 200 - s.x) << s.y;  // symmetric about cx
    const int32_t dy = s.y - 300;
    EXPECT_LE((s.x - 200) * (s.x - 200) + dy * dy, 40 * 40 + 40);
  }
  EXPECT_EQ(disc.front().w, disc.back().w);
  EXPECT_EQ(disc[40].w, 81);  // the middle row is the diameter
  for (const Span& s : spansOf(200, 300, 40, false)) {
    const int32_t dy = s.y - 300;
    for (int32_t x = s.x; x < s.x + s.w; ++x) {
      const int32_t d2 = (x - 200) * (x - 200) + dy * dy;
      EXPECT_LE(d2, 40 * 40 + 40);
      EXPECT_GT(d2, 39 * 39 + 39);
    }
  }
}

TEST(CanvasClipTest, CirclesWalkOnlyCanvasRowsAndClipTheirSpans) {
  // Radius far larger than the canvas, covering it: one full-width span a row.
  auto spans = spansOf(240, 400, INT16_MAX, true);
  ASSERT_EQ(spans.size(), static_cast<size_t>(H));
  for (const Span& s : spans) {
    EXPECT_EQ(s.x, 0);
    EXPECT_EQ(s.w, W);
  }
  // Its ring is entirely off the canvas, but still only canvas rows are walked.
  EXPECT_TRUE(spansOf(240, 400, INT16_MAX, false).empty());
  // Off the canvas to the left: nothing, whatever the rows.
  EXPECT_TRUE(spansOf(-20000, 400, 19000, true).empty());
  // Below the canvas: no rows at all.
  EXPECT_TRUE(spansOf(240, 30000, 1000, true).empty());
  // Straddling the right edge: spans stop at the edge.
  for (const Span& s : spansOf(W - 5, 100, 20, true)) EXPECT_LE(s.x + s.w, W);
}

}  // namespace
