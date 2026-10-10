#pragma once

#include <cstdint>

// The touch latch of GameMatchActivity (loop task only): the frame on the panel and millis() at the first pass that saw
// a finger down (isScreenTouchHeld), kept until a pass with no finger down, so a contact held from under one seat's
// frame until after the next seat's was pushed carries the first. A contact the loop never saw down (it lifted while
// the loop was blocked) has no latch and is tagged when its lift is read, and a tap is also back-dated (CanvasGate::
// frameAt). Plain data, no atomics: one task reads and writes it.
//
// Changing what the latch dates (a back-dated latch, deferred-work.md e5-xr) is a change to latch() alone.
class TouchLatch {
 public:
  // The first pass that sees a finger down: `frame` is the frame on the panel (CanvasGate::frameOnPanel), `ms` is
  // millis(), (x, y) is the first sample (isScreenTouchHeld returns the live point, so for a fast slide it can be past
  // the true touch-down), in logical screen pixels. Sets the first sample only here.
  void latch(const uint32_t frame, const uint32_t ms, const int x, const int y) {
    frame_ = frame;
    ms_ = ms;
    latched_ = true;
    firstX_ = x;
    firstY_ = y;
  }
  // The last sample of every pass that sees a finger, for the touch log line of a contact that ends with no gesture.
  // Meaningful only while latched().
  void sample(const int x, const int y) {
    lastX_ = x;
    lastY_ = y;
  }
  // Whether a finger is still down on this pass (isScreenTouchHeld); set before any early return of the pass's gesture
  // read.
  void setHeld(const bool held) { held_ = held; }
  // No finger down: the contact is over, posted, dropped, or no gesture at all, and the next one latches its own.
  void endIfNotHeld() {
    if (!held_) latched_ = false;
  }
  // Frees the latch.
  void free() { latched_ = false; }

  bool latched() const { return latched_; }
  uint32_t frame() const { return frame_; }
  uint32_t ms() const { return ms_; }
  int firstX() const { return firstX_; }
  int firstY() const { return firstY_; }
  int lastX() const { return lastX_; }
  int lastY() const { return lastY_; }

 private:
  uint32_t frame_ = 0;
  uint32_t ms_ = 0;
  bool latched_ = false;
  int firstX_ = 0;
  int firstY_ = 0;
  int lastX_ = 0;
  int lastY_ = 0;
  bool held_ = false;
};
