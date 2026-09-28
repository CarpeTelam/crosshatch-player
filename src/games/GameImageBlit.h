#pragma once

#include <GameImages.h>

#include <cstddef>
#include <cstdint>

// A game image (GameCore::GameImages) as pixels, pure so it is host-tested: each
// row's runs of one colour, clipped to a canvas. FrameReplay fills the runs for
// ch.gfx.image, opaque: black pixels and white pixels are both drawn.
namespace GameImageBlit {

// Whether pixel (x, y) of an image's rows is black. As the converter writes them
// (PngToBmpConverter's 1-bit layout): top-down rows of `rowBytes` bytes, 1 bit per
// pixel, MSB first, bit 1 white (palette index 1), bit 0 black.
inline bool blackAt(const uint8_t* pixels, const uint32_t rowBytes, const uint32_t x, const uint32_t y) {
  const uint8_t byte = pixels[static_cast<size_t>(y) * rowBytes + (x >> 3)];
  return ((byte >> (7 - (x & 7))) & 1) == 0;
}

// Calls fn(y, x, w, black) for each horizontal run of one colour of the image drawn
// at its own size with its top-left at (left, top), clipped to the canvas
// [0, width) x [0, height); `black` is the ink to fill the run with. With
// `drawBlack` (ch.gfx.image's "black") the image draws as converted: its black
// pixels black, its white pixels white; without it ("white") the two swap.
// Adjacent pixels of one colour make one run; every visible pixel is in exactly
// one run. Only the rows and columns on the canvas are walked, and a row's padding
// bits never are, so an image wholly off it costs nothing.
template <typename Fn>
void runs(const GameCore::ImageSpan& image, const uint8_t* pixels, const int32_t left, const int32_t top,
          const int32_t width, const int32_t height, const bool drawBlack, Fn&& fn) {
  if (!pixels || image.width == 0 || image.height == 0 || image.rowBytes == 0) return;
  // The visible part, in image pixels from its top-left.
  const int64_t firstX = left < 0 ? -static_cast<int64_t>(left) : 0;
  const int64_t firstY = top < 0 ? -static_cast<int64_t>(top) : 0;
  const int64_t imageW = image.width;
  const int64_t imageH = image.height;
  const int64_t endX = static_cast<int64_t>(width) - left < imageW ? static_cast<int64_t>(width) - left : imageW;
  const int64_t endY = static_cast<int64_t>(height) - top < imageH ? static_cast<int64_t>(height) - top : imageH;
  if (endX <= firstX || endY <= firstY) return;
  for (int64_t dy = firstY; dy < endY; ++dy) {
    int64_t runStart = firstX;
    bool runBlack = false;
    for (int64_t dx = firstX; dx < endX; ++dx) {
      const bool black = blackAt(pixels, image.rowBytes, static_cast<uint32_t>(dx), static_cast<uint32_t>(dy));
      if (dx == firstX) {
        runBlack = black;
      } else if (black != runBlack) {
        fn(static_cast<int32_t>(top + dy), static_cast<int32_t>(left + runStart), static_cast<int32_t>(dx - runStart),
           runBlack == drawBlack);
        runStart = dx;
        runBlack = black;
      }
    }
    fn(static_cast<int32_t>(top + dy), static_cast<int32_t>(left + runStart), static_cast<int32_t>(endX - runStart),
       runBlack == drawBlack);
  }
}

}  // namespace GameImageBlit
