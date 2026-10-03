#pragma once

// The harness's recording renderer (../stubs/GfxRenderer.h) plus what the match asks of a
// renderer and the games double does not model: displayBuffer (recorded with the calls
// made before it, and the texts the framebuffer holds then) and tapToLogical (the identity:
// the harness screen is portrait and the touch already logical). The shared double is renamed for the include and
// derived from, so it stays as it is; every source of a suite that includes this header is built with it (a source
// built against the shared double has another GfxRenderer layout).

#include <functional>
#include <string>
#include <vector>

#define GfxRenderer GfxRendererRecorder
#include "../stubs/GfxRenderer.h"
#undef GfxRenderer

class GfxRenderer : public GfxRendererRecorder {
 public:
  using GfxRendererRecorder::GfxRendererRecorder;

  // As lib/GfxRenderer: the harness screen is always portrait.
  enum class Orientation { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };
  Orientation getOrientation() const { return Orientation::Portrait; }

  // One displayBuffer call: the refresh mode, how many renderer calls came before it, and every text on the
  // framebuffer it pushed (textsOnScreen).
  struct Shown {
    HalDisplay::RefreshMode mode;
    size_t callsBefore;
    std::vector<std::string> texts;
  };

  // The device's displayBuffer pushes the whole framebuffer to the panel, whatever drew into it, and its clearScreen
  // wipes it: so what a push shows is everything drawn since the last clearScreen and nothing before it.
  void displayBuffer(const HalDisplay::RefreshMode mode = HalDisplay::FAST_REFRESH, const bool = false) const {
    shown.push_back({mode, calls.size(), keepCalls ? textsOnScreen() : std::vector<std::string>{}});
    // What a test does while the panel is being refreshed (retro deferral e3r-2): the frame is drawn and this call has
    // not returned, so the match must still be dropping gestures.
    if (onDisplay) onDisplay();
  }
  // The base's clearScreen, then onClear. On the device the VM task runs on the other core while the render task draws,
  // so it may publish a frame between any two of render's steps; onClear lets a test place that publish at a clear.
  void clearScreen(const uint8_t color = 0xFF) const {
    GfxRendererRecorder::clearScreen(color);
    if (onClear) onClear();
  }
  void tapToLogical(const float x, const float y, int& logicalX, int& logicalY) const {
    logicalX = static_cast<int>(x);
    logicalY = static_cast<int>(y);
  }

  // One drawIcon call: the bitmap's bytes (size * size / 8), where, and how big.
  struct IconDrawn {
    std::vector<uint8_t> bitmap;
    int x;
    int y;
    int size;
  };
  void drawIcon(const uint8_t bitmap[], const int x, const int y, const int size) const {
    icons.push_back({std::vector<uint8_t>(bitmap, bitmap + size * size / 8), x, y, size});
  }
  // The snapshot calls Home makes around a cover (no panel memory here: a region holds nothing).
  size_t getRegionByteSize(int, int, const int w, const int h) const { return static_cast<size_t>((w + 7) / 8) * h; }
  bool copyRegionToBuffer(int, int, int, int, uint8_t*, size_t) const { return true; }
  bool copyBufferToRegion(int, int, int, int, const uint8_t*, size_t) const { return true; }

  // A text a FreeInkUI view drew (screen_stubs/components/UiAppHost.h's RecordingTarget::text): on the device,
  // FreeInkUIGfxRenderer::text draws it into this same framebuffer, so a push shows it beside the canvas's text.
  // Recorded at the current call, so a later clearScreen wipes it.
  void noteUiText(const char* text) const {
    if (text) uiTexts.push_back({calls.size(), text});
  }

  // The texts the framebuffer holds now: the canvas's DrawText runs (joined into one string a ch.gfx.text, as
  // match::drawnTexts joins them) and the UI texts noted, in the order they were drawn, since the last clearScreen.
  // Needs keepCalls (it aborts without it, and a push then records no texts); a forget() drops what came before it as
  // a clearScreen would.
  std::vector<std::string> textsOnScreen() const {
    if (!keepCalls) {
      std::fprintf(stderr, "GfxRenderer double: textsOnScreen needs keepCalls\n");
      std::abort();
    }
    size_t from = 0;
    for (size_t i = calls.size(); i > 0; --i) {
      if (calls[i - 1].kind == Kind::ClearScreen) {
        from = i;
        break;
      }
    }
    std::vector<std::string> texts;
    size_t note = 0;
    while (note < uiTexts.size() && uiTexts[note].at < from) ++note;
    bool joining = false;
    int lineY = 0;
    for (size_t i = from; i <= calls.size(); ++i) {
      for (; note < uiTexts.size() && uiTexts[note].at == i; ++note) {
        texts.push_back(uiTexts[note].text);
        joining = false;
      }
      if (i == calls.size()) break;
      const Call& call = calls[i];
      if (call.kind != Kind::DrawText) {
        joining = false;
        continue;
      }
      if (!joining || call.y != lineY) texts.emplace_back();
      texts.back() += call.text;
      joining = true;
      lineY = call.y;
    }
    return texts;
  }

  // The base's forget(), and the UI texts noted, whose call indices it resets.
  void forget() const {
    GfxRendererRecorder::forget();
    uiTexts.clear();
  }

  // Forgets the recorded calls, the screen model, and the displays.
  void forgetAll() const {
    forget();
    shown.clear();
    icons.clear();
  }

  // Runs inside every displayBuffer call, after it is recorded; empty by default.
  mutable std::function<void()> onDisplay;
  // Runs inside every clearScreen call, after the screen is cleared; empty by default.
  mutable std::function<void()> onClear;
  mutable std::vector<Shown> shown;
  mutable std::vector<IconDrawn> icons;

 private:
  struct UiText {
    size_t at;  // calls.size() when it was drawn: after call at - 1, before call at
    std::string text;
  };
  mutable std::vector<UiText> uiTexts;
};
