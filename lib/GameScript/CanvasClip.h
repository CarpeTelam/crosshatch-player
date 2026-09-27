#pragma once

#include <cmath>
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
      {r.x, r.y, r.w, 1},            // top
      {r.x, r.y + r.h - 1, r.w, 1},  // bottom
      {r.x, r.y, 1, r.h},            // left
      {r.x + r.w - 1, r.y, 1, r.h},  // right
  };
  int count = 0;
  for (const CanvasRect& edge : edges) {
    CanvasRect clipped;
    if (!clipToCanvas(edge, width, height, clipped)) continue;
    out[count++] = clipped;
  }
  return count;
}

// Clips the segment (x1, y1)-(x2, y2) to the canvas [0, width) x [0, height)
// (Cohen-Sutherland), moving the endpoints onto the canvas edges. False (points
// untouched) when no part of it is on the canvas. Drawing the clipped segment
// costs at most the canvas, however far away the endpoints were.
inline bool clipLine(int32_t& x1, int32_t& y1, int32_t& x2, int32_t& y2, const int32_t width, const int32_t height) {
  if (width <= 0 || height <= 0) return false;
  constexpr int LEFT = 1;
  constexpr int RIGHT = 2;
  constexpr int ABOVE = 4;
  constexpr int BELOW = 8;
  const int64_t maxX = width - 1;
  const int64_t maxY = height - 1;
  const auto outcode = [&](const int64_t x, const int64_t y) {
    int code = 0;
    if (x < 0) code |= LEFT;
    if (x > maxX) code |= RIGHT;
    if (y < 0) code |= ABOVE;
    if (y > maxY) code |= BELOW;
    return code;
  };
  int64_t ax = x1;
  int64_t ay = y1;
  int64_t bx = x2;
  int64_t by = y2;
  int codeA = outcode(ax, ay);
  int codeB = outcode(bx, by);
  // Each pass puts one endpoint on an edge line, clearing that bit for good;
  // integer rounding can set another, so a few passes more than four are allowed.
  for (int pass = 0; pass < 8; ++pass) {
    if ((codeA | codeB) == 0) {
      x1 = static_cast<int32_t>(ax);
      y1 = static_cast<int32_t>(ay);
      x2 = static_cast<int32_t>(bx);
      y2 = static_cast<int32_t>(by);
      return true;
    }
    if ((codeA & codeB) != 0) return false;
    const int code = codeA != 0 ? codeA : codeB;
    int64_t x = 0;
    int64_t y = 0;
    if (code & ABOVE) {
      x = ax + (bx - ax) * (0 - ay) / (by - ay);
      y = 0;
    } else if (code & BELOW) {
      x = ax + (bx - ax) * (maxY - ay) / (by - ay);
      y = maxY;
    } else if (code & LEFT) {
      y = ay + (by - ay) * (0 - ax) / (bx - ax);
      x = 0;
    } else {
      y = ay + (by - ay) * (maxX - ax) / (bx - ax);
      x = maxX;
    }
    if (code == codeA) {
      ax = x;
      ay = y;
      codeA = outcode(ax, ay);
    } else {
      bx = x;
      by = y;
      codeB = outcode(bx, by);
    }
  }
  return false;
}

// floor(sqrt(n)) for 0 <= n < 2^31, from a float estimate corrected to exact.
inline int64_t floorSqrt(const int64_t n) {
  int64_t root = static_cast<int64_t>(std::sqrt(static_cast<float>(n)));
  while (root > 0 && root * root > n) --root;
  while ((root + 1) * (root + 1) <= n) ++root;
  return root;
}

// The circle of centre (cx, cy) and radius r, filled or as a 1-pixel ring, as
// horizontal spans: fn(y, x, w) for each span, clipped to the canvas [0, width) x
// [0, height). Only canvas rows are walked, so the cost is bounded by the canvas
// however large r is. A row's half-width is floor(sqrt(r^2 + r - dy^2)) (the
// disc of radius r + 1/2); the ring is the disc minus the disc of radius r - 1.
// A negative r draws nothing; r = 0 is one pixel.
template <typename Fn>
inline void circleRows(const int32_t cx, const int32_t cy, const int32_t r, const bool filled, const int32_t width,
                       const int32_t height, Fn&& fn) {
  if (r < 0 || width <= 0 || height <= 0) return;
  const int64_t rr = r;
  const auto span = [&](const int64_t y, int64_t left, int64_t right) {
    if (left < 0) left = 0;
    if (right > width - 1) right = width - 1;
    if (left > right) return;
    fn(static_cast<int32_t>(y), static_cast<int32_t>(left), static_cast<int32_t>(right - left + 1));
  };
  const int64_t top = cy - rr > 0 ? cy - rr : 0;
  const int64_t bottom = cy + rr < height - 1 ? cy + rr : height - 1;
  for (int64_t y = top; y <= bottom; ++y) {
    const int64_t dy = y - cy;
    const int64_t outer = floorSqrt(rr * rr + rr - dy * dy);
    int64_t inner = -1;
    if (!filled && rr >= 1) {
      const int64_t innerSquared = (rr - 1) * (rr - 1) + (rr - 1) - dy * dy;
      if (innerSquared >= 0) inner = floorSqrt(innerSquared);
    }
    if (inner < 0) {
      span(y, cx - outer, cx + outer);
    } else {
      span(y, cx - outer, cx - inner - 1);
      span(y, cx + inner + 1, cx + outer);
    }
  }
}

}  // namespace GameScript
