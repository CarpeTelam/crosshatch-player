#pragma once

#include <HalDisplay.h>
#include <TextMetrics.h>

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
  // The advance tables of the fonts text is drawn in, one per size, for
  // ch.text_width; the match passes them to the VM at start (AD-7). The stand-in
  // until the size-to-font map (entry 11) supplies the real tables.
  static constexpr GameScript::TextMetrics textMetrics() { return GameScript::TextMetrics::standIn(); }
};
