#pragma once

#include <GameEvent.h>
#include <IGameRules.h>
#include <SeatShown.h>

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
// does not play is shown as the local seat that last drew (else the first local seat),
// so the round starts, and a move, a timer, and a tap are served, while the turn is
// remote. A turn that is no seat gets no input and no draw, so its round counts as
// started only at a local seat's first frame.
class MatchRounds {
 public:
  MatchRounds(GameTimer& timer, InputQueue& queue) : timer(timer), queue(queue) {}
  MatchRounds(const MatchRounds&) = delete;
  MatchRounds& operator=(const MatchRounds&) = delete;

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
  // A Timer event after the round is over: no timer is ever delivered once `over` is true, in any mode (solo, open or
  // hidden pass, nearby), whichever seat the device shows. Callers pass the Session's status().over explicitly, never
  // a seat inferred from it (seat 0 is only a pass round's over frame; a roster with no local seat also reports 0).
  // Neither play nor step reads such an event (the caller that logs the drop asks first). Every other event kind is
  // never late, and neither is a timer due before the round's end.
  static bool lateTimer(const GameCore::GameEvent& event, const bool roundOver) {
    return event.kind == GameCore::EventKind::Timer && roundOver;
  }
  // A Timer event on a roster with no local seat: no seat of this device could read it, and seat 0, the frame for
  // everyone, is never an input seat, so play and step drop it too (after the late check; the caller that logs the
  // drop asks first, with game.timer().accepts). Every other event kind is read as before.
  bool noSeatTimer(const GameCore::GameEvent& event) const;
  // Draws the snapshot for `seat` (0: the frame for everyone). A round's first
  // frame counts it as started; a frame of a round that is over counts its end, once. A local seat drawn is the
  // one that last showed (lastLocalSeat).
  GameCore::Outcome draw(uint8_t seat);

  // ---- the open match (VM task): the steps with the seat seatShown names ----

  // When seatShown names NO_SEAT (a turn that is no seat of the roster), each of these
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
  // The local seat draw() published last, or NO_SEAT before one: "the local seat that last showed" for a turn seat
  // another device plays. Seat 0 and NO_SEAT frames never update it. Kept for the life of the object, Play again
  // included. VM task.
  uint8_t lastLocalSeat() const { return lastLocal; }

 private:
  GameCore::Outcome beginRound();
  // draw(shownSeat()), or nothing (Ok) for NO_SEAT.
  GameCore::Outcome drawShown();
  void countRoundEnd();

  GameTimer& timer;
  InputQueue& queue;
  GameCore::Session* session = nullptr;
  bool roundOver = false;                 // VM task: the current round has been counted
  bool firstFramePending = false;         // VM task: the current round has not drawn yet
  uint8_t lastLocal = GameCore::NO_SEAT;  // VM task: the local seat drawn last
  std::atomic<bool> playAgain{false};
  std::atomic<uint32_t> started{0};
  std::atomic<uint32_t> ended{0};
};

}  // namespace GameScript
