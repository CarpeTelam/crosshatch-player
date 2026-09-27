#pragma once

#include <cstdint>

namespace GameScript {

// A rectangle in canvas pixels. Wider than the display list's int16_t fields so
// x + w cannot overflow.
struct CanvasRect {
  int32_t x = 0;
  int32_t y = 0;
  int32_t w = 0;
  int32_t h = 0;
};

// Intersects r with the canvas [0, width) x [0, height). False (out untouched)
// when nothing is left, including for a non-positive w or h.
inline bool clipToCanvas(const CanvasRect& r, const int32_t width, const int32_t height, CanvasRect& out) {
  if (r.w <= 0 || r.h <= 0) return false;
  const int32_t left = r.x > 0 ? r.x : 0;
  const int32_t top = r.y > 0 ? r.y : 0;
  const int32_t right = r.x + r.w < width ? r.x + r.w : width;
  const int32_t bottom = r.y + r.h < height ? r.y + r.h : height;
  if (left >= right || top >= bottom) return false;
  out = CanvasRect{left, top, right - left, bottom - top};
  return true;
}

// The 1-pixel outline of r as up to four edge rectangles, each clipped to the
// canvas; edges wholly off the canvas are left out. Returns how many were
// written. Drawing only these keeps the cost bounded by the canvas, not by r.
inline int outlineEdges(const CanvasRect& r, const int32_t width, const int32_t height, CanvasRect (&out)[4]) {
  if (r.w <= 0 || r.h <= 0) return 0;
  const CanvasRect edges[4] = {
      {r.x, r.y, r.w, 1},                // top
      {r.x, r.y + r.h - 1, r.w, 1},      // bottom
      {r.x, r.y, 1, r.h},                // left
      {r.x + r.w - 1, r.y, 1, r.h},      // right
  };
  int count = 0;
  for (const CanvasRect& edge : edges) {
    CanvasRect clipped;
    if (!clipToCanvas(edge, width, height, clipped)) continue;
    out[count++] = clipped;
  }
  return count;
}

}  // namespace GameScript
