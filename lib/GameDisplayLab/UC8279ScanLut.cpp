#include "UC8279ScanLut.h"

#include <cstring>

namespace GameDisplayLab {

namespace {

// UC8279_PLL_CODES spelled as the frame-period screen's labels and log lines print them.
constexpr const char* RATE_LABELS[UC8279_RATE_COUNT] = {"0x0E", "0x0D", "0x0F"};

// The phase-byte level field (the top two bits), as makeQualityBank's net-charge sign reads it.
enum Level : uint8_t { GND = 0, VSH = 1, VSL = 2 };

// One table: its prefix, then the group `[0x01, P1, P2, P3, P4, 0x01, 0x01]` with `frames` split over the four phases
// at `level` (D7), then zeros to UC8279_TABLE_BYTES.
void writeTable(uint8_t* table, const uint8_t prefix, const uint8_t frames, const Level level) {
  memset(table, 0, UC8279_TABLE_BYTES);
  table[0] = prefix;
  table[1] = 0x01;
  uint8_t left = frames;
  for (uint8_t phase = 0; phase < UC8279_PHASES; ++phase) {
    const uint8_t n = left < UC8279_PHASE_MAX_FRAMES ? left : UC8279_PHASE_MAX_FRAMES;
    // An unused phase is 0x00, level included.
    table[2 + phase] = n == 0 ? 0x00 : static_cast<uint8_t>(level << 6 | n);
    left = static_cast<uint8_t>(left - n);
  }
  table[6] = 0x01;
  table[7] = 0x01;
}

}  // namespace

const char* uc8279RateLabel(const uint8_t index) { return index < UC8279_RATE_COUNT ? RATE_LABELS[index] : nullptr; }

bool uc8279PllCode(const uint8_t index, uint8_t& code) {
  if (index >= UC8279_RATE_COUNT) return false;
  code = UC8279_PLL_CODES[index];
  return true;
}

const char* uc8279RowLabel(const uint8_t row, const bool swap) {
  // Static strings for the four (row, swap) pairs; test_uc8279_scan_lut.cpp pins them against the tables' levels.
  switch (row) {
    case 1:
      return swap ? "KW VSH" : "KW VSL";
    case 2:
      return swap ? "WK VSL" : "WK VSH";
    default:
      return nullptr;  // only rows 1 and 2 are drive rows
  }
}

bool buildUc8279ScanTables(uint8_t* out, const uint8_t frames, const bool swap) {
  if (frames > UC8279_MAX_FRAMES) return false;
  const Level kw = swap ? VSH : VSL;
  const Level wk = swap ? VSL : VSH;
  // D9: VCOM, WW and KK keep the scan's length at GND, so every table runs the same frames.
  constexpr uint8_t PREFIX = 0x20;
  const Level levels[UC8279_TABLES] = {GND, GND, kw, wk, GND};  // VCOM, WW, KW, WK, KK
  for (size_t t = 0; t < UC8279_TABLES; ++t) {
    writeTable(out + t * UC8279_TABLE_BYTES, static_cast<uint8_t>(PREFIX + t), frames, levels[t]);
  }
  return true;
}

void encodeUc8279Planes(const uint8_t* black, const uint8_t* white, uint8_t* oldPlane, uint8_t* newPlane,
                        const size_t bytes) {
  for (size_t i = 0; i < bytes; ++i) {
    oldPlane[i] = static_cast<uint8_t>(~white[i]);
    newPlane[i] = static_cast<uint8_t>(~black[i]);
  }
}

}  // namespace GameDisplayLab
