#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

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
    table.ranges = ranges;
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
  EXPECT_EQ(metrics.width("A\x80" "A", TextSize::Medium), 10 + 5 + 10);
  EXPECT_EQ(metrics.width("\xC3" "A", TextSize::Medium), 5 + 10);
}

TEST_F(TableTest, StopsAtTheFirstNul) {
  const char text[] = {'A', 'B', '\0', 'C', '\0'};
  EXPECT_EQ(metrics.width(text, TextSize::Medium), 10 + 8);
}

}  // namespace
