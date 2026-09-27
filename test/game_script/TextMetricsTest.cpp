#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "TextMetrics.h"

using namespace GameScript;

namespace {

TEST(TextMetricsTest, TheStandInAdvancesAFixedWidthPerSize) {
  constexpr TextMetrics metrics = TextMetrics::standIn();
  EXPECT_EQ(metrics.width("", TextSize::Small), 0);
  EXPECT_EQ(metrics.width("abc", TextSize::Small), 3 * STAND_IN_ADVANCE_PX[0]);
  EXPECT_EQ(metrics.width("abc", TextSize::Medium), 3 * STAND_IN_ADVANCE_PX[1]);
  EXPECT_EQ(metrics.width("abc", TextSize::Large), 3 * STAND_IN_ADVANCE_PX[2]);
  // One advance per code point, not per byte: "é" and "€" are one each.
  EXPECT_EQ(metrics.width("\xC3\xA9\xE2\x82\xAC", TextSize::Small), 2 * STAND_IN_ADVANCE_PX[0]);
}

// Glyph records wider than the advance, as EpdGlyph is, to exercise the stride.
struct Glyph {
  uint8_t width;
  uint8_t height;
  uint16_t advanceX;
  int16_t left;
  int16_t top;
  uint32_t dataOffset;
};

class TableTest : public ::testing::Test {
 protected:
  TableTest() {
    // 'A'..'C' are glyphs 0..2, U+00E9 is glyph 3; advances in 1/16 px.
    const uint16_t fp4[] = {10 * 16, 7 * 16 + 8, 7 * 16 + 7, 12 * 16};
    for (size_t i = 0; i < 4; ++i) glyphs[i].advanceX = fp4[i];
    TextMetrics m = TextMetrics::standIn();
    AdvanceTable& table = m.tables[static_cast<size_t>(TextSize::Medium)];
    table.ranges = reinterpret_cast<const uint8_t*>(ranges);
    table.rangeCount = 2;
    table.advances = reinterpret_cast<const uint8_t*>(glyphs) + offsetof(Glyph, advanceX);
    table.stride = sizeof(Glyph);
    table.fallback = 5 * 16;
    metrics = m;
  }

  Glyph glyphs[4] = {};
  const AdvanceRange ranges[2] = {{0x41, 0x43, 0}, {0xE9, 0xE9, 3}};
  TextMetrics metrics;
};

TEST_F(TableTest, LooksUpEachCodePointAndRoundsEachAdvance) {
  EXPECT_EQ(metrics.tables[1].advanceOf('A'), 160);
  EXPECT_EQ(metrics.tables[1].advanceOf(0xE9), 192);
  // 7.5 px rounds up to 8 and 7.4375 down to 7, each glyph on its own.
  EXPECT_EQ(metrics.width("B", TextSize::Medium), 8);
  EXPECT_EQ(metrics.width("C", TextSize::Medium), 7);
  EXPECT_EQ(metrics.width("ABC", TextSize::Medium), 10 + 8 + 7);
  EXPECT_EQ(metrics.width("A\xC3\xA9", TextSize::Medium), 10 + 12);
  // The other sizes keep their own tables.
  EXPECT_EQ(metrics.width("ABC", TextSize::Small), 3 * STAND_IN_ADVANCE_PX[0]);
}

TEST_F(TableTest, CodePointsOutsideEveryRangeTakeTheFallback) {
  // Below the first range, between ranges, and past the last.
  EXPECT_EQ(metrics.width(" ", TextSize::Medium), 5);
  EXPECT_EQ(metrics.width("D", TextSize::Medium), 5);
  EXPECT_EQ(metrics.width("\xC3\xAA", TextSize::Medium), 5);
  EXPECT_EQ(metrics.width("\xF0\x9F\x98\x80", TextSize::Medium), 5);
}

TEST_F(TableTest, MalformedUtf8CountsAsReplacementCharacters) {
  // A stray continuation byte, and a lead byte whose continuation is missing.
  EXPECT_EQ(metrics.width("A\x80"
                          "A",
                          TextSize::Medium),
            10 + 5 + 10);
  EXPECT_EQ(metrics.width("\xC3"
                          "A",
                          TextSize::Medium),
            5 + 10);
}

TEST_F(TableTest, StopsAtTheFirstNul) {
  const char text[] = {'A', 'B', '\0', 'C', '\0'};
  EXPECT_EQ(metrics.width(text, TextSize::Medium), 10 + 8);
}

// One glyph as replay draws it: the code point handed to the font and its pen x.
struct Placed {
  uint32_t codepoint;
  int64_t x;
  bool known;
};

// Lays text out as FrameReplay::drawText does: from alignedStart, each glyph at
// start + penX.
std::vector<Placed> layOut(const TextMetrics& metrics, const char* text, const TextSize size, const int64_t x,
                           const Align align) {
  std::vector<Placed> placed;
  const int64_t start = TextMetrics::alignedStart(x, metrics.width(text, size), align);
  metrics.forEachGlyph(text, size, [&](const uint32_t cp, const int64_t pen, const bool known) {
    placed.push_back(Placed{cp, start + pen, known});
  });
  return placed;
}

TEST_F(TableTest, ForEachGlyphGivesEachPenPositionAndReturnsTheWidth) {
  std::vector<Placed> seen;
  const int64_t width = metrics.forEachGlyph(
      "AB\xC3\xA9"
      "D",
      TextSize::Medium,
      [&](const uint32_t cp, const int64_t pen, const bool known) { seen.push_back(Placed{cp, pen, known}); });
  ASSERT_EQ(seen.size(), 4u);
  EXPECT_EQ(seen[0].codepoint, 'A');
  EXPECT_EQ(seen[0].x, 0);
  EXPECT_TRUE(seen[0].known);
  EXPECT_EQ(seen[1].x, 10);
  EXPECT_EQ(seen[2].codepoint, 0xE9u);
  EXPECT_EQ(seen[2].x, 18);
  // 'D' is in no range: the font has no glyph for it, and it takes the fallback.
  EXPECT_EQ(seen[3].codepoint, 'D');
  EXPECT_EQ(seen[3].x, 30);
  EXPECT_FALSE(seen[3].known);
  EXPECT_EQ(width, 35);
  EXPECT_EQ(width, metrics.width("AB\xC3\xA9"
                                 "D",
                                 TextSize::Medium));
}

TEST_F(TableTest, DrawnTextIsExactlyAsWideAsTextWidthForEveryAlignment) {
  // The rule replay keeps: the first glyph's pen is the aligned start, and the pen
  // after the last glyph is start + ch.text_width, for any string and alignment.
  const char* const texts[] = {"A", "ABC", "CAB\xC3\xA9", "D\x80Z",
                               "\xF0\x9F\x98\x80"
                               "A"};
  for (const char* text : texts) {
    const int64_t width = metrics.width(text, TextSize::Medium);
    for (const Align align : {Align::Left, Align::Center, Align::Right}) {
      const auto placed = layOut(metrics, text, TextSize::Medium, 200, align);
      ASSERT_FALSE(placed.empty());
      const int64_t start = placed.front().x;
      const Placed& last = placed.back();
      uint16_t lastAdvance = metrics.tables[1].fallback;
      metrics.tables[1].find(last.codepoint, lastAdvance);
      EXPECT_EQ(last.x + TextMetrics::toPixel(lastAdvance) - start, width) << text;
      // Each glyph's pen moves by exactly its rounded table advance.
      for (size_t i = 1; i < placed.size(); ++i) {
        EXPECT_EQ(placed[i].x - placed[i - 1].x,
                  TextMetrics::toPixel(metrics.tables[1].advanceOf(placed[i - 1].codepoint)));
      }
    }
  }
}

TEST(TextMetricsTest, AlignmentMovesTheStartByTheWidth) {
  EXPECT_EQ(TextMetrics::alignedStart(100, 41, Align::Left), 100);
  EXPECT_EQ(TextMetrics::alignedStart(100, 41, Align::Center), 80);  // half of 41 is 20
  EXPECT_EQ(TextMetrics::alignedStart(100, 41, Align::Right), 59);
  EXPECT_EQ(TextMetrics::alignedStart(0, 0, Align::Right), 0);
  constexpr TextMetrics metrics = TextMetrics::standIn();
  const auto placed = layOut(metrics, "abcd", TextSize::Large, 240, Align::Center);
  ASSERT_EQ(placed.size(), 4u);
  EXPECT_EQ(placed[0].x, 240 - 2 * STAND_IN_ADVANCE_PX[2]);
  EXPECT_EQ(placed[3].x + STAND_IN_ADVANCE_PX[2], 240 + 2 * STAND_IN_ADVANCE_PX[2]);
}

TEST(TextMetricsTest, RangesAreReadAtTheirStrideLikeFontIntervalsInsideLargerRecords) {
  // Interval records with a trailing field, as a table can point into any array of
  // records that begin with first, last, index.
  struct Record {
    uint32_t first;
    uint32_t last;
    uint32_t index;
    uint32_t extra;
  };
  const Record records[] = {{'a', 'b', 1, 0xFFFFFFFF}, {'x', 'x', 0, 0xFFFFFFFF}};
  const uint16_t advances[] = {3 * 16, 5 * 16, 6 * 16};
  TextMetrics metrics = TextMetrics::standIn();
  AdvanceTable& small = metrics.tables[0];
  small.ranges = reinterpret_cast<const uint8_t*>(records);
  small.rangeCount = 2;
  small.rangeStride = sizeof(Record);
  small.advances = reinterpret_cast<const uint8_t*>(advances);
  small.fallback = 1 * 16;
  EXPECT_EQ(metrics.width("abx", TextSize::Small), 5 + 6 + 3);
  EXPECT_EQ(metrics.width("c", TextSize::Small), 1);
  uint16_t advance = 0;
  EXPECT_FALSE(small.find('c', advance));
  EXPECT_TRUE(small.find('x', advance));
  EXPECT_EQ(advance, 3 * 16);
}

}  // namespace
