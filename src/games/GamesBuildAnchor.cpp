#if FREEINK_CAP_GAMES

// Build anchor for the game libraries. PlatformIO's default
// `chain` dependency finder follows these includes without evaluating the #if, so
// every env, C3 included, compiles lib/GameCore, lib/GameScript, lib/GameIcons, and
// lib/lua; the linker drops them where nothing references them. Envs using `deep+`
// (the simulator) evaluate the #if and build them only where FREEINK_CAP_GAMES is set.
// Keep this file free of definitions; it exists only for its includes and the
// compile-time checks on them.
#include <GameIcons.h>
#include <LuaGame.h>
#include <Session.h>

// <climits> comes before the Lua headers, and games rely on 64-bit Lua integers.
// lua.hpp is Lua's own extern "C" wrapper for lua.h, lualib.h, and lauxlib.h.
#include <climits>
#include <lua.hpp>

static_assert(sizeof(lua_Integer) == 8, "games rely on 64-bit Lua integers");

#endif  // FREEINK_CAP_GAMES
