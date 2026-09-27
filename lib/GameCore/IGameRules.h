#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "GameEvent.h"
#include "Roster.h"

namespace GameCore {

// How a call into the game ended. A ScriptError ends the session (AD-14); Cancelled
// means the runtime stopped the call, and shows nothing.
enum class Outcome : uint8_t { Ok, ScriptError, Cancelled };

// The ctx table setup receives (AD-8).
struct GameContext {
  uint8_t seats = 1;
  Mode mode = Mode::Solo;
  int32_t api = API_LEVEL;
};

// A validated status (AD-8, AD-11): a turn seat while the round is on, or over with
// the winners as a mask (bit seat - 1; empty is a draw).
struct Status {
  bool over = false;
  uint8_t turn = 0;  // 0 when over
  uint16_t winners = 0;
};

// Port: the game's rules, implemented by the script adapter (GameScript::LuaGame).
// States and moves cross it as codec bytes, so the Session never sees a script
// value. A span a call returns points into the adapter's scratch and stays valid
// until the next call. Every call returns Ok, or ScriptError or Cancelled, after
// which the session ends.
class IGameRules {
 public:
  virtual ~IGameRules() = default;

  // The initial state's bytes.
  virtual Outcome setup(const GameContext& ctx, std::span<const uint8_t>& state) = 0;
  // The state's status, validated against the roster: a turn or winner that is not
  // one of its seats is a ScriptError.
  virtual Outcome status(std::span<const uint8_t> state, const Roster& roster, Status& out) = 0;
  // Applies a move from `seat`. Accepted: `next` holds the new state's bytes.
  // Rejected: `next` is empty and `reason` holds the NUL-terminated reason, cut to fit.
  virtual Outcome apply(std::span<const uint8_t> state, uint8_t seat, std::span<const uint8_t> move,
                        std::span<const uint8_t>& next, std::span<char> reason) = 0;
  // Draws the state for `seat` (into the adapter's frame).
  virtual Outcome draw(std::span<const uint8_t> state, uint8_t seat) = 0;
  // Delivers an event to `seat`'s input; `move` holds the returned move's bytes, or
  // is empty for none.
  virtual Outcome input(std::span<const uint8_t> state, uint8_t seat, const GameEvent& event,
                        std::span<const uint8_t>& move) = 0;
};

}  // namespace GameCore
