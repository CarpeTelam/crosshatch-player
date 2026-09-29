#pragma once

#include <cstdint>

// The clip both blits share (GameIconBlit::inkRuns, GameImageBlit::runs): the part of a bitmap drawn with its
// top-left at (left, top) that lies on the canvas [0, width) x [0, height). Pure and header-only so the host tests
// build it. It is int64 throughout: a game passes any int16 origin, and the canvas edge minus a very negative origin
// must not wrap.
namespace BlitClip {

// The visible part in drawn pixels from the bitmap's top-left: columns [firstX, endX) and rows [firstY, endY).
struct Visible {
  int64_t firstX = 0;
  int64_t firstY = 0;
  int64_t endX = 0;
  int64_t endY = 0;

  // Nothing is on the canvas: the bitmap is wholly off it, or is empty.
  bool empty() const { return endX <= firstX || endY <= firstY; }
};

// The visible part of a `drawnWidth` x `drawnHeight` bitmap at (left, top).
constexpr Visible visible(const int32_t left, const int32_t top, const int32_t width, const int32_t height,
                          const int64_t drawnWidth, const int64_t drawnHeight) {
  Visible v;
  v.firstX = left < 0 ? -static_cast<int64_t>(left) : 0;
  v.firstY = top < 0 ? -static_cast<int64_t>(top) : 0;
  v.endX = static_cast<int64_t>(width) - left < drawnWidth ? static_cast<int64_t>(width) - left : drawnWidth;
  v.endY = static_cast<int64_t>(height) - top < drawnHeight ? static_cast<int64_t>(height) - top : drawnHeight;
  return v;
}

}  // namespace BlitClip
