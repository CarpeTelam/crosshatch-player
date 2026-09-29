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

  // Forgets the recorded calls, the screen model, and the displays.
  void forgetAll() const {
    forget();
    shown.clear();
  }

  mutable std::vector<Shown> shown;
};
