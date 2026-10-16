// The display lab's SSD1677 scan LUT (lib/GameDisplayLab/Ssd1677ScanLut): byte-exact layout, TP placement, frame-rate
// bytes, the stock voltage tail, the row codes, and the plane bytes that select each row.

#include <gtest/gtest.h>

#include <cstdint>

#include "Ssd1677ScanLut.h"

namespace {

using namespace GameDisplayLab;

struct Lut {
  uint8_t bytes[LUT_BYTES];
};

Lut build(const uint8_t tp, const uint8_t rate, const VsCode row1, const VsCode row2) {
  Lut lut;
  // Poison first: the builder must write every byte.
  for (uint8_t& b : lut.bytes) b = 0xA5;
  buildOneFrameLut(lut.bytes, tp, rate, row1, row2);
  return lut;
}

TEST(Ssd1677ScanLut, LayoutConstants) {
  EXPECT_EQ(LUT_BYTES, 112u);
  EXPECT_EQ(ROW_BYTES, 10u);
  EXPECT_EQ(TP_BASE, 50u);
  EXPECT_EQ(FRAME_RATE_BASE, 100u);
  EXPECT_EQ(VOLTAGE_BASE, 105u);
  EXPECT_EQ(VOLTAGE_BASE + VOLTAGE_BYTES, 110u);
}

TEST(Ssd1677ScanLut, RowsOneAndTwoPhaseAOnly) {
  const Lut lut = build(10, 0x22, VsCode::Vsl, VsCode::Vsh1);
  // Row 0 (hold), row 3 and row 4 (VCOM): all VSS.
  for (unsigned i = 0; i < ROW_BYTES; ++i) {
    EXPECT_EQ(lut.bytes[0 * ROW_BYTES + i], 0x00) << "row 0 byte " << i;
    EXPECT_EQ(lut.bytes[3 * ROW_BYTES + i], 0x00) << "row 3 byte " << i;
    EXPECT_EQ(lut.bytes[4 * ROW_BYTES + i], 0x00) << "row 4 byte " << i;
  }
  // Row 1: phase A of group 0 is VSL (10) in the top two bits; nothing else.
  EXPECT_EQ(lut.bytes[1 * ROW_BYTES + 0], 0x80);
  // Row 2: VSH1 (01) in the top two bits.
  EXPECT_EQ(lut.bytes[2 * ROW_BYTES + 0], 0x40);
  for (unsigned i = 1; i < ROW_BYTES; ++i) {
    EXPECT_EQ(lut.bytes[1 * ROW_BYTES + i], 0x00) << "row 1 byte " << i;
    EXPECT_EQ(lut.bytes[2 * ROW_BYTES + i], 0x00) << "row 2 byte " << i;
  }
}

TEST(Ssd1677ScanLut, EveryVoltageCodeLandsInPhaseA) {
  EXPECT_EQ(build(1, 0x22, VsCode::Vss, VsCode::Vss).bytes[ROW_BYTES], 0x00);
  EXPECT_EQ(build(1, 0x22, VsCode::Vsh1, VsCode::Vss).bytes[ROW_BYTES], 0x40);
  EXPECT_EQ(build(1, 0x22, VsCode::Vsl, VsCode::Vss).bytes[ROW_BYTES], 0x80);
  EXPECT_EQ(build(1, 0x22, VsCode::Vsh2, VsCode::Vss).bytes[ROW_BYTES], 0xC0);
  EXPECT_EQ(build(1, 0x22, VsCode::Vss, VsCode::Vsh2).bytes[2 * ROW_BYTES], 0xC0);
}

TEST(Ssd1677ScanLut, TpGoesToGroupZeroPhaseAOnly) {
  const Lut lut = build(100, 0x22, VsCode::Vsl, VsCode::Vsh1);
  EXPECT_EQ(lut.bytes[TP_BASE], 100);
  // TP B, C, D and RP of group 0, and all of groups 1-9.
  for (size_t i = TP_BASE + 1; i < FRAME_RATE_BASE; ++i) EXPECT_EQ(lut.bytes[i], 0x00) << "byte " << i;
  EXPECT_EQ(build(1, 0x22, VsCode::Vsl, VsCode::Vsh1).bytes[TP_BASE], 1);
  EXPECT_EQ(build(255, 0x22, VsCode::Vsl, VsCode::Vsh1).bytes[TP_BASE], 255);
}

TEST(Ssd1677ScanLut, FrameRateFillsAllFiveBytes) {
  const Lut lut = build(10, 0x44, VsCode::Vsl, VsCode::Vsh1);
  for (size_t i = 0; i < FRAME_RATE_BYTES; ++i) EXPECT_EQ(lut.bytes[FRAME_RATE_BASE + i], 0x44) << "byte " << i;
  EXPECT_EQ(build(10, 0x11, VsCode::Vsl, VsCode::Vsh1).bytes[FRAME_RATE_BASE + 4], 0x11);
}

TEST(Ssd1677ScanLut, VoltageTailIsStockAndReservedIsZero) {
  const Lut lut = build(10, 0x22, VsCode::Vsl, VsCode::Vsh1);
  EXPECT_EQ(lut.bytes[105], 0x17);  // VGH
  EXPECT_EQ(lut.bytes[106], 0x41);  // VSH1
  EXPECT_EQ(lut.bytes[107], 0xA8);  // VSH2
  EXPECT_EQ(lut.bytes[108], 0x32);  // VSL
  EXPECT_EQ(lut.bytes[109], 0x30);  // VCOM
  EXPECT_EQ(lut.bytes[110], 0x00);
  EXPECT_EQ(lut.bytes[111], 0x00);
  // The tail never depends on the arguments.
  const Lut other = build(255, 0xFF, VsCode::Vsh2, VsCode::Vsh2);
  for (size_t i = VOLTAGE_BASE; i < LUT_BYTES; ++i) EXPECT_EQ(other.bytes[i], lut.bytes[i]) << "byte " << i;
}

TEST(Ssd1677ScanLut, PlaneFillsSelectEachRow) {
  const PlaneFill hold = planeFillFor(LutRow::Hold);
  EXPECT_EQ(hold.bw, 0x00);
  EXPECT_EQ(hold.red, 0x00);
  // Row 1 is (RED, BW) = 01: the BW plane set.
  const PlaneFill row1 = planeFillFor(LutRow::Row1);
  EXPECT_EQ(row1.bw, 0xFF);
  EXPECT_EQ(row1.red, 0x00);
  // Row 2 is 10: the RED plane set.
  const PlaneFill row2 = planeFillFor(LutRow::Row2);
  EXPECT_EQ(row2.bw, 0x00);
  EXPECT_EQ(row2.red, 0xFF);
}

TEST(Ssd1677ScanLut, ExperimentTables) {
  EXPECT_EQ(sizeof(FRAME_PERIOD_TPS), 3u);
  EXPECT_EQ(sizeof(FRAME_PERIOD_RATES), 3u);
  EXPECT_EQ(FRAME_PERIOD_SCANS, 9u);
  EXPECT_EQ(FRAME_PERIOD_TPS[0], 1);
  EXPECT_EQ(FRAME_PERIOD_TPS[1], 10);
  EXPECT_EQ(FRAME_PERIOD_TPS[2], 100);
  EXPECT_EQ(FRAME_PERIOD_RATES[0], 0x22);
  EXPECT_EQ(FRAME_PERIOD_RATES[1], 0x11);
  EXPECT_EQ(FRAME_PERIOD_RATES[2], 0x44);
  EXPECT_EQ(POLARITY_TP, 10);
  EXPECT_EQ(POLARITY_ROW1, VsCode::Vsl);
  EXPECT_EQ(POLARITY_ROW2, VsCode::Vsh1);
}

TEST(Ssd1677ScanLut, VsNames) {
  EXPECT_STREQ(vsName(VsCode::Vss), "VSS");
  EXPECT_STREQ(vsName(VsCode::Vsh1), "VSH1");
  EXPECT_STREQ(vsName(VsCode::Vsl), "VSL");
  EXPECT_STREQ(vsName(VsCode::Vsh2), "VSH2");
}

}  // namespace
