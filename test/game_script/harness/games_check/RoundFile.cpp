#include "RoundFile.h"

#include <cstring>
#include <lua.hpp>

namespace games_check {

namespace {

// Reading a round file's table is done outside any protected call (ScriptVm::inspect), so every function below uses
// only Lua calls that cannot raise: lua_type, lua_next over keys it was handed, lua_rawgeti, lua_rawlen, lua_tointegerx
// on a number, lua_tostring on a string (never on a number, which would convert it in place), and no push of a new
// string. A problem is recorded in `error` and the reading stops; nothing here throws or raises.

bool readInteger(lua_State* L, const int index, int64_t& out) {
  if (lua_type(L, index) != LUA_TNUMBER) return false;
  int converted = 0;
  const lua_Integer value = lua_tointegerx(L, index, &converted);
  if (!converted) return false;
  out = value;
  return true;
}

const char* typeOf(lua_State* L, const int index) { return luaL_typename(L, index); }

std::string quoted(const char* text) { return std::string("'") + text + "'"; }

struct StepsRead {
  uint8_t seats = 0;
  // What a problem calls the list: "steps", or "steps(state)'s result" for what a `steps` function returned.
  std::string label = "steps";
  std::vector<Step> steps;
  std::string error;
};

// The step table at `index`; its position `number` (1-based) names it in a problem.
bool readStep(lua_State* L, int index, const lua_Unsigned number, StepsRead& read, Step& step) {
  index = lua_absindex(L, index);
  const std::string at = read.label + "[" + std::to_string(number) + "]";
  if (lua_type(L, index) != LUA_TTABLE) {
    read.error = at + " must be a table {seat, x, y}, not a " + typeOf(L, index);
    return false;
  }
  bool seatSeen = false;
  bool xSeen = false;
  bool ySeen = false;
  lua_pushnil(L);
  while (lua_next(L, index)) {
    if (lua_type(L, -2) != LUA_TSTRING) {
      read.error = at + " has a key that is a " + typeOf(L, -2) + ", not a name";
      lua_pop(L, 2);
      return false;
    }
    const std::string key = lua_tostring(L, -2);
    const std::string where = at + "." + key;
    int64_t number64 = 0;
    bool ok = true;
    if (key == "seat") {
      seatSeen = true;
      ok = readInteger(L, -1, number64);
      if (!ok) {
        read.error = where + " must be an integer";
      } else if (number64 < 1 || number64 > read.seats) {
        read.error =
            where + " must be a seat in 1.." + std::to_string(read.seats) + ", not " + std::to_string(number64);
        ok = false;
      } else {
        step.seat = static_cast<int>(number64);
      }
    } else if (key == "x" || key == "y") {
      (key == "x" ? xSeen : ySeen) = true;
      ok = readInteger(L, -1, number64);
      if (!ok) {
        read.error = where + " must be an integer";
      } else if (number64 < INT16_MIN || number64 > INT16_MAX) {
        read.error = where + " must be a canvas coordinate (-32768..32767), not " + std::to_string(number64);
        ok = false;
      } else {
        (key == "x" ? step.x : step.y) = static_cast<int>(number64);
      }
    } else if (key == "wait") {
      ok = readInteger(L, -1, number64);
      if (!ok) {
        read.error = where + " must be an integer number of milliseconds";
      } else if (number64 < 0 || number64 > UINT32_MAX) {
        read.error = where + " must be 0 or more milliseconds, not " + std::to_string(number64);
        ok = false;
      } else {
        step.waitMs = static_cast<uint32_t>(number64);
      }
    } else if (key == "move") {
      ok = lua_type(L, -1) == LUA_TBOOLEAN;
      if (!ok) {
        read.error = where + " must be a boolean, not a " + typeOf(L, -1);
      } else {
        step.moves = lua_toboolean(L, -1) != 0;
      }
    } else if (key == "shows") {
      ok = lua_type(L, -1) == LUA_TSTRING && lua_rawlen(L, -1) > 0;
      if (!ok) {
        read.error = where + " must be a non-empty string, not a " + typeOf(L, -1);
      } else {
        step.hasShows = true;
        step.shows = lua_tostring(L, -1);
      }
    } else {
      read.error = where + " is not a step key (seat, x, y, wait, move, shows)";
      ok = false;
    }
    if (!ok) {
      lua_pop(L, 2);
      return false;
    }
    lua_pop(L, 1);
  }
  const char* missing = !seatSeen ? "seat" : !xSeen ? "x" : !ySeen ? "y" : nullptr;
  if (missing) {
    read.error = at + "." + missing + " is missing";
    return false;
  }
  return true;
}

// The list at `index`, `steps` or what a `steps` function returned: a non-empty sequence of steps.
bool readSteps(lua_State* L, int index, StepsRead& read) {
  const std::string& what = read.label;
  index = lua_absindex(L, index);
  if (lua_type(L, index) != LUA_TTABLE) {
    read.error = what + " must be a list of steps, not a " + typeOf(L, index);
    return false;
  }
  const lua_Unsigned count = lua_rawlen(L, index);
  lua_Unsigned keys = 0;
  lua_pushnil(L);
  while (lua_next(L, index)) {
    int64_t position = 0;
    if (!readInteger(L, -2, position) || position < 1 || static_cast<lua_Unsigned>(position) > count) {
      read.error = what + " must be a list: it has a key that is not a position 1.." + std::to_string(count);
      lua_pop(L, 2);
      return false;
    }
    ++keys;
    lua_pop(L, 1);
  }
  if (keys != count || count == 0) {
    read.error = count == 0 && keys == 0 ? what + " is empty" : what + " must be a list with no gaps";
    return false;
  }
  for (lua_Unsigned i = 1; i <= count; ++i) {
    lua_rawgeti(L, index, static_cast<lua_Integer>(i));
    Step step;
    const bool ok = readStep(L, -1, i, read, step);
    lua_pop(L, 1);
    if (!ok) return false;
    read.steps.push_back(std::move(step));
  }
  return true;
}

struct RoundRead {
  const GameFacts* facts = nullptr;
  Round* round = nullptr;
  std::string error;
  bool stepsIsFunction = false;
};

bool readSettings(lua_State* L, const int index, RoundRead& read) {
  if (lua_type(L, index) != LUA_TTABLE) {
    read.error = "settings must be a table of setting id and value, not a " + std::string(typeOf(L, index));
    return false;
  }
  const GameCore::ManifestSettings* declared = read.facts->settings;
  const int table = lua_absindex(L, index);
  lua_pushnil(L);
  while (lua_next(L, table)) {
    if (lua_type(L, -2) != LUA_TSTRING) {
      read.error = "settings has a key that is a " + std::string(typeOf(L, -2)) + ", not a setting id";
      lua_pop(L, 2);
      return false;
    }
    const std::string id = lua_tostring(L, -2);
    const int at = declared ? declared->indexOf(id) : -1;
    if (at < 0) {
      read.error = "settings." + id + ": the manifest declares no setting " + quoted(id.c_str());
      lua_pop(L, 2);
      return false;
    }
    if (lua_type(L, -1) != LUA_TSTRING) {
      read.error = "settings." + id + " must be a string, not a " + typeOf(L, -1);
      lua_pop(L, 2);
      return false;
    }
    const std::string value = lua_tostring(L, -1);
    if (declared->settings[at].indexOf(value) < 0) {
      read.error = "settings." + id + ": " + quoted(value.c_str()) + " is not one of its values";
      lua_pop(L, 2);
      return false;
    }
    read.round->settings.emplace_back(id, value);
    lua_pop(L, 1);
  }
  return true;
}

bool readMode(lua_State* L, const int index, RoundRead& read) {
  if (lua_type(L, index) != LUA_TSTRING) {
    read.error = "mode must be \"solo\" or \"pass\", not a " + std::string(typeOf(L, index));
    return false;
  }
  const std::string mode = lua_tostring(L, index);
  GameCore::Manifest::Mode bit = GameCore::Manifest::MODE_SOLO;
  if (mode == "solo") {
    read.round->mode = GameCore::Mode::Solo;
  } else if (mode == "pass") {
    read.round->mode = GameCore::Mode::Pass;
    bit = GameCore::Manifest::MODE_PASS;
  } else {
    read.error = "mode " + quoted(mode.c_str()) + " is not solo or pass";
    return false;
  }
  if (!read.facts->manifest->hasMode(bit)) {
    read.error = "mode " + quoted(mode.c_str()) + " is not one the manifest declares";
    return false;
  }
  if ((read.facts->hostModes & bit) == 0) {
    read.error = "mode " + quoted(mode.c_str()) + " is declared but this host cannot start it";
    return false;
  }
  return true;
}

bool readWinners(lua_State* L, const int index, const uint8_t seats, RoundRead& read) {
  if (lua_type(L, index) != LUA_TTABLE) {
    read.error = "winners must be a list of seats, not a " + std::string(typeOf(L, index));
    return false;
  }
  const int table = lua_absindex(L, index);
  // A list: every key is a position 1..n, as `steps` is.
  const lua_Unsigned count = lua_rawlen(L, table);
  lua_Unsigned keys = 0;
  lua_pushnil(L);
  while (lua_next(L, table)) {
    int64_t position = 0;
    if (!readInteger(L, -2, position) || position < 1 || static_cast<lua_Unsigned>(position) > count) {
      read.error = "winners must be a list of seats: it has a key that is not a position 1.." + std::to_string(count);
      lua_pop(L, 2);
      return false;
    }
    ++keys;
    lua_pop(L, 1);
  }
  if (keys != count) {
    read.error = "winners must be a list of seats with no gaps";
    return false;
  }
  uint16_t winners = 0;
  for (lua_Unsigned i = 1; i <= count; ++i) {
    lua_rawgeti(L, table, static_cast<lua_Integer>(i));
    int64_t seat = 0;
    const bool isInteger = readInteger(L, -1, seat);
    if (!isInteger) {
      read.error = "winners must hold seat numbers (integers), not a " + std::string(typeOf(L, -1));
    } else if (seat < 1 || seat > seats) {
      read.error = "winners holds " + std::to_string(seat) + ", which is no seat in 1.." + std::to_string(seats);
    } else if ((winners >> (seat - 1) & 1u) != 0) {
      read.error = "winners names seat " + std::to_string(seat) + " twice";
    }
    lua_pop(L, 1);
    if (!read.error.empty()) return false;
    winners = static_cast<uint16_t>(winners | (1u << (seat - 1)));
  }
  read.round->winners = winners;
  return true;
}

// The returned table at the top of the stack, read into read.round; false with read.error set.
bool readRound(lua_State* L, RoundRead& read) {
  Round& round = *read.round;
  if (!lua_checkstack(L, 16)) {
    read.error = "the Lua stack is exhausted";
    return false;
  }
  const int table = lua_gettop(L);
  if (lua_type(L, table) != LUA_TTABLE) {
    read.error =
        "the file must return a table {mode, steps, winners or unfinished}, not a " + std::string(typeOf(L, table));
    return false;
  }
  bool modeSeen = false;
  bool stepsSeen = false;
  bool winnersSeen = false;
  bool unfinished = false;
  // Pass 1: every key is known, and what the second pass needs (the mode, whose seats bound the steps and winners).
  lua_pushnil(L);
  while (lua_next(L, table)) {
    if (lua_type(L, -2) != LUA_TSTRING) {
      read.error = "the table has a key that is a " + std::string(typeOf(L, -2)) + ", not a name";
      lua_pop(L, 2);
      return false;
    }
    const std::string key = lua_tostring(L, -2);
    bool ok = true;
    int64_t number = 0;
    if (key == "mode") {
      modeSeen = true;
      ok = readMode(L, -1, read);
    } else if (key == "settings") {
      ok = readSettings(L, -1, read);
    } else if (key == "seed") {
      ok = readInteger(L, -1, number);
      if (!ok) {
        read.error = "seed must be an integer, not a " + std::string(typeOf(L, -1));
      } else if (number < 0 || number > UINT32_MAX) {
        read.error = "seed must be an integer in 0..4294967295, not " + std::to_string(number);
        ok = false;
      } else {
        round.seed = static_cast<uint32_t>(number);
      }
    } else if (key == "steps") {
      stepsSeen = true;
    } else if (key == "winners") {
      winnersSeen = true;
    } else if (key == "unfinished") {
      ok = lua_type(L, -1) == LUA_TBOOLEAN;
      if (!ok) {
        read.error = "unfinished must be true, not a " + std::string(typeOf(L, -1));
      } else {
        unfinished = lua_toboolean(L, -1) != 0;
      }
    } else {
      read.error = quoted(key.c_str()) + " is not a key of a round (mode, settings, seed, steps, winners, unfinished)";
      ok = false;
    }
    if (!ok) {
      lua_pop(L, 2);
      return false;
    }
    lua_pop(L, 1);
  }
  if (!modeSeen) {
    read.error = "mode is missing";
    return false;
  }
  if (!stepsSeen) {
    read.error = "steps is missing";
    return false;
  }
  if (winnersSeen && unfinished) {
    read.error = "winners and unfinished are both set; a round says how it ends with one of them";
    return false;
  }
  if (!winnersSeen && !unfinished) {
    read.error = "neither winners nor unfinished is set; a round says how it ends with one of them";
    return false;
  }
  round.unfinished = unfinished;
  const uint8_t seats = read.facts->seatsOf(round.mode);
  // Pass 2: the keys that need the seats.
  lua_pushnil(L);
  while (lua_next(L, table)) {
    const std::string key = lua_tostring(L, -2);  // pass 1 saw every key is a string
    bool ok = true;
    if (key == "steps") {
      if (lua_type(L, -1) == LUA_TFUNCTION) {
        read.stepsIsFunction = true;
      } else {
        StepsRead steps;
        steps.seats = seats;
        ok = readSteps(L, -1, steps);
        if (ok) {
          round.steps = std::move(steps.steps);
        } else {
          read.error = steps.error;
        }
      }
    } else if (key == "winners") {
      ok = readWinners(L, -1, seats, read);
    }
    if (!ok) {
      lua_pop(L, 2);
      return false;
    }
    lua_pop(L, 1);
  }
  return true;
}

struct ReadCall {
  RoundRead* read;
  bool ok;
};

struct KeepSteps {
  int source;
  int* ref;
};

struct StepsCall {
  StepsRead* read;
  bool ok;
};

}  // namespace

uint8_t GameFacts::seatsOf(const GameCore::Mode mode) const {
  if (mode == GameCore::Mode::Solo) return 1;
  if (!manifest) return 0;
  return GameCore::passSeats(manifest->seatsMin, manifest->seatsMax, hostMaxSeats);
}

Round::~Round() {
  if (vm) vm->vm().release(stepsRef);
}

std::string winnersText(const uint16_t winners) {
  std::string text = "{";
  for (int seat = 1; seat <= GameCore::Roster::MAX_SEATS; ++seat) {
    if ((winners >> (seat - 1) & 1u) == 0) continue;
    if (text.size() > 1) text += ',';
    text += std::to_string(seat);
  }
  return text + "}";
}

bool loadRound(std::unique_ptr<OwnedVm> vm, const std::string& name, const std::string& text, const GameFacts& facts,
               Round& out, std::string& error) {
  out.name = name;
  const std::string file = out.file();
  ScriptVm& script = vm->vm();
  int ref = ScriptVm::NO_REF;
  const std::string chunkName = "@" + file;
  const VmResult ran = script.runChunk(text, chunkName.c_str(), ref);
  if (!ran.ok()) {
    error = file + ": " + ran.message;
    return false;
  }
  RoundRead read;
  read.facts = &facts;
  read.round = &out;
  ReadCall call{&read, false};
  script.inspect(
      ref,
      [](lua_State* L, void* context) {
        auto& call = *static_cast<ReadCall*>(context);
        call.ok = readRound(L, *call.read);
      },
      &call);
  if (!call.ok) {
    script.release(ref);
    error = file + ": " + read.error;
    return false;
  }
  if (read.stepsIsFunction) {
    // Keep the `steps` function itself in the registry, so the round can call it once its session has begun.
    KeepSteps keep{ref, &out.stepsRef};
    const VmResult kept = script.protect(
        [](lua_State* L, void* context) {
          auto& keep = *static_cast<KeepSteps*>(context);
          lua_rawgeti(L, LUA_REGISTRYINDEX, keep.source);
          lua_getfield(L, -1, "steps");
          *keep.ref = luaL_ref(L, LUA_REGISTRYINDEX);
        },
        &keep);
    if (!kept.ok()) {
      script.release(ref);
      error = file + ": " + kept.message;
      return false;
    }
  }
  script.release(ref);
  out.stepsFromFunction = read.stepsIsFunction;
  out.vm = std::move(vm);
  return true;
}

bool resolveSteps(Round& round, const std::span<const uint8_t> initialState, const GameFacts& facts,
                  std::string& error) {
  if (!round.stepsFromFunction) return true;
  ScriptVm& script = round.vm->vm();
  int result = ScriptVm::NO_REF;
  const VmResult called = script.callWithState(round.stepsRef, initialState, result);
  if (!called.ok()) {
    error = round.file() + ": steps(state): " + called.message;
    return false;
  }
  StepsRead read;
  read.seats = facts.seatsOf(round.mode);
  read.label = "steps(state)'s result";
  StepsCall call{&read, false};
  script.inspect(
      result,
      [](lua_State* L, void* context) {
        auto& call = *static_cast<StepsCall*>(context);
        call.ok = readSteps(L, -1, *call.read);
      },
      &call);
  script.release(result);
  if (!call.ok) {
    error = round.file() + ": " + read.error;
    return false;
  }
  round.steps = std::move(read.steps);
  return true;
}

}  // namespace games_check
