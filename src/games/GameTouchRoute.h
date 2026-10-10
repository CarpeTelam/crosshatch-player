#pragma once

#include <cstdint>

// What GameMatchActivity::loopPlaying does with a gesture GameTouch::classify sent to the game ("aimed"): post it, and
// with which frame, or drop it, and why. Pure given a pure Source, so it is host-tested; the activity keeps reading the
// hardware, calling GameVM::postInput, and logging, and performs the Decision.
namespace GameTouchRoute {

// GameVM::UNTAGGED, which the VM never drops; the activity pins the two together with a static_assert. This header
// includes the standard library only.
constexpr uint32_t UNTAGGED = UINT32_MAX;

// Post: send the touch to the VM with Decision::shownFrame. DropBeforeHandOff: the first-frame path found the contact
// began before the hand-off passed (the activity logs beforeHandOffMs). DropBeforeDisplay: the frame is not on the
// panel yet (the activity logs it).
enum class Action : uint8_t { Post, DropBeforeHandOff, DropBeforeDisplay };

// What the loop pass already has in hand. `latched`, `latchedFrame`, and `latchedMs` are the touch latch's
// (TouchLatch); `awaitingRound` and `awaitingDisplay` are the canvas gate's (CanvasGate::wait); `playingSinceMs` is the
// millis() at the last transition handle() took.
struct Aimed {
  bool tap;
  bool awaitingRound;
  bool awaitingDisplay;
  bool latched;
  uint32_t latchedFrame;
  uint32_t latchedMs;
  uint32_t playingSinceMs;
};

struct Decision {
  Action action = Action::Post;
  uint32_t shownFrame = 0;  // Post: the frame to tag the touch with, never UNTAGGED
  int beforeHandOffMs = 0;  // DropBeforeHandOff: how long before the hand-off passed the contact began
};

// `source` supplies what is read lazily, only on the path that reads it (reading it every pass would change timing, and
// take the frame record's lock on every tap):
//   bool firstFramePushing();         // GameMatchActivity::firstFramePushing, an acquire load
//   uint32_t nowMs();                 // millis()
//   uint32_t heldMs();                // HalGPIO::lastTouchHeldMs(), a tap's touch-only held time
//   uint32_t frameOnPanel();          // CanvasGate::frameOnPanel
//   uint32_t frameAt(uint32_t atMs);  // CanvasGate::frameAt
//   uint32_t currentFrame();          // GameVM::frameGen()
// A pass with `awaitingRound` never reads firstFramePushing; a dropped touch reads nothing further; a swipe never asks
// frameAt.
template <class Source>
Decision decide(const Aimed& in, Source& source) {
  Decision out;
  // The first seat frame after "I'm ready" accepts a touch made during its push (the owner's decision of 2026-10-03:
  // the hand-off needs one tap per step): once the VM has published the frame (!awaitingRound), a touch whose
  // touch-down came at or after the hand-off passed (playingSinceMs) is that seat's move, whatever the push has
  // reached. It skips the latched frame and frameAt and is posted with the frame the VM has now, so
  // GameVM::madeUnderAnotherSeat still keeps it for that seat. A contact whose touch-down preceded the transition (the
  // press that passed the hand-off, lifting late) is dropped. Open pass and solo never set the flag.
  if (!in.awaitingRound && source.firstFramePushing()) {
    const uint32_t nowMs = source.nowMs();
    uint32_t began = in.latched ? in.latchedMs : nowMs;
    if (in.tap) {
      const auto backDated = static_cast<uint32_t>(nowMs - source.heldMs());
      if (static_cast<int32_t>(backDated - began) < 0) began = backDated;
    }
    if (static_cast<int32_t>(began - in.playingSinceMs) >= 0) {
      const uint32_t gen = source.currentFrame();
      // As the tag below: UNTAGGED is never dropped by the VM, one less can only drop.
      out.action = Action::Post;
      out.shownFrame = gen == UNTAGGED ? gen - 1 : gen;
    } else {
      out.action = Action::DropBeforeHandOff;
      out.beforeHandOffMs = -static_cast<int>(static_cast<int32_t>(began - in.playingSinceMs));
    }
  } else if (in.awaitingDisplay) {
    out.action = Action::DropBeforeDisplay;
  } else {
    // With the frame on the panel when the loop first saw the finger down (or now, for a contact it never saw down), so
    // the VM drops it if another seat has been drawn since (a pass match). A tap is also back-dated by its touch-only
    // held time (HalGPIO::lastTouchHeldMs; MappedInputManager::getHeldTime answers a button's hold on a pass with a
    // button edge) to its first touch sample against the last push (frameAt), and the older frame is posted. What this
    // cannot reach is in game-canvas.md. A frame number equal to GameVM::UNTAGGED, which the VM never drops, is posted
    // one less, which can only drop.
    uint32_t shown = in.latched ? in.latchedFrame : source.frameOnPanel();
    if (in.tap) {
      const uint32_t atTouchDown = source.frameAt(static_cast<uint32_t>(source.nowMs() - source.heldMs()));
      if (static_cast<int32_t>(atTouchDown - shown) < 0) shown = atTouchDown;
    }
    out.action = Action::Post;
    out.shownFrame = shown == UNTAGGED ? shown - 1 : shown;
  }
  return out;
}

}  // namespace GameTouchRoute
