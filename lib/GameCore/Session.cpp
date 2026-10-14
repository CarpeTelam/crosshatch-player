#include "Session.h"

#include <cstring>

namespace GameCore {

Session::Session(const Roster& roster, IGameRules& rules) : seats(roster), rules(rules) {}

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
  unshipped = false;
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
  const Outcome started = afterSnapshot();
  unshipped = started == Outcome::Ok && seats.localSeatCount() < seats.seats;
  return started;
}

Outcome Session::adopt(const PeerState& peer, Adoption& result) {
  if (!seats.isSeat(peer.fromSeat) || seats.isLocal(peer.fromSeat)) {
    result = Adoption::NotPeer;
    return Outcome::Ok;
  }
  if (pending()) {
    result = Adoption::Busy;
    return Outcome::Ok;
  }
  if (peer.snapshot.empty() || peer.snapshot.size() > SNAPSHOT_BYTES) {
    result = Adoption::BadSnapshot;
    return Outcome::Ok;
  }
  // Newer in serial-number order: the u16 difference as a signed value is above 0.
  const int16_t ahead = static_cast<int16_t>(static_cast<uint16_t>(peer.ver - static_cast<uint16_t>(version)));
  if (ahead <= 0) {
    result = Adoption::Stale;
    return Outcome::Ok;
  }
  const bool startsRound = peer.fromSeat == 1 && (!hasState() || current.over);
  const bool holdsTurn = hasState() && !current.over && current.turn == peer.fromSeat;
  if (!startsRound && !holdsTurn) {
    result = Adoption::OutOfTurn;
    return Outcome::Ok;
  }
  const Status& claimed = peer.status;
  const uint32_t seatMask = (uint32_t{1} << (seats.seats < Roster::MAX_SEATS ? seats.seats : Roster::MAX_SEATS)) - 1;
  const bool statusOk = claimed.over ? claimed.turn == 0 && (claimed.winners & ~seatMask) == 0
                                     : seats.isSeat(claimed.turn) && claimed.winners == 0;
  if (!statusOk) {
    result = Adoption::BadStatus;
    return Outcome::Ok;
  }
  // Adopted: the local counter takes the peer's value, keeping its upper 16 bits, so it never goes backwards.
  std::memcpy(state, peer.snapshot.data(), peer.snapshot.size());
  stateLength = peer.snapshot.size();
  version += static_cast<uint32_t>(ahead);
  settled = version;
  current = peer.status;
  unshipped = false;  // a newer peer STATE means the peer saw ours
  if (startsRound) overDelivered = false;
  result = startsRound ? Adoption::RoundStarted : Adoption::Adopted;
  return deliverOver();
}

bool Session::pendingShipment(Shipment& out) const {
  if (!unshipped) return false;
  out.ver = static_cast<uint16_t>(version);
  out.status = current;
  out.snapshot = snapshot();
  return true;
}

Outcome Session::handle(const GameEvent& event, const uint8_t seat) {
  if (!hasState()) return Outcome::Ok;
  std::span<const uint8_t> returned;
  const Outcome outcome = rules.input(snapshot(), seat, event, returned);
  if (outcome != Outcome::Ok || returned.empty()) return outcome;
  const bool turnSeat = !current.over && current.turn == seat && seats.isLocal(seat);
  const bool runtimeEvent = event.kind == EventKind::Rejected || event.kind == EventKind::Over;
  if (pending() || !turnSeat || runtimeEvent || returned.size() > MOVE_BYTES) {
    ++discarded;
    return Outcome::Ok;
  }
  std::memcpy(move, returned.data(), returned.size());
  moveLength = returned.size();
  moveSeat = seat;
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
    return deliver(rejected, moveSeat);
  }
  if (!keepState(next)) return Outcome::ScriptError;
  unshipped = false;  // a snapshot whose status is not computed ships nothing
  const Outcome accepted = afterSnapshot();
  unshipped = accepted == Outcome::Ok && seats.localSeatCount() < seats.seats;
  return accepted;
}

Outcome Session::draw(const uint8_t seat) {
  if (!hasState()) return Outcome::Ok;
  return rules.draw(snapshot(), seat);
}

Outcome Session::afterSnapshot() {
  const Outcome outcome = rules.status(snapshot(), seats, current);
  if (outcome == Outcome::Ok) settled = version;
  if (outcome != Outcome::Ok) return outcome;
  return deliverOver();
}

Outcome Session::deliverOver() {
  if (!current.over || overDelivered) return Outcome::Ok;
  overDelivered = true;
  GameEvent over;
  over.kind = EventKind::Over;
  for (uint8_t seat = 1; seat <= seats.seats && seat <= Roster::MAX_SEATS; ++seat) {
    if (!seats.isLocal(seat)) continue;
    const Outcome delivered = deliver(over, seat);
    if (delivered != Outcome::Ok) return delivered;
  }
  return Outcome::Ok;
}

bool Session::keepState(const std::span<const uint8_t> bytes) {
  if (bytes.empty() || bytes.size() > SNAPSHOT_BYTES) return false;
  std::memcpy(state, bytes.data(), bytes.size());
  stateLength = bytes.size();
  ++version;
  return true;
}

Outcome Session::deliver(const GameEvent& event, const uint8_t seat) {
  std::span<const uint8_t> ignored;
  const Outcome outcome = rules.input(snapshot(), seat, event, ignored);
  if (outcome == Outcome::Ok && !ignored.empty()) ++discarded;
  return outcome;
}

}  // namespace GameCore
