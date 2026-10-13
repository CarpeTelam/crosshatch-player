#include "DiagLog.h"

#if CROSSHATCH_SD_LOG

#include <Arduino.h>
#include <BoardConfig.h>
#include <esp_mac.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>

#include "DiagLogBuffer.h"
#include "DiagLogFlush.h"
#include "DiagLogStore.h"
#include "DiagLogWriter.h"

// The device side of the diagnostics log: the buffer every task appends to, the writer task's state, and
// the one task that drains it. Nothing here logs (a line logged by the writer would go back into the buffer it drains),
// and every allocation is nothrow and checked: with -fno-exceptions a failed `new` aborts.
//
// Contexts. logPrintf (the caller) runs in tasks only: it formats with snprintf and prints to the USB CDC, neither of
// which is interrupt-safe, so the hook needs no interrupt variant. It runs on both cores and from the Wi-Fi tasks; the
// hook's only shared state is the buffer behind a critical section and two atomics.
//
// Timing. Lines logged before the card mounts (setup logs before Storage.begin) wait in the buffer, which holds about
// 8 KB, and are written when the card is ready; if the card never mounts, the buffer fills and counts what it drops.
// Sleep. The writer drains within tens of milliseconds of a line and closes the file after every batch. The sleep paths
// of src/main.cpp call flushBeforeSleep() just before Storage.prepareForDeepSleep() unmounts the card (ledger row 14),
// which waits, up to SLEEP_FLUSH_TIMEOUT_MS, until the writer has put everything logged so far on the card, so a batch
// does not straddle the unmount unless the card is slow or failing (then a batch that straddles it fails and is
// counted). Lines logged after that call (the "Entering deep sleep" line) cannot reach the card; the wake resets the
// MCU and the next boot starts a new file.

namespace {

constexpr size_t BUFFER_BYTES = 8192;
constexpr size_t CHUNK_BYTES = 512;
constexpr uint32_t TASK_STACK_BYTES = 6144;
constexpr UBaseType_t TASK_PRIORITY = tskIDLE_PRIORITY + 1;
constexpr TickType_t IDLE_WAIT_TICKS = pdMS_TO_TICKS(500);  // longest sleep when idle (a notification ends it at once)
constexpr TickType_t COALESCE_TICKS = pdMS_TO_TICKS(25);    // after a notification, so a burst is one batch
constexpr TickType_t RETRY_TICKS = pdMS_TO_TICKS(500);      // when the card is not ready or refused a write

#define DIAG_LOG_STR2(x) #x
#define DIAG_LOG_STR(x) DIAG_LOG_STR2(x)

// What names the build. The recipe passes the commit (-DCROSSHATCH_BUILD_ID=<short sha>, no quotes); without it the
// version string stands in, which names the commit for `default` and `sticky` only.
#ifdef CROSSPOINT_VERSION
constexpr char VERSION[] = CROSSPOINT_VERSION;
#else
constexpr char VERSION[] = "unknown";
#endif
#ifdef CROSSHATCH_BUILD_ID
constexpr char BUILD_ID[] = DIAG_LOG_STR(CROSSHATCH_BUILD_ID);
constexpr const char* FIRMWARE = BUILD_ID;
#else
constexpr const char* FIRMWARE = VERSION;
#endif
constexpr char BUILT[] = __DATE__ " " __TIME__;

using Buffer = DiagLog::LineBuffer<BUFFER_BYTES>;
Buffer buffer;
std::atomic<bool> started{false};
std::atomic<TaskHandle_t> writerTask{nullptr};
std::atomic<bool> writerBusy{false};
// True from a refused batch until the next pump that is not refused: flush must not call lines that left the buffer in
// a refused batch "on the card".
std::atomic<bool> writerFailing{false};
// True while a flush waits: the writer then retries a card that was not ready or refused a batch every COALESCE_TICKS
// instead of every RETRY_TICKS, so a flush a few hundred milliseconds long still gets its turn (the lines of the boot
// wait for the card, and the early-sleep paths of src/main.cpp can flush just after it mounted).
std::atomic<bool> flushWaiting{false};
// Set by the task when it could not allocate its context and deleted itself. The creator may store the task's handle
// after that, so the handle alone says nothing about a live task: append and flush check this first.
std::atomic<bool> writerDead{false};

uint32_t nowMs() { return static_cast<uint32_t>(millis()); }

// Everything the task owns, in one nothrow allocation that is never freed.
struct Context {
  char device[8] = "0000";
  char chunk[CHUNK_BYTES] = {};
  DiagLog::Writer<Buffer>* writer = nullptr;
  alignas(DiagLog::Writer<Buffer>) unsigned char writerStorage[sizeof(DiagLog::Writer<Buffer>)] = {};
};

void writerMain(void*) {
  auto* context = new (std::nothrow) Context;
  if (context == nullptr) {
    writerDead.store(true);
    vTaskDelete(nullptr);
    return;
  }
  uint8_t mac[6] = {};
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
    std::snprintf(context->device, sizeof(context->device), "%02X%02X", mac[4], mac[5]);
  }
  const DiagLog::Identity identity{context->device, BoardConfig::ACTIVE.name ? BoardConfig::ACTIVE.name : "unknown",
                                   FIRMWARE, VERSION, BUILT};
  context->writer = new (context->writerStorage) DiagLog::Writer<Buffer>(
      DiagLog::deviceFileStore(), buffer, identity, DiagLog::Limits{}, context->chunk, CHUNK_BYTES, nowMs);
  for (;;) {
    writerBusy.store(true);
    const DiagLog::Step step = context->writer->pump();
    // A refused batch's lines are gone from the buffer, so the flag outlives an unmount that follows the refusal.
    writerFailing.store(step == DiagLog::Step::Failed || (step == DiagLog::Step::NotReady && writerFailing.load()));
    writerBusy.store(false);
    switch (step) {
      case DiagLog::Step::Wrote:
        vTaskDelay(1);  // other tasks run between batches
        break;
      case DiagLog::Step::Idle:
        ulTaskNotifyTake(pdTRUE, IDLE_WAIT_TICKS);
        vTaskDelay(COALESCE_TICKS);
        break;
      case DiagLog::Step::NotReady:
      case DiagLog::Step::Failed:
        // RETRY_TICKS in COALESCE_TICKS slices, so a flush that starts during the wait ends it at the next slice.
        for (TickType_t waited = 0; waited < RETRY_TICKS; waited += COALESCE_TICKS) {
          vTaskDelay(COALESCE_TICKS);
          if (flushWaiting.load()) break;
        }
        break;
    }
  }
}

void startWriter() {
  bool expected = false;
  if (!started.compare_exchange_strong(expected, true)) return;
  TaskHandle_t handle = nullptr;
  if (xTaskCreate(writerMain, "diaglog", TASK_STACK_BYTES, nullptr, TASK_PRIORITY, &handle) == pdPASS) {
    writerTask.store(handle);
  } else {
    started.store(false);  // a later append tries again
  }
}

}  // namespace

namespace DiagLog {

void append(const char* line) {
  if (line == nullptr) return;
  buffer.append(line, strnlen(line, Buffer::MAX_LINE + 1));
  startWriter();
  if (writerDead.load()) return;
  const TaskHandle_t handle = writerTask.load();
  if (handle != nullptr) xTaskNotifyGive(handle);
}

namespace {

// The platform half of DiagLog::waitUntilDrained (DiagLogFlush.h) over this file's task state.
struct FlushProbe {
  TaskHandle_t handle;
  uint32_t nowMs() const { return static_cast<uint32_t>(millis()); }
  void pause() const { vTaskDelay(pdMS_TO_TICKS(5)); }
  void wake() const { xTaskNotifyGive(handle); }
  bool cardReady() const { return deviceFileStore().ready(); }
  bool writerBusy() const { return ::writerBusy.load(); }
  bool writerFailing() const { return ::writerFailing.load(); }
};

}  // namespace

bool flush(uint32_t timeoutMs) {
  if (writerDead.load()) return false;
  const TaskHandle_t handle = writerTask.load();
  if (handle == nullptr) return false;
  FlushProbe probe{handle};
  flushWaiting.store(true);
  const bool drained = waitUntilDrained(buffer, probe, timeoutMs);
  flushWaiting.store(false);
  return drained;
}

void flushBeforeSleep() { flush(SLEEP_FLUSH_TIMEOUT_MS); }

}  // namespace DiagLog

#endif  // CROSSHATCH_SD_LOG
