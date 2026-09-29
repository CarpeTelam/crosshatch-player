#pragma once

// A recording GfxRenderer for host tests of the code in src/games: the methods
// FrameReplay, GameIconDraw, and GameViewport call, with lib/GfxRenderer's names and
// signatures, so those sources build against it unchanged. Not the real renderer: it
// has no panel, orientation, or dithering. It does two things.
//   - Records every call, in order (`calls`), with the clip rectangle in force, so a
//     test can check what the code asked for, apart from what a clip would hide.
//   - Keeps a logical-screen model (`pixel`) that honours the clip rectangle as the
//     real one does (a pixel outside the rectangle is not drawn), so a test can check
//     what would show. clearScreen paints it white, fillRect black or white, and
//     fillRectDither one of four values.
// drawLine and drawText are recorded only.

#include <EpdFontFamily.h>
#include <HalDisplay.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <string>
#include <vector>

// As lib/GfxRenderer/GfxRenderer.h.
enum Color : uint8_t { Clear = 0x00, White = 0x01, LightGray = 0x05, DarkGray = 0x0A, Black = 0x10 };

class GfxRenderer {
 public:
  enum class Kind : uint8_t { ClearScreen, FillRect, FillRectDither, DrawLine, DrawText, SetClipRect };

  struct Call {
    Kind kind = Kind::ClearScreen;
    int x = 0, y = 0, w = 0, h = 0;  // FillRect*, SetClipRect; DrawLine: x, y to x2, y2 (w and h unused)
    int x2 = 0, y2 = 0;
    bool black = false;              // FillRect, DrawLine, DrawText
    ::Color color = ::Color::Clear;  // FillRectDither
    int fontId = 0;                  // DrawText
    std::string text;                // DrawText
    std::array<int, 4> clip{};       // the clip rectangle (x, y, w, h) in force when the call was made
  };

  // What one screen pixel holds.
  enum Pixel : uint8_t { Untouched = 0, PixelWhite = 1, PixelBlack = 2, PixelLight = 3, PixelDark = 4 };

  explicit GfxRenderer(int width = 480, int height = 800)
      : width(width), height(height), screen(static_cast<size_t>(width) * height, Untouched) {}

  // ---- What FrameReplay, GameIconDraw, and GameViewport call ----
  int getScreenWidth() const { return width; }
  int getScreenHeight() const { return height; }
  // The bezel a game canvas leaves out: 474 x 788 of 480 x 800, as the real boards do.
  void getOrientedViewableTRBL(int* outTop, int* outRight, int* outBottom, int* outLeft) const {
    *outTop = insetTop;
    *outRight = insetRight;
    *outBottom = insetBottom;
    *outLeft = insetLeft;
  }

  const std::map<int, EpdFontFamily>& getFontMap() const { return fontMap; }
  int getLineHeight(const int fontId) const {
    const auto found = fontMap.find(fontId);
    return found == fontMap.end() ? 0 : found->second.getData(EpdFontFamily::REGULAR)->advanceY;
  }

  void clearScreen(const uint8_t color = 0xFF) const {
    Call call;
    call.kind = Kind::ClearScreen;
    record(call);
    std::fill(screen.begin(), screen.end(), color == 0xFF ? PixelWhite : PixelBlack);
  }

  std::array<int, 4> getClipRect() const { return {clipLeft, clipTop, clipRight - clipLeft, clipBottom - clipTop}; }
  void setClipRect(const int x, const int y, const int w, const int h) const {
    clipLeft = x;
    clipTop = y;
    clipRight = x + w;
    clipBottom = y + h;
    Call call;
    call.kind = Kind::SetClipRect;
    call.x = x;
    call.y = y;
    call.w = w;
    call.h = h;
    record(call);
  }

  void fillRect(const int x, const int y, const int w, const int h, const bool state = true) const {
    Call call;
    call.kind = Kind::FillRect;
    call.x = x;
    call.y = y;
    call.w = w;
    call.h = h;
    call.black = state;
    record(call);
    paint(x, y, w, h, state ? PixelBlack : PixelWhite);
  }

  void fillRectDither(const int x, const int y, const int w, const int h, const ::Color color) const {
    Call call;
    call.kind = Kind::FillRectDither;
    call.x = x;
    call.y = y;
    call.w = w;
    call.h = h;
    call.color = color;
    record(call);
    switch (color) {
      case ::Color::Clear:
        break;
      case ::Color::White:
        paint(x, y, w, h, PixelWhite);
        break;
      case ::Color::LightGray:
        paint(x, y, w, h, PixelLight);
        break;
      case ::Color::DarkGray:
        paint(x, y, w, h, PixelDark);
        break;
      case ::Color::Black:
        paint(x, y, w, h, PixelBlack);
        break;
    }
  }

  void drawLine(const int x1, const int y1, const int x2, const int y2, const bool state = true) const {
    Call call;
    call.kind = Kind::DrawLine;
    call.x = x1;
    call.y = y1;
    call.x2 = x2;
    call.y2 = y2;
    call.black = state;
    record(call);
  }

  void drawText(const int fontId, const int x, const int y, const char* text, const bool black = true,
                const EpdFontFamily::Style = EpdFontFamily::REGULAR) const {
    Call call;
    call.kind = Kind::DrawText;
    call.x = x;
    call.y = y;
    call.fontId = fontId;
    call.text = text;
    call.black = black;
    record(call);
  }

  // ---- Test set-up ----
  // Registers a built-in font under `fontId`: ASCII 0x20-0x7E, each glyph advancing
  // `advancePixels`, lines `lineHeight` apart. What FrameReplay::loadFonts reads.
  void addFont(const int fontId, const int lineHeight, const int advancePixels = 10) {
    fontData.emplace_back();
    FontData& font = fontData.back();
    font.glyphs.assign(0x7E - 0x20 + 1, EpdGlyph{});
    for (auto& glyph : font.glyphs) glyph.advanceX = static_cast<uint16_t>(advancePixels * 16);
    font.interval = EpdUnicodeInterval{0x20, 0x7E, 0};
    font.data = EpdFontData{};
    font.data.glyph = font.glyphs.data();
    font.data.intervals = &font.interval;
    font.data.intervalCount = 1;
    font.data.advanceY = static_cast<uint8_t>(lineHeight);
    fonts.emplace_back(&font.data);
    fontMap.emplace(fontId, EpdFontFamily(&fonts.back()));
  }
  void setInsets(const int top, const int right, const int bottom, const int left) {
    insetTop = top;
    insetRight = right;
    insetBottom = bottom;
    insetLeft = left;
  }

  // ---- What a test reads ----
  mutable std::vector<Call> calls;

  // What the screen model holds at (x, y); Untouched outside the screen.
  Pixel pixel(const int x, const int y) const {
    return x < 0 || y < 0 || x >= width || y >= height ? Untouched : screen[static_cast<size_t>(y) * width + x];
  }
  size_t pixelCount(const Pixel value) const {
    return static_cast<size_t>(std::count(screen.begin(), screen.end(), value));
  }
  // How many calls of `kind` since construction or forget(), kept even when `keepCalls` is off.
  size_t count(const Kind kind) const { return counts[static_cast<size_t>(kind)]; }
  // Off, `calls` stays empty and only the counts and the screen model are kept: for a frame that
  // makes a million calls.
  bool keepCalls = true;
  // The fillRect and fillRectDither calls that are not wholly inside (x, y, w, h): what the
  // code asked to draw off a canvas, however the clip rectangle then hid it.
  size_t fillsOutside(const int x, const int y, const int w, const int h) const {
    if (!keepCalls) {
      std::fprintf(stderr, "GfxRenderer double: fillsOutside needs keepCalls\n");
      std::abort();  // an empty call list would answer 0 and pass without checking
    }
    size_t n = 0;
    for (const Call& call : calls) {
      if (call.kind != Kind::FillRect && call.kind != Kind::FillRectDither) continue;
      if (call.x < x || call.y < y || call.x + call.w > x + w || call.y + call.h > y + h) ++n;
    }
    return n;
  }
  // Forgets the calls and repaints the screen model as untouched.
  void forget() const {
    calls.clear();
    counts.fill(0);
    std::fill(screen.begin(), screen.end(), Untouched);
  }

 private:
  struct FontData {
    std::vector<EpdGlyph> glyphs;
    EpdUnicodeInterval interval{};
    EpdFontData data{};
  };

  void record(Call& call) const {
    ++counts[static_cast<size_t>(call.kind)];
    if (!keepCalls) return;
    call.clip = getClipRect();
    calls.push_back(call);
  }
  // Paints the part of the rectangle inside the clip rectangle and the screen.
  void paint(const int x, const int y, const int w, const int h, const Pixel value) const {
    const int x0 = std::max({x, clipLeft, 0});
    const int y0 = std::max({y, clipTop, 0});
    const int x1 = std::min({x + w, clipRight, width});
    const int y1 = std::min({y + h, clipBottom, height});
    for (int py = y0; py < y1; ++py)
      for (int px = x0; px < x1; ++px) screen[static_cast<size_t>(py) * width + px] = value;
  }

  int width;
  int height;
  int insetTop = 6;
  int insetRight = 3;
  int insetBottom = 6;
  int insetLeft = 3;
  mutable std::vector<Pixel> screen;
  mutable std::array<size_t, 6> counts{};
  mutable int clipLeft = 0;
  mutable int clipTop = 0;
  mutable int clipRight = 32767;
  mutable int clipBottom = 32767;
  std::deque<FontData> fontData;
  std::deque<EpdFont> fonts;
  std::map<int, EpdFontFamily> fontMap;
};
