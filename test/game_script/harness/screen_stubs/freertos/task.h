#pragma once

// The task calls GameVM makes, over host threads (see ../FakeRtos.h).

#include <chrono>
#include <thread>

#include "../FakeRtos.h"
#include "FreeRTOS.h"

using TaskHandle_t = fakertos::Task*;
using TaskFunction_t = void (*)(void*);

enum eNotifyAction { eNoAction = 0, eSetBits, eIncrement, eSetValueWithOverwrite, eSetValueWithoutOverwrite };
enum eTaskState { eRunning = 0, eReady, eBlocked, eSuspended, eDeleted, eInvalid };

inline BaseType_t xTaskCreatePinnedToCore(TaskFunction_t entry, const char* /*name*/, const uint32_t stackBytes,
                                          void* param, UBaseType_t /*priority*/, TaskHandle_t* out,
                                          BaseType_t /*core*/) {
  fakertos::State& s = fakertos::S();
  if (s.failNextCreate.exchange(false)) return pdFAIL;  // *out stays as the caller set it
  // Kept in the registry for good: a notify to an ended task must still land on a live object.
  auto owned = std::make_unique<fakertos::Task>();
  fakertos::Task* task = owned.get();
  task->stackBytes = stackBytes;
  {
    std::lock_guard<std::mutex> lock(s.m);
    s.tasks.push_back(std::move(owned));
  }
  ++s.liveTasks;
  ++s.tasksCreated;
  if (out) *out = task;  // before the thread runs, so the caller never sees a started task without a handle
  std::thread([task, entry, param] {
    fakertos::current = task;
    task->stackBase = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
    entry(param);
    std::lock_guard<std::mutex> lock(fakertos::S().m);
    task->alive = false;
    if (task->counted) {
      task->counted = false;
      --fakertos::S().liveTasks;
    }
  }).detach();
  return pdPASS;
}

// nullptr, or the calling task's own handle, ends the caller (the entry function returns
// after it, where the device never returns); another task's handle stops it for good.
inline void vTaskDelete(TaskHandle_t handle) {
  fakertos::State& s = fakertos::S();
  std::lock_guard<std::mutex> lock(s.m);
  if (!handle || handle == fakertos::current) {
    if (fakertos::current) fakertos::current->alive = false;
    return;
  }
  handle->deleted = true;
  handle->alive = false;
  if (handle->counted) {
    handle->counted = false;
    --s.liveTasks;
  }
  s.cv.notify_all();
}

inline BaseType_t xTaskNotify(TaskHandle_t handle, uint32_t /*value*/, eNotifyAction action) {
  fakertos::State& s = fakertos::S();
  std::lock_guard<std::mutex> lock(s.m);
  if (!handle || !handle->alive) {
    ++s.deadNotifies;
    return pdFAIL;
  }
  if (action == eIncrement) ++handle->notifyCount;
  s.cv.notify_all();
  return pdPASS;
}

// Waits for a notification count (portMAX_DELAY, or `wait` ticks); pdTRUE clears it.
inline uint32_t ulTaskNotifyTake(const BaseType_t clearOnExit, const TickType_t wait) {
  fakertos::State& s = fakertos::S();
  fakertos::Task* task = fakertos::current;
  if (!task) return 0;  // only a task waits for a notification
  std::unique_lock<std::mutex> lock(s.m);
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(wait);
  for (;;) {
    fakertos::holdIfNeeded(lock, task, -1);
    if (task->notifyCount > 0) {
      const uint32_t count = task->notifyCount;
      task->notifyCount = clearOnExit ? 0 : count - 1;
      return count;
    }
    if (wait == portMAX_DELAY) {
      s.cv.wait(lock);
    } else if (s.cv.wait_until(lock, end) == std::cv_status::timeout) {
      return 0;
    }
  }
}

inline void vTaskDelay(const TickType_t ticks) {
  if (fakertos::current) {
    std::unique_lock<std::mutex> lock(fakertos::S().m);
    fakertos::holdIfNeeded(lock, fakertos::current, -1);
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(ticks));
}

inline void vTaskSuspend(TaskHandle_t handle) {
  std::lock_guard<std::mutex> lock(fakertos::S().m);
  handle->suspended = true;
  fakertos::S().cv.notify_all();
}

inline void vTaskResume(TaskHandle_t handle) {
  std::lock_guard<std::mutex> lock(fakertos::S().m);
  handle->suspended = false;
  fakertos::S().cv.notify_all();
}

inline eTaskState eTaskGetState(TaskHandle_t handle) {
  fakertos::State& s = fakertos::S();
  std::lock_guard<std::mutex> lock(s.m);
  if (handle->deleted || !handle->alive) return eDeleted;
  if (!handle->parked) return eRunning;
  return handle->suspended ? eSuspended : eBlocked;
}

// The lowest address the task may use: its requested depth below its first frame.
inline StackType_t* pxTaskGetStackStart(TaskHandle_t handle) {
  fakertos::Task* task = handle ? handle : fakertos::current;
  return reinterpret_cast<StackType_t*>(task->stackBase - task->stackBytes);
}

inline UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t handle) {
  fakertos::Task* task = handle ? handle : fakertos::current;
  return task ? task->stackBytes / 2 : 0;
}
