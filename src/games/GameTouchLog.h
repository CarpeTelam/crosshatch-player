#pragma once

#include <FreeInkUICore.h>
#include <GameEvent.h>

#include <cstdarg>
#include <cstddef>
#include <cstdio>

#include "GameTouch.h"

// The log text of one gesture read in a round in play (state Playing) and the host's classification of it (the Outcome
// GameTouch::classify gave): the line GameMatchActivity logs at LOG_DBG after `<id>: touch `. "Sent" means classify
// accepted it; a later drop keeps its own line ("dropped a touch before the frame was on the panel", "dropped a touch
// that began ... before the hand-off passed"), and the VM or the game declining a tap is not logged. The lines show in
// builds with LOG_LEVEL 2 (the development envs of platformio.ini and the simulator); the release envs use 1 and print
// none. Pure, so host tests pin the text. A tap, long press or swipe prints the screen point(s), the canvas point the
// game got it at (when sent), the hold when the SDK latched one, and, when it was not sent, why. A contact with no
// gesture prints `screen (a)->(b)`: the first sample the loop saw to the last one it saw, which for a fast slide can be
// past the true touch-down. Unknown positions print `screen unknown`; an unknown hold prints no `held`; a long press
// never prints a hold (the SDK fires it at 500 ms and suppresses the lift, so lastTouchHeldMs is stale).
namespace GameTouchLog {

struct Line {
  char text[128] = {};
};

namespace detail {

// Appends printf-style text at `used`, never past the buffer; a line that does not fit is cut, never overrun.
__attribute__((format(printf, 3, 4))) inline void append(Line& line, size_t& used, const char* format, ...) {
  if (used >= sizeof(line.text) - 1) return;
  va_list args;
  va_start(args, format);
  const int written = std::vsnprintf(line.text + used, sizeof(line.text) - used, format, args);
  va_end(args);
  if (written <= 0) return;
  used += static_cast<size_t>(written);
  if (used > sizeof(line.text) - 1) used = sizeof(line.text) - 1;
}

inline const char* directionName(const GameTouch::Gesture& gesture) {
  switch (freeink::ui::swipeDirection(gesture.x, gesture.y, gesture.endX, gesture.endY)) {
    case freeink::ui::SwipeDir::Left:
      return "left";
    case freeink::ui::SwipeDir::Right:
      return "right";
    case freeink::ui::SwipeDir::Up:
      return "up";
    case freeink::ui::SwipeDir::Down:
      return "down";
    case freeink::ui::SwipeDir::None:
      break;
  }
  return nullptr;
}

inline const char* reason(const GameTouch::Outcome outcome) {
  switch (outcome) {
    case GameTouch::Outcome::Sent:
    case GameTouch::Outcome::NoGesture:
      break;
    case GameTouch::Outcome::Ended:
      return "not sent, not a tap, long press or swipe";
    case GameTouch::Outcome::SystemEdge:
      return "not sent, system edge swipe";
    case GameTouch::Outcome::NoDirection:
      return "not sent, no direction";
    case GameTouch::Outcome::OffCanvas:
      return "not sent, off the canvas";
  }
  return nullptr;
}

}  // namespace detail

// The line for `gesture` and its `outcome`; `event` is the game's event, read for the canvas point when the outcome is
// Sent. Empty for no gesture.
inline Line line(const GameTouch::Gesture& gesture, const GameTouch::Outcome outcome,
                 const GameCore::GameEvent& event) {
  using GameTouch::Kind;
  Line result;
  size_t used = 0;
  if (gesture.kind == Kind::None || outcome == GameTouch::Outcome::NoGesture) return result;

  switch (gesture.kind) {
    case Kind::Tap:
      detail::append(result, used, "tap");
      break;
    case Kind::LongPress:
      detail::append(result, used, "long press");
      break;
    case Kind::Swipe: {
      // The SDK always names a direction (ties go horizontal), so NoDirection is defensive: it says none then.
      const char* direction = outcome == GameTouch::Outcome::NoDirection ? nullptr : detail::directionName(gesture);
      if (direction != nullptr) {
        detail::append(result, used, "swipe %s", direction);
      } else {
        detail::append(result, used, "swipe");
      }
      break;
    }
    case Kind::Ended:
      detail::append(result, used, "contact ended");
      break;
    case Kind::None:
      return result;
  }

  if (gesture.kind == Kind::Swipe) {
    detail::append(result, used, " screen (%d,%d)->(%d,%d)", gesture.x, gesture.y, gesture.endX, gesture.endY);
  } else if (gesture.kind == Kind::Ended) {
    if (gesture.x < 0 || gesture.y < 0) {
      detail::append(result, used, " screen unknown");
    } else if (gesture.endX < 0 || gesture.endY < 0) {
      detail::append(result, used, " screen (%d,%d)->unknown", gesture.x, gesture.y);
    } else {
      detail::append(result, used, " screen (%d,%d)->(%d,%d)", gesture.x, gesture.y, gesture.endX, gesture.endY);
    }
  } else {
    detail::append(result, used, " screen (%d,%d)", gesture.x, gesture.y);
  }

  if (outcome == GameTouch::Outcome::Sent) detail::append(result, used, " canvas (%d,%d)", event.x, event.y);
  if (gesture.heldMs >= 0 && gesture.kind != Kind::LongPress) {
    detail::append(result, used, " held %d ms", static_cast<int>(gesture.heldMs));
  }
  if (const char* why = detail::reason(outcome)) detail::append(result, used, ": %s", why);
  return result;
}

}  // namespace GameTouchLog
