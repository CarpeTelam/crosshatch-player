#pragma once

#include <cstddef>
#include <cstdint>

#include "DisplayList.h"

namespace GameScript {

// Text measurement for ch.text_width (AD-7): per-size advance tables that the
// match passes in at VM start, so measuring is pure and works in any callback.
// The tables are plain data in the shape of the built-in fonts (EpdFontData), so
// the host hands them over without copying glyphs: FrameReplay supplies them for
// the three sizes it draws (entry 11); until then a fixed-advance stand-in.

// Code points first..last have glyphs index..index + (last - first), like
// EpdUnicodeInterval.
struct AdvanceRange {
  uint32_t first = 0;
  uint32_t last = 0;
  uint32_t index = 0;
};

// One size's advances, in 12.4 fixed point (1/16 px) like EpdGlyph::advanceX.
struct AdvanceTable {
  // Sorted by first, disjoint.
  const AdvanceRange* ranges = nullptr;
  uint32_t rangeCount = 0;
  // Glyph i's advance is the native uint16_t at advances + i * stride, so a table
  // can point into an array of larger glyph records (stride sizeof(EpdGlyph)).
  const uint8_t* advances = nullptr;
  uint16_t stride = sizeof(uint16_t);
  // The advance of a code point in no range: the font's replacement glyph's.
  uint16_t fallback = 0;

  // The 12.4 advance of one code point.
  uint16_t advanceOf(uint32_t codepoint) const;
};

// The fixed pixel advances of the stand-in, per size.
inline constexpr uint16_t STAND_IN_ADVANCE_PX[] = {8, 10, 14};

struct TextMetrics {
  AdvanceTable tables[3];  // indexed by TextSize

  // The width in pixels of `text` drawn at `size`: each code point's advance
  // rounded to whole pixels, summed (no kerning). Decodes UTF-8 as the renderer
  // does (a malformed sequence is U+FFFD) and stops at the first NUL, where
  // drawing stops too. `text` must be NUL-terminated (Lua strings are).
  int64_t width(const char* text, TextSize size) const;

  // Deterministic metrics for host tests and builds whose replay has no font
  // tables yet: every code point advances STAND_IN_ADVANCE_PX of its size.
  static constexpr TextMetrics standIn() {
    TextMetrics metrics;
    for (size_t i = 0; i < 3; ++i) metrics.tables[i].fallback = static_cast<uint16_t>(STAND_IN_ADVANCE_PX[i] << 4);
    return metrics;
  }
};

}  // namespace GameScript
