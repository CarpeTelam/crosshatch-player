#pragma once

#include <GameIcons.h>

#include <cstddef>
#include <cstdint>

// A library icon as pixels, pure so it is host-tested: the bitmap and scale a
// drawn size uses, and the icon's ink as horizontal runs clipped to a canvas.
// drawGameIconAt (GameIconDraw.h) fills the runs, for ch.gfx.icon's replay and
// for screens alike.
namespace GameIconBlit {

// The drawn sizes in pixels, indexed like ch.gfx's size names (small, medium,
// large). Large is the medium bitmap with each pixel drawn as a 2 x 2 block.
inline constexpr int DRAWN_PIXELS[] = {GameIcons::SMALL_PIXELS, GameIcons::MEDIUM_PIXELS, 2 * GameIcons::MEDIUM_PIXELS};

// One icon's bitmap at one drawn size: `pixels` square in GfxRenderer::drawIcon's
// layout, each bitmap pixel drawn as a `scale` x `scale` block.
struct Source {
  const uint8_t* bitmap = nullptr;
  int pixels = 0;
  int scale = 1;
};

// The bitmap that draws icon `index` of GameIcons::ICONS at `drawnPixels`; false
// (out untouched) for an index past the library or a size not in DRAWN_PIXELS.
inline bool sourceFor(const size_t index, const int drawnPixels, Source& out) {
  if (index >= GameIcons::ICON_COUNT) return false;
  const GameIcons::Icon& icon = GameIcons::ICONS[index];
  if (drawnPixels == DRAWN_PIXELS[0]) {
    out = Source{icon.small, GameIcons::SMALL_PIXELS, 1};
  } else if (drawnPixels == DRAWN_PIXELS[1]) {
    out = Source{icon.medium, GameIcons::MEDIUM_PIXELS, 1};
  } else if (drawnPixels == DRAWN_PIXELS[2]) {
    out = Source{icon.medium, GameIcons::MEDIUM_PIXELS, 2};
  } else {
    return false;
  }
  return true;
}

// Whether the bitmap pixel drawn at (x, y), both in [0, pixels), is ink. As in
// GfxRenderer::drawIcon: stored (row, col) is drawn at (pixels - 1 - row, col),
// 1 bit per pixel, MSB first, rows padded to whole bytes, bit 0 = ink.
inline bool inkAt(const uint8_t* bitmap, const int pixels, const int x, const int y) {
  const int row = pixels - 1 - x;
  const int col = y;
  const int rowBytes = (pixels + 7) / 8;
  return ((bitmap[row * rowBytes + (col >> 3)] >> (7 - (col & 7))) & 1) == 0;
}

// Calls fn(y, x, w) for each horizontal run of ink of the icon drawn with its
// top-left at (left, top), clipped to the canvas [0, width) x [0, height).
// Adjacent ink pixels make one run. Only the rows and columns on the canvas are
// walked, so an icon wholly off it costs nothing.
template <typename Fn>
void inkRuns(const Source& source, const int32_t left, const int32_t top, const int32_t width, const int32_t height,
             Fn&& fn) {
  if (!source.bitmap || source.pixels <= 0 || source.scale <= 0) return;
  const int64_t side = static_cast<int64_t>(source.pixels) * source.scale;
  // The visible part, in drawn pixels from the icon's top-left.
  const int64_t firstX = left < 0 ? -static_cast<int64_t>(left) : 0;
  const int64_t firstY = top < 0 ? -static_cast<int64_t>(top) : 0;
  const int64_t endX = static_cast<int64_t>(width) - left < side ? static_cast<int64_t>(width) - left : side;
  const int64_t endY = static_cast<int64_t>(height) - top < side ? static_cast<int64_t>(height) - top : side;
  for (int64_t dy = firstY; dy < endY; ++dy) {
    const int bitmapY = static_cast<int>(dy / source.scale);
    int64_t runStart = -1;
    for (int64_t dx = firstX; dx < endX; ++dx) {
      const bool ink = inkAt(source.bitmap, source.pixels, static_cast<int>(dx / source.scale), bitmapY);
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
