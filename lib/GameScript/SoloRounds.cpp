#include "SoloRounds.h"

#include <Session.h>

#include "GameInput.h"
#include "GameTimer.h"

namespace GameScript {

using GameCore::Outcome;

void SoloRounds::requestPlayAgain() {
  queue.clear();
  playAgain.store(true, std::memory_order_release);
}

Outcome SoloRounds::start(GameCore::Session& played) {
  session = &played;
  return startRound();
}

Outcome SoloRounds::restart() {
  timer.cancel();
  return startRound();
}

Outcome SoloRounds::step(const GameCore::GameEvent& event) {
  // A timer the game re-armed or cancelled after this event fired is not due.
  if (!timer.accepts(event)) return Outcome::Ok;
  Outcome outcome = session->handle(event);
  if (outcome == Outcome::Ok) outcome = session->applyPending();
  if (outcome == Outcome::Ok) outcome = session->draw();
  if (outcome == Outcome::Ok) countRoundEnd();
  return outcome;
}

Outcome SoloRounds::startRound() {
  roundOver = false;
  Outcome outcome = session->start();
  if (outcome == Outcome::Ok) outcome = session->draw();
  if (outcome == Outcome::Ok) countRoundEnd();
  return outcome;
}

void SoloRounds::countRoundEnd() {
  if (roundOver || !session->status().over) return;
  roundOver = true;
  ended.fetch_add(1, std::memory_order_acq_rel);
}

}  // namespace GameScript
