#pragma once

#include <Utf8.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "DisplayList.h"

namespace GameScript {

// Text measurement for ch.text_width (AD-7): per-size advance tables that the
// match passes in at VM start, so measuring is pure and works in any callback.
// The tables are plain data in the shape of the built-in fonts (EpdFontData), so
// the host hands them over without copying glyphs or intervals: FrameReplay
// points them at the three fonts it draws in, and draws each glyph at the pen
// position forEachGlyph gives, so drawn text is exactly as wide as measured.

// Code points first..last have glyphs index..index + (last - first), laid out
// like EpdUnicodeInterval (three native uint32_t).
struct AdvanceRange {
  uint32_t first = 0;
  uint32_t last = 0;
  uint32_t index = 0;
};

// One size's advances, in 12.4 fixed point (1/16 px) like EpdGlyph::advanceX.
struct AdvanceTable {
  // rangeCount records laid out like AdvanceRange, the i-th at ranges + i *
  // rangeStride, sorted by first and disjoint. Read by memcpy, so a table can
  // point straight at a font's EpdUnicodeInterval array.
  const uint8_t* ranges = nullptr;
  uint32_t rangeCount = 0;
  uint16_t rangeStride = sizeof(AdvanceRange);
  // Glyph i's advance is the native uint16_t at advances + i * stride, so a table
  // can point into an array of larger glyph records (stride sizeof(EpdGlyph)).
  const uint8_t* advances = nullptr;
  uint16_t stride = sizeof(uint16_t);
  // The advance of a code point in no range: the font's replacement glyph's.
  uint16_t fallback = 0;

  // The 12.4 advance of one code point.
  uint16_t advanceOf(uint32_t codepoint) const;
  // True, with its 12.4 advance, when the code point is in a range; false (the
  // font has no glyph for it) otherwise.
  bool find(uint32_t codepoint, uint16_t& advance) const;
};

// The fixed pixel advances of the stand-in, per size.
inline constexpr uint16_t STAND_IN_ADVANCE_PX[] = {8, 10, 14};

struct TextMetrics {
  AdvanceTable tables[3];  // indexed by TextSize

  // The width in pixels of `text` drawn at `size`: each code point's advance
  // rounded to whole pixels, summed (no kerning). Decodes UTF-8 as the renderer
  // does (a malformed sequence is U+FFFD) and stops at the first NUL, where
  // drawing stops too. `text` must be NUL-terminated (Lua strings are).
  int64_t width(const char* text, TextSize size) const {
    return forEachGlyph(text, size, [](uint32_t, int64_t, bool) {});
  }

  // Walks `text` as width() measures it, calling fn(codepoint, penX, known) for
  // each code point: penX is its pen position from the text's start, and known
  // is false when the size's font has no glyph for it (it then advances by the
  // table's fallback, the replacement glyph's). Returns the width. Replay draws
  // each glyph at penX, so drawn and measured widths are equal by construction.
  template <typename Fn>
  int64_t forEachGlyph(const char* text, const TextSize size, Fn&& fn) const {
    const AdvanceTable& table = tables[static_cast<size_t>(size)];
    const auto* cursor = reinterpret_cast<const unsigned char*>(text);
    int64_t pen = 0;
    while (const uint32_t codepoint = utf8NextCodepoint(&cursor)) {
      uint16_t advance = table.fallback;
      const bool known = table.find(codepoint, advance);
      fn(codepoint, pen, known);
      pen += toPixel(advance);
    }
    return pen;
  }

  // Where text of `width` pixels starts when its x is aligned by `align`: at x,
  // centred on x (half the width, rounded down, to its left), or ending at x.
  static constexpr int64_t alignedStart(const int64_t x, const int64_t width, const Align align) {
    switch (align) {
      case Align::Center:
        return x - width / 2;
      case Align::Right:
        return x - width;
      case Align::Left:
        break;
    }
    return x;
  }

  // 12.4 fixed point to whole pixels, rounding to nearest (EpdFontData.h's fp4::toPixel).
  static constexpr int64_t toPixel(const uint16_t fp4) { return (static_cast<int64_t>(fp4) + 8) >> 4; }

  // Deterministic metrics for host tests, and what replay keeps for a size whose
  // built-in font is missing (FrameReplay::loadFonts): every code point advances
  // STAND_IN_ADVANCE_PX of its size.
  static constexpr TextMetrics standIn() {
    TextMetrics metrics;
    for (size_t i = 0; i < 3; ++i) metrics.tables[i].fallback = static_cast<uint16_t>(STAND_IN_ADVANCE_PX[i] << 4);
    return metrics;
  }
};

}  // namespace GameScript
