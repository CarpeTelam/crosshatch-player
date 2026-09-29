#pragma once

// The fills the real FrameReplay::draw makes for a frame, counted on the recording
// renderer. Its own header and source, apart from GfxRenderer.h, so a suite that
// uses `using namespace GameScript` (where GameScript::Color and the renderer's
// global ::Color would clash) can ask without including the renderer double.

#include <DisplayList.h>
#include <GameImages.h>

#include <cstdint>
#include <string>
#include <vector>

namespace harness {

struct ReplayResult {
  bool drawn = false;               // FrameReplay::draw's result
  uint64_t fills = 0;               // fillRect calls: what icons and images make, a run at a time
  std::vector<std::string> errors;  // LOG_ERR lines (an icon or image it could not draw)
};

// Draws `frame` on a canvas of canvasWidth x canvasHeight at the screen's origin.
ReplayResult replayFills(const GameScript::DisplayList& frame, const GameCore::GameImages& images, int canvasWidth,
                         int canvasHeight);

}  // namespace harness
