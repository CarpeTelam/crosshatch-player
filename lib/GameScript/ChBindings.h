#pragma once

struct lua_State;

namespace GameScript {

class CallGuard;
class DisplayList;
struct GameSources;

// Everything a ch.* C function may reach, owned by LuaGame and found through the
// state's extra space. Bindings never see LuaGame itself, so new bindings add a
// field here (text metrics, clock, timer, store slot) instead of a dependency.
struct BindingContext {
  // The back display list while draw runs; null otherwise, which makes ch.gfx an error.
  DisplayList* drawTarget = nullptr;
  // The game's modules, which require() resolves against.
  const GameSources* sources = nullptr;
  // The hook's limits for the current call (LuaGame's).
  CallGuard* guard = nullptr;
};

void setBindingContext(lua_State* L, BindingContext* context);
BindingContext* bindingContext(lua_State* L);

// Creates the global `ch` table and its sub-tables. May raise a Lua error (out of
// memory), so call it only inside a protected call.
void openChLibrary(lua_State* L);

}  // namespace GameScript
