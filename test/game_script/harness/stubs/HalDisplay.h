#pragma once

// The one piece of HalDisplay that src/games reads on the host: the refresh modes
// (FrameReplay::refreshMode). The real header pulls in Arduino and the panel driver.

class HalDisplay {
 public:
  enum RefreshMode { FULL_REFRESH, HALF_REFRESH, FAST_REFRESH };  // as lib/hal/HalDisplay.h
};
