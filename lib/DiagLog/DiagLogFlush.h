#pragma once

// The wait behind DiagLog::flush (lib/DiagLog/DiagLog.h), kept apart from the FreeRTOS task so the host tests run the
// same code the device does. The writer task drains the buffer by itself; this only waits for it, and gives up at a
// bound, so a card that refuses writes can delay a caller (the sleep paths of src/main.cpp) by that bound and no more.

#include <cstdint>

namespace DiagLog {

// Waits until every line appended before the call is on the card, or `timeoutMs` has passed. True only for the first:
// the buffer is empty, the writer is between batches, and the last batch it tried was not refused (a refused batch's
// lines left the buffer, so an empty buffer alone would say yes for lines that are gone). Else false: the bound passed,
// or the card is unmounted with lines still buffered (they stay buffered).
//
// Probe supplies the platform: nowMs() (a clock that may wrap), pause() (a short wait that lets the writer run), wake()
// (nudges the writer to start now), cardReady(), writerBusy() (a batch is in flight), writerFailing() (the last batch
// was refused and none has succeeded since).
template <class Buffer, class Probe>
bool waitUntilDrained(const Buffer& buffer, Probe& probe, uint32_t timeoutMs) {
  const uint32_t start = probe.nowMs();
  probe.wake();
  for (;;) {
    if (buffer.used() == 0 && !probe.writerBusy()) return !probe.writerFailing();
    if (!probe.cardReady()) return false;
    if (static_cast<uint32_t>(probe.nowMs() - start) >= timeoutMs) return false;
    probe.pause();
  }
}

}  // namespace DiagLog
