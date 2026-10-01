#pragma once

#include <cstdint>

#include "IGameRules.h"
#include "MatchLifecycle.h"
#include "Roster.h"

namespace GameCore {

// The seat whose frame this device shows (and whose input it reads) in match state `state`.
// For the two roster shapes a match has: one local seat (solo, a nearby device), shown in every
// state; or every seat local (pass), which shows the turn seat while the round is on and seat 0,
// the frame for everyone, once it is over: in Over, or when the status already is (the step that
// ended it). A roster with several local seats but not all would be shown its turn seat even when
// that seat is not local; no match has one. Pure; entry 2 adds the Result and HandOff states.
inline uint8_t seatShown(const MatchState state, const Roster& roster, const Status& status) {
  // No roster has no local seat; one would show seat 0, the frame for everyone.
  if (roster.localSeatCount() <= 1) return roster.firstLocalSeat();
  if (state == MatchState::Over || status.over) return 0;
  return status.turn;
}

}  // namespace GameCore
