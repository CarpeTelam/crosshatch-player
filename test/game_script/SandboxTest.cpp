#include <gtest/gtest.h>

#include <atomic>
#include <climits>
#include <lua.hpp>
#include <string>

#include "ChBindings.h"
#include "LuaGameFixture.h"

using namespace GameScript;
using GameScriptTestSupport::DirectGame;
using GameScriptTestSupport::FixedRandom;
using GameScriptTestSupport::readFixture;

namespace {

class SandboxTest : public GameScriptTestSupport::LuaGameTest {
 protected:
  // Runs setup and one draw; returns the drawn text, or the error prefixed "error: ".
  std::string startAndDraw(GameCore::IRandom& rng) {
    const HostPorts withRng{rng, clock, log, store};
    DirectGame game(arena, frames, sources, withRng, canvas);
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
  // tables, ch, require, and print (which is ch.log).
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
    DirectGame game(arena, frames, sources, ports, canvas);
    EXPECT_EQ(game.start(), Outcome::ScriptError) << c.fixture;
    EXPECT_TRUE(contains(game.errorMessage(), c.message)) << c.fixture << " -> " << game.errorMessage();
  }
  for (const char* name : {"loadfile", "dofile", "debug", "coroutine", "package"}) {
    useSource("main",
              std::string("return { setup = function() return { result = type(") + name + ") } end," + DRAW_RESULT);
    EXPECT_EQ(startAndDraw(), "nil") << name;
  }
}

TEST_F(SandboxTest, TableLoopsPastTheLimitAreRefused) {
  for (const char* fixture : {"table_move", "table_insert_len"}) {
    useFault(fixture);
    DirectGame game(arena, frames, sources, ports, canvas);
    EXPECT_EQ(game.start(), Outcome::ScriptError) << fixture;
    EXPECT_TRUE(contains(game.errorMessage(), "more than 65536 elements")) << fixture << " -> " << game.errorMessage();
  }
  // At the limit, and ordinary uses, still work.
  useSource("main", std::string("return { setup = function()\n"
                                "  local a = table.move({}, 1, 65536, 1)\n"
                                "  local t = {1, 2, 3} table.insert(t, 2, 9) table.remove(t, 1)\n"
                                "  table.insert(t, 7)\n"
                                "  return { result = #a .. ' ' .. table.concat(t, ',') } end,") +
                        DRAW_RESULT);
  EXPECT_EQ(startAndDraw(), "0 9,2,3,7");
}

TEST_F(SandboxTest, TableLimitsStopTheGameEvenUnderPcall) {
  struct Case {
    const char* call;
    const char* message;
  };
  const Case cases[] = {
      {"pcall(table.move, {}, 1, 70000, 1)", "table.move: more than 65536 elements"},
      {"pcall(table.insert, setmetatable({}, { __len = function() return 70000 end }), 1)",
       "table.insert: more than 65536 elements"},
      {"pcall(function() table.remove(setmetatable({}, { __len = function() return 70000 end })) end)",
       "table.remove: more than 65536 elements"},
  };
  for (const auto& c : cases) {
    useSource("main", std::string("return { setup = function() ") + c.call + " return { result = 'survived' } end," +
                          DRAW_RESULT);
    DirectGame game(arena, frames, sources, ports, canvas);
    EXPECT_EQ(game.start(), Outcome::ScriptError) << c.call;
    EXPECT_TRUE(contains(game.errorMessage(), c.message)) << c.call << " -> " << game.errorMessage();
    EXPECT_EQ(game.callGuard().fault(), Fault::Binding) << c.call;
  }
}

TEST_F(SandboxTest, SetmetatableRefusesGcFinalizers) {
  for (const char* fixture : {"gc_recursive", "gc_loop"}) {
    useFault(fixture);
    DirectGame game(arena, frames, sources, ports, canvas);
    modelTaskStack(game);
    EXPECT_EQ(game.start(), Outcome::ScriptError) << fixture;
    EXPECT_TRUE(contains(game.errorMessage(), "setmetatable: __gc metamethods are not supported"))
        << fixture << " -> " << game.errorMessage();
  }
  // __close too (a raising __close could outlive the heap cap), even as false.
  for (const char* value : {"function() end", "false"}) {
    useSource("main", std::string("return { setup = function() setmetatable({}, { __close = ") + value +
                          " }) return {} end," + DRAW_RESULT);
    DirectGame game(arena, frames, sources, ports, canvas);
    EXPECT_EQ(game.start(), Outcome::ScriptError) << value;
    EXPECT_TRUE(contains(game.errorMessage(), "main.lua:1: setmetatable: __close metamethods are not supported"))
        << value << " -> " << game.errorMessage();
  }
  // Everything else setmetatable does is unchanged.
  useSource("main", std::string("return { setup = function()\n"
                                "  local t = setmetatable({}, { __index = function() return 5 end })\n"
                                "  local ok1, e1 = pcall(setmetatable, 1, {})\n"
                                "  local p = setmetatable({}, { __metatable = 'locked' })\n"
                                "  local ok2, e2 = pcall(setmetatable, p, {})\n"
                                "  return { result = t.x .. ' | ' .. e1 .. ' | ' .. e2 } end,") +
                        DRAW_RESULT);
  EXPECT_EQ(startAndDraw(),
            "5 | bad argument #1 to 'setmetatable' (table expected, got number) | cannot change a protected metatable");
}

TEST_F(SandboxTest, PrintIsChLogNeverStdout) {
  useSource(
      "main",
      std::string("return { setup = function() print('hello', 1) return { result = tostring(print == ch.log) } end,") +
          DRAW_RESULT);
  testing::internal::CaptureStdout();
  const std::string drawn = startAndDraw();
  EXPECT_EQ(testing::internal::GetCapturedStdout(), "");
  EXPECT_EQ(drawn, "true");
  EXPECT_EQ(log.lines, std::vector<std::string>{"hello\t1"});
}

TEST_F(SandboxTest, RequireNeedsParserHeadroom) {
  // Top-level requires fit the modelled task stack...
  useSources({{"main", readFixture("modules/main.lua")},
              {"util", readFixture("modules/util.lua")},
              {"quiet", readFixture("modules/quiet.lua")}});
  {
    DirectGame game(arena, frames, sources, ports, canvas);
    modelTaskStack(game);
    EXPECT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  }
  // ...but not from deep in a recursion, where the parser could overrun the stack.
  // The refusal is a guard fault, so a script that swallows it with pcall still stops.
  for (const char* onError : {"error(v, 0)", "return 'caught'"}) {
    useSources({{"main", std::string("local function f(n)\n"
                                     "  if n == 0 then return require('util') end\n"
                                     "  local ok, v = pcall(f, n - 1)\n"
                                     "  if not ok then ") +
                             onError +
                             " end\n"
                             "  return v\n"
                             "end\n"
                             "f(12)\n"
                             "return {}"},
                {"util", readFixture("modules/util.lua")}});
    DirectGame game(arena, frames, sources, ports, canvas);
    modelTaskStack(game);
    EXPECT_EQ(game.start(), Outcome::ScriptError) << onError;
    EXPECT_TRUE(contains(game.errorMessage(), "script recursion too deep to load a module"))
        << onError << " -> " << game.errorMessage();
    EXPECT_EQ(game.callGuard().fault(), Fault::Binding) << onError;
  }
}

TEST_F(SandboxTest, LockedSectionsAreCountedForAbandon) {
  // A binding's locked section is visible to GameVM::abandon through the context.
  std::atomic<uint32_t> sections{0};
  BindingContext context;
  context.lockedSections = &sections;
  lua_State* L = luaL_newstate();
  setBindingContext(L, &context);
  enterLockedSection(L);
  enterLockedSection(L);
  EXPECT_EQ(sections.load(), 2u);
  leaveLockedSection(L);
  leaveLockedSection(L);
  EXPECT_EQ(sections.load(), 0u);
  lua_close(L);

  useSource("main", readFixture("tracer/main.lua"));
  DirectGame game(arena, frames, sources, ports, canvas);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  EXPECT_FALSE(game.inLockedBinding());
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
    DirectGame game(arena, frames, sources, ports, canvas);
    EXPECT_EQ(game.start(), Outcome::ScriptError);
    EXPECT_TRUE(contains(game.errorMessage(), "main.lua:2: module 'nothere' not found")) << game.errorMessage();
  }
  useSources({{"main", "require('cycle_a') return {}"},
              {"cycle_a", readFixture("modules/cycle_a.lua")},
              {"cycle_b", readFixture("modules/cycle_b.lua")}});
  {
    DirectGame game(arena, frames, sources, ports, canvas);
    EXPECT_EQ(game.start(), Outcome::ScriptError);
    EXPECT_TRUE(contains(game.errorMessage(), "circular require of 'cycle_a'")) << game.errorMessage();
  }
  useSources({{"main", "require('bin') return {}"}, {"bin", binaryChunk()}});
  {
    DirectGame game(arena, frames, sources, ports, canvas);
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
