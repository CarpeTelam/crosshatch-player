#pragma once

#include <cstdint>

#include "DisplayList.h"

namespace GameScript {

// How many fast refreshes in a row a game gets before replay escalates the next
// one to a half refresh, which clears e-ink ghosting (AD-7).
inline constexpr uint8_t FAST_REFRESH_LIMIT = 10;

// FrameReplay's refresh decision (AD-7), kept pure so it is host-tested. Its
// inputs are each new frame's hash and refresh request (the maximum over the
// frames coalesced into it, FrameBuffers::takeFront), a forceFull flag the match
// sets, and its own count of fast refreshes in a row. It remembers the frame on
// screen, so a frame identical to it is neither drawn nor refreshed.
class RefreshPolicy {
 public:
  // The next frame is drawn and refreshed in full even if identical to the one
  // on screen: the screen shows something else (another screen, a runtime view,
  // an overlay that has just closed). Set at construction, since a match starts
  // over the Games list.
  void forceFull() { force = true; }
  bool fullForced() const { return force; }

  // Decides how to show a frame with this hash and refresh request. False when
  // it is identical to the frame on screen and nothing is forced: skip it.
  // Otherwise true with the refresh to use, and the frame counts as on screen.
  bool decide(uint64_t frameHash, Refresh hint, Refresh& mode);

 private:
  bool force = true;
  bool shown = false;
  uint64_t shownHash = 0;
  uint8_t fastInARow = 0;
};

}  // namespace GameScript
