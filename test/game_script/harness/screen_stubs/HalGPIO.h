#pragma once

// The HalGPIO calls the match makes (a swipe's start and end points, the last contact's held time) and the global
// `gpio` it makes it on. The real header is the board's pin map and interrupt handling.
// A test scripts one swipe with `gpio.swipe`; MappedInputManager::clear() forgets it, as
// the next frame does.

// Like the real header, it brings in Arduino.h (millis), which screens use without including it.
#include <Arduino.h>

class HalGPIO {
 public:
  struct Swipe {
    bool pending = false;
    float startX = 0, startY = 0, endX = 0, endY = 0;
  };

  bool wasSwipe(float& startX, float& startY, float& endX, float& endY) const {
    if (!swipe.pending) return false;
    startX = swipe.startX;
    startY = swipe.startY;
    endX = swipe.endX;
    endY = swipe.endY;
    return true;
  }

  // The last contact's touch-only held time (the device's InputManager::lastTouchHeldMs, set on its release update):
  // the input double's liftTouch() and quickTap() set it.
  unsigned long lastTouchHeldMs() const { return touchHeldMs; }

  Swipe swipe;
  unsigned long touchHeldMs = 0;
};

extern HalGPIO gpio;
