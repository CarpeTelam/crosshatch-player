#pragma once

#include <atomic>
#include <cstdint>

#include "IClock.h"

namespace GameCore {

// The ledger of the time a match spent in MatchState::Paused, behind ch.time.ms (play time: the clock since the game
// loaded, less every pause). Any task reads; enter() and leave() have a single writer, the main (Arduino loop) task:
// GameMatchActivity::handle() is reached only from there, from loop() and handleHomeGesture() through
// ActivityManager::loop() and from onExit() and fail() through ActivityManager's action processing and the
// activity's own loop, all serialised by that task (docs/activity-manager.md, FreeRTOS Task Model: the render task
// never calls handle()).
//
// The word packs the state and one number so a reader never combines two variables a concurrent leave() could change
// between its loads (which would count the interval just closed twice):
//   bit 63 clear (running): the low bits are the paused total so far, `closed`.
//   bit 63 set (paused):    the low bits are `since - closed`, the pause's start less that total.
// Entering stores `now - closed` and leaving stores `now - (since - closed)`, the same subtraction
// (closed + now - since = now - (since - closed)), so both just flip the bit. The paused total at `now` is then
// `running ? low : now - low`.
//
// Reading: playMs loads the word, reads the clock, and loads the word again; when the word changed (a write completed
// in between) it starts over. So a read is exact for the state it validated, and neither way a read can straddle a
// pause edge (an old word paired with a newer clock, or the reverse) is left. A retry follows only a completed write,
// and writes are one per pause edge, so a reader is lock-free in practice and never spins on a quiet ledger. The only
// skew left is the writer's own gap between reading its clock and its store (a few instructions): a read that
// straddles an edge can differ from the exact value by that gap. The clock is monotone, so `now - low` does not go
// negative; the clamps stay as a guard anyway.
//
// The binding takes no lock of its own, but the 64-bit atomic is not lock-free on the ESP32-S3: ESP-IDF's
// components/newlib/src/stdatomic.c emulates it with one global portMUX spinlock (a critical section, interrupts
// masked) held only for the 8-byte load, store, or memcpy, never across blocking work.
class PauseClock {
 public:
  static constexpr uint64_t PAUSED_BIT = uint64_t{1} << 63;
  static constexpr uint64_t VALUE_MASK = ~PAUSED_BIT;

  // Loop task: the match entered Paused at clock time `now`. No-op when already paused.
  void enter(const uint64_t now) {
    const uint64_t word = state.load(std::memory_order_relaxed);
    if (word & PAUSED_BIT) return;
    state.store(PAUSED_BIT | ((now - (word & VALUE_MASK)) & VALUE_MASK), std::memory_order_release);
  }

  // Loop task: the match left Paused at clock time `now`. No-op when not paused.
  void leave(const uint64_t now) {
    const uint64_t word = state.load(std::memory_order_relaxed);
    if (!(word & PAUSED_BIT)) return;
    state.store((now - (word & VALUE_MASK)) & VALUE_MASK, std::memory_order_release);
  }

  // Any task: whether the match is in Paused now.
  bool paused() const { return state.load(std::memory_order_acquire) & PAUSED_BIT; }

  // Any task: the milliseconds spent in Paused up to clock time `now`, which must be read after
  // the word was loaded.
  uint64_t pausedMs(const uint64_t now) const { return pausedMs(state.load(std::memory_order_acquire), now); }

  // The paused total a loaded `word` records at clock time `now`.
  static uint64_t pausedMs(const uint64_t word, const uint64_t now) {
    const uint64_t low = word & VALUE_MASK;
    if (!(word & PAUSED_BIT)) return low;
    return now > low ? now - low : 0;
  }

  // Any task: milliseconds since `startMs` less the time in Paused, frozen while paused. Re-reads until the word is the
  // same before and after the clock read (the class comment).
  uint64_t playMs(const IClock& clock, const uint64_t startMs) const {
    uint64_t word;
    uint64_t now;
    do {
      word = state.load(std::memory_order_acquire);
      now = clock.nowMs();
    } while (state.load(std::memory_order_acquire) != word);
    const uint64_t elapsed = now > startMs ? now - startMs : 0;
    const uint64_t pausedTotal = pausedMs(word, now);
    return pausedTotal < elapsed ? elapsed - pausedTotal : 0;
  }

 private:
  std::atomic<uint64_t> state{0};
};

}  // namespace GameCore
