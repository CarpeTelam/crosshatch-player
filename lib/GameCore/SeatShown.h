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
// state but HandOff; or several local seats (pass: every seat), which shows the turn seat while the
// round is on and seat 0, the frame for everyone, once it is over: in Over, or when the status
// already is (the step that ended it). A hidden pass match's Result shows `mover`, the seat that
// just moved, and its HandOff shows NO_SEAT, whatever the roster. Paused shows what Playing shows
// (the turn seat, or the one local seat): a hidden pass match's render answers its Paused from the
// state it was paused from itself (GameMatchActivity::canvasUnderView), and its VM keeps its own
// view (GameVM). Fails closed: a turn seat or mover this device does not play shows NO_SEAT, never
// another device's seat. Pure.
inline uint8_t seatShown(const MatchState state, const Roster& roster, const Status& status,
                         const uint8_t mover = NO_SEAT) {
  if (state == MatchState::HandOff) return NO_SEAT;
  // No roster has no local seat; one would show seat 0, the frame for everyone.
  if (roster.localSeatCount() <= 1) return roster.firstLocalSeat();
  if (state == MatchState::Over || status.over) return 0;
  // Fails closed: a Result asked without a mover, or with one this device does not play,
  // draws nothing rather than another seat's private view.
  if (state == MatchState::Result) return roster.isLocal(mover) ? mover : NO_SEAT;
  // The same for a turn seat that is not local (a roster with several local seats but not all): LuaGame already
  // refuses a turn outside 1..n, but not one of another device's seats.
  return roster.isLocal(status.turn) ? status.turn : NO_SEAT;
}

}  // namespace GameCore
