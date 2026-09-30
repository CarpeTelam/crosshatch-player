#pragma once

// The one HalGPIO call the match makes (a swipe's start and end points) and the global
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

  Swipe swipe;
};

extern HalGPIO gpio;
