#if FREEINK_CAP_GAMES

#include "FrameReplay.h"

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
      case GameScript::Op::Rect:
        if (command.w <= 0 || command.h <= 0) break;
        if (command.filled) {
          renderer.fillRect(ox + command.x, oy + command.y, command.w, command.h, inked(command.color));
        } else {
          renderer.drawRect(ox + command.x, oy + command.y, command.w, command.h, inked(command.color));
        }
        break;
      case GameScript::Op::Text:
        // Every size draws in one UI font until the size-to-font map arrives.
        renderer.drawText(UI_12_FONT_ID, ox + command.x, oy + command.y, command.text, inked(command.color));
        break;
    }
  }
  renderer.setClipRect(savedClip[0], savedClip[1], savedClip[2], savedClip[3]);
}

#endif  // FREEINK_CAP_GAMES
