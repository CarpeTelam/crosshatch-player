#if FREEINK_CAP_GAMES

#include "GameVM.h"

#include <Arduino.h>
#include <DisplayList.h>
#include <Logging.h>
#include <SeatShown.h>
#include <Session.h>

#include <cstring>
#include <new>
#include <utility>

#include "FrameReplay.h"
#include "GameViewport.h"

namespace {

// VM task, after a start, restart, or step that ended `outcome`: hands the Session's snapshot
// to the loop task when its ver moved since `published`, which it then holds. An Ok outcome
// always publishes. A Cancelled one publishes only when the snapshot's status was computed
// (Session::settledVer), since otherwise status().over is the previous snapshot's and the
// snapshot may never have been accepted by the game; a ScriptError never does.
void publishCommitted(SnapshotMailbox& mailbox, const GameCore::Session& session, uint32_t& published,
                      const GameScript::Outcome outcome) {
  const uint32_t ver = session.ver();
  if (ver == published) return;
  const bool settled = session.settledVer() == ver;
  if (outcome == GameScript::Outcome::ScriptError || (outcome == GameScript::Outcome::Cancelled && !settled)) return;
  published = ver;
  mailbox.publish(session.snapshot(), ver, session.status().over);
}

// VM task, after a round started (`started`) or an event: logs the start, and an
// end counted since `endedBefore`.
void logRound(const GameCore::Session& session, const GameScript::MatchRounds& rounds, const uint32_t endedBefore,
              const bool started) {
  if (started) LOG_INF("GAME", "Round started at ver %u", static_cast<unsigned>(session.ver()));
  if (rounds.roundsEnded() != endedBefore) {
    LOG_INF("GAME", "Round over at ver %u; winners mask 0x%x", static_cast<unsigned>(session.ver()),
            static_cast<unsigned>(session.status().winners));
  }
}

}  // namespace

std::unique_ptr<GameVM> GameVM::create(GameAssets&& assets, const GameViewport& viewport, const FrameReplay& replay,
                                       const char* gameId, GameScript::StoreSlot& store, const GameCore::Roster& roster,
                                       const bool handOff, const GameCore::SettingValues& settings) {
  const GameScript::Canvas canvas{static_cast<int16_t>(viewport.width()), static_cast<int16_t>(viewport.height()),
                                  replay.textMetrics()};
  // The two frames, then the mailbox's one snapshot: one block, since the mailbox lives
  // as long as the frames and a second PSRAM block would only fragment.
  constexpr size_t frameBytes = 2 * GameScript::MAX_BYTES + GameCore::SNAPSHOT_BYTES;
  auto frameStorage = HalMemory::allocatePsram(frameBytes);
  if (!frameStorage) {
    LOG_ERR("GAME", "OOM: %u bytes of PSRAM for frame buffers and the snapshot mailbox",
            static_cast<unsigned>(frameBytes));
    return nullptr;
  }
  // The constructor is private, so makeUniqueNoThrow cannot reach it; the unique_ptr
  // owns the nothrow allocation at once.
  std::unique_ptr<GameVM> vm(new (std::nothrow) GameVM(std::move(assets), std::move(frameStorage), canvas, gameId,
                                                       store, roster, handOff, settings));
  if (!vm) {
    LOG_ERR("GAME", "OOM: %u byte GameVM", static_cast<unsigned>(sizeof(GameVM)));
    return nullptr;
  }
  if (!vm->arena.allocate()) return nullptr;
  return vm;
}

GameVM::GameVM(GameAssets&& loaded, HalMemory::PsramBuffer storage, const GameScript::Canvas& canvas,
               const char* gameId, GameScript::StoreSlot& store, const GameCore::Roster& roster, const bool handOff,
               const GameCore::SettingValues& settings)
    : assets(std::move(loaded)),
      roster(roster),
      frameStorage(std::move(storage)),
      frameBuffers(frameStorage.get(), frameStorage.get() + GameScript::MAX_BYTES, GameScript::MAX_BYTES),
      mailbox({frameStorage.get() + 2 * GameScript::MAX_BYTES, GameCore::SNAPSHOT_BYTES}),
      log(gameId),
      game(arena.allocator(), frameBuffers, assets.sources(), GameScript::HostPorts{random, clock, log, store, &paused},
           canvas, assets.images()),
      handOff(handOff) {
  game.setSettings(settings);
}

bool GameVM::start() {
  {
    std::lock_guard<std::mutex> lock(taskMutex);
    taskAlive = true;
  }
  xTaskCreatePinnedToCore(&GameVM::taskEntry, "GameVM", TASK_STACK_BYTES, this, TASK_PRIORITY, &task, TASK_CORE);
  if (!task) {
    std::lock_guard<std::mutex> lock(taskMutex);
    taskAlive = false;
    LOG_ERR("GAME", "Cannot create the GameVM task (%u byte stack)", static_cast<unsigned>(TASK_STACK_BYTES));
    return false;
  }
  return true;
}

bool GameVM::setResume(const std::span<const uint8_t> snapshot, const uint16_t ver) {
  if (snapshot.empty() || snapshot.size() > GameCore::SNAPSHOT_BYTES) return false;
  std::memcpy(mailbox.storage().data(), snapshot.data(), snapshot.size());
  resumeLength = snapshot.size();
  resumeVer = ver;
  return true;
}

void GameVM::taskEntry(void* param) {
  static_cast<GameVM*>(param)->run();
  // run() has published `done`, after which the owner may free the object; touch
  // nothing of it here. In the simulator this is a no-op and the thread returns.
  vTaskDelete(nullptr);
}

void GameVM::run() {
  using GameScript::Outcome;
  // The hook keeps STACK_HEADROOM_BYTES free above this address. The simulator's
  // thread has a large stack, so it models the task's 16 KiB from here.
#if defined(SIMULATOR)
  const uintptr_t stackFloor = reinterpret_cast<uintptr_t>(__builtin_frame_address(0)) - TASK_STACK_BYTES;
#else
  const auto stackFloor = reinterpret_cast<uintptr_t>(pxTaskGetStackStart(nullptr));
#endif
  game.setStackFloor(stackFloor);
  // The Session and (in LuaGame::load) the codec scratch come from the arena's
  // reserve, which Lua's region never touches, so Lua can never starve either.
  GameScript::ArenaAllocator& heap = arena.allocator();
  GameCore::Session* session = heap.create<GameCore::Session>(roster, game);
  Outcome outcome = Outcome::ScriptError;
  if (session) {
    outcome = game.load();
  } else {
    sessionOutOfMemory = true;
  }
  // The ver last handed to the loop task: a restored one was saved already.
  uint32_t published = 0;
  if (outcome == Outcome::Ok) {
    if (resumeLength != 0) {
      // The seed sits in the mailbox's storage; it is read here, before the first publish.
      const std::span<const uint8_t> seed = mailbox.storage().first(resumeLength);
      if (session->restore(seed, resumeVer)) {
        published = session->ver();
        LOG_INF("GAME", "Resuming at ver %u", static_cast<unsigned>(published));
      } else {
        LOG_ERR("GAME", "Cannot restore a %u-byte snapshot; starting a new round", static_cast<unsigned>(resumeLength));
      }
    }
    if (handOff) {
      // A hidden pass match draws no seat until the match asks (showTurnSeat): the device is
      // on its way to the first mover, and the round counts as started at that first draw.
      playing = session;
      handOffView = GameCore::MatchState::HandOff;
      outcome = rounds.begin(*session);
      announceTurnSeat(outcome);
    } else {
      outcome = rounds.start(*session);
      if (outcome == Outcome::Ok) noteSeatDrawn(rounds.shownSeat());
    }
    publishCommitted(mailbox, *session, published, outcome);
    // A hidden round has drawn nothing yet: it logs its start at its first seat's frame.
    if (outcome == Outcome::Ok) logRound(*session, rounds, 0, !handOff);
  }
  while (outcome == Outcome::Ok && !quitRequested.load(std::memory_order_acquire)) {
    const uint32_t endedBefore = rounds.roundsEnded();
    const uint32_t startedBefore = rounds.roundsStarted();
    if (handOff) {
      // Read before takePlayAgain(): the match asks for Play again before it can ask for the
      // new round's seat, so a request seen here with Play again is served in the new round.
      const uint32_t requested = seatRequests.load(std::memory_order_acquire);
      if (requested != seatTaken) {
        seatTaken = requested;
        seatPending = true;
      }
    }
    if (rounds.takePlayAgain()) {
      if (handOff) {
        // The new round begins on the hand-off screen, as the first did; a held timer was the
        // last round's (beginAgain cancels the pending one).
        handOffView = GameCore::MatchState::HandOff;
        timerHeld = false;
        outcome = rounds.beginAgain();
        announceTurnSeat(outcome);
      } else {
        outcome = rounds.restart();
        if (outcome == Outcome::Ok) noteSeatDrawn(rounds.shownSeat());
      }
      publishCommitted(mailbox, *session, published, outcome);
      if (outcome == Outcome::Ok) logRound(*session, rounds, endedBefore, !handOff);
      continue;
    }
    if (seatPending) {
      // Ahead of the queue: the next seat's frame, then what was queued (or held) for it.
      seatPending = false;
      outcome = showSeatNow();
      publishCommitted(mailbox, *session, published, outcome);
      if (outcome == Outcome::Ok) logRound(*session, rounds, endedBefore, rounds.roundsStarted() != startedBefore);
      continue;
    }
    GameScript::InputEvent event;
    if (!queue.pop(event)) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      continue;
    }
    if (handOff) {
      outcome = stepHandOff(event);
    } else {
      // A touch made under another seat's frame is dropped before the game sees it (postInput); a timer goes on.
      if (madeUnderAnotherSeat(event)) continue;
      // A timer due after the round is over (any mode) is dropped by step with no draw; a stale one silently.
      if (GameScript::MatchRounds::lateTimer(event, session->status().over) && game.timer().accepts(event)) {
        LOG_DBG("GAME", "Dropped a timer due after the round was over");
        continue;
      }
      outcome = rounds.step(event);
      // The seat step drew, when it drew (a stale timer draws nothing and leaves the seat as it was).
      if (outcome == Outcome::Ok) noteSeatDrawn(rounds.shownSeat());
    }
    publishCommitted(mailbox, *session, published, outcome);
    if (outcome == Outcome::Ok) {
      logRound(*session, rounds, endedBefore, handOff && rounds.roundsStarted() != startedBefore);
    }
  }
  if (outcome == Outcome::ScriptError) {
    LOG_ERR("LUA", "Script error: %s", failureText());
    scriptFailed.store(true, std::memory_order_release);
  } else if (outcome == Outcome::Cancelled) {
    LOG_INF("GAME", "VM cancelled");
  }
  game.close();
  playing = nullptr;
  heap.destroy(session);
  const uintptr_t deepest = game.callGuard().deepestAddress();
  const unsigned hookHeadroom =
      deepest != UINTPTR_MAX && deepest > stackFloor ? static_cast<unsigned>(deepest - stackFloor) : 0;
  LOG_INF("GAME", "VM stopped; arena peak %u bytes, stack high-water %u bytes free, least at a hook %u bytes",
          static_cast<unsigned>(arena.allocator().peakBytes()),
          static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)), hookHeadroom);
  {
    std::lock_guard<std::mutex> lock(taskMutex);
    taskAlive = false;
  }
  done.store(true, std::memory_order_release);
}

void GameVM::playAgain() {
  rounds.requestPlayAgain();
  notifyTask();
}

uint32_t GameVM::showTurnSeat() {
  const uint32_t request = seatRequests.fetch_add(1, std::memory_order_acq_rel) + 1;
  notifyTask();
  return request;
}

GameScript::Outcome GameVM::drawShown() {
  const uint8_t seat = GameCore::seatShown(handOffView, roster, playing->status(), mover);
  // The hand-off (or a Result without a mover this device plays): no seat's frame.
  if (seat == GameCore::NO_SEAT) return GameScript::Outcome::Ok;
  const GameScript::Outcome outcome = rounds.draw(seat);
  if (outcome == GameScript::Outcome::Ok) noteSeatDrawn(seat);
  return outcome;
}

GameScript::Outcome GameVM::showSeatNow() {
  // Every event still queued was posted before this seat's frame is on the panel (the match posts no game input until
  // then, only timers), so it is played under the view it was meant for: a mover's late tap reaches the mover, its
  // move discarded, or on the hand-off screen nobody, and a timer is held. None reaches the next seat.
  GameScript::InputEvent queued;
  while (queue.pop(queued)) {
    const GameScript::Outcome drained = stepHandOff(queued);
    if (drained != GameScript::Outcome::Ok) return drained;
  }
  // A turn seat this device does not play (a roster with some seats local, not all): no seat takes the device, so the
  // view stays HandOff and the request stays unserved, and the match keeps the blank on the panel.
  if (GameCore::seatShown(GameCore::MatchState::Playing, roster, playing->status(), mover) == GameCore::NO_SEAT) {
    LOG_INF("GAME", "Turn seat %u is not this device's; no seat shown", static_cast<unsigned>(playing->status().turn));
    return GameScript::Outcome::Ok;
  }
  handOffView = GameCore::MatchState::Playing;
  GameScript::Outcome outcome = drawShown();
  if (outcome != GameScript::Outcome::Ok) return outcome;
  // After the publish: a render that sees the request served takes this seat's frame or a later one.
  seatServed.store(seatTaken, std::memory_order_release);
  if (!timerHeld) return outcome;
  // A timer that fell due on the way to this seat is this seat's, right after its first frame.
  timerHeld = false;
  return stepHandOff(heldTimer);
}

GameScript::Outcome GameVM::stepHandOff(GameScript::InputEvent event) {
  // A stale timer is dropped before anything, with no draw (as MatchRounds::step drops it).
  if (!game.timer().accepts(event)) return GameScript::Outcome::Ok;
  if (handOffView != GameCore::MatchState::Playing) {
    if (event.kind == GameCore::EventKind::Timer) {
      // Held for the next seat: the seat on screen is done, and no seat is shown in HandOff.
      timerHeld = true;
      heldTimer = event;
      return GameScript::Outcome::Ok;
    }
    // No seat holds the device: nothing reads its input.
    if (handOffView == GameCore::MatchState::HandOff) return GameScript::Outcome::Ok;
  }
  // Playing: the turn seat (seat 0 once over); Result: the mover, whose move the Session
  // discards (it is not the turn seat), so a tap queued behind a turn-passing move changes
  // only that seat's own view.
  const uint8_t seat = GameCore::seatShown(handOffView, roster, playing->status(), mover);
  // A timer due after the round is over is dropped, never delivered, whichever seat is shown (status.over, not seat 0:
  // a roster with no local seat shows none and is dropped the same way, and logged).
  if (GameScript::MatchRounds::lateTimer(event, playing->status().over)) {
    LOG_DBG("GAME", "Dropped a timer due after the round was over");
    return GameScript::Outcome::Ok;
  }
  // Fails closed, as seatShown does: no seat this device plays, so no input is read.
  if (seat == GameCore::NO_SEAT) return GameScript::Outcome::Ok;
  // A touch made under another seat's frame never reaches this one: seat 0's after the move that ended the round, or
  // the next seat's (postInput). The mover's own late tap in Result was made under the mover's frame and goes on.
  if (madeUnderAnotherSeat(event)) return GameScript::Outcome::Ok;
  const bool wasPlaying = handOffView == GameCore::MatchState::Playing;
  GameScript::Outcome outcome = rounds.play(event, seat);
  if (outcome != GameScript::Outcome::Ok) return outcome;
  const GameCore::Status& status = playing->status();
  // A move that ends the round is RoundOver's (seat 0 is drawn), never a turn change.
  const bool passed = wasPlaying && !status.over && status.turn != seat;
  if (passed) {
    handOffView = GameCore::MatchState::Result;
    mover = seat;
  }
  outcome = drawShown();
  if (outcome != GameScript::Outcome::Ok || !passed) return outcome;
  // After the mover's frame is published, so the match's Result shows it; the seat first.
  nextSeat.store(status.turn, std::memory_order_release);
  turnChanges.fetch_add(1, std::memory_order_acq_rel);
  return outcome;
}

void GameVM::announceTurnSeat(const GameScript::Outcome outcome) {
  if (outcome != GameScript::Outcome::Ok) return;
  // The seat first, as stepHandOff stores it before it counts a turn change: a reader that sees the count moved reads
  // this round's first seat. The round has begun (setup or the restored save, then status), so its turn seat is known
  // before any seat is drawn; a round already over names no seat (turn 0).
  nextSeat.store(playing->status().turn, std::memory_order_release);
  roundAnnouncements.fetch_add(1, std::memory_order_acq_rel);
}

void GameVM::noteSeatDrawn(const uint8_t seat) {
  if (seat == drawnSeat) return;
  drawnSeat = seat;
  // The VM task alone publishes, so this is the frame just drawn.
  seatFrame = frameBuffers.frameGen();
}

bool GameVM::madeUnderAnotherSeat(GameScript::InputEvent& event) {
  if (event.kind == GameCore::EventKind::Timer) return false;  // its serial is the timer's own (GameTimer::accepts)
  const uint32_t shownFrame = event.serial;
  event.serial = 0;
  if (shownFrame == UNTAGGED) return false;
  // Wrap-safe: frame numbers count up from the seat's first frame, so a difference read as signed says which came
  // first even after the count wraps.
  if (static_cast<int32_t>(shownFrame - seatFrame) >= 0) return false;
  LOG_INF("GAME", "Dropped a touch made under frame %u, before seat %u's first frame %u",
          static_cast<unsigned>(shownFrame), static_cast<unsigned>(drawnSeat), static_cast<unsigned>(seatFrame));
  return true;
}

uint32_t GameVM::runningForMs(const uint32_t nowMs) {
  if (!busy()) return 0;
  const uint32_t call = game.callSerial();
  if (call != watchedCall) {
    watchedCall = call;
    watchedSinceMs = nowMs;
  }
  return nowMs - watchedSinceMs;
}

void GameVM::pollTimer() {
  GameScript::InputEvent event;
  if (game.timer().takeDueEvent(clock.nowMs(), event)) postInput(event);
}

bool GameVM::drawFront(const GfxRenderer& renderer, const GameViewport& viewport, FrameReplay& replay,
                       uint32_t* takenFrame) {
  if (frameBuffers.frameGen() == 0) return false;
  bool drawn = false;
  frameBuffers.takeFront([&](const GameScript::DisplayList& frame, const GameScript::Refresh hint) {
    // Under the frame mutex, which publish holds while it counts a frame: this is the front frame's number.
    if (takenFrame) *takenFrame = frameBuffers.frameGen();
    drawn = replay.draw(renderer, viewport, frame, hint, assets.images());
  });
  return drawn;
}

void GameVM::postInput(const GameScript::InputEvent& event, const uint32_t shownFrame) {
  GameScript::InputEvent posted = event;
  // A touch's serial is unused (only a Timer's is read), so it carries the frame it was made under to the VM task.
  if (posted.kind != GameCore::EventKind::Timer) posted.serial = shownFrame;
  if (queue.push(posted)) LOG_INF("GAME", "Input queue full; dropped the oldest non-timer event");
  notifyTask();
}

void GameVM::notifyTask() {
  std::lock_guard<std::mutex> lock(taskMutex);
  if (taskAlive) xTaskNotify(task, 1, eIncrement);
}

void GameVM::cancel() {
  quitRequested.store(true, std::memory_order_release);
  game.requestCancel();
  notifyTask();
}

bool GameVM::join(const uint32_t timeoutMs) {
  if (!task) return true;
  // The time is the clock's, not the polls': a poll can take longer than STOP_POLL_MS (a
  // busy core, a tick coarser than the poll), and sleep waits for this under RenderLock.
  const uint32_t began = millis();
  while (!finished()) {
    if (millis() - began >= timeoutMs) return false;
    vTaskDelay(pdMS_TO_TICKS(STOP_POLL_MS));
  }
  return true;
}

bool GameVM::stop(const uint32_t timeoutMs) {
  cancel();
  return join(timeoutMs);
}

bool GameVM::abandon(std::unique_ptr<GameVM> vm, void (*beforeDelete)(GameVM&, void*), void* user) {
  if (!vm) return true;
  // Leaked from here on unless the task ends after all: it may hold this object's
  // mutexes (input queue, task mutex), and deleting a task releases nothing it holds.
  GameVM* stuck = vm.release();
#if defined(SIMULATOR)
  // The shim has no vTaskSuspend, and its vTaskDelete only detaches the thread,
  // which would keep running in the arena; waiting cannot help.
  if (stuck->finished()) {
    if (beforeDelete) beforeDelete(*stuck, user);
    delete stuck;
    return true;
  }
  LOG_ERR("GAME", "VM stuck; the simulator cannot stop its thread, so all of it is leaked");
  return false;
#else
  // Counted in millis(), as join is: an iteration is a poll plus deleteIfStuckInLua's settle.
  const uint32_t began = millis();
  while (millis() - began < ABANDON_WAIT_MS) {
    if (stuck->finished()) {
      // It ended after the caller's last look, perhaps publishing a snapshot on the way out.
      if (beforeDelete) beforeDelete(*stuck, user);
      delete stuck;
      return true;
    }
    if (stuck->deleteIfStuckInLua()) {
      // The Lua state lives in the arena, and nothing else points into it.
      stuck->game.abandon();
      stuck->arena.release();
      stuck->frameStorage.reset();
      stuck->assets.release();
      LOG_ERR("GAME", "Abandoned the stuck VM: freed its PSRAM, leaked %u bytes",
              static_cast<unsigned>(sizeof(GameVM)));
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(STOP_POLL_MS));
  }
  LOG_ERR("GAME", "VM stuck and not safely deletable; leaking all of it");
  return false;
#endif
}

#if !defined(SIMULATOR)
bool GameVM::deleteIfStuckInLua() {
  // Held throughout, so the task cannot reach its own vTaskDelete meanwhile.
  std::lock_guard<std::mutex> lock(taskMutex);
  if (!taskAlive) return false;  // ending on its own
  vTaskSuspend(task);
  // A task running on the other core stops once that core takes the yield.
  constexpr int settleTicks = 10;
  for (int i = 0; i < settleTicks && eTaskGetState(task) == eRunning; ++i) vTaskDelay(1);
  if (eTaskGetState(task) == eSuspended && game.inLua() && !game.inLockedBinding() && !frameBuffers.inSwap()) {
    vTaskDelete(task);
    taskAlive = false;
    return true;
  }
  vTaskResume(task);
  return false;
}
#endif

#endif  // FREEINK_CAP_GAMES
