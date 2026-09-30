#pragma once

// The one thing CoverGridHomeUi asks of the board (freeink-sdk BoardConfig.h, whose real header
// needs ESP-IDF): whether it has a touch panel. A test turns it off to see the button board's home.

namespace BoardConfig {

inline bool& touchPanel() {
  static bool present = true;
  return present;
}
inline bool hasTouch() { return touchPanel(); }

}  // namespace BoardConfig
