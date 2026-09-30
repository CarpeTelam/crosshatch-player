#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "GameInput.h"
#include "LuaGameFixture.h"

using namespace GameScript;
using GameScriptTestSupport::DirectGame;

namespace {

class CallGuardTest : public GameScriptTestSupport::LuaGameTest {
 protected:
  // Starts the game and, if that works, runs one draw and one tap.
  Outcome runAll(DirectGame& game) {
    Outcome outcome = game.start();
    if (outcome == Outcome::Ok) outcome = game.draw();
    if (outcome == Outcome::Ok) outcome = game.input(InputEvent{InputKind::Tap, 10, 10});
    return outcome;
  }
};

TEST_F(CallGuardTest, EveryEndlessLoopEndsOnTheBudget) {
  for (const char* fixture : {"loop_load", "loop_setup", "loop_draw", "loop_input", "loop_in_pcall"}) {
    useFault(fixture);
    DirectGame game(arena, frames, sources, ports, canvas);
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
    DirectGame game(arena, frames, sources, ports, canvas);
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
  DirectGame game(arena, frames, sources, ports, canvas);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(frontText(), "handled main.lua:2: x");
}

TEST_F(CallGuardTest, TheBudgetIsPerCall) {
  // About 1.4 M instructions a draw: under the budget each time, over it in total.
  useSource("main",
            "return { setup = function() return {} end,\n"
            "  draw = function() local x = 0 for i = 1, 700000 do x = x + i end ch.gfx.clear('white') end }");
  DirectGame game(arena, frames, sources, ports, canvas);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  for (int i = 0; i < 3; ++i) EXPECT_EQ(game.draw(), Outcome::Ok) << i << ": " << game.errorMessage();
}

TEST_F(CallGuardTest, ALuaErrorIsAScriptError) {
  useFault("lua_error");
  DirectGame game(arena, frames, sources, ports, canvas);
  EXPECT_EQ(game.start(), Outcome::ScriptError);
  EXPECT_TRUE(contains(game.errorMessage(), "main.lua:2: boom")) << game.errorMessage();
  EXPECT_EQ(game.callGuard().fault(), Fault::None);
}

TEST_F(CallGuardTest, ACancelBeforeACallEndsItAsCancelled) {
  useSource("main", GameScriptTestSupport::readFixture("tracer/main.lua"));
  DirectGame game(arena, frames, sources, ports, canvas);
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
    DirectGame game(arena, frames, sources, ports, canvas);
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

// The loop fixture's "Slow C calls forever" band shows the clean cancel: the watchdog
// cancels it and the hook, which runs at the next call, stops it well inside the
// match's 500 ms join (GameMatchActivity::STOP_TIMEOUT_MS). A device call is far
// slower than a host call, so the bound here is a tenth of the join, on the host.
TEST_F(CallGuardTest, ACancelStopsTheLoopFixturesSlowCallsBandWithinOneSlowCall) {
  for (const int cancelAfterMs : {100, 137, 173}) {
    useSource("main", GameScriptTestSupport::readFixture("loop/main.lua"));
    DirectGame game(arena, frames, sources, ports, canvas);
    ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
    ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
    const DrawCommand* band = nullptr;
    const auto commands = frontCommands();
    for (const auto& c : commands) {
      if (c.op == Op::Text && std::string("Slow C calls forever") == std::string(c.text, c.textLength)) band = &c;
    }
    ASSERT_NE(band, nullptr);

    using Clock = std::chrono::steady_clock;
    std::atomic<Clock::time_point> cancelledAt{Clock::time_point{}};
    std::thread canceller([&game, &cancelledAt, cancelAfterMs] {
      while (!game.inLua()) std::this_thread::yield();
      std::this_thread::sleep_for(std::chrono::milliseconds(cancelAfterMs));
      cancelledAt = Clock::now();
      game.requestCancel();
    });
    const Outcome outcome =
        game.input(InputEvent{InputKind::Tap, static_cast<int16_t>(band->x), static_cast<int16_t>(band->y)});
    const auto returnedAt = Clock::now();
    canceller.join();
    EXPECT_EQ(outcome, Outcome::Cancelled) << game.errorMessage();
    EXPECT_EQ(game.callGuard().fault(), Fault::Cancelled);  // not the budget: the loop's calls are few instructions
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(returnedAt - cancelledAt.load()).count(), 50)
        << "cancel after " << cancelAfterMs << " ms";
  }
}

TEST_F(CallGuardTest, NestedPcallStopsOnStackHeadroom) {
  useFault("nested_pcall");
  {
    // Without a floor, Lua's own C-stack error is caught by the script's pcall
    // and the script carries on (on the host's large stack).
    DirectGame game(arena, frames, sources, ports, canvas);
    EXPECT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  }
  DirectGame game(arena, frames, sources, ports, canvas);
  modelTaskStack(game);
  EXPECT_EQ(game.start(), Outcome::ScriptError);
  EXPECT_STREQ(game.errorMessage(), "script recursion too deep (C stack nearly full)");
  EXPECT_EQ(game.callGuard().fault(), Fault::Stack);
  EXPECT_GE(game.callGuard().deepestAddress(), game.callGuard().stackFloor());
}

TEST_F(CallGuardTest, RecursiveIndexEndsInAScriptError) {
  // A level costs about 256 B here, so LUAI_MAXCCALLS (30) stops it before the
  // guard would; with or without a floor it is a ScriptError, never a crash.
  useFault("recursive_index");
  for (const bool floor : {false, true}) {
    DirectGame game(arena, frames, sources, ports, canvas);
    if (floor) modelTaskStack(game);
    EXPECT_EQ(game.start(), Outcome::ScriptError);
    EXPECT_TRUE(contains(game.errorMessage(), "C stack overflow") ||
                contains(game.errorMessage(), "script recursion too deep"))
        << game.errorMessage();
  }
}

TEST_F(CallGuardTest, ParserAndPatternRecursionEndInScriptErrors) {
  // Neither runs a hook; LUAI_MAXCCALLS and MAXCCALLS (lib/lua/library.json) bound them.
  struct Case {
    const char* fixture;
    const char* message;
  };
  const Case cases[] = {
      {"deep_parens", "C stack overflow"},
      {"deep_pattern", "pattern too complex"},
  };
  for (const auto& c : cases) {
    useFault(c.fixture);
    DirectGame game(arena, frames, sources, ports, canvas);
    modelTaskStack(game);
    EXPECT_EQ(game.start(), Outcome::ScriptError) << c.fixture;
    EXPECT_TRUE(contains(game.errorMessage(), c.message)) << c.fixture << " -> " << game.errorMessage();
  }
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
    const int levels[2] = {5, 15};  // inside LUAI_MAXCCALLS (30)
    for (int i = 0; i < 2; ++i) {
      useSource("main", "LEVELS = " + std::to_string(levels[i]) + "\n" + body);
      DirectGame game(arena, frames, sources, ports, canvas);
      ASSERT_EQ(game.start(), Outcome::Ok) << c.name << ": " << game.errorMessage();
      deepest[i] = game.callGuard().deepestAddress();
    }
    const double perLevel = static_cast<double>(deepest[0] - deepest[1]) / (levels[1] - levels[0]);

    // Unbounded (-1 never reaches 0), in a modelled task stack: the depth reached,
    // and which limit stopped it (the guard, or LUAI_MAXCCALLS as Lua's own error).
    useSource("main", "LEVELS = -1\n" + body);
    DirectGame game(arena, frames, sources, ports, canvas);
    modelTaskStack(game);
    const Outcome outcome = game.start();
    const bool byGuard = game.callGuard().fault() == Fault::Stack;
    const std::string message = game.errorMessage();
    ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
    const int depth = std::stoi(frontText());

    std::cout << "[stack] " << c.name << ": " << perLevel << " B a level on this host; stopped at depth " << depth
              << " by " << (byGuard ? "the headroom guard" : "LUAI_MAXCCALLS") << " (" << message << ")\n";
    RecordProperty(std::string(c.key) + "_bytes_per_level", static_cast<int>(perLevel));
    RecordProperty(std::string(c.key) + "_depth_in_16k", depth);
    EXPECT_GT(perLevel, 64.0) << c.name;
    EXPECT_GT(depth, 4) << c.name;
    EXPECT_LE(depth, 30) << c.name;
    if (std::string(c.key) == "nested_pcall") {
      // Lua's own error would be caught by the script's pcall; only the sticky guard ends it.
      EXPECT_EQ(outcome, Outcome::ScriptError);
      EXPECT_TRUE(byGuard) << message;
    }
  }
}

TEST_F(CallGuardTest, ASandboxedTracerRunsWellInsideTheModelledStack) {
  useSource("main", GameScriptTestSupport::readFixture("tracer/main.lua"));
  DirectGame game(arena, frames, sources, ports, canvas);
  modelTaskStack(game);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.input(InputEvent{InputKind::Tap, 100, 200}), Outcome::Ok) << game.errorMessage();
  const uintptr_t headroom = game.callGuard().deepestAddress() - game.callGuard().stackFloor();
  std::cout << "[stack] tracer: least headroom at a hook " << headroom << " B of " << VM_STACK_BYTES << "\n";
  EXPECT_GT(headroom, CallGuard::STACK_HEADROOM_BYTES);
}

}  // namespace
