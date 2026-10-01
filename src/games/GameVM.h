#pragma once

#include <FrameBuffers.h>
#include <GameInput.h>
#include <HalMemory.h>
#include <LuaGame.h>
#include <Roster.h>
#include <SeatShown.h>
#include <SoloRounds.h>
#include <VmFailure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>

#include "GameArena.h"
#include "GameAssets.h"
#include "GameClock.h"
#include "GameLog.h"
#include "GameRandom.h"
#include "SnapshotMailbox.h"

namespace GameScript {
class StoreSlot;
}

class FrameReplay;
class GameViewport;
class GfxRenderer;

// The GameVM task and everything it touches (AD-5): the loaded sources, the arena,
// the frame buffers, the input queue, the LuaGame with its clock and log, and, in
// the arena while the task runs, the match's GameCore::Session that drives it. The
// ch.store slot is the match's, borrowed. The task alone calls into Lua; it never
// takes RenderLock, calls ActivityManager, or touches Storage. The match posts
// input and reads frames, and destroys this object only after join() returns true
// (or before start()); otherwise it hands it to abandon(). Each snapshot the Session
// commits goes to the loop task through committed() (latest wins), which writes
// resume.bin; setResume() starts the Session from a saved one instead of setup.
//
// A hidden pass match's VM (create's `handOff`) draws only the seat the match asks for, so no
// frame of one seat is ever published while the device is on its way to another: nothing after
// a round begins (the match shows the blank hand-off screen), the turn seat once the match asks
// with showTurnSeat(), and, after a move that passes the turn, the mover again, which it counts
// (turnsPassed) so the match enters Result. Meanwhile it holds a timer that falls due until the
// next seat is shown. Solo and open pass VMs run SoloRounds' start, restart, and step as before.
class GameVM {
 public:
  static constexpr uint32_t TASK_STACK_BYTES = GameScript::VM_STACK_BYTES;
  static constexpr int TASK_PRIORITY = 1;
  static constexpr int TASK_CORE = 1;
  // How often join() and abandon() poll. A wait ends late by its last iteration: one poll,
  // and in abandon() also deleteIfStuckInLua's settle of up to 10 ticks and its wait for
  // taskMutex. The forced exit's recorded bound (docs/crosshatch/game-canvas.md) assumes 5.
  static constexpr uint32_t STOP_POLL_MS = 5;
  // How long abandon() waits, counted in millis() from when it began, for the stuck
  // task to be safely deletable (late by its last iteration; see STOP_POLL_MS).
  static constexpr uint32_t ABANDON_WAIT_MS = 500;
  // Size of errorMessage()'s buffer, for callers that copy it.
  static constexpr size_t ERROR_CAPACITY = GameScript::LuaGame::ERROR_CAPACITY;

  // Takes the loaded sources and allocates the arena and both frame buffers (and the
  // snapshot mailbox's storage after them) in PSRAM. The game sees `viewport`'s canvas
  // size as ch.screen and measures ch.text_width with `replay`'s text metrics (after
  // FrameReplay::loadFonts); `gameId` tags its log lines, `store` (which must outlive
  // the task, see MatchStore) backs ch.store, and the Session plays `roster` (solo, or a
  // pass match). `handOff` is a hidden pass match's (the class comment). Null (logged) when
  // memory runs out.
  static std::unique_ptr<GameVM> create(GameAssets&& assets, const GameViewport& viewport, const FrameReplay& replay,
                                        const char* gameId, GameScript::StoreSlot& store,
                                        const GameCore::Roster& roster, bool handOff = false);

  GameVM(const GameVM&) = delete;
  GameVM& operator=(const GameVM&) = delete;

  // Starts the task, which runs setup and the first draw. False (logged) when the
  // task cannot be created.
  bool start();
  // Before start(): the saved snapshot (a saved match's, `ver` its u16) the Session
  // restores in place of setup (GameCore::Session::restore), kept in the mailbox's
  // storage until the task reads it. False (nothing changes) when it is empty or larger
  // than GameCore::SNAPSHOT_BYTES.
  bool setResume(std::span<const uint8_t> snapshot, uint16_t ver);
  // The snapshots the VM has committed, latest wins, for the loop task to save (loop
  // task: SnapshotMailbox::take, through GameSaveStore::flushResume). The VM publishes
  // each new ver once, before it logs the round, and none for a restored start. Lives
  // as long as this object: the match saves what is pending before it frees or abandons
  // the VM.
  SnapshotMailbox& committed() { return mailbox; }
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

  // ---- a hidden pass match (create's `handOff`); all 0 for any other ----

  // Loop task: asks the VM to draw the turn seat (the one that takes the device after the
  // hand-off), ahead of any queued event, and returns the request's number. A request made
  // after playAgain() is served in the new round.
  uint32_t showTurnSeat();
  // The last request whose frame is published: once it reaches a request's number, the front
  // frame is that seat's (or a later one of the same seat; no other is drawn before the next
  // turn change). Any task.
  uint32_t seatShownRequest() const { return seatServed.load(std::memory_order_acquire); }
  // Moves that passed the turn so far: the VM counts one after it has drawn the mover's frame
  // again. Any task.
  uint32_t turnsPassed() const { return turnChanges.load(std::memory_order_acquire); }
  // The turn seat the last counted move passed to; read after turnsPassed() moved. Any task.
  uint8_t passedTo() const { return nextSeat.load(std::memory_order_acquire); }
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
  // Failed before any game code ran (any host failure), so the error view's
  // headline says the game could not start (AD-14; GameScript::failedToStart).
  bool failedToStart() const { return GameScript::failedToStart(failure()); }
  // The error view's detail: `texts` (tr() text) for a host failure, errorMessage()
  // for the script's own. Gated like failure(), through both.
  const char* failureDetail(const HostFailureTexts& texts) const {
    return GameScript::failureDetail(failure(), texts, errorMessage());
  }
  // The failure's English text, for the log; the error view shows it only for a
  // Script failure. "" until failed(), for the same reason as failure(): the VM
  // task writes what failureText() reads.
  const char* errorMessage() const { return failed() ? failureText() : ""; }

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
  // Waits for the task to end, polling every STOP_POLL_MS, until timeoutMs of millis()
  // have passed since it began. It returns late by its last iteration (a poll), and
  // so at most a poll after timeoutMs unless a poll itself is delayed by the scheduler.
  // True when it has ended (or never started).
  bool join(uint32_t timeoutMs);
  // cancel(), then join(timeoutMs).
  bool stop(uint32_t timeoutMs);

  // For a VM whose join timed out (a script stuck inside a C library call). Once
  // the task is suspended inside Lua, outside a locked binding (enterLockedSection),
  // with inSwap clear, it is deleted and the arena, frame storage, and sources are
  // freed; the GameVM object itself is leaked, since the task may hold its mutexes.
  // If that never happens within ABANDON_WAIT_MS of millis(), or in the simulator (which
  // cannot stop a thread), all of it is leaked. Call from the loop task while the
  // render task is not reading frames (RenderLock held, as in onExit). Returns true
  // when the task is gone (ended or deleted); false when it may still run, and so
  // still post to the store slot, which the caller must then leak too. A task that ends
  // within the wait may have published a last snapshot after the caller's own flush, so
  // `beforeDelete(vm, user)`, if given, runs on it before it is deleted.
  static bool abandon(std::unique_ptr<GameVM> vm, void (*beforeDelete)(GameVM&, void*) = nullptr, void* user = nullptr);

 private:
  GameVM(GameAssets&& assets, HalMemory::PsramBuffer frameStorage, const GameScript::Canvas& canvas, const char* gameId,
         GameScript::StoreSlot& store, const GameCore::Roster& roster, bool handOff);
  static void taskEntry(void* param);
  void run();
  // ---- a hidden pass match's steps (VM task) ----
  // Draws the seat GameCore::seatShown names for handOffView, the status, and mover; nothing
  // (Ok) for NO_SEAT, the hand-off.
  GameScript::Outcome drawShown();
  // Serves seatTaken: the events still queued are played under the old view first (each was
  // posted before the hand-off), then the view becomes Playing and the turn seat is drawn; the
  // request counts as served once that frame is published. Then a held timer is delivered, to
  // that seat.
  GameScript::Outcome showSeatNow();
  // One event in the hand-off flow: a stale timer is dropped; in Result or HandOff a timer is
  // held and, in HandOff, every other event dropped; otherwise the event goes to the seat
  // shown, and a move that passes the turn makes the view Result with that seat as the mover.
  GameScript::Outcome stepHandOff(const GameScript::InputEvent& event);
  // errorMessage() without its gate: for run(), the task that writes it, which
  // logs it before it publishes `done`.
  const char* failureText() const { return sessionOutOfMemory ? "not enough memory" : game.errorMessage(); }
  // Notifies the task only while it is alive: it deletes itself when it ends, and
  // a notification to a deleted task would touch freed memory.
  void notifyTask();
  // Device only (the simulator cannot stop a thread): suspends the task, and
  // deletes it if it is inside Lua, outside a locked binding, and not swapping
  // frames; otherwise resumes it. True when deleted.
  bool deleteIfStuckInLua();

  GameAssets assets;
  // Who plays: the Session run() builds is this roster's.
  GameCore::Roster roster;
  GameArena arena;
  HalMemory::PsramBuffer frameStorage;
  GameScript::FrameBuffers frameBuffers;
  // Over the last SNAPSHOT_BYTES of frameStorage, after the two frames.
  SnapshotMailbox mailbox;
  GameRandom random;
  GameClock clock;
  GameLog log;
  GameScript::InputQueue queue;
  GameScript::LuaGame game;
  GameScript::SoloRounds rounds{game.timer(), queue};
  // A hidden pass match's VM (create's `handOff`); fixed for its life.
  const bool handOff;
  // VM task, a hidden pass match only: the Session run() plays; the state whose seat the VM
  // draws (HandOff: none, Playing: the turn seat, Result: the mover); the seat that last
  // passed the turn; a timer event held until the next seat is shown; and the request being
  // served (seatTaken, pending until drawn).
  GameCore::Session* playing = nullptr;
  GameCore::MatchState handOffView = GameCore::MatchState::HandOff;
  uint8_t mover = GameCore::NO_SEAT;
  bool timerHeld = false;
  GameScript::InputEvent heldTimer;
  uint32_t seatTaken = 0;
  bool seatPending = false;
  // Loop task to VM task: showTurnSeat()'s requests. VM task to any: the request served,
  // the moves that passed the turn, and the seat the last one passed to (stored first).
  std::atomic<uint32_t> seatRequests{0};
  std::atomic<uint32_t> seatServed{0};
  std::atomic<uint32_t> turnChanges{0};
  std::atomic<uint8_t> nextSeat{0};
  TaskHandle_t task = nullptr;
  std::mutex taskMutex;    // guards taskAlive against the task's exit
  bool taskAlive = false;  // true from start() until run() is about to end
  std::atomic<bool> quitRequested{false};
  std::atomic<bool> done{false};
  std::atomic<bool> scriptFailed{false};
  // setResume's snapshot, in the mailbox's storage: written before the task starts
  // and read by it before its first publish.
  size_t resumeLength = 0;
  uint16_t resumeVer = 0;
  // Set by the task before `done` when the Session did not fit in the arena.
  bool sessionOutOfMemory = false;
  // runningForMs's view of the current call (loop task only).
  uint32_t watchedCall = 0;
  uint32_t watchedSinceMs = 0;
};
