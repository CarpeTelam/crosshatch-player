#pragma once

#include <atomic>
#include <cstdint>

#include "Protocol.h"

namespace GameCore {

// The latched end of a nearby match (spine AD-18, R5): the GameLink task latches the first reason the match is over
// (a received ABORT, or a peer silent for silenceMs, which is Protocol::AbortReason::Timeout exactly as LobbyHandOff
// latches it); the match's loop() takes it once. Lock-free, so the link task never waits for the match and an open
// overlay cannot make it drop a reason. No allocation, no log.
class EndSlot {
 public:
  // GameLink task. True when this call set the reason; false when one is already latched (the first wins): counted.
  bool latch(const Protocol::AbortReason reason) {
    uint8_t none = NONE;
    if (code.compare_exchange_strong(none, static_cast<uint8_t>(reason), std::memory_order_acq_rel)) return true;
    ignoredCount.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  // Any task: a reason has been latched (taken or not).
  bool latched() const { return code.load(std::memory_order_acquire) != NONE; }
  // The latched reason, still latched.
  bool peek(Protocol::AbortReason& out) const {
    const uint8_t latchedCode = code.load(std::memory_order_acquire);
    if (latchedCode == NONE) return false;
    out = static_cast<Protocol::AbortReason>(latchedCode);
    return true;
  }
  // The match's loop(): the latched reason, once. True once after a latch; later calls false. A later latch() stays
  // ignored after a take, so the reason the match acted on is the one the log shows.
  bool take(Protocol::AbortReason& out) {
    Protocol::AbortReason reason;
    if (!peek(reason) || taken.exchange(true, std::memory_order_acq_rel))
      return false;  // `out` is written on true only
    out = reason;
    return true;
  }
  uint32_t ignored() const { return ignoredCount.load(std::memory_order_relaxed); }  // latch() calls after the first

 private:
  static constexpr uint8_t NONE = 0;  // no AbortReason has the value 0
  std::atomic<uint8_t> code{NONE};
  std::atomic<bool> taken{false};
  std::atomic<uint32_t> ignoredCount{0};
};

}  // namespace GameCore
