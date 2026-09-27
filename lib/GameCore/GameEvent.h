#pragma once

#include <cstdint>

namespace GameCore {

// What input() is told about (AD-8 and the Input events convention). Touch events
// come from the canvas; Rejected and Over come from the Session; Timer from
// ch.timer.after (AD-23). Later stories add LongPress and Swipe.
enum class EventKind : uint8_t { Tap, Rejected, Over, Timer };

struct GameEvent {
  EventKind kind = EventKind::Tap;
  int16_t x = 0;  // canvas coordinates, for touch events
  int16_t y = 0;
  // Rejected only: the reason apply gave, NUL-terminated; valid during the call.
  const char* reason = nullptr;
  // Timer only: which arming of the timer fired, so an event that a later
  // ch.timer.after or cancel made stale is dropped before input() sees it.
  uint32_t serial = 0;
};

}  // namespace GameCore
