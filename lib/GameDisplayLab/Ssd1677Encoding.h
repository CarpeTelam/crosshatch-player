#pragma once

#include <cstddef>
#include <cstdint>

#include "Ssd1677ScanLut.h"

// The SSD1677 scan backend's pure parts (the display-lab epic, D1 to D3): its frame-rate list, the LUT for one
// ScanRequest, and the two RAM planes from the two drive masks. Pure C++, pinned by
// test/game_display_lab/test_ssd1677_encoding.cpp against Part A's bytes; the facade calls live in
// src/activities/games/display_lab/Ssd1677ScanBackend.
namespace GameDisplayLab {

// The backend's frame-rate list is FRAME_PERIOD_RATES in order, so rate index 0 is 0x22, the byte Part A's polarity
// scan used.
constexpr uint8_t SSD1677_RATE_COUNT = static_cast<uint8_t>(sizeof(FRAME_PERIOD_RATES));

// The rate's label ("0x22", "0x11", "0x44"), or nullptr when `index` is not below SSD1677_RATE_COUNT.
const char* ssd1677RateLabel(uint8_t index);

// The codes of LUT row 1 (the BW-plane row, white mask) and row 2 (the RED-plane row, black mask).
struct Ssd1677RowCodes {
  VsCode row1;
  VsCode row2;
};
// POLARITY_ROW1 on row 1 and POLARITY_ROW2 on row 2, or the two swapped when `swap`.
Ssd1677RowCodes ssd1677RowCodes(bool swap);

// The name of drive row `row` (1 or 2) under ssd1677RowCodes(swap): its LUT and code ("LUT1 VSL", "LUT2 VSH1", or
// "LUT1 VSH1", "LUT2 VSL" when `swap`); nullptr for any other row.
const char* ssd1677RowLabel(uint8_t row, bool swap);

// Writes the scan LUT (LUT_BYTES) for `frames` frames at rate `rateIndex` into `out`: buildOneFrameLut with
// FRAME_PERIOD_RATES[rateIndex] and ssd1677RowCodes(swap). False, with `out` untouched, when `rateIndex` is not below
// SSD1677_RATE_COUNT.
bool buildSsd1677ScanLut(uint8_t* out, uint8_t frames, uint8_t rateIndex, bool swap);

// The two RAM planes for one scan, `bytes` each (D2): the white mask is the BW plane (a set bit selects LUT1) and the
// black mask the RED plane (LUT2); a pixel in neither selects LUT0 and holds, one in both LUT3, which the scan LUT also
// leaves all VSS.
void encodeSsd1677Planes(const uint8_t* black, const uint8_t* white, uint8_t* bw, uint8_t* red, size_t bytes);

}  // namespace GameDisplayLab
