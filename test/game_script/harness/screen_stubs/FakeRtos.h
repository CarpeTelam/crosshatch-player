#pragma once

// FreeRTOS, Arduino's millis, and esp_timer on host threads, with a clock the test
// moves, for the code in src/games that runs a task: GameVM and its clock. One header,
// every function inline, so the doubles of the same name (freertos/*.h, Arduino.h,
// esp_timer.h, esp_random.h) all reach one state.
//
// What it models, and how it differs from the device:
//   - A task is a detached std::thread. xTaskCreatePinnedToCore ignores the priority
//     and core. The thread's stack is the host's (8 MB); pxTaskGetStackStart answers
//     the requested depth below the task's first frame, as the simulator's shim does, so
//     the hook's headroom check runs against a 16 KB model. uxTaskGetStackHighWaterMark
//     is a constant (GameVM only logs it).
//   - vTaskDelete(nullptr) ends the calling task: the entry function returns (the device
//     never returns from it). vTaskDelete(handle) of another task means it never runs
//     again: it parks at its next stub call (or where it already waits) and stays there
//     for the life of the process, which is what deleting a suspended task does to its
//     stack. Nothing a deleted task owns is touched afterwards.
//   - vTaskSuspend sets a flag; the task parks at its next stub call (a clock read,
//     xTaskNotify wait, or vTaskDelay). A task that runs no stub call never parks, as a
//     core that has not yet taken the yield keeps running, so eTaskGetState reports
//     eRunning and GameVM::abandon resumes it.
//   - xTaskNotify counts (eIncrement); ulTaskNotifyTake(pdTRUE) waits for a count and
//     clears it. A notify to a task that has ended is counted (deadNotifies): on the
//     device it touches freed memory.
//   - The clock is `fakertos::nowMs`, moved with advance(). Every read on a task thread
//     is a stub call, so the gate below can hold it.
//   - The gate holds a task thread at its next clock read (At::Clock) or log line
//     (At::Log), after skipping the first `skip` of them: a script stuck inside a host
//     call. A clock read is outside GameVM's locked bindings (GameVM::abandon may delete
//     the task there); a log line from ch.log is inside one (it may not). pass() lets the
//     held task by that one point with the gate still armed (so the next read holds it
//     again); release() disarms it.
// vTaskDelay, delay(), and a notify wait that times out really wait (1 tick = 1 ms) and also move
// the clock by what they waited, so a wait that counts elapsed millis() ends, and a test that
// advances the clock by hand adds to what the code's own waits have advanced.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace fakertos {

struct Task {
  std::atomic<bool> alive{true};     // false once it ended or was deleted
  std::atomic<bool> deleted{false};  // deleted by another task: never runs again
  std::atomic<bool> suspended{false};
  std::atomic<bool> parked{false};  // waiting inside a stub call (suspend, gate, delete)
  bool counted = true;              // in liveTasks (under State::m)
  uint32_t notifyCount = 0;         // under State::m
  uintptr_t stackBase = 0;          // written and read by the task itself
  uint32_t stackBytes = 0;
};

struct State {
  std::mutex m;
  std::condition_variable cv;
  std::atomic<uint64_t> nowMs{1000};
  std::atomic<int> liveTasks{0};
  std::atomic<int> deadNotifies{0};
  std::atomic<int> tasksCreated{0};
  std::atomic<bool> failNextCreate{false};
  // Called (outside the lock) on each xTaskNotify from a thread that is not a task: what the loop
  // task does to wake the VM (postInput, cancel, playAgain). Lets a test read the state of the
  // loop task's world at that moment, such as whether the RenderLock is held.
  std::function<void()> onLoopNotify;
  std::vector<std::unique_ptr<Task>>
      tasks;  // every task ever created, kept so a late notify still lands on a live object
  bool gateArmed = false;
  int gateAt = 0;  // At
  int gateSkip = 0;
  uint64_t gateGeneration = 0;
  int gateParked = 0;
  std::vector<uint64_t> gateWaiting;  // the generation each held task waits on
};

// Where the gate can hold a task.
enum class At { Clock = 0, Log = 1 };

// Never destroyed: a deleted task stays parked on the condition variable for the life of
// the process, and destroying a variable with a waiter is undefined.
inline State& S() {
  static State* state = new State;
  return *state;
}

inline thread_local Task* current = nullptr;

// Holds the calling task while it is suspended or deleted, and, at a clock read or a log
// line (`where` 0 or 1; -1 for any other stub call), while the gate is armed for that
// point. Called with State::m held; returns with it held once nothing holds the task.
inline void holdIfNeeded(std::unique_lock<std::mutex>& lock, Task* task, const int where) {
  State& s = S();
  for (;;) {
    if (task->deleted) {
      task->parked = true;
      s.cv.wait(lock, [] { return false; });
    }
    if (task->suspended) {
      task->parked = true;
      s.cv.wait(lock, [&] { return !task->suspended || task->deleted; });
      task->parked = false;
      continue;
    }
    if (where >= 0 && s.gateArmed && s.gateAt == where) {
      if (s.gateSkip > 0) {
        --s.gateSkip;
        return;
      }
      const uint64_t generation = s.gateGeneration;
      task->parked = true;
      ++s.gateParked;
      s.gateWaiting.push_back(generation);
      s.cv.notify_all();
      s.cv.wait(lock, [&] { return !s.gateArmed || s.gateGeneration != generation || task->deleted; });
      --s.gateParked;
      for (auto it = s.gateWaiting.begin(); it != s.gateWaiting.end(); ++it) {
        if (*it == generation) {
          s.gateWaiting.erase(it);
          break;
        }
      }
      task->parked = false;
      s.cv.notify_all();
      if (s.gateGeneration != generation && s.gateArmed && !task->deleted) return;  // passed this point
      continue;
    }
    return;
  }
}

// A clock read or a log line on a task thread: where a suspend, a delete, or the gate bites.
inline void checkpoint(const At where) {
  Task* task = current;
  if (!task) return;
  std::unique_lock<std::mutex> lock(S().m);
  holdIfNeeded(lock, task, static_cast<int>(where));
}

inline uint64_t millis() {
  checkpoint(At::Clock);
  return S().nowMs.load();
}
inline void advance(const uint64_t ms) { S().nowMs += ms; }

// ---- the gate (a script stuck in a host call) ----
inline void arm(const At at = At::Clock, const int skip = 0) {
  std::lock_guard<std::mutex> lock(S().m);
  S().gateArmed = true;
  S().gateAt = static_cast<int>(at);
  S().gateSkip = skip;
}
// True once a task thread is held at the gate.
inline bool waitParked(const int timeoutMs = 10000) {
  State& s = S();
  std::unique_lock<std::mutex> lock(s.m);
  return s.cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return s.gateParked > 0; });
}
// Lets the held task by this point, gate still armed, and returns once it has left it.
inline void pass() {
  State& s = S();
  std::unique_lock<std::mutex> lock(s.m);
  const uint64_t passed = s.gateGeneration++;
  s.cv.notify_all();
  // Every task that was waiting on an older generation has left; one held since (at the next
  // point) is waiting on the new one.
  s.cv.wait(lock, [&] {
    for (const uint64_t waiting : s.gateWaiting)
      if (waiting <= passed) return false;
    return true;
  });
}
inline void release() {
  std::lock_guard<std::mutex> lock(S().m);
  S().gateArmed = false;
  S().cv.notify_all();
}

// Waits until every task thread has returned (a deleted one does not count).
inline bool waitNoTasks(const int timeoutMs = 10000) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (S().liveTasks.load() > 0) {
    if (std::chrono::steady_clock::now() > end) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return true;
}

// Between tests: only with no task running.
inline void reset() {
  State& s = S();
  std::lock_guard<std::mutex> lock(s.m);
  s.nowMs = 1000;
  s.deadNotifies = 0;
  s.tasksCreated = 0;
  s.failNextCreate = false;
  s.gateArmed = false;
  s.gateSkip = 0;
  s.onLoopNotify = nullptr;
  s.cv.notify_all();
}

}  // namespace fakertos
