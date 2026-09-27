#pragma once

#include <GameEvent.h>

#include <cstdint>
#include <mutex>

namespace GameScript {

// ch.timer.after refuses a shorter delay (AD-23; `timer_min_ms` in api-level-1.txt).
inline constexpr uint32_t TIMER_MIN_MS = 1000;

// The game's one pending timer (AD-23; `timers_pending_count 1`). The VM task arms
// and cancels it from ch.timer (inside a locked section); the loop task polls it
// (GameVM::pollTimer) and posts a Timer event through the input queue, so a due
// timer reaches input() like a tap, and the VM task can stay blocked between
// events. Times are GameCore::IClock milliseconds. Thread-safe.
class GameTimer {
 public:
  // Due at nowMs + delayMs; replaces a pending timer.
  void arm(uint64_t nowMs, uint64_t delayMs);
  // Clears a pending timer, and makes an event it already fired stale.
  void cancel();
  bool pending() const;
  // Loop task: true once, when a pending timer is due at nowMs, with the arming's
  // serial for the event.
  bool takeDue(uint64_t nowMs, uint32_t& serial);
  // VM task, before input(): false for a Timer event whose arming was replaced or
  // cancelled after it fired; true for every other event.
  bool accepts(const GameCore::GameEvent& event) const;

 private:
  mutable std::mutex mutex;
  bool armed = false;
  uint64_t dueMs = 0;
  uint32_t serial = 0;  // bumped by every arm and cancel
};

}  // namespace GameScript
