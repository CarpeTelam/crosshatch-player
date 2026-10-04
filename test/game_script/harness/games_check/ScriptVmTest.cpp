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
  std::unique_ptr<ScriptVm> newVm() { return makeUniqueNoThrow<ScriptVm>(arena, sources, ports, canvas, images); }

  static bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }
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
                               images, 1, error);
  ASSERT_TRUE(owned) << error;
  int ref = ScriptVm::NO_REF;
  const VmResult ran =
      owned->vm().runChunk("return require('helpers').twice(require('util').answer)", "@both.lua", ref);
  ASSERT_TRUE(ran.ok()) << ran.message;
  int64_t value = 0;
  owned->vm().inspect(ref, [](lua_State* L, void* out) { *static_cast<int64_t*>(out) = lua_tointeger(L, -1); }, &value);
  EXPECT_EQ(value, 84);

  EXPECT_FALSE(OwnedVm::create(sources, {ModuleText{"util", "return {}"}}, images, 1, error));
  EXPECT_TRUE(has(error, "util.lua")) << error;
  EXPECT_TRUE(has(error, "game's own modules")) << error;
  EXPECT_FALSE(OwnedVm::create(sources, {ModuleText{"Bad-Name", "return {}"}}, images, 1, error));
  EXPECT_TRUE(has(error, "no module name")) << error;
}

TEST_F(ScriptVmTest, TheSeedMakesMathRandomRepeatOrDiffer) {
  useSources({});
  auto draw = [&](const uint32_t seed) {
    std::string error;
    auto owned = OwnedVm::create(sources, {}, images, seed, error);
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
