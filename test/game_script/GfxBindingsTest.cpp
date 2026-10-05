#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "GameIcons.h"
#include "LuaGameFixture.h"
#include "ReplayFills.h"

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

const char* weightName(const IconWeight weight) {
  switch (weight) {
    case IconWeight::Regular:
      return "regular";
    case IconWeight::Fill:
      return "fill";
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
    case Op::Icon:
      return std::string("icon ") + (c.icon < GameIcons::ICON_COUNT ? GameIcons::ICONS[c.icon].name : "?") + " " +
             n(c.x) + " " + n(c.y) + " " + sizeName(c.size) + " " + colorName(c.color) + " " + weightName(c.weight);
    case Op::Image:
      return "image " + n(c.image) + " " + n(c.x) + " " + n(c.y) + " " + colorName(c.color);
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

  // The front frame's icon and image budget charged.
  uint32_t frontBlitPixels() {
    uint32_t pixels = 0;
    frames.readFront([&](const DisplayList& list) { pixels = list.blitPixels(); });
    return pixels;
  }

  // One image, "checker": a 480 x 800 checkerboard, whose every pixel is a run of
  // its own, so its replay fills each covered pixel on its own (the worst case).
  void useChecker() {
    constexpr uint32_t W = 480;
    constexpr uint32_t H = 800;
    constexpr uint32_t ROW_BYTES = W / 8;  // already a multiple of 4
    imageSpans.assign(1, GameCore::ImageSpan{});
    std::strncpy(imageSpans[0].name, "checker", GameCore::IMAGE_NAME_BYTES);
    imageSpans[0].width = W;
    imageSpans[0].height = H;
    imageSpans[0].rowBytes = ROW_BYTES;
    imageSpans[0].offset = 0;
    imagePixels.assign(static_cast<size_t>(ROW_BYTES) * H, 0);
    for (uint32_t y = 0; y < H; ++y) {
      std::memset(imagePixels.data() + static_cast<size_t>(y) * ROW_BYTES, (y & 1) ? 0xAA : 0x55, ROW_BYTES);
    }
    images.spans = imageSpans.data();
    images.count = imageSpans.size();
    images.pixels = imagePixels.data();
  }

  // One image, "gray": 480 x 800 as the installer's converter writes a mid-gray PNG (an ordered dither, so every
  // other pixel is white); only its size and dither matter to the timing fixture's frames.
  void useGray() {
    constexpr uint32_t W = 480;
    constexpr uint32_t H = 800;
    constexpr uint32_t ROW_BYTES = W / 8;
    imageSpans.assign(1, GameCore::ImageSpan{});
    std::strncpy(imageSpans[0].name, "gray", GameCore::IMAGE_NAME_BYTES);
    imageSpans[0].width = W;
    imageSpans[0].height = H;
    imageSpans[0].rowBytes = ROW_BYTES;
    imageSpans[0].offset = 0;
    imagePixels.assign(static_cast<size_t>(ROW_BYTES) * H, 0);
    for (uint32_t y = 0; y < H; ++y) {
      std::memset(imagePixels.data() + static_cast<size_t>(y) * ROW_BYTES, (y & 1) ? 0xAA : 0x55, ROW_BYTES);
    }
    images.spans = imageSpans.data();
    images.count = imageSpans.size();
    images.pixels = imagePixels.data();
  }

  // The fills the real FrameReplay::draw makes for the front frame, counted on the src/games
  // harness's recording renderer (test/game_script/harness/ReplayFills.h): each fillRect it
  // asks for, which only icons and images make. A frame drawn with an error logged (an icon
  // or image it could not draw) is a failure, not a cheap frame.
  uint64_t frontBlitFills() {
    harness::ReplayResult result;
    frames.readFront(
        [&](const DisplayList& list) { result = harness::replayFills(list, images, canvas.width, canvas.height); });
    EXPECT_TRUE(result.drawn);
    for (const std::string& line : result.errors) ADD_FAILURE() << line;
    return result.fills;
  }
};

constexpr const char* BLIT_BUDGET_TEXT = "the frame's icons and images cover over 1048576 pixels";

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
    ch.gfx.icon("dice-six", 1, 2, "small", "black"); ch.gfx.icon("circle", -3, 4, "medium", "white")
    ch.gfx.icon("heart", 70000, -70000, "large", "black"); ch.gfx.icon("x", 6.0, 7, "large", "white")
    ch.gfx.icon("x", 8, 9, "small", "white"); ch.gfx.icon("dice-six", 10, 11, "medium", "black")
    ch.gfx.icon("game-controller", 1, 2, "small", "black", nil)
    ch.gfx.icon("arrow-u-up-left", 1, 2, "medium", "white", "regular")
    ch.gfx.icon("dice-six", 1, 2, "small", "black", "fill"); ch.gfx.icon("circle", 3, 4, "large", "white", "fill")
    ch.gfx.refresh("half")
  )"));
  DirectGame game(arena, frames, sources, ports, canvas);
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
      "icon dice-six 1 2 small black regular",
      "icon circle -3 4 medium white regular",
      "icon heart 32767 -32768 large black regular",
      "icon x 6 7 large white regular",
      "icon x 8 9 small white regular",
      "icon dice-six 10 11 medium black regular",
      "icon game-controller 1 2 small black regular",
      "icon arrow-u-up-left 1 2 medium white regular",
      "icon dice-six 1 2 small black fill",
      "icon circle 3 4 large white fill",
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
    DirectGame game(arena, frames, sources, ports, canvas);
    ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
    ASSERT_EQ(game.draw(), Outcome::Ok) << c.body << ": " << game.errorMessage();
    EXPECT_EQ(frontRefresh(), c.expected) << c.body;
  }
  // The next frame starts at fast again.
  useSource("main", drawing("if ui.again then return end; ui.again = true; ch.gfx.refresh('full')"));
  DirectGame game(arena, frames, sources, ports, canvas);
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
      {"icon", "ch.gfx.icon('dice-six', 0, 0, 'small', 'black')"},
      {"image", "ch.gfx.image('badge', 0, 0, 'black')"},
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
      DirectGame game(arena, frames, sources, ports, canvas);
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
  DirectGame game(arena, frames, sources, ports, canvas);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(frontCommands().size(), MAX_COMMANDS);
  game.close();  // one VM per arena, as on the device: the reserve holds one scratch

  useSource("main", drawing("for i = 1, 2049 do ch.gfx.clear('white') end"));
  DirectGame over(arena, frames, sources, ports, canvas);
  ASSERT_EQ(over.start(), Outcome::Ok) << over.errorMessage();
  EXPECT_EQ(over.draw(), Outcome::ScriptError);
  EXPECT_STREQ(over.errorMessage(), "main.lua:3: frame is full (at most 2048 drawing calls or 32768 bytes)");
  EXPECT_EQ(frames.frameGen(), 1u);  // the overflowing frame was not published
}

// Both gfx faults stop the game (game-api-seed.md section 6): a script's pcall
// catches them at most once, and the call still ends in a ScriptError.
TEST_F(GfxBindingsTest, AFullFrameUnderPcallStillStopsTheGameUnpublished) {
  const char* const bodies[] = {
      // pcall straight on the binding: the error has no Lua caller to name.
      "for i = 1, 2100 do pcall(ch.gfx.rect, 0, 0, 1, 1, 'black') end",
      // pcall around Lua that overflows, which then carries on drawing.
      "pcall(function() for i = 1, 2100 do ch.gfx.clear('white') end end)\n"
      "ch.gfx.refresh('full')",
  };
  for (const char* body : bodies) {
    useSource("main", drawing(body));
    DirectGame game(arena, frames, sources, ports, canvas);
    ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
    const uint32_t before = frames.frameGen();
    EXPECT_EQ(game.draw(), Outcome::ScriptError) << body;
    EXPECT_TRUE(contains(game.errorMessage(), "frame is full (at most 2048 drawing calls or 32768 bytes)"))
        << body << " -> " << game.errorMessage();
    EXPECT_EQ(game.callGuard().fault(), Fault::Binding) << body;
    EXPECT_EQ(frames.frameGen(), before) << body;  // the cut frame was not published
  }
}

TEST_F(GfxBindingsTest, GfxOutsideDrawUnderPcallStillStopsTheGame) {
  const std::string sourcesFor[] = {
      "return { setup = function() pcall(ch.gfx.clear, 'white') return {} end }",
      "return { setup = function() local ok = pcall(function() ch.gfx.line(0, 0, 1, 1, 'black') end)\n"
      "  return { ok = ok } end }",
      "return { setup = function() return {} end, draw = function() end,\n"
      "  input = function() pcall(ch.gfx.refresh, 'full') return nil end }",
  };
  const char* const messages[] = {
      "ch.gfx.clear called outside draw",
      "main.lua:1: ch.gfx.line called outside draw",
      "ch.gfx.refresh called outside draw",
  };
  for (size_t i = 0; i < std::size(sourcesFor); ++i) {
    useSource("main", sourcesFor[i]);
    DirectGame game(arena, frames, sources, ports, canvas);
    Outcome outcome = game.start();
    if (outcome == Outcome::Ok) outcome = game.input(InputEvent{InputKind::Tap, 1, 1});
    EXPECT_EQ(outcome, Outcome::ScriptError) << sourcesFor[i];
    EXPECT_TRUE(contains(game.errorMessage(), messages[i])) << sourcesFor[i] << " -> " << game.errorMessage();
    EXPECT_EQ(game.callGuard().fault(), Fault::Binding) << sourcesFor[i];
  }
}

TEST_F(GfxBindingsTest, TheByteLimitIs32KiB) {
  // One text command takes 10 header bytes, its text, and a NUL: 32,757 characters
  // fill the frame exactly, and one more passes it.
  useSource("main", drawing("ch.gfx.text(0, 0, string.rep('x', 32757), 'small', 'black')"));
  DirectGame exact(arena, frames, sources, ports, canvas);
  ASSERT_EQ(exact.start(), Outcome::Ok) << exact.errorMessage();
  ASSERT_EQ(exact.draw(), Outcome::Ok) << exact.errorMessage();
  frames.readFront([](const DisplayList& list) { EXPECT_EQ(list.bytes(), MAX_BYTES); });
  exact.close();  // one VM per arena, as on the device: the reserve holds one scratch

  useSource("main", drawing("ch.gfx.text(0, 0, string.rep('x', 32758), 'small', 'black')"));
  DirectGame one(arena, frames, sources, ports, canvas);
  ASSERT_EQ(one.start(), Outcome::Ok) << one.errorMessage();
  EXPECT_EQ(one.draw(), Outcome::ScriptError);
  EXPECT_STREQ(one.errorMessage(), "main.lua:3: frame is full (at most 2048 drawing calls or 32768 bytes)");
  one.close();

  // Many small commands reach it too.
  useSource("main", drawing("for i = 1, 400 do ch.gfx.text(0, 0, string.rep('y', 90), 'small', 'black') end"));
  DirectGame many(arena, frames, sources, ports, canvas);
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
      {"ch.gfx.icon({}, 0, 0, 'small', 'black')", "bad argument #1 to 'icon' (string expected, got table)"},
      {"ch.gfx.icon(nil, 0, 0, 'small', 'black')", "bad argument #1 to 'icon' (string expected, got nil)"},
      {"ch.gfx.icon('dice-six', 0.5, 0, 'small', 'black')", "bad argument #2 to 'icon' (number has no integer"},
      {"ch.gfx.icon('dice-six', 0, '1.5', 'small', 'black')", "bad argument #3 to 'icon' (number has no integer"},
      {"ch.gfx.icon('dice-six', 0, 0, 'huge', 'black')", "bad argument #4 to 'icon' (invalid option 'huge')"},
      {"ch.gfx.icon('dice-six', 0, 0, nil, 'black')", "bad argument #4 to 'icon' (string expected, got nil)"},
      {"ch.gfx.icon('dice-six', 0, 0, 'small', 'light')", "bad argument #5 to 'icon' (\"light\" and \"dark\" are only"},
      {"ch.gfx.icon('dice-six', 0, 0, 'small', 'dark')", "bad argument #5 to 'icon' (\"light\" and \"dark\" are only"},
      {"ch.gfx.icon('dice-six', 0, 0, 'small')", "bad argument #5 to 'icon' (string expected, got no value)"},
      {"ch.gfx.icon('dice-six', 0, 0, 'small', 'black', 'bold')", "bad argument #6 to 'icon' (invalid option 'bold')"},
      {"ch.gfx.icon('dice-six', 0, 0, 'small', 'black', 1)", "bad argument #6 to 'icon' (invalid option '1')"},
      {"ch.gfx.icon('dice-six', 0, 0, 'small', 'black', {})", "bad argument #6 to 'icon' (string expected, got table)"},
      {"ch.gfx.icon('dice-six', 0, 0, 'small', 'black', 'Fill')", "bad argument #6 to 'icon' (invalid option 'Fill')"},
      {"ch.gfx.image({}, 0, 0, 'black')", "bad argument #1 to 'image' (string expected, got table)"},
      {"ch.gfx.image(nil, 0, 0, 'black')", "bad argument #1 to 'image' (string expected, got nil)"},
      {"ch.gfx.image('badge', 0.5, 0, 'black')", "bad argument #2 to 'image' (number has no integer"},
      {"ch.gfx.image('badge', 0, '1.5', 'black')", "bad argument #3 to 'image' (number has no integer"},
      {"ch.gfx.image('badge', 0, nil, 'black')", "bad argument #3 to 'image' (number expected, got nil)"},
      {"ch.gfx.image('badge', 0, 0, 'light')", "bad argument #4 to 'image' (\"light\" and \"dark\" are only"},
      {"ch.gfx.image('badge', 0, 0, 'dark')", "bad argument #4 to 'image' (\"light\" and \"dark\" are only"},
      {"ch.gfx.image('badge', 0, 0, 'grey')", "bad argument #4 to 'image' (invalid option 'grey')"},
      {"ch.gfx.image('badge', 0, 0)", "bad argument #4 to 'image' (string expected, got no value)"},
  };
  for (const auto& c : cases) {
    useSource("main", drawing(c[0]));
    DirectGame game(arena, frames, sources, ports, canvas);
    ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
    EXPECT_EQ(game.draw(), Outcome::ScriptError) << c[0];
    EXPECT_TRUE(contains(game.errorMessage(), c[1])) << c[0] << " -> " << game.errorMessage();
  }
  EXPECT_EQ(frames.frameGen(), 0u);
}

TEST_F(GfxBindingsTest, IconArgumentErrorsAreCatchable) {
  // Unlike an unknown name, a bad argument is an ordinary Lua error: pcall
  // catches it and draw carries on.
  const char* const cases[][2] = {
      {"'huge', 'black'", "false main.lua:3: bad argument #4 to 'icon' (invalid option 'huge')"},
      {"'small', 'black', 'bold'", "false main.lua:3: bad argument #6 to 'icon' (invalid option 'bold')"},
  };
  for (const auto& c : cases) {
    useSource("main", drawing(std::string("local ok, err = pcall(function() ch.gfx.icon('dice-six', 0, 0, ") + c[0] +
                              ") end)\nch.gfx.text(0, 0, tostring(ok) .. ' ' .. err, 'small', 'black')"));
    DirectGame game(arena, frames, sources, ports, canvas);
    ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
    ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
    EXPECT_EQ(frontText(), c[1]);
    EXPECT_EQ(game.callGuard().fault(), Fault::None);
  }
}

// An unknown icon name stops the game like a full frame: through the guard, so a
// script's pcall catches it at most once, and the frame is not published.
TEST_F(GfxBindingsTest, AnUnknownIconStopsTheGameEvenUnderPcall) {
  const char* const bodies[] = {
      "ch.gfx.icon('no_such_icon', 0, 0, 'small', 'black')",
      "pcall(function() ch.gfx.icon('no_such_icon', 0, 0, 'small', 'black') end)\n"
      "ch.gfx.text(0, 0, 'still drawing', 'small', 'black')",
  };
  for (const char* body : bodies) {
    useSource("main", drawing(std::string("ch.gfx.clear('white')\n") + body));
    DirectGame game(arena, frames, sources, ports, canvas);
    ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
    EXPECT_EQ(game.draw(), Outcome::ScriptError) << body;
    EXPECT_STREQ(game.errorMessage(), "main.lua:4: ch.gfx.icon: unknown icon \"no_such_icon\"") << body;
    EXPECT_EQ(game.callGuard().fault(), Fault::Binding) << body;
    EXPECT_EQ(frames.frameGen(), 0u) << body;  // the frame was not published
  }
  // pcall straight on the binding: no Lua caller to name.
  // mark_x was the library's name for x before it took Phosphor's names; it is unknown now.
  useSource("main", drawing("pcall(ch.gfx.icon, 'mark_x', 0, 0, 'small', 'black')"));
  DirectGame game(arena, frames, sources, ports, canvas);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(game.draw(), Outcome::ScriptError);
  EXPECT_TRUE(contains(game.errorMessage(), "ch.gfx.icon: unknown icon \"mark_x\"")) << game.errorMessage();
  EXPECT_EQ(game.callGuard().fault(), Fault::Binding);
  EXPECT_EQ(frames.frameGen(), 0u);
}

TEST_F(GfxBindingsTest, AnUnknownIconsNameIsShownShortAndPrintable) {
  struct Case {
    const char* name;  // Lua expression
    std::string shown;
  };
  std::string cut = "a";
  for (int i = 0; i < 15; ++i) cut += "\xC3\xA9";  // 31 bytes; the 32nd byte, the lead of the next e-acute, is cut off
  cut += "?";
  const Case cases[] = {
      {"string.rep('a', 40)", std::string(32, 'a')},
      {"string.rep('b', 32)", std::string(32, 'b')},
      {"'a' .. string.rep('\\u{e9}', 20)", cut},          // the cut splits the 16th e-acute: its lead byte is '?'
      {"string.rep('\\x80', 40)", std::string(32, '?')},  // continuation bytes only: 32 marks, not an empty name
      {"'x\\ny\"z\\0w\\127v\\tu'", "x?y?z?w?v?u"},
      {"''", ""},
      {"42", "42"},  // a number is a string to Lua's string checks
      // Retro R9 g: a byte that is not part of a well-formed UTF-8 sequence is '?', so the error view shows valid text.
      {"'a\\xC3' .. 'b'", "a?b"},    // a lone lead byte
      {"'a\\x80b'", "a?b"},          // a lone continuation byte
      {"'\\xE2\\x82'", "??"},        // a 3-byte sequence cut short
      {"'\\xC0\\xAF\\xFF'", "???"},  // leads the encoding never uses
      {"'\\xC3\\xA9\\xE2\\x82\\xAC\\xF0\\x9F\\x98\\x80'",
       "\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80"},                                // well-formed 2, 3, 4
      {"string.rep('a', 31) .. '\\xC3' .. 'zz'", std::string(31, 'a') + "?"},  // the 32nd byte is a lone lead
      // Overlong forms, surrogates, and code points past U+10FFFF are not well-formed either; their neighbours are.
      {"'\\xE0\\x80\\x80'", "???"},                    // overlong
      {"'\\xE0\\x9F\\xBF'", "???"},                    // overlong, at the edge
      {"'\\xE0\\xA0\\x80'", "\xE0\xA0\x80"},           // U+0800, the first 3-byte code point
      {"'\\xED\\xA0\\x80'", "???"},                    // a surrogate
      {"'\\xED\\x9F\\xBF'", "\xED\x9F\xBF"},           // U+D7FF, the last before them
      {"'\\xF0\\x8F\\xBF\\xBF'", "????"},              // overlong
      {"'\\xF0\\x90\\x80\\x80'", "\xF0\x90\x80\x80"},  // U+10000, the first 4-byte code point
      {"'\\xF4\\x90\\x80\\x80'", "????"},              // past U+10FFFF
      {"'\\xF4\\x8F\\xBF\\xBF'", "\xF4\x8F\xBF\xBF"},  // U+10FFFF
  };
  for (const auto& c : cases) {
    useSource("main", drawing(std::string("ch.gfx.icon(") + c.name + ", 0, 0, 'small', 'black')"));
    DirectGame game(arena, frames, sources, ports, canvas);
    ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
    EXPECT_EQ(game.draw(), Outcome::ScriptError) << c.name;
    EXPECT_EQ(std::string(game.errorMessage()), "main.lua:3: ch.gfx.icon: unknown icon \"" + c.shown + "\"") << c.name;
    EXPECT_EQ(game.callGuard().fault(), Fault::Binding) << c.name;
  }
}

TEST_F(GfxBindingsTest, AnIconIsOneCommandWithinTheFrameLimits) {
  useSource("main", drawing("for i = 1, 2047 do ch.gfx.clear('white') end\n"
                            "ch.gfx.icon('circle', 0, 0, 'large', 'black', 'fill')"));
  DirectGame game(arena, frames, sources, ports, canvas);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(frontCommands().size(), MAX_COMMANDS);
  EXPECT_EQ(frontCommands().back().op, Op::Icon);
  game.close();  // one VM per arena, as on the device: the reserve holds one scratch

  useSource("main", drawing("for i = 1, 2048 do ch.gfx.clear('white') end\n"
                            "ch.gfx.icon('circle', 0, 0, 'large', 'black', 'fill')"));
  DirectGame over(arena, frames, sources, ports, canvas);
  ASSERT_EQ(over.start(), Outcome::Ok) << over.errorMessage();
  EXPECT_EQ(over.draw(), Outcome::ScriptError);
  EXPECT_STREQ(over.errorMessage(), "main.lua:4: frame is full (at most 2048 drawing calls or 32768 bytes)");
  EXPECT_EQ(frames.frameGen(), 1u);
}

TEST_F(GfxBindingsTest, ImageCallsDecodeToTheirTableIndex) {
  useImages("images");  // badge, dot; icon.bmp is not an image
  ASSERT_EQ(images.count, 2u);
  useSource("main", drawing(R"(
    ch.gfx.image("badge", 1, 2, "black"); ch.gfx.image("dot", 3, 4, "white")
    ch.gfx.image("dot", -70000, 70000, "black"); ch.gfx.image("badge", 6.0, -7, "white")
  )"));
  DirectGame game(arena, frames, sources, ports, canvas, images);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
  const std::vector<std::string> expected = {
      "image 0 1 2 black",
      "image 1 3 4 white",
      "image 1 -32768 32767 black",
      "image 0 6 -7 white",
  };
  EXPECT_EQ(described(), expected);
}

// An unknown image name stops the game like an unknown icon: through the guard,
// so a script's pcall catches it at most once, and the frame is not published.
TEST_F(GfxBindingsTest, AnUnknownImageStopsTheGameEvenUnderPcall) {
  const char* const bodies[] = {
      "ch.gfx.image('no_such_image', 0, 0, 'black')",
      "pcall(function() ch.gfx.image('no_such_image', 0, 0, 'white') end)\n"
      "ch.gfx.text(0, 0, 'still drawing', 'small', 'black')",
      // A prefix, and a name the table has with more after it.
      "ch.gfx.image('bad', 0, 0, 'black')",
      "ch.gfx.image('badge.bmp', 0, 0, 'black')",
      // icon.bmp is never an image.
      "ch.gfx.image('icon', 0, 0, 'black')",
  };
  const char* const shown[] = {"no_such_image", "no_such_image", "bad", "badge.bmp", "icon"};
  for (const bool withImages : {true, false}) {
    if (withImages) {
      useImages("images");
    } else {
      images = GameCore::GameImages{};
    }
    for (size_t i = 0; i < std::size(bodies); ++i) {
      useSource("main", drawing(std::string("ch.gfx.clear('white')\n") + bodies[i]));
      DirectGame game(arena, frames, sources, ports, canvas, images);
      ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
      EXPECT_EQ(game.draw(), Outcome::ScriptError) << bodies[i];
      EXPECT_EQ(std::string(game.errorMessage()),
                std::string("main.lua:4: ch.gfx.image: unknown image \"") + shown[i] + "\"")
          << bodies[i];
      EXPECT_EQ(game.callGuard().fault(), Fault::Binding) << bodies[i];
      EXPECT_EQ(frames.frameGen(), 0u) << bodies[i];  // the frame was not published
    }
  }
  // No images at all, the LuaGame default: every name is unknown.
  useSource("main", drawing("pcall(ch.gfx.image, 'badge', 0, 0, 'black')"));
  DirectGame game(arena, frames, sources, ports, canvas);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(game.draw(), Outcome::ScriptError);
  EXPECT_TRUE(contains(game.errorMessage(), "ch.gfx.image: unknown image \"badge\"")) << game.errorMessage();
  EXPECT_EQ(game.callGuard().fault(), Fault::Binding);
  EXPECT_EQ(frames.frameGen(), 0u);
}

TEST_F(GfxBindingsTest, AnUnknownImagesNameIsShownShortAndPrintable) {
  struct Case {
    const char* name;  // Lua expression
    std::string shown;
  };
  const Case cases[] = {
      {"string.rep('a', 40)", std::string(32, 'a')},
      {"'x\\ny\"z\\0w\\127v\\tu'", "x?y?z?w?v?u"},
      {"''", ""},
      {"7", "7"},
      {"'a\\xC3' .. 'b'", "a?b"},  // as for icons (retro R9 g): a lone UTF-8 lead byte is '?'
      {"'\\xC3\\xA9'", "\xC3\xA9"},
  };
  useImages("images");
  for (const auto& c : cases) {
    useSource("main", drawing(std::string("ch.gfx.image(") + c.name + ", 0, 0, 'black')"));
    DirectGame game(arena, frames, sources, ports, canvas, images);
    ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
    EXPECT_EQ(game.draw(), Outcome::ScriptError) << c.name;
    EXPECT_EQ(std::string(game.errorMessage()), "main.lua:3: ch.gfx.image: unknown image \"" + c.shown + "\"")
        << c.name;
    EXPECT_EQ(game.callGuard().fault(), Fault::Binding) << c.name;
  }
}

TEST_F(GfxBindingsTest, AnImageIsOneCommandWithinTheFrameLimits) {
  useImages("images");
  useSource("main", drawing("for i = 1, 2047 do ch.gfx.clear('white') end\n"
                            "ch.gfx.image('badge', 0, 0, 'black')"));
  DirectGame game(arena, frames, sources, ports, canvas, images);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(frontCommands().size(), MAX_COMMANDS);
  EXPECT_EQ(frontCommands().back().op, Op::Image);
  game.close();  // one VM per arena, as on the device: the reserve holds one scratch

  useSource("main", drawing("for i = 1, 2048 do ch.gfx.clear('white') end\n"
                            "ch.gfx.image('badge', 0, 0, 'black')"));
  DirectGame over(arena, frames, sources, ports, canvas, images);
  ASSERT_EQ(over.start(), Outcome::Ok) << over.errorMessage();
  EXPECT_EQ(over.draw(), Outcome::ScriptError);
  EXPECT_STREQ(over.errorMessage(), "main.lua:4: frame is full (at most 2048 drawing calls or 32768 bytes)");
  EXPECT_EQ(frames.frameGen(), 1u);
}

// ch.gfx.icon and ch.gfx.image share a budget of MAX_BLIT_PIXELS canvas pixels a
// frame (api-level-1.txt's frame_icon_image_pixels): exactly the budget draws, one
// pixel more stops the game, and the frame is not published.
TEST_F(GfxBindingsTest, IconsAndImagesDrawUpToTheFramesPixelBudget) {
  ASSERT_EQ(MAX_BLIT_PIXELS, 1048576u);
  ASSERT_EQ(canvas.width, 480);
  ASSERT_EQ(canvas.height, 800);
  useChecker();
  // 2 x 384,000, 480 x 584 = 280,320 (rows 216..799), and 1 x 256 (x = 479):
  // 1,048,576.
  const std::string checkerAtBudget =
      "ch.gfx.image('checker', 0, 0, 'black'); ch.gfx.image('checker', 0, 0, 'white')\n"
      "ch.gfx.image('checker', 0, 216, 'black'); ch.gfx.image('checker', 479, 544, 'white')";
  // 64 large icons of 128 x 128 = 16,384 pixels each: 1,048,576.
  const std::string iconsAtBudget = "for i = 1, 64 do ch.gfx.icon('circle', 0, 0, 'large', 'black', 'fill') end";
  struct Case {
    std::string atBudget;
    std::string onePixelMore;
    const char* kind;
  };
  const Case cases[] = {
      {checkerAtBudget, "ch.gfx.image('checker', 479, 799, 'black')", "image"},
      {iconsAtBudget, "ch.gfx.icon('x', -127, -127, 'large', 'black')", "icon"},
      // Across the kinds: the budget is one.
      {iconsAtBudget, "ch.gfx.image('checker', -479, -799, 'black')", "image"},
      {checkerAtBudget, "ch.gfx.icon('x', 479, 799, 'small', 'black')", "icon"},
  };
  for (const Case& c : cases) {
    {
      useSource("main", drawing(c.atBudget));
      DirectGame game(arena, frames, sources, ports, canvas, images);
      ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
      ASSERT_EQ(game.draw(), Outcome::Ok) << c.atBudget << " -> " << game.errorMessage();
      EXPECT_EQ(frontBlitPixels(), MAX_BLIT_PIXELS) << c.atBudget;
    }
    useSource("main", drawing(c.atBudget + "\n" + c.onePixelMore));
    DirectGame over(arena, frames, sources, ports, canvas, images);
    ASSERT_EQ(over.start(), Outcome::Ok) << over.errorMessage();
    const uint32_t before = frames.frameGen();
    EXPECT_EQ(over.draw(), Outcome::ScriptError) << c.onePixelMore;
    EXPECT_EQ(std::string(over.errorMessage()), std::string("main.lua:") + (c.atBudget == iconsAtBudget ? "4" : "5") +
                                                    ": ch.gfx." + c.kind + ": " + BLIT_BUDGET_TEXT)
        << c.onePixelMore;
    EXPECT_EQ(over.callGuard().fault(), Fault::Binding) << c.onePixelMore;
    EXPECT_EQ(frames.frameGen(), before) << c.onePixelMore;  // not published
  }
}

// Like a full frame, the budget stops the game under pcall: the fault is sticky,
// and the cut frame is not published.
TEST_F(GfxBindingsTest, TheFramesPixelBudgetUnderPcallStillStopsTheGameUnpublished) {
  useImages("images");
  const char* const bodies[] = {
      // pcall straight on the binding: the error has no Lua caller to name.
      "for i = 1, 70 do pcall(ch.gfx.icon, 'circle', 0, 0, 'large', 'black') end",
      // pcall around Lua that passes it, which then carries on drawing.
      "pcall(function() for i = 1, 65 do ch.gfx.icon('circle', 0, 0, 'large', 'black') end end)\n"
      "ch.gfx.text(0, 0, 'still drawing', 'small', 'black')",
      "pcall(function() for i = 1, 200 do ch.gfx.image('badge', 0, 0, 'black') end end)\n"
      "ch.gfx.image('dot', 0, 0, 'black')",
  };
  for (const char* body : bodies) {
    useSource("main", drawing(body));
    DirectGame game(arena, frames, sources, ports, canvas, images);
    ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
    const uint32_t before = frames.frameGen();
    EXPECT_EQ(game.draw(), Outcome::ScriptError) << body;
    EXPECT_TRUE(contains(game.errorMessage(), BLIT_BUDGET_TEXT)) << body << " -> " << game.errorMessage();
    EXPECT_EQ(game.callGuard().fault(), Fault::Binding) << body;
    EXPECT_EQ(frames.frameGen(), before) << body;
  }
}

// The checks before the budget keep their order: a bad argument is still an
// ordinary error pcall catches, and an unknown name is reported as such, even with
// the budget spent.
TEST_F(GfxBindingsTest, TheBudgetComesAfterTheArgumentAndNameChecks) {
  const std::string full = "for i = 1, 64 do ch.gfx.icon('circle', 0, 0, 'large', 'black') end\n";
  useSource("main",
            drawing(full + "local ok, err = pcall(function() ch.gfx.icon('circle', 0, 0, 'huge', 'black') end)\n"
                           "ch.gfx.text(0, 0, tostring(ok) .. ' ' .. err, 'small', 'black')"));
  {
    DirectGame game(arena, frames, sources, ports, canvas);
    ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
    ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
    EXPECT_EQ(frontText(), "false main.lua:4: bad argument #4 to 'icon' (invalid option 'huge')");
    EXPECT_EQ(game.callGuard().fault(), Fault::None);
  }
  useSource("main", drawing(full + "ch.gfx.icon('no_such_icon', 0, 0, 'large', 'black')"));
  DirectGame game(arena, frames, sources, ports, canvas);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(game.draw(), Outcome::ScriptError);
  EXPECT_STREQ(game.errorMessage(), "main.lua:4: ch.gfx.icon: unknown icon \"no_such_icon\"");
}

// Only the pixels a command's replay walks count: none for one wholly off the
// canvas (at its saturated coordinates), the visible part of one partly on.
TEST_F(GfxBindingsTest, OnlyTheVisiblePixelsCountTowardTheBudget) {
  useImages("images");  // badge 100 x 60, dot 37 x 37
  useSource("main", drawing(R"(
    for i = 1, 100 do
      ch.gfx.icon('circle', -200, 0, 'large', 'black'); ch.gfx.icon('circle', 480, 0, 'large', 'black')
      ch.gfx.icon('circle', 0, 800, 'large', 'black'); ch.gfx.icon('circle', 0, -128, 'large', 'black')
      ch.gfx.icon('circle', -70000, 0, 'large', 'black'); ch.gfx.icon('circle', 70000, 70000, 'large', 'black')
      ch.gfx.image('badge', -100, 0, 'black'); ch.gfx.image('badge', 0, 70000, 'white')
      ch.gfx.image('dot', -70000, -70000, 'black')
    end
  )"));
  {
    DirectGame game(arena, frames, sources, ports, canvas, images);
    ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
    ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
    EXPECT_EQ(frontBlitPixels(), 0u);
    EXPECT_EQ(frontBlitFills(), 0u);
  }

  struct Case {
    const char* call;
    uint32_t pixels;
  };
  const Case cases[] = {
      {"ch.gfx.image('badge', 440, 0, 'black')", 40 * 60},          // right edge
      {"ch.gfx.image('badge', 0, 770, 'white')", 100 * 30},         // bottom edge
      {"ch.gfx.image('dot', -30, -30, 'black')", 7 * 7},            // top-left corner
      {"ch.gfx.icon('x', -100, 700, 'large', 'black')", 28 * 100},  // left and bottom
      {"ch.gfx.icon('x', 470, 10, 'small', 'white')", 10 * 32},     // right edge
      {"ch.gfx.icon('x', 10, 10, 'medium', 'black')", 64 * 64},     // whole
  };
  for (const Case& c : cases) {
    useSource("main", drawing(c.call));
    DirectGame game(arena, frames, sources, ports, canvas, images);
    ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
    ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
    EXPECT_EQ(frontBlitPixels(), c.pixels) << c.call;
  }
}

// Each frame starts from 0: a game that spends the whole budget every frame keeps
// drawing.
TEST_F(GfxBindingsTest, EachFrameStartsItsPixelBudgetAtZero) {
  useSource("main", drawing("for i = 1, 64 do ch.gfx.icon('square', 0, 0, 'large', 'black', 'fill') end"));
  DirectGame game(arena, frames, sources, ports, canvas);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  for (int frame = 0; frame < 3; ++frame) {
    ASSERT_EQ(game.draw(), Outcome::Ok) << frame << ": " << game.errorMessage();
    EXPECT_EQ(frontBlitPixels(), MAX_BLIT_PIXELS) << frame;
  }
}

// The budget bounds icon and image replay (filled rects and circles are not
// charged): a frame at the budget makes at most MAX_BLIT_PIXELS fills, and a
// checkerboard, a run a pixel, exactly that many (was up to 2,048 x 384,000).
TEST_F(GfxBindingsTest, AFrameAtTheBudgetReplaysInAtMostThatManyFills) {
  useChecker();
  const char* const bodies[] = {
      "ch.gfx.image('checker', 0, 0, 'black'); ch.gfx.image('checker', 0, 0, 'white')\n"
      "ch.gfx.image('checker', 0, 216, 'black'); ch.gfx.image('checker', 479, 544, 'white')",
      "for i = 1, 64 do ch.gfx.icon('circle', 0, 0, 'large', 'black', 'fill') end",
      "for i = 0, 63 do ch.gfx.icon('dice-five', (i % 4) * 100, (i // 4) * 40, 'large', 'black') end",
      "ch.gfx.image('checker', 0, 0, 'black'); ch.gfx.image('checker', 0, 0, 'white')\n"
      "for i = 1, 17 do ch.gfx.icon('square', 300, 600, 'large', 'white') end\n"
      "ch.gfx.image('checker', 224, 792, 'black')",
  };
  const uint64_t exact[] = {MAX_BLIT_PIXELS, 0, 0, 0};  // 0: at most
  for (size_t i = 0; i < std::size(bodies); ++i) {
    useSource("main", drawing(bodies[i]));
    DirectGame game(arena, frames, sources, ports, canvas, images);
    ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
    ASSERT_EQ(game.draw(), Outcome::Ok) << bodies[i] << " -> " << game.errorMessage();
    EXPECT_EQ(frontBlitPixels(), MAX_BLIT_PIXELS) << bodies[i];
    const uint64_t fills = frontBlitFills();
    EXPECT_LE(fills, MAX_BLIT_PIXELS) << bodies[i];
    if (exact[i] != 0) EXPECT_EQ(fills, exact[i]) << bodies[i];
  }
}

TEST_F(GfxBindingsTest, ScreenIsTheCanvasPassedAtStart) {
  useSource("main",
            "local w, h = ch.screen.w, ch.screen.h\n"
            "return { setup = function() return { size = w .. 'x' .. h } end,\n"
            "  draw = function(s) ch.gfx.text(0, 0, s.size .. ' ' .. math.type(ch.screen.w), 'small', 'black') end }");
  {
    DirectGame game(arena, frames, sources, ports, canvas);
    ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
    ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
    EXPECT_EQ(frontText(), "480x800 integer");
  }
  // LuaGame keeps its own copy: the match's canvas is a local of onEnter.
  Canvas small{123, 45, TextMetrics::standIn()};
  DirectGame game(arena, frames, sources, ports, small);
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
  DirectGame game(arena, frames, sources, ports, canvas);
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
  large.ranges = reinterpret_cast<const uint8_t*>(ranges);
  large.rangeCount = 1;
  large.advances = reinterpret_cast<const uint8_t*>(advances);
  large.fallback = 2 * 16;
  useSource("main",
            "return { setup = function() return {} end,\n"
            "  input = function(s, seat, ui) ui.w = ch.text_width('ABAz', 'large') .. ' ' ..\n"
            "    ch.text_width('ABAz', 'small') end,\n"
            "  draw = function(s, seat, ui) ch.gfx.text(0, 0, tostring(ui.w), 'small', 'black') end }");
  DirectGame game(arena, frames, sources, ports, custom);
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
    DirectGame game(arena, frames, sources, ports, canvas);
    ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
    EXPECT_EQ(game.input(InputEvent{InputKind::Tap, 1, 1}), Outcome::ScriptError) << c[0];
    EXPECT_TRUE(contains(game.errorMessage(), c[1])) << c[0] << " -> " << game.errorMessage();
  }
}

}  // namespace

// The timing fixture (test/game_script/fixtures/timing/, entry 13 of epic-install-and-launcher) draws its three
// frames at the limits, whatever the canvas: the whole icon and image pixel budget, and the whole command limit. The
// device run times them (the fixtures README); this pins that each is exactly what it says and is not refused.
TEST_F(GfxBindingsTest, TheTimingFixturesBandsSitAtTheLimitsOnEveryCanvas) {
  useGray();
  useSource("main", GameScriptTestSupport::readFixture("timing/main.lua"));
  struct Size {
    int16_t w;
    int16_t h;
  };
  // The Sticky's 474 x 788 (the test canvas; the X4 Pro's is 466 x 788), and one too small to hold the icons apart.
  const Size sizes[] = {{474, 788}, {480, 800}, {300, 500}};
  for (const Size size : sizes) {
    canvas = GameScript::Canvas{size.w, size.h, GameScript::TextMetrics::standIn()};
    const std::string where = std::to_string(size.w) + "x" + std::to_string(size.h);
    // Band 1: the one image, once.
    {
      SessionGame game(*this);
      ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
      ASSERT_EQ(game.tap(50, 150), Outcome::Ok) << game.errorMessage();
      const auto commands = frontCommands();
      ASSERT_EQ(commands.size(), 1u) << where;
      EXPECT_EQ(commands[0].op, Op::Image);
      EXPECT_EQ(frontBlitPixels(), static_cast<uint32_t>(std::min<int>(size.w, 480) * std::min<int>(size.h, 800)));
      EXPECT_GT(frontBlitFills(), 100000u) << where << ": the dithered gray is a run or two a pixel pair";
    }
    // Band 2: exactly the budget, so a frame that adds one pixel more is refused (the next test).
    {
      SessionGame game(*this);
      ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
      ASSERT_EQ(game.tap(50, 260), Outcome::Ok) << game.errorMessage();
      EXPECT_EQ(frontBlitPixels(), MAX_BLIT_PIXELS) << where;
      EXPECT_TRUE(std::any_of(log.lines.begin(), log.lines.end(), [](const std::string& line) {
        return line == "band 2 charges 1048576 pixels";
      })) << where;
      EXPECT_LT(frontCommands().size(), 100u);
    }
    // Band 3: the command limit, every one a filled rect over the whole canvas, in the four colors in turn.
    {
      SessionGame game(*this);
      ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
      ASSERT_EQ(game.tap(50, 370), Outcome::Ok) << game.errorMessage();
      const auto commands = frontCommands();
      ASSERT_EQ(commands.size(), MAX_COMMANDS) << where;
      for (const auto& c : commands) {
        ASSERT_EQ(c.op, Op::Rect);
        EXPECT_TRUE(c.filled);
        EXPECT_EQ(c.x, 0);
        EXPECT_EQ(c.y, 0);
        EXPECT_EQ(c.w, size.w);
        EXPECT_EQ(c.h, size.h);
      }
      EXPECT_EQ(commands.back().color, Color::Dark);
      EXPECT_EQ(commands.front().color, Color::Light);
    }
  }
}
