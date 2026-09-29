#include "Session.h"

#include <cstring>

namespace GameCore {

Session::Session(const Roster& roster, IGameRules& rules)
    : seats(roster), rules(rules), localSeat(roster.firstLocalSeat()) {}

bool Session::restore(const std::span<const uint8_t> snapshot, const uint32_t ver) {
  if (snapshot.empty() || snapshot.size() > SNAPSHOT_BYTES) return false;
  std::memcpy(state, snapshot.data(), snapshot.size());
  stateLength = snapshot.size();
  version = ver;
  restored = true;
  return true;
}

Outcome Session::start() {
  moveLength = 0;
  overDelivered = false;
  if (restored) {
    // Once: a rematch (Play again) calls start() again and runs setup.
    restored = false;
    return afterSnapshot();
  }
  const GameContext ctx{seats.seats, seats.mode, seats.api};
  std::span<const uint8_t> initial;
  const Outcome outcome = rules.setup(ctx, initial);
  if (outcome != Outcome::Ok) return outcome;
  if (!keepState(initial)) return Outcome::ScriptError;
  return afterSnapshot();
}

Outcome Session::handle(const GameEvent& event) {
  std::span<const uint8_t> returned;
  const Outcome outcome = rules.input(snapshot(), localSeat, event, returned);
  if (outcome != Outcome::Ok || returned.empty()) return outcome;
  const bool ownTurn = !current.over && current.turn == localSeat;
  const bool runtimeEvent = event.kind == EventKind::Rejected || event.kind == EventKind::Over;
  if (pending() || !ownTurn || runtimeEvent || returned.size() > MOVE_BYTES) {
    ++discarded;
    return Outcome::Ok;
  }
  std::memcpy(move, returned.data(), returned.size());
  moveLength = returned.size();
  moveSeat = localSeat;
  return Outcome::Ok;
}

Outcome Session::applyPending() {
  if (!pending()) return Outcome::Ok;
  std::span<const uint8_t> next;
  reason[0] = '\0';
  const Outcome outcome = rules.apply(snapshot(), moveSeat, {move, moveLength}, next, reason);
  moveLength = 0;
  if (outcome != Outcome::Ok) return outcome;
  if (next.empty()) {
    reason[REJECT_REASON_BYTES] = '\0';
    GameEvent rejected;
    rejected.kind = EventKind::Rejected;
    rejected.reason = reason;
    return deliver(rejected);
  }
  if (!keepState(next)) return Outcome::ScriptError;
  return afterSnapshot();
}

Outcome Session::draw() { return rules.draw(snapshot(), localSeat); }

Outcome Session::afterSnapshot() {
  const Outcome outcome = rules.status(snapshot(), seats, current);
  if (outcome == Outcome::Ok) settled = version;
  if (outcome != Outcome::Ok || !current.over || overDelivered) return outcome;
  overDelivered = true;
  GameEvent over;
  over.kind = EventKind::Over;
  return deliver(over);
}

bool Session::keepState(const std::span<const uint8_t> bytes) {
  if (bytes.empty() || bytes.size() > SNAPSHOT_BYTES) return false;
  std::memcpy(state, bytes.data(), bytes.size());
  stateLength = bytes.size();
  ++version;
  return true;
}

Outcome Session::deliver(const GameEvent& event) {
  std::span<const uint8_t> ignored;
  const Outcome outcome = rules.input(snapshot(), localSeat, event, ignored);
  if (outcome == Outcome::Ok && !ignored.empty()) ++discarded;
  return outcome;
}

}  // namespace GameCore
