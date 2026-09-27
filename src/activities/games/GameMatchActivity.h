#pragma once

#include <Manifest.h>

#include <atomic>
#include <cstdint>
#include <memory>

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "games/FrameReplay.h"
#include "games/GameVM.h"
#include "games/GameViewport.h"

// One solo match (AD-20): owns the GameVM task and, through it, the game's assets,
// arena, and frame buffers. The canvas is drawn by FrameReplay and fed by taps
// mapped through GameViewport; the UiAppHost is for the runtime's own views.
class GameMatchActivity final : public Activity, private UiAppHost {
 public:
  GameMatchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const GameCore::Manifest& manifest);
  ~GameMatchActivity() override;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  // True while a callback runs, so the CPU stays at full clock for the budget (AD-5).
  bool skipLoopDelay() override { return vm && vm->busy(); }

 private:
  enum class State : uint8_t { Starting, Playing, Error };
  // How long onExit waits after cancel for the VM to end before abandoning it (AD-5).
  static constexpr uint32_t STOP_TIMEOUT_MS = 500;

  void showError(const char* detail);
  void renderError();

  GameCore::Manifest manifest;
  GameViewport viewport;
  FrameReplay replay;
  std::unique_ptr<GameVM> vm;
  std::atomic<State> state{State::Starting};  // written by the loop task, read by render
  uint32_t shownFrame = 0;
  char errorDetail[GameVM::ERROR_CAPACITY] = {};
};
