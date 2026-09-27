#if FREEINK_CAP_GAMES

#include "GameViewport.h"

#include <GfxRenderer.h>

GameViewport GameViewport::forRenderer(const GfxRenderer& renderer) {
  int top = 0;
  int right = 0;
  int bottom = 0;
  int left = 0;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  GameViewport viewport;
  viewport.x = left;
  viewport.y = top;
  viewport.w = renderer.getScreenWidth() - left - right;
  viewport.h = renderer.getScreenHeight() - top - bottom;
  return viewport;
}

bool GameViewport::toCanvas(const int screenX, const int screenY, int16_t& canvasX, int16_t& canvasY) const {
  const int cx = screenX - x;
  const int cy = screenY - y;
  if (cx < 0 || cy < 0 || cx >= w || cy >= h) return false;
  canvasX = static_cast<int16_t>(cx);
  canvasY = static_cast<int16_t>(cy);
  return true;
}

#endif  // FREEINK_CAP_GAMES
