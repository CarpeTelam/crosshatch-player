#include "TextMetrics.h"

namespace GameScript {

bool AdvanceTable::find(const uint32_t codepoint, uint16_t& advance) const {
  // The last range starting at or before the code point is the only candidate.
  const auto rangeAt = [this](const uint32_t i) {
    AdvanceRange range;
    std::memcpy(&range, ranges + static_cast<size_t>(i) * rangeStride, sizeof(range));
    return range;
  };
  uint32_t low = 0;
  uint32_t high = rangeCount;
  while (low < high) {
    const uint32_t mid = low + (high - low) / 2;
    if (rangeAt(mid).first <= codepoint) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }
  if (low == 0) return false;
  const AdvanceRange range = rangeAt(low - 1);
  if (codepoint > range.last) return false;
  std::memcpy(&advance, advances + static_cast<size_t>(range.index + (codepoint - range.first)) * stride,
              sizeof(advance));
  return true;
}

uint16_t AdvanceTable::advanceOf(const uint32_t codepoint) const {
  uint16_t advance = fallback;
  find(codepoint, advance);
  return advance;
}

}  // namespace GameScript
