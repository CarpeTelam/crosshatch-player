#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

// GameMatchActivity's canvas gate and the frame-on-panel record: the round and seat counts the match waits for and the
// ones the render task has displayed, the number of the frame on the panel, and the last canvas push. Holds the
// counters and the record together on purpose: the frameDisplayed -> roundsDisplayed -> seatDisplayed store order is
// then one function (markDisplayed), not an order two classes' callers must keep. What stays the caller's: recordPush
// runs before markDisplayed, since it records the frame on the panel before this push. Includes the standard library
// only; GameVM satisfies the VmLike template parameter of wait().
class CanvasGate {
 public:
  using Clock = uint32_t (*)();

  // The GameVM::roundsStarted() the match waits for: 1 at first (the match's own first round), and Play again's
  // roundsStarted() plus one after it (loop task writes, handle() before `shown`). Until the count reaches it, the
  // loop asks for no render, since every frame published meanwhile is the last round's (or, at first, there is none),
  // drops every gesture, since none was aimed at the new round, and renderCanvas draws nothing, so a Resume in the gap
  // keeps the pause menu rather than a board whose taps are dropped. Atomic since render reads it. Stores `count` as
  // given.
  void awaitRounds(const uint32_t count) { roundsStartedAwaited.store(count); }
  // A hidden pass match's seat gate, the same for the hand-off: the GameVM::showTurnSeat() request the match waits for
  // (stored by handle() before it shows Playing). Until the VM has served the request the loop asks for no render and
  // drops gestures, and renderCanvas draws nothing, so no frame of the last seat follows the hand-off; until render
  // has displayed it, gestures are still dropped. 0 (the gate open) for any other match.
  void awaitSeat(const uint32_t request) { seatAwaitedRequest.store(request); }
  uint32_t roundsAwaited() const { return roundsStartedAwaited.load(); }
  uint32_t seatAwaited() const { return seatAwaitedRequest.load(); }

  struct Wait {
    bool round;    // the VM has not published the round's (or the seat's) first frame
    bool display;  // round, or render has not yet handed that frame to the panel
  };
  // Until a round's first frame is published, and after that until the render task has handed it to the panel
  // (roundsDisplayed), the screen still shows what came before: the title screen before the match's first round, the
  // end-of-round menu or the last round's frame after Play again. A tap there is not aimed at the round, so it is read
  // (which consumes the contact) and dropped. Only `round` also holds the frame back: once the count has moved, the
  // frame is asked for and drawn.
  // A hidden pass match's next seat is held back the same way: until the VM has served the hand-off's request, the
  // front frame may still be the last seat's, and until render has pushed the new one the hand-off screen is on the
  // panel.
  // Loads the awaited counts, then the VM's (seatShownRequest only when the round count did not already hold), then
  // the displayed counts with acquire, only when needed.
  template <class VmLike>
  Wait wait(const VmLike& vm) const {
    const uint32_t awaited = roundsStartedAwaited.load();
    const uint32_t seat = seatAwaitedRequest.load();
    const bool round = vm.roundsStarted() < awaited || vm.seatShownRequest() < seat;
    const bool display = round || roundsDisplayed.load(std::memory_order_acquire) < awaited ||
                         seatDisplayed.load(std::memory_order_acquire) < seat;
    return Wait{round, display};
  }

  // Render task: whether the frame drawn for `started` and `served` (read once, before anything is drawn) may reach the
  // panel: false in the Play-again gap and before a hidden pass match's next seat has published.
  bool open(const uint32_t started, const uint32_t served) const {
    return !(started < roundsStartedAwaited.load() || served < seatAwaitedRequest.load());
  }

  // Loop task: the number of the frame on the panel (acquire, pairing with markDisplayed's release).
  uint32_t frameOnPanel() const { return frameDisplayed.load(std::memory_order_acquire); }

  // Loop task: the frame on the panel at millis() `atMs`, as far as the last push tells: the frame before it when
  // `atMs` is before that push completed, else frameDisplayed.
  uint32_t frameAt(const uint32_t atMs) const {
    std::lock_guard<std::mutex> lock(pushMutex);
    // Wrap-safe, as millis() is: before the last push completed, the frame before it was on the panel. `recorded`,
    // not doneMs != 0, says a push was recorded: one can complete at millis() 0, the 2^32 ms wrap.
    if (lastPush.recorded && static_cast<int32_t>(atMs - lastPush.doneMs) < 0) return lastPush.before;
    return frameDisplayed.load(std::memory_order_acquire);
  }

  // Render task, after a canvas push that changed the panel (displayBuffer returned, which it does once the panel's
  // refresh has completed, so a touch down before now was made under the frame before this one: frameAt). Records the
  // frame on the panel before this push and `clock()` (millis()) under pushMutex, read inside the lock. Lock order:
  // RenderLock (the caller's), then pushMutex; the loop takes pushMutex alone.
  void recordPush(const Clock clock) {
    std::lock_guard<std::mutex> lock(pushMutex);
    lastPush = LastPush{frameDisplayed.load(std::memory_order_relaxed), clock(), true};
  }

  // Render task, every way out of renderCanvas past the gate, a frame replay skipped as identical to the one on screen
  // included (which the first frame of a round never is, on a screen cleared for it): the loop drops gestures until it
  // does, so a return above this would drop them for good. Release: the frame's drawing is behind it for the loop task
  // that acquires it. The frame's number first (`taken`, GameVM::drawFront's, read under the frame mutex, so a frame
  // the VM published while render ran is named when it is the one drawn), so a loop that sees the counts posts its
  // touches with it.
  void markDisplayed(const uint32_t taken, const uint32_t started, const uint32_t served) {
    frameDisplayed.store(taken, std::memory_order_release);
    roundsDisplayed.store(started, std::memory_order_release);
    seatDisplayed.store(served, std::memory_order_release);
  }

 private:
  // The last canvas push that changed the panel, written by recordPush and read by frameAt, under pushMutex: the frame
  // on the panel before it, millis() when it completed, and whether a push was recorded at all.
  struct LastPush {
    uint32_t before = 0;
    uint32_t doneMs = 0;
    bool recorded = false;
  };

  std::atomic<uint32_t> roundsStartedAwaited{1};
  std::atomic<uint32_t> seatAwaitedRequest{0};
  // Written by render: the roundsStarted() it saw when it last got through the gate and had drawn (and handed to the
  // panel with displayBuffer) the frame. The loop drops gestures until this reaches roundsStartedAwaited, so a tap made
  // while the panel is still refreshing to the round's first frame is not aimed at that round either. displayBuffer
  // returns once the driver has the frame: how much of the panel's own refresh is behind that is the driver's, and
  // unmeasured here.
  std::atomic<uint32_t> roundsDisplayed{0};
  // The GameVM::seatShownRequest() render saw when it last got through the gate and had pushed the frame.
  std::atomic<uint32_t> seatDisplayed{0};
  // Written by render after renderCanvas's push: the number of the frame it drew, which loopPlaying posts each touch
  // with (GameVM::postInput), so the VM drops one made under another seat's frame. Stored before roundsDisplayed and
  // seatDisplayed, so a loop that sees those sees this frame's number.
  std::atomic<uint32_t> frameDisplayed{0};
  mutable std::mutex pushMutex;
  LastPush lastPush;
};
