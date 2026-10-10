#pragma once

#include <atomic>
#include <cstdint>

#include "IClock.h"

namespace GameCore {

// The ledger of the time a match spent in MatchState::Paused, behind ch.time.ms (play time: the clock since the game
// loaded, less every pause). Any task reads through playMs; enter() and leave() have one writer.
//
// One packed word, so a reader never combines two variables a concurrent leave() could change between its loads
// (which would count the interval just closed twice):
//   bit 63 clear (running): the low bits are the paused total so far, `closed`.
//   bit 63 set (paused):    the low bits are `since - closed`, the pause's start less that total.
// Entering stores `now - closed` and leaving stores `now - (since - closed)`, the same subtraction
// (closed + now - since = now - (since - closed)), so both just flip the bit. The paused total at `now` is then
// `running ? low : now - low`.
//
// playMs is a seqlock reader: it loads the word (acquire), reads the clock, issues an acquire fence, and reloads the
// word (relaxed), starting over while the word changed. The fence is what orders the clock read before the reload; two
// acquire loads alone would let the clock read move after the second one. A read is then exact for the state it
// validated, with neither straddle left (an old word with a newer clock, or the reverse). A retry follows only a
// completed write, one per pause edge, so a quiet ledger never retries. The only skew left is the writer's own gap
// between reading its clock and its store (a few instructions): a read straddling an edge can differ by that gap. The
// clock is monotone, so `now - low` does not go negative; the clamps stay as a guard anyway.
//
// The binding takes no lock of its own, but the 64-bit atomic is not lock-free on the ESP32-S3: ESP-IDF's
// components/newlib/src/stdatomic.c emulates it with one global portMUX spinlock (a critical section, interrupts
// masked) held only for the 8-byte load, store, or memcpy, never across blocking work.
class PauseClock {
 public:
  static constexpr uint64_t PAUSED_BIT = uint64_t{1} << 63;
  static constexpr uint64_t VALUE_MASK = ~PAUSED_BIT;

  // Single writer, the main (Arduino loop) task, for enter() and leave(): GameMatchActivity::handle() is reached only
  // from there, from loop() and handleHomeGesture() through ActivityManager::loop() and from onExit() and fail()
  // through ActivityManager's action processing and the activity's own loop (docs/activity-manager.md, FreeRTOS Task
  // Model: the render task never calls handle()).

  // The match entered Paused at clock time `now`. No-op when already paused.
  void enter(const uint64_t now) {
    const uint64_t word = state.load(std::memory_order_relaxed);
    if (word & PAUSED_BIT) return;
    state.store(PAUSED_BIT | ((now - (word & VALUE_MASK)) & VALUE_MASK), std::memory_order_release);
  }

  // The match left Paused at clock time `now`. No-op when not paused.
  void leave(const uint64_t now) {
    const uint64_t word = state.load(std::memory_order_relaxed);
    if (!(word & PAUSED_BIT)) return;
    state.store((now - (word & VALUE_MASK)) & VALUE_MASK, std::memory_order_release);
  }

  // The paused total a loaded `word` records at clock time `now`.
  static uint64_t pausedMs(const uint64_t word, const uint64_t now) {
    const uint64_t low = word & VALUE_MASK;
    if (!(word & PAUSED_BIT)) return low;
    return now > low ? now - low : 0;
  }

  // Any task: milliseconds since `startMs` less the time in Paused, frozen while paused (the class comment's seqlock
  // read).
  uint64_t playMs(const IClock& clock, const uint64_t startMs) const {
    uint64_t word;
    uint64_t now;
    do {
      word = state.load(std::memory_order_acquire);
      now = clock.nowMs();
      std::atomic_thread_fence(std::memory_order_acquire);
    } while (state.load(std::memory_order_relaxed) != word);
    const uint64_t elapsed = now > startMs ? now - startMs : 0;
    const uint64_t pausedTotal = pausedMs(word, now);
    return pausedTotal < elapsed ? elapsed - pausedTotal : 0;
  }

 private:
  std::atomic<uint64_t> state{0};
};

}  // namespace GameCore
