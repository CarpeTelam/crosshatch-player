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

  const BindingContext* context = bindingContext(L);
  const GameSources* sources = context->sources;
  const SourceSpan* span = sources ? sources->find(name) : nullptr;
  if (!span) return luaL_error(L, "module '%s' not found", name);
  // The parser runs no hook, so it needs its whole budget free before it starts.
  if (context->guard && !context->guard->hasHeadroom(CallGuard::PARSE_HEADROOM_BYTES)) {
    return luaL_error(L, "require '%s': script recursion too deep to load a module", name);
  }

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

// Calls the wrapped library function (upvalue 1) with the same arguments.
int callWrapped(lua_State* L) {
  lua_pushvalue(L, lua_upvalueindex(1));
  lua_insert(L, 1);
  lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
  return lua_gettop(L);
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
  return callWrapped(L);
}

// setmetatable(t, mt): lbaselib's setmetatable, line for line, plus a refusal of
// a metatable with a __gc field. Finalizers run with hooks off, so a __gc would
// escape the budget and the stack check; Lua marks an object for finalization
// only when its metatable has a raw __gc field at this call, so checking here is
// enough. Written out rather than wrapped so argument errors still name it.
int guardedSetmetatable(lua_State* L) {
  const int type = lua_type(L, 2);
  luaL_checktype(L, 1, LUA_TTABLE);
  luaL_argexpected(L, type == LUA_TNIL || type == LUA_TTABLE, 2, "nil or table");
  if (type == LUA_TTABLE) {
    lua_pushliteral(L, "__gc");
    const bool hasGc = lua_rawget(L, 2) != LUA_TNIL;
    lua_pop(L, 1);
    if (hasGc) return luaL_error(L, "setmetatable: __gc metamethods are not supported");
  }
  if (luaL_getmetafield(L, 1, "__metatable") != LUA_TNIL) return luaL_error(L, "cannot change a protected metatable");
  lua_settop(L, 2);
  lua_setmetatable(L, 1);
  return 1;
}

int tooManyElements(lua_State* L, const char* function) {
  return luaL_error(L, "table.%s: more than %d elements", function, static_cast<int>(TABLE_ELEMENTS_LIMIT));
}

// table.move(a1, f, e, t [,a2]): its copy loop runs in C, where no hook runs.
int guardedMove(lua_State* L) {
  const lua_Integer first = luaL_checkinteger(L, 2);
  const lua_Integer last = luaL_checkinteger(L, 3);
  if (last >= first && static_cast<lua_Unsigned>(last) - static_cast<lua_Unsigned>(first) >= TABLE_ELEMENTS_LIMIT) {
    return tooManyElements(L, "move");
  }
  return callWrapped(L);
}

// table.insert and table.remove shift up to #t elements in C; #t comes from __len.
int checkLength(lua_State* L, const char* function) {
  if (lua_type(L, 1) == LUA_TTABLE && luaL_len(L, 1) > static_cast<lua_Integer>(TABLE_ELEMENTS_LIMIT)) {
    return tooManyElements(L, function);
  }
  return callWrapped(L);
}

int guardedInsert(lua_State* L) { return checkLength(L, "insert"); }
int guardedRemove(lua_State* L) { return checkLength(L, "remove"); }

// Replaces field `name` of the table on top of the stack with fn closed over the original.
void wrapField(lua_State* L, const char* name, lua_CFunction fn) {
  lua_getfield(L, -1, name);
  lua_pushcclosure(L, fn, 1);
  lua_setfield(L, -2, name);
}

lua_Integer seedWord(GameCore::IRandom& random) {
  const uint64_t high = random.next32();
  const uint64_t low = random.next32();
  return static_cast<lua_Integer>((high << 32) | low);
}

// math.randomseed(...): Lua's (upvalue 1), except that with no argument the seed
// comes from IRandom (upvalue 2): Lua's own no-argument seed calls time(), which
// takes a newlib lock an abandon could leave held. With arguments it is unchanged.
int guardedRandomseed(lua_State* L) {
  if (lua_isnone(L, 1)) {
    auto& random = *static_cast<GameCore::IRandom*>(lua_touserdata(L, lua_upvalueindex(2)));
    const lua_Integer first = seedWord(random);
    const lua_Integer second = seedWord(random);
    lua_pushinteger(L, first);
    lua_pushinteger(L, second);
  }
  return callWrapped(L);
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
  lua_pushcfunction(L, &guardedSetmetatable);
  lua_setglobal(L, "setmetatable");
  lua_pushcfunction(L, &chLog);
  lua_setglobal(L, "print");
  lua_getglobal(L, LUA_TABLIBNAME);
  wrapField(L, "move", &guardedMove);
  wrapField(L, "insert", &guardedInsert);
  wrapField(L, "remove", &guardedRemove);
  lua_pop(L, 1);

  // luaopen_math above seeded itself with luaL_makeseed, which calls time() once;
  // that is here in load(), before any game code runs, and is reseeded now.
  lua_getglobal(L, LUA_MATHLIBNAME);
  lua_getfield(L, -1, "randomseed");
  const lua_Integer first = seedWord(random);
  const lua_Integer second = seedWord(random);
  lua_pushinteger(L, first);
  lua_pushinteger(L, second);
  lua_call(L, 2, 0);
  lua_getfield(L, -1, "randomseed");
  lua_pushlightuserdata(L, &random);
  lua_pushcclosure(L, &guardedRandomseed, 2);
  lua_setfield(L, -2, "randomseed");
  lua_pop(L, 1);
}

}  // namespace GameScript
