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
// its own instruction budget and no Lua error escapes to the C++ caller. A returned value is kept in the registry
// under an int reference, and read with `inspect`, whose callback must not raise.
//
// The limits are a VmLimits the caller names. With VmLimits::device() the VM is the device's sandbox to the letter
// (CallGuard::INSTRUCTION_BUDGET, 2,000,000, the device's heap in the rig): the load-nesting probe and the
// equivalence tests use it. With VmLimits::check() it runs only the check's own code and differs from the device in
// four ways, each of which makes it more permissive and none stricter:
//   - a larger budget (host::CHECK_INSTRUCTION_BUDGET), counted by a wrapper count hook that replaces CallGuard's
//     (CallGuard::INSTRUCTION_BUDGET is a lib constant, and lib/ is not this check's to change); a call event, and
//     every recorded fault (a binding or memory fault re-installs CallGuard::hook), still go to CallGuard's. The
//     budget fault is a Binding fault in CallGuard's terms and reads "<chunk>:<line>: instruction budget exceeded", as
//     the device's does; the VM result is a Fault either way;
//   - a larger Lua heap (the rig's region and cap, host::CHECK_LUA_HEAP_BYTES);
//   - the Lua global `host` (HostBounds.h's numbers as dialog_top, dialog_bottom, banner_top, canvas_h,
//     image_size(name) -> w, h over the installed images, and launcher_icon() -> source, name, weight: the Games
//     launcher's pick for the installed game, set by the caller with setLauncherIcon) and `within_device_budget(f,
//     ...)`, which runs `f` and raises when it spent the device's 2,000,000 instructions or more, counted on the same
//     hook from a fresh interval, so a check can still prove that a game call fits the device's budget;
//   - CallGuard's throw hook records a memory error only while CallGuard::hook is installed, which a check VM's is not,
//   so
//     a check VM watches the arena itself: it takes ArenaAllocator::luaCapRefusals() + luaRegionRefusals() when a call
//     is armed, and any growth (checked in the count hook, which raises "not enough memory" through guard.raiseStatic
//     so it is sticky as the device's is, and again after the call) makes the call a Fault, whatever the script's own
//     pcall caught. Any refused allocation is a fault in a check VM, even one Lua would have recovered from by
//     collecting and retrying: the cap is 4 times the device's, so a refusal means the check is out of room.

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
#include <utility>
#include <vector>

#include "GamesCheckRig.h"

struct lua_State;
struct lua_Debug;

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

// Where the Games launcher draws a game's row icon from (GameRowIcon::choose over the installed game):
// `host.launcher_icon()` answers it as "package", as "library", name, "fill" or "regular", or as "fallback".
struct LauncherIcon {
  enum class Source : uint8_t { Unset, Package, Library, Fallback };
  Source source = Source::Unset;
  std::string name;  // the library icon, for Library
  bool fill = false;
};

class ScriptVm {
 public:
  // A function the protected call runs: raises as Lua code does, on a state whose stack holds only the call's own
  // slots.
  using Body = void (*)(lua_State*, void*);
  // lua.h's LUA_NOREF, which luaL_ref never returns for a value.
  static constexpr int NO_REF = -2;

  // Everything borrowed must outlive the VM. `ports.random` also seeds math.random (openSandbox) and the Lua state.
  // `limits` has no default; the arena must have been split and capped to match (GamesCheckRig::create does).
  ScriptVm(GameScript::ArenaAllocator& arena, const GameScript::GameSources& sources,
           const GameScript::HostPorts& ports, const GameScript::Canvas& canvas, const GameCore::GameImages& images,
           const VmLimits& limits);
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

  // What `host.launcher_icon()` answers; a check VM raises from it until this is called.
  void setLauncherIcon(LauncherIcon pick) { launcherIcon_ = std::move(pick); }

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
  // A check VM's count hook (see above), and the two globals it registers.
  static void countHook(lua_State* L, lua_Debug* ar);
  static int withinDeviceBudget(lua_State* L);
  static int imageSize(lua_State* L);
  static int launcherIcon(lua_State* L);
  // ArenaAllocator refusals (cap or region) since the call was armed.
  bool refusedSinceArm() const;
  static void openHost(lua_State* L);
  void close();

  GameScript::ArenaAllocator& arena;
  const GameScript::GameSources& sources;
  const GameCore::GameImages& images;
  const GameScript::HostPorts ports;
  const GameScript::Canvas canvas;
  const VmLimits limits;
  // A check VM's count of instructions this entry has spent, in whole hook intervals, and the text of its budget fault
  // (CallGuard::raiseStatic keeps the pointer until the next arm()).
  uint64_t spent = 0;
  size_t refusalsAtArm = 0;
  char budgetText[GameScript::CallGuard::MESSAGE_CAPACITY] = {};
  LauncherIcon launcherIcon_;
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
  // the result; the extras are copied. `seed` seeds the VM's math.random: checks.lua's is 1. `canvas` is `ch.screen`
  // and `limits` the VM's bounds; neither has a default, so a caller that leaves one out does not compile.
  static std::unique_ptr<OwnedVm> create(const GameScript::GameSources& game, const std::vector<ModuleText>& extras,
                                         const GameCore::GameImages& images, uint32_t seed, std::string& error,
                                         CanvasSize canvas, const VmLimits& limits);
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

// The most modules that may be loading at once while main.lua loads: main itself and the one it requires. The device's
// sandbox refuses to parse a module with under CallGuard::PARSE_HEADROOM_BYTES of its 16 KiB VM stack free
// (Sandbox.cpp `require`: "script recursion too deep to load a module"), and a module that loads inside another
// module's load (main > a > b) is what ran a game out of it in the simulator. This host's frames are about a third of
// the simulator's, so bytes are no usable bound here; a depth is, and it answers as the simulator does on any build.
inline constexpr size_t MAX_LOAD_NESTING = 2;

// The deepest chain of modules loading inside one another while main.lua loads: `depth` modules, named `chain` as
// "main > a > b". A main whose load raises still has the chain it reached (just "main" when it raises at once: depth
// 1). Empty and 0 only when the probe's chunk itself faulted (a guard fault in main's own load, which the rounds name:
// no finding) or failed (`error` is then set: the probe is broken, and that is a failure of the check, not a pass).
struct LoadNesting {
  size_t depth = 0;
  std::string chain;
  std::string error;  // the probe could not read a chain: its chunk raised, or answered with no string
};

// ScriptVm's double of Sandbox.cpp's parser-headroom refusal, as a depth rule. It wraps the global `require` to count a
// name the first time it is required (a cache hit never nests), runs `pcall(require, "main")`, restores `require`, and
// reports the deepest chain. Stricter than the device in one way: it counts depth, not bytes, so it can refuse a load
// the device takes. More permissive in another: only main's own load is probed, so a module required in a function body
// (setup, draw, input, a helper main calls later) is not, and nesting loaded lazily is the author's. ScriptVmTest pins
// it to the sandbox at a modelled stack margin where the sandbox refuses the nested game and takes the flat one.
// Runs one guarded chunk on `owned`'s VM, which it leaves with `main` loaded (or not): use a VM of its own. `script`
// replaces the probe's chunk, for a test that reaches the failure branches; leave it null.
LoadNesting probeLoadNesting(OwnedVm& owned, const char* script = nullptr);

}  // namespace games_check
