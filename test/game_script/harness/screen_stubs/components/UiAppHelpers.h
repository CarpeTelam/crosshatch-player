#pragma once

// touchSnapshotFrom, the one helper of components/UiAppHelpers.h the games screens use,
// as a copy of the real one's body over the scripted MappedInputManager. The real header
// includes the theme, the scale table, and the list icons, none of which build on the host.
// A change to the real function is not seen here: entry 4's suite pins the match's use of
// it (a tap on a view's row, a long press on the canvas), not the function.

#include <FreeInkApp.h>

#include "MappedInputManager.h"

inline freeink::ui::InputSnapshot touchSnapshotFrom(const MappedInputManager& mappedInput,
                                                    const bool withLongPress = false) {
  int tx = 0;
  int ty = 0;
  if (withLongPress && mappedInput.wasScreenLongPress(tx, ty)) {
    freeink::ui::InputSnapshot snap{};
    snap.touchReleased = true;
    snap.longPress = true;
    snap.touchX = static_cast<int16_t>(tx);
    snap.touchY = static_cast<int16_t>(ty);
    return snap;
  }

  freeink::ui::InputSnapshot snap{};
  if (mappedInput.isScreenTouchHeld(tx, ty)) {
    snap.touchHeld = true;
    snap.touchX = static_cast<int16_t>(tx);
    snap.touchY = static_cast<int16_t>(ty);
  }
  if (mappedInput.wasScreenTouchDown(tx, ty)) {
    snap.touchPressed = true;
    snap.touchX = static_cast<int16_t>(tx);
    snap.touchY = static_cast<int16_t>(ty);
  }
  if (mappedInput.wasScreenTapped(tx, ty)) {
    snap.touchReleased = true;
    snap.touchX = static_cast<int16_t>(tx);
    snap.touchY = static_cast<int16_t>(ty);
  } else if (mappedInput.wasScreenTouchReleased()) {
    snap.touchReleased = true;
    snap.touchX = -1;
    snap.touchY = -1;
  }
  return snap;
}
