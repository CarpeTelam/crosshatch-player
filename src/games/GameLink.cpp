#if FREEINK_CAP_GAMES

#include "GameLink.h"

namespace {
// xTaskCreatePinnedToCore's success (pdPASS in FreeRTOS's projdefs.h; the simulator's shim defines neither it nor
// TickType_t, so this file names neither).
constexpr BaseType_t TASK_CREATED = 1;
constexpr uint32_t TICK_MS = static_cast<uint32_t>(portTICK_PERIOD_MS);
}  // namespace

void GameLink::sleepMs(const uint32_t ms) { vTaskDelay(ticksFor(ms, TICK_MS)); }

bool GameLink::start(const uint32_t stepMilliseconds) {
  if (started.load(std::memory_order_acquire)) return running();  // once: a stopped GameLink starts no second task
  stepMs = stepMilliseconds > 0 ? stepMilliseconds : STEP_MS;
  quit.store(false, std::memory_order_release);
  done.store(false, std::memory_order_release);
  task = nullptr;
  // Before the task can run: `started` is what running() and stop() read.
  started.store(true, std::memory_order_release);
  const BaseType_t created = xTaskCreatePinnedToCore(&GameLink::taskEntry, "GameLink", TASK_STACK_BYTES, this,
                                                     TASK_PRIORITY, &task, TASK_CORE);
  if (created != TASK_CREATED || task == nullptr) {
    started.store(false, std::memory_order_release);
    return false;
  }
  return true;
}

bool GameLink::stop(const uint32_t joinMs) {
  if (!started.load(std::memory_order_acquire)) return true;
  quit.store(true, std::memory_order_release);
  // A poll waits at least one tick, so the count is by the longer of STOP_POLL_MS and a tick: the bound holds at 100
  // Hz.
  const uint32_t pollMs = STOP_POLL_MS > TICK_MS ? STOP_POLL_MS : TICK_MS;
  const uint32_t polls = joinMs / pollMs;
  for (uint32_t poll = 0; poll < polls; ++poll) {
    if (done.load(std::memory_order_acquire)) return true;
    sleepMs(STOP_POLL_MS);
  }
  return done.load(std::memory_order_acquire);
}

void GameLink::taskEntry(void* param) {
  static_cast<GameLink*>(param)->run();
  // run() has published `done`, after which the owner may free the object; touch nothing of it here. In the
  // simulator and the host harness this ends the thread's work and the entry returns.
  vTaskDelete(nullptr);
}

void GameLink::run() {
  while (!quit.load(std::memory_order_acquire)) {
    work.linkStep();
    sleepMs(stepMs);
  }
  work.linkStopped(static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr)));
  done.store(true, std::memory_order_release);
}

#endif  // FREEINK_CAP_GAMES
