#pragma once

#include <FrameBuffers.h>
#include <GameInput.h>
#include <HalMemory.h>
#include <LuaGame.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

#include "GameArena.h"
#include "GameAssets.h"
#include "GameRandom.h"

// The GameVM task and everything it touches (AD-5): the loaded sources, the arena,
// the frame buffers, the input queue, and the LuaGame. The task alone calls into
// Lua; it never takes RenderLock, calls ActivityManager, or touches Storage. The
// match posts input and reads frames, and destroys this object only after stop()
// returns true (or before start()).
class GameVM {
 public:
  static constexpr uint32_t TASK_STACK_BYTES = 16 * 1024;
  static constexpr int TASK_PRIORITY = 1;
  static constexpr int TASK_CORE = 1;

  // Takes the loaded sources and allocates the arena and both frame buffers in
  // PSRAM. Null (logged) when memory runs out.
  static std::unique_ptr<GameVM> create(GameAssets&& assets);

  GameVM(const GameVM&) = delete;
  GameVM& operator=(const GameVM&) = delete;

  // Starts the task, which runs setup and the first draw. False (logged) when the
  // task cannot be created.
  bool start();
  // Queues an input event for the task; a full queue drops its oldest event.
  void postInput(const GameScript::InputEvent& event);
  GameScript::FrameBuffers& frames() { return frameBuffers; }

  // The task has ended (after stop(), or on its own after a ScriptError).
  bool finished() const { return done.load(std::memory_order_acquire); }
  // Ended with a ScriptError; errorMessage() then holds Lua's message.
  bool failed() const { return finished() && scriptFailed.load(std::memory_order_acquire); }
  const char* errorMessage() const { return game.errorMessage(); }

  // Asks the task to quit after its current callback and waits up to timeoutMs for
  // it to end. True when it has ended (or never started).
  bool stop(uint32_t timeoutMs);

 private:
  GameVM(GameAssets&& assets, HalMemory::PsramBuffer frameStorage);
  static void taskEntry(void* param);
  void run();
  // Notifies the task only while it is alive: it deletes itself when it ends, and
  // a notification to a deleted task would touch freed memory.
  void notifyTask();

  GameAssets assets;
  GameArena arena;
  HalMemory::PsramBuffer frameStorage;
  GameScript::FrameBuffers frameBuffers;
  GameRandom random;
  GameScript::InputQueue queue;
  GameScript::LuaGame game;
  TaskHandle_t task = nullptr;
  std::mutex taskMutex;     // guards taskAlive against the task's exit
  bool taskAlive = false;   // true from start() until run() is about to end
  std::atomic<bool> quitRequested{false};
  std::atomic<bool> done{false};
  std::atomic<bool> scriptFailed{false};
};
