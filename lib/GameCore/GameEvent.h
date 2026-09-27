#pragma once

#include <cstdint>

namespace GameCore {

// What input() is told about (AD-8 and the Input events convention). Touch events
// come from the canvas; Rejected and Over come from the Session; Timer from
// ch.timer.after (AD-23).
enum class EventKind : uint8_t { Tap, Rejected, Over, Timer, LongPress, Swipe };

// A swipe's dominant direction on the canvas.
enum class SwipeDir : uint8_t { None, Left, Right, Up, Down };

struct GameEvent {
  EventKind kind = EventKind::Tap;
  int16_t x = 0;  // canvas coordinates, for touch events (a swipe's start)
  int16_t y = 0;
  // Rejected only: the reason apply gave, NUL-terminated; valid during the call.
  const char* reason = nullptr;
  // Timer only: which arming of the timer fired, so an event that a later
  // ch.timer.after or cancel made stale is dropped before input() sees it.
  uint32_t serial = 0;
  // Swipe only.
  SwipeDir dir = SwipeDir::None;
};

}  // namespace GameCore
