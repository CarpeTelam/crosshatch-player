#pragma once

#include <HalDisplay.h>

class GfxRenderer;
class GameViewport;

namespace GameScript {
class DisplayList;
}

// Turns a game frame into pixels and decides how to refresh it (AD-7: the only
// refresh policy). Drawing happens on the render task, inside
// FrameBuffers::readFront, so the frame cannot change underneath it.
class FrameReplay {
 public:
  // Clears the screen and draws the frame's commands inside the viewport, clipped
  // to it.
  // cppcheck-suppress functionStatic // gains per-match refresh state (hint, fast-refresh counter)
  void draw(const GfxRenderer& renderer, const GameViewport& viewport, const GameScript::DisplayList& frame) const;
  // The refresh for the frame just drawn: always full until the hint, forceFull,
  // and the fast-refresh counter arrive.
  // cppcheck-suppress functionStatic // see draw()
  HalDisplay::RefreshMode refreshMode() const { return HalDisplay::FULL_REFRESH; }
};
