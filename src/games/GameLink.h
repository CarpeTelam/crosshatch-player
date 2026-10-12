#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdint>

// The GameLink task (spine AD-18, R5, R7): the one task that drains the nearby link, resends, and detects silence, so
// an open overlay that stops the match's loop() cannot make the device go quiet to its peer. It owns no state of the
// match: it calls GameLinkWork::linkStep() once per step, and the work (NearbySession) does everything. It never takes
// RenderLock and never touches an activity.
//
// Mirrors GameVM::start / taskEntry / run: the task ends itself after publishing `done`, and the owner frees what the
// task touches only after stop() returned true. The step wait is vTaskDelay, the one wait the device, the simulator's
// shim, and the host harness (screen_stubs/FakeRtos.h) all share.
class GameLinkWork {
 public:
  virtual ~GameLinkWork() = default;
  // GameLink task: one step of the work, every `stepMs`. Never blocks on the air (ILink.h).
  virtual void linkStep() = 0;
  // GameLink task: once, after the last step and before `done` is published; `stackHighWaterBytes` is the task's
  // least free stack. The work is not touched after the task publishes `done`.
  virtual void linkStopped(uint32_t stackHighWaterBytes) = 0;
};

class GameLink {
 public:
  // Estimates until entry 11 measures the stack and the battery (R12): 4 KB is R7's, the step is of the order of the
  // lobby suites' STEP_MS and crossplay's per-loop tick.
  static constexpr uint32_t TASK_STACK_BYTES = 4096;  // bytes on ESP-IDF, as GameVM::TASK_STACK_BYTES
  static constexpr int TASK_PRIORITY = 1;
  static constexpr int TASK_CORE = 1;
  static constexpr uint32_t STEP_MS = 10;
  // How long stop() waits for the task to end when its caller gives no other bound: a step is short, and the longest
  // one waits for the NearbySession lock behind a call that holds it for its own short body.
  static constexpr uint32_t JOIN_MS = 250;
  // stop() polls this often.
  static constexpr uint32_t STOP_POLL_MS = 2;

  explicit GameLink(GameLinkWork& work) : work(work) {}
  // A task that is still running must have been stopped (stop() true) or deliberately leaked with its owner.
  GameLink(const GameLink&) = delete;
  GameLink& operator=(const GameLink&) = delete;

  // Any task. True when the task runs (also when it already does). False (logged by the caller) when the task cannot be
  // created, or when an earlier task was stopped: a GameLink starts once. `stepMs` is how long the task waits
  // between steps (a test uses 1 or 2).
  bool start(uint32_t stepMs = STEP_MS);
  // Any task, never from a step. Asks the task to end and waits up to `joinMs` for it, in `joinMs / STOP_POLL_MS`
  // polls of STOP_POLL_MS (counted, not clocked, so a stopped test clock cannot hang it). True when no task remains
  // (also when none was started); false when it did not end, in which case the owner leaks what the task touches.
  bool stop(uint32_t joinMs = JOIN_MS);
  // The task was started and has not published `done`.
  bool running() const { return started.load(std::memory_order_acquire) && !done.load(std::memory_order_acquire); }

  // Ticks for a wait of `ms` at a tick of `tickPeriodMs`: rounded up, and at least one for a positive `ms`
  // (pdMS_TO_TICKS rounds 2 ms down to 0 ticks at 100 Hz, and vTaskDelay(0) is a yield, not a wait).
  static constexpr uint32_t ticksFor(const uint32_t ms, const uint32_t tickPeriodMs) {
    if (ms == 0) return 0;
    const uint32_t period = tickPeriodMs > 0 ? tickPeriodMs : 1;
    return (ms + period - 1) / period;
  }
  // A wait of at least `ms` and at least one tick, whatever the tick rate. Any task.
  static void sleepMs(uint32_t ms);

 private:
  static void taskEntry(void* param);
  void run();

  GameLinkWork& work;
  TaskHandle_t task = nullptr;
  uint32_t stepMs = STEP_MS;
  std::atomic<bool> started{false};
  std::atomic<bool> quit{false};
  std::atomic<bool> done{false};
};
