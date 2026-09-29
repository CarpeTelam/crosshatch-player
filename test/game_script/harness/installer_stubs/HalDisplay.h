#pragma once

// What PngToBmpConverter reads of the panel on the host: the refresh modes, and the size its
// full-screen entry point converts to (the installer uses only the sized entry points).
#include <cstdint>

class HalDisplay {
 public:
  enum RefreshMode { FULL_REFRESH, HALF_REFRESH, FAST_REFRESH };  // as lib/hal/HalDisplay.h

  uint16_t getDisplayWidth() const { return 480; }
  uint16_t getDisplayHeight() const { return 800; }
};

extern HalDisplay display;  // defined by the suite
