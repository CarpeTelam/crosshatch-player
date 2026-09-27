#if FREEINK_CAP_GAMES

#include "FrameReplay.h"

#include <CanvasClip.h>
#include <DisplayList.h>
#include <GfxRenderer.h>

#include "GameViewport.h"
#include "fontIds.h"

namespace {

// Ink for a color; light and dark fills are drawn dithered once they exist.
bool inked(const GameScript::Color color) {
  return color == GameScript::Color::Black || color == GameScript::Color::Dark;
}

}  // namespace

// cppcheck-suppress functionStatic // see FrameReplay.h
void FrameReplay::draw(const GfxRenderer& renderer, const GameViewport& viewport,
                       const GameScript::DisplayList& frame) const {
  renderer.clearScreen();
  const auto savedClip = renderer.getClipRect();
  const int ox = viewport.originX();
  const int oy = viewport.originY();
  renderer.setClipRect(ox, oy, viewport.width(), viewport.height());

  auto reader = frame.reader();
  GameScript::DrawCommand command;
  while (reader.next(command)) {
    switch (command.op) {
      case GameScript::Op::Clear:
        renderer.fillRect(ox, oy, viewport.width(), viewport.height(), inked(command.color));
        break;
      case GameScript::Op::Rect: {
        // Clip to the canvas before drawing, so an off-canvas rect costs nothing and
        // a huge one costs at most the canvas (the render task holds RenderLock and
        // the frame mutex here).
        const GameScript::CanvasRect rect{command.x, command.y, command.w, command.h};
        const bool ink = inked(command.color);
        if (command.filled) {
          GameScript::CanvasRect visible;
          if (GameScript::clipToCanvas(rect, viewport.width(), viewport.height(), visible)) {
            renderer.fillRect(ox + visible.x, oy + visible.y, visible.w, visible.h, ink);
          }
        } else {
          GameScript::CanvasRect edges[4];
          const int count = GameScript::outlineEdges(rect, viewport.width(), viewport.height(), edges);
          for (int i = 0; i < count; ++i) {
            renderer.fillRect(ox + edges[i].x, oy + edges[i].y, edges[i].w, edges[i].h, ink);
          }
        }
        break;
      }
      case GameScript::Op::Text:
        // Every size draws in one UI font until the size-to-font map arrives.
        renderer.drawText(UI_12_FONT_ID, ox + command.x, oy + command.y, command.text, inked(command.color));
        break;
      case GameScript::Op::Line:
      case GameScript::Op::Circle:
        // Recorded in the frame; drawn once replay covers every command kind.
        break;
    }
  }
  renderer.setClipRect(savedClip[0], savedClip[1], savedClip[2], savedClip[3]);
}

#endif  // FREEINK_CAP_GAMES
