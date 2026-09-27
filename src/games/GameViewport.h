#pragma once

#include <cstdint>

class GfxRenderer;

// The script canvas (AD-7): where on the renderer's logical screen the game draws,
// and the size exposed to it. FrameReplay maps canvas to screen with it and the
// match maps touches back (GameTouch.h), so drawing and touches always agree. The
// canvas is the logical screen minus the board's bezel insets: on the X4 Pro and
// the Sticky alike, 474 x 788 of the 480 x 800 portrait screen.
class GameViewport {
 public:
  GameViewport() = default;
  // A canvas of w x h at (x, y) on the logical screen (tests; the match uses forRenderer).
  GameViewport(const int left, const int top, const int canvasW, const int canvasH)
      : x(left), y(top), w(canvasW), h(canvasH) {}
  static GameViewport forRenderer(const GfxRenderer& renderer);

  int originX() const { return x; }
  int originY() const { return y; }
  int width() const { return w; }
  int height() const { return h; }

  // Screen to canvas; false when the point is outside the canvas.
  bool toCanvas(const int screenX, const int screenY, int16_t& canvasX, int16_t& canvasY) const {
    const int cx = screenX - x;
    const int cy = screenY - y;
    if (cx < 0 || cy < 0 || cx >= w || cy >= h) return false;
    canvasX = static_cast<int16_t>(cx);
    canvasY = static_cast<int16_t>(cy);
    return true;
  }

 private:
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
};
