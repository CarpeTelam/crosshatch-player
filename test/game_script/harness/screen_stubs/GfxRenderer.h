#pragma once

// The harness's recording renderer (../stubs/GfxRenderer.h) plus what the match asks of a
// renderer and the games double does not model: displayBuffer (recorded with the calls
// made before it) and tapToLogical (the identity: the harness screen is portrait and the
// touch already logical). The shared double is renamed for the include and derived from,
// so it stays as it is; every source of a suite that includes this header is built with it
// (a source built against the shared double has another GfxRenderer layout).

#define GfxRenderer GfxRendererRecorder
#include "../stubs/GfxRenderer.h"
#undef GfxRenderer

class GfxRenderer : public GfxRendererRecorder {
 public:
  using GfxRendererRecorder::GfxRendererRecorder;

  // As lib/GfxRenderer: the harness screen is always portrait.
  enum class Orientation { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };
  Orientation getOrientation() const { return Orientation::Portrait; }

  // One displayBuffer call: the refresh mode, and how many renderer calls came before it.
  struct Shown {
    HalDisplay::RefreshMode mode;
    size_t callsBefore;
  };

  void displayBuffer(const HalDisplay::RefreshMode mode = HalDisplay::FAST_REFRESH, const bool = false) const {
    shown.push_back({mode, calls.size()});
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

  // Forgets the recorded calls, the screen model, and the displays.
  void forgetAll() const {
    forget();
    shown.clear();
    icons.clear();
  }

  mutable std::vector<Shown> shown;
  mutable std::vector<IconDrawn> icons;
};
