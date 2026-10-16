#pragma once

#include <cstdint>

// The gray engine's slot clock (the display-lab epic, D5): one slot (a drive scan and its follow-up) per period, the
// period max(the slot's busy time + MARGIN_US, MIN_PERIOD_US) from the slot just run, at a fixed phase: whole
// periods that passed while a slot ran are skipped (counted, never queued). Pure, pinned by test_slot_scheduler.cpp.
// Times are a free-running microsecond clock (micros()); every comparison is a wrap-safe signed difference.
// Also keeps the window statistics the transition screen shows and logs: scans per second and milliseconds per scan.
namespace GameDisplayLab {

class SlotScheduler {
 public:
  static constexpr uint32_t MIN_PERIOD_US = 100000;
  static constexpr uint32_t MARGIN_US = 5000;

  // The first slot is due at `nowUs`; the period, the skip count and the window restart.
  void start(const uint32_t nowUs) {
    next_ = nowUs;
    period_ = MIN_PERIOD_US;
    started_ = true;
    resetWindow(nowUs);
  }
  void stop() { started_ = false; }
  bool started() const { return started_; }

  // Whether the next slot's time has come.
  bool due(const uint32_t nowUs) const { return started_ && static_cast<int32_t>(nowUs - next_) >= 0; }

  // A slot that began at `slotStartUs` ended at `nowUs` after `busyUs` of scans: the next slot is one period after
  // its start, less any whole periods already past by `nowUs`.
  void slotDone(const uint32_t slotStartUs, const uint32_t nowUs, const uint32_t busyUs) {
    const uint32_t wanted = busyUs > UINT32_MAX - MARGIN_US ? UINT32_MAX : busyUs + MARGIN_US;
    period_ = wanted > MIN_PERIOD_US ? wanted : MIN_PERIOD_US;
    next_ = slotStartUs + period_;
    const int32_t late = static_cast<int32_t>(nowUs - next_);
    if (late < 0) return;
    const uint32_t whole = static_cast<uint32_t>(late) / period_;
    next_ += whole * period_;
    skipped_ += whole;
  }

  uint32_t periodUs() const { return period_; }
  uint32_t nextUs() const { return next_; }
  // Slot periods skipped since the window started (start() and resetWindow() restart it).
  uint32_t skipped() const { return skipped_; }

  // --- window statistics -----------------------------------------------------------------------------------------
  void record(const uint32_t scans, const uint32_t busyUs) {
    windowScans_ += scans;
    windowBusyUs_ += busyUs;
  }
  // Starts a new window: its scans, busy time and skipped slots restart.
  void resetWindow(const uint32_t nowUs) {
    windowStart_ = nowUs;
    windowScans_ = 0;
    windowBusyUs_ = 0;
    skipped_ = 0;
  }
  uint32_t windowUs(const uint32_t nowUs) const { return nowUs - windowStart_; }
  // Scans per second over the window, in tenths (123 = 12.3 scans/s); 0 for an empty window.
  uint32_t scansPerSecondTenths(const uint32_t nowUs) const {
    const uint32_t span = windowUs(nowUs);
    if (span == 0) return 0;
    return static_cast<uint32_t>(static_cast<uint64_t>(windowScans_) * 10000000ULL / span);
  }
  // Mean busy milliseconds per scan over the window; 0 without scans.
  uint32_t msPerScan() const {
    return windowScans_ == 0 ? 0 : static_cast<uint32_t>(windowBusyUs_ / windowScans_ / 1000ULL);
  }
  uint32_t windowScans() const { return windowScans_; }

 private:
  uint32_t next_ = 0;
  uint32_t period_ = MIN_PERIOD_US;
  uint32_t skipped_ = 0;
  bool started_ = false;
  uint32_t windowStart_ = 0;
  uint32_t windowScans_ = 0;
  uint64_t windowBusyUs_ = 0;
};

}  // namespace GameDisplayLab
