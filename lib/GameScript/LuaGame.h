#pragma once

#include <cstddef>
#include <cstdint>

#include "ChBindings.h"

struct lua_State;

namespace GameCore {
class IRandom;
}

namespace GameScript {

class ArenaAllocator;
class FrameBuffers;
struct GameSources;
struct InputEvent;

// How a call into the game ended. A ScriptError ends the session (AD-14); later
// stories add Cancelled for a stop requested by the runtime.
enum class Outcome : uint8_t { Ok, ScriptError };

// One game's Lua VM: the state (its heap in `arena`), the game table main.lua
// returned, the current state value, and seat 1's `ui` table. Every entry into
// Lua, from opening the libraries to each callback, goes through one lua_pcall
// trampoline, so no Lua error can escape to the C++ caller. Confined to the task
// that calls start() (the GameVM task on the device).
class LuaGame {
 public:
  static constexpr size_t ERROR_CAPACITY = 160;

  LuaGame(ArenaAllocator& arena, FrameBuffers& frames, const GameSources& sources, GameCore::IRandom& random);
  ~LuaGame();
  LuaGame(const LuaGame&) = delete;
  LuaGame& operator=(const LuaGame&) = delete;

  // Creates the state, opens the libraries and `ch`, runs main.lua, and calls
  // setup(ctx) with ctx = {seats = 1, mode = "solo"}.
  Outcome start();
  // Calls draw(state, 1, ui) into the back buffer and publishes it on success.
  Outcome draw();
  // Calls input(state, 1, ui, event). The returned move is not used yet.
  Outcome input(const InputEvent& event);
  // Closes the state; its memory returns to the arena.
  void close();

  bool started() const { return L != nullptr; }
  // The last ScriptError's message (Lua's, with its chunk and line when it has them).
  const char* errorMessage() const { return error; }

 private:
  enum class Entry : uint8_t { Load, Setup, Draw, Input };
  struct Call {
    LuaGame* game;
    Entry entry;
    const InputEvent* event;
  };

  static int trampoline(lua_State* L);
  static int messageHandler(lua_State* L);
  Outcome enter(Entry entry, const InputEvent* event);
  Outcome fail(const char* message);

  // Run inside the trampoline; each may raise.
  void loadEntry(lua_State* L);
  void setupEntry(lua_State* L);
  void drawEntry(lua_State* L);
  void inputEntry(lua_State* L, const InputEvent& event);
  void pushGameFunction(lua_State* L, const char* name);

  ArenaAllocator& arena;
  FrameBuffers& frames;
  const GameSources& sources;
  GameCore::IRandom& random;
  BindingContext bindings;
  lua_State* L = nullptr;
  // Registry references (LUA_NOREF until set).
  int gameRef = -2;
  int stateRef = -2;
  int uiRef = -2;
  char error[ERROR_CAPACITY] = {};
};

}  // namespace GameScript
