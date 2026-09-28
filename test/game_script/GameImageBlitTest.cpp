#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <utility>
#include <vector>

#include "ConverterBmpLayout.h"
#include "GameImageBlit.h"
#include "GameImages.h"

using GameImageBlit::blackAt;

namespace {

using ColourRun = std::array<int32_t, 4>;   // y, x, w, black
using Pixel = std::pair<int32_t, int32_t>;  // x, y

// An image in the converter's layout, with its span.
struct TestImage {
  GameCore::ImageSpan span{};
  std::vector<uint8_t> rows;
};

// A width x height image whose pixel (x, y) is white when white(x, y); padding
// bits are 0 as the converter leaves them, or all 1 with `onesInPadding`.
TestImage makeImage(const int width, const int height, const std::function<bool(int, int)>& white,
                    const bool onesInPadding = false) {
  TestImage image;
  image.rows = ConverterBmpLayout::converterRows1bit(width, height, white);
  image.span.width = static_cast<uint32_t>(width);
  image.span.height = static_cast<uint32_t>(height);
  image.span.rowBytes = static_cast<uint32_t>((width + 31) / 32 * 4);
  if (onesInPadding) {
    for (int y = 0; y < height; ++y) {
      for (uint32_t x = static_cast<uint32_t>(width); x < image.span.rowBytes * 8; ++x) {
        image.rows[y * image.span.rowBytes + x / 8] |= static_cast<uint8_t>(0x80 >> (x % 8));
      }
    }
  }
  return image;
}

std::vector<ColourRun> runsOf(const TestImage& image, const int32_t left, const int32_t top, const int32_t width,
                              const int32_t height, const bool drawBlack = true) {
  std::vector<ColourRun> runs;
  GameImageBlit::runs(image.span, image.rows.data(), left, top, width, height, drawBlack,
                      [&](const int32_t y, const int32_t x, const int32_t w, const bool black) {
                        runs.push_back({y, x, w, black ? 1 : 0});
                      });
  return runs;
}

// Each pixel the runs cover and its colour; a pixel covered twice fails.
std::map<Pixel, bool> covered(const std::vector<ColourRun>& runs) {
  std::map<Pixel, bool> pixels;
  for (const ColourRun& run : runs) {
    EXPECT_GT(run[2], 0);
    for (int32_t x = run[1]; x < run[1] + run[2]; ++x) {
      EXPECT_TRUE(pixels.emplace(Pixel{x, run[0]}, run[3] != 0).second) << x << "," << run[0] << " twice";
    }
  }
  return pixels;
}

// What an opaque image at (left, top) should put on a width x height canvas.
std::map<Pixel, bool> expected(const std::function<bool(int, int)>& white, const int imageW, const int imageH,
                               const int32_t left, const int32_t top, const int32_t width, const int32_t height) {
  std::map<Pixel, bool> pixels;
  for (int y = 0; y < imageH; ++y) {
    for (int x = 0; x < imageW; ++x) {
      const int64_t cx = static_cast<int64_t>(left) + x;
      const int64_t cy = static_cast<int64_t>(top) + y;
      if (cx < 0 || cy < 0 || cx >= width || cy >= height) continue;
      pixels[{static_cast<int32_t>(cx), static_cast<int32_t>(cy)}] = !white(x, y);
    }
  }
  return pixels;
}

// A pattern with runs of both colours in every row: a checker of 3 x 2 cells.
bool checker(const int x, const int y) { return ((x / 3) + (y / 2)) % 2 == 0; }

TEST(GameImageBlitTest, BlackAtReadsTheConvertersBitOrder) {
  // Row 0: only pixel 0 black; row 1: only pixel 9 black; 12 px wide, 4-byte rows.
  const TestImage image = makeImage(12, 2, [](const int x, const int y) { return !(y == 0 ? x == 0 : x == 9); });
  EXPECT_EQ(image.span.rowBytes, 4u);
  EXPECT_EQ(image.rows[0], 0x7F);      // MSB first: pixel 0 is bit 7; bit 1 is white
  EXPECT_EQ(image.rows[4 + 1], 0xB0);  // pixels 8-11 white but 9; 12-15 are padding, left 0
  for (uint32_t x = 0; x < 12; ++x) {
    EXPECT_EQ(blackAt(image.rows.data(), 4, x, 0), x == 0) << x;
    EXPECT_EQ(blackAt(image.rows.data(), 4, x, 1), x == 9) << x;
  }
}

TEST(GameImageBlitTest, EveryVisiblePixelIsDrawnOnceInItsColour) {
  for (const int w : {1, 7, 8, 32, 33, 100}) {
    const TestImage image = makeImage(w, 5, checker);
    const auto runs = runsOf(image, 10, 20, 480, 800);
    EXPECT_EQ(covered(runs), expected(checker, w, 5, 10, 20, 480, 800)) << w;
    // Adjacent pixels of one colour are one run: never two touching runs of a colour.
    for (size_t i = 1; i < runs.size(); ++i) {
      if (runs[i][0] == runs[i - 1][0]) {
        EXPECT_NE(runs[i][3], runs[i - 1][3]) << w;
      }
    }
  }
  // One colour throughout: one run per row, the image's width.
  const TestImage white = makeImage(33, 3, [](int, int) { return true; });
  const std::vector<ColourRun> rows = {{0, 0, 33, 0}, {1, 0, 33, 0}, {2, 0, 33, 0}};
  EXPECT_EQ(runsOf(white, 0, 0, 480, 800), rows);
  const TestImage black = makeImage(5, 1, [](int, int) { return false; });
  EXPECT_EQ(runsOf(black, 3, 4, 480, 800), (std::vector<ColourRun>{{4, 3, 5, 1}}));
}

TEST(GameImageBlitTest, BlackDrawsAsConvertedAndWhiteSwapsTheInk) {
  const TestImage image = makeImage(37, 5, checker);
  const std::map<Pixel, bool> asConverted = expected(checker, 37, 5, 4, 6, 480, 800);
  EXPECT_EQ(covered(runsOf(image, 4, 6, 480, 800, true)), asConverted);
  std::map<Pixel, bool> swapped = asConverted;
  for (auto& [pixel, black] : swapped) black = !black;
  EXPECT_EQ(covered(runsOf(image, 4, 6, 480, 800, false)), swapped);
  // The runs are the same; only their ink differs.
  const auto black = runsOf(image, 4, 6, 480, 800, true);
  const auto white = runsOf(image, 4, 6, 480, 800, false);
  ASSERT_EQ(black.size(), white.size());
  for (size_t i = 0; i < black.size(); ++i) {
    EXPECT_EQ((ColourRun{black[i][0], black[i][1], black[i][2], 1 - black[i][3]}), white[i]) << i;
  }
  // A white image drawn "white" fills black; a black one fills white.
  const TestImage allWhite = makeImage(5, 1, [](int, int) { return true; });
  EXPECT_EQ(runsOf(allWhite, 0, 0, 480, 800, false), (std::vector<ColourRun>{{0, 0, 5, 1}}));
  const TestImage allBlack = makeImage(5, 1, [](int, int) { return false; });
  EXPECT_EQ(runsOf(allBlack, 0, 0, 480, 800, false), (std::vector<ColourRun>{{0, 0, 5, 0}}));
}

TEST(GameImageBlitTest, PaddingBitsAreNeverDrawn) {
  // 33 px: 31 padding bits a row, 0 (black) as the converter leaves them, or 1.
  for (const bool ones : {false, true}) {
    const TestImage image = makeImage(33, 4, [](int, int) { return true; }, ones);
    const auto pixels = covered(runsOf(image, 0, 0, 480, 800));
    EXPECT_EQ(pixels.size(), 33u * 4u) << ones;
    for (const auto& [pixel, isBlack] : pixels) {
      EXPECT_LT(pixel.first, 33) << ones;
      EXPECT_FALSE(isBlack) << ones;
    }
  }
}

TEST(GameImageBlitTest, RunsAreClippedAtEachEdge) {
  const int w = 37;
  const int h = 11;
  const TestImage image = makeImage(w, h, checker);
  struct Case {
    int32_t left;
    int32_t top;
  };
  // Over the left, right, top, and bottom edges, two corners, and wider than the canvas.
  const Case cases[] = {{-10, 5}, {80, 5}, {5, -4}, {5, 55}, {-30, -8}, {90, 58}, {-5, 2}};
  for (const Case& c : cases) {
    EXPECT_EQ(covered(runsOf(image, c.left, c.top, 100, 60)), expected(checker, w, h, c.left, c.top, 100, 60))
        << c.left << "," << c.top;
  }
  // A canvas narrower than the image: every run stays inside it.
  const auto narrow = runsOf(image, -3, -2, 20, 5);
  EXPECT_EQ(covered(narrow), expected(checker, w, h, -3, -2, 20, 5));
  for (const ColourRun& run : narrow) {
    EXPECT_GE(run[1], 0);
    EXPECT_LE(run[1] + run[2], 20);
    EXPECT_GE(run[0], 0);
    EXPECT_LT(run[0], 5);
  }
}

TEST(GameImageBlitTest, AnImageWhollyOffTheCanvasDrawsNothing) {
  const TestImage image = makeImage(37, 11, checker);
  const std::vector<Pixel> offsets = {{-37, 0}, {100, 0}, {0, -11}, {0, 60}, {-32768, 32767}, {32767, -32768}};
  for (const auto& [left, top] : offsets) {
    EXPECT_TRUE(runsOf(image, left, top, 100, 60).empty()) << left << "," << top;
  }
  EXPECT_TRUE(runsOf(image, 0, 0, 0, 0).empty());
  EXPECT_TRUE(runsOf(image, 0, 0, -5, 60).empty());
  // Just on: one column, one row.
  EXPECT_EQ(covered(runsOf(image, -36, -10, 100, 60)), expected(checker, 37, 11, -36, -10, 100, 60));
  EXPECT_EQ(runsOf(image, -36, -10, 100, 60).size(), 1u);
  // An empty span or no rows: nothing.
  GameCore::ImageSpan empty{};
  bool called = false;
  GameImageBlit::runs(empty, image.rows.data(), 0, 0, 100, 60, true,
                      [&](int32_t, int32_t, int32_t, bool) { called = true; });
  GameImageBlit::runs(image.span, nullptr, 0, 0, 100, 60, true,
                      [&](int32_t, int32_t, int32_t, bool) { called = true; });
  EXPECT_FALSE(called);
}

}  // namespace
