#pragma once

#include <atomic>
#include <cstdint>

#include "TextMetrics.h"

struct lua_State;

namespace GameScript {

class CallGuard;
class DisplayList;
struct GameSources;

// The host's screen as a game sees it, fixed at VM start (AD-7): the canvas size
// (GameViewport's), exposed as ch.screen, and the text metrics behind
// ch.text_width (FrameReplay's font tables).
struct Canvas {
  int16_t width = 0;
  int16_t height = 0;
  TextMetrics text;
};

// Everything a ch.* C function may reach, owned by LuaGame and found through the
// state's extra space. Bindings never see LuaGame itself, so new bindings add a
// field here (text metrics, clock, timer, store slot) instead of a dependency.
struct BindingContext {
  // The back display list while draw runs; null otherwise, which makes ch.gfx an error.
  DisplayList* drawTarget = nullptr;
  // ch.screen and ch.text_width (LuaGame's copy).
  const Canvas* canvas = nullptr;
  // The game's modules, which require() resolves against.
  const GameSources* sources = nullptr;
  // The hook's limits for the current call (LuaGame's).
  CallGuard* guard = nullptr;
  // Bindings inside a locked section right now (LuaGame's); see enterLockedSection.
  std::atomic<uint32_t>* lockedSections = nullptr;
};

void setBindingContext(lua_State* L, BindingContext* context);
BindingContext* bindingContext(lua_State* L);

// A binding that takes a lock (a mutex, a newlib call that locks) brackets the
// locked part with these, so GameVM::abandon never deletes the task while it holds
// the lock. Nothing between them may raise a Lua error or call into Lua.
void enterLockedSection(lua_State* L);
void leaveLockedSection(lua_State* L);

// Creates the global `ch` table and its sub-tables; ch.screen comes from the
// context's canvas, which must be set. May raise a Lua error (out of
// memory), so call it only inside a protected call.
void openChLibrary(lua_State* L);

}  // namespace GameScript
