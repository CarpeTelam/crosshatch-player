#include "ScriptVm.h"

#include <Codec.h>
#include <IRandom.h>
#include <Sandbox.h>

#include <cstdio>
#include <cstring>
#include <lua.hpp>
#include <new>

namespace games_check {

using GameScript::ArenaAllocator;
using GameScript::Fault;

namespace {

// One protected call's arguments, passed through the trampoline.
struct Call {
  ScriptVm::Body body;
  void* context;
};

struct ChunkCall {
  std::string_view text;
  const char* name;
  int* ref;
};

struct RequireCall {
  const char* name;
  int* ref;
};

struct FunctionCall {
  int function;
  const std::span<const uint8_t>* snapshot;  // null: no argument
  int* ref;
};

VmResult failure(const VmResult::Kind kind, const char* message) {
  VmResult result;
  result.kind = kind;
  result.message = message;
  return result;
}

// CallGuard's hook mask (CallGuard.cpp's HOOK_MASK): every HOOK_INTERVAL instructions and every function call.
constexpr int HOOK_MASK = LUA_MASKCOUNT | LUA_MASKCALL;

// The check VM whose protected call is running, for its count hook and `within_device_budget`, which Lua calls with
// no way to name a VM. Set around lua_pcall in protect() and put back as it was, so a nested use stays right.
thread_local ScriptVm* runningVm = nullptr;

}  // namespace

ScriptVm::ScriptVm(ArenaAllocator& arena, const GameScript::GameSources& sources, const GameScript::HostPorts& ports,
                   const GameScript::Canvas& canvas, const GameCore::GameImages& images, const VmLimits& limits)
    : arena(arena), sources(sources), images(images), ports(ports), canvas(canvas), limits(limits) {}

ScriptVm::~ScriptVm() { close(); }

void ScriptVm::close() {
  if (L) lua_close(L);
  L = nullptr;
  pendingTimer.cancel();
  arena.release(scratch);
  scratch = nullptr;
  bindings = GameScript::BindingContext{};
}

VmResult ScriptVm::load() {
  close();
  // From the reserve, which the Lua heap can never take (LuaGame::load).
  scratch = arena.allocate(GameScript::LuaGame::SCRATCH_BYTES);
  if (!scratch) return failure(VmResult::Kind::Fault, "not enough memory");
  L = lua_newstate(&ArenaAllocator::luaAlloc, &arena, ports.random.next32());
  if (!L) return failure(VmResult::Kind::Fault, "not enough memory");
  bindings.canvas = &canvas;
  bindings.sources = &sources;
  bindings.images = &images;
  bindings.guard = &guard;
  bindings.lockedSections = &lockedSections;
  bindings.clock = &ports.clock;
  bindings.paused = ports.paused;
  bindings.startMs = ports.clock.nowMs();
  bindings.timer = &pendingTimer;
  bindings.store = &ports.store;
  bindings.scratch = scratch;
  bindings.scratchBytes = GameScript::LuaGame::SCRATCH_BYTES;
  bindings.log = &ports.log;
  GameScript::setBindingContext(L, &bindings);
  guard.install(L);
  struct Open {
    GameCore::IRandom* random;
    bool host;
  } open{&ports.random, limits.checkCode};
  return protect(
      [](lua_State* state, void* context) {
        const auto& open = *static_cast<Open*>(context);
        GameScript::openSandbox(state, *open.random);
        GameScript::openChLibrary(state);
        if (open.host) openHost(state);
      },
      &open);
}

void ScriptVm::openHost(lua_State* L) {
  lua_createtable(L, 0, 5);
  lua_pushinteger(L, host::DIALOG_TOP);
  lua_setfield(L, -2, "dialog_top");
  lua_pushinteger(L, host::DIALOG_BOTTOM);
  lua_setfield(L, -2, "dialog_bottom");
  lua_pushinteger(L, host::BANNER_TOP);
  lua_setfield(L, -2, "banner_top");
  lua_pushinteger(L, host::CANVAS_HEIGHT);
  lua_setfield(L, -2, "canvas_h");
  lua_pushcfunction(L, &ScriptVm::imageSize);
  lua_setfield(L, -2, "image_size");
  lua_setglobal(L, "host");
  lua_pushcfunction(L, &ScriptVm::withinDeviceBudget);
  lua_setglobal(L, "within_device_budget");
}

// host.image_size(name) -> width, height: the installed image `name` (no ".bmp"), as the installer wrote it.
int ScriptVm::imageSize(lua_State* L) {
  size_t length = 0;
  const char* name = luaL_checklstring(L, 1, &length);
  const GameCore::GameImages* images = GameScript::bindingContext(L)->images;
  const int at = images ? images->find(name, length) : -1;
  if (at < 0) return luaL_error(L, "host.image_size: the game has no image '%s'", name);
  lua_pushinteger(L, images->spans[at].width);
  lua_pushinteger(L, images->spans[at].height);
  return 2;
}

// within_device_budget(f, ...): runs f(...) and returns its results, and raises (an ordinary Lua error, so the check
// fails and the others still run) when f spent the device's CallGuard::INSTRUCTION_BUDGET or more. The interval is
// restarted first, so the count starts on a hook boundary as the device's does at the start of a call, and a call of
// n instructions is n / HOOK_INTERVAL events whichever phase the check's own work left the interval in.
int ScriptVm::withinDeviceBudget(lua_State* L) {
  luaL_checktype(L, 1, LUA_TFUNCTION);
  ScriptVm* vm = runningVm;
  if (!vm || !vm->limits.checkCode) return luaL_error(L, "within_device_budget is for a check VM");
  // A sticky fault the script caught: raise before the hook interval is reset below, so a faulted script cannot go on.
  if (vm->refusedSinceArm()) vm->guard.raiseStatic(L, "not enough memory");
  if (vm->guard.fault() != Fault::None) return luaL_error(L, "within_device_budget: %s", vm->guard.message());
  lua_sethook(L, &ScriptVm::countHook, HOOK_MASK, GameScript::CallGuard::HOOK_INTERVAL);
  const uint64_t before = vm->spent;
  lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
  const uint64_t used = vm->spent - before;
  if (used >= GameScript::CallGuard::INSTRUCTION_BUDGET) {
    return luaL_error(L, "within_device_budget: the call spent %d instructions or more; the device's budget is %d",
                      static_cast<int>(used), static_cast<int>(GameScript::CallGuard::INSTRUCTION_BUDGET));
  }
  return lua_gettop(L);
}

bool ScriptVm::refusedSinceArm() const { return arena.luaCapRefusals() + arena.luaRegionRefusals() != refusalsAtArm; }

// A check VM's count hook: the budget is host::CHECK_INSTRUCTION_BUDGET, counted here; call events (the stack
// headroom, a cancel) are CallGuard's. Once a fault is recorded CallGuard::hook is installed again, at interval 1.
void ScriptVm::countHook(lua_State* L, lua_Debug* ar) {
  ScriptVm* vm = runningVm;
  if (ar->event != LUA_HOOKCOUNT || !vm) {
    GameScript::CallGuard::hook(L, ar);
    return;
  }
  vm->spent += GameScript::CallGuard::HOOK_INTERVAL;
  if (vm->refusedSinceArm()) vm->guard.raiseStatic(L, "not enough memory");
  if (vm->spent < vm->limits.instructionBudget) return;
  if (lua_getinfo(L, "Sl", ar) && ar->currentline > 0) {
    snprintf(vm->budgetText, sizeof(vm->budgetText), "%s:%d: instruction budget exceeded", ar->short_src,
             ar->currentline);
  } else {
    snprintf(vm->budgetText, sizeof(vm->budgetText), "instruction budget exceeded");
  }
  vm->guard.raiseStatic(L, vm->budgetText);
}

int ScriptVm::messageHandler(lua_State* L) {
  if (lua_type(L, 1) == LUA_TSTRING) return 1;
  if (luaL_callmeta(L, 1, "__tostring") && lua_type(L, -1) == LUA_TSTRING) return 1;
  if (lua_type(L, 1) == LUA_TNUMBER) {
    lua_pushstring(L, lua_tostring(L, 1));
    return 1;
  }
  lua_pushfstring(L, "(error object is a %s value)", luaL_typename(L, 1));
  return 1;
}

int ScriptVm::trampoline(lua_State* L) {
  auto* call = static_cast<Call*>(lua_touserdata(L, 1));
  lua_settop(L, 0);
  call->body(L, call->context);
  return 0;
}

VmResult ScriptVm::protect(const Body body, void* context) {
  if (!L) return failure(VmResult::Kind::Error, "the script VM is not loaded");
  Call call{body, context};
  guard.arm(L);
  spent = 0;
  refusalsAtArm = arena.luaCapRefusals() + arena.luaRegionRefusals();
  if (limits.checkCode) lua_sethook(L, &ScriptVm::countHook, HOOK_MASK, GameScript::CallGuard::HOOK_INTERVAL);
  lua_settop(L, 0);
  lua_pushcfunction(L, &ScriptVm::messageHandler);
  lua_pushcfunction(L, &ScriptVm::trampoline);
  lua_pushlightuserdata(L, &call);
  ScriptVm* const outer = runningVm;
  runningVm = this;
  const int status = lua_pcall(L, 1, 0, 1);
  runningVm = outer;
  VmResult result;
  // A guard fault wins over the status: the script may have caught it and returned (LuaGame::enter).
  if (guard.fault() != Fault::None) {
    result = failure(VmResult::Kind::Fault, guard.message());
  } else if (limits.checkCode && refusedSinceArm()) {
    // CallGuard records a memory error only while its own hook is installed (luaport_memoryerror), which a check VM's
    // is not, so a script's pcall could swallow the error and the call end Ok: any refused allocation is a fault here.
    result = failure(VmResult::Kind::Fault, "not enough memory");
  } else if (status != LUA_OK) {
    result = failure(VmResult::Kind::Error, lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : "unknown error");
  }
  lua_settop(L, 0);
  return result;
}

VmResult ScriptVm::runChunk(const std::string_view text, const char* chunkName, int& ref) {
  ChunkCall chunk{text, chunkName, &ref};
  ref = LUA_REFNIL;
  return protect(
      [](lua_State* L, void* context) {
        auto& chunk = *static_cast<ChunkCall*>(context);
        // Text mode, as main.lua loads: a precompiled chunk is refused.
        if (luaL_loadbufferx(L, chunk.text.data(), chunk.text.size(), chunk.name, "t") != LUA_OK) lua_error(L);
        lua_call(L, 0, 1);
        *chunk.ref = luaL_ref(L, LUA_REGISTRYINDEX);
      },
      &chunk);
}

VmResult ScriptVm::requireModule(const char* name, int& ref) {
  RequireCall require{name, &ref};
  ref = LUA_REFNIL;
  return protect(
      [](lua_State* L, void* context) {
        auto& require = *static_cast<RequireCall*>(context);
        lua_getglobal(L, "require");
        lua_pushstring(L, require.name);
        lua_call(L, 1, 1);
        *require.ref = luaL_ref(L, LUA_REGISTRYINDEX);
      },
      &require);
}

VmResult ScriptVm::call(const int functionRef, int& resultRef) {
  FunctionCall function{functionRef, nullptr, &resultRef};
  resultRef = LUA_REFNIL;
  return protect(
      [](lua_State* L, void* context) {
        auto& function = *static_cast<FunctionCall*>(context);
        lua_rawgeti(L, LUA_REGISTRYINDEX, function.function);
        lua_call(L, 0, 1);
        *function.ref = luaL_ref(L, LUA_REGISTRYINDEX);
      },
      &function);
}

VmResult ScriptVm::callWithState(const int functionRef, const std::span<const uint8_t> snapshot, int& resultRef) {
  FunctionCall function{functionRef, &snapshot, &resultRef};
  resultRef = LUA_REFNIL;
  return protect(
      [](lua_State* L, void* context) {
        auto& function = *static_cast<FunctionCall*>(context);
        lua_rawgeti(L, LUA_REGISTRYINDEX, function.function);
        pushSnapshot(L, *function.snapshot);
        lua_call(L, 1, 1);
        *function.ref = luaL_ref(L, LUA_REGISTRYINDEX);
      },
      &function);
}

void ScriptVm::pushSnapshot(lua_State* L, const std::span<const uint8_t> snapshot) {
  const GameScript::Codec::Error error =
      GameScript::Codec::decode(L, snapshot.data(), snapshot.size(), GameScript::Codec::SNAPSHOT_LIMIT);
  if (error != GameScript::Codec::Error::None) {
    luaL_error(L, "state could not be decoded (%s)", GameScript::Codec::errorName(error));
  }
}

void ScriptVm::inspect(const int ref, const Body body, void* context) {
  if (!L) return;
  const int top = lua_gettop(L);
  if (ref == LUA_NOREF || ref == LUA_REFNIL) {
    lua_pushnil(L);
  } else {
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
  }
  body(L, context);
  lua_settop(L, top);
}

void ScriptVm::release(int& ref) {
  if (L && ref != LUA_NOREF && ref != LUA_REFNIL) luaL_unref(L, LUA_REGISTRYINDEX, ref);
  ref = LUA_NOREF;
}

bool isModuleName(const std::string& name) {
  if (name.empty() || name.size() > GameScript::SourceSpan::MAX_NAME_BYTES) return false;
  for (const char c : name) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
  }
  return true;
}

LoadNesting probeLoadNesting(OwnedVm& owned, const char* script) {
  // `seen` marks a name the first time it is required; the second time it is a cache hit (or a cycle the sandbox
  // names), which loads nothing, so it does not nest. The wrapper re-raises what the load raised, as the sandbox's
  // require would, so main's own error is main's (the outer pcall drops it: the rounds name it).
  static const char PROBE[] = R"lua(
local real, seen, chain, deepest = require, {}, {}, ""
local depth = 0
local function wrapped(name)
  if seen[name] then return real(name) end
  seen[name] = true
  chain[#chain + 1] = name
  if #chain > depth then
    depth, deepest = #chain, table.concat(chain, " > ")
  end
  local got = table.pack(pcall(real, name))
  chain[#chain] = nil
  if not got[1] then error(got[2], 0) end
  return table.unpack(got, 2, got.n)
end
require = wrapped
pcall(require, "main")
require = real
return deepest
)lua";
  LoadNesting nesting;
  ScriptVm& vm = owned.vm();
  int ref = ScriptVm::NO_REF;
  const VmResult ran = vm.runChunk(script ? script : PROBE, "@load-nesting-probe", ref);
  if (ran.ok()) {
    bool text = false;
    struct Read {
      LoadNesting* nesting;
      bool* text;
    } read{&nesting, &text};
    vm.inspect(
        ref,
        [](lua_State* L, void* out) {
          auto& read = *static_cast<Read*>(out);
          if (lua_type(L, -1) != LUA_TSTRING) return;
          read.nesting->chain = lua_tostring(L, -1);
          *read.text = true;
        },
        &read);
    if (!text) nesting.error = "the probe answered with no chain";
  } else if (!ran.fault()) {
    // A fault is main's own (the instruction budget, the heap): the rounds name it. An error is the probe's.
    nesting.error = ran.message;
  }
  vm.release(ref);
  if (!nesting.chain.empty()) {
    // The names are [a-z0-9_]{1,32}, so " > " separates them.
    nesting.depth = 1;
    for (size_t at = nesting.chain.find(" > "); at != std::string::npos; at = nesting.chain.find(" > ", at + 3)) {
      ++nesting.depth;
    }
  }
  return nesting;
}

OwnedVm::~OwnedVm() {
  script.reset();  // closes the state, which returns its heap to the rig's arena, before the rig goes
}

std::unique_ptr<OwnedVm> OwnedVm::create(const GameScript::GameSources& game, const std::vector<ModuleText>& extras,
                                         const GameCore::GameImages& images, const uint32_t seed, std::string& error,
                                         const CanvasSize canvas, const VmLimits& limits) {
  std::unique_ptr<OwnedVm> owned(new (std::nothrow) OwnedVm());
  if (!owned) {
    error = "out of memory";
    return nullptr;
  }
  owned->rigOwner = GamesCheckRig::create(seed, canvas, limits);
  if (!owned->rigOwner) {
    error = "out of memory";
    return nullptr;
  }
  // The game's modules first, copied (their text sits in the installer's PSRAM block), then the companion's.
  for (size_t i = 0; i < game.count; ++i) {
    GameScript::SourceSpan span = game.spans[i];
    span.offset = static_cast<uint32_t>(owned->text.size());
    owned->text.append(game.textOf(game.spans[i]), game.spans[i].length);
    owned->spans.push_back(span);
  }
  for (const ModuleText& extra : extras) {
    if (!isModuleName(extra.name)) {
      error = "the companion module '" + extra.name + ".lua' has no module name ([a-z0-9_]{1,32})";
      return nullptr;
    }
    for (const GameScript::SourceSpan& have : owned->spans) {
      if (extra.name == have.name) {
        error = "the companion module '" + extra.name + ".lua' has the name of one of the game's own modules";
        return nullptr;
      }
    }
    GameScript::SourceSpan span{};
    std::memcpy(span.name, extra.name.c_str(), extra.name.size() + 1);
    span.offset = static_cast<uint32_t>(owned->text.size());
    span.length = static_cast<uint32_t>(extra.text.size());
    owned->text += extra.text;
    owned->spans.push_back(span);
  }
  owned->merged.spans = owned->spans.data();
  owned->merged.count = owned->spans.size();
  owned->merged.text = owned->text.data();
  owned->images = images;
  owned->script.reset(new (std::nothrow) ScriptVm(owned->rigOwner->arena(), owned->merged, owned->rigOwner->ports(),
                                                  owned->rigOwner->canvas(), owned->images, limits));
  if (!owned->script) {
    error = "out of memory";
    return nullptr;
  }
  const VmResult loaded = owned->script->load();
  if (!loaded.ok()) {
    error = "the script VM did not start: " + loaded.message;
    return nullptr;
  }
  return owned;
}

}  // namespace games_check
