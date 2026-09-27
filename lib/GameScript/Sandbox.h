#pragma once

struct lua_State;

namespace GameCore {
class IRandom;
}

#include <cstddef>

namespace GameScript {

// table.move, table.insert, and table.remove refuse more elements than this (a
// Lua error): their loops run in C, where the hook cannot stop them. The 256 KiB
// heap holds at most about 16 K array slots, so no real table comes close.
inline constexpr size_t TABLE_ELEMENTS_LIMIT = 65536;

// Opens the libraries a game gets (AD-6): base without load, loadfile, and dofile;
// table, string, math, and utf8; and a global require(name) that runs name.lua
// from the game's own sources (BindingContext::sources) in text mode, once, and
// returns its value (true for nil). math.random is seeded from `random`. No io,
// os, debug, coroutine, or package. setmetatable refuses __gc, print writes
// nothing (until entry 10), xpcall skips its handler for a CallGuard fault, and
// table.move/insert/remove refuse more than TABLE_ELEMENTS_LIMIT elements. May raise a Lua error (out of memory), so
// call it only inside a protected call.
void openSandbox(lua_State* L, GameCore::IRandom& random);

}  // namespace GameScript
