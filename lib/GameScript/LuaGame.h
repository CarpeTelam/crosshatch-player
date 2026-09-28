#pragma once

#include <GameImages.h>
#include <IGameRules.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

#include "CallGuard.h"
#include "ChBindings.h"
#include "Codec.h"
#include "GameTimer.h"

struct lua_State;

namespace GameCore {
class IClock;
class IGameLog;
class IRandom;
}  // namespace GameCore

namespace GameScript {

class ArenaAllocator;
class FrameBuffers;
class StoreSlot;
struct GameSources;

// The host services a game's VM borrows, each outliving it: seeds (the string
// hash, math.random), the clock behind ch.time and ch.timer, the log behind ch.log
// and print, and the match's ch.store slot.
struct HostPorts {
  GameCore::IRandom& random;
  GameCore::IClock& clock;
  GameCore::IGameLog& log;
  StoreSlot& store;
};

// How a call into the game ended (GameCore's): a ScriptError ends the session
// (AD-14); a Cancelled call was stopped by the runtime (requestCancel) and shows nothing.
using Outcome = GameCore::Outcome;

// One game's Lua VM and the script side of the game contract (AD-8, AD-9): the
// state (its heap in `arena`), the game table main.lua returned, and one `ui` table
// per seat, which persists across calls and is never encoded. As the rules behind
// GameCore::Session it sees states and moves only as codec bytes: every call
// decodes a fresh state, so changes made in draw, input, or status are discarded,
// and setup's and apply's results and input's moves are encoded under the AD-10
// limits into one codec scratch taken from the arena before the state exists.
//
// Every entry into Lua, from opening the libraries to each callback and its codec
// work, goes through one lua_pcall trampoline, so no Lua error can escape to the
// C++ caller, and each is limited by the CallGuard (budget, cancel, stack
// headroom). Confined to the task that calls load() (the GameVM task on the
// device), except requestCancel(), inLua(), inLockedBinding(), callSerial(), and
// timer(), which any task may call.
class LuaGame : public GameCore::IGameRules {
 public:
  static constexpr size_t ERROR_CAPACITY = 160;
  // One scratch serves every encode and ch.store.get's copy (they never overlap: an
  // encode's output is copied out before Lua runs again); sized for the largest limit.
  static constexpr size_t SCRATCH_BYTES = Codec::scratchBytes(Codec::STORE_LIMIT);

  // `canvas` (copied) is what ch.screen and ch.text_width report; `images` (kept
  // by reference, like `sources`) is what ch.gfx.image draws.
  LuaGame(ArenaAllocator& arena, FrameBuffers& frames, const GameSources& sources, const HostPorts& ports,
          const Canvas& canvas, const GameCore::GameImages& images = GameCore::NO_IMAGES);
  ~LuaGame() override;
  LuaGame(const LuaGame&) = delete;
  LuaGame& operator=(const LuaGame&) = delete;

  // Takes the codec scratch from the arena, creates the state, opens the sandbox and
  // `ch`, and runs main.lua, which must return the game table.
  Outcome load();

  // GameCore::IGameRules, each one call into Lua. setup gets ctx = {seats, mode,
  // api}; status must return {turn = seat} or {over = true, winners = {seat...}};
  // apply returns the new state, or nil and a reason (a string, or nil for "");
  // draw draws into the back buffer and publishes it on success; input returns a
  // move table or nil (a Timer event arrives as {kind = "timer"}).
  Outcome setup(const GameCore::GameContext& ctx, std::span<const uint8_t>& state) override;
  Outcome status(std::span<const uint8_t> state, const GameCore::Roster& roster, GameCore::Status& out) override;
  Outcome apply(std::span<const uint8_t> state, uint8_t seat, std::span<const uint8_t> move,
                std::span<const uint8_t>& next, std::span<char> reason) override;
  Outcome draw(std::span<const uint8_t> state, uint8_t seat) override;
  Outcome input(std::span<const uint8_t> state, uint8_t seat, const GameCore::GameEvent& event,
                std::span<const uint8_t>& move) override;

  // Closes the state, clears the timer, and returns the scratch; all of its memory
  // goes back to the arena. With the Lua stack empty between calls, lua_close runs
  // no script code except __gc finalizers.
  void close();
  // Forgets the state and the scratch without lua_close, for a VM whose task was
  // deleted mid-call (GameVM::abandon); the owner then frees the arena in one piece.
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
  // Any task: counts entries into Lua (calls and lua_close), so a watchdog can time
  // each call on its own.
  uint32_t callSerial() const { return calls.load(std::memory_order_acquire); }
  const CallGuard& callGuard() const { return guard; }
  // Any task: ch.timer's pending timer, which the owner polls (GameTimer).
  GameTimer& timer() { return pendingTimer; }

  bool started() const { return L != nullptr; }
  // The last ScriptError's message (Lua's, with its chunk and line when it has
  // them, cut at a UTF-8 boundary to fit), or "cancelled". For a host failure it
  // is English log text, which the error view replaces with its own tr() text.
  const char* errorMessage() const { return error; }
  // A ScriptError the host raised itself rather than the script (AD-14): its
  // scratch or state did not fit in the arena, or an entry ran before load().
  enum class HostFailure : uint8_t { None, OutOfMemory, NotLoaded };
  // The last ScriptError's host failure; None for the script's own errors, and
  // after a Cancelled or a new load().
  HostFailure hostFailure() const { return hostFailed; }

 private:
  enum class Entry : uint8_t { Load, Setup, Status, Apply, Draw, Input };
  // One entry's arguments and result slots, passed to the trampoline.
  struct Call {
    LuaGame* game = nullptr;
    Entry entry = Entry::Load;
    uint8_t seat = 0;
    std::span<const uint8_t> state;
    std::span<const uint8_t> move;
    const GameCore::GameEvent* event = nullptr;
    const GameCore::GameContext* ctx = nullptr;
    const GameCore::Roster* roster = nullptr;
    std::span<const uint8_t>* encoded = nullptr;  // setup's or apply's state, input's move
    GameCore::Status* status = nullptr;
    std::span<char> reason;
  };

  static int trampoline(lua_State* L);
  static int messageHandler(lua_State* L);
  Outcome enter(Call& call);
  Outcome fail(const char* message, HostFailure kind = HostFailure::None);
  Outcome cancelled();

  // Run inside the trampoline; each may raise.
  void loadEntry(lua_State* L);
  void setupEntry(lua_State* L, Call& call);
  void statusEntry(lua_State* L, Call& call);
  void applyEntry(lua_State* L, Call& call);
  void drawEntry(lua_State* L, const Call& call);
  void inputEntry(lua_State* L, Call& call);
  void pushGameFunction(lua_State* L, const char* name);
  void pushUi(lua_State* L, uint8_t seat);
  // Encodes the value on top as `what` for `function`, under `limit`.
  void encodeTop(lua_State* L, const char* function, const char* what, size_t limit, std::span<const uint8_t>& out);

  ArenaAllocator& arena;
  FrameBuffers& frames;
  const GameSources& sources;
  const GameCore::GameImages& images;
  const HostPorts ports;
  const Canvas canvas;
  GameTimer pendingTimer;
  BindingContext bindings;
  CallGuard guard;
  std::atomic<bool> running{false};
  std::atomic<uint32_t> lockedSections{0};
  std::atomic<uint32_t> calls{0};
  lua_State* L = nullptr;
  void* scratch = nullptr;  // SCRATCH_BYTES from the arena while loaded
  // Registry references (LUA_NOREF until set).
  int gameRef = -2;
  int uisRef = -2;  // seat -> ui table
  char error[ERROR_CAPACITY] = {};
  HostFailure hostFailed = HostFailure::None;
};

}  // namespace GameScript
