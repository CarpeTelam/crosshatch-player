#include "UC8179ScanLut.h"

#include <cstring>

namespace GameDisplayLab {

namespace {

// UC8179_PLL_CODES spelled as the frame-period screen's labels and log lines print them.
constexpr const char* RATE_LABELS[UC8179_RATE_COUNT] = {"0x06", "0x05", "0x07"};

// The level select of phase A (data byte 0, bits 7-6), as Uc8179Driver.cpp's R23h layout reads it. Level 00 is GND on
// WW and KK and VCOM DC on the VCOM table.
enum Level : uint8_t { GND = 0, VDH = 1, VDL = 2 };

// The group's repeat count, 0x01 in every stock UC8179 group.
constexpr uint8_t REPEAT = 0x01;

// One table: its prefix, then the group `[level << 6, frames, 0, 0, 0, REPEAT]`, then zeros to UC8179_TABLE_BYTES.
void writeTable(uint8_t* table, const uint8_t prefix, const uint8_t frames, const Level level) {
  memset(table, 0, UC8179_TABLE_BYTES);
  table[0] = prefix;
  table[1] = static_cast<uint8_t>(level << 6);
  table[2] = frames;  // TA, an 8-bit count
  table[6] = REPEAT;
}

}  // namespace

const char* uc8179RateLabel(const uint8_t index) { return index < UC8179_RATE_COUNT ? RATE_LABELS[index] : nullptr; }

bool uc8179PllCode(const uint8_t index, uint8_t& code) {
  if (index >= UC8179_RATE_COUNT) return false;
  code = UC8179_PLL_CODES[index];
  return true;
}

const char* uc8179RowLabel(const uint8_t row, const bool swap) {
  // Static strings for the four (row, swap) pairs; test_uc8179_scan_lut.cpp pins them against the tables' levels.
  switch (row) {
    case 1:
      return swap ? "KW VDH" : "KW VDL";
    case 2:
      return swap ? "WK VDL" : "WK VDH";
    default:
      return nullptr;  // only rows 1 and 2 are drive rows
  }
}

void buildUc8179ScanTables(uint8_t* out, const uint8_t frames, const bool swap) {
  const Level kw = swap ? VDH : VDL;
  const Level wk = swap ? VDL : VDH;
  // D9: VCOM, WW and KK keep the scan's length at level 00 (GND; VCOM DC on VCOM), so every table runs the same frames.
  constexpr uint8_t PREFIX = 0x20;
  const Level levels[UC8179_TABLES] = {GND, GND, kw, wk, GND};  // VCOM, WW, KW, WK, KK
  for (size_t t = 0; t < UC8179_TABLES; ++t) {
    writeTable(out + t * UC8179_TABLE_BYTES, static_cast<uint8_t>(PREFIX + t), frames, levels[t]);
  }
}

void encodeUc8179Planes(const uint8_t* black, const uint8_t* white, uint8_t* oldPlane, uint8_t* newPlane,
                        const size_t bytes) {
  for (size_t i = 0; i < bytes; ++i) {
    oldPlane[i] = static_cast<uint8_t>(~white[i]);
    newPlane[i] = static_cast<uint8_t>(~black[i]);
  }
}

}  // namespace GameDisplayLab
