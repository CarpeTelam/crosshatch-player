#pragma once

struct lua_State;

namespace GameCore {
class IRandom;
}

namespace GameScript {

// Opens the libraries a game gets (AD-6): base without load, loadfile, and dofile;
// table, string, math, and utf8; and a global require(name) that runs name.lua
// from the game's own sources (BindingContext::sources) in text mode, once, and
// returns its value (true for nil). math.random is seeded from `random`. No io,
// os, debug, coroutine, or package. May raise a Lua error (out of memory), so call
// it only inside a protected call.
void openSandbox(lua_State* L, GameCore::IRandom& random);

}  // namespace GameScript
