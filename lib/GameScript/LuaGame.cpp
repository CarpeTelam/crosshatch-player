#include "LuaGame.h"

#include <IRandom.h>

#include <climits>
#include <cstdio>
#include <lua.hpp>

#include "ArenaAllocator.h"
#include "FrameBuffers.h"
#include "GameInput.h"
#include "GameSources.h"

static_assert(sizeof(lua_Integer) == 8, "games rely on 64-bit Lua integers");
static_assert(LUA_NOREF == -2, "LuaGame.h initialises its references to LUA_NOREF");

namespace GameScript {

namespace {

constexpr lua_Integer SOLO_SEAT = 1;

void openLibrary(lua_State* L, const char* name, lua_CFunction open) {
  luaL_requiref(L, name, open, 1);
  lua_pop(L, 1);
}

}  // namespace

LuaGame::LuaGame(ArenaAllocator& arena, FrameBuffers& frames, const GameSources& sources, GameCore::IRandom& random)
    : arena(arena), frames(frames), sources(sources), random(random) {}

LuaGame::~LuaGame() { close(); }

Outcome LuaGame::fail(const char* message) {
  snprintf(error, sizeof(error), "%s", message);
  return Outcome::ScriptError;
}

Outcome LuaGame::start() {
  close();
  error[0] = '\0';
  L = lua_newstate(&ArenaAllocator::luaAlloc, &arena, random.next32());
  if (!L) return fail("not enough memory");
  setBindingContext(L, &bindings);
  const Outcome loaded = enter(Entry::Load, nullptr);
  if (loaded != Outcome::Ok) return loaded;
  return enter(Entry::Setup, nullptr);
}

Outcome LuaGame::draw() {
  if (!L) return fail("game not started");
  DisplayList& back = frames.back();
  back.clear();
  bindings.drawTarget = &back;
  const Outcome outcome = enter(Entry::Draw, nullptr);
  bindings.drawTarget = nullptr;
  if (outcome == Outcome::Ok) frames.publish();
  return outcome;
}

Outcome LuaGame::input(const InputEvent& event) {
  if (!L) return fail("game not started");
  return enter(Entry::Input, &event);
}

void LuaGame::close() {
  if (!L) return;
  lua_close(L);
  L = nullptr;
  gameRef = stateRef = uiRef = LUA_NOREF;
  bindings = BindingContext{};
}

Outcome LuaGame::enter(const Entry entry, const InputEvent* event) {
  // Only pushes that cannot raise happen out here: light C functions and a light
  // userdata need no allocation, and a fresh stack has LUA_MINSTACK free slots.
  Call call{this, entry, event};
  lua_settop(L, 0);
  lua_pushcfunction(L, &LuaGame::messageHandler);
  lua_pushcfunction(L, &LuaGame::trampoline);
  lua_pushlightuserdata(L, &call);
  const int status = lua_pcall(L, 1, 0, 1);
  Outcome outcome = Outcome::Ok;
  if (status != LUA_OK) {
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
  const auto* call = static_cast<const Call*>(lua_touserdata(L, 1));
  lua_settop(L, 0);
  LuaGame& game = *call->game;
  switch (call->entry) {
    case Entry::Load:
      game.loadEntry(L);
      break;
    case Entry::Setup:
      game.setupEntry(L);
      break;
    case Entry::Draw:
      game.drawEntry(L);
      break;
    case Entry::Input:
      game.inputEntry(L, *call->event);
      break;
  }
  return 0;
}

void LuaGame::loadEntry(lua_State* L) {
  openLibrary(L, LUA_GNAME, luaopen_base);
  openLibrary(L, LUA_TABLIBNAME, luaopen_table);
  openLibrary(L, LUA_STRLIBNAME, luaopen_string);
  openLibrary(L, LUA_MATHLIBNAME, luaopen_math);
  openLibrary(L, LUA_UTF8LIBNAME, luaopen_utf8);
  openChLibrary(L);

  const SourceSpan* main = sources.find("main");
  if (!main) {
    luaL_error(L, "main.lua not found");
    return;
  }
  // Text mode only: a precompiled chunk is refused here.
  if (luaL_loadbufferx(L, sources.textOf(*main), main->length, "@main.lua", "t") != LUA_OK) lua_error(L);
  lua_call(L, 0, 1);
  if (!lua_istable(L, -1)) luaL_error(L, "main.lua must return a table, not a %s", luaL_typename(L, -1));
  gameRef = luaL_ref(L, LUA_REGISTRYINDEX);
  lua_createtable(L, 0, 0);
  uiRef = luaL_ref(L, LUA_REGISTRYINDEX);
}

void LuaGame::pushGameFunction(lua_State* L, const char* name) {
  lua_rawgeti(L, LUA_REGISTRYINDEX, gameRef);
  lua_getfield(L, -1, name);
  if (!lua_isfunction(L, -1)) luaL_error(L, "game.%s is not a function", name);
  lua_remove(L, -2);
}

void LuaGame::setupEntry(lua_State* L) {
  pushGameFunction(L, "setup");
  lua_createtable(L, 0, 2);
  lua_pushinteger(L, 1);
  lua_setfield(L, -2, "seats");
  lua_pushliteral(L, "solo");
  lua_setfield(L, -2, "mode");
  lua_call(L, 1, 1);
  stateRef = luaL_ref(L, LUA_REGISTRYINDEX);
}

void LuaGame::drawEntry(lua_State* L) {
  pushGameFunction(L, "draw");
  lua_rawgeti(L, LUA_REGISTRYINDEX, stateRef);
  lua_pushinteger(L, SOLO_SEAT);
  lua_rawgeti(L, LUA_REGISTRYINDEX, uiRef);
  lua_call(L, 3, 0);
}

void LuaGame::inputEntry(lua_State* L, const InputEvent& event) {
  pushGameFunction(L, "input");
  lua_rawgeti(L, LUA_REGISTRYINDEX, stateRef);
  lua_pushinteger(L, SOLO_SEAT);
  lua_rawgeti(L, LUA_REGISTRYINDEX, uiRef);
  lua_createtable(L, 0, 3);
  lua_pushliteral(L, "tap");
  lua_setfield(L, -2, "kind");
  lua_pushinteger(L, event.x);
  lua_setfield(L, -2, "x");
  lua_pushinteger(L, event.y);
  lua_setfield(L, -2, "y");
  lua_call(L, 4, 1);  // the move; Session consumes it once it exists
}

}  // namespace GameScript
