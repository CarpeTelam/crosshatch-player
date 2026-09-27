#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "ArenaAllocator.h"
#include "DisplayList.h"
#include "FrameBuffers.h"
#include "GameInput.h"
#include "GameSources.h"
#include "IRandom.h"
#include "LuaGame.h"

using namespace GameScript;

namespace {

class FixedRandom : public GameCore::IRandom {
 public:
  uint32_t next32() override { return 0x12345678u; }
};

std::string readFixture(const std::string& relative) {
  std::ifstream in(std::string(GAME_SCRIPT_FIXTURES_DIR) + "/" + relative, std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

// Owns everything a LuaGame borrows: a malloc'd arena block (the device uses PSRAM),
// two frame lists, and a one-module source table.
class LuaGameTest : public ::testing::Test {
 protected:
  LuaGameTest()
      : arenaBlock(ARENA_BYTES), front(MAX_BYTES), back(MAX_BYTES), frames(front.data(), back.data(), MAX_BYTES) {
    arena.reset(arenaBlock.data(), arenaBlock.size());
  }

  void useSource(const std::string& moduleName, const std::string& text) {
    source = text;
    std::memset(&span, 0, sizeof(span));
    std::strncpy(span.name, moduleName.c_str(), SourceSpan::MAX_NAME_BYTES);
    span.offset = 0;
    span.length = static_cast<uint32_t>(source.size());
    sources.spans = &span;
    sources.count = 1;
    sources.text = source.data();
  }

  // The front frame's commands after the last publish.
  std::vector<DrawCommand> frontCommands() {
    std::vector<DrawCommand> commands;
    frames.readFront([&](const DisplayList& list) {
      auto reader = list.reader();
      DrawCommand c;
      while (reader.next(c)) commands.push_back(c);
    });
    return commands;
  }

  static bool hasText(const std::vector<DrawCommand>& commands, const std::string& text) {
    for (const auto& c : commands) {
      if (c.op == Op::Text && text == c.text) return true;
    }
    return false;
  }

  std::vector<uint8_t> arenaBlock;
  ArenaAllocator arena;
  std::vector<uint8_t> front;
  std::vector<uint8_t> back;
  FrameBuffers frames;
  FixedRandom random;
  std::string source;
  SourceSpan span{};
  GameSources sources;
};

TEST_F(LuaGameTest, TracerSetupAndDrawThroughTheTrampoline) {
  useSource("main", readFixture("tracer/main.lua"));
  LuaGame game(arena, frames, sources, random);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  EXPECT_GT(arena.bytesInUse(), 0u);  // the VM heap lives in the arena
  ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(frames.frameGen(), 1u);

  const auto commands = frontCommands();
  ASSERT_EQ(commands.size(), 5u);
  EXPECT_EQ(commands[0].op, Op::Clear);
  EXPECT_EQ(commands[0].color, Color::White);
  EXPECT_EQ(commands[1].op, Op::Rect);
  EXPECT_FALSE(commands[1].filled);
  EXPECT_EQ(commands[1].w, 400);
  EXPECT_EQ(commands[2].size, TextSize::Large);
  EXPECT_TRUE(hasText(commands, "Tracer"));
  EXPECT_TRUE(hasText(commands, "Taps: 0"));

  game.close();
  EXPECT_EQ(arena.bytesInUse(), 0u);  // lua_close returned every block
}

TEST_F(LuaGameTest, TapReachesInputAndTheNextDrawShowsIt) {
  useSource("main", readFixture("tracer/main.lua"));
  LuaGame game(arena, frames, sources, random);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.draw(), Outcome::Ok);
  ASSERT_EQ(game.input(InputEvent{InputKind::Tap, 100, 200}), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();

  const auto commands = frontCommands();
  EXPECT_TRUE(hasText(commands, "Taps: 1"));
  const DrawCommand& marker = commands.back();
  EXPECT_EQ(marker.op, Op::Rect);
  EXPECT_TRUE(marker.filled);
  EXPECT_EQ(marker.x, 80);
  EXPECT_EQ(marker.y, 180);
  EXPECT_EQ(marker.w, 40);
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
    LuaGame game(arena, frames, sources, random);
    EXPECT_EQ(game.start(), Outcome::ScriptError) << c.source;
    EXPECT_NE(std::string(game.errorMessage()).find(c.message), std::string::npos)
        << c.source << " -> " << game.errorMessage();
  }
}

TEST_F(LuaGameTest, MissingMainIsAScriptError) {
  useSource("helper", "return {}");
  LuaGame game(arena, frames, sources, random);
  EXPECT_EQ(game.start(), Outcome::ScriptError);
  EXPECT_STREQ(game.errorMessage(), "main.lua not found");
}

TEST_F(LuaGameTest, DrawAndInputErrorsDoNotPublish) {
  useSource("main",
            "return { setup = function() return {} end,\n"
            "  draw = function(s, seat, ui) ch.gfx.clear('white'); if ui.bad then ch.gfx.clear('grey') end end,\n"
            "  input = function(s, seat, ui, ev) if ev.x > 10 then error('bad tap') end; ui.bad = true end }");
  LuaGame game(arena, frames, sources, random);
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
  LuaGame game(arena, frames, sources, random);
  EXPECT_EQ(game.start(), Outcome::ScriptError);
  EXPECT_NE(std::string(game.errorMessage()).find("not enough memory"), std::string::npos) << game.errorMessage();
}

}  // namespace
