#include "StockBench.h"

#include <algorithm>
#include <cstring>

namespace GameDisplayLab {

namespace {

size_t rowBytesFor(const uint16_t width) { return (static_cast<size_t>(width) + 7u) / 8u; }

// The index (0 .. parts - 1) of the part position `p` falls in when `length` is cut into `parts` equal parts, the last
// one taking the remainder. A length shorter than `parts` gives one pixel per part, the rest to the last.
uint16_t partOf(const uint16_t p, const uint16_t length, const uint16_t parts) {
  const uint16_t size = static_cast<uint16_t>(length / parts);
  const uint16_t part = size > 0 ? static_cast<uint16_t>(p / size) : p;
  return std::min<uint16_t>(part, static_cast<uint16_t>(parts - 1));
}

// Sets bit `x` of a row (MSB first).
void setBit(uint8_t* row, const uint16_t x) { row[x >> 3] |= static_cast<uint8_t>(0x80u >> (x & 7)); }

}  // namespace

void fillChecker(uint8_t* frame, const uint16_t width, const uint16_t height, const bool inverted) {
  if (frame == nullptr) return;
  const size_t stride = rowBytesFor(width);
  memset(frame, 0, stride * height);
  for (uint16_t y = 0; y < height; ++y) {
    uint8_t* const row = frame + static_cast<size_t>(y) * stride;
    const uint16_t r = partOf(y, height, 2);
    for (uint16_t x = 0; x < width; ++x) {
      const uint16_t c = partOf(x, width, 4);
      const bool white = ((c + r) % 2 == 0) != inverted;
      if (white) setBit(row, x);
    }
  }
}

Shade rampShadeAt(const uint16_t x, const uint16_t y, const uint16_t width, const uint16_t height, const BandAxis axis,
                  const bool reversed) {
  const uint16_t length = axis == BandAxis::X ? width : height;
  if (length == 0) return Shade::K;
  uint16_t p = axis == BandAxis::X ? x : y;
  p = std::min<uint16_t>(p, static_cast<uint16_t>(length - 1));
  if (reversed) p = static_cast<uint16_t>(length - 1 - p);
  // The OEM ramp's four bands keep the engine's four named shades (levels 0, 2, 4, 6 since story dlab-g2).
  constexpr Shade BANDS[] = {Shade::K, Shade::D, Shade::L, Shade::W};
  constexpr uint16_t BAND_COUNT = sizeof(BANDS) / sizeof(BANDS[0]);
  return BANDS[partOf(p, length, BAND_COUNT)];
}

void fillGrayRamp(uint8_t* base, uint8_t* lsb, uint8_t* msb, const uint16_t width, const uint16_t height,
                  const BandAxis axis, const bool reversed) {
  if (base == nullptr || lsb == nullptr || msb == nullptr) return;
  const size_t stride = rowBytesFor(width);
  const size_t bytes = stride * height;
  memset(base, 0, bytes);
  memset(lsb, 0, bytes);
  memset(msb, 0, bytes);
  for (uint16_t y = 0; y < height; ++y) {
    const size_t offset = static_cast<size_t>(y) * stride;
    for (uint16_t x = 0; x < width; ++x) {
      switch (rampShadeAt(x, y, width, height, axis, reversed)) {
        case Shade::K:
          break;  // base black, masks (0, 0)
        case Shade::D:
          setBit(lsb + offset, x);
          setBit(msb + offset, x);
          break;
        case Shade::L:
          setBit(msb + offset, x);
          break;
        case Shade::W:
          setBit(base + offset, x);
          break;
        case Shade::TRANSPARENT:
          break;  // never a ramp band
      }
    }
  }
}

TimingSummary summarizeTimings(const uint32_t* values, const size_t count) {
  TimingSummary summary{0, 0, 0, 0};
  const size_t n = values != nullptr ? std::min(count, MAX_TIMINGS) : 0;
  if (n == 0) return summary;
  uint32_t sorted[MAX_TIMINGS];
  std::copy(values, values + n, sorted);
  std::sort(sorted, sorted + n);
  summary.min = sorted[0];
  summary.max = sorted[n - 1];
  if (n % 2 == 1) {
    summary.median = sorted[n / 2];
  } else {
    const uint32_t low = sorted[n / 2 - 1];
    const uint32_t high = sorted[n / 2];
    summary.median = low + (high - low) / 2;  // sorted, so high >= low: no overflow, rounded down
  }
  summary.count = n;
  return summary;
}

bool transitionFellBack(const uint32_t us, const uint32_t referenceUs, const bool armed) {
  if (!armed) return true;
  // 64-bit product: FELL_BACK_FACTOR * referenceUs cannot wrap.
  return referenceUs > 0 && static_cast<uint64_t>(us) >= static_cast<uint64_t>(FELL_BACK_FACTOR) * referenceUs;
}

}  // namespace GameDisplayLab
