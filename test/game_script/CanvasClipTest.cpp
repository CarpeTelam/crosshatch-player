#include <gtest/gtest.h>

#include <cstdint>

#include "CanvasClip.h"

using GameScript::CanvasRect;
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
  EXPECT_FALSE(clipToCanvas({-20, 0, 20, 10}, W, H, out));  // ends exactly at x = 0
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

}  // namespace
