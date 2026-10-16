#include "Ssd1677ScanLut.h"

#include <cstring>

namespace GameDisplayLab {

namespace {

// Phase A is the top two bits of a row's group byte.
constexpr unsigned PHASE_A_SHIFT = 6;

constexpr size_t rowByte(const unsigned row, const unsigned group) { return row * ROW_BYTES + group; }

}  // namespace

void buildOneFrameLut(uint8_t* out, const uint8_t tp, const uint8_t frameRateByte, const VsCode row1,
                      const VsCode row2) {
  memset(out, 0, LUT_BYTES);
  // Rows 0, 3 and 4 stay all VSS: a pixel with both bits clear holds, and VCOM is not driven.
  out[rowByte(1, 0)] = static_cast<uint8_t>(static_cast<uint8_t>(row1) << PHASE_A_SHIFT);
  out[rowByte(2, 0)] = static_cast<uint8_t>(static_cast<uint8_t>(row2) << PHASE_A_SHIFT);
  // Group 0's phase A is the one phase; its B, C, D and RP, and every other group, are zero from the memset.
  out[TP_BASE] = tp;
  for (size_t i = 0; i < FRAME_RATE_BYTES; ++i) out[FRAME_RATE_BASE + i] = frameRateByte;
  for (size_t i = 0; i < VOLTAGE_BYTES; ++i) out[VOLTAGE_BASE + i] = STOCK_VOLTAGES[i];
}

PlaneFill planeFillFor(const LutRow row) {
  switch (row) {
    case LutRow::Hold:
      return {0x00, 0x00};
    case LutRow::Row1:
      return {0xFF, 0x00};  // BW bit set, RED bit clear: 01
    case LutRow::Row2:
      return {0x00, 0xFF};  // RED bit set, BW bit clear: 10
  }
  return {0x00, 0x00};
}

const char* vsName(const VsCode code) {
  switch (code) {
    case VsCode::Vss:
      return "VSS";
    case VsCode::Vsh1:
      return "VSH1";
    case VsCode::Vsl:
      return "VSL";
    case VsCode::Vsh2:
      return "VSH2";
  }
  return "?";
}

}  // namespace GameDisplayLab
