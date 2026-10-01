#pragma once

#include <cstdint>

#include "IGameRules.h"
#include "MatchLifecycle.h"
#include "Roster.h"

namespace GameCore {

// No seat: the device draws no seat's frame (the hand-off screen) and reads no seat's input.
// Never drawn: a seat number is 1..Roster::MAX_SEATS, and 0 is the frame for everyone.
inline constexpr uint8_t NO_SEAT = 0xFF;

// The seat whose frame this device shows (and whose input it reads) in match state `state`.
// For the two roster shapes a match has: one local seat (solo, a nearby device), shown in every
// state but HandOff; or every seat local (pass), which shows the turn seat while the round is on
// and seat 0, the frame for everyone, once it is over: in Over, or when the status already is (the
// step that ended it). A hidden pass match's Result shows `mover`, the seat that just moved, and
// its HandOff shows NO_SEAT, whatever the roster. Paused here is the open/solo answer only (the
// turn seat, or the one local seat): a match asks with its lifecycle (the overload below), which
// answers a hidden match's Paused from the state it was paused from. A roster with several local
// seats but not all would be shown its turn seat even when that seat is not local; no match has
// one. Pure.
inline uint8_t seatShown(const MatchState state, const Roster& roster, const Status& status,
                         const uint8_t mover = NO_SEAT) {
  if (state == MatchState::HandOff) return NO_SEAT;
  // No roster has no local seat; one would show seat 0, the frame for everyone.
  if (roster.localSeatCount() <= 1) return roster.firstLocalSeat();
  if (state == MatchState::Over || status.over) return 0;
  // Fails closed: a Result asked without a mover, or with one this device does not play,
  // draws nothing rather than another seat's private view.
  if (state == MatchState::Result) return roster.isLocal(mover) ? mover : NO_SEAT;
  return status.turn;
}

// The seat a match shows: the state form with `lifecycle.state()`, except that Paused answers
// for the state it returns to, so a hidden pass match paused from Result keeps the mover's view
// and one paused from HandOff shows NO_SEAT, never the next seat's view. An open pass or solo
// match's Paused shows what the state form shows (its resumesTo() is Playing). Pure.
inline uint8_t seatShown(const MatchLifecycle& lifecycle, const Roster& roster, const Status& status,
                         const uint8_t mover = NO_SEAT) {
  const MatchState state = lifecycle.state() == MatchState::Paused ? lifecycle.resumesTo() : lifecycle.state();
  return seatShown(state, roster, status, mover);
}

}  // namespace GameCore
