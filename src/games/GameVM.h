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

class FrameReplay;
class GameViewport;
class GfxRenderer;

// The GameVM task and everything it touches (AD-5): the loaded sources, the arena,
// the frame buffers, the input queue, the LuaGame, and, in the arena while the task
// runs, the solo GameCore::Session that drives it. The task alone calls into
// Lua; it never takes RenderLock, calls ActivityManager, or touches Storage. The
// match posts input and reads frames, and destroys this object only after join()
// returns true (or before start()); otherwise it hands it to abandon().
class GameVM {
 public:
  static constexpr uint32_t TASK_STACK_BYTES = GameScript::VM_STACK_BYTES;
  static constexpr int TASK_PRIORITY = 1;
  static constexpr int TASK_CORE = 1;
  // How long abandon() waits for the stuck task to be safely deletable.
  static constexpr uint32_t ABANDON_WAIT_MS = 500;
  // Size of errorMessage()'s buffer, for callers that copy it.
  static constexpr size_t ERROR_CAPACITY = GameScript::LuaGame::ERROR_CAPACITY;

  // Takes the loaded sources and allocates the arena and both frame buffers in
  // PSRAM; `canvas` is what the game sees as ch.screen and ch.text_width. Null
  // (logged) when memory runs out.
  static std::unique_ptr<GameVM> create(GameAssets&& assets, const GameScript::Canvas& canvas);

  GameVM(const GameVM&) = delete;
  GameVM& operator=(const GameVM&) = delete;

  // Starts the task, which runs setup and the first draw. False (logged) when the
  // task cannot be created.
  bool start();
  // Queues a tap at canvas (x, y) for input(); a full queue drops its oldest event.
  void postTap(int16_t x, int16_t y);
  // Frames published so far (0 before the first draw returns). Any task.
  uint32_t frameGen() const { return frameBuffers.frameGen(); }
  // Draws the front frame with `replay` under the frame mutex; false (nothing
  // drawn) before the first frame. Render task only.
  bool drawFront(const GfxRenderer& renderer, const GameViewport& viewport, const FrameReplay& replay);

  // The task has ended (after stop(), or on its own after a ScriptError).
  bool finished() const { return done.load(std::memory_order_acquire); }
  // Ended with a ScriptError; errorMessage() then holds Lua's message.
  bool failed() const { return finished() && scriptFailed.load(std::memory_order_acquire); }
  const char* errorMessage() const { return failure ? failure : game.errorMessage(); }

  // True while a callback runs in Lua; the match then skips its loop delay (AD-5).
  bool busy() const { return game.inLua(); }
  // How long the current call into Lua has run at nowMs (millis()), timed from the
  // first poll that saw it; 0 when idle. Each call is timed on its own, though one
  // input may make several (input, apply, status, over). Loop task only; the match
  // treats a call past its watchdog limit as a stuck script (AD-5).
  uint32_t runningForMs(uint32_t nowMs);

  // Sets the cancel flag, which the hook turns into Cancelled at the next hook
  // event, and asks the task to quit. Returns at once.
  void cancel();
  // Waits up to timeoutMs for the task to end. True when it has (or never started).
  bool join(uint32_t timeoutMs);
  // cancel(), then join(timeoutMs).
  bool stop(uint32_t timeoutMs);

  // For a VM whose join timed out (a script stuck inside a C library call). Once
  // the task is suspended inside Lua, outside a locked binding (enterLockedSection),
  // with inSwap clear, it is deleted and the
  // arena, frame storage, and sources are freed; the GameVM object itself is
  // leaked, since the task may hold its mutexes. If that never happens within
  // ABANDON_WAIT_MS, or in the simulator (which cannot stop a thread), all of it
  // is leaked. Call from the loop task while the render task is not reading
  // frames (RenderLock held, as in onExit).
  static void abandon(std::unique_ptr<GameVM> vm);

 private:
  GameVM(GameAssets&& assets, HalMemory::PsramBuffer frameStorage, const GameScript::Canvas& canvas);
  static void taskEntry(void* param);
  void run();
  void postInput(const GameScript::InputEvent& event);
  // Notifies the task only while it is alive: it deletes itself when it ends, and
  // a notification to a deleted task would touch freed memory.
  void notifyTask();
  // Device only (the simulator cannot stop a thread): suspends the task, and
  // deletes it if it is inside Lua, outside a locked binding, and not swapping
  // frames; otherwise resumes it.
  // True when deleted.
  bool deleteIfStuckInLua();

  GameAssets assets;
  GameArena arena;
  HalMemory::PsramBuffer frameStorage;
  GameScript::FrameBuffers frameBuffers;
  GameRandom random;
  GameScript::InputQueue queue;
  GameScript::LuaGame game;
  TaskHandle_t task = nullptr;
  std::mutex taskMutex;    // guards taskAlive against the task's exit
  bool taskAlive = false;  // true from start() until run() is about to end
  std::atomic<bool> quitRequested{false};
  std::atomic<bool> done{false};
  std::atomic<bool> scriptFailed{false};
  // Set (to a literal) when the VM fails outside Lua; errorMessage() then shows it.
  const char* failure = nullptr;
  // runningForMs's view of the current call (loop task only).
  uint32_t watchedCall = 0;
  uint32_t watchedSinceMs = 0;
};
