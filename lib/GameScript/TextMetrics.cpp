#include "TextMetrics.h"

#include <Utf8.h>

#include <cstring>

namespace GameScript {

namespace {

// 12.4 fixed point to whole pixels, rounding to nearest (EpdFontData.h's fp4::toPixel).
int64_t toPixel(const uint16_t fp4) { return (static_cast<int64_t>(fp4) + 8) >> 4; }

}  // namespace

uint16_t AdvanceTable::advanceOf(const uint32_t codepoint) const {
  // The last range starting at or before the code point is the only candidate.
  uint32_t low = 0;
  uint32_t high = rangeCount;
  while (low < high) {
    const uint32_t mid = low + (high - low) / 2;
    if (ranges[mid].first <= codepoint) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  if (low == 0) return fallback;
  const AdvanceRange& range = ranges[low - 1];
  if (codepoint > range.last) return fallback;
  uint16_t advance = 0;
  std::memcpy(&advance, advances + static_cast<size_t>(range.index + (codepoint - range.first)) * stride,
              sizeof(advance));
  return advance;
}

int64_t TextMetrics::width(const char* text, const TextSize size) const {
  const AdvanceTable& table = tables[static_cast<size_t>(size)];
  const auto* cursor = reinterpret_cast<const unsigned char*>(text);
  int64_t pixels = 0;
  while (const uint32_t codepoint = utf8NextCodepoint(&cursor)) pixels += toPixel(table.advanceOf(codepoint));
  return pixels;
}

}  // namespace GameScript
