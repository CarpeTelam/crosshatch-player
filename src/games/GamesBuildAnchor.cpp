#if FREEINK_CAP_GAMES

// Build anchor for the game libraries. PlatformIO's default
// `chain` dependency finder follows these includes without evaluating the #if, so
// every env, C3 included, compiles lib/GameCore, lib/GameScript, and lib/lua; the
// linker drops them where nothing references them. Envs using `deep+`
// (the simulator) evaluate the #if and build them only where FREEINK_CAP_GAMES is set.
// This file's own body compiles only where FREEINK_CAP_GAMES is set, so the checks
// here run only there. Header-only lib/GameIcons's static_asserts still run wherever
// lib/GameScript compiles, the C3 included: lib/GameScript/ChBindings.cpp includes
// GameIcons.h without an #if.
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
