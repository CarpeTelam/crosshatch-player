#if FREEINK_CAP_GAMES

#include "FrameReplay.h"

#include <CanvasClip.h>
#include <ChBindings.h>
#include <EpdFontData.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Utf8.h>

#include <cstddef>
#include <iterator>

#include "GameIconBlit.h"
#include "GameIconDraw.h"
#include "GameImageBlit.h"
#include "GameViewport.h"
#include "fontIds.h"

namespace {

// Icons draw at DRAWN_PIXELS[size], indexed by TextSize like the fonts below.
static_assert(std::size(GameIconBlit::DRAWN_PIXELS) == static_cast<size_t>(GameScript::TextSize::Large) + 1,
              "one drawn icon size per TextSize");
// An icon command's weight is the library's, in the same order.
static_assert(static_cast<size_t>(GameScript::IconWeight::Regular) == static_cast<size_t>(GameIcons::Weight::Regular),
              "IconWeight follows GameIcons::Weight");
static_assert(static_cast<size_t>(GameScript::IconWeight::Fill) == static_cast<size_t>(GameIcons::Weight::Fill),
              "IconWeight follows GameIcons::Weight");
static_assert(static_cast<size_t>(GameScript::IconWeight::Fill) + 1 == GameIcons::WEIGHT_COUNT,
              "one IconWeight per library weight");

// The built-in font each text size draws in, indexed by TextSize.
constexpr int TEXT_FONT_IDS[] = {UI_10_FONT_ID, UI_12_FONT_ID, NOTOSANS_18_FONT_ID};

// A table's ranges point straight at a font's intervals, read as AdvanceRange.
static_assert(sizeof(EpdUnicodeInterval) == sizeof(GameScript::AdvanceRange), "interval layout");
static_assert(offsetof(EpdUnicodeInterval, first) == offsetof(GameScript::AdvanceRange, first), "interval layout");
static_assert(offsetof(EpdUnicodeInterval, last) == offsetof(GameScript::AdvanceRange, last), "interval layout");
static_assert(offsetof(EpdUnicodeInterval, offset) == offsetof(GameScript::AdvanceRange, index), "interval layout");

// The renderer's fill for a game color; light and dark are its dithered grays.
::Color fillColor(const GameScript::Color color) {
  switch (color) {
    case GameScript::Color::Light:
      return ::Color::LightGray;
    case GameScript::Color::Dark:
      return ::Color::DarkGray;
    case GameScript::Color::Black:
      return ::Color::Black;
    case GameScript::Color::White:
      break;
  }
  return ::Color::White;
}

// Writes the UTF-8 form of one code point and a NUL into out.
void encodeUtf8(const uint32_t cp, char (&out)[5]) {
  auto* p = reinterpret_cast<unsigned char*>(out);
  if (cp < 0x80) {
    *p++ = static_cast<unsigned char>(cp);
  } else if (cp < 0x800) {
    *p++ = static_cast<unsigned char>(0xC0 | (cp >> 6));
    *p++ = static_cast<unsigned char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    *p++ = static_cast<unsigned char>(0xE0 | (cp >> 12));
    *p++ = static_cast<unsigned char>(0x80 | ((cp >> 6) & 0x3F));
    *p++ = static_cast<unsigned char>(0x80 | (cp & 0x3F));
  } else {
    *p++ = static_cast<unsigned char>(0xF0 | (cp >> 18));
    *p++ = static_cast<unsigned char>(0x80 | ((cp >> 12) & 0x3F));
    *p++ = static_cast<unsigned char>(0x80 | ((cp >> 6) & 0x3F));
    *p++ = static_cast<unsigned char>(0x80 | (cp & 0x3F));
  }
  *p = 0;
}

}  // namespace

void FrameReplay::loadFonts(const GfxRenderer& renderer) {
  const auto& fonts = renderer.getFontMap();
  for (size_t i = 0; i < 3; ++i) {
    const auto found = fonts.find(TEXT_FONT_IDS[i]);
    const EpdFontData* data = found == fonts.end() ? nullptr : found->second.getData(EpdFontFamily::REGULAR);
    if (!data || !data->glyph || !data->intervals) {
      LOG_ERR("GAME", "No built-in font for %s text; measuring with the stand-in", GameScript::SIZE_NAMES[i]);
      continue;
    }
    GameScript::AdvanceTable& table = metrics.tables[i];
    table.ranges = reinterpret_cast<const uint8_t*>(data->intervals);
    table.rangeCount = data->intervalCount;
    table.rangeStride = sizeof(EpdUnicodeInterval);
    table.advances = reinterpret_cast<const uint8_t*>(data->glyph) + offsetof(EpdGlyph, advanceX);
    table.stride = sizeof(EpdGlyph);
    // What a code point the font lacks draws as: its replacement glyph, or
    // nothing (advance 0) when it has none, like GfxRenderer::drawText.
    uint16_t replacement = 0;
    table.fallback = table.find(REPLACEMENT_GLYPH, replacement) ? replacement : 0;
  }
}

bool FrameReplay::draw(const GfxRenderer& renderer, const GameViewport& viewport, const GameScript::DisplayList& frame,
                       const GameScript::Refresh hint, const GameCore::GameImages& images) {
  if (!policy.decide(frame.hash(), hint, mode)) {
    LOG_DBG("GAME", "Frame identical to the one on screen; not refreshed");
    return false;
  }
  renderer.clearScreen();
  const auto savedClip = renderer.getClipRect();
  const int ox = viewport.originX();
  const int oy = viewport.originY();
  const int width = viewport.width();
  const int height = viewport.height();
  renderer.setClipRect(ox, oy, width, height);

  // Every shape is clipped to the canvas before drawing, so an off-canvas command
  // costs nothing and a huge one at most the canvas (the render task holds
  // RenderLock and the frame mutex here).
  const auto fill = [&](const GameScript::CanvasRect& r, const GameScript::Color color) {
    renderer.fillRectDither(ox + r.x, oy + r.y, r.w, r.h, fillColor(color));
  };
  auto reader = frame.reader();
  GameScript::DrawCommand command;
  while (reader.next(command)) {
    switch (command.op) {
      case GameScript::Op::Clear:
        fill(GameScript::CanvasRect{0, 0, width, height}, command.color);
        break;
      case GameScript::Op::Rect: {
        const GameScript::CanvasRect rect{command.x, command.y, command.w, command.h};
        if (command.filled) {
          GameScript::CanvasRect visible;
          if (GameScript::clipToCanvas(rect, width, height, visible)) fill(visible, command.color);
        } else {
          GameScript::CanvasRect edges[4];
          const int count = GameScript::outlineEdges(rect, width, height, edges);
          for (int i = 0; i < count; ++i) fill(edges[i], command.color);
        }
        break;
      }
      case GameScript::Op::Text:
        drawText(renderer, viewport, command);
        break;
      case GameScript::Op::Line: {
        int32_t x1 = command.x;
        int32_t y1 = command.y;
        int32_t x2 = command.x2;
        int32_t y2 = command.y2;
        if (GameScript::clipLine(x1, y1, x2, y2, width, height)) {
          renderer.drawLine(ox + x1, oy + y1, ox + x2, oy + y2, command.color == GameScript::Color::Black);
        }
        break;
      }
      case GameScript::Op::Circle:
        GameScript::circleRows(command.x, command.y, command.r, command.filled, width, height,
                               [&](const int32_t y, const int32_t x, const int32_t w) {
                                 fill(GameScript::CanvasRect{x, y, w, 1}, command.color);
                               });
        break;
      case GameScript::Op::Icon:
        drawGameIconAt(renderer, command.icon, GameIconBlit::DRAWN_PIXELS[static_cast<size_t>(command.size)], ox, oy,
                       width, height, command.x, command.y, command.color == GameScript::Color::Black,
                       command.weight == GameScript::IconWeight::Fill);
        break;
      case GameScript::Op::Image: {
        if (command.image >= images.count) {
          LOG_ERR("GAME", "No game image %u (the game has %u); not drawn", static_cast<unsigned>(command.image),
                  static_cast<unsigned>(images.count));
          break;
        }
        // Opaque: every visible pixel is filled with the ink runs() gives it.
        const GameCore::ImageSpan& image = images.spans[command.image];
        GameImageBlit::runs(image, images.pixelsOf(image), command.x, command.y, width, height,
                            command.color == GameScript::Color::Black,
                            [&](const int32_t y, const int32_t x, const int32_t w, const bool black) {
                              renderer.fillRect(ox + x, oy + y, w, 1, black);
                            });
        break;
      }
    }
  }
  renderer.setClipRect(savedClip[0], savedClip[1], savedClip[2], savedClip[3]);
  return true;
}

void FrameReplay::drawBlank(const GfxRenderer& renderer) {
  renderer.clearScreen();
  policy.forceFull();
}

void FrameReplay::drawText(const GfxRenderer& renderer, const GameViewport& viewport,
                           const GameScript::DrawCommand& text) const {
  const int fontId = TEXT_FONT_IDS[static_cast<size_t>(text.size)];
  const int lineHeight = renderer.getLineHeight(fontId);
  if (text.y >= viewport.height() || text.y + lineHeight <= 0) return;
  const int64_t start = GameScript::TextMetrics::alignedStart(text.x, metrics.width(text.text, text.size), text.align);
  const int top = viewport.originY() + text.y;
  const bool black = text.color == GameScript::Color::Black;
  // One code point per drawText: no kerning or ligature can apply, and the pen
  // moves by the same rounded advances ch.text_width sums. A code point the font
  // lacks is drawn as its replacement glyph, so it is never routed to another font.
  metrics.forEachGlyph(text.text, text.size, [&](const uint32_t codepoint, const int64_t pen, const bool known) {
    const int64_t x = start + pen;
    // Wholly off the canvas (glyphs never reach two line heights past their pen).
    if (x >= viewport.width() || x + 2 * lineHeight < 0) return;
    char glyph[5];
    encodeUtf8(known ? codepoint : REPLACEMENT_GLYPH, glyph);
    renderer.drawText(fontId, viewport.originX() + static_cast<int>(x), top, glyph, black);
  });
}

HalDisplay::RefreshMode FrameReplay::refreshMode() const {
  switch (mode) {
    case GameScript::Refresh::Fast:
      return HalDisplay::FAST_REFRESH;
    case GameScript::Refresh::Half:
      return HalDisplay::HALF_REFRESH;
    case GameScript::Refresh::Full:
      break;
  }
  return HalDisplay::FULL_REFRESH;
}

#endif  // FREEINK_CAP_GAMES
