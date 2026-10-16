// The display lab's drive masks (lib/GameDisplayLab/MaskPlanes): the framebuffer byte order (MSB first, row-major,
// (width + 7) / 8 bytes per row), rectangles with partial bytes and clipping, pixels, and the invalid view.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "MaskPlanes.h"

namespace {

using GameDisplayLab::MaskPlanes;
using Mask = MaskPlanes::Mask;

// Two buffers poisoned with `fill` and a view over them.
struct Planes {
  std::vector<uint8_t> black;
  std::vector<uint8_t> white;
  MaskPlanes view;

  Planes(const uint16_t width, const uint16_t height, const uint8_t fill = 0x00)
      : black(MaskPlanes::bytesFor(width, height), fill), white(MaskPlanes::bytesFor(width, height), fill) {
    view = MaskPlanes(black.data(), white.data(), width, height);
  }
};

// Every pixel of `mask` is set exactly when it lies in [x0, x1) x [y0, y1).
void expectExactly(const MaskPlanes& view, const Mask mask, const int x0, const int y0, const int x1, const int y1) {
  for (int y = 0; y < view.height(); ++y) {
    for (int x = 0; x < view.width(); ++x) {
      const bool inside = x >= x0 && x < x1 && y >= y0 && y < y1;
      ASSERT_EQ(view.test(mask, x, y), inside) << "pixel " << x << "," << y;
    }
  }
}

bool allZero(const std::vector<uint8_t>& bytes) {
  for (const uint8_t b : bytes) {
    if (b != 0) return false;
  }
  return true;
}

TEST(MaskPlanes, DefaultViewIsInvalidAndIgnoresWrites) {
  MaskPlanes view;
  EXPECT_FALSE(view.valid());
  EXPECT_EQ(view.black(), nullptr);
  EXPECT_EQ(view.white(), nullptr);
  EXPECT_EQ(view.bytes(), 0u);
  view.clear();
  view.fill(Mask::Black);
  view.setRect(Mask::White, 0, 0, 10, 10);
  view.setPixel(Mask::Black, 0, 0);
  EXPECT_FALSE(view.test(Mask::Black, 0, 0));
}

TEST(MaskPlanes, FramebufferGeometry) {
  EXPECT_EQ(MaskPlanes::rowBytesFor(800), 100u);
  EXPECT_EQ(MaskPlanes::bytesFor(800, 480), 48000u);
  EXPECT_EQ(MaskPlanes::rowBytesFor(801), 101u);
  EXPECT_EQ(MaskPlanes::rowBytesFor(7), 1u);
  Planes planes(800, 480);
  EXPECT_TRUE(planes.view.valid());
  EXPECT_EQ(planes.view.width(), 800);
  EXPECT_EQ(planes.view.height(), 480);
  EXPECT_EQ(planes.view.rowBytes(), 100u);
  EXPECT_EQ(planes.view.bytes(), 48000u);
  EXPECT_EQ(planes.view.black(), planes.black.data());
  EXPECT_EQ(planes.view.white(), planes.white.data());
}

TEST(MaskPlanes, ConstructingTouchesNothing) {
  Planes planes(16, 2, 0x5A);
  for (const uint8_t b : planes.black) EXPECT_EQ(b, 0x5A);
  for (const uint8_t b : planes.white) EXPECT_EQ(b, 0x5A);
}

TEST(MaskPlanes, ClearZeroesBothMasks) {
  Planes planes(800, 480, 0xFF);
  planes.view.clear();
  EXPECT_TRUE(allZero(planes.black));
  EXPECT_TRUE(allZero(planes.white));
}

TEST(MaskPlanes, FillSetsOnlyThatMask) {
  Planes planes(800, 480);
  planes.view.fill(Mask::White);
  for (const uint8_t b : planes.white) ASSERT_EQ(b, 0xFF);
  EXPECT_TRUE(allZero(planes.black));
  planes.view.clear();
  planes.view.fill(Mask::Black);
  for (const uint8_t b : planes.black) ASSERT_EQ(b, 0xFF);
  EXPECT_TRUE(allZero(planes.white));
}

TEST(MaskPlanes, FillLeavesThePadBitsClear) {
  Planes planes(12, 2);  // 2 bytes per row, the last 4 bits of each row past the width
  planes.view.fill(Mask::Black);
  EXPECT_EQ(planes.black, (std::vector<uint8_t>{0xFF, 0xF0, 0xFF, 0xF0}));
}

TEST(MaskPlanes, PixelBitOrderIsMsbFirstRowMajor) {
  Planes planes(800, 480);
  planes.view.setPixel(Mask::Black, 0, 0);
  EXPECT_EQ(planes.black[0], 0x80);
  planes.view.setPixel(Mask::Black, 7, 0);
  EXPECT_EQ(planes.black[0], 0x81);
  planes.view.setPixel(Mask::Black, 9, 0);
  EXPECT_EQ(planes.black[1], 0x40);
  planes.view.setPixel(Mask::Black, 799, 0);
  EXPECT_EQ(planes.black[99], 0x01);
  planes.view.setPixel(Mask::Black, 0, 1);
  EXPECT_EQ(planes.black[100], 0x80);
  planes.view.setPixel(Mask::Black, 799, 479);
  EXPECT_EQ(planes.black[47999], 0x01);
  EXPECT_TRUE(allZero(planes.white));
  EXPECT_TRUE(planes.view.test(Mask::Black, 9, 0));
  EXPECT_FALSE(planes.view.test(Mask::Black, 8, 0));
  EXPECT_FALSE(planes.view.test(Mask::White, 9, 0));
}

TEST(MaskPlanes, PixelOutsideIsIgnored) {
  Planes planes(16, 4);
  planes.view.setPixel(Mask::White, -1, 0);
  planes.view.setPixel(Mask::White, 0, -1);
  planes.view.setPixel(Mask::White, 16, 0);
  planes.view.setPixel(Mask::White, 0, 4);
  EXPECT_TRUE(allZero(planes.white));
  EXPECT_TRUE(allZero(planes.black));
  EXPECT_FALSE(planes.view.test(Mask::White, -1, 0));
  EXPECT_FALSE(planes.view.test(Mask::White, 16, 0));
  EXPECT_FALSE(planes.view.test(Mask::White, 0, 4));
}

TEST(MaskPlanes, RectInsideOneByte) {
  Planes planes(16, 3);
  planes.view.setRect(Mask::White, 2, 1, 3, 1);  // pixels 2-4 of row 1
  EXPECT_EQ(planes.white, (std::vector<uint8_t>{0x00, 0x00, 0x38, 0x00, 0x00, 0x00}));
  EXPECT_TRUE(allZero(planes.black));
}

TEST(MaskPlanes, RectAcrossPartialAndWholeBytes) {
  Planes planes(32, 2);
  planes.view.setRect(Mask::Black, 5, 0, 22, 2);  // pixels 5-26: 3 bits, one whole byte, 3 bits
  const std::vector<uint8_t> row = {0x07, 0xFF, 0xFF, 0xE0};
  std::vector<uint8_t> expected = row;
  expected.insert(expected.end(), row.begin(), row.end());
  EXPECT_EQ(planes.black, expected);
  EXPECT_TRUE(allZero(planes.white));
  expectExactly(planes.view, Mask::Black, 5, 0, 27, 2);
}

TEST(MaskPlanes, RectSetsExactlyItsPixels) {
  Planes planes(800, 480);
  planes.view.setRect(Mask::White, 13, 7, 101, 33);
  expectExactly(planes.view, Mask::White, 13, 7, 114, 40);
  EXPECT_TRUE(allZero(planes.black));
}

TEST(MaskPlanes, RectAddsToWhatIsSet) {
  Planes planes(16, 1);
  planes.view.setRect(Mask::Black, 0, 0, 2, 1);
  planes.view.setRect(Mask::Black, 6, 0, 4, 1);
  EXPECT_EQ(planes.black, (std::vector<uint8_t>{0xC3, 0xC0}));
}

TEST(MaskPlanes, RectIsClippedToTheMask) {
  Planes planes(20, 6);
  planes.view.setRect(Mask::Black, -5, -2, 9, 4);  // clipped to [0, 4) x [0, 2)
  expectExactly(planes.view, Mask::Black, 0, 0, 4, 2);
  planes.view.clear();
  planes.view.setRect(Mask::Black, 15, 4, 100, 100);  // clipped to [15, 20) x [4, 6)
  expectExactly(planes.view, Mask::Black, 15, 4, 20, 6);
  // The row's pad bits past the width stay clear.
  EXPECT_EQ(planes.black[4 * 3 + 2], 0xF0);
  planes.view.clear();
  planes.view.setRect(Mask::Black, -2147483647, -2147483647, 2147483647, 2147483647);  // no overflow: up to x, y -1
  EXPECT_TRUE(allZero(planes.black));
  planes.view.setRect(Mask::Black, 2147483600, 0, 2147483647, 6);
  EXPECT_TRUE(allZero(planes.black));
  EXPECT_TRUE(allZero(planes.white));
}

TEST(MaskPlanes, EmptyOrOutsideRectIsANoOp) {
  Planes planes(20, 6);
  planes.view.setRect(Mask::White, 2, 2, 0, 3);
  planes.view.setRect(Mask::White, 2, 2, 3, 0);
  planes.view.setRect(Mask::White, 2, 2, -3, 3);
  planes.view.setRect(Mask::White, 2, 2, 3, -1);
  planes.view.setRect(Mask::White, 20, 0, 5, 5);
  planes.view.setRect(Mask::White, 0, 6, 5, 5);
  planes.view.setRect(Mask::White, -5, 0, 5, 5);
  planes.view.setRect(Mask::White, 0, -5, 5, 5);
  EXPECT_TRUE(allZero(planes.white));
  EXPECT_TRUE(allZero(planes.black));
}

}  // namespace
