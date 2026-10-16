#pragma once

#include <cstddef>
#include <cstdint>

#include "MaskPlanes.h"
#include "Ssd1677ScanLut.h"

// The two experiments' request layout, controller-neutral and pure, so the host suite pins what the screens send
// (test/game_display_lab/test_ssd1677_encoding.cpp checks it against Part A's framePeriodScan and planeFillFor).
namespace GameDisplayLab {

// Scan `i` of the frame-period experiment over a backend with `rateCount` rates (> 0): tp outer, rate inner. The TP,
// and the index into the backend's rate list.
constexpr uint8_t framePeriodTp(const size_t i, const uint8_t rateCount) { return FRAME_PERIOD_TPS[i / rateCount]; }
constexpr uint8_t framePeriodRate(const size_t i, const uint8_t rateCount) {
  return static_cast<uint8_t>(i % rateCount);
}

// The polarity experiment's rows: row 1 drives every pixel through the white mask (the BW-plane row, LUT1, on the
// SSD1677), row 2 through the black mask (the RED-plane row, LUT2).
constexpr size_t POLARITY_ROWS = 2;
inline constexpr MaskPlanes::Mask POLARITY_ROW_MASKS[POLARITY_ROWS] = {MaskPlanes::Mask::White,
                                                                       MaskPlanes::Mask::Black};

}  // namespace GameDisplayLab
