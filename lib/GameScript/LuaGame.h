#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "CallGuard.h"
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

// How a call into the game ended. A ScriptError ends the session (AD-14); a
// Cancelled call was stopped by the runtime (requestCancel) and shows nothing.
enum class Outcome : uint8_t { Ok, ScriptError, Cancelled };

// One game's Lua VM: the state (its heap in `arena`), the game table main.lua
// returned, the current state value, and seat 1's `ui` table. Every entry into
// Lua, from opening the libraries to each callback, goes through one lua_pcall
// trampoline, so no Lua error can escape to the C++ caller, and each is limited
// by the CallGuard (budget, cancel, stack headroom). Confined to the task that
// calls start() (the GameVM task on the device), except requestCancel() and
// inLua(), which any task may call.
class LuaGame {
 public:
  static constexpr size_t ERROR_CAPACITY = 160;

  LuaGame(ArenaAllocator& arena, FrameBuffers& frames, const GameSources& sources, GameCore::IRandom& random);
  ~LuaGame();
  LuaGame(const LuaGame&) = delete;
  LuaGame& operator=(const LuaGame&) = delete;

  // Creates the state, opens the sandbox and `ch`, runs main.lua, and calls
  // setup(ctx) with ctx = {seats = 1, mode = "solo"}.
  Outcome start();
  // Calls draw(state, 1, ui) into the back buffer and publishes it on success.
  Outcome draw();
  // Calls input(state, 1, ui, event). The returned move is not used yet.
  Outcome input(const InputEvent& event);
  // Closes the state; its memory returns to the arena. With the Lua stack empty
  // between calls, lua_close runs no script code except __gc finalizers.
  void close();
  // Forgets the state without lua_close, for a VM whose task was deleted mid-call
  // (GameVM::abandon); the owner then frees the arena in one piece.
  void abandon();

  // Any task: the current or next call ends as Cancelled at its next hook event.
  void requestCancel() { guard.requestCancel(); }
  // The lowest address this task's stack may use; see CallGuard::setStackFloor.
  void setStackFloor(uintptr_t lowest) { guard.setStackFloor(lowest); }
  // Any task: true while a call (or lua_close) runs inside Lua. There the task
  // touches the arena, the sources, the back display list, and whatever the Lua
  // libraries and ch bindings touch; a binding that takes a lock marks itself
  // (enterLockedSection), and inLockedBinding() is then true.
  bool inLua() const { return running.load(std::memory_order_acquire); }
  bool inLockedBinding() const { return lockedSections.load(std::memory_order_acquire) != 0; }
  const CallGuard& callGuard() const { return guard; }

  bool started() const { return L != nullptr; }
  // The last ScriptError's message (Lua's, with its chunk and line when it has
  // them), or "cancelled".
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
  Outcome cancelled();

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
  CallGuard guard;
  std::atomic<bool> running{false};
  std::atomic<uint32_t> lockedSections{0};
  lua_State* L = nullptr;
  // Registry references (LUA_NOREF until set).
  int gameRef = -2;
  int stateRef = -2;
  int uiRef = -2;
  char error[ERROR_CAPACITY] = {};
};

}  // namespace GameScript
