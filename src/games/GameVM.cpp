#if FREEINK_CAP_GAMES

#include "GameVM.h"

#include <Arduino.h>
#include <DisplayList.h>
#include <Logging.h>
#include <Session.h>

#include <new>
#include <utility>

#include "FrameReplay.h"

namespace {

constexpr uint32_t STOP_POLL_MS = 5;

}  // namespace

std::unique_ptr<GameVM> GameVM::create(GameAssets&& assets, const GameScript::Canvas& canvas) {
  constexpr size_t frameBytes = 2 * GameScript::MAX_BYTES;
  auto frameStorage = HalMemory::allocatePsram(frameBytes);
  if (!frameStorage) {
    LOG_ERR("GAME", "OOM: %u bytes of PSRAM for frame buffers", static_cast<unsigned>(frameBytes));
    return nullptr;
  }
  // The constructor is private, so makeUniqueNoThrow cannot reach it; the unique_ptr
  // owns the nothrow allocation at once.
  std::unique_ptr<GameVM> vm(new (std::nothrow) GameVM(std::move(assets), std::move(frameStorage), canvas));
  if (!vm) {
    LOG_ERR("GAME", "OOM: %u byte GameVM", static_cast<unsigned>(sizeof(GameVM)));
    return nullptr;
  }
  if (!vm->arena.allocate()) return nullptr;
  return vm;
}

GameVM::GameVM(GameAssets&& loaded, HalMemory::PsramBuffer storage, const GameScript::Canvas& canvas)
    : assets(std::move(loaded)),
      frameStorage(std::move(storage)),
      frameBuffers(frameStorage.get(), frameStorage.get() + GameScript::MAX_BYTES, GameScript::MAX_BYTES),
      game(arena.allocator(), frameBuffers, assets.sources(), random, canvas) {}

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
  GameCore::Session* session = heap.create<GameCore::Session>(GameCore::Roster::solo(), game);
  Outcome outcome = Outcome::ScriptError;
  if (session) {
    outcome = game.load();
  } else {
    failure = "not enough memory";
  }
  if (outcome == Outcome::Ok) outcome = session->start();
  if (outcome == Outcome::Ok) outcome = session->draw();
  bool overLogged = false;
  while (outcome == Outcome::Ok && !quitRequested.load(std::memory_order_acquire)) {
    if (!overLogged && session->status().over) {
      overLogged = true;
      LOG_INF("GAME", "Round over at ver %u; winners mask 0x%x", static_cast<unsigned>(session->ver()),
              static_cast<unsigned>(session->status().winners));
    }
    GameScript::InputEvent event;
    if (!queue.pop(event)) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      continue;
    }
    outcome = session->handle(event);
    if (outcome == Outcome::Ok) outcome = session->applyPending();
    if (outcome == Outcome::Ok) outcome = session->draw();
  }
  if (outcome == Outcome::ScriptError) {
    LOG_ERR("LUA", "Script error: %s", errorMessage());
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

uint32_t GameVM::runningForMs(const uint32_t nowMs) {
  if (!busy()) return 0;
  const uint32_t call = game.callSerial();
  if (call != watchedCall) {
    watchedCall = call;
    watchedSinceMs = nowMs;
  }
  return nowMs - watchedSinceMs;
}

void GameVM::postTap(const int16_t x, const int16_t y) {
  postInput(GameScript::InputEvent{GameScript::InputKind::Tap, x, y});
}

bool GameVM::drawFront(const GfxRenderer& renderer, const GameViewport& viewport, const FrameReplay& replay) {
  if (frameBuffers.frameGen() == 0) return false;
  frameBuffers.readFront([&](const GameScript::DisplayList& frame) { replay.draw(renderer, viewport, frame); });
  return true;
}

void GameVM::postInput(const GameScript::InputEvent& event) {
  if (queue.push(event)) LOG_INF("GAME", "Input queue full; dropped the oldest event");
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
  for (uint32_t waited = 0; !finished(); waited += STOP_POLL_MS) {
    if (waited >= timeoutMs) return false;
    vTaskDelay(pdMS_TO_TICKS(STOP_POLL_MS));
  }
  return true;
}

bool GameVM::stop(const uint32_t timeoutMs) {
  cancel();
  return join(timeoutMs);
}

void GameVM::abandon(std::unique_ptr<GameVM> vm) {
  if (!vm) return;
  // Leaked from here on unless the task ends after all: it may hold this object's
  // mutexes (input queue, task mutex), and deleting a task releases nothing it holds.
  GameVM* stuck = vm.release();
#if defined(SIMULATOR)
  // The shim has no vTaskSuspend, and its vTaskDelete only detaches the thread,
  // which would keep running in the arena; waiting cannot help.
  if (stuck->finished()) {
    delete stuck;
    return;
  }
  LOG_ERR("GAME", "VM stuck; the simulator cannot stop its thread, so all of it is leaked");
#else
  for (uint32_t waited = 0; waited < ABANDON_WAIT_MS; waited += STOP_POLL_MS) {
    if (stuck->finished()) {
      delete stuck;
      return;
    }
    if (stuck->deleteIfStuckInLua()) {
      // The Lua state lives in the arena, and nothing else points into it.
      stuck->game.abandon();
      stuck->arena.release();
      stuck->frameStorage.reset();
      stuck->assets.release();
      LOG_ERR("GAME", "Abandoned the stuck VM: freed its PSRAM, leaked %u bytes",
              static_cast<unsigned>(sizeof(GameVM)));
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(STOP_POLL_MS));
  }
  LOG_ERR("GAME", "VM stuck and not safely deletable; leaking all of it");
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
