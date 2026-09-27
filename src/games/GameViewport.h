#pragma once

#include <cstdint>

class GfxRenderer;

// The script canvas (AD-7): where on the renderer's logical screen the game draws,
// and the size exposed to it. FrameReplay maps canvas to screen with it and the
// match maps touches back, so drawing and taps always agree. The canvas is the
// portrait screen minus the board's bezel insets.
class GameViewport {
 public:
  static GameViewport forRenderer(const GfxRenderer& renderer);

  int originX() const { return x; }
  int originY() const { return y; }
  int width() const { return w; }
  int height() const { return h; }

  // Screen to canvas; false when the point is outside the canvas.
  bool toCanvas(int screenX, int screenY, int16_t& canvasX, int16_t& canvasY) const;

 private:
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
};
