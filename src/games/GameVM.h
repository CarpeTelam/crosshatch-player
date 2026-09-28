#pragma once

#include <FrameBuffers.h>
#include <GameInput.h>
#include <HalMemory.h>
#include <LuaGame.h>
#include <SoloRounds.h>
#include <VmFailure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

#include "GameArena.h"
#include "GameAssets.h"
#include "GameClock.h"
#include "GameLog.h"
#include "GameRandom.h"

namespace GameScript {
class StoreSlot;
}

class FrameReplay;
class GameViewport;
class GfxRenderer;

// The GameVM task and everything it touches (AD-5): the loaded sources, the arena,
// the frame buffers, the input queue, the LuaGame with its clock and log, and, in
// the arena while the task runs, the solo GameCore::Session that drives it. The
// ch.store slot is the match's, borrowed. The task alone calls into Lua; it never
// takes RenderLock, calls ActivityManager, or touches Storage. The match posts
// input and reads frames, and destroys this object only after join() returns true
// (or before start()); otherwise it hands it to abandon().
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
  // PSRAM. The game sees `viewport`'s canvas size as ch.screen and measures
  // ch.text_width with `replay`'s text metrics (after FrameReplay::loadFonts);
  // `gameId` tags its log lines, and `store` (which must outlive the task, see
  // MatchStore) backs ch.store. Null (logged) when memory runs out.
  static std::unique_ptr<GameVM> create(GameAssets&& assets, const GameViewport& viewport, const FrameReplay& replay,
                                        const char* gameId, GameScript::StoreSlot& store);

  GameVM(const GameVM&) = delete;
  GameVM& operator=(const GameVM&) = delete;

  // Starts the task, which runs setup and the first draw. False (logged) when the
  // task cannot be created.
  bool start();
  // Queues an event for input(): a touch event (GameTouch.h), or pollTimer's Timer
  // event. A full queue drops its oldest event that is not a Timer (InputQueue),
  // with a log line.
  void postInput(const GameScript::InputEvent& event);
  // Loop task: queues a Timer event once ch.timer's pending timer is due (AD-23).
  // The VM drops it if the game re-armed or cancelled the timer meanwhile.
  void pollTimer();
  // Rounds that have ended so far: the VM counts one when the status turns over,
  // after the Session has delivered `over` and the round's last frame is
  // published. The match enters Over when the count moves (AD-21). Any task.
  uint32_t roundsEnded() const { return rounds.roundsEnded(); }
  // Rounds that have started so far: the VM counts one once the round's first
  // frame is published. After playAgain(), every frame published before this
  // count moves is the last round's (SoloRounds::roundsStarted). Any task.
  uint32_t roundsStarted() const { return rounds.roundsStarted(); }
  // Asks the VM for a new round (Play again): drops the queued events, and before
  // its next event the VM cancels the pending timer and runs Session::start() and
  // draw(), so ver keeps counting (GameScript::SoloRounds).
  void playAgain();
  // Frames published so far (0 before the first draw returns). Any task.
  uint32_t frameGen() const { return frameBuffers.frameGen(); }
  // Hands the front frame, with the largest refresh request of the frames
  // coalesced into it, to `replay` under the frame mutex. False when nothing was
  // drawn: before the first frame, or when replay skipped a frame identical to
  // the one on screen. Render task only.
  bool drawFront(const GfxRenderer& renderer, const GameViewport& viewport, FrameReplay& replay);

  // The task has ended (after stop(), or on its own after a ScriptError).
  bool finished() const { return done.load(std::memory_order_acquire); }
  // Ended with a ScriptError; failure() says whose.
  bool failed() const { return finished() && scriptFailed.load(std::memory_order_acquire); }
  // Why the VM failed, so the match words a host failure in tr() text (AD-14) and
  // never has to name GameScript; VmFailure.h says what each value means.
  using Failure = GameScript::VmFailure;
  using HostFailureTexts = GameScript::HostFailureTexts;
  // Returns before reading sessionOutOfMemory or game.hostFailure(): the VM task
  // writes both, and failed() acquires `done`, so they are safe to read only once
  // it is true. vmHealthy() calls this on every pass while the task still runs.
  Failure failure() const {
    if (!failed()) return Failure::None;
    return GameScript::vmFailure(true, sessionOutOfMemory, game.hostFailure());
  }
  // Failed before any game code ran: the error view's headline says it could not start.
  bool failedToStart() const { return GameScript::failedToStart(failure()); }
  // The error view's detail: `texts` (tr() text) for a host failure, errorMessage()
  // for the script's own.
  const char* failureDetail(const HostFailureTexts& texts) const {
    return GameScript::failureDetail(failure(), texts, errorMessage());
  }
  // The failure's English text, for the log; the error view shows it only for a
  // Script failure.
  const char* errorMessage() const { return sessionOutOfMemory ? "not enough memory" : game.errorMessage(); }

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
  // with inSwap clear, it is deleted and the arena, frame storage, and sources are
  // freed; the GameVM object itself is leaked, since the task may hold its mutexes.
  // If that never happens within ABANDON_WAIT_MS, or in the simulator (which
  // cannot stop a thread), all of it is leaked. Call from the loop task while the
  // render task is not reading frames (RenderLock held, as in onExit). Returns true
  // when the task is gone (ended or deleted); false when it may still run, and so
  // still post to the store slot, which the caller must then leak too.
  static bool abandon(std::unique_ptr<GameVM> vm);

 private:
  GameVM(GameAssets&& assets, HalMemory::PsramBuffer frameStorage, const GameScript::Canvas& canvas, const char* gameId,
         GameScript::StoreSlot& store);
  static void taskEntry(void* param);
  void run();
  // Notifies the task only while it is alive: it deletes itself when it ends, and
  // a notification to a deleted task would touch freed memory.
  void notifyTask();
  // Device only (the simulator cannot stop a thread): suspends the task, and
  // deletes it if it is inside Lua, outside a locked binding, and not swapping
  // frames; otherwise resumes it. True when deleted.
  bool deleteIfStuckInLua();

  GameAssets assets;
  GameArena arena;
  HalMemory::PsramBuffer frameStorage;
  GameScript::FrameBuffers frameBuffers;
  GameRandom random;
  GameClock clock;
  GameLog log;
  GameScript::InputQueue queue;
  GameScript::LuaGame game;
  GameScript::SoloRounds rounds{game.timer(), queue};
  TaskHandle_t task = nullptr;
  std::mutex taskMutex;    // guards taskAlive against the task's exit
  bool taskAlive = false;  // true from start() until run() is about to end
  std::atomic<bool> quitRequested{false};
  std::atomic<bool> done{false};
  std::atomic<bool> scriptFailed{false};
  // Set by the task before `done` when the Session did not fit in the arena.
  bool sessionOutOfMemory = false;
  // runningForMs's view of the current call (loop task only).
  uint32_t watchedCall = 0;
  uint32_t watchedSinceMs = 0;
};
