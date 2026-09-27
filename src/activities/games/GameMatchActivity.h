#pragma once

#include <LuaGame.h>
#include <Manifest.h>

#include <atomic>
#include <cstdint>
#include <memory>

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "games/FrameReplay.h"
#include "games/GameViewport.h"

class GameVM;

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

 private:
  enum class State : uint8_t { Starting, Playing, Error };
  // How long onExit waits for the VM to finish its current callback and end.
  static constexpr uint32_t STOP_TIMEOUT_MS = 500;

  void showError(const char* detail);
  void renderError();

  GameCore::Manifest manifest;
  GameViewport viewport;
  FrameReplay replay;
  std::unique_ptr<GameVM> vm;
  std::atomic<State> state{State::Starting};  // written by the loop task, read by render
  uint32_t shownFrame = 0;
  char errorDetail[GameScript::LuaGame::ERROR_CAPACITY] = {};
};
