#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include "BlitClip.h"

// The clip GameIconBlit::inkRuns and GameImageBlit::runs share (retro A2). Their own tests draw through it; these
// pin its arithmetic, including the int32 extremes a game reaches with any int16 origin and a huge canvas.
namespace {

using BlitClip::Visible;
using BlitClip::visible;

TEST(BlitClipTest, ABitmapWhollyOnTheCanvasIsAllVisible) {
  const Visible v = visible(10, 20, 480, 800, 32, 32);
  EXPECT_EQ(v.firstX, 0);
  EXPECT_EQ(v.firstY, 0);
  EXPECT_EQ(v.endX, 32);
  EXPECT_EQ(v.endY, 32);
  EXPECT_FALSE(v.empty());
}

TEST(BlitClipTest, ABitmapPastTheTopLeftLosesItsFirstRowsAndColumns) {
  const Visible v = visible(-5, -7, 480, 800, 32, 32);
  EXPECT_EQ(v.firstX, 5);
  EXPECT_EQ(v.firstY, 7);
  EXPECT_EQ(v.endX, 32);
  EXPECT_EQ(v.endY, 32);
}

TEST(BlitClipTest, ABitmapPastTheBottomRightLosesItsLastRowsAndColumns) {
  const Visible v = visible(470, 790, 480, 800, 32, 32);
  EXPECT_EQ(v.endX, 10);
  EXPECT_EQ(v.endY, 10);
  EXPECT_EQ(v.firstX, 0);
  EXPECT_EQ(v.firstY, 0);
}

TEST(BlitClipTest, ABitmapWhollyOffTheCanvasIsEmptyOnEachSide) {
  EXPECT_TRUE(visible(-32, 0, 480, 800, 32, 32).empty());  // its right edge is at column 0
  EXPECT_TRUE(visible(0, -32, 480, 800, 32, 32).empty());
  EXPECT_TRUE(visible(480, 0, 480, 800, 32, 32).empty());
  EXPECT_TRUE(visible(0, 800, 480, 800, 32, 32).empty());
  EXPECT_FALSE(visible(-31, 0, 480, 800, 32, 32).empty());  // one column left
  EXPECT_FALSE(visible(479, 799, 480, 800, 32, 32).empty());
}

TEST(BlitClipTest, ANonPositiveCanvasOrBitmapIsEmpty) {
  EXPECT_TRUE(visible(0, 0, 0, 800, 32, 32).empty());
  EXPECT_TRUE(visible(0, 0, 480, -1, 32, 32).empty());
  EXPECT_TRUE(visible(0, 0, 480, 800, 0, 32).empty());
  EXPECT_TRUE(visible(0, 0, 480, 800, 32, 0).empty());
}

TEST(BlitClipTest, TheInt32ExtremesDoNotWrap) {
  constexpr int32_t MIN = std::numeric_limits<int32_t>::min();
  constexpr int32_t MAX = std::numeric_limits<int32_t>::max();
  const Visible far = visible(MIN, MIN, 480, 800, 128, 128);
  EXPECT_EQ(far.firstX, -static_cast<int64_t>(MIN));
  EXPECT_TRUE(far.empty());  // 2^31 columns are skipped before the bitmap starts
  EXPECT_TRUE(visible(MAX, MAX, 480, 800, 128, 128).empty());
  // A canvas at the extreme with an origin at the opposite one: width - left must not overflow int32.
  const Visible wide = visible(MIN, 0, MAX, 800, 128, 128);
  EXPECT_EQ(wide.endX, 128);
  EXPECT_TRUE(wide.empty());
}

}  // namespace
