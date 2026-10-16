// The UltraChip stock-path bench's pure parts (lib/GameDisplayLab/StockBench): the checker test frame and its
// inversion, the gray ramp's bands in the overlay encoding on both axes and mirrored, the timing summary, and the
// fell-back heuristic.

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <vector>

#include "StockBench.h"

namespace {

using namespace GameDisplayLab;

size_t rowBytes(const uint16_t width) { return (static_cast<size_t>(width) + 7u) / 8u; }

bool bitAt(const std::vector<uint8_t>& plane, const uint16_t width, const uint16_t x, const uint16_t y) {
  const uint8_t byte = plane[static_cast<size_t>(y) * rowBytes(width) + (x >> 3)];
  return (byte & (0x80u >> (x & 7))) != 0;
}

// Every pad bit past `width` in each row's last byte is clear.
void expectPadClear(const std::vector<uint8_t>& plane, const uint16_t width, const uint16_t height) {
  if (width % 8 == 0) return;
  const uint8_t padMask = static_cast<uint8_t>(0xFFu >> (width % 8));
  for (uint16_t y = 0; y < height; ++y) {
    ASSERT_EQ(plane[static_cast<size_t>(y) * rowBytes(width) + rowBytes(width) - 1] & padMask, 0) << "row " << y;
  }
}

// ---- fillChecker ----

TEST(StockBenchChecker, EightBlocksAt800x480InFramebufferOrder) {
  constexpr uint16_t W = 800;
  constexpr uint16_t H = 480;
  std::vector<uint8_t> frame(rowBytes(W) * H, 0xA5);
  fillChecker(frame.data(), W, H, false);
  // Blocks are 200 x 240: 25 bytes each per row. Row 0: white, black, white, black.
  for (size_t i = 0; i < 100; ++i) {
    const uint8_t expected = (i / 25) % 2 == 0 ? 0xFF : 0x00;
    ASSERT_EQ(frame[i], expected) << "top byte " << i;
    ASSERT_EQ(frame[239 * 100 + i], expected) << "last top-row byte " << i;
    // The bottom row of blocks starts black.
    ASSERT_EQ(frame[240 * 100 + i], static_cast<uint8_t>(~expected)) << "bottom byte " << i;
    ASSERT_EQ(frame[479 * 100 + i], static_cast<uint8_t>(~expected)) << "last byte " << i;
  }
  for (uint16_t y = 0; y < H; y += 7) {
    for (uint16_t x = 0; x < W; x += 3) {
      const bool white = ((x / 200) + (y / 240)) % 2 == 0;
      ASSERT_EQ(bitAt(frame, W, x, y), white) << x << "," << y;
    }
  }
}

TEST(StockBenchChecker, InvertedIsTheComplement) {
  constexpr uint16_t W = 800;
  constexpr uint16_t H = 480;
  std::vector<uint8_t> plain(rowBytes(W) * H);
  std::vector<uint8_t> inverted(rowBytes(W) * H);
  fillChecker(plain.data(), W, H, false);
  fillChecker(inverted.data(), W, H, true);
  for (size_t i = 0; i < plain.size(); ++i) ASSERT_EQ(inverted[i], static_cast<uint8_t>(~plain[i])) << "byte " << i;
  EXPECT_EQ(inverted[0], 0x00);  // block (0, 0) black
}

TEST(StockBenchChecker, WidthNotAMultipleOfEightKeepsPadBitsClearAndGivesTheRemainderToTheLastBlock) {
  constexpr uint16_t W = 803;  // blocks of 200, the last column 203 wide
  constexpr uint16_t H = 5;    // rows of 2, the last row 3 high
  for (const bool inverted : {false, true}) {
    std::vector<uint8_t> frame(rowBytes(W) * H, 0xFF);
    fillChecker(frame.data(), W, H, inverted);
    expectPadClear(frame, W, H);
    for (uint16_t y = 0; y < H; ++y) {
      const unsigned r = y < 2 ? 0 : 1;
      for (uint16_t x = 0; x < W; ++x) {
        const unsigned c = x < 600 ? x / 200 : 3;
        const bool white = ((c + r) % 2 == 0) != inverted;
        ASSERT_EQ(bitAt(frame, W, x, y), white) << x << "," << y << (inverted ? " inverted" : "");
      }
    }
  }
}

TEST(StockBenchChecker, NullFrameIsIgnored) { fillChecker(nullptr, 800, 480, false); }

// ---- rampShadeAt ----

TEST(StockBenchRamp, FourEqualBandsAlongXWithTheRemainderInTheLast) {
  constexpr uint16_t W = 803;
  constexpr uint16_t H = 480;
  EXPECT_EQ(rampShadeAt(0, 0, W, H, BandAxis::X, false), Shade::Black);
  EXPECT_EQ(rampShadeAt(199, 479, W, H, BandAxis::X, false), Shade::Black);
  EXPECT_EQ(rampShadeAt(200, 0, W, H, BandAxis::X, false), Shade::DarkGray);
  EXPECT_EQ(rampShadeAt(399, 0, W, H, BandAxis::X, false), Shade::DarkGray);
  EXPECT_EQ(rampShadeAt(400, 0, W, H, BandAxis::X, false), Shade::LightGray);
  EXPECT_EQ(rampShadeAt(599, 0, W, H, BandAxis::X, false), Shade::LightGray);
  EXPECT_EQ(rampShadeAt(600, 0, W, H, BandAxis::X, false), Shade::White);
  EXPECT_EQ(rampShadeAt(802, 0, W, H, BandAxis::X, false), Shade::White);
  // Past the panel counts as its last pixel.
  EXPECT_EQ(rampShadeAt(9999, 0, W, H, BandAxis::X, false), Shade::White);
}

TEST(StockBenchRamp, ReversedMirrorsTheBands) {
  constexpr uint16_t W = 800;
  constexpr uint16_t H = 480;
  EXPECT_EQ(rampShadeAt(799, 0, W, H, BandAxis::X, true), Shade::Black);
  EXPECT_EQ(rampShadeAt(600, 0, W, H, BandAxis::X, true), Shade::Black);
  EXPECT_EQ(rampShadeAt(599, 0, W, H, BandAxis::X, true), Shade::DarkGray);
  EXPECT_EQ(rampShadeAt(400, 0, W, H, BandAxis::X, true), Shade::DarkGray);
  EXPECT_EQ(rampShadeAt(399, 0, W, H, BandAxis::X, true), Shade::LightGray);
  EXPECT_EQ(rampShadeAt(200, 0, W, H, BandAxis::X, true), Shade::LightGray);
  EXPECT_EQ(rampShadeAt(199, 0, W, H, BandAxis::X, true), Shade::White);
  EXPECT_EQ(rampShadeAt(0, 0, W, H, BandAxis::X, true), Shade::White);
  for (uint16_t x = 0; x < W; ++x) {
    ASSERT_EQ(rampShadeAt(x, 7, W, H, BandAxis::X, true), rampShadeAt(W - 1 - x, 7, W, H, BandAxis::X, false)) << x;
  }
}

TEST(StockBenchRamp, AlongYIgnoresX) {
  constexpr uint16_t W = 800;
  constexpr uint16_t H = 480;  // bands of 120
  for (const uint16_t x : {0, 333, 799}) {
    EXPECT_EQ(rampShadeAt(x, 0, W, H, BandAxis::Y, false), Shade::Black);
    EXPECT_EQ(rampShadeAt(x, 119, W, H, BandAxis::Y, false), Shade::Black);
    EXPECT_EQ(rampShadeAt(x, 120, W, H, BandAxis::Y, false), Shade::DarkGray);
    EXPECT_EQ(rampShadeAt(x, 240, W, H, BandAxis::Y, false), Shade::LightGray);
    EXPECT_EQ(rampShadeAt(x, 360, W, H, BandAxis::Y, false), Shade::White);
    EXPECT_EQ(rampShadeAt(x, 479, W, H, BandAxis::Y, false), Shade::White);
    EXPECT_EQ(rampShadeAt(x, 479, W, H, BandAxis::Y, true), Shade::Black);
    EXPECT_EQ(rampShadeAt(x, 360, W, H, BandAxis::Y, true), Shade::Black);
    EXPECT_EQ(rampShadeAt(x, 359, W, H, BandAxis::Y, true), Shade::DarkGray);
    EXPECT_EQ(rampShadeAt(x, 0, W, H, BandAxis::Y, true), Shade::White);
  }
}

TEST(StockBenchRamp, ShortAndEmptyAxes) {
  // Shorter than four bands: one pixel each, from Black.
  EXPECT_EQ(rampShadeAt(0, 0, 3, 1, BandAxis::X, false), Shade::Black);
  EXPECT_EQ(rampShadeAt(1, 0, 3, 1, BandAxis::X, false), Shade::DarkGray);
  EXPECT_EQ(rampShadeAt(2, 0, 3, 1, BandAxis::X, false), Shade::LightGray);
  EXPECT_EQ(rampShadeAt(0, 0, 0, 0, BandAxis::X, false), Shade::Black);
  EXPECT_EQ(rampShadeAt(0, 0, 0, 0, BandAxis::Y, true), Shade::Black);
}

// ---- fillGrayRamp ----

struct Ramp {
  std::vector<uint8_t> base;
  std::vector<uint8_t> lsb;
  std::vector<uint8_t> msb;

  Ramp(const uint16_t width, const uint16_t height)
      : base(rowBytes(width) * height, 0x5A),
        lsb(rowBytes(width) * height, 0x5A),
        msb(rowBytes(width) * height, 0x5A) {}
};

// Each pixel's (base, lsb, msb) bits are the overlay encoding of its shade.
void expectOverlayEncoding(const Ramp& ramp, const uint16_t width, const uint16_t height, const BandAxis axis,
                           const bool reversed) {
  for (uint16_t y = 0; y < height; ++y) {
    for (uint16_t x = 0; x < width; ++x) {
      bool base = false;
      bool lsb = false;
      bool msb = false;
      switch (rampShadeAt(x, y, width, height, axis, reversed)) {
        case Shade::Black:
          break;
        case Shade::DarkGray:
          lsb = msb = true;
          break;
        case Shade::LightGray:
          msb = true;
          break;
        case Shade::White:
          base = true;
          break;
      }
      ASSERT_EQ(bitAt(ramp.base, width, x, y), base) << "base " << x << "," << y;
      ASSERT_EQ(bitAt(ramp.lsb, width, x, y), lsb) << "lsb " << x << "," << y;
      ASSERT_EQ(bitAt(ramp.msb, width, x, y), msb) << "msb " << x << "," << y;
    }
  }
  expectPadClear(ramp.base, width, height);
  expectPadClear(ramp.lsb, width, height);
  expectPadClear(ramp.msb, width, height);
}

TEST(StockBenchGrayRamp, BandBytesAlongXAt800) {
  constexpr uint16_t W = 800;
  constexpr uint16_t H = 4;
  Ramp ramp(W, H);
  fillGrayRamp(ramp.base.data(), ramp.lsb.data(), ramp.msb.data(), W, H, BandAxis::X, false);
  // Bands of 200 px = 25 bytes: K (0,0,0), D (0,1,1), L (0,0,1), W (1,0,0) as (base, lsb, msb).
  for (uint16_t y = 0; y < H; ++y) {
    for (size_t i = 0; i < 100; ++i) {
      const size_t at = static_cast<size_t>(y) * 100 + i;
      const size_t band = i / 25;
      ASSERT_EQ(ramp.base[at], band == 3 ? 0xFF : 0x00) << at;
      ASSERT_EQ(ramp.lsb[at], band == 1 ? 0xFF : 0x00) << at;
      ASSERT_EQ(ramp.msb[at], band == 1 || band == 2 ? 0xFF : 0x00) << at;
    }
  }
}

TEST(StockBenchGrayRamp, EveryPixelEncodedOnBothAxesAndReversed) {
  constexpr uint16_t W = 803;
  constexpr uint16_t H = 21;
  for (const BandAxis axis : {BandAxis::X, BandAxis::Y}) {
    for (const bool reversed : {false, true}) {
      Ramp ramp(W, H);
      fillGrayRamp(ramp.base.data(), ramp.lsb.data(), ramp.msb.data(), W, H, axis, reversed);
      expectOverlayEncoding(ramp, W, H, axis, reversed);
    }
  }
}

TEST(StockBenchGrayRamp, ReversedYPutsBlackAtTheBottom) {
  constexpr uint16_t W = 16;
  constexpr uint16_t H = 8;  // bands of two rows
  Ramp ramp(W, H);
  fillGrayRamp(ramp.base.data(), ramp.lsb.data(), ramp.msb.data(), W, H, BandAxis::Y, true);
  // Rows 0-1 white, 2-3 light, 4-5 dark, 6-7 black.
  const uint8_t base[H] = {0xFF, 0xFF, 0, 0, 0, 0, 0, 0};
  const uint8_t lsb[H] = {0, 0, 0, 0, 0xFF, 0xFF, 0, 0};
  const uint8_t msb[H] = {0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0};
  for (uint16_t y = 0; y < H; ++y) {
    for (size_t b = 0; b < 2; ++b) {
      ASSERT_EQ(ramp.base[y * 2 + b], base[y]) << "row " << y;
      ASSERT_EQ(ramp.lsb[y * 2 + b], lsb[y]) << "row " << y;
      ASSERT_EQ(ramp.msb[y * 2 + b], msb[y]) << "row " << y;
    }
  }
}

TEST(StockBenchGrayRamp, NullPlaneWritesNothing) {
  std::vector<uint8_t> base(2, 0x5A);
  std::vector<uint8_t> lsb(2, 0x5A);
  fillGrayRamp(base.data(), lsb.data(), nullptr, 16, 1, BandAxis::X, false);
  EXPECT_EQ(base[0], 0x5A);
  EXPECT_EQ(lsb[0], 0x5A);
}

// ---- summarizeTimings ----

TEST(StockBenchSummary, EmptyGivesZeros) {
  const TimingSummary none = summarizeTimings(nullptr, 5);
  EXPECT_EQ(none.min, 0u);
  EXPECT_EQ(none.median, 0u);
  EXPECT_EQ(none.max, 0u);
  EXPECT_EQ(none.count, 0u);
  const uint32_t values[] = {7};
  const TimingSummary zero = summarizeTimings(values, 0);
  EXPECT_EQ(zero.min, 0u);
  EXPECT_EQ(zero.median, 0u);
  EXPECT_EQ(zero.max, 0u);
  EXPECT_EQ(zero.count, 0u);
}

TEST(StockBenchSummary, OddCountTakesTheMiddleValue) {
  const uint32_t values[] = {40, 10, 30, 50, 20};
  const TimingSummary s = summarizeTimings(values, 5);
  EXPECT_EQ(s.min, 10u);
  EXPECT_EQ(s.median, 30u);
  EXPECT_EQ(s.max, 50u);
  EXPECT_EQ(s.count, 5u);
  const uint32_t one[] = {123};
  const TimingSummary single = summarizeTimings(one, 1);
  EXPECT_EQ(single.min, 123u);
  EXPECT_EQ(single.median, 123u);
  EXPECT_EQ(single.max, 123u);
  EXPECT_EQ(single.count, 1u);
}

TEST(StockBenchSummary, EvenCountTakesTheLowerRoundedMeanOfTheMiddleTwo) {
  const uint32_t values[] = {400, 100, 301, 200};
  const TimingSummary s = summarizeTimings(values, 4);
  EXPECT_EQ(s.min, 100u);
  EXPECT_EQ(s.median, 250u);  // (200 + 301) / 2 = 250.5, rounded down
  EXPECT_EQ(s.max, 400u);
  EXPECT_EQ(s.count, 4u);
  // The mean of two values near UINT32_MAX does not overflow.
  constexpr uint32_t TOP = std::numeric_limits<uint32_t>::max();
  const uint32_t big[] = {TOP, TOP - 3};
  EXPECT_EQ(summarizeTimings(big, 2).median, TOP - 2);  // (2^32 - 1 + 2^32 - 4) / 2 rounded down
}

TEST(StockBenchSummary, TheTenTransitionsOfARun) {
  const uint32_t values[] = {310, 290, 300, 305, 295, 1800, 299, 301, 302, 298};
  const TimingSummary s = summarizeTimings(values, 10);
  EXPECT_EQ(s.min, 290u);
  EXPECT_EQ(s.median, 300u);  // middle two 300 and 301
  EXPECT_EQ(s.max, 1800u);
  EXPECT_EQ(s.count, 10u);
}

TEST(StockBenchSummary, OverTheCapReadsOnlyTheFirstSixteen) {
  std::vector<uint32_t> values(20);
  for (size_t i = 0; i < values.size(); ++i) values[i] = static_cast<uint32_t>(i + 1);  // 1 .. 20
  values[19] = 0;  // past the cap: must not become the min
  const TimingSummary s = summarizeTimings(values.data(), values.size());
  EXPECT_EQ(s.count, MAX_TIMINGS);
  EXPECT_EQ(s.min, 1u);
  EXPECT_EQ(s.max, 16u);
  EXPECT_EQ(s.median, 8u);  // (8 + 9) / 2 rounded down
}

TEST(StockBenchSummary, DoesNotReorderTheCallersValues) {
  const uint32_t values[] = {3, 1, 2};
  summarizeTimings(values, 3);
  EXPECT_EQ(values[0], 3u);
  EXPECT_EQ(values[1], 1u);
  EXPECT_EQ(values[2], 2u);
}

// ---- transitionFellBack ----

TEST(StockBenchFellBack, UnarmedAlwaysFellBack) {
  EXPECT_TRUE(transitionFellBack(0, 0, false));
  EXPECT_TRUE(transitionFellBack(10, 1000, false));
  EXPECT_TRUE(transitionFellBack(5000, 1000, false));
}

TEST(StockBenchFellBack, ArmedAgainstTheFactor) {
  static_assert(FELL_BACK_FACTOR == 2);
  EXPECT_FALSE(transitionFellBack(1000, 1000, true));  // as long as the reference
  EXPECT_FALSE(transitionFellBack(1999, 1000, true));  // just under the factor
  EXPECT_TRUE(transitionFellBack(2000, 1000, true));   // at the factor
  EXPECT_TRUE(transitionFellBack(2001, 1000, true));   // over it
  EXPECT_FALSE(transitionFellBack(10, 1000, true));    // faster than the reference
}

TEST(StockBenchFellBack, ZeroReferenceJudgesNoArmedCallAFallBack) {
  EXPECT_FALSE(transitionFellBack(0, 0, true));
  EXPECT_FALSE(transitionFellBack(std::numeric_limits<uint32_t>::max(), 0, true));
}

TEST(StockBenchFellBack, NearUint32MaxDoesNotOverflow) {
  constexpr uint32_t TOP = std::numeric_limits<uint32_t>::max();
  // 2 * reference wraps in 32 bits; the heuristic still compares the true product.
  EXPECT_FALSE(transitionFellBack(TOP, TOP / 2 + 1, true));  // 2 * ref = 2^32 > TOP
  EXPECT_TRUE(transitionFellBack(TOP, TOP / 2, true));       // 2 * ref = TOP - 1
  EXPECT_FALSE(transitionFellBack(TOP - 1, TOP, true));
  EXPECT_FALSE(transitionFellBack(TOP, TOP, true));
  EXPECT_TRUE(transitionFellBack(TOP, 1, true));
}

}  // namespace
