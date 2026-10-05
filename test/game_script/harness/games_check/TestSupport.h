#pragma once

// What the games check's own tests share: a fixture file's text, and the snapshot bytes of a Lua table (the same Codec
// v1 a game's setup and apply are encoded with).

#include <Codec.h>

#include <cstdint>
#include <fstream>
#include <lua.hpp>
#include <sstream>
#include <string>
#include <vector>

namespace games_check::test {

inline std::string readTextFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

// The snapshot bytes of the Lua table `tableExpression` builds; empty when it does not encode.
inline std::vector<uint8_t> encodeState(const std::string& tableExpression) {
  lua_State* L = luaL_newstate();
  // The base and string libraries only: the vendored Lua has no linit.c (the sandbox opens its own libraries).
  luaL_requiref(L, "_G", luaopen_base, 1);
  luaL_requiref(L, "string", luaopen_string, 1);
  lua_settop(L, 0);
  std::vector<uint8_t> bytes;
  if (luaL_dostring(L, ("return " + tableExpression).c_str()) == LUA_OK) {
    std::vector<uint8_t> scratch(GameScript::Codec::scratchBytes(GameScript::Codec::SNAPSHOT_LIMIT));
    const GameScript::Codec::Encoded encoded =
        GameScript::Codec::encode(L, -1, GameScript::Codec::SNAPSHOT_LIMIT, scratch.data(), scratch.size());
    if (encoded.error == GameScript::Codec::Error::None) bytes.assign(encoded.data, encoded.data + encoded.length);
  }
  lua_close(L);
  return bytes;
}

// A hidden two-seat pass game of the tests' own whose seats move twice each (the turn passes after every second tap),
// and whose tap left of x = 50 is rejected: a move that keeps the turn and a rejected tap, the two cases pass-hidden
// (one move, one turn) never plays. It is not the engine's only kept-turn game: the `pass-keep` fixture and
// Battleship's placement moves (a seat's placement keeps the turn until its fleet is ready) keep the turn too. Every
// call logs its seat, as pass-hidden's do. Shared by the engine tests (the log RoundPlayer produces) and the flow pin
// (the log GameVM's hidden flow produces).
inline constexpr const char* HIDDEN_KEEPS_TURN_MANIFEST =
    R"({"id": "keeps-turn", "name": "Keeps turn", "version": "1.0.0", "api": 1, "seats": {"min": 2, "max": 2}, "modes": ["pass"], "hidden": true})";

inline constexpr const char* HIDDEN_KEEPS_TURN_GAME = R"lua(
local game = {}
function game.setup(ctx) return { seats = ctx.seats, taps = 0 } end
function game.status(state)
  if state.taps >= 4 then return { over = true, winners = {} } end
  return { turn = state.taps // 2 % state.seats + 1 }
end
function game.apply(state, seat, move)
  ch.log("apply seat " .. seat)
  if move.reject then return nil, "not there" end
  state.taps = state.taps + 1
  return state
end
function game.input(state, seat, ui, ev)
  if ev.kind == "tap" then
    ch.log("tap for seat " .. seat)
    if ev.x < 50 then return { reject = true } end
    return { tap = true }
  elseif ev.kind == "rejected" then
    ch.log("rejected for seat " .. seat)
  elseif ev.kind == "over" then
    ch.log("over for seat " .. seat)
  end
  return nil
end
function game.draw(state, seat, ui)
  ch.log("draw for seat " .. seat)
  ch.gfx.clear("white")
  ch.gfx.text(40, 40, "Seat " .. seat .. ", taps " .. state.taps, "medium", "black")
end
return game
)lua";

// The round over it: seat 1 moves and keeps the turn, is rejected (`move = false`), moves again and passes it; seat 2
// moves twice and ends the round.
inline constexpr const char* HIDDEN_KEEPS_TURN_ROUND = R"lua(
return { mode = "pass", winners = {}, steps = {
  { seat = 1, x = 100, y = 200, shows = "taps 1" },
  { seat = 1, x = 10, y = 200, move = false, shows = "taps 1" },
  { seat = 1, x = 100, y = 200, shows = "taps 2" },
  { seat = 2, x = 100, y = 200, shows = "taps 3" },
  { seat = 2, x = 100, y = 200 } } }
)lua";

// What both flows log for it, in order (GameVM::stepHandOff: a move that keeps the turn, or a rejected tap, redraws the
// turn seat alone, with no Result or hand-off; one that passes the turn draws the mover, then the next seat).
inline const std::vector<std::string> HIDDEN_KEEPS_TURN_LOG = {
    "draw for seat 1", "tap for seat 1",      "apply seat 1",    "draw for seat 1", "tap for seat 1",
    "apply seat 1",    "rejected for seat 1", "draw for seat 1", "tap for seat 1",  "apply seat 1",
    "draw for seat 1", "draw for seat 2",     "tap for seat 2",  "apply seat 2",    "draw for seat 2",
    "tap for seat 2",  "apply seat 2",        "over for seat 1", "over for seat 2", "draw for seat 0"};

}  // namespace games_check::test
