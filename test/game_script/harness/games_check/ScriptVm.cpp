#include "ScriptVm.h"

#include <Codec.h>
#include <IRandom.h>
#include <Sandbox.h>

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

}  // namespace

ScriptVm::ScriptVm(ArenaAllocator& arena, const GameScript::GameSources& sources, const GameScript::HostPorts& ports,
                   const GameScript::Canvas& canvas, const GameCore::GameImages& images)
    : arena(arena), sources(sources), images(images), ports(ports), canvas(canvas) {}

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
  bindings.startMs = ports.clock.nowMs();
  bindings.timer = &pendingTimer;
  bindings.store = &ports.store;
  bindings.scratch = scratch;
  bindings.scratchBytes = GameScript::LuaGame::SCRATCH_BYTES;
  bindings.log = &ports.log;
  GameScript::setBindingContext(L, &bindings);
  guard.install(L);
  return protect(
      [](lua_State* state, void* random) {
        GameScript::openSandbox(state, *static_cast<GameCore::IRandom*>(random));
        GameScript::openChLibrary(state);
      },
      &ports.random);
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
  lua_settop(L, 0);
  lua_pushcfunction(L, &ScriptVm::messageHandler);
  lua_pushcfunction(L, &ScriptVm::trampoline);
  lua_pushlightuserdata(L, &call);
  const int status = lua_pcall(L, 1, 0, 1);
  VmResult result;
  // A guard fault wins over the status: the script may have caught it and returned (LuaGame::enter).
  if (guard.fault() != Fault::None) {
    result = failure(VmResult::Kind::Fault, guard.message());
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

OwnedVm::~OwnedVm() {
  script.reset();  // closes the state, which returns its heap to the rig's arena, before the rig goes
}

std::unique_ptr<OwnedVm> OwnedVm::create(const GameScript::GameSources& game, const std::vector<ModuleText>& extras,
                                         const GameCore::GameImages& images, const uint32_t seed, std::string& error) {
  std::unique_ptr<OwnedVm> owned(new (std::nothrow) OwnedVm());
  if (!owned) {
    error = "out of memory";
    return nullptr;
  }
  owned->rigOwner = GamesCheckRig::create(seed);
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
                                                  owned->rigOwner->canvas(), owned->images));
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
