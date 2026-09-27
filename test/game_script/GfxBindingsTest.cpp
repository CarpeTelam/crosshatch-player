#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "LuaGameFixture.h"

using namespace GameScript;
using GameScriptTestSupport::DirectGame;
using GameScriptTestSupport::LuaGameTest;

namespace {

const char* colorName(const Color color) {
  switch (color) {
    case Color::White:
      return "white";
    case Color::Light:
      return "light";
    case Color::Dark:
      return "dark";
    case Color::Black:
      return "black";
  }
  return "?";
}

const char* sizeName(const TextSize size) {
  switch (size) {
    case TextSize::Small:
      return "small";
    case TextSize::Medium:
      return "medium";
    case TextSize::Large:
      return "large";
  }
  return "?";
}

const char* alignName(const Align align) {
  switch (align) {
    case Align::Left:
      return "left";
    case Align::Center:
      return "center";
    case Align::Right:
      return "right";
  }
  return "?";
}

// One decoded command as text, every field its kind carries.
std::string describe(const DrawCommand& c) {
  const auto n = [](const int v) { return std::to_string(v); };
  const std::string filled = c.filled ? " filled" : " outline";
  switch (c.op) {
    case Op::Clear:
      return std::string("clear ") + colorName(c.color);
    case Op::Rect:
      return "rect " + n(c.x) + " " + n(c.y) + " " + n(c.w) + " " + n(c.h) + " " + colorName(c.color) + filled;
    case Op::Line:
      return "line " + n(c.x) + " " + n(c.y) + " " + n(c.x2) + " " + n(c.y2) + " " + colorName(c.color);
    case Op::Circle:
      return "circle " + n(c.x) + " " + n(c.y) + " " + n(c.r) + " " + colorName(c.color) + filled;
    case Op::Text:
      return "text " + n(c.x) + " " + n(c.y) + " '" + std::string(c.text, c.textLength) + "' " + sizeName(c.size) +
             " " + colorName(c.color) + " " + alignName(c.align);
  }
  return "?";
}

// A game whose draw runs `body`.
std::string drawing(const std::string& body) {
  return "return { setup = function() return {} end,\n  draw = function(state, seat, ui)\n" + body + "\n  end }";
}

class GfxBindingsTest : public LuaGameTest {
 protected:
  std::vector<std::string> described() {
    std::vector<std::string> lines;
    for (const auto& c : frontCommands()) lines.push_back(describe(c));
    return lines;
  }

  Refresh frontRefresh() {
    Refresh hint = Refresh::Fast;
    frames.readFront([&](const DisplayList& list) { hint = list.refresh(); });
    return hint;
  }
};

TEST_F(GfxBindingsTest, EveryCallAndArgumentFormDecodes) {
  useSource("main", drawing(R"(
    ch.gfx.clear("white"); ch.gfx.clear("light"); ch.gfx.clear("dark"); ch.gfx.clear("black")
    ch.gfx.rect(1, 2, 3, 4, "white", true); ch.gfx.rect(1, 2, 3, 4, "light", true)
    ch.gfx.rect(1, 2, 3, 4, "dark", 1); ch.gfx.rect(1, 2, 3, 4, "black", true)
    ch.gfx.rect(-5, 6, 70000, -8, "black"); ch.gfx.rect(5, 6, 7, 8, "white", false)
    ch.gfx.line(1, 2, 3, 4, "black"); ch.gfx.line(-1, -2, 40000, -40000, "white")
    ch.gfx.circle(10, 20, 5, "white", true); ch.gfx.circle(10, 20, 5, "light", true)
    ch.gfx.circle(10, 20, 5, "dark", true); ch.gfx.circle(10, 20, 5, "black", true)
    ch.gfx.circle(1, 2, -3, "black"); ch.gfx.circle(1, 2, 3, "white", nil)
    ch.gfx.text(1, 2, "a", "small", "black"); ch.gfx.text(1, 2, "b", "medium", "white", "left")
    ch.gfx.text(1, 2, "c", "large", "black", "center"); ch.gfx.text(1, 2, "d", "small", "white", "right")
    ch.gfx.text(3.0, 4, 12, "medium", "black", nil)
    ch.gfx.refresh("half")
  )"));
  DirectGame game(arena, frames, sources, random, canvas);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
  const std::vector<std::string> expected = {
      "clear white",
      "clear light",
      "clear dark",
      "clear black",
      "rect 1 2 3 4 white filled",
      "rect 1 2 3 4 light filled",
      "rect 1 2 3 4 dark filled",
      "rect 1 2 3 4 black filled",
      "rect -5 6 32767 -8 black outline",
      "rect 5 6 7 8 white outline",
      "line 1 2 3 4 black",
      "line -1 -2 32767 -32768 white",
      "circle 10 20 5 white filled",
      "circle 10 20 5 light filled",
      "circle 10 20 5 dark filled",
      "circle 10 20 5 black filled",
      "circle 1 2 -3 black outline",
      "circle 1 2 3 white outline",
      "text 1 2 'a' small black left",
      "text 1 2 'b' medium white left",
      "text 1 2 'c' large black center",
      "text 1 2 'd' small white right",
      "text 3 4 '12' medium black left",
  };
  EXPECT_EQ(described(), expected);
  EXPECT_EQ(frontRefresh(), Refresh::Half);
}

TEST_F(GfxBindingsTest, RefreshRequestsTheLargestModeOfTheFrame) {
  struct Case {
    const char* body;
    Refresh expected;
  };
  const Case cases[] = {
      {"ch.gfx.clear('white')", Refresh::Fast},
      {"ch.gfx.refresh()", Refresh::Fast},
      {"ch.gfx.refresh('fast')", Refresh::Fast},
      {"ch.gfx.refresh('half')", Refresh::Half},
      {"ch.gfx.refresh('full')", Refresh::Full},
      {"ch.gfx.refresh('full'); ch.gfx.refresh('fast'); ch.gfx.refresh('half')", Refresh::Full},
  };
  for (const auto& c : cases) {
    useSource("main", drawing(c.body));
    DirectGame game(arena, frames, sources, random, canvas);
    ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
    ASSERT_EQ(game.draw(), Outcome::Ok) << c.body << ": " << game.errorMessage();
    EXPECT_EQ(frontRefresh(), c.expected) << c.body;
  }
  // The next frame starts at fast again.
  useSource("main", drawing("if ui.again then return end; ui.again = true; ch.gfx.refresh('full')"));
  DirectGame game(arena, frames, sources, random, canvas);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.draw(), Outcome::Ok);
  EXPECT_EQ(frontRefresh(), Refresh::Full);
  ASSERT_EQ(game.draw(), Outcome::Ok);
  EXPECT_EQ(frontRefresh(), Refresh::Fast);
}

TEST_F(GfxBindingsTest, EveryGfxCallOutsideDrawIsAScriptError) {
  const char* const calls[][2] = {
      {"clear", "ch.gfx.clear('white')"},
      {"rect", "ch.gfx.rect(0, 0, 1, 1, 'black')"},
      {"line", "ch.gfx.line(0, 0, 1, 1, 'black')"},
      {"circle", "ch.gfx.circle(0, 0, 1, 'black')"},
      {"text", "ch.gfx.text(0, 0, 'x', 'small', 'black')"},
      {"refresh", "ch.gfx.refresh('full')"},
  };
  for (const auto& call : calls) {
    const std::string name = call[0];
    const std::string code = call[1];
    const std::string message = "ch.gfx." + name + " called outside draw";
    const std::string sourcesFor[] = {
        // At load, in setup, and in input.
        code + "\nreturn { setup = function() return {} end }",
        "return { setup = function() " + code + " return {} end }",
        "return { setup = function() return {} end, draw = function() end,\n"
        "  input = function() " +
            code + " end }",
    };
    for (const auto& source : sourcesFor) {
      useSource("main", source);
      DirectGame game(arena, frames, sources, random, canvas);
      Outcome outcome = game.start();
      if (outcome == Outcome::Ok) outcome = game.input(InputEvent{InputKind::Tap, 1, 1});
      EXPECT_EQ(outcome, Outcome::ScriptError) << source;
      EXPECT_TRUE(contains(game.errorMessage(), message)) << source << " -> " << game.errorMessage();
    }
  }
  EXPECT_EQ(frames.frameGen(), 0u);
}

TEST_F(GfxBindingsTest, GfxInStatusOrApplyIsAScriptError) {
  // status runs right after setup; apply after an accepted tap.
  useSource("main",
            "return { setup = function() return {} end,\n"
            "  status = function() ch.gfx.line(0, 0, 1, 1, 'black') return { turn = 1 } end,\n"
            "  draw = function() end, input = function() end }");
  {
    SessionGame game(*this);
    EXPECT_EQ(game.start(), Outcome::ScriptError);
    EXPECT_TRUE(contains(game.errorMessage(), "ch.gfx.line called outside draw")) << game.errorMessage();
  }
  useSource("main",
            "return { setup = function() return {} end, status = function() return { turn = 1 } end,\n"
            "  apply = function(s) ch.gfx.circle(0, 0, 1, 'black') return s end,\n"
            "  draw = function() end, input = function(s, seat, ui, ev) return { x = ev.x } end }");
  SessionGame game(*this);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(game.tap(1, 1), Outcome::ScriptError);
  EXPECT_TRUE(contains(game.errorMessage(), "ch.gfx.circle called outside draw")) << game.errorMessage();
}

TEST_F(GfxBindingsTest, TheCommandLimitIs2048AndRefreshIsNotACommand) {
  useSource("main", drawing("for i = 1, 2048 do ch.gfx.line(0, 0, i, i, 'black') end\n"
                            "ch.gfx.refresh('full')"));
  DirectGame game(arena, frames, sources, random, canvas);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(frontCommands().size(), MAX_COMMANDS);

  useSource("main", drawing("for i = 1, 2049 do ch.gfx.clear('white') end"));
  DirectGame over(arena, frames, sources, random, canvas);
  ASSERT_EQ(over.start(), Outcome::Ok) << over.errorMessage();
  EXPECT_EQ(over.draw(), Outcome::ScriptError);
  EXPECT_STREQ(over.errorMessage(), "main.lua:3: frame is full (at most 2048 drawing calls or 32768 bytes)");
  EXPECT_EQ(frames.frameGen(), 1u);  // the overflowing frame was not published
}

TEST_F(GfxBindingsTest, TheByteLimitIs32KiB) {
  // One text command takes 10 header bytes, its text, and a NUL: 32,757 characters
  // fill the frame exactly, and one more passes it.
  useSource("main", drawing("ch.gfx.text(0, 0, string.rep('x', 32757), 'small', 'black')"));
  DirectGame exact(arena, frames, sources, random, canvas);
  ASSERT_EQ(exact.start(), Outcome::Ok) << exact.errorMessage();
  ASSERT_EQ(exact.draw(), Outcome::Ok) << exact.errorMessage();
  frames.readFront([](const DisplayList& list) { EXPECT_EQ(list.bytes(), MAX_BYTES); });

  useSource("main", drawing("ch.gfx.text(0, 0, string.rep('x', 32758), 'small', 'black')"));
  DirectGame one(arena, frames, sources, random, canvas);
  ASSERT_EQ(one.start(), Outcome::Ok) << one.errorMessage();
  EXPECT_EQ(one.draw(), Outcome::ScriptError);
  EXPECT_STREQ(one.errorMessage(), "main.lua:3: frame is full (at most 2048 drawing calls or 32768 bytes)");

  // Many small commands reach it too.
  useSource("main", drawing("for i = 1, 400 do ch.gfx.text(0, 0, string.rep('y', 90), 'small', 'black') end"));
  DirectGame many(arena, frames, sources, random, canvas);
  ASSERT_EQ(many.start(), Outcome::Ok) << many.errorMessage();
  EXPECT_EQ(many.draw(), Outcome::ScriptError);
  EXPECT_TRUE(contains(many.errorMessage(), "frame is full")) << many.errorMessage();
  EXPECT_EQ(frames.frameGen(), 1u);
}

TEST_F(GfxBindingsTest, BadArgumentsAreScriptErrors) {
  const char* const cases[][2] = {
      {"ch.gfx.line(0, 0, 1, 1, 'light')", "bad argument #5 to 'line' (\"light\" and \"dark\" are only for fills)"},
      {"ch.gfx.rect(0, 0, 1, 1, 'dark')", "bad argument #5 to 'rect' (\"light\" and \"dark\" are only for fills)"},
      {"ch.gfx.rect(0, 0, 1, 1, 'light', false)", "bad argument #5 to 'rect' (\"light\" and \"dark\""},
      {"ch.gfx.circle(0, 0, 1, 'light')", "bad argument #4 to 'circle' (\"light\" and \"dark\" are only for fills)"},
      {"ch.gfx.text(0, 0, 'x', 'small', 'dark')", "bad argument #5 to 'text' (\"light\" and \"dark\""},
      {"ch.gfx.clear('grey')", "bad argument #1 to 'clear' (invalid option 'grey')"},
      {"ch.gfx.clear()", "bad argument #1 to 'clear' (string expected, got no value)"},
      {"ch.gfx.rect(0, 0, 1, 1, 'Black', true)", "invalid option 'Black'"},
      {"ch.gfx.line(0, 0, 1, 1)", "bad argument #5 to 'line' (string expected, got no value)"},
      {"ch.gfx.circle(0, 0, 1, 'grey', true)", "bad argument #4 to 'circle' (invalid option 'grey')"},
      {"ch.gfx.text(0, 0, 'x', 'huge', 'black')", "bad argument #4 to 'text' (invalid option 'huge')"},
      {"ch.gfx.text(0, 0, 'x', nil, 'black')", "bad argument #4 to 'text' (string expected, got nil)"},
      {"ch.gfx.text(0, 0, 'x', 'small', 'black', 'middle')", "bad argument #6 to 'text' (invalid option 'middle')"},
      {"ch.gfx.text(0, 0, {}, 'small', 'black')", "bad argument #3 to 'text' (string expected, got table)"},
      {"ch.gfx.refresh('slow')", "bad argument #1 to 'refresh' (invalid option 'slow')"},
      {"ch.gfx.rect(0.5, 0, 1, 1, 'black')", "bad argument #1 to 'rect' (number has no integer representation)"},
      {"ch.gfx.circle(0, 0, 'r', 'black')", "bad argument #3 to 'circle' (number expected, got string)"},
  };
  for (const auto& c : cases) {
    useSource("main", drawing(c[0]));
    DirectGame game(arena, frames, sources, random, canvas);
    ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
    EXPECT_EQ(game.draw(), Outcome::ScriptError) << c[0];
    EXPECT_TRUE(contains(game.errorMessage(), c[1])) << c[0] << " -> " << game.errorMessage();
  }
  EXPECT_EQ(frames.frameGen(), 0u);
}

TEST_F(GfxBindingsTest, ScreenIsTheCanvasPassedAtStart) {
  useSource("main",
            "local w, h = ch.screen.w, ch.screen.h\n"
            "return { setup = function() return { size = w .. 'x' .. h } end,\n"
            "  draw = function(s) ch.gfx.text(0, 0, s.size .. ' ' .. math.type(ch.screen.w), 'small', 'black') end }");
  {
    DirectGame game(arena, frames, sources, random, canvas);
    ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
    ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
    EXPECT_EQ(frontText(), "480x800 integer");
  }
  // LuaGame keeps its own copy: the match's canvas is a local of onEnter.
  Canvas small{123, 45, TextMetrics::standIn()};
  DirectGame game(arena, frames, sources, random, small);
  small = Canvas{1, 1, TextMetrics::standIn()};
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(frontText(), "123x45 integer");
}

TEST_F(GfxBindingsTest, TextWidthWorksInInputAndEveryCallback) {
  // The width at load, in setup, in input, and in draw.
  useSource("main",
            "local atLoad = ch.text_width('abc', 'small')\n"
            "return { setup = function() return { atLoad = atLoad, atSetup = ch.text_width('ab', 'medium') } end,\n"
            "  input = function(s, seat, ui, ev) ui.inInput = ch.text_width(ev.x == 1 and 'abcd' or '\\u{e9}', 'large')"
            " end,\n"
            "  draw = function(s, seat, ui)\n"
            "    ch.gfx.text(0, 0, s.atLoad .. ' ' .. s.atSetup .. ' ' .. tostring(ui.inInput) .. ' ' ..\n"
            "      ch.text_width('', 'small') .. ' ' .. math.type(ch.text_width('x', 'small')), 'small', 'black')\n"
            "  end }");
  DirectGame game(arena, frames, sources, random, canvas);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(frontText(), "24 20 nil 0 integer");
  ASSERT_EQ(game.input(InputEvent{InputKind::Tap, 1, 0}), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.draw(), Outcome::Ok);
  EXPECT_EQ(frontText(), "24 20 56 0 integer");
  ASSERT_EQ(game.input(InputEvent{InputKind::Tap, 2, 0}), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.draw(), Outcome::Ok);
  EXPECT_EQ(frontText(), "24 20 14 0 integer");  // "é" is one code point
}

TEST_F(GfxBindingsTest, TextWidthReadsTheTablesPassedAtStart) {
  // A table for the large size: 'A'..'B' advance 9.5 px (rounds to 10) and 3 px;
  // anything else 2 px.
  const uint16_t advances[] = {9 * 16 + 8, 3 * 16};
  const AdvanceRange ranges[] = {{'A', 'B', 0}};
  Canvas custom{480, 800, TextMetrics::standIn()};
  AdvanceTable& large = custom.text.tables[static_cast<size_t>(TextSize::Large)];
  large.ranges = ranges;
  large.rangeCount = 1;
  large.advances = reinterpret_cast<const uint8_t*>(advances);
  large.fallback = 2 * 16;
  useSource("main",
            "return { setup = function() return {} end,\n"
            "  input = function(s, seat, ui) ui.w = ch.text_width('ABAz', 'large') .. ' ' ..\n"
            "    ch.text_width('ABAz', 'small') end,\n"
            "  draw = function(s, seat, ui) ch.gfx.text(0, 0, tostring(ui.w), 'small', 'black') end }");
  DirectGame game(arena, frames, sources, random, custom);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.input(InputEvent{InputKind::Tap, 1, 1}), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
  // Large: 10 + 3 + 10 + 2; small keeps the stand-in's 8 px.
  EXPECT_EQ(frontText(), "25 32");
}

TEST_F(GfxBindingsTest, TextWidthBadArgumentsAreScriptErrors) {
  const char* const cases[][2] = {
      {"ch.text_width('x', 'huge')", "bad argument #2 to 'text_width' (invalid option 'huge')"},
      {"ch.text_width('x')", "bad argument #2 to 'text_width' (string expected, got no value)"},
      {"ch.text_width(nil, 'small')", "bad argument #1 to 'text_width' (string expected, got nil)"},
  };
  for (const auto& c : cases) {
    useSource("main", std::string("return { setup = function() return {} end, draw = function() end,\n"
                                  "  input = function() ") +
                          c[0] + " end }");
    DirectGame game(arena, frames, sources, random, canvas);
    ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
    EXPECT_EQ(game.input(InputEvent{InputKind::Tap, 1, 1}), Outcome::ScriptError) << c[0];
    EXPECT_TRUE(contains(game.errorMessage(), c[1])) << c[0] << " -> " << game.errorMessage();
  }
}

}  // namespace
