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
// A rejection's reason, as REJECT carries it (AD-13) and input() receives it in every mode.
inline constexpr size_t REJECT_REASON_BYTES = 64;

// One match's session on the authority (AD-9, AD-11), for one local seat: the
// roster, the encoded snapshot that is the source of truth, its version, the
// status computed after each snapshot, and the one pending move. It drives the
// rules and holds no game logic. About 1.8 KB, so the owner allocates it from the
// VM arena (AD-5); confined to the VM task.
//
// Moves are applied strictly one at a time: a move returned while one is pending,
// after the round is over, off-turn, or in answer to a Rejected or Over event is
// discarded (the last also keeps a game that answers every rejection with a move
// from looping inside one step).
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
  // Starts a round: setup(ctx), a new snapshot, its status, and Over if the round
  // is already over; or, after restore(), the restored state's status alone. Called
  // again for a rematch, it runs setup and keeps counting ver.
  Outcome start();
  // Delivers an event to the local seat's input and keeps the move it returns,
  // unless the rules above discard it.
  Outcome handle(const GameEvent& event);
  // Applies the pending move, if any. Accepted: the new snapshot, ver + 1, its
  // status, and Over once when the round ends. Rejected: input gets Rejected.
  Outcome applyPending();
  // Draws the snapshot for the local seat.
  Outcome draw();

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
  // A new snapshot is in place: compute its status and deliver Over once.
  Outcome afterSnapshot();
  // Copies an accepted state; false (never for a rules adapter that enforces the
  // limit) when it does not fit.
  bool keepState(std::span<const uint8_t> bytes);
  // Delivers a runtime event (Rejected, Over) to the local seat; any move is discarded.
  Outcome deliver(const GameEvent& event);

  Roster seats;
  IGameRules& rules;
  uint8_t localSeat;
  uint32_t version = 0;
  uint32_t settled = 0;
  Status current;
  bool overDelivered = false;
  bool restored = false;  // restore() ran and start() has not yet taken it
  uint32_t discarded = 0;
  size_t stateLength = 0;
  size_t moveLength = 0;
  uint8_t moveSeat = 0;
  uint8_t state[SNAPSHOT_BYTES] = {};
  uint8_t move[MOVE_BYTES] = {};
  char reason[REJECT_REASON_BYTES + 1] = {};
};

}  // namespace GameCore
