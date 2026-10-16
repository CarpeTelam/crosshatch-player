#include "Ssd1677Encoding.h"

#include <cstring>

namespace GameDisplayLab {

namespace {

// FRAME_PERIOD_RATES spelled as the frame-period screen's labels and log lines print them.
constexpr const char* RATE_LABELS[SSD1677_RATE_COUNT] = {"0x22", "0x11", "0x44"};

}  // namespace

const char* ssd1677RateLabel(const uint8_t index) { return index < SSD1677_RATE_COUNT ? RATE_LABELS[index] : nullptr; }

Ssd1677RowCodes ssd1677RowCodes(const bool swap) {
  if (swap) return {POLARITY_ROW2, POLARITY_ROW1};
  return {POLARITY_ROW1, POLARITY_ROW2};
}

const char* ssd1677RowLabel(const uint8_t row, const bool swap) {
  // Static strings for the four (row, swap) pairs; test_ssd1677_encoding.cpp pins them against ssd1677RowCodes.
  switch (row) {
    case 1:
      return swap ? "LUT1 VSH1" : "LUT1 VSL";
    case 2:
      return swap ? "LUT2 VSL" : "LUT2 VSH1";
    default:
      return nullptr;  // only rows 1 and 2 are drive rows
  }
}

bool buildSsd1677ScanLut(uint8_t* out, const uint8_t frames, const uint8_t rateIndex, const bool swap) {
  if (rateIndex >= SSD1677_RATE_COUNT) return false;
  const Ssd1677RowCodes codes = ssd1677RowCodes(swap);
  buildOneFrameLut(out, frames, FRAME_PERIOD_RATES[rateIndex], codes.row1, codes.row2);
  return true;
}

void encodeSsd1677Planes(const uint8_t* black, const uint8_t* white, uint8_t* bw, uint8_t* red, const size_t bytes) {
  memcpy(bw, white, bytes);
  memcpy(red, black, bytes);
}

}  // namespace GameDisplayLab
