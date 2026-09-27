#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "GameInput.h"
#include "LuaGameFixture.h"

using namespace GameScript;
using GameScriptTestSupport::DirectGame;
using GameScriptTestSupport::LuaGameTest;
using GameScriptTestSupport::readFixture;

namespace {

TEST_F(LuaGameTest, TracerSetupAndDrawThroughTheTrampoline) {
  useSource("main", readFixture("tracer/main.lua"));
  DirectGame game(arena, frames, sources, ports, canvas);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  EXPECT_GT(arena.bytesInUse(), 0u);    // the VM heap lives in the arena
  EXPECT_FALSE(game.snapshot.empty());  // setup's state, encoded
  ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(frames.frameGen(), 1u);

  const auto commands = frontCommands();
  ASSERT_EQ(commands.size(), 6u);
  EXPECT_EQ(commands[0].op, Op::Clear);
  EXPECT_EQ(commands[0].color, Color::White);
  EXPECT_EQ(commands[1].op, Op::Rect);
  EXPECT_FALSE(commands[1].filled);
  EXPECT_EQ(commands[1].w, 400);
  EXPECT_EQ(commands[2].size, TextSize::Large);
  EXPECT_TRUE(hasText(commands, "Tracer"));
  EXPECT_TRUE(hasText(commands, "Taps: 0 of 5"));
  EXPECT_TRUE(hasText(commands, "solo, 1 seat, api 1"));

  game.close();
  EXPECT_EQ(arena.bytesInUse(), 0u);  // lua_close and the scratch returned every block
}

TEST_F(LuaGameTest, ATapComesBackAsAMoveInCodecBytes) {
  useSource("main", readFixture("tracer/main.lua"));
  DirectGame game(arena, frames, sources, ports, canvas);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.input(InputEvent{InputKind::Tap, 100, 200}), Outcome::Ok) << game.errorMessage();
  // {x = 100, y = 200}: table, narr 0, nrec 2, "x" = int zigzag 200, "y" = int zigzag 400.
  const std::vector<uint8_t> expected = {0x06, 0x00, 0x02, 0x05, 0x01, 'x',  0x03, 0xC8,
                                         0x01, 0x05, 0x01, 'y',  0x03, 0x90, 0x03};
  EXPECT_EQ(game.lastMove, expected);
  ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
  EXPECT_TRUE(hasText(frontCommands(), "Taps: 0 of 5"));  // no Session, so nothing applied it
}

TEST_F(LuaGameTest, EachEntryIntoLuaBumpsTheCallSerial) {
  // GameVM's watchdog times each call on its own by watching this counter.
  useSource("main", readFixture("tracer/main.lua"));
  DirectGame game(arena, frames, sources, ports, canvas);
  EXPECT_EQ(game.callSerial(), 0u);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();  // load, setup
  EXPECT_EQ(game.callSerial(), 2u);
  ASSERT_EQ(game.draw(), Outcome::Ok);
  ASSERT_EQ(game.input(InputEvent{InputKind::Tap, 1, 1}), Outcome::Ok);
  EXPECT_EQ(game.callSerial(), 4u);
  game.close();
  EXPECT_EQ(game.callSerial(), 5u);  // lua_close runs __gc, so it is timed too
}

TEST_F(LuaGameTest, ErrorsBecomeScriptErrorsWithTheirMessage) {
  struct Case {
    const char* source;
    const char* message;
  };
  const Case cases[] = {
      {"return { setup = function() error('boom') end }", "main.lua:1: boom"},
      {"return 5", "main.lua must return a table, not a number"},
      {"return {}", "game.setup is not a function"},
      {"return { setup = function() ch.gfx.clear('white') end }", "ch.gfx.clear called outside draw"},
      {"return { setup = function() error({}) end }", "(error object is a table value)"},
      {"return { setup = function() error(42) end }", "42"},
      {"this is not lua", "main.lua:1:"},
      {"\x1bLua", "attempt to load a binary chunk"},
  };
  for (const auto& c : cases) {
    useSource("main", c.source);
    DirectGame game(arena, frames, sources, ports, canvas);
    EXPECT_EQ(game.start(), Outcome::ScriptError) << c.source;
    EXPECT_NE(std::string(game.errorMessage()).find(c.message), std::string::npos)
        << c.source << " -> " << game.errorMessage();
  }
}

TEST_F(LuaGameTest, MissingMainIsAScriptError) {
  useSource("helper", "return {}");
  DirectGame game(arena, frames, sources, ports, canvas);
  EXPECT_EQ(game.start(), Outcome::ScriptError);
  EXPECT_STREQ(game.errorMessage(), "main.lua not found");
}

TEST_F(LuaGameTest, DrawAndInputErrorsDoNotPublish) {
  useSource("main",
            "return { setup = function() return {} end,\n"
            "  draw = function(s, seat, ui) ch.gfx.clear('white'); if ui.bad then ch.gfx.clear('grey') end end,\n"
            "  input = function(s, seat, ui, ev) if ev.x > 10 then error('bad tap') end; ui.bad = true end }");
  DirectGame game(arena, frames, sources, ports, canvas);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.draw(), Outcome::Ok);
  EXPECT_EQ(game.input(InputEvent{InputKind::Tap, 50, 0}), Outcome::ScriptError);
  EXPECT_NE(std::string(game.errorMessage()).find("main.lua:3: bad tap"), std::string::npos) << game.errorMessage();
  ASSERT_EQ(game.input(InputEvent{InputKind::Tap, 5, 0}), Outcome::Ok);
  EXPECT_EQ(game.draw(), Outcome::ScriptError);  // 'grey' is not a color
  EXPECT_NE(std::string(game.errorMessage()).find("invalid option 'grey'"), std::string::npos) << game.errorMessage();
  EXPECT_EQ(frames.frameGen(), 1u);  // the failed draw published nothing
}

TEST_F(LuaGameTest, HeapExhaustionIsAScriptError) {
  // A small arena stands in for the 256 KiB cap.
  arena.reset(arenaBlock.data(), 48 * 1024);
  useSource("main", "local t = {} for i = 1, 1e7 do t[i] = tostring(i) end return {}");
  DirectGame game(arena, frames, sources, ports, canvas);
  EXPECT_EQ(game.start(), Outcome::ScriptError);
  EXPECT_NE(std::string(game.errorMessage()).find("not enough memory"), std::string::npos) << game.errorMessage();
}

TEST_F(LuaGameTest, TheHeapCapStopsAHeapBombInTheFullArena) {
  useFault("heap");
  {
    // The default cap on the full 256 KiB arena.
    DirectGame game(arena, frames, sources, ports, canvas);
    EXPECT_EQ(game.start(), Outcome::ScriptError);
    EXPECT_TRUE(contains(game.errorMessage(), "not enough memory")) << game.errorMessage();
    EXPECT_LE(arena.luaBytes(), LUA_HEAP_BYTES);
    game.close();
    EXPECT_EQ(arena.luaBytes(), 0u);
    EXPECT_EQ(arena.bytesInUse(), 0u);
  }
  // A lower cap stops it while the arena still has room: the counter is the cap.
  arena.reset(arenaBlock.data(), arenaBlock.size());
  arena.setLuaLimit(64 * 1024);
  DirectGame game(arena, frames, sources, ports, canvas);
  EXPECT_EQ(game.start(), Outcome::ScriptError);
  EXPECT_TRUE(contains(game.errorMessage(), "not enough memory")) << game.errorMessage();
  EXPECT_LE(arena.peakBytes(), arena.capacity() / 2);
}

TEST_F(LuaGameTest, AbandonForgetsTheStateWithoutClosingIt) {
  useSource("main", readFixture("tracer/main.lua"));
  DirectGame game(arena, frames, sources, ports, canvas);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  const size_t held = arena.bytesInUse();
  game.abandon();
  EXPECT_FALSE(game.started());
  EXPECT_EQ(arena.bytesInUse(), held);  // nothing freed: the owner drops the arena whole
  EXPECT_EQ(game.draw(), Outcome::ScriptError);
  EXPECT_STREQ(game.errorMessage(), "game not started");
}

}  // namespace
