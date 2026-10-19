#pragma once

#include <cstddef>
#include <cstdint>

#include "GrayPreset.h"

// The UltraChip stock-path bench's pure parts (story dlab-u1) and the lab's menu gate (D10): pure C++, pinned by
// test/game_display_lab/test_stock_bench.cpp. Every frame and plane here is a full-screen 1-bpp bitmap in the
// framebuffer's byte order (as GameDisplayLab::MaskPlanes: row-major, (width + 7) / 8 bytes per row, the leftmost pixel
// of each byte in its most significant bit), in physical panel coordinates. Bits past the width in a row's last byte
// are always written clear.
namespace GameDisplayLab {

// Which lab menu an X4 Pro gets (the display-lab epic, D10). A controller with a scan backend gets the scan menu (Frame
// period, Polarity, Transition test, Tuning, Save preset, Exit); an UltraChip one (UC8179 or UC8279) with a backend
// also gets the stock bench's two rows (Transition timing, Gray ramp) before Exit, so the stock transition and the OEM
// ramp stay beside the lab to compare against; an UltraChip one without a backend gets the stock bench's menu alone
// (Transition timing, Gray ramp, Exit); any other controller gets no lab.
enum class LabMenu : uint8_t { None, Scan, ScanWithStockRows, StockBench };

constexpr LabMenu labMenuFor(const bool ultraChip, const bool hasScanBackend) {
  if (hasScanBackend) return ultraChip ? LabMenu::ScanWithStockRows : LabMenu::Scan;
  return ultraChip ? LabMenu::StockBench : LabMenu::None;
}

// Every row any lab menu shows, in menu order.
enum class LabRow : uint8_t {
  FramePeriod,
  Polarity,
  TransitionTest,
  GrayViewer,
  Tuning,
  SavePreset,
  TransitionTiming,
  GrayRamp,
  Exit
};
constexpr size_t LAB_ROW_COUNT = static_cast<size_t>(LabRow::Exit) + 1;

// The rows of `menu`, in order, into `out`; returns how many. Scan: Frame period, Polarity, Transition test, Gray
// viewer, Tuning, Save preset, Exit; ScanWithStockRows: those with Transition timing and Gray ramp before Exit;
// StockBench: Transition timing, Gray ramp, Exit; None: none.
constexpr size_t labMenuRows(const LabMenu menu, LabRow (&out)[LAB_ROW_COUNT]) {
  size_t count = 0;
  const bool scanRows = menu == LabMenu::Scan || menu == LabMenu::ScanWithStockRows;
  const bool stockRows = menu == LabMenu::ScanWithStockRows || menu == LabMenu::StockBench;
  if (scanRows) {
    out[count++] = LabRow::FramePeriod;
    out[count++] = LabRow::Polarity;
    out[count++] = LabRow::TransitionTest;
    out[count++] = LabRow::GrayViewer;
    out[count++] = LabRow::Tuning;
    out[count++] = LabRow::SavePreset;
  }
  // D10: the stock bench's two rows, on an UltraChip controller with or without a scan backend.
  if (stockRows) {
    out[count++] = LabRow::TransitionTiming;
    out[count++] = LabRow::GrayRamp;
  }
  if (menu != LabMenu::None) out[count++] = LabRow::Exit;
  return count;
}

// The transition test frame: a checker of 4 x 2 blocks, each width / 4 by height / 2 (the last column and the last row
// of blocks take the remainder). Block (c, r) is white (bit set) when (c + r) is even, the other way round when
// `inverted`. `frame` holds MaskPlanes::bytesFor(width, height) bytes ((width + 7) / 8 bytes per row).
void fillChecker(uint8_t* frame, uint16_t width, uint16_t height, bool inverted);

// The gray ramp's four bands are the gray engine's four named shades (GrayPreset.h: K, D, L, W, darkest first, on
// levels 0, 2, 4, 6 of seven).
// The panel axis the ramp's bands run along: X (bands are vertical stripes) or Y (horizontal stripes).
enum class BandAxis : uint8_t { X, Y };

// The shade at physical pixel (x, y) of the gray ramp: four equal bands K, D, L, W along `axis`,
// starting at its origin (x = 0 or y = 0), or at its far end when `reversed`. The W band always takes the
// remainder: it lies at the far end of the axis, or at its origin when `reversed`. A coordinate past the panel counts
// as its last pixel.
Shade rampShadeAt(uint16_t x, uint16_t y, uint16_t width, uint16_t height, BandAxis axis, bool reversed);

// The gray ramp in the facade's overlay encoding (freeink-sdk/docs/grayscale-capabilities.md): the base frame (bit set
// = white) is white only in the White band and black elsewhere; the masks in (LSB, MSB) are (1, 1) dark gray,
// (0, 1) light gray, and (0, 0) for black and white. Each buffer holds MaskPlanes::bytesFor(width, height) bytes.
void fillGrayRamp(uint8_t* base, uint8_t* lsb, uint8_t* msb, uint16_t width, uint16_t height, BandAxis axis,
                  bool reversed);

// The most timings summarizeTimings reads; the transition run has ten.
constexpr size_t MAX_TIMINGS = 16;

struct TimingSummary {
  uint32_t min;
  uint32_t median;
  uint32_t max;
  size_t count;  // the values summarized: the count given, capped at MAX_TIMINGS
};

// Min, median and max of the first min(count, MAX_TIMINGS) values. The median of an even count is the mean of the two
// middle values, rounded down. A count of 0 (or a null `values`) gives all zeros.
TimingSummary summarizeTimings(const uint32_t* values, size_t count);

// A transition taking at least this many times the stock FAST refresh is taken for a fall-back (an unmeasured guess;
// docs/crosshatch/display-lab.md).
constexpr uint32_t FELL_BACK_FACTOR = 2;

// Whether a displayGrayscaleBase(FAST) call that took `us` fell back to a plain refresh instead of the non-flashing
// transition: always when the transition was not armed (the driver then runs a plain refresh), otherwise when it took
// FELL_BACK_FACTOR times the reference FAST refresh `referenceUs` or more (an armed driver falls back only to a full
// GC). A zero reference judges no armed call a fall-back. Overflow-safe for every argument.
bool transitionFellBack(uint32_t us, uint32_t referenceUs, bool armed);

}  // namespace GameDisplayLab
