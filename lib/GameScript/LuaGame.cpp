#include "LuaGame.h"

#include <IRandom.h>
#include <Session.h>

#include <climits>
#include <cstdio>
#include <cstring>
#include <lua.hpp>

#include "ArenaAllocator.h"
#include "FrameBuffers.h"
#include "GameSources.h"
#include "Sandbox.h"

static_assert(sizeof(lua_Integer) == 8, "games rely on 64-bit Lua integers");
static_assert(LUA_NOREF == -2, "LuaGame.h initialises its references to LUA_NOREF");

namespace GameScript {

static_assert(Codec::SNAPSHOT_LIMIT == GameCore::SNAPSHOT_BYTES, "the codec and the Session share the state limit");
static_assert(Codec::MOVE_LIMIT == GameCore::MOVE_BYTES, "the codec and the Session share the move limit");
// The reserve holds the Session and the scratch; luaAlloc never touches it.
static_assert(ArenaAllocator::blockBytes(LuaGame::SCRATCH_BYTES) +
                      ArenaAllocator::blockBytes(sizeof(GameCore::Session)) <=
                  SCRATCH_RESERVE_BYTES,
              "SCRATCH_RESERVE_BYTES must hold the codec scratch and the Session");

namespace {

using GameCore::EventKind;

// Pushes the value `bytes` encode; `what` names it in the error.
void pushDecoded(lua_State* L, const std::span<const uint8_t> bytes, const size_t limit, const char* what) {
  const Codec::Error error = Codec::decode(L, bytes.data(), bytes.size(), limit);
  if (error != Codec::Error::None) luaL_error(L, "%s could not be decoded (%s)", what, Codec::errorName(error));
}

void pushEvent(lua_State* L, const GameCore::GameEvent& event) {
  switch (event.kind) {
    case EventKind::Tap:
      lua_createtable(L, 0, 3);
      lua_pushliteral(L, "tap");
      lua_setfield(L, -2, "kind");
      lua_pushinteger(L, event.x);
      lua_setfield(L, -2, "x");
      lua_pushinteger(L, event.y);
      lua_setfield(L, -2, "y");
      return;
    case EventKind::Rejected:
      lua_createtable(L, 0, 2);
      lua_pushliteral(L, "rejected");
      lua_setfield(L, -2, "kind");
      lua_pushstring(L, event.reason ? event.reason : "");
      lua_setfield(L, -2, "reason");
      return;
    case EventKind::Over:
      lua_createtable(L, 0, 1);
      lua_pushliteral(L, "over");
      lua_setfield(L, -2, "kind");
      return;
  }
}

// The seat on top of the stack (popped); raises unless it is one of the roster's.
uint8_t popSeat(lua_State* L, const GameCore::Roster& roster, const char* field) {
  int isInteger = 0;
  const lua_Integer seat = lua_type(L, -1) == LUA_TNUMBER ? lua_tointegerx(L, -1, &isInteger) : 0;
  if (!isInteger) {
    luaL_error(L, "%s must be a seat in 1..%d, not a %s", field, static_cast<int>(roster.seats), luaL_typename(L, -1));
  }
  if (!roster.isSeat(seat)) luaL_error(L, "%s is %I, not a seat in 1..%d", field, seat, static_cast<int>(roster.seats));
  lua_pop(L, 1);
  return static_cast<uint8_t>(seat);
}

// Reads the status table on top of the stack (AD-8), checking every seat against
// the roster (AD-11).
GameCore::Status readStatus(lua_State* L, const GameCore::Roster& roster) {
  if (!lua_istable(L, -1)) luaL_error(L, "status must return a table, not a %s", luaL_typename(L, -1));
  const int table = lua_gettop(L);
  GameCore::Status status;
  lua_pushliteral(L, "over");
  lua_rawget(L, table);
  status.over = lua_toboolean(L, -1) != 0;
  lua_pop(L, 1);
  if (!status.over) {
    lua_pushliteral(L, "turn");
    lua_rawget(L, table);
    status.turn = popSeat(L, roster, "status.turn");
    return status;
  }
  lua_pushliteral(L, "winners");
  lua_rawget(L, table);
  if (!lua_istable(L, -1)) luaL_error(L, "status.winners must be a list of seats, not a %s", luaL_typename(L, -1));
  const lua_Unsigned count = lua_rawlen(L, -1);
  if (count > roster.seats) {
    luaL_error(L, "status.winners lists %I seats; this match has %d", static_cast<lua_Integer>(count),
               static_cast<int>(roster.seats));
  }
  for (lua_Unsigned i = 1; i <= count; ++i) {
    char field[32];
    snprintf(field, sizeof(field), "status.winners[%u]", static_cast<unsigned>(i));
    lua_rawgeti(L, -1, static_cast<lua_Integer>(i));
    status.winners |= static_cast<uint16_t>(1u << (popSeat(L, roster, field) - 1));
  }
  lua_pop(L, 1);
  return status;
}

// Copies the string (or number) on top into `out`, NUL-terminated, cut at a UTF-8
// boundary to fit.
void copyReason(lua_State* L, const std::span<char> out) {
  if (out.empty()) return;
  size_t length = 0;
  const char* text = lua_tolstring(L, -1, &length);
  size_t kept = length < out.size() - 1 ? length : out.size() - 1;
  if (kept < length) {
    while (kept > 0 && (static_cast<uint8_t>(text[kept]) & 0xC0) == 0x80) --kept;
  }
  std::memcpy(out.data(), text, kept);
  out[kept] = '\0';
}

}  // namespace

LuaGame::LuaGame(ArenaAllocator& arena, FrameBuffers& frames, const GameSources& sources, GameCore::IRandom& random,
                 const Canvas& canvas)
    : arena(arena), frames(frames), sources(sources), random(random), canvas(canvas) {}

LuaGame::~LuaGame() { close(); }

Outcome LuaGame::fail(const char* message) {
  snprintf(error, sizeof(error), "%s", message);
  return Outcome::ScriptError;
}

Outcome LuaGame::cancelled() {
  snprintf(error, sizeof(error), "%s", "cancelled");
  return Outcome::Cancelled;
}

Outcome LuaGame::load() {
  close();
  error[0] = '\0';
  // From the reserve, which the Lua heap can never take.
  scratch = arena.allocate(SCRATCH_BYTES);
  if (!scratch) return fail("not enough memory");
  L = lua_newstate(&ArenaAllocator::luaAlloc, &arena, random.next32());
  if (!L) return fail("not enough memory");
  bindings.canvas = &canvas;
  bindings.sources = &sources;
  bindings.guard = &guard;
  bindings.lockedSections = &lockedSections;
  setBindingContext(L, &bindings);
  guard.install(L);
  Call call;
  call.entry = Entry::Load;
  return enter(call);
}

Outcome LuaGame::setup(const GameCore::GameContext& ctx, std::span<const uint8_t>& state) {
  state = {};
  if (!L) return fail("game not started");
  Call call;
  call.entry = Entry::Setup;
  call.ctx = &ctx;
  call.encoded = &state;
  const Outcome outcome = enter(call);
  if (outcome != Outcome::Ok) state = {};
  return outcome;
}

Outcome LuaGame::status(const std::span<const uint8_t> state, const GameCore::Roster& roster, GameCore::Status& out) {
  if (!L) return fail("game not started");
  Call call;
  call.entry = Entry::Status;
  call.state = state;
  call.roster = &roster;
  call.status = &out;
  return enter(call);
}

Outcome LuaGame::apply(const std::span<const uint8_t> state, const uint8_t seat, const std::span<const uint8_t> move,
                       std::span<const uint8_t>& next, const std::span<char> reason) {
  next = {};
  if (!reason.empty()) reason[0] = '\0';
  if (!L) return fail("game not started");
  Call call;
  call.entry = Entry::Apply;
  call.state = state;
  call.seat = seat;
  call.move = move;
  call.encoded = &next;
  call.reason = reason;
  const Outcome outcome = enter(call);
  if (outcome != Outcome::Ok) next = {};
  return outcome;
}

Outcome LuaGame::draw(const std::span<const uint8_t> state, const uint8_t seat) {
  if (!L) return fail("game not started");
  DisplayList& back = frames.back();
  back.clear();
  bindings.drawTarget = &back;
  Call call;
  call.entry = Entry::Draw;
  call.state = state;
  call.seat = seat;
  const Outcome outcome = enter(call);
  bindings.drawTarget = nullptr;
  if (outcome == Outcome::Ok) frames.publish();
  return outcome;
}

Outcome LuaGame::input(const std::span<const uint8_t> state, const uint8_t seat, const GameCore::GameEvent& event,
                       std::span<const uint8_t>& move) {
  move = {};
  if (!L) return fail("game not started");
  Call call;
  call.entry = Entry::Input;
  call.state = state;
  call.seat = seat;
  call.event = &event;
  call.encoded = &move;
  const Outcome outcome = enter(call);
  if (outcome != Outcome::Ok) move = {};
  return outcome;
}

void LuaGame::close() {
  if (L) {
    calls.fetch_add(1, std::memory_order_acq_rel);
    running.store(true, std::memory_order_release);
    lua_close(L);
    running.store(false, std::memory_order_release);
  }
  arena.release(scratch);
  abandon();
}

void LuaGame::abandon() {
  L = nullptr;
  scratch = nullptr;
  gameRef = uisRef = LUA_NOREF;
  bindings = BindingContext{};
}

Outcome LuaGame::enter(Call& call) {
  if (guard.cancelRequested()) return cancelled();
  // Only pushes that cannot raise happen out here: light C functions and a light
  // userdata need no allocation, and a fresh stack has LUA_MINSTACK free slots.
  call.game = this;
  guard.arm(L);
  lua_settop(L, 0);
  lua_pushcfunction(L, &LuaGame::messageHandler);
  lua_pushcfunction(L, &LuaGame::trampoline);
  lua_pushlightuserdata(L, &call);
  calls.fetch_add(1, std::memory_order_acq_rel);
  running.store(true, std::memory_order_release);
  const int status = lua_pcall(L, 1, 0, 1);
  running.store(false, std::memory_order_release);
  // A guard fault wins over the status: the script may have caught it and returned.
  const Fault fault = guard.fault();
  Outcome outcome = Outcome::Ok;
  if (fault == Fault::Cancelled) {
    outcome = cancelled();
  } else if (fault != Fault::None) {
    outcome = fail(guard.message());
  } else if (status != LUA_OK) {
    // The handler leaves a string; a memory error skips the handler but carries
    // Lua's preallocated message. Reading a string never allocates.
    const char* message = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "unknown error";
    outcome = fail(message);
  }
  lua_settop(L, 0);
  return outcome;
}

int LuaGame::messageHandler(lua_State* L) {
  if (lua_type(L, 1) == LUA_TSTRING) return 1;
  if (luaL_callmeta(L, 1, "__tostring") && lua_type(L, -1) == LUA_TSTRING) return 1;
  if (lua_type(L, 1) == LUA_TNUMBER) {
    lua_pushstring(L, lua_tostring(L, 1));
    return 1;
  }
  lua_pushfstring(L, "(error object is a %s value)", luaL_typename(L, 1));
  return 1;
}

int LuaGame::trampoline(lua_State* L) {
  auto& call = *static_cast<Call*>(lua_touserdata(L, 1));
  lua_settop(L, 0);
  LuaGame& game = *call.game;
  switch (call.entry) {
    case Entry::Load:
      game.loadEntry(L);
      break;
    case Entry::Setup:
      game.setupEntry(L, call);
      break;
    case Entry::Status:
      game.statusEntry(L, call);
      break;
    case Entry::Apply:
      game.applyEntry(L, call);
      break;
    case Entry::Draw:
      game.drawEntry(L, call);
      break;
    case Entry::Input:
      game.inputEntry(L, call);
      break;
  }
  return 0;
}

void LuaGame::loadEntry(lua_State* L) {
  openSandbox(L, random);
  openChLibrary(L);

  const SourceSpan* main = sources.find("main");
  if (!main) {
    luaL_error(L, "main.lua not found");
    return;
  }
  // main.lua loads in text mode, so a precompiled main.lua is refused; the
  // sandbox has no load(), so a script cannot reach binary chunks either.
  if (luaL_loadbufferx(L, sources.textOf(*main), main->length, "@main.lua", "t") != LUA_OK) lua_error(L);
  lua_call(L, 0, 1);
  if (!lua_istable(L, -1)) luaL_error(L, "main.lua must return a table, not a %s", luaL_typename(L, -1));
  gameRef = luaL_ref(L, LUA_REGISTRYINDEX);
  lua_createtable(L, 0, 0);
  uisRef = luaL_ref(L, LUA_REGISTRYINDEX);
}

void LuaGame::pushGameFunction(lua_State* L, const char* name) {
  lua_rawgeti(L, LUA_REGISTRYINDEX, gameRef);
  lua_getfield(L, -1, name);
  if (!lua_isfunction(L, -1)) luaL_error(L, "game.%s is not a function", name);
  lua_remove(L, -2);
}

void LuaGame::pushUi(lua_State* L, const uint8_t seat) {
  lua_rawgeti(L, LUA_REGISTRYINDEX, uisRef);
  if (lua_rawgeti(L, -1, seat) == LUA_TNIL) {
    lua_pop(L, 1);
    lua_createtable(L, 0, 0);
    lua_pushvalue(L, -1);
    lua_rawseti(L, -3, seat);
  }
  lua_remove(L, -2);
}

void LuaGame::encodeTop(lua_State* L, const char* function, const char* what, const size_t limit,
                        std::span<const uint8_t>& out) {
  const Codec::Encoded encoded = Codec::encode(L, -1, limit, scratch, SCRATCH_BYTES);
  if (encoded.error == Codec::Error::TooLarge) {
    luaL_error(L, "%s: %s is too large (over %d bytes)", function, what, static_cast<int>(limit));
  }
  if (encoded.error != Codec::Error::None) {
    luaL_error(L, "%s: %s cannot be encoded (%s)", function, what, Codec::errorName(encoded.error));
  }
  out = {encoded.data, encoded.length};
}

void LuaGame::setupEntry(lua_State* L, Call& call) {
  pushGameFunction(L, "setup");
  lua_createtable(L, 0, 3);
  lua_pushinteger(L, call.ctx->seats);
  lua_setfield(L, -2, "seats");
  lua_pushstring(L, GameCore::modeName(call.ctx->mode));
  lua_setfield(L, -2, "mode");
  lua_pushinteger(L, call.ctx->api);
  lua_setfield(L, -2, "api");
  lua_call(L, 1, 1);
  if (!lua_istable(L, -1)) luaL_error(L, "setup must return a state table, not a %s", luaL_typename(L, -1));
  encodeTop(L, "setup", "state", Codec::SNAPSHOT_LIMIT, *call.encoded);
}

void LuaGame::statusEntry(lua_State* L, Call& call) {
  pushGameFunction(L, "status");
  pushDecoded(L, call.state, Codec::SNAPSHOT_LIMIT, "state");
  lua_call(L, 1, 1);
  *call.status = readStatus(L, *call.roster);
}

void LuaGame::applyEntry(lua_State* L, Call& call) {
  pushGameFunction(L, "apply");
  pushDecoded(L, call.state, Codec::SNAPSHOT_LIMIT, "state");
  lua_pushinteger(L, call.seat);
  pushDecoded(L, call.move, Codec::MOVE_LIMIT, "move");
  lua_call(L, 3, 2);
  if (lua_isnil(L, -2)) {
    // Rejected: the reason is a string, a number (as text), or nil (empty).
    if (lua_isnil(L, -1)) {
      lua_pop(L, 1);
      lua_pushliteral(L, "");
    }
    if (!lua_isstring(L, -1))
      luaL_error(L, "apply: a rejection's reason must be a string, not a %s", luaL_typename(L, -1));
    copyReason(L, call.reason);
    return;
  }
  lua_pop(L, 1);
  if (!lua_istable(L, -1)) {
    luaL_error(L, "apply must return a state table, or nil and a reason, not a %s", luaL_typename(L, -1));
  }
  encodeTop(L, "apply", "state", Codec::SNAPSHOT_LIMIT, *call.encoded);
}

void LuaGame::drawEntry(lua_State* L, const Call& call) {
  pushGameFunction(L, "draw");
  pushDecoded(L, call.state, Codec::SNAPSHOT_LIMIT, "state");
  lua_pushinteger(L, call.seat);
  pushUi(L, call.seat);
  lua_call(L, 3, 0);
}

void LuaGame::inputEntry(lua_State* L, Call& call) {
  pushGameFunction(L, "input");
  pushDecoded(L, call.state, Codec::SNAPSHOT_LIMIT, "state");
  lua_pushinteger(L, call.seat);
  pushUi(L, call.seat);
  pushEvent(L, *call.event);
  lua_call(L, 4, 1);
  if (lua_isnil(L, -1)) return;
  if (!lua_istable(L, -1)) luaL_error(L, "input must return a move table or nil, not a %s", luaL_typename(L, -1));
  encodeTop(L, "input", "move", Codec::MOVE_LIMIT, *call.encoded);
}

}  // namespace GameScript
