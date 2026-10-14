#pragma once

#include <cstdint>

// The on-device diagnostics log (epic-play-nearby entry 6.19): a build with -DCROSSHATCH_SD_LOG=1 writes every log
// line to a per-boot file on the device's SD card, so a device run needs no computer. lib/Logging/Logging.cpp makes
// the one call into this library, inside `#if CROSSHATCH_SD_LOG` (docs/crosshatch/upstream-touches.md, row 13), and
// src/main.cpp makes three more before it unmounts the card for sleep (row 14); with the define off, the calls, this
// header's include, and the library's code are not in the build.
//
// Independent of the games: it sees the lines of any code that logs, and a later device run uses it as it is.
namespace DiagLog {

// The hook: hands one formatted log line (as logPrintf built it, "[ms] [LVL] [origin] text\n") to a bounded buffer and
// returns at once. It never blocks, never takes the SD card, and never logs. The first call starts the writer task.
// Callable from any task on either core; not from an interrupt handler (logPrintf is not either).
void append(const char* line);

// Waits up to timeoutMs for the lines appended so far to reach the card; true when they did, false when the bound
// passed, the card is unmounted with lines still buffered, or the last batch was refused (those lines are gone). The
// writer drains within tens of milliseconds of a line by itself; this waits for it (DiagLogFlush.h is the wait).
bool flush(uint32_t timeoutMs);

// The longest the sleep paths wait, so a card that refuses writes delays sleep by this much and no more, once per path.
constexpr uint32_t SLEEP_FLUSH_TIMEOUT_MS = 250;

// flush(SLEEP_FLUSH_TIMEOUT_MS), for the calls src/main.cpp makes just before each Storage.prepareForDeepSleep()
// (docs/crosshatch/upstream-touches.md, row 14). The result is not used: sleep goes on either way.
void flushBeforeSleep();

}  // namespace DiagLog
