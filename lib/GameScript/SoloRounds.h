#pragma once

#include <GameEvent.h>
#include <IGameRules.h>

#include <atomic>
#include <cstdint>

namespace GameCore {
class Session;
}

namespace GameScript {

class GameTimer;
class InputQueue;

// The round loop the GameVM task runs over one Session (AD-21): the first round,
// one step per input event, Play again, and the count of ended rounds the match
// watches to enter Over, and the count of started rounds it watches after Play
// again. requestPlayAgain(), roundsStarted(), and roundsEnded() are for the loop
// task; everything else runs on the VM task. Host-tested with a LuaGame.
//
// The name predates pass and play: the loop serves a roster with one local seat
// (solo, nearby), with every seat local (pass), or with some local and not all. It
// comes in two layers. The steps (begin, beginAgain, play, draw) take the seat from
// their caller and never pick one. The open match's composition (start, restart, step)
// runs them with the seat GameCore::seatShown names: the one local seat, or with
// several the turn seat, and seat 0 once the round is over. A turn seat this device
// does not play gets no input and no draw, so its round counts as started only at a
// local seat's first frame.
class SoloRounds {
 public:
  SoloRounds(GameTimer& timer, InputQueue& queue) : timer(timer), queue(queue) {}
  SoloRounds(const SoloRounds&) = delete;
  SoloRounds& operator=(const SoloRounds&) = delete;

  // Loop task: Play again. Drops the queued events, which were aimed at the last
  // round, and asks the VM for a new round before its next event.
  void requestPlayAgain();
  // Rounds that have ended so far: one is counted when the status turns over,
  // after the Session has delivered `over` and the round's last frame is drawn.
  uint32_t roundsEnded() const { return ended.load(std::memory_order_acquire); }
  // Rounds that have started so far: one is counted once a round's first frame is
  // published (the first draw() after begin() or beginAgain()), before that round can
  // count as ended.
  // Any frame published after Play again and before this count moves is the last
  // round's, so the match asks for no render until it does.
  uint32_t roundsStarted() const { return started.load(std::memory_order_acquire); }

  // VM task: true once after requestPlayAgain().
  bool takePlayAgain() { return playAgain.exchange(false, std::memory_order_acq_rel); }

  // ---- the steps (VM task): each takes its seat from the caller ----

  // The first round of `session`: setup, status, and `over` if it is already over.
  // Draws nothing; the round counts as started at its first draw().
  GameCore::Outcome begin(GameCore::Session& session);
  // A new round on the same Session, so ver keeps counting; the pending timer is
  // cancelled first, so a timer event already queued is stale. Draws nothing.
  GameCore::Outcome beginAgain();
  // One event to `seat`'s input, then the pending move. A timer event the game
  // re-armed or cancelled after it fired is dropped first, and so is a late one
  // (lateTimer). Draws nothing.
  GameCore::Outcome play(const GameCore::GameEvent& event, uint8_t seat);
  // A Timer event for seat 0, the frame for everyone: the round is over, and seat 0 is
  // never an input seat, so no step reads the event (play and step drop it; the
  // caller that logs the drop asks first). Other seats' timers, and every other event
  // kind, are never late.
  static bool lateTimer(const GameCore::GameEvent& event, const uint8_t seat) {
    return event.kind == GameCore::EventKind::Timer && seat == 0;
  }
  // Draws the snapshot for `seat` (0: the frame for everyone). A round's first
  // frame counts it as started; a frame of a round that is over counts its end, once.
  GameCore::Outcome draw(uint8_t seat);

  // ---- the open match (VM task): the steps with the seat seatShown names ----

  // When seatShown names NO_SEAT (a turn seat this device does not play), each of these
  // reads no input, draws nothing, and returns Ok.
  // begin, then draw.
  GameCore::Outcome start(GameCore::Session& session);
  // beginAgain, then draw.
  GameCore::Outcome restart();
  // One event as the game sees it: a stale or late timer event is dropped with no draw; else
  // play, then draw (the seat shown after the move).
  GameCore::Outcome step(const GameCore::GameEvent& event);
  // The seat GameCore::seatShown names for the session's status now: after start, restart, or a step that drew, the
  // seat whose frame was published last (GameVM notes it, to drop a touch posted under another seat's frame).
  uint8_t shownSeat() const;

 private:
  GameCore::Outcome beginRound();
  // draw(shownSeat()), or nothing (Ok) for NO_SEAT.
  GameCore::Outcome drawShown();
  void countRoundEnd();

  GameTimer& timer;
  InputQueue& queue;
  GameCore::Session* session = nullptr;
  bool roundOver = false;          // VM task: the current round has been counted
  bool firstFramePending = false;  // VM task: the current round has not drawn yet
  std::atomic<bool> playAgain{false};
  std::atomic<uint32_t> started{0};
  std::atomic<uint32_t> ended{0};
};

}  // namespace GameScript
