#include "MatchRounds.h"

#include <SeatShown.h>
#include <Session.h>

#include "GameInput.h"
#include "GameTimer.h"

namespace GameScript {

using GameCore::Outcome;

void MatchRounds::requestPlayAgain() {
  queue.clear();
  playAgain.store(true, std::memory_order_release);
}

Outcome MatchRounds::begin(GameCore::Session& played) {
  session = &played;
  return beginRound();
}

Outcome MatchRounds::beginAgain() {
  timer.cancel();
  return beginRound();
}

Outcome MatchRounds::play(const GameCore::GameEvent& event, const uint8_t seat) {
  // A timer the game re-armed or cancelled after this event fired is not due.
  if (!timer.accepts(event)) return Outcome::Ok;
  // A timer that fell due after the round is over is never delivered, whichever seat is shown.
  if (lateTimer(event, session->status().over)) return Outcome::Ok;
  // A roster with no local seat reads no timer, whichever seat the caller names.
  if (noSeatTimer(event)) return Outcome::Ok;
  const Outcome outcome = session->handle(event, seat);
  if (outcome != Outcome::Ok) return outcome;
  return session->applyPending();
}

bool MatchRounds::noSeatTimer(const GameCore::GameEvent& event) const {
  return event.kind == GameCore::EventKind::Timer && session->roster().localSeatCount() == 0;
}

Outcome MatchRounds::draw(const uint8_t seat) {
  const Outcome outcome = session->draw(seat);
  if (outcome != Outcome::Ok) return outcome;
  // The seat that last showed: only a local seat counts (not seat 0, the frame for everyone).
  if (session->roster().isLocal(seat)) lastLocal = seat;
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

void MatchRounds::attach(GameCore::Session& played) {
  session = &played;
  roundOver = false;
  firstFramePending = true;
}

Outcome MatchRounds::adopted(const GameCore::Adoption adoption) {
  switch (adoption) {
    case GameCore::Adoption::Adopted:
      return drawShown();
    case GameCore::Adoption::RoundStarted:
      // The host's next round: what beginAgain does for the round this device starts itself, minus setup (the host ran
      // it).
      timer.cancel();
      return beginAdopted();
    case GameCore::Adoption::NotPeer:
    case GameCore::Adoption::Busy:
    case GameCore::Adoption::BadSnapshot:
    case GameCore::Adoption::Stale:
    case GameCore::Adoption::OutOfTurn:
    case GameCore::Adoption::BadStatus:
      break;  // the Session changed nothing
  }
  return Outcome::Ok;
}

Outcome MatchRounds::start(GameCore::Session& played) {
  const Outcome outcome = begin(played);
  if (outcome != Outcome::Ok) return outcome;
  return drawShown();
}

Outcome MatchRounds::restart() {
  const Outcome outcome = beginAgain();
  if (outcome != Outcome::Ok) return outcome;
  return drawShown();
}

Outcome MatchRounds::step(const GameCore::GameEvent& event) {
  // A nearby device before the host's first STATE holds no state: nothing to read, and a draw would publish an empty
  // first frame and count a started round. Before every guard below, and the same Ok they return.
  if (!session->hasState()) return Outcome::Ok;
  // A stale timer is dropped before anything, with no draw (play would drop it too,
  // but the draw after it would publish a frame nothing changed).
  if (!timer.accepts(event)) return Outcome::Ok;
  // Dropped with no draw, as a stale one (GameVM logs it); asked before the seat, so a roster with no local seat drops
  // it for the same reason.
  if (lateTimer(event, session->status().over)) return Outcome::Ok;
  // A timer on a roster with no local seat is dropped likewise (GameVM logs it), not read as seat 0.
  if (noSeatTimer(event)) return Outcome::Ok;
  // A turn that is no seat: nothing reads the event.
  const uint8_t seat = shownSeat();
  if (seat == GameCore::NO_SEAT) return Outcome::Ok;
  const Outcome outcome = play(event, seat);
  if (outcome != Outcome::Ok) return outcome;
  return drawShown();
}

Outcome MatchRounds::drawShown() {
  // A turn that is no seat is never drawn: the frame on screen stays, and no other device's seat is drawn here.
  const uint8_t seat = shownSeat();
  if (seat == GameCore::NO_SEAT) return Outcome::Ok;
  return draw(seat);
}

Outcome MatchRounds::beginRound() {
  roundOver = false;
  firstFramePending = true;
  return session->start();
}

Outcome MatchRounds::beginAdopted() {
  roundOver = false;
  firstFramePending = true;
  return drawShown();
}

void MatchRounds::countRoundEnd() {
  if (roundOver || !session->status().over) return;
  roundOver = true;
  ended.fetch_add(1, std::memory_order_acq_rel);
}

uint8_t MatchRounds::shownSeat() const {
  const GameCore::Status& status = session->status();
  const GameCore::MatchState state = status.over ? GameCore::MatchState::Over : GameCore::MatchState::Playing;
  return GameCore::seatShown(state, session->roster(), status, GameCore::NO_SEAT, lastLocal);
}

}  // namespace GameScript
