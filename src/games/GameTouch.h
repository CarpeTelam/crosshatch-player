#pragma once

#include <FreeInkUICore.h>
#include <GameEvent.h>

#include <cstdint>

#include "GameViewport.h"

// Touch to game events (AD-7, the Input events convention): the match reads one
// gesture per loop pass in logical screen pixels and this turns it into what
// input() sees, in canvas pixels. Pure, so it is host-tested.
namespace GameTouch {

// Ended is a contact that lifted with no tap, no long press and no swipe (it slid past the tap slop and was not a
// swipe either). It never reaches a game; it exists so the log can say what became of the touch.
enum class Kind : uint8_t { None, Tap, LongPress, Swipe, Ended };

// One gesture on the logical screen. A tap or long press is at (x, y); a swipe runs from (x, y) to (endX, endY); an
// ended contact runs from the first sample the match saw (x, y) to the last one it saw (endX, endY), -1 when it never
// saw it down; the first sample is the live point of the first pass that saw the finger down, which for a fast slide
// can be past the true touch-down. heldMs is the contact's hold as the SDK latched it at release
// (HalGPIO::lastTouchHeldMs), -1 when there is none: a long press has none (the SDK fires it at 500 ms and suppresses
// the lift).
struct Gesture {
  Kind kind = Kind::None;
  int x = 0;
  int y = 0;
  int endX = 0;
  int endY = 0;
  int32_t heldMs = -1;
};

// True when the swipe is one of the system's edge gestures, which never reach a
// game: Back (a right swipe from the left 25%), Home (an up swipe from the bottom
// 14%), and Menu or the light panel (a down swipe from the top 14%). Classified
// by the SDK on the whole logical screen, as MappedInputManager does.
inline bool isSystemEdgeSwipe(const Gesture& swipe, const int screenW, const int screenH) {
  using freeink::ui::ScreenEdge;
  for (const ScreenEdge edge : {ScreenEdge::Left, ScreenEdge::Bottom, ScreenEdge::Top}) {
    if (freeink::ui::edgeSwipe(edge, swipe.x, swipe.y, swipe.endX, swipe.endY, screenW, screenH)) return true;
  }
  return false;
}

// What became of a gesture: delivered to the game, or why not.
enum class Outcome : uint8_t {
  Sent,
  NoGesture,    // Kind::None: nothing happened
  Ended,        // Kind::Ended: a contact that was no tap, long press or swipe
  SystemEdge,   // a swipe that is the system's Back, Home or Menu gesture
  NoDirection,  // a swipe with no dominant direction
  OffCanvas,    // starts on the bezel
};

// The game's event for `gesture` on a screenW x screenH logical screen: a tap or
// long press where it happened, a swipe at its start with its dominant direction,
// each in canvas coordinates, written to `out` only when the outcome is Sent. The
// guards run in this order: no gesture, an ended contact, then for a swipe the
// system edge (before the direction is read: a Back or Home swipe must never reach
// a game), then no direction, then a start off the canvas.
inline Outcome classify(const Gesture& gesture, const int screenW, const int screenH, const GameViewport& viewport,
                        GameCore::GameEvent& out) {
  GameCore::GameEvent event;
  switch (gesture.kind) {
    case Kind::None:
      return Outcome::NoGesture;
    case Kind::Ended:
      return Outcome::Ended;
    case Kind::Tap:
      event.kind = GameCore::EventKind::Tap;
      break;
    case Kind::LongPress:
      event.kind = GameCore::EventKind::LongPress;
      break;
    case Kind::Swipe: {
      if (isSystemEdgeSwipe(gesture, screenW, screenH)) return Outcome::SystemEdge;
      event.kind = GameCore::EventKind::Swipe;
      switch (freeink::ui::swipeDirection(gesture.x, gesture.y, gesture.endX, gesture.endY)) {
        case freeink::ui::SwipeDir::Left:
          event.dir = GameCore::SwipeDir::Left;
          break;
        case freeink::ui::SwipeDir::Right:
          event.dir = GameCore::SwipeDir::Right;
          break;
        case freeink::ui::SwipeDir::Up:
          event.dir = GameCore::SwipeDir::Up;
          break;
        case freeink::ui::SwipeDir::Down:
          event.dir = GameCore::SwipeDir::Down;
          break;
        case freeink::ui::SwipeDir::None:
          return Outcome::NoDirection;
      }
      break;
    }
  }
  if (!viewport.toCanvas(gesture.x, gesture.y, event.x, event.y)) return Outcome::OffCanvas;
  out = event;
  return Outcome::Sent;
}

// The game's event for `gesture`: true (and `out` written) when classify sends it. False (out untouched) for no
// gesture, an ended contact, a system edge swipe, a swipe with no direction, and a start off the canvas.
inline bool toEvent(const Gesture& gesture, const int screenW, const int screenH, const GameViewport& viewport,
                    GameCore::GameEvent& out) {
  return classify(gesture, screenW, screenH, viewport, out) == Outcome::Sent;
}

}  // namespace GameTouch
