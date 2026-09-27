#pragma once

#include <FreeInkUICore.h>
#include <GameEvent.h>

#include <cstdint>

#include "GameViewport.h"

// Touch to game events (AD-7, the Input events convention): the match reads one
// gesture per loop pass in logical screen pixels and this turns it into what
// input() sees, in canvas pixels. Pure, so it is host-tested.
namespace GameTouch {

enum class Kind : uint8_t { None, Tap, LongPress, Swipe };

// One gesture on the logical screen. A tap or long press is at (x, y); a swipe
// runs from (x, y) to (endX, endY).
struct Gesture {
  Kind kind = Kind::None;
  int x = 0;
  int y = 0;
  int endX = 0;
  int endY = 0;
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

// The game's event for `gesture` on a screenW x screenH logical screen: a tap or
// long press where it happened, a swipe at its start with its dominant direction,
// each in canvas coordinates. False (out untouched) for no gesture, for one that
// starts off the canvas, and for a system edge swipe.
inline bool toEvent(const Gesture& gesture, const int screenW, const int screenH, const GameViewport& viewport,
                    GameCore::GameEvent& out) {
  GameCore::GameEvent event;
  switch (gesture.kind) {
    case Kind::None:
      return false;
    case Kind::Tap:
      event.kind = GameCore::EventKind::Tap;
      break;
    case Kind::LongPress:
      event.kind = GameCore::EventKind::LongPress;
      break;
    case Kind::Swipe: {
      if (isSystemEdgeSwipe(gesture, screenW, screenH)) return false;
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
          return false;
      }
      break;
    }
  }
  if (!viewport.toCanvas(gesture.x, gesture.y, event.x, event.y)) return false;
  out = event;
  return true;
}

}  // namespace GameTouch
