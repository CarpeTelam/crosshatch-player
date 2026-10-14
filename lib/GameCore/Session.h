#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "GameEvent.h"
#include "IGameRules.h"
#include "Roster.h"

namespace GameCore {

// Codec output limits (AD-10), in bytes; GameScript::Codec's limits must equal them.
inline constexpr size_t SNAPSHOT_BYTES = 1400;
inline constexpr size_t MOVE_BYTES = 256;
// A rejection's reason, as input() receives it in every mode; no REJECT frame crosses a link (a rejected move stays on
// its device, AD-13, 2026-10-12). The 64-byte cut is the event's.
inline constexpr size_t REJECT_REASON_BYTES = 64;

// A STATE the link brought from the peer, as adopt() takes it: Protocol::State's fields plus the sender's seat.
struct PeerState {
  // The sender's seat, which the caller derives from the link identity of the frame's sender
  // (MatchSink::seatOf), never from the payload (AD-11).
  uint8_t fromSeat = 0;
  uint16_t ver = 0;  // the wire ver: the low 16 bits of the sender's Session::ver()
  Status status;     // the status the sender shipped (AD-9); this device does not compute it
  std::span<const uint8_t> snapshot;
};

// What adopt() did. Adopted and RoundStarted adopt the state; every other value changed nothing.
enum class Adoption : uint8_t {
  Adopted,       // the peer held the turn: its move
  RoundStarted,  // seat 1 started a round: this device had no state, or its status was over
  NotPeer,       // fromSeat is no seat of the roster, or is a seat this device plays
  Busy,          // a local move is pending (handle() kept one that applyPending() has not taken)
  BadSnapshot,   // empty, or longer than SNAPSHOT_BYTES
  Stale,         // ver is not newer than this device's (u16, compared across its wrap)
  OutOfTurn,     // the sender neither holds the turn in this device's status nor starts a round
  BadStatus,     // a turn or winner that is no seat of the roster, or `over` with a turn / not over with winners
};

// A snapshot this device made that the peer must be told of: Protocol::State's fields (Protocol.h includes
// Session.h, so the type is repeated; MatchSupport's pair rig builds a Protocol::Frame from it and the suites
// pin that the two agree).
struct Shipment {
  uint16_t ver = 0;                   // uint16_t(ver()) of the snapshot
  Status status;                      // the status computed for it (AD-9)
  std::span<const uint8_t> snapshot;  // aliases the Session's snapshot: valid until the next Session call
};

// One match's session on the authority (AD-9, AD-11), for the roster's local seats
// (one in solo, every seat in pass): the roster, the encoded snapshot that is the
// source of truth, its version, the status computed after each snapshot, and the one
// pending move. The caller names the seat of each input and draw; the Session never
// picks one. It drives the rules and holds no game logic. About 1.8 KB, so the owner
// allocates it from the VM arena (AD-5); confined to the VM task.
//
// A nearby device that is not seat 1 never calls start(): its first state is its first adopt() (hasState()).
//
// Moves are applied strictly one at a time, and only from the turn seat: a move
// returned while one is pending, after the round is over, by any seat but the turn
// seat (or a turn seat that is not local), or in answer to a Rejected or Over event is
// discarded (the last also keeps a game that answers every rejection with a move from
// looping inside one step).
class Session {
 public:
  Session(const Roster& roster, IGameRules& rules);
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  // Before start(): makes `snapshot` at `ver` the state the next start() begins from
  // (a saved match resumed), so that start() skips setup, keeps the state and ver, and
  // computes the status (and delivers Over if it is already over). False, changing
  // nothing, when the snapshot is empty or larger than SNAPSHOT_BYTES.
  bool restore(std::span<const uint8_t> snapshot, uint32_t ver);
  // Check pending() before taking a STATE off the StateQueue: it was acked when queued, so a taken entry that adopt()
  // answers with Busy is lost.
  // Mid-match, on the VM task, only between two input-to-apply cycles (never while pending()): makes the peer's
  // snapshot this device's state, and its shipped status this device's status, when the rules allow. `result` says
  // what happened. The return value is the Over delivery's: Ok, or the ScriptError or Cancelled of the input() call
  // that took the `over` event (the state is adopted by then, as after applyPending). A refusal returns Ok and
  // changes nothing. Refusals, in this order: NotPeer, Busy, BadSnapshot, Stale, OutOfTurn, BadStatus. The status is
  // the shipped one: rules.status() is not called (AD-9). Unlike restore() it runs mid-match or with no state, and
  // never calls setup or status.
  Outcome adopt(const PeerState& peer, Adoption& result);
  // True once this device holds a state: after start(), or after restore() and start(), or after a first adopt().
  // Until then handle() and draw() do nothing and call no rules (the match shows "waiting for the host").
  bool hasState() const { return stateLength != 0; }
  // True while a snapshot made here has not been handed to the link: set when start() runs setup, or applyPending()
  // accepts a move, and the call returns Ok (a ScriptError or Cancelled ships nothing), for a roster with a seat this
  // device does not play. Never set in solo or pass. Cleared by shipped(), by an adopt, and by the next start().
  bool shipmentPending() const { return unshipped; }
  // The latest snapshot to ship; false when none is pending. Latest wins: a second snapshot before shipped() replaces
  // the first; the owner never starts a round while an `over` shipment is pending.
  bool pendingShipment(Shipment& out) const;
  // The owner handed the shipment to the link (ReliableLink::send returned true). Until then it stays pending and the
  // owner offers it again at its next step, so a send the link refused loses nothing.
  void shipped() { unshipped = false; }
  // Starts a round: setup(ctx), a new snapshot, its status, and Over if the round
  // is already over; or, after restore(), the restored state's status alone. Called
  // again for a rematch, it runs setup and keeps counting ver.
  Outcome start();
  // Delivers an event to `seat`'s input and keeps the move it returns, as `seat`'s,
  // unless the rules above discard it.
  Outcome handle(const GameEvent& event, uint8_t seat);
  // Applies the pending move, if any. Accepted: the new snapshot, ver + 1, its
  // status, and Over once a round to each local seat, in seat order, when the round
  // ends. Rejected: the mover's input gets Rejected.
  Outcome applyPending();
  // Draws the snapshot for `seat` (0: the frame for everyone).
  Outcome draw(uint8_t seat);

  const Roster& roster() const { return seats; }
  // Increases with every snapshot and never resets, rematches included (AD-13).
  uint32_t ver() const { return version; }
  const Status& status() const { return current; }
  // The ver whose status the rules have computed (Ok): equal to ver() unless a call was
  // cancelled or failed between committing a snapshot and finishing its status, when
  // status() is still the previous snapshot's. A restored snapshot is settled by start().
  uint32_t settledVer() const { return settled; }
  bool pending() const { return moveLength != 0; }
  std::span<const uint8_t> snapshot() const { return {state, stateLength}; }
  // Moves discarded so far (for tests and logs).
  uint32_t discardedMoves() const { return discarded; }

 private:
  // A new snapshot is in place: compute its status and deliver Over once a round to
  // each local seat.
  Outcome afterSnapshot();
  // Delivers Over, once a round, to each local seat when the status is over.
  Outcome deliverOver();
  // Copies an accepted state; false (never for a rules adapter that enforces the
  // limit) when it does not fit.
  bool keepState(std::span<const uint8_t> bytes);
  // Delivers a runtime event (Rejected, Over) to `seat`; any move is discarded.
  Outcome deliver(const GameEvent& event, uint8_t seat);

  Roster seats;
  IGameRules& rules;
  uint32_t version = 0;
  uint32_t settled = 0;
  Status current;
  bool overDelivered = false;
  bool restored = false;   // restore() ran and start() has not yet taken it
  bool unshipped = false;  // a snapshot made here has not been handed to the link
  uint32_t discarded = 0;
  size_t stateLength = 0;
  size_t moveLength = 0;
  uint8_t moveSeat = 0;
  uint8_t state[SNAPSHOT_BYTES] = {};
  uint8_t move[MOVE_BYTES] = {};
  char reason[REJECT_REASON_BYTES + 1] = {};
};

}  // namespace GameCore
