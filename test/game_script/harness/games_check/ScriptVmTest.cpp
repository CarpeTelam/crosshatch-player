// ScriptVm stands in for the device sandbox (LuaGame::loadEntry) in the scripts the games check runs. These tests pin
// the two together: the same snippets run through a real LuaGame (DirectGame, as SandboxTest does) and through ScriptVm
// must give the same answer, so a change to the sandbox that the stand-in does not follow fails here.

#include <Codec.h>
#include <Memory.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <lua.hpp>
#include <memory>
#include <string>
#include <vector>

#include "LuaGameFixture.h"
#include "ScriptVm.h"
#include "TestSupport.h"

namespace {

using games_check::ModuleText;
using games_check::OwnedVm;
using games_check::ScriptVm;
using games_check::VmLimits;
using games_check::VmResult;
using GameScriptTestSupport::DirectGame;

class ScriptVmTest : public GameScriptTestSupport::LuaGameTest {
 protected:
  // The text of Lua expression `expression` as a game's setup computes it (and its draw prints it), or "error:
  // <message>".
  std::string viaGame(const std::string& expression, const std::vector<Module>& modules = {}) {
    std::vector<Module> all = {
        {"main", "return { setup = function() return { r = tostring(" + expression +
                     ") } end,\n  draw = function(s) ch.gfx.text(0, 0, s.r, 'small', 'black') end }"}};
    all.insert(all.end(), modules.begin(), modules.end());
    useSources(all);
    auto game = makeUniqueNoThrow<DirectGame>(arena, frames, sources, ports, canvas);  // large: on the heap
    GameScript::Outcome outcome = game->start();
    if (outcome == GameScript::Outcome::Ok) outcome = game->draw();
    if (outcome != GameScript::Outcome::Ok) return std::string("error: ") + game->errorMessage();
    return frontText();
  }

  // The same expression as a chunk in a ScriptVm over the same ports, sources, and canvas.
  std::string viaVm(const std::string& expression, const std::vector<Module>& modules = {}) {
    useSources(modules);
    auto vmOwner = newVm();
    ScriptVm& vm = *vmOwner;
    const VmResult loaded = vm.load();
    if (!loaded.ok()) return "load failed: " + loaded.message;
    int ref = ScriptVm::NO_REF;
    const VmResult ran = vm.runChunk("return tostring(" + expression + ")", "@pin.lua", ref);
    if (!ran.ok()) return "error: " + ran.message;
    std::string text;
    vm.inspect(
        ref,
        [](lua_State* L, void* out) {
          if (lua_type(L, -1) == LUA_TSTRING) *static_cast<std::string*>(out) = lua_tostring(L, -1);
        },
        &text);
    return text;
  }

  // A ScriptVm over the fixture's arena, ports, sources, and canvas; on the heap (it holds a CallGuard and the binding
  // context).
  std::unique_ptr<ScriptVm> newVm() {
    return makeUniqueNoThrow<ScriptVm>(arena, sources, ports, canvas, images, VmLimits::device());
  }

  static bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

  // Whether a real LuaGame (DirectGame) loads the modules with its stack floor set so that exactly `margin` bytes of
  // stack below this frame are usable by a require: the floor is this frame minus the parser headroom a require needs,
  // minus `margin`. False, with the game's message in `message`, when the sandbox refuses a load. Not inlined, so the
  // frame the floor is taken from is the same for every margin.
  __attribute__((noinline)) bool loadsAtMargin(const std::vector<Module>& modules, const size_t margin,
                                               std::string& message) {
    useSources(modules);
    auto game = makeUniqueNoThrow<DirectGame>(arena, frames, sources, ports, canvas);  // large: on the heap
    game->setStackFloor(reinterpret_cast<uintptr_t>(__builtin_frame_address(0)) -
                        GameScript::CallGuard::PARSE_HEADROOM_BYTES - margin);
    const GameScript::Outcome outcome = game->start();
    message = outcome == GameScript::Outcome::Ok ? "" : game->errorMessage();
    return outcome == GameScript::Outcome::Ok;
  }

  // The least margin at which the sandbox takes `modules`: loading is monotonic in the margin, so it is bisected.
  size_t leastMargin(const std::vector<Module>& modules) {
    std::string message;
    size_t low = 0, high = 1u << 20;  // far more than any frame of this host
    EXPECT_TRUE(loadsAtMargin(modules, high, message)) << message;
    while (low < high) {
      const size_t middle = (low + high) / 2;
      if (loadsAtMargin(modules, middle, message)) {
        high = middle;
      } else {
        low = middle + 1;
      }
    }
    return low;
  }

  // The probe's answer for `modules` in an OwnedVm of the same sources.
  games_check::LoadNesting probe(const std::vector<Module>& modules) {
    useSources(modules);
    std::string error;
    auto owned = OwnedVm::create(sources, {}, images, 1, error, games_check::CANVAS_474, VmLimits::device());
    EXPECT_TRUE(owned) << error;
    if (!owned) return {};
    return games_check::probeLoadNesting(*owned);
  }
};

TEST_F(ScriptVmTest, TheForbiddenLibrariesAreNilInBoth) {
  const std::string expression =
      "tostring(os) .. ' ' .. tostring(load) .. ' ' .. tostring(package) .. ' ' .. tostring(io) .. "
      "' ' .. tostring(debug) .. ' ' .. tostring(dofile)";
  EXPECT_EQ(viaGame(expression), "nil nil nil nil nil nil");
  EXPECT_EQ(viaVm(expression), "nil nil nil nil nil nil");
}

TEST_F(ScriptVmTest, RequireFindsTheGamesModulesInBoth) {
  const std::vector<Module> modules = {{"util", "return { answer = 42 }"}};
  EXPECT_EQ(viaGame("require('util').answer", modules), "42");
  EXPECT_EQ(viaVm("require('util').answer", modules), "42");
  // A module that is not there is the same error.
  EXPECT_TRUE(has(viaGame("require('absent')", modules), "module 'absent' not found"));
  EXPECT_TRUE(has(viaVm("require('absent')", modules), "module 'absent' not found"));
}

TEST_F(ScriptVmTest, ALoopFaultsAtTheInstructionBudgetInBoth) {
  const std::string loop = "(function() while true do end end)()";
  EXPECT_TRUE(has(viaGame(loop), "instruction budget exceeded")) << viaGame(loop);
  EXPECT_TRUE(has(viaVm(loop), "instruction budget exceeded")) << viaVm(loop);
}

TEST_F(ScriptVmTest, ChGfxOutsideDrawIsAnErrorInBoth) {
  const std::string outside = "(function() ch.gfx.clear('white') return 1 end)()";
  EXPECT_TRUE(has(viaGame(outside), "ch.gfx.clear called outside draw")) << viaGame(outside);
  EXPECT_TRUE(has(viaVm(outside), "ch.gfx.clear called outside draw")) << viaVm(outside);
}

TEST_F(ScriptVmTest, MathRandomIsSeededFromThePortsRandomInBoth) {
  // Both VMs draw their seeds from the same fixed IRandom, so the same first draw comes out of each.
  const std::string draw = "math.random(1000000)";
  const std::string viaLuaGame = viaGame(draw);
  ASSERT_FALSE(has(viaLuaGame, "error")) << viaLuaGame;
  EXPECT_EQ(viaVm(draw), viaLuaGame);
}

TEST_F(ScriptVmTest, ChScreenIsTheCanvasThePortsGaveInBoth) {
  // ch.screen is the canvas the ports gave it (OwnedVm gives the device's: RoundFileTest).
  EXPECT_EQ(viaGame("ch.screen.w .. 'x' .. ch.screen.h"), "480x800");
  EXPECT_EQ(viaVm("ch.screen.w .. 'x' .. ch.screen.h"), "480x800");
}

TEST_F(ScriptVmTest, AnErrorLeavesTheVmUsableButAFaultIsNamedAsOne) {
  useSources({});
  auto vmOwner = newVm();
  ScriptVm& vm = *vmOwner;
  ASSERT_TRUE(vm.load().ok());
  int ref = ScriptVm::NO_REF;
  const VmResult raised = vm.runChunk("error('boom')", "@raise.lua", ref);
  EXPECT_EQ(raised.kind, VmResult::Kind::Error);
  EXPECT_TRUE(has(raised.message, "raise.lua:1: boom")) << raised.message;
  const VmResult fine = vm.runChunk("return 1 + 1", "@fine.lua", ref);
  EXPECT_TRUE(fine.ok()) << fine.message;
  const VmResult looped = vm.runChunk("while true do end", "@loop.lua", ref);
  EXPECT_EQ(looped.kind, VmResult::Kind::Fault);
  EXPECT_TRUE(has(looped.message, "loop.lua:1: instruction budget exceeded")) << looped.message;
  // A pcall cannot keep the script going past the fault, as in a game.
  const VmResult caught = vm.runChunk("pcall(function() while true do end end) return 1", "@caught.lua", ref);
  EXPECT_EQ(caught.kind, VmResult::Kind::Fault) << caught.message;
}

TEST_F(ScriptVmTest, EveryEntryHasItsOwnInstructionBudget) {
  useSources({});
  auto vmOwner = newVm();
  ScriptVm& vm = *vmOwner;
  ASSERT_TRUE(vm.load().ok());
  int ref = ScriptVm::NO_REF;
  // About 1.5 million instructions each: three of them are over one budget together and under it apart.
  for (int i = 0; i < 3; ++i) {
    const VmResult ran = vm.runChunk("for i = 1, 1400000 do end return i", "@spin.lua", ref);
    EXPECT_TRUE(ran.ok()) << i << ": " << ran.message;
  }
}

// Row 4: a VM that runs only the check's own code has larger limits than the device's; the played game and the probe
// keep the device's. The device VM and a LuaGame agree on both limits, and a check VM differs from them there only.
TEST_F(ScriptVmTest, ADeviceVmFaultsAtTheDevicesBudgetAndHeapAsALuaGameDoes) {
  const std::string spin = "(function() for i = 1, 3000000 do end return 1 end)()";
  EXPECT_TRUE(has(viaGame(spin), "instruction budget exceeded")) << viaGame(spin);
  EXPECT_TRUE(has(viaVm(spin), "instruction budget exceeded")) << viaVm(spin);
  const std::string heap =
      "(function() local t = {} for i = 1, 400 do t[i] = string.rep('x', 900) .. i end return #t end)()";
  EXPECT_TRUE(has(viaGame(heap), "not enough memory")) << viaGame(heap);
  EXPECT_TRUE(has(viaVm(heap), "not enough memory")) << viaVm(heap);
  // The check's globals are no part of the device's sandbox.
  const std::string globals = "tostring(host) .. ' ' .. tostring(within_device_budget)";
  EXPECT_EQ(viaGame(globals), "nil nil");
  EXPECT_EQ(viaVm(globals), "nil nil");
}

TEST_F(ScriptVmTest, ACheckVmHasTheLargerLimitsTheGlobalsAndStillFaultsPastThem) {
  useSources({});
  std::string error;
  auto owned = OwnedVm::create(sources, {}, images, 1, error, games_check::CANVAS_474, VmLimits::check());
  ASSERT_TRUE(owned) << error;
  ScriptVm& vm = owned->vm();
  int ref = ScriptVm::NO_REF;
  // 3 M instructions and about 360 KB of heap: past the device's limits, inside the check's.
  EXPECT_TRUE(vm.runChunk("for i = 1, 3000000 do end return 1", "@spin.lua", ref).ok());
  EXPECT_TRUE(
      vm.runChunk("local t = {} for i = 1, 400 do t[i] = string.rep('x', 900) .. i end return #t", "@heap.lua", ref)
          .ok());
  // The check's own limits: 16 M instructions, with the chunk and line the device's message names; and the 1 MB heap.
  const VmResult looped = vm.runChunk("while true do end", "@loop.lua", ref);
  EXPECT_EQ(looped.kind, VmResult::Kind::Fault);
  EXPECT_TRUE(has(looped.message, "loop.lua:1: instruction budget exceeded")) << looped.message;
  const VmResult caught = vm.runChunk("pcall(function() while true do end end) return 1", "@caught.lua", ref);
  EXPECT_EQ(caught.kind, VmResult::Kind::Fault) << caught.message;
  const VmResult bomb =
      vm.runChunk("local t = {} for i = 1, 3000 do t[i] = string.rep('x', 900) .. i end return #t", "@bomb.lua", ref);
  EXPECT_EQ(bomb.kind, VmResult::Kind::Fault) << bomb.message;
  EXPECT_TRUE(has(bomb.message, "not enough memory")) << bomb.message;
  // A heap-cap error the script's own pcall catches does not end the call Ok: any refused allocation is a fault in a
  // check VM (the guard's memory hook is not installed there), the script returns normally, and the call still comes
  // back a Fault.
  const VmResult swallowed = vm.runChunk(
      "local ok = pcall(function() local t = {} for i = 1, 3000 do t[i] = string.rep('x', 900) .. i end return #t end) "
      "return ok",
      "@swallowed.lua", ref);
  EXPECT_EQ(swallowed.kind, VmResult::Kind::Fault) << swallowed.message;
  EXPECT_TRUE(has(swallowed.message, "not enough memory")) << swallowed.message;
  // And within_device_budget does not run a function once the call has faulted that way.
  const VmResult after = vm.runChunk(
      "pcall(function() local t = {} for i = 1, 3000 do t[i] = string.rep('x', 900) .. i end end) "
      "within_device_budget(function() ran = true end) return 1",
      "@after-fault.lua", ref);
  EXPECT_EQ(after.kind, VmResult::Kind::Fault) << after.message;
  int64_t ran = 0;
  EXPECT_TRUE(vm.runChunk("return ran == nil and 1 or 2", "@ran.lua", ref).ok());
  vm.inspect(ref, [](lua_State* L, void* out) { *static_cast<int64_t*>(out) = lua_tointeger(L, -1); }, &ran);
  EXPECT_EQ(ran, 1) << "a function ran under within_device_budget after the call had faulted";
  // The VM is usable after a fault, as after one in a device VM: the next entry starts with a fresh budget.
  EXPECT_TRUE(vm.runChunk("return 1", "@after.lua", ref).ok());
  // The limits are the named constants.
  EXPECT_EQ(VmLimits::device().luaHeapBytes, GameScript::LUA_HEAP_BYTES);
  EXPECT_EQ(VmLimits::device().luaRegionBytes, GameScript::LUA_REGION_BYTES);
  EXPECT_EQ(VmLimits::device().instructionBudget, GameScript::CallGuard::INSTRUCTION_BUDGET);
  EXPECT_EQ(VmLimits::check().luaHeapBytes, games_check::host::CHECK_LUA_HEAP_BYTES);
  EXPECT_EQ(VmLimits::check().instructionBudget, games_check::host::CHECK_INSTRUCTION_BUDGET);
  EXPECT_GT(VmLimits::check().luaHeapBytes, GameScript::LUA_HEAP_BYTES);
  EXPECT_GT(VmLimits::check().instructionBudget, GameScript::CallGuard::INSTRUCTION_BUDGET);
  EXPECT_EQ(VmLimits::deviceLess(16384).luaHeapBytes, GameScript::LUA_HEAP_BYTES - 16384);
}

TEST_F(ScriptVmTest, AReturnedFunctionIsKeptAndCalledWithTheDecodedState) {
  useSources({});
  auto vmOwner = newVm();
  ScriptVm& vm = *vmOwner;
  ASSERT_TRUE(vm.load().ok());
  int function = ScriptVm::NO_REF;
  ASSERT_TRUE(vm.runChunk("return function(state) return state.n * 2 + #state.cells end", "@keep.lua", function).ok());
  const std::vector<uint8_t> state = games_check::test::encodeState("{ n = 20, cells = { 0, 0, 0 } }");
  ASSERT_FALSE(state.empty());
  int result = ScriptVm::NO_REF;
  const VmResult called = vm.callWithState(function, state, result);
  ASSERT_TRUE(called.ok()) << called.message;
  int64_t value = 0;
  vm.inspect(result, [](lua_State* L, void* out) { *static_cast<int64_t*>(out) = lua_tointeger(L, -1); }, &value);
  EXPECT_EQ(value, 43);
  // Bytes that are no snapshot are an error naming the decoder, not a crash.
  const std::vector<uint8_t> junk = {0xFF, 0xFF, 0xFF};
  const VmResult bad = vm.callWithState(function, junk, result);
  EXPECT_EQ(bad.kind, VmResult::Kind::Error);
  EXPECT_TRUE(has(bad.message, "could not be decoded")) << bad.message;
  vm.release(function);
  EXPECT_EQ(function, ScriptVm::NO_REF);
}

TEST_F(ScriptVmTest, CompanionModulesAreRequirableAndAClashWithTheGamesOwnIsAFailure) {
  useSources({{"main", "return {}"}, {"util", "return { answer = 42 }"}});
  std::string error;
  auto owned = OwnedVm::create(sources, {ModuleText{"helpers", "return { twice = function(n) return n * 2 end }"}},
                               images, 1, error, games_check::CANVAS_474, VmLimits::check());
  ASSERT_TRUE(owned) << error;
  int ref = ScriptVm::NO_REF;
  const VmResult ran =
      owned->vm().runChunk("return require('helpers').twice(require('util').answer)", "@both.lua", ref);
  ASSERT_TRUE(ran.ok()) << ran.message;
  int64_t value = 0;
  owned->vm().inspect(ref, [](lua_State* L, void* out) { *static_cast<int64_t*>(out) = lua_tointeger(L, -1); }, &value);
  EXPECT_EQ(value, 84);

  EXPECT_FALSE(OwnedVm::create(sources, {ModuleText{"util", "return {}"}}, images, 1, error, games_check::CANVAS_474,
                               VmLimits::check()));
  EXPECT_TRUE(has(error, "util.lua")) << error;
  EXPECT_TRUE(has(error, "game's own modules")) << error;
  EXPECT_FALSE(OwnedVm::create(sources, {ModuleText{"Bad-Name", "return {}"}}, images, 1, error,
                               games_check::CANVAS_474, VmLimits::check()));
  EXPECT_TRUE(has(error, "no module name")) << error;
}

TEST_F(ScriptVmTest, TheLoadNestingProbeFlagsWhatTheSandboxRefusesAtAModelledStackMargin) {
  // The same two games, one that requires both modules from main and one whose module requires the other (main > a >
  // b), through the sandbox with a stack floor that leaves only `margin` bytes below this frame for a require (the
  // modelled task stack of SandboxTest.RequireNeedsParserHeadroom, with the margin as the unknown): the sandbox
  // takes the flat game at the least margin that fits it, refuses the nested one there ("script recursion too deep
  // to load a module": its second require starts a frame deeper), and takes it only with more. The probe, a depth
  // rule that needs no stack model, flags exactly the nested one. This host's frames are about a third of the
  // simulator's, so what the margin is does not matter; that it separates the two games the way the probe does does.
  const std::vector<Module> flat = {{"main", "require('a') require('b') return { setup = function() return {} end }"},
                                    {"a", "return { a = true }"},
                                    {"b", "return { b = true }"}};
  const std::vector<Module> nested = {{"main", "require('a') return { setup = function() return {} end }"},
                                      {"a", "require('b') return { a = true }"},
                                      {"b", "return { b = true }"}};
  const size_t flatMargin = leastMargin(flat);
  std::string message;
  EXPECT_TRUE(loadsAtMargin(flat, flatMargin, message)) << message;
  ASSERT_GT(flatMargin, 0u) << "the flat game needs some stack";
  EXPECT_FALSE(loadsAtMargin(flat, flatMargin - 1, message)) << "the least margin is least";
  EXPECT_FALSE(loadsAtMargin(nested, flatMargin, message)) << "the sandbox took the nested game at the flat margin";
  EXPECT_TRUE(has(message, "script recursion too deep to load a module")) << message;
  EXPECT_TRUE(has(message, "require 'b'")) << message;
  EXPECT_GT(leastMargin(nested), flatMargin);

  const games_check::LoadNesting flatProbe = probe(flat);
  EXPECT_EQ(flatProbe.depth, 2u) << flatProbe.chain;
  EXPECT_LE(flatProbe.depth, games_check::MAX_LOAD_NESTING);
  const games_check::LoadNesting nestedProbe = probe(nested);
  EXPECT_EQ(nestedProbe.chain, "main > a > b");
  EXPECT_EQ(nestedProbe.depth, 3u);
  EXPECT_GT(nestedProbe.depth, games_check::MAX_LOAD_NESTING);
}

TEST_F(ScriptVmTest, TheLoadNestingProbeCountsAFirstRequireOnlyAndLeavesRequireAsItWas) {
  // A module already loaded does not nest: main requires b, then a, and a requires b.
  const games_check::LoadNesting cached =
      probe({{"main", "require('b') require('a') return {}"}, {"a", "require('b') return {}"}, {"b", "return {}"}});
  EXPECT_EQ(cached.depth, 2u) << cached.chain;
  // A require in a function body is not run while main loads, so it is not probed.
  const games_check::LoadNesting lazy = probe({{"main", "return { setup = function() require('a') end }"},
                                               {"a", "require('b') return {}"},
                                               {"b", "return {}"}});
  EXPECT_EQ(lazy.chain, "main");
  // A main that raises still reports the chain it reached, and one with no module requires reports main alone.
  const games_check::LoadNesting raised =
      probe({{"main", "require('a') error('main broke')"}, {"a", "require('b') return {}"}, {"b", "return {}"}});
  EXPECT_EQ(raised.chain, "main > a > b");
  const games_check::LoadNesting plain = probe({{"main", "return {}"}});
  EXPECT_EQ(plain.chain, "main");
  EXPECT_EQ(plain.depth, 1u);
  // The wrapper is gone afterwards: require is the sandbox's again, and a module still loads (and a failed one is the
  // sandbox's own error).
  useSources({{"main", "return {}"}, {"a", "return { a = 1 }"}});
  std::string error;
  auto owned = OwnedVm::create(sources, {}, images, 1, error, games_check::CANVAS_474, VmLimits::device());
  ASSERT_TRUE(owned) << error;
  games_check::probeLoadNesting(*owned);
  int ref = ScriptVm::NO_REF;
  const VmResult ran =
      owned->vm().runChunk("return tostring(require('a').a) .. tostring(pcall(require, 'absent'))", "@after.lua", ref);
  ASSERT_TRUE(ran.ok()) << ran.message;
  std::string text;
  owned->vm().inspect(
      ref,
      [](lua_State* L, void* out) {
        if (lua_type(L, -1) == LUA_TSTRING) *static_cast<std::string*>(out) = lua_tostring(L, -1);
      },
      &text);
  EXPECT_EQ(text, "1false");
}

TEST_F(ScriptVmTest, ALoadNestingProbeThatRaisesOrAnswersNoStringIsAnErrorNotAFinding) {
  useSources({{"main", "return {}"}});
  std::string error;
  auto owned = OwnedVm::create(sources, {}, images, 1, error, games_check::CANVAS_474, VmLimits::device());
  ASSERT_TRUE(owned) << error;
  const games_check::LoadNesting raised = games_check::probeLoadNesting(*owned, "error('probe broke')");
  EXPECT_TRUE(has(raised.error, "probe broke")) << raised.error;
  EXPECT_EQ(raised.depth, 0u);
  const games_check::LoadNesting number = games_check::probeLoadNesting(*owned, "return 5");
  EXPECT_FALSE(number.error.empty());
  EXPECT_EQ(number.depth, 0u);
  // A guard fault is not the probe's error: the instruction budget in the chunk is main's kind of failure, ignored.
  const games_check::LoadNesting faulted = games_check::probeLoadNesting(*owned, "while true do end");
  EXPECT_TRUE(faulted.error.empty()) << faulted.error;
  EXPECT_EQ(faulted.depth, 0u);
  // The real probe on a main that raises at once: the chain is main alone.
  const games_check::LoadNesting plainRaise = probe({{"main", "error('main broke')"}});
  EXPECT_TRUE(plainRaise.error.empty());
  EXPECT_EQ(plainRaise.chain, "main");
  EXPECT_EQ(plainRaise.depth, 1u);
}

TEST_F(ScriptVmTest, TheSeedMakesMathRandomRepeatOrDiffer) {
  useSources({});
  auto draw = [&](const uint32_t seed) {
    std::string error;
    auto owned = OwnedVm::create(sources, {}, images, seed, error, games_check::CANVAS_474, VmLimits::check());
    EXPECT_TRUE(owned) << error;
    int ref = ScriptVm::NO_REF;
    int64_t value = -1;
    if (owned && owned->vm().runChunk("return math.random(1000000000)", "@draw.lua", ref).ok()) {
      owned->vm().inspect(
          ref, [](lua_State* L, void* out) { *static_cast<int64_t*>(out) = lua_tointeger(L, -1); }, &value);
    }
    return value;
  };
  EXPECT_EQ(draw(1), draw(1));
  EXPECT_NE(draw(1), draw(2));
}

}  // namespace
