#pragma once

#include <GameIcons.h>

#include <cstddef>
#include <cstdint>

#include "BlitClip.h"

// A library icon as pixels, pure so it is host-tested: the bitmap and scale a
// drawn size uses, and the icon's ink as horizontal runs clipped to a canvas.
// drawGameIconAt (GameIconDraw.h) fills the runs, for ch.gfx.icon's replay and
// for screens alike. The bitmaps are packed (GameIcons.h): a draw decodes one
// drawn row at a time into an 8-byte buffer on the stack, with no heap.
namespace GameIconBlit {

// The drawn sizes in pixels (GameIcons.h's, which the bindings charge against the
// frame's pixel budget too).
using GameIcons::DRAWN_PIXELS;

// One icon's bitmap at one drawn size: `pixels` square, packed (GameIcons.h's
// layout: a length, then PackBits that decode to the drawn rows), each bitmap pixel
// drawn as a `scale` x `scale` block.
struct Source {
  GameIcons::PackedBitmap bitmap;
  int pixels = 0;
  int scale = 1;
};

// The bitmap that draws icon `index` of GameIcons::ICONS in `weight` at
// `drawnPixels`; false (out untouched) for an index past the library, a weight
// past WEIGHT_COUNT, or a size not in DRAWN_PIXELS.
inline bool sourceFor(const size_t index, const int drawnPixels, const GameIcons::Weight weight, Source& out) {
  const auto w = static_cast<size_t>(weight);
  if (index >= GameIcons::ICON_COUNT || w >= GameIcons::WEIGHT_COUNT) return false;
  const GameIcons::Icon& icon = GameIcons::ICONS[index];
  if (drawnPixels == DRAWN_PIXELS[0]) {
    out = Source{icon.small[w], GameIcons::SMALL_PIXELS, 1};
  } else if (drawnPixels == DRAWN_PIXELS[1]) {
    out = Source{icon.medium[w], GameIcons::MEDIUM_PIXELS, 1};
  } else if (drawnPixels == DRAWN_PIXELS[2]) {
    out = Source{icon.medium[w], GameIcons::MEDIUM_PIXELS, 2};
  } else {
    return false;
  }
  return true;
}

// Whether the bitmap pixel drawn at (x, y), both in [0, pixels), is ink: bit 0 = ink,
// MSB first, in the decoded row y. False for a malformed bitmap. It decodes the rows
// above y, so it suits tests, not a draw; inkRuns decodes each row once.
inline bool inkAt(const GameIcons::PackedBitmap bitmap, const int pixels, const int x, const int y) {
  if (!bitmap.data || pixels <= 0 || pixels % 8 != 0 || pixels / 8 > static_cast<int>(GameIcons::MAX_ROW_BYTES) ||
      x < 0 || y < 0 || x >= pixels || y >= pixels) {
    return false;
  }
  uint8_t row[GameIcons::MAX_ROW_BYTES];
  GameIcons::PackedReader reader(bitmap);
  for (int i = 0; i <= y; ++i) {
    if (!reader.row(row, static_cast<size_t>(pixels / 8))) return false;
  }
  return ((row[x >> 3] >> (7 - (x & 7))) & 1) == 0;
}

// Calls fn(y, x, w) for each horizontal run of ink of the icon drawn with its
// top-left at (left, top), clipped to the canvas [0, width) x [0, height).
// Adjacent ink pixels make one run. Only the rows and columns on the canvas are
// drawn, so an icon wholly off it costs nothing, and the rows above the canvas are
// decoded, not drawn (at most `pixels`). A malformed row, which only a generator bug
// makes (GameIcons.h's static_assert rejects it at build time), ends the icon: it and
// the rows after it draw nothing, and no byte past the bitmap's length is read.
template <typename Fn>
void inkRuns(const Source& source, const int32_t left, const int32_t top, const int32_t width, const int32_t height,
             Fn&& fn) {
  if (!source.bitmap.data || source.pixels <= 0 || source.scale <= 0) return;
  // A row of the bitmap is whole bytes and fits the decode buffer.
  if (source.pixels % 8 != 0 || source.pixels / 8 > static_cast<int>(GameIcons::MAX_ROW_BYTES)) return;
  const int64_t side = static_cast<int64_t>(source.pixels) * source.scale;
  // The visible part, in drawn pixels from the icon's top-left.
  const BlitClip::Visible clip = BlitClip::visible(left, top, width, height, side, side);
  if (clip.empty()) return;
  const int64_t firstX = clip.firstX;
  const int64_t firstY = clip.firstY;
  const int64_t endX = clip.endX;
  const int64_t endY = clip.endY;
  uint8_t row[GameIcons::MAX_ROW_BYTES];
  GameIcons::PackedReader reader(source.bitmap);
  const auto rowBytes = static_cast<size_t>(source.pixels / 8);
  int decoded = -1;  // the bitmap row in `row`
  for (int64_t dy = firstY; dy < endY; ++dy) {
    const int bitmapY = static_cast<int>(dy / source.scale);
    while (decoded < bitmapY) {
      if (!reader.row(row, rowBytes)) return;
      ++decoded;
    }
    int64_t runStart = -1;
    for (int64_t dx = firstX; dx < endX; ++dx) {
      const int bitmapX = static_cast<int>(dx / source.scale);
      const bool ink = ((row[bitmapX >> 3] >> (7 - (bitmapX & 7))) & 1) == 0;
      if (ink && runStart < 0) {
        runStart = dx;
      } else if (!ink && runStart >= 0) {
        fn(static_cast<int32_t>(top + dy), static_cast<int32_t>(left + runStart), static_cast<int32_t>(dx - runStart));
        runStart = -1;
      }
    }
    if (runStart >= 0) {
      fn(static_cast<int32_t>(top + dy), static_cast<int32_t>(left + runStart), static_cast<int32_t>(endX - runStart));
    }
  }
}

}  // namespace GameIconBlit
