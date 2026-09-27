#if FREEINK_CAP_GAMES

#include "GameVM.h"

#include <DisplayList.h>
#include <Logging.h>

#include <new>
#include <utility>

namespace {

constexpr uint32_t STOP_POLL_MS = 5;

}  // namespace

std::unique_ptr<GameVM> GameVM::create(GameAssets&& assets) {
  constexpr size_t frameBytes = 2 * GameScript::MAX_BYTES;
  auto frameStorage = HalMemory::allocatePsram(frameBytes);
  if (!frameStorage) {
    LOG_ERR("GAME", "OOM: %u bytes of PSRAM for frame buffers", static_cast<unsigned>(frameBytes));
    return nullptr;
  }
  // The constructor is private, so makeUniqueNoThrow cannot reach it; the unique_ptr
  // owns the nothrow allocation at once.
  std::unique_ptr<GameVM> vm(new (std::nothrow) GameVM(std::move(assets), std::move(frameStorage)));
  if (!vm) {
    LOG_ERR("GAME", "OOM: %u byte GameVM", static_cast<unsigned>(sizeof(GameVM)));
    return nullptr;
  }
  if (!vm->arena.allocate()) return nullptr;
  return vm;
}

GameVM::GameVM(GameAssets&& loaded, HalMemory::PsramBuffer storage)
    : assets(std::move(loaded)),
      frameStorage(std::move(storage)),
      frameBuffers(frameStorage.get(), frameStorage.get() + GameScript::MAX_BYTES, GameScript::MAX_BYTES),
      game(arena.allocator(), frameBuffers, assets.sources(), random) {}

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
  Outcome outcome = game.start();
  if (outcome == Outcome::Ok) outcome = game.draw();
  while (outcome == Outcome::Ok && !quitRequested.load(std::memory_order_acquire)) {
    GameScript::InputEvent event;
    if (!queue.pop(event)) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      continue;
    }
    outcome = game.input(event);
    if (outcome == Outcome::Ok) outcome = game.draw();
  }
  if (outcome != Outcome::Ok) {
    LOG_ERR("LUA", "Script error: %s", game.errorMessage());
    scriptFailed.store(true, std::memory_order_release);
  }
  game.close();
  LOG_INF("GAME", "VM stopped; arena peak %u bytes, stack high-water %u bytes free",
          static_cast<unsigned>(arena.allocator().peakBytes()),
          static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  {
    std::lock_guard<std::mutex> lock(taskMutex);
    taskAlive = false;
  }
  done.store(true, std::memory_order_release);
}

void GameVM::postInput(const GameScript::InputEvent& event) {
  if (queue.push(event)) LOG_INF("GAME", "Input queue full; dropped the oldest event");
  notifyTask();
}

void GameVM::notifyTask() {
  std::lock_guard<std::mutex> lock(taskMutex);
  if (taskAlive) xTaskNotify(task, 1, eIncrement);
}

bool GameVM::stop(const uint32_t timeoutMs) {
  if (!task) return true;
  quitRequested.store(true, std::memory_order_release);
  notifyTask();
  for (uint32_t waited = 0; !finished(); waited += STOP_POLL_MS) {
    if (waited >= timeoutMs) return false;
    vTaskDelay(pdMS_TO_TICKS(STOP_POLL_MS));
  }
  return true;
}

#endif  // FREEINK_CAP_GAMES
