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

// The solo round loop the GameVM task runs over one Session (AD-21): the first
// round, one step per input event, Play again, and the count of ended rounds the
// match watches to enter Over, and the count of started rounds it watches after
// Play again. requestPlayAgain(), roundsStarted(), and roundsEnded() are for the
// loop task; everything else runs on the VM task. Host-tested with a LuaGame.
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
  // published (start() and each restart()), before that round can count as ended.
  // Any frame published after Play again and before this count moves is the last
  // round's, so the match asks for no render until it does.
  uint32_t roundsStarted() const { return started.load(std::memory_order_acquire); }

  // VM task: the first round of `session` (setup, status, `over` if it is already
  // over, then draw).
  GameCore::Outcome start(GameCore::Session& session);
  // VM task: true once after requestPlayAgain().
  bool takePlayAgain() { return playAgain.exchange(false, std::memory_order_acq_rel); }
  // VM task: a new round on the same Session, so ver keeps counting; the pending
  // timer is cancelled first, so a timer event already queued is stale.
  GameCore::Outcome restart();
  // VM task: one event as the game sees it: a stale timer event is dropped, then
  // input, the pending move, and a draw.
  GameCore::Outcome step(const GameCore::GameEvent& event);

 private:
  GameCore::Outcome startRound();
  void countRoundEnd();

  GameTimer& timer;
  InputQueue& queue;
  GameCore::Session* session = nullptr;
  bool roundOver = false;  // VM task: the current round has been counted
  std::atomic<bool> playAgain{false};
  std::atomic<uint32_t> started{0};
  std::atomic<uint32_t> ended{0};
};

}  // namespace GameScript
