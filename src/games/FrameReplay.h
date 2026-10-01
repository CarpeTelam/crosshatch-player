#pragma once

#include <DisplayList.h>
#include <GameImages.h>
#include <HalDisplay.h>
#include <RefreshPolicy.h>
#include <TextMetrics.h>

#include <cstdint>

class GfxRenderer;
class GameViewport;

// Turns a game frame into pixels and decides how to refresh it (AD-7: the only
// refresh policy). Drawing happens on the render task, inside
// FrameBuffers::takeFront, so the frame cannot change underneath it.
//
// Text sizes map to built-in flash fonts (small: the 10 pt UI font, medium: the
// 12 pt UI font, large: NotoSans 18). Their advance tables are what ch.text_width
// measures with, and text is drawn glyph by glyph at those advances (no kerning or
// ligatures), so drawn text is exactly as wide as ch.text_width says.
class FrameReplay {
 public:
  // Points the three sizes' advance tables at the renderer's built-in fonts (flash
  // data, so copies of textMetrics() stay valid for as long as the firmware runs).
  // Call before textMetrics() is handed to the VM. A missing font keeps the
  // stand-in for its size (logged).
  void loadFonts(const GfxRenderer& renderer);
  const GameScript::TextMetrics& textMetrics() const { return metrics; }

  // The next frame is drawn and refreshed in full, even if identical to the one
  // shown before: the screen shows something else now. Render task.
  void forceFull() { policy.forceFull(); }

  // The blank hand-off screen of a hidden pass match: a cleared screen with the HandOff view's library icon
  // (GameViewIcons::forView) at 128 px, black, centred on the canvas, and no game command at all. Forces the next
  // frame in full, since the screen no longer shows one. The caller pushes it (displayBuffer) with its own refresh.
  // Render task.
  void drawBlank(const GfxRenderer& renderer, const GameViewport& viewport);

  // Draws the frame inside the viewport, clipped to it, unless it is identical to
  // the frame on screen and nothing forces it; `hint` is the largest refresh
  // request of the frames coalesced into it, and `images` the table its image
  // commands index (the VM's, which lives as long as the frame). True when drawn:
  // then refreshMode() is the refresh to show it with. Render task.
  bool draw(const GfxRenderer& renderer, const GameViewport& viewport, const GameScript::DisplayList& frame,
            GameScript::Refresh hint, const GameCore::GameImages& images);
  HalDisplay::RefreshMode refreshMode() const;

 private:
  void drawText(const GfxRenderer& renderer, const GameViewport& viewport, const GameScript::DrawCommand& text) const;

  GameScript::TextMetrics metrics = GameScript::TextMetrics::standIn();
  GameScript::RefreshPolicy policy;
  GameScript::Refresh mode = GameScript::Refresh::Full;
};
