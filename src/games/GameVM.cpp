#if FREEINK_CAP_GAMES

#include "GameVM.h"

#include <Arduino.h>
#include <DisplayList.h>
#include <Logging.h>
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
void logRound(const GameCore::Session& session, const GameScript::SoloRounds& rounds, const uint32_t endedBefore,
              const bool started) {
  if (started) LOG_INF("GAME", "Round started at ver %u", static_cast<unsigned>(session.ver()));
  if (rounds.roundsEnded() != endedBefore) {
    LOG_INF("GAME", "Round over at ver %u; winners mask 0x%x", static_cast<unsigned>(session.ver()),
            static_cast<unsigned>(session.status().winners));
  }
}

}  // namespace

std::unique_ptr<GameVM> GameVM::create(GameAssets&& assets, const GameViewport& viewport, const FrameReplay& replay,
                                       const char* gameId, GameScript::StoreSlot& store,
                                       const GameCore::Roster& roster) {
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
  std::unique_ptr<GameVM> vm(new (std::nothrow)
                                 GameVM(std::move(assets), std::move(frameStorage), canvas, gameId, store, roster));
  if (!vm) {
    LOG_ERR("GAME", "OOM: %u byte GameVM", static_cast<unsigned>(sizeof(GameVM)));
    return nullptr;
  }
  if (!vm->arena.allocate()) return nullptr;
  return vm;
}

GameVM::GameVM(GameAssets&& loaded, HalMemory::PsramBuffer storage, const GameScript::Canvas& canvas,
               const char* gameId, GameScript::StoreSlot& store, const GameCore::Roster& roster)
    : assets(std::move(loaded)),
      roster(roster),
      frameStorage(std::move(storage)),
      frameBuffers(frameStorage.get(), frameStorage.get() + GameScript::MAX_BYTES, GameScript::MAX_BYTES),
      mailbox({frameStorage.get() + 2 * GameScript::MAX_BYTES, GameCore::SNAPSHOT_BYTES}),
      log(gameId),
      game(arena.allocator(), frameBuffers, assets.sources(), GameScript::HostPorts{random, clock, log, store}, canvas,
           assets.images()) {}

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
    outcome = rounds.start(*session);
    publishCommitted(mailbox, *session, published, outcome);
    if (outcome == Outcome::Ok) logRound(*session, rounds, 0, true);
  }
  while (outcome == Outcome::Ok && !quitRequested.load(std::memory_order_acquire)) {
    const uint32_t endedBefore = rounds.roundsEnded();
    if (rounds.takePlayAgain()) {
      outcome = rounds.restart();
      publishCommitted(mailbox, *session, published, outcome);
      if (outcome == Outcome::Ok) logRound(*session, rounds, endedBefore, true);
      continue;
    }
    GameScript::InputEvent event;
    if (!queue.pop(event)) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      continue;
    }
    outcome = rounds.step(event);
    publishCommitted(mailbox, *session, published, outcome);
    if (outcome == Outcome::Ok) logRound(*session, rounds, endedBefore, false);
  }
  if (outcome == Outcome::ScriptError) {
    LOG_ERR("LUA", "Script error: %s", failureText());
    scriptFailed.store(true, std::memory_order_release);
  } else if (outcome == Outcome::Cancelled) {
    LOG_INF("GAME", "VM cancelled");
  }
  game.close();
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

bool GameVM::drawFront(const GfxRenderer& renderer, const GameViewport& viewport, FrameReplay& replay) {
  if (frameBuffers.frameGen() == 0) return false;
  bool drawn = false;
  frameBuffers.takeFront([&](const GameScript::DisplayList& frame, const GameScript::Refresh hint) {
    drawn = replay.draw(renderer, viewport, frame, hint, assets.images());
  });
  return drawn;
}

void GameVM::postInput(const GameScript::InputEvent& event) {
  if (queue.push(event)) LOG_INF("GAME", "Input queue full; dropped the oldest non-timer event");
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
