#include "SoloRounds.h"

#include <SeatShown.h>
#include <Session.h>

#include "GameInput.h"
#include "GameTimer.h"

namespace GameScript {

using GameCore::Outcome;

void SoloRounds::requestPlayAgain() {
  queue.clear();
  playAgain.store(true, std::memory_order_release);
}

Outcome SoloRounds::begin(GameCore::Session& played) {
  session = &played;
  return beginRound();
}

Outcome SoloRounds::beginAgain() {
  timer.cancel();
  return beginRound();
}

Outcome SoloRounds::play(const GameCore::GameEvent& event, const uint8_t seat) {
  // A timer the game re-armed or cancelled after this event fired is not due.
  if (!timer.accepts(event)) return Outcome::Ok;
  // A timer that fell due after the round is over: seat 0 is a frame, never an input seat.
  if (lateTimer(event, seat)) return Outcome::Ok;
  const Outcome outcome = session->handle(event, seat);
  if (outcome != Outcome::Ok) return outcome;
  return session->applyPending();
}

Outcome SoloRounds::draw(const uint8_t seat) {
  const Outcome outcome = session->draw(seat);
  if (outcome != Outcome::Ok) return outcome;
  if (firstFramePending) {
    // After the round's first frame is published and before any end, so the match
    // never sees a round end without its start.
    firstFramePending = false;
    started.fetch_add(1, std::memory_order_acq_rel);
  }
  // After the draw, so the round's last frame is out before the match sees the end.
  countRoundEnd();
  return outcome;
}

Outcome SoloRounds::start(GameCore::Session& played) {
  const Outcome outcome = begin(played);
  if (outcome != Outcome::Ok) return outcome;
  return drawShown();
}

Outcome SoloRounds::restart() {
  const Outcome outcome = beginAgain();
  if (outcome != Outcome::Ok) return outcome;
  return drawShown();
}

Outcome SoloRounds::step(const GameCore::GameEvent& event) {
  // A stale timer is dropped before anything, with no draw (play would drop it too,
  // but the draw after it would publish a frame nothing changed).
  if (!timer.accepts(event)) return Outcome::Ok;
  // No seat this device plays has the turn: nothing reads the event.
  const uint8_t seat = shownSeat();
  if (seat == GameCore::NO_SEAT) return Outcome::Ok;
  // Dropped with no draw, as a stale one (GameVM logs it).
  if (lateTimer(event, seat)) return Outcome::Ok;
  const Outcome outcome = play(event, seat);
  if (outcome != Outcome::Ok) return outcome;
  return drawShown();
}

Outcome SoloRounds::drawShown() {
  // A turn seat this device does not play (a roster with some seats local, not all) is never drawn: the frame on
  // screen stays, and no other device's seat is drawn here.
  const uint8_t seat = shownSeat();
  if (seat == GameCore::NO_SEAT) return Outcome::Ok;
  return draw(seat);
}

Outcome SoloRounds::beginRound() {
  roundOver = false;
  firstFramePending = true;
  return session->start();
}

void SoloRounds::countRoundEnd() {
  if (roundOver || !session->status().over) return;
  roundOver = true;
  ended.fetch_add(1, std::memory_order_acq_rel);
}

uint8_t SoloRounds::shownSeat() const {
  const GameCore::Status& status = session->status();
  const GameCore::MatchState state = status.over ? GameCore::MatchState::Over : GameCore::MatchState::Playing;
  return GameCore::seatShown(state, session->roster(), status);
}

}  // namespace GameScript
