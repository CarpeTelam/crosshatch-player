#pragma once

// UiAppHost over a recording FreeInkUI target, with the real FreeInkApp (the SDK's
// FreeInkUI.cpp is built): the same protocol as src/components/UiAppHost.cpp (the uiReady
// gate that resetUi closes and renderUi opens, routing only through it), whose body is
// copied here because the real one builds a GfxRendererTarget and the UITheme. What a
// screen draws through the app lands in `uiTarget`: every text line drawn, as the wrapped
// lines a real target would draw, with the rectangle each sits in.
//
// The target measures a glyph as 10 px and a line as 20 px whatever the font, so wrapping
// and layout are the host's, not the panel's: a test pins which text is shown, and what it
// says, never where it sits on the real panel.

#include <FreeInkApp.h>

#include <atomic>
#include <cstring>
#include <string>
#include <vector>

#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "components/UiAppHelpers.h"

namespace screen {

// A line of text the app drew: the words, and the rectangle it was laid out in.
struct DrawnText {
  std::string text;
  freeink::ui::Rect rect;
};

class RecordingTarget final : public freeink::ui::DrawTarget {
 public:
  explicit RecordingTarget(const GfxRenderer& renderer) : renderer(renderer) { newest() = this; }
  ~RecordingTarget() override {
    if (newest() == this) newest() = nullptr;
  }

  // The target of the UiAppHost constructed last, still alive: a screen's host is a private
  // base, so a test reads what its screen drew through this.
  static RecordingTarget*& newest() {
    static RecordingTarget* target = nullptr;
    return target;
  }

  freeink::ui::DeviceContext deviceContext() const {
    freeink::ui::DeviceContext device;
    device.width = static_cast<int16_t>(renderer.getScreenWidth());
    device.height = static_cast<int16_t>(renderer.getScreenHeight());
    device.hasButtons = true;
    device.hasTouch = true;
    int top = 0, right = 0, bottom = 0, left = 0;
    renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
    device.safeArea = freeink::ui::Insets{static_cast<int16_t>(top), static_cast<int16_t>(right),
                                          static_cast<int16_t>(bottom), static_cast<int16_t>(left)};
    return device;
  }

  freeink::ui::Size measureText(freeink::ui::FontId, const char* text, freeink::ui::TextStyle) const override {
    return {static_cast<int16_t>(text ? std::strlen(text) * GLYPH_WIDTH : 0), LINE_HEIGHT};
  }
  int16_t lineHeight(freeink::ui::FontId) const override { return LINE_HEIGHT; }
  void fill(freeink::ui::Rect, freeink::ui::Paint, uint8_t, uint8_t) override { ++fills; }
  void stroke(freeink::ui::Rect, freeink::ui::Paint, uint8_t, uint8_t, uint8_t) override { ++strokes; }
  void line(freeink::ui::Point, freeink::ui::Point, uint8_t, freeink::ui::Paint) override {}
  void triangle(freeink::ui::Point, freeink::ui::Point, freeink::ui::Point, freeink::ui::Paint) override {}
  void text(freeink::ui::Rect rect, const char* text, freeink::ui::TextStyle style) override {
    freeink::ui::layoutText(*this, rect, text, style, [this](const char* line, const freeink::ui::Rect where) {
      drawn.push_back({line, where});
    });
  }
  void bitmap(freeink::ui::Rect, freeink::ui::BitmapRef, freeink::ui::BitmapMode, freeink::ui::Paint,
              freeink::ui::Rotation) override {
    ++bitmaps;
  }

  // Whether some drawn line is exactly `line`.
  bool drewLine(const std::string& line) const {
    for (const DrawnText& entry : drawn)
      if (entry.text == line) return true;
    return false;
  }
  // The drawn lines joined with '\n', for a message that wrapped.
  std::string joined() const {
    std::string all;
    for (const DrawnText& entry : drawn) all += (all.empty() ? "" : "\n") + entry.text;
    return all;
  }
  void forget() {
    drawn.clear();
    fills = strokes = bitmaps = 0;
  }

  static constexpr int16_t GLYPH_WIDTH = 10;
  static constexpr int16_t LINE_HEIGHT = 20;

  std::vector<DrawnText> drawn;
  int fills = 0;
  int strokes = 0;
  int bitmaps = 0;

 private:
  const GfxRenderer& renderer;
};

}  // namespace screen

class UiAppHost {
 public:
  using UiApp = freeink::ui::FreeInkApp<24, 6>;
  using UiScreen = UiApp::ScreenType;

  explicit UiAppHost(const GfxRenderer& renderer) : uiTarget(renderer), app(uiTarget, uiTarget.deviceContext()) {}

  void resetUi() { uiReady = false; }

  void renderUi() {
    app.setDevice(uiTarget.deviceContext());
    app.render();
    uiReady = true;
  }

  struct TouchRoute {
    freeink::ui::ActionEvent event{};
    freeink::ui::InputSnapshot snap{};
    bool routed = false;
    explicit operator bool() const { return static_cast<bool>(event); }
  };

  TouchRoute routeTouch(const MappedInputManager& input, const bool withLongPress = false,
                        const bool routeHeld = false) {
    TouchRoute result;
    if (!uiReady) return result;
    result.snap = touchSnapshotFrom(input, withLongPress);
    if (!result.snap.touchPressed && !result.snap.touchReleased && !(routeHeld && result.snap.touchHeld)) {
      return result;
    }
    result.routed = true;
    result.event = app.route(result.snap);
    return result;
  }

  freeink::ui::ActionEvent route(const freeink::ui::InputSnapshot& snap) {
    if (!uiReady) return {};
    return app.route(snap);
  }

  void closeRouting() { uiReady = false; }
  bool routingReady() const { return uiReady.load(); }

  screen::RecordingTarget uiTarget;  // must precede `app`: the app holds a reference to it
  UiApp app;

 private:
  std::atomic<bool> uiReady{false};
};
