#include <gtest/gtest.h>

#include <atomic>
#include <iostream>
#include <string>
#include <thread>

#include "GameInput.h"
#include "LuaGameFixture.h"

using namespace GameScript;

namespace {

class CallGuardTest : public GameScriptTestSupport::LuaGameTest {
 protected:
  // Starts the game and, if that works, runs one draw and one tap.
  Outcome runAll(LuaGame& game) {
    Outcome outcome = game.start();
    if (outcome == Outcome::Ok) outcome = game.draw();
    if (outcome == Outcome::Ok) outcome = game.input(InputEvent{InputKind::Tap, 10, 10});
    return outcome;
  }
};

TEST_F(CallGuardTest, EveryEndlessLoopEndsOnTheBudget) {
  for (const char* fixture : {"loop_load", "loop_setup", "loop_draw", "loop_input", "loop_in_pcall"}) {
    useFault(fixture);
    LuaGame game(arena, frames, sources, random);
    EXPECT_EQ(runAll(game), Outcome::ScriptError) << fixture;
    // The message names the line the budget ran out on.
    EXPECT_TRUE(contains(game.errorMessage(), "main.lua:")) << fixture << " -> " << game.errorMessage();
    EXPECT_TRUE(contains(game.errorMessage(), "instruction budget exceeded"))
        << fixture << " -> " << game.errorMessage();
    EXPECT_EQ(game.callGuard().fault(), Fault::Budget) << fixture;
  }
}

TEST_F(CallGuardTest, AnXpcallHandlerCannotOutliveAGuardFault) {
  // Lua calls a message handler at the error point; for an error raised from the
  // hook, hooks are off there, so the handler would run unguarded.
  for (const char* fixture : {"loop_in_xpcall_handler", "recurse_in_xpcall_handler"}) {
    useFault(fixture);
    LuaGame game(arena, frames, sources, random);
    modelTaskStack(game);
    EXPECT_EQ(game.start(), Outcome::ScriptError) << fixture;
    EXPECT_EQ(game.callGuard().fault(), Fault::Budget) << fixture << " -> " << game.errorMessage();
  }
  // A plain Lua error still reaches the handler, at the error point.
  useSource("main",
            std::string("return { setup = function()\n"
                        "  local ok, e = xpcall(function() error('x') end, function(m) return 'handled ' .. m end)\n"
                        "  return { result = e } end,\n"
                        "  draw = function(s) ch.gfx.text(0, 0, s.result, 'small', 'black') end }"));
  LuaGame game(arena, frames, sources, random);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(frontText(), "handled main.lua:2: x");
}

TEST_F(CallGuardTest, TheBudgetIsPerCall) {
  // About 1.4 M instructions a draw: under the budget each time, over it in total.
  useSource("main",
            "return { setup = function() return {} end,\n"
            "  draw = function() local x = 0 for i = 1, 700000 do x = x + i end ch.gfx.clear('white') end }");
  LuaGame game(arena, frames, sources, random);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  for (int i = 0; i < 3; ++i) EXPECT_EQ(game.draw(), Outcome::Ok) << i << ": " << game.errorMessage();
}

TEST_F(CallGuardTest, ALuaErrorIsAScriptError) {
  useFault("lua_error");
  LuaGame game(arena, frames, sources, random);
  EXPECT_EQ(game.start(), Outcome::ScriptError);
  EXPECT_TRUE(contains(game.errorMessage(), "main.lua:2: boom")) << game.errorMessage();
  EXPECT_EQ(game.callGuard().fault(), Fault::None);
}

TEST_F(CallGuardTest, ACancelBeforeACallEndsItAsCancelled) {
  useSource("main", GameScriptTestSupport::readFixture("tracer/main.lua"));
  LuaGame game(arena, frames, sources, random);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  game.requestCancel();
  EXPECT_EQ(game.draw(), Outcome::Cancelled);
  EXPECT_STREQ(game.errorMessage(), "cancelled");
  EXPECT_EQ(game.input(InputEvent{}), Outcome::Cancelled);  // it stays cancelled
  EXPECT_EQ(frames.frameGen(), 0u);
}

TEST_F(CallGuardTest, ACancelFromAnotherTaskStopsARunningLoopEvenUnderPcall) {
  for (const char* fixture : {"loop_draw", "loop_in_pcall"}) {
    useFault(fixture);
    LuaGame game(arena, frames, sources, random);
    // Both loop in draw, so the cancel lands while the loop runs.
    ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
    std::thread canceller([&game] {
      while (!game.inLua()) std::this_thread::yield();
      game.requestCancel();
    });
    const Outcome outcome = game.draw();
    canceller.join();
    EXPECT_EQ(outcome, Outcome::Cancelled) << fixture << " -> " << game.errorMessage();
    EXPECT_FALSE(game.inLua());
  }
}

TEST_F(CallGuardTest, NestedPcallStopsOnStackHeadroom) {
  useFault("nested_pcall");
  {
    // Without a floor, Lua's own C-stack error is caught by the script's pcall
    // and the script carries on (on the host's large stack).
    LuaGame game(arena, frames, sources, random);
    EXPECT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  }
  LuaGame game(arena, frames, sources, random);
  modelTaskStack(game);
  EXPECT_EQ(game.start(), Outcome::ScriptError);
  EXPECT_STREQ(game.errorMessage(), "script recursion too deep (C stack nearly full)");
  EXPECT_EQ(game.callGuard().fault(), Fault::Stack);
  EXPECT_GE(game.callGuard().deepestAddress(), game.callGuard().stackFloor());
}

TEST_F(CallGuardTest, RecursiveIndexStopsOnStackHeadroom) {
  useFault("recursive_index");
  {
    LuaGame game(arena, frames, sources, random);
    EXPECT_EQ(game.start(), Outcome::ScriptError);
    EXPECT_TRUE(contains(game.errorMessage(), "C stack overflow")) << game.errorMessage();  // Lua's limit
  }
  LuaGame game(arena, frames, sources, random);
  modelTaskStack(game);
  EXPECT_EQ(game.start(), Outcome::ScriptError);
  EXPECT_STREQ(game.errorMessage(), "script recursion too deep (C stack nearly full)");
}

// Measures the C stack a level of each recursion costs on this host, and how deep
// each gets before the guard stops it in a modelled 16 KiB task stack.
TEST_F(CallGuardTest, MeasuresStackCostPerLevel) {
  struct Case {
    const char* name;
    const char* key;
    const char* recurse;  // defines f(n), recursing n levels through C
  };
  const Case cases[] = {
      {"nested pcall", "nested_pcall", "local function f(n) DEPTH = DEPTH + 1 if n ~= 0 then pcall(f, n - 1) end end"},
      {"recursive __index", "recursive_index",
       "local t = setmetatable({}, { __index = function(t, n) DEPTH = DEPTH + 1 if n ~= 0 then return t[n - 1] end end "
       "})\n"
       "local function f(n) return t[n] end"},
  };
  for (const auto& c : cases) {
    const std::string body = std::string("DEPTH = 0\n") + c.recurse +
                             "\nreturn { setup = function() f(LEVELS) return {} end,\n"
                             "  draw = function() ch.gfx.text(0, 0, tostring(DEPTH), 'small', 'black') end }";
    uintptr_t deepest[2] = {};
    const int levels[2] = {10, 30};
    for (int i = 0; i < 2; ++i) {
      useSource("main", "LEVELS = " + std::to_string(levels[i]) + "\n" + body);
      LuaGame game(arena, frames, sources, random);
      ASSERT_EQ(game.start(), Outcome::Ok) << c.name << ": " << game.errorMessage();
      deepest[i] = game.callGuard().deepestAddress();
    }
    const double perLevel = static_cast<double>(deepest[0] - deepest[1]) / (levels[1] - levels[0]);

    // Unbounded (-1 never reaches 0), in a modelled task stack: the depth reached.
    useSource("main", "LEVELS = -1\n" + body);
    LuaGame game(arena, frames, sources, random);
    modelTaskStack(game);
    EXPECT_EQ(game.start(), Outcome::ScriptError) << c.name;
    EXPECT_EQ(game.callGuard().fault(), Fault::Stack) << c.name << ": " << game.errorMessage();
    ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
    const int depth = std::stoi(frontText());

    std::cout << "[stack] " << c.name << ": " << perLevel << " B a level on this host; the guard stopped it at depth "
              << depth << " of Lua's 200 in a " << VM_STACK_BYTES << " B stack\n";
    RecordProperty(std::string(c.key) + "_bytes_per_level", static_cast<int>(perLevel));
    RecordProperty(std::string(c.key) + "_depth_in_16k", depth);
    EXPECT_GT(perLevel, 64.0) << c.name;
    EXPECT_GT(depth, 4) << c.name;
    EXPECT_LT(depth, 200) << c.name;  // the guard, not Lua's limit, stopped it
  }
}

TEST_F(CallGuardTest, ASandboxedTracerRunsWellInsideTheModelledStack) {
  useSource("main", GameScriptTestSupport::readFixture("tracer/main.lua"));
  LuaGame game(arena, frames, sources, random);
  modelTaskStack(game);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.input(InputEvent{InputKind::Tap, 100, 200}), Outcome::Ok) << game.errorMessage();
  const uintptr_t headroom = game.callGuard().deepestAddress() - game.callGuard().stackFloor();
  std::cout << "[stack] tracer: least headroom at a hook " << headroom << " B of " << VM_STACK_BYTES << "\n";
  EXPECT_GT(headroom, CallGuard::STACK_HEADROOM_BYTES);
}

}  // namespace
