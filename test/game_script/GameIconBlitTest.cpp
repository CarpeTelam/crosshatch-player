#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "GameIconBlit.h"
#include "GameIcons.h"

using GameIconBlit::inkAt;
using GameIconBlit::inkRuns;
using GameIconBlit::Source;
using GameIconBlit::sourceFor;

namespace {

using InkRun = std::array<int32_t, 3>;  // y, x, w
using Pixel = std::pair<int, int>;      // x, y

static_assert(GameIcons::find("mark_x", 6) >= 0, "find works at compile time");
static_assert(GameIcons::find("mark", 4) == -1, "a prefix is not a name");

// A blank `pixels`-square bitmap in drawIcon's layout (every bit 1: no ink).
std::vector<uint8_t> blank(const int pixels) {
  return std::vector<uint8_t>(static_cast<size_t>(pixels) * ((pixels + 7) / 8), 0xFF);
}

// Clears the stored bit at (row, col): ink there.
void inkStored(std::vector<uint8_t>& bitmap, const int pixels, const int row, const int col) {
  bitmap[row * ((pixels + 7) / 8) + (col >> 3)] &= static_cast<uint8_t>(~(0x80 >> (col & 7)));
}

// Ink at drawn (x, y), through drawIcon's mapping: stored row pixels - 1 - x, col y.
void inkDrawn(std::vector<uint8_t>& bitmap, const int pixels, const int x, const int y) {
  inkStored(bitmap, pixels, pixels - 1 - x, y);
}

// The pixels GfxRenderer::drawIcon plots for a bitmap at (0, 0): its loop, with
// drawPixel recording instead of drawing.
std::set<Pixel> drawIconPixels(const uint8_t* bitmap, const int size) {
  std::set<Pixel> plotted;
  const int rowBytes = (size + 7) / 8;
  for (int row = 0; row < size; row++) {
    for (int col = 0; col < size; col++) {
      const uint8_t byte = bitmap[row * rowBytes + (col >> 3)];
      const bool ink = ((byte >> (7 - (col & 7))) & 1) == 0;
      if (ink) plotted.insert({size - 1 - row, col});
    }
  }
  return plotted;
}

std::set<Pixel> inkAtPixels(const uint8_t* bitmap, const int size) {
  std::set<Pixel> ink;
  for (int y = 0; y < size; ++y) {
    for (int x = 0; x < size; ++x) {
      if (inkAt(bitmap, size, x, y)) ink.insert({x, y});
    }
  }
  return ink;
}

std::vector<InkRun> runsOf(const Source& source, const int32_t left, const int32_t top, const int32_t width,
                           const int32_t height) {
  std::vector<InkRun> runs;
  inkRuns(source, left, top, width, height,
          [&](const int32_t y, const int32_t x, const int32_t w) { runs.push_back({y, x, w}); });
  return runs;
}

// Every pixel the runs cover.
std::set<Pixel> covered(const std::vector<InkRun>& runs) {
  std::set<Pixel> pixels;
  for (const InkRun& run : runs) {
    for (int32_t x = run[1]; x < run[1] + run[2]; ++x) pixels.insert({x, run[0]});
  }
  return pixels;
}

TEST(GameIconBlitTest, FindHitsEveryNameAndMissesEverythingElse) {
  for (size_t i = 0; i < GameIcons::ICON_COUNT; ++i) {
    const char* name = GameIcons::ICONS[i].name;
    EXPECT_EQ(GameIcons::find(name, std::strlen(name)), static_cast<int>(i)) << name;
  }
  const std::string misses[] = {
      "no_such_icon",
      "",
      "die",
      "die_",
      "die_66",
      "mark_xx",
      "suit_heart_",
      "a",
      "zzz",
      "DIE_6",
      std::string("die_6\0", 6),
      std::string("mark\0x", 6),
      std::string("\0die_6", 6),
  };
  for (const std::string& name : misses) {
    EXPECT_EQ(GameIcons::find(name.data(), name.size()), -1) << name;
  }
  // No NUL needed: the length bounds the name.
  const char longer[] = {'m', 'a', 'r', 'k', '_', 'o', 'X'};
  EXPECT_EQ(GameIcons::find(longer, 6), GameIcons::find("mark_o", 6));
  EXPECT_EQ(GameIcons::find(longer, 7), -1);
}

TEST(GameIconBlitTest, InkAtMatchesDrawIconsMapping) {
  // One stored bit at a time, in a 32 px bitmap and at the edges of a 64 px one.
  const std::pair<int, std::vector<Pixel>> cases[] = {
      {32, {{0, 0}, {5, 9}, {31, 0}, {0, 31}, {31, 31}, {7, 8}}},
      {64, {{0, 0}, {63, 63}, {40, 3}}},
  };
  for (const auto& [pixels, cells] : cases) {
    for (const auto& [row, col] : cells) {
      std::vector<uint8_t> bitmap = blank(pixels);
      inkStored(bitmap, pixels, row, col);
      const std::set<Pixel> expected = {{pixels - 1 - row, col}};
      EXPECT_EQ(drawIconPixels(bitmap.data(), pixels), expected) << row << "," << col;
      EXPECT_EQ(inkAtPixels(bitmap.data(), pixels), expected) << row << "," << col;
    }
  }
  // And every bitmap of the library.
  for (const GameIcons::Icon& icon : GameIcons::ICONS) {
    EXPECT_EQ(inkAtPixels(icon.small, 32), drawIconPixels(icon.small, 32)) << icon.name;
    EXPECT_EQ(inkAtPixels(icon.medium, 64), drawIconPixels(icon.medium, 64)) << icon.name;
  }
}

TEST(GameIconBlitTest, SourceForPicksTheBitmapAndScale) {
  for (size_t i = 0; i < GameIcons::ICON_COUNT; ++i) {
    Source source;
    ASSERT_TRUE(sourceFor(i, 32, source));
    EXPECT_EQ(source.bitmap, GameIcons::ICONS[i].small);
    EXPECT_EQ(source.pixels, 32);
    EXPECT_EQ(source.scale, 1);
    ASSERT_TRUE(sourceFor(i, 64, source));
    EXPECT_EQ(source.bitmap, GameIcons::ICONS[i].medium);
    EXPECT_EQ(source.pixels, 64);
    EXPECT_EQ(source.scale, 1);
    ASSERT_TRUE(sourceFor(i, 128, source));
    EXPECT_EQ(source.bitmap, GameIcons::ICONS[i].medium);
    EXPECT_EQ(source.pixels, 64);
    EXPECT_EQ(source.scale, 2);
  }
  Source untouched;
  EXPECT_FALSE(sourceFor(GameIcons::ICON_COUNT, 32, untouched));
  EXPECT_FALSE(sourceFor(0, 48, untouched));
  EXPECT_FALSE(sourceFor(0, 0, untouched));
  EXPECT_FALSE(sourceFor(0, 256, untouched));
  EXPECT_EQ(untouched.bitmap, nullptr);
  EXPECT_EQ((std::vector<int>(std::begin(GameIconBlit::DRAWN_PIXELS), std::end(GameIconBlit::DRAWN_PIXELS))),
            (std::vector<int>{32, 64, 128}));
}

TEST(GameIconBlitTest, RunsMergeAdjacentInkAndSplitAtGaps) {
  std::vector<uint8_t> bitmap = blank(32);
  for (int x = 3; x <= 7; ++x) inkDrawn(bitmap, 32, x, 5);
  inkDrawn(bitmap, 32, 9, 5);
  inkDrawn(bitmap, 32, 31, 5);  // touching the right edge
  inkDrawn(bitmap, 32, 0, 6);
  const Source source{bitmap.data(), 32, 1};
  const std::vector<InkRun> expected = {
      {5 + 10, 3 + 20, 5}, {5 + 10, 9 + 20, 1}, {5 + 10, 31 + 20, 1}, {6 + 10, 20, 1}};
  EXPECT_EQ(runsOf(source, 20, 10, 480, 800), expected);
}

TEST(GameIconBlitTest, ScaleTwoDrawsEachPixelAsATwoByTwoBlock) {
  std::vector<uint8_t> bitmap = blank(64);
  inkDrawn(bitmap, 64, 0, 0);
  inkDrawn(bitmap, 64, 10, 20);
  inkDrawn(bitmap, 64, 11, 20);
  inkDrawn(bitmap, 64, 63, 63);
  const Source source{bitmap.data(), 64, 2};
  const std::vector<InkRun> expected = {
      {100, 50, 2}, {101, 50, 2}, {140, 70, 4}, {141, 70, 4}, {226, 176, 2}, {227, 176, 2},
  };
  EXPECT_EQ(runsOf(source, 50, 100, 480, 800), expected);
}

TEST(GameIconBlitTest, RunsAreClippedToTheCanvas) {
  std::vector<uint8_t> full(128, 0x00);  // a 32 px bitmap that is all ink
  const Source source{full.data(), 32, 1};
  const auto square = [](const int left, const int top, const int right, const int bottom) {
    std::set<Pixel> pixels;
    for (int y = top; y < bottom; ++y) {
      for (int x = left; x < right; ++x) pixels.insert({x, y});
    }
    return pixels;
  };
  // Inside: every pixel, one run per row.
  EXPECT_EQ(runsOf(source, 10, 10, 100, 60).size(), 32u);
  EXPECT_EQ(covered(runsOf(source, 10, 10, 100, 60)), square(10, 10, 42, 42));
  // Over each edge.
  EXPECT_EQ(covered(runsOf(source, -10, 5, 100, 60)), square(0, 5, 22, 37));
  EXPECT_EQ(covered(runsOf(source, 90, 5, 100, 60)), square(90, 5, 100, 37));
  EXPECT_EQ(covered(runsOf(source, 5, -20, 100, 60)), square(5, 0, 37, 12));
  EXPECT_EQ(covered(runsOf(source, 5, 50, 100, 60)), square(5, 50, 37, 60));
  EXPECT_EQ(covered(runsOf(source, -5, -5, 20, 20)), square(0, 0, 20, 20));
  // Wholly off each edge, far off, and on an empty canvas: nothing.
  for (const auto& [left, top] : std::vector<Pixel>{{-32, 0}, {100, 0}, {0, -32}, {0, 60}, {-30000, 30000}}) {
    EXPECT_TRUE(runsOf(source, left, top, 100, 60).empty()) << left << "," << top;
  }
  EXPECT_TRUE(runsOf(source, 0, 0, 0, 0).empty());
  EXPECT_TRUE(runsOf(source, 0, 0, -5, 60).empty());
  // Scaled: a 128 px icon over the canvas's bottom-right corner.
  const Source large{full.data(), 32, 2};  // stand-in: 64 drawn px from the 32 px bitmap
  EXPECT_EQ(covered(runsOf(large, 70, 40, 100, 60)), square(70, 40, 100, 60));
}

TEST(GameIconBlitTest, TheMarkOIsARingWithAnEmptyCentre) {
  const int index = GameIcons::find("mark_o", 6);
  ASSERT_GE(index, 0);
  Source source;
  ASSERT_TRUE(sourceFor(static_cast<size_t>(index), 64, source));
  EXPECT_FALSE(inkAt(source.bitmap, 64, 32, 32));
  EXPECT_FALSE(inkAt(source.bitmap, 64, 0, 0));
  // The middle row crosses the ring twice: two runs, far apart.
  std::vector<InkRun> middle;
  for (const InkRun& run : runsOf(source, 0, 0, 64, 64)) {
    if (run[0] == 32) middle.push_back(run);
  }
  ASSERT_EQ(middle.size(), 2u);
  EXPECT_LT(middle[0][1] + middle[0][2], 16);
  EXPECT_GT(middle[1][1], 48);
  // The ring is inked above and below the centre too.
  EXPECT_TRUE(inkAt(source.bitmap, 64, 32, 8));
  EXPECT_TRUE(inkAt(source.bitmap, 64, 32, 56));
}

// The generated icon data fits its 48 KiB flash budget (docs/crosshatch/game-icons.md, Size), counted in the
// device's layout: both bitmaps per icon, one ICONS entry of three 4-byte pointers on the ESP32 (12 B, whatever
// sizeof(Icon) is on the host), and each NUL-terminated name.
TEST(GameIconBlitTest, TheIconDataFitsIn48KiB) {
  constexpr size_t DEVICE_ICON_ENTRY_BYTES = 3 * 4;
  size_t bytes = GameIcons::ICON_COUNT * (GameIcons::SMALL_BYTES + GameIcons::MEDIUM_BYTES) +
                 GameIcons::ICON_COUNT * DEVICE_ICON_ENTRY_BYTES;
  for (const GameIcons::Icon& icon : GameIcons::ICONS) bytes += std::strlen(icon.name) + 1;
  EXPECT_LE(bytes, 49152u);
}

}  // namespace
