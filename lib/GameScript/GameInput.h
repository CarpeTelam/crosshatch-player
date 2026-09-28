#pragma once

#include <GameEvent.h>

#include <cstddef>
#include <cstdint>
#include <mutex>

namespace GameScript {

// Events the loop task posts to the VM for input(): taps, long presses, and swipes
// in canvas coordinates (GameTouch.h) and due timers (GameVM::pollTimer), as the
// domain's event type (GameCore/GameEvent.h).
using InputKind = GameCore::EventKind;
using InputEvent = GameCore::GameEvent;

inline constexpr size_t INPUT_QUEUE_DEPTH = 8;

// Depth-bounded queue between the loop task (push) and the VM task (pop). A push
// into a full queue drops the oldest event that is not a Timer and says so, so the
// caller can log it. A Timer event is never dropped for a touch: GameTimer::takeDue
// has already disarmed the timer, so a lost event would stop the game's clock for
// good. Only a queue of nothing but Timer events drops its oldest, which a later
// arming has already made stale.
class InputQueue {
 public:
  // Returns true when an older event was dropped to make room.
  bool push(const InputEvent& event);
  // False when empty.
  bool pop(InputEvent& out);
  // Drops every queued event (Play again: they were aimed at the last round).
  void clear();

 private:
  std::mutex mutex;
  InputEvent ring[INPUT_QUEUE_DEPTH];
  size_t head = 0;  // next to pop
  size_t size = 0;
};

}  // namespace GameScript
