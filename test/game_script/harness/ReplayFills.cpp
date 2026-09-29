#include "ReplayFills.h"

#include "FrameReplay.h"
#include "GameViewport.h"
#include "GfxRenderer.h"
#include "Logging.h"

namespace harness {

ReplayResult replayFills(const GameScript::DisplayList& frame, const GameCore::GameImages& images,
                         const int canvasWidth, const int canvasHeight) {
  GfxRenderer renderer(canvasWidth, canvasHeight);
  renderer.keepCalls = false;  // a frame at the budget makes a million calls
  const GameViewport viewport(0, 0, canvasWidth, canvasHeight);
  FrameReplay replay;
  fakelog::lines.clear();
  ReplayResult result;
  result.drawn = replay.draw(renderer, viewport, frame, frame.refresh(), images);
  result.fills = renderer.count(GfxRenderer::Kind::FillRect);
  for (const std::string& line : fakelog::lines) {
    if (line.rfind("ERR", 0) == 0) result.errors.push_back(line);
  }
  return result;
}

}  // namespace harness
