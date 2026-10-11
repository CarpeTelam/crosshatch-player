#pragma once

// HomeActivity.cpp includes src/HapticFeedback.h and calls haptic_feedback::touchAction when a touch activates a
// book, row, or tab. The real one drives the motor (HalHaptics, an SDK library) by the user's setting; this double
// only counts the calls, which a test reads and clears.
namespace haptic_feedback {
inline int& touchActions() {
  static int count = 0;
  return count;
}
inline void touchAction(bool = false) { ++touchActions(); }
}  // namespace haptic_feedback
