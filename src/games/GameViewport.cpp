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

#endif  // FREEINK_CAP_GAMES
