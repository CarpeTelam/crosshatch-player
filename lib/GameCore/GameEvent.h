#pragma once

#include <cstdint>

namespace GameCore {

// What input() is told about (AD-8 and the Input events convention). Touch events
// come from the canvas; Rejected and Over come from the Session. Later stories add
// LongPress, Swipe, and Timer.
enum class EventKind : uint8_t { Tap, Rejected, Over };

struct GameEvent {
  EventKind kind = EventKind::Tap;
  int16_t x = 0;  // canvas coordinates, for touch events
  int16_t y = 0;
  // Rejected only: the reason apply gave, NUL-terminated; valid during the call.
  const char* reason = nullptr;
};

}  // namespace GameCore
