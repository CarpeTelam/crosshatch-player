#pragma once

// A Lua state with the sandbox a game gets, for the scripts the check itself runs: a round file (rounds/<name>.lua),
// a game's checks.lua, and a round's `steps(state)`. Not a game: it has no main.lua game table, no state codec round
// trip, and no `ui` tables; it is `LuaGame::loadEntry`'s sandbox (the same ArenaAllocator::luaAlloc heap, openSandbox,
// openChLibrary, binding context, and CallGuard) and a way to run a chunk or call a function under the guard.
//
// A test double of the device sandbox, and it says so: ScriptVmTest pins the shared behaviour (os, load, and package
// are nil, require finds the game's modules, a loop faults at the instruction budget, ch.gfx outside draw is an error)
// by running the same snippets through LuaGame (DirectGame) and through this class.
//
// Every entry into Lua (opening the libraries, a chunk, a call) is one lua_pcall under CallGuard::arm, so each has
// its own CallGuard::INSTRUCTION_BUDGET (2,000,000) and no Lua error escapes to the C++ caller. A returned value is
// kept in the registry under an int reference, and read with `inspect`, whose callback must not raise.

#include <ArenaAllocator.h>
#include <CallGuard.h>
#include <ChBindings.h>
#include <GameImages.h>
#include <GameSources.h>
#include <GameTimer.h>
#include <LuaGame.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "GamesCheckRig.h"

struct lua_State;

namespace games_check {

// How one entry into Lua ended: Ok; Error, a Lua error the script raised (the VM stays usable); or Fault, a CallGuard
// fault (the instruction budget, the heap cap, a binding limit such as a full frame): LuaGame ends the game on one, so
// the check stops the script's remaining work on one too.
struct VmResult {
  enum class Kind : uint8_t { Ok, Error, Fault };
  Kind kind = Kind::Ok;
  std::string message;

  bool ok() const { return kind == Kind::Ok; }
  bool fault() const { return kind == Kind::Fault; }
};

class ScriptVm {
 public:
  // A function the protected call runs: raises as Lua code does, on a state whose stack holds only the call's own
  // slots.
  using Body = void (*)(lua_State*, void*);
  // lua.h's LUA_NOREF, which luaL_ref never returns for a value.
  static constexpr int NO_REF = -2;

  // Everything borrowed must outlive the VM. `ports.random` also seeds math.random (openSandbox) and the Lua state.
  ScriptVm(GameScript::ArenaAllocator& arena, const GameScript::GameSources& sources,
           const GameScript::HostPorts& ports, const GameScript::Canvas& canvas, const GameCore::GameImages& images);
  ~ScriptVm();
  ScriptVm(const ScriptVm&) = delete;
  ScriptVm& operator=(const ScriptVm&) = delete;

  // Takes the codec scratch from the arena's reserve, creates the state, opens the sandbox and `ch`, and installs the
  // guard (the opening is one guarded call).
  VmResult load();
  bool started() const { return L != nullptr; }
  lua_State* state() const { return L; }

  // `body` as one guarded call: a fresh instruction budget, no fault, an empty stack on return.
  VmResult protect(Body body, void* context);
  // Compiles `text` as a text chunk named `chunkName` ("@rounds/x.lua": the name error messages show), runs it, and
  // keeps the value it returns in `ref` (REFNIL for nil).
  VmResult runChunk(std::string_view text, const char* chunkName, int& ref);
  // The sandbox's require(name): runs the game's (or the companion's) module once and keeps its value in `ref`.
  VmResult requireModule(const char* name, int& ref);
  // Calls the function kept under `functionRef` with no arguments, keeping its first result in `resultRef`.
  VmResult call(int functionRef, int& resultRef);
  // The same with one argument, the state `snapshot` encodes, decoded into a table (pushSnapshot).
  VmResult callWithState(int functionRef, std::span<const uint8_t> snapshot, int& resultRef);

  // Pushes the value kept under `ref`, calls `body` with it on top of the stack, and restores the stack. `body` runs
  // outside any protected call, so it must not raise: use lua_next, lua_type, lua_to* on values already typed, and
  // never a call that allocates or runs a metamethod (lua_getfield, lua_pushstring, lua_tolstring on a number).
  void inspect(int ref, Body body, void* context);
  // Drops the registry's hold on `ref` and sets it to NO_REF.
  void release(int& ref);

  // Pushes the table `snapshot` encodes (Codec::decode, the same call LuaGame makes for status, apply, draw, and
  // input); raises "state could not be decoded" when the bytes are not a valid snapshot. Call inside a protected call.
  static void pushSnapshot(lua_State* L, std::span<const uint8_t> snapshot);

 private:
  static int trampoline(lua_State* L);
  static int messageHandler(lua_State* L);
  void close();

  GameScript::ArenaAllocator& arena;
  const GameScript::GameSources& sources;
  const GameCore::GameImages& images;
  const GameScript::HostPorts ports;
  const GameScript::Canvas canvas;
  GameScript::GameTimer pendingTimer;
  GameScript::BindingContext bindings;
  GameScript::CallGuard guard;
  std::atomic<uint32_t> lockedSections{0};
  lua_State* L = nullptr;
  void* scratch = nullptr;
};

// A module the companion folder adds beside the game's own: its top-level `<name>.lua` files.
struct ModuleText {
  std::string name;
  std::string text;
};

// A ScriptVm with everything it borrows: its own rig (arena, ports, canvas), and the game's modules and the companion's
// merged into one source table. A round keeps one alive until its `steps` function ran.
class OwnedVm {
 public:
  // Null, with `error` set, on a companion module whose name is no module name or that a game module already has
  // (the game's module wins nowhere: a clash is a failure), or when memory ran out. `game` and `images` must outlive
  // the result; the extras are copied. `seed` seeds the VM's math.random: checks.lua's is 1.
  static std::unique_ptr<OwnedVm> create(const GameScript::GameSources& game, const std::vector<ModuleText>& extras,
                                         const GameCore::GameImages& images, uint32_t seed, std::string& error);
  ~OwnedVm();

  ScriptVm& vm() { return *script; }
  GamesCheckRig& rig() { return *rigOwner; }

 private:
  OwnedVm() = default;

  std::unique_ptr<GamesCheckRig> rigOwner;
  std::string text;
  std::vector<GameScript::SourceSpan> spans;
  GameScript::GameSources merged;
  GameCore::GameImages images;
  std::unique_ptr<ScriptVm> script;
};

// True when `name` can be a module name: [a-z0-9_]{1,32}, as GameAssets loads them.
bool isModuleName(const std::string& name);

}  // namespace games_check
