#include "Sandbox.h"

#include <IRandom.h>

#include <climits>
#include <cstdint>
#include <initializer_list>
#include <lua.hpp>

#include "CallGuard.h"
#include "ChBindings.h"
#include "GameSources.h"

static_assert(sizeof(lua_Integer) == 8, "games rely on 64-bit Lua integers");

namespace GameScript {

namespace {

// Registry table of loaded modules, by name.
constexpr const char* MODULES_KEY = "GameScript.modules";
// Marks a module whose chunk is running, so a require cycle is an error instead
// of an endless recursion. Only its address is used.
constexpr char LOADING = 0;

void* loadingTag() { return const_cast<char*>(&LOADING); }

void openLibrary(lua_State* L, const char* name, lua_CFunction open) {
  luaL_requiref(L, name, open, 1);
  lua_pop(L, 1);
}

// Leaves the error on top, forgets the half-loaded module, and raises again.
int forgetAndRaise(lua_State* L, const int modules, const char* name) {
  lua_pushnil(L);
  lua_setfield(L, modules, name);
  return lua_error(L);
}

// require(name)
int require(lua_State* L) {
  const char* name = luaL_checkstring(L, 1);
  lua_settop(L, 1);
  luaL_getsubtable(L, LUA_REGISTRYINDEX, MODULES_KEY);
  constexpr int modules = 2;
  lua_getfield(L, modules, name);
  if (lua_touserdata(L, -1) == loadingTag()) return luaL_error(L, "circular require of '%s'", name);
  if (!lua_isnil(L, -1)) return 1;
  lua_pop(L, 1);

  const GameSources* sources = bindingContext(L)->sources;
  const SourceSpan* span = sources ? sources->find(name) : nullptr;
  if (!span) return luaL_error(L, "module '%s' not found", name);

  lua_pushlightuserdata(L, loadingTag());
  lua_setfield(L, modules, name);
  const char* chunkName = lua_pushfstring(L, "@%s.lua", name);
  // Text mode: a precompiled module is refused like a precompiled main.lua.
  if (luaL_loadbufferx(L, sources->textOf(*span), span->length, chunkName, "t") != LUA_OK) {
    return forgetAndRaise(L, modules, name);
  }
  lua_pushvalue(L, 1);
  if (lua_pcall(L, 1, 1, 0) != LUA_OK) return forgetAndRaise(L, modules, name);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    lua_pushboolean(L, 1);
  }
  lua_pushvalue(L, -1);
  lua_setfield(L, modules, name);
  return 1;
}

// The message handler xpcall really gets; upvalue 1 is the script's handler. Lua
// calls a message handler at the error point, and for an error the CallGuard
// raised from its hook, hooks are still off there, so the script's handler would
// run with no budget, cancel, or stack check. For such a fault the error passes
// through untouched; for any other error the script's handler runs as usual.
int guardedHandler(lua_State* L) {
  if (bindingContext(L)->guard->fault() != Fault::None) return 1;
  lua_pushvalue(L, lua_upvalueindex(1));
  lua_insert(L, 1);
  lua_call(L, lua_gettop(L) - 1, 1);
  return 1;
}

// xpcall(f, handler, ...): the base xpcall (upvalue 1) with the handler wrapped.
int guardedXpcall(lua_State* L) {
  luaL_checktype(L, 2, LUA_TFUNCTION);
  lua_pushvalue(L, 2);
  lua_pushcclosure(L, &guardedHandler, 1);
  lua_replace(L, 2);
  lua_pushvalue(L, lua_upvalueindex(1));
  lua_insert(L, 1);
  lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
  return lua_gettop(L);
}

lua_Integer seedWord(GameCore::IRandom& random) {
  const uint64_t high = random.next32();
  const uint64_t low = random.next32();
  return static_cast<lua_Integer>((high << 32) | low);
}

}  // namespace

void openSandbox(lua_State* L, GameCore::IRandom& random) {
  openLibrary(L, LUA_GNAME, luaopen_base);
  openLibrary(L, LUA_TABLIBNAME, luaopen_table);
  openLibrary(L, LUA_STRLIBNAME, luaopen_string);
  openLibrary(L, LUA_MATHLIBNAME, luaopen_math);
  openLibrary(L, LUA_UTF8LIBNAME, luaopen_utf8);

  // Scripts load code only through require, from their own sources in text mode.
  for (const char* name : {"load", "loadfile", "dofile"}) {
    lua_pushnil(L);
    lua_setglobal(L, name);
  }
  lua_pushcfunction(L, &require);
  lua_setglobal(L, "require");
  lua_getglobal(L, "xpcall");
  lua_pushcclosure(L, &guardedXpcall, 1);
  lua_setglobal(L, "xpcall");

  lua_getglobal(L, LUA_MATHLIBNAME);
  lua_getfield(L, -1, "randomseed");
  const lua_Integer first = seedWord(random);
  const lua_Integer second = seedWord(random);
  lua_pushinteger(L, first);
  lua_pushinteger(L, second);
  lua_call(L, 2, 0);
  lua_pop(L, 1);
}

}  // namespace GameScript
