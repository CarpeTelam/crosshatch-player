#include <gtest/gtest.h>

#include <climits>
#include <lua.hpp>
#include <string>

#include "LuaGameFixture.h"

using namespace GameScript;
using GameScriptTestSupport::FixedRandom;
using GameScriptTestSupport::readFixture;

namespace {

class SandboxTest : public GameScriptTestSupport::LuaGameTest {
 protected:
  // Runs setup and one draw; returns the drawn text, or the error prefixed "error: ".
  std::string startAndDraw(GameCore::IRandom& rng) {
    LuaGame game(arena, frames, sources, rng);
    Outcome outcome = game.start();
    if (outcome == Outcome::Ok) outcome = game.draw();
    if (outcome != Outcome::Ok) return std::string("error: ") + game.errorMessage();
    return frontText();
  }
  std::string startAndDraw() { return startAndDraw(random); }

  // A real binary chunk, as string.dump or luac would write it.
  static std::string binaryChunk() {
    lua_State* L = luaL_newstate();
    std::string bytes;
    luaL_loadstring(L, "return 1");
    lua_dump(
        L,
        [](lua_State*, const void* p, size_t size, void* out) {
          static_cast<std::string*>(out)->append(static_cast<const char*>(p), size);
          return 0;
        },
        &bytes, 0);
    lua_close(L);
    return bytes;
  }
};

constexpr const char* DRAW_RESULT =
    "\n  draw = function(s) ch.gfx.text(0, 0, tostring(s.result), 'small', 'black') end }";

TEST_F(SandboxTest, GlobalsAreTheLevelOneBaseSetChAndRequire) {
  // The base library entries of docs/crosshatch/api-level-1.txt, the four library
  // tables, ch, require, and print (which entry 10 maps to ch.log).
  useSource("main", std::string("return { setup = function()\n"
                                "  local names = {}\n"
                                "  for k in pairs(_G) do names[#names + 1] = k end\n"
                                "  table.sort(names)\n"
                                "  return { result = table.concat(names, ' ') } end,") +
                        DRAW_RESULT);
  EXPECT_EQ(
      startAndDraw(),
      "_G _VERSION assert ch collectgarbage error getmetatable ipairs math next pairs pcall print rawequal rawget "
      "rawlen rawset require select setmetatable string table tonumber tostring type utf8 warn xpcall");
}

TEST_F(SandboxTest, ForbiddenLibrariesAndLoadersAreScriptErrors) {
  struct Case {
    const char* fixture;
    const char* message;
  };
  const Case cases[] = {
      {"io", "attempt to index a nil value (global 'io')"},
      {"os", "attempt to index a nil value (global 'os')"},
      {"load", "attempt to call a nil value (global 'load')"},
      {"binary_chunk", "attempt to call a nil value (global 'load')"},
  };
  for (const auto& c : cases) {
    useFault(c.fixture);
    LuaGame game(arena, frames, sources, random);
    EXPECT_EQ(game.start(), Outcome::ScriptError) << c.fixture;
    EXPECT_TRUE(contains(game.errorMessage(), c.message)) << c.fixture << " -> " << game.errorMessage();
  }
  for (const char* name : {"loadfile", "dofile", "debug", "coroutine", "package"}) {
    useSource("main",
              std::string("return { setup = function() return { result = type(") + name + ") } end," + DRAW_RESULT);
    EXPECT_EQ(startAndDraw(), "nil") << name;
  }
}

TEST_F(SandboxTest, RequireLoadsModulesOnceFromTheSourceTable) {
  useSources({{"main", readFixture("modules/main.lua")},
              {"util", readFixture("modules/util.lua")},
              {"quiet", readFixture("modules/quiet.lua")}});
  // same, loads, the name passed to the chunk, nil becomes true, and a call works.
  EXPECT_EQ(startAndDraw(), "true 1 util true 5");
}

TEST_F(SandboxTest, RequireFaultsAreScriptErrors) {
  useFault("missing_require");
  {
    LuaGame game(arena, frames, sources, random);
    EXPECT_EQ(game.start(), Outcome::ScriptError);
    EXPECT_TRUE(contains(game.errorMessage(), "main.lua:2: module 'nothere' not found")) << game.errorMessage();
  }
  useSources({{"main", "require('cycle_a') return {}"},
              {"cycle_a", readFixture("modules/cycle_a.lua")},
              {"cycle_b", readFixture("modules/cycle_b.lua")}});
  {
    LuaGame game(arena, frames, sources, random);
    EXPECT_EQ(game.start(), Outcome::ScriptError);
    EXPECT_TRUE(contains(game.errorMessage(), "circular require of 'cycle_a'")) << game.errorMessage();
  }
  useSources({{"main", "require('bin') return {}"}, {"bin", binaryChunk()}});
  {
    LuaGame game(arena, frames, sources, random);
    EXPECT_EQ(game.start(), Outcome::ScriptError);
    EXPECT_TRUE(contains(game.errorMessage(), "attempt to load a binary chunk")) << game.errorMessage();
  }
  // A failed module is forgotten, so requiring it again fails the same way.
  useSources({{"main", std::string("local a, e1 = pcall(require, 'broken')\n"
                                   "local b, e2 = pcall(require, 'broken')\n"
                                   "return { setup = function() return { result = e1 .. ' / ' .. e2 } end,") +
                           DRAW_RESULT},
              {"broken", readFixture("modules/broken.lua")}});
  EXPECT_EQ(startAndDraw(), "broken.lua:2: broken module / broken.lua:2: broken module");
}

TEST_F(SandboxTest, MathRandomIsSeededFromIRandom) {
  useSource("main", std::string("return { setup = function()\n"
                                "  local r = {} for i = 1, 4 do r[i] = math.random(1, 1000000) end\n"
                                "  return { result = table.concat(r, ' ') } end,") +
                        DRAW_RESULT);
  FixedRandom a(1), b(1), c(2);
  const std::string first = startAndDraw(a);
  EXPECT_EQ(first.rfind("error", 0), std::string::npos) << first;
  EXPECT_EQ(startAndDraw(b), first);  // same seed, same sequence
  EXPECT_NE(startAndDraw(c), first);  // another seed, another sequence
}

}  // namespace
