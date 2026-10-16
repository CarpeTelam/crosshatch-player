// The SSD1677 scan backend's pure parts (lib/GameDisplayLab/Ssd1677Encoding): its rate list, the LUT for a request,
// and the planes from the two masks, pinned against Part A's bytes (buildOneFrameLut with the raw frame-rate byte, and
// planeFillFor) for all nine frame-period scans and both polarity rows.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "LabExperiments.h"
#include "MaskPlanes.h"
#include "Ssd1677Encoding.h"
#include "Ssd1677ScanLut.h"

namespace {

using namespace GameDisplayLab;
using Mask = MaskPlanes::Mask;

constexpr uint16_t WIDTH = 800;
constexpr uint16_t HEIGHT = 480;

struct Lut {
  uint8_t bytes[LUT_BYTES];
};

Lut poisoned() {
  Lut lut;
  for (uint8_t& b : lut.bytes) b = 0xA5;
  return lut;
}

// Part A's LUT for one scan: the raw frame-rate byte and the polarity codes on rows 1 and 2.
Lut partA(const uint8_t tp, const uint8_t rateByte) {
  Lut lut = poisoned();
  buildOneFrameLut(lut.bytes, tp, rateByte, POLARITY_ROW1, POLARITY_ROW2);
  return lut;
}

Lut seam(const uint8_t frames, const uint8_t rateIndex, const bool swap) {
  Lut lut = poisoned();
  EXPECT_TRUE(buildSsd1677ScanLut(lut.bytes, frames, rateIndex, swap));
  return lut;
}

bool same(const Lut& a, const Lut& b) { return memcmp(a.bytes, b.bytes, LUT_BYTES) == 0; }

// Full-screen masks, the planes encoded from them, and a check that every plane byte is the expected one.
struct Screen {
  std::vector<uint8_t> black = std::vector<uint8_t>(MaskPlanes::bytesFor(WIDTH, HEIGHT), 0xA5);
  std::vector<uint8_t> white = std::vector<uint8_t>(MaskPlanes::bytesFor(WIDTH, HEIGHT), 0xA5);
  std::vector<uint8_t> bw = std::vector<uint8_t>(MaskPlanes::bytesFor(WIDTH, HEIGHT), 0xA5);
  std::vector<uint8_t> red = std::vector<uint8_t>(MaskPlanes::bytesFor(WIDTH, HEIGHT), 0xA5);
  MaskPlanes masks{black.data(), white.data(), WIDTH, HEIGHT};

  void encode() { encodeSsd1677Planes(black.data(), white.data(), bw.data(), red.data(), bw.size()); }
};

void expectEvery(const std::vector<uint8_t>& plane, const uint8_t value, const char* name) {
  for (size_t i = 0; i < plane.size(); ++i) ASSERT_EQ(plane[i], value) << name << " byte " << i;
}

TEST(Ssd1677Encoding, RateListIsPartAFrameRateBytesInOrder) {
  EXPECT_EQ(SSD1677_RATE_COUNT, 3u);
  EXPECT_STREQ(ssd1677RateLabel(0), "0x22");
  EXPECT_STREQ(ssd1677RateLabel(1), "0x11");
  EXPECT_STREQ(ssd1677RateLabel(2), "0x44");
  for (uint8_t i = 0; i < SSD1677_RATE_COUNT; ++i) {
    // Part A printed the label as "0x%02X" of the byte, and its log as "0x%02x": the same text for these bytes.
    char upper[8];
    char lower[8];
    snprintf(upper, sizeof(upper), "0x%02X", static_cast<unsigned>(FRAME_PERIOD_RATES[i]));
    snprintf(lower, sizeof(lower), "0x%02x", static_cast<unsigned>(FRAME_PERIOD_RATES[i]));
    EXPECT_STREQ(ssd1677RateLabel(i), upper) << "rate " << static_cast<unsigned>(i);
    EXPECT_STREQ(ssd1677RateLabel(i), lower) << "rate " << static_cast<unsigned>(i);
  }
  EXPECT_EQ(ssd1677RateLabel(3), nullptr);
  EXPECT_EQ(ssd1677RateLabel(255), nullptr);
}

TEST(Ssd1677Encoding, RowCodesSwapOnRequest) {
  const Ssd1677RowCodes plain = ssd1677RowCodes(false);
  EXPECT_EQ(plain.row1, POLARITY_ROW1);
  EXPECT_EQ(plain.row2, POLARITY_ROW2);
  const Ssd1677RowCodes swapped = ssd1677RowCodes(true);
  EXPECT_EQ(swapped.row1, POLARITY_ROW2);
  EXPECT_EQ(swapped.row2, POLARITY_ROW1);
}

TEST(Ssd1677Encoding, RowLabelsNameEachRowsLutAndCode) {
  for (const bool swap : {false, true}) {
    const Ssd1677RowCodes codes = ssd1677RowCodes(swap);
    const std::string row1 = std::string("LUT1 ") + vsName(codes.row1);
    const std::string row2 = std::string("LUT2 ") + vsName(codes.row2);
    ASSERT_NE(ssd1677RowLabel(1, swap), nullptr) << "swap " << swap;
    ASSERT_NE(ssd1677RowLabel(2, swap), nullptr) << "swap " << swap;
    EXPECT_EQ(row1, ssd1677RowLabel(1, swap)) << "swap " << swap;
    EXPECT_EQ(row2, ssd1677RowLabel(2, swap)) << "swap " << swap;
    EXPECT_EQ(ssd1677RowLabel(0, swap), nullptr) << "swap " << swap;
    EXPECT_EQ(ssd1677RowLabel(3, swap), nullptr) << "swap " << swap;
  }
  // The unswapped labels the polarity screen shows.
  EXPECT_STREQ(ssd1677RowLabel(1, false), "LUT1 VSL");
  EXPECT_STREQ(ssd1677RowLabel(2, false), "LUT2 VSH1");
}

TEST(Ssd1677Encoding, FramePeriodScanOrderIsPartA) {
  // The frame-period screen's scan i (framePeriodTp/Rate) is Part A's scan i: the same TP and the same rate byte.
  EXPECT_EQ(SSD1677_RATE_COUNT * sizeof(FRAME_PERIOD_TPS), FRAME_PERIOD_SCANS);
  for (size_t i = 0; i < FRAME_PERIOD_SCANS; ++i) {
    const ScanParams params = framePeriodScan(i);
    EXPECT_EQ(framePeriodTp(i, SSD1677_RATE_COUNT), params.tp) << "scan " << i;
    const uint8_t rateIndex = framePeriodRate(i, SSD1677_RATE_COUNT);
    ASSERT_LT(rateIndex, SSD1677_RATE_COUNT) << "scan " << i;
    EXPECT_EQ(FRAME_PERIOD_RATES[rateIndex], params.rate) << "scan " << i;
  }
}

TEST(LabExperiments, FramePeriodOrderIsTpOuterRateInnerForAnyRateCount) {
  constexpr size_t TPS = sizeof(FRAME_PERIOD_TPS);
  for (const uint8_t rateCount : {static_cast<uint8_t>(1), static_cast<uint8_t>(16)}) {
    size_t i = 0;
    for (size_t tp = 0; tp < TPS; ++tp) {
      for (uint8_t rate = 0; rate < rateCount; ++rate, ++i) {
        EXPECT_EQ(framePeriodTp(i, rateCount), FRAME_PERIOD_TPS[tp]) << "rates " << +rateCount << " scan " << i;
        EXPECT_EQ(framePeriodRate(i, rateCount), rate) << "rates " << +rateCount << " scan " << i;
      }
    }
    EXPECT_EQ(i, TPS * rateCount);
  }
}

TEST(Ssd1677Encoding, FramePeriodLutsEqualPartA) {
  for (size_t i = 0; i < FRAME_PERIOD_SCANS; ++i) {
    const ScanParams params = framePeriodScan(i);
    const Lut sent = seam(framePeriodTp(i, SSD1677_RATE_COUNT), framePeriodRate(i, SSD1677_RATE_COUNT), false);
    EXPECT_TRUE(same(sent, partA(params.tp, params.rate))) << "scan " << i;
  }
}

TEST(Ssd1677Encoding, PolarityLutEqualsPartA) {
  EXPECT_TRUE(same(seam(POLARITY_TP, 0, false), partA(POLARITY_TP, FRAME_PERIOD_RATES[0])));
}

TEST(Ssd1677Encoding, SwapSwapsTheRowCodesOnly) {
  const Lut swapped = seam(POLARITY_TP, 0, true);
  Lut expected = poisoned();
  buildOneFrameLut(expected.bytes, POLARITY_TP, FRAME_PERIOD_RATES[0], POLARITY_ROW2, POLARITY_ROW1);
  EXPECT_TRUE(same(swapped, expected));
  // Row 1 now carries VSH1 (01) and row 2 VSL (10); everything else is Part A's.
  EXPECT_EQ(swapped.bytes[1 * ROW_BYTES], 0x40);
  EXPECT_EQ(swapped.bytes[2 * ROW_BYTES], 0x80);
  const Lut plain = seam(POLARITY_TP, 0, false);
  for (size_t i = 0; i < LUT_BYTES; ++i) {
    if (i == 1 * ROW_BYTES || i == 2 * ROW_BYTES) continue;
    EXPECT_EQ(swapped.bytes[i], plain.bytes[i]) << "byte " << i;
  }
}

TEST(Ssd1677Encoding, BadRateIndexIsRefusedAndWritesNothing) {
  for (const uint8_t rateIndex : {SSD1677_RATE_COUNT, static_cast<uint8_t>(16), static_cast<uint8_t>(255)}) {
    Lut lut = poisoned();
    EXPECT_FALSE(buildSsd1677ScanLut(lut.bytes, 10, rateIndex, false)) << "rate " << static_cast<unsigned>(rateIndex);
    for (const uint8_t b : lut.bytes) ASSERT_EQ(b, 0xA5);
  }
}

TEST(Ssd1677Encoding, AllHoldMasksGivePartAHoldPlanes) {
  Screen screen;
  screen.masks.clear();
  screen.encode();
  const PlaneFill hold = planeFillFor(LutRow::Hold);
  expectEvery(screen.bw, hold.bw, "BW");
  expectEvery(screen.red, hold.red, "RED");
}

TEST(Ssd1677Encoding, WhiteMaskGivesPartARowOnePlanes) {
  Screen screen;
  screen.masks.clear();
  screen.masks.fill(Mask::White);
  screen.encode();
  const PlaneFill row1 = planeFillFor(LutRow::Row1);
  expectEvery(screen.bw, row1.bw, "BW");     // 0xFF
  expectEvery(screen.red, row1.red, "RED");  // 0x00
}

TEST(Ssd1677Encoding, BlackMaskGivesPartARowTwoPlanes) {
  Screen screen;
  screen.masks.clear();
  screen.masks.fill(Mask::Black);
  screen.encode();
  const PlaneFill row2 = planeFillFor(LutRow::Row2);
  expectEvery(screen.bw, row2.bw, "BW");     // 0x00
  expectEvery(screen.red, row2.red, "RED");  // 0xFF
}

TEST(Ssd1677Encoding, PolarityRowMasksGivePartARowPlanes) {
  // The polarity screen's row r fills POLARITY_ROW_MASKS[r] on cleared masks: row 1 must select LUT1, row 2 LUT2.
  constexpr LutRow PART_A_ROWS[POLARITY_ROWS] = {LutRow::Row1, LutRow::Row2};
  for (size_t row = 0; row < POLARITY_ROWS; ++row) {
    Screen screen;
    screen.masks.clear();
    screen.masks.fill(POLARITY_ROW_MASKS[row]);
    screen.encode();
    const PlaneFill expected = planeFillFor(PART_A_ROWS[row]);
    SCOPED_TRACE(row + 1);
    expectEvery(screen.bw, expected.bw, "BW");
    expectEvery(screen.red, expected.red, "RED");
  }
}

TEST(Ssd1677Encoding, PlanesFollowTheMasksPixelForPixel) {
  Screen screen;
  screen.masks.clear();
  screen.masks.setRect(Mask::White, 3, 0, 10, 2);
  screen.masks.setRect(Mask::Black, 8, 1, 4, 1);  // overlaps white: both bits, LUT3 (holds)
  screen.masks.setPixel(Mask::Black, 799, 479);
  screen.encode();
  EXPECT_EQ(screen.bw, screen.white);
  EXPECT_EQ(screen.red, screen.black);
  EXPECT_EQ(screen.bw[0], 0x1F);
  EXPECT_EQ(screen.bw[1], 0xF8);
  EXPECT_EQ(screen.red[100 + 1], 0xF0);
  EXPECT_EQ(screen.red[47999], 0x01);
  EXPECT_EQ(screen.bw[47999], 0x00);
}

}  // namespace
