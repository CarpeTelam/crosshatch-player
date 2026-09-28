#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "GameIcons.h"
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

TEST_F(LuaGameTest, LongPressesAndSwipesReachInputWithTheirFields) {
  // The gallery fixture prints the last touch event's kind, x, y, and dir.
  useSource("main", readFixture("gallery/main.lua"));
  DirectGame game(arena, frames, sources, ports, canvas);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  InputEvent press;
  press.kind = InputKind::LongPress;
  press.x = 12;
  press.y = 34;
  ASSERT_EQ(game.input(press), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
  EXPECT_TRUE(hasText(frontCommands(), "1: long_press at 12,34"));
  const char* const dirs[] = {"left", "right", "up", "down"};
  const GameCore::SwipeDir kinds[] = {GameCore::SwipeDir::Left, GameCore::SwipeDir::Right, GameCore::SwipeDir::Up,
                                      GameCore::SwipeDir::Down};
  for (int i = 0; i < 4; ++i) {
    InputEvent swipe;
    swipe.kind = InputKind::Swipe;
    swipe.x = static_cast<int16_t>(100 + i);
    swipe.y = 200;
    swipe.dir = kinds[i];
    ASSERT_EQ(game.input(swipe), Outcome::Ok) << game.errorMessage();
    ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
    EXPECT_TRUE(
        hasText(frontCommands(), std::to_string(i + 2) + ": swipe at " + std::to_string(100 + i) + ",200 " + dirs[i]))
        << frontText();
  }
  ASSERT_EQ(log.lines.size(), 5u);
  EXPECT_EQ(log.lines[0], "event\tlong_press\t12\t34\tnil");
  EXPECT_EQ(log.lines[1], "event\tswipe\t100\t200\tleft");
}

TEST_F(LuaGameTest, TheGalleryDrawsEveryCommandAndFillColor) {
  useSource("main", readFixture("gallery/main.lua"));
  DirectGame game(arena, frames, sources, ports, canvas);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
  bool ops[5] = {};
  bool rectFills[4] = {};
  bool circleFills[4] = {};
  bool aligns[3] = {};
  bool sizes[3] = {};
  bool whiteInk = false;
  for (const DrawCommand& c : frontCommands()) {
    ops[static_cast<int>(c.op)] = true;
    if (c.op == Op::Rect && c.filled) rectFills[static_cast<int>(c.color)] = true;
    if (c.op == Op::Circle && c.filled) circleFills[static_cast<int>(c.color)] = true;
    if (c.op == Op::Text) {
      aligns[static_cast<int>(c.align)] = true;
      sizes[static_cast<int>(c.size)] = true;
    }
    if ((c.op == Op::Line || c.op == Op::Text || (c.op == Op::Rect && !c.filled)) && c.color == Color::White) {
      whiteInk = true;
    }
  }
  for (int i = 0; i < 5; ++i) EXPECT_TRUE(ops[i]) << i;
  for (int i = 0; i < 4; ++i) {
    EXPECT_TRUE(rectFills[i]) << i;
    EXPECT_TRUE(circleFills[i]) << i;
  }
  for (int i = 0; i < 3; ++i) {
    EXPECT_TRUE(aligns[i]) << i;
    EXPECT_TRUE(sizes[i]) << i;
  }
  EXPECT_TRUE(whiteInk);
}

// The icons fixture: the first draw is the black page, a tap turns to the white
// page; each draws every library icon at each size in that page's color.
TEST_F(LuaGameTest, TheIconsFixtureDrawsEveryIconAtEachSizeInBothColors) {
  useSource("main", readFixture("icons/main.lua"));
  DirectGame game(arena, frames, sources, ports, canvas);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  const Color pages[] = {Color::Black, Color::White};
  for (const Color ink : pages) {
    if (ink == Color::White) {
      ASSERT_EQ(game.input(InputEvent{InputKind::Tap, 10, 10}), Outcome::Ok) << game.errorMessage();
    }
    ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
    std::vector<std::vector<bool>> seen(GameIcons::ICON_COUNT, std::vector<bool>(3, false));
    for (const DrawCommand& c : frontCommands()) {
      if (c.op == Op::Clear) {
        EXPECT_NE(c.color, ink);
      }
      if (c.op != Op::Icon) continue;
      EXPECT_EQ(c.color, ink);
      ASSERT_LT(c.icon, GameIcons::ICON_COUNT);
      seen[c.icon][static_cast<size_t>(c.size)] = true;
    }
    for (size_t i = 0; i < GameIcons::ICON_COUNT; ++i) {
      for (size_t size = 0; size < 3; ++size) {
        EXPECT_TRUE(seen[i][size]) << GameIcons::ICONS[i].name << " size " << size << " page " << static_cast<int>(ink);
      }
    }
  }
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

// The memory limit stops the game (game-api-seed.md section 6): pcall, xpcall,
// and a require under pcall catch the memory error at most once, and the call
// still ends in a ScriptError with Lua's own text and no frame published.
TEST_F(LuaGameTest, TheHeapCapStopsTheGameEvenUnderPcall) {
  const std::string bomb = "local t = {} for i = 1, 1e7 do t[i] = i end";
  const std::vector<Module> cases[] = {
      {{"main",
        "return { setup = function() return {} end,\n"
        "  draw = function() pcall(function() " +
            bomb + " end) ch.gfx.clear('white') end }"}},
      {{"main",
        "return { setup = function() return {} end,\n"
        "  draw = function() xpcall(function() " +
            bomb + " end, function(m) return m end) ch.gfx.clear('white') end }"}},
      {{"main",
        "return { setup = function() return {} end,\n"
        "  draw = function() pcall(require, 'bomb') ch.gfx.clear('white') end }"},
       {"bomb", bomb + "\nreturn t"}},
      // Lua's own memory error text raised at level 0 is a memory error too (accepted).
      {{"main",
        "return { setup = function() return {} end,\n"
        "  draw = function() pcall(error, 'not enough memory', 0) ch.gfx.clear('white') end }"}},
  };
  for (const auto& modules : cases) {
    useSources(modules);
    DirectGame game(arena, frames, sources, ports, canvas);
    ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
    const uint32_t before = frames.frameGen();
    EXPECT_EQ(game.draw(), Outcome::ScriptError) << modules[0].second;
    EXPECT_STREQ(game.errorMessage(), "not enough memory") << modules[0].second;
    EXPECT_EQ(game.callGuard().fault(), Fault::Memory) << modules[0].second;
    EXPECT_EQ(frames.frameGen(), before) << modules[0].second;
  }
  // Other errors under pcall and xpcall are still the script's to handle.
  useSource("main",
            "return { setup = function() return {} end,\n"
            "  draw = function() local ok, e = pcall(error, 'x', 0)\n"
            "    local ok2, e2 = xpcall(error, function(m) return 'h ' .. m end, 'y', 0)\n"
            "    ch.gfx.text(0, 0, tostring(ok) .. e .. tostring(ok2) .. e2, 'small', 'black') end }");
  DirectGame game(arena, frames, sources, ports, canvas);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(frontText(), "falsexfalseh y");
}

// setmetatable refuses __close, and the string metatable is sealed, so the plain
// ways to make a closable value fail. Each attempt, under pcall and xpcall, is a
// plain error the script catches, before the heap bomb. (A __close added later is
// ACloseAddedLaterCannotOutliveTheHeapCap.)
TEST_F(LuaGameTest, AScriptCannotMakeAClosableValue) {
  struct Case {
    const char* body;
    const char* message;
  };
  const Case cases[] = {
      {"local c <close> = setmetatable({}, { __close = function() error('recovered', 0) end })",
       "setmetatable: __close metamethods are not supported"},
      {"getmetatable('').__close = function() error('recovered', 0) end local c <close> = 'x'",
       "attempt to index a boolean value"},
      {"local c <close> = 'x'", "variable 'c' got a non-closable value"},
      {"for _ in next, {}, nil, setmetatable({}, { __close = print }) do end",
       "setmetatable: __close metamethods are not supported"},
  };
  for (const char* wrap : {"pcall(f)", "xpcall(f, function(m) return m end)"}) {
    for (const auto& c : cases) {
      useSource("main", std::string("return { setup = function() return {} end,\n"
                                    "  draw = function()\n"
                                    "    local f = function() ") +
                            c.body +
                            " local s = string.rep('x', 1 << 20) end\n"
                            "    local ok, e = " +
                            wrap +
                            "\n"
                            "    ch.gfx.text(0, 0, tostring(ok) .. ' ' .. tostring(e), 'small', 'black') end }");
      DirectGame game(arena, frames, sources, ports, canvas);
      ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
      ASSERT_EQ(game.draw(), Outcome::Ok) << wrap << " " << c.body << ": " << game.errorMessage();
      EXPECT_EQ(frontText().rfind("false ", 0), 0u) << wrap << " " << c.body << " -> " << frontText();
      EXPECT_TRUE(contains(frontText().c_str(), c.message)) << wrap << " " << c.body << " -> " << frontText();
      EXPECT_EQ(game.callGuard().fault(), Fault::None) << wrap << " " << c.body;
    }
  }
}

// Lua looks __close up again when it closes, so a __close added to a metatable
// after setmetatable still runs, and whatever the close does while a memory error
// unwinds (raises, or finds no __close left to call) replaces the error
// (luaD_closeprotected). The guard records the memory error where Lua throws it
// (lib/lua/port/luai_throw.h), so the call still ends: with the bomb in the
// protected function, in a module it requires, or in a module with its own
// to-be-closed value.
TEST_F(LuaGameTest, ACloseAddedLaterCannotOutliveTheHeapCap) {
  struct Closable {
    const char* before;  // makes `v` closable
    const char* after;   // runs after `local c <close> = v`
  };
  const Closable closables[] = {
      {"local mt = {} local v = setmetatable({}, mt) mt.__close = function() error('recovered', 0) end", ""},
      {"local mt = {} local v = setmetatable({}, mt) mt.__close = error", ""},
      {"local mt = {} local v = setmetatable({}, mt) mt.__close = print", "mt.__close = nil"},
      {"local mt = {} local v = setmetatable({}, mt) rawset(mt, '__close', function() error('r', 0) end)", ""},
      {"setmetatable(_G, {}) getmetatable(_G).__close = function() error('r', 0) end local v = _G", ""},
  };
  const std::string inlineBomb = "local s = string.rep('x', 1 << 20)";
  const std::vector<std::string> inFunction = {"pcall(f)", "xpcall(f, function(m) return m end)"};
  const std::vector<std::string> inModule = {"pcall(require, 'm')", "xpcall(require, function(m) return m end, 'm')"};
  for (const auto& closable : closables) {
    const std::string body = std::string(closable.before) + "\nlocal c <close> = v\n" + closable.after + "\n";
    struct Placement {
      std::string function;  // the body of main's f
      std::string module;    // module m
      const std::vector<std::string>& calls;
    };
    const Placement placements[] = {
        {body + inlineBomb, "return true", inFunction},
        {body + "require('bomb')", "return true", inFunction},
        {"", body + inlineBomb + "\nreturn true", inModule},
    };
    for (const auto& placement : placements) {
      for (const std::string& call : placement.calls) {
        useSources({{"main",
                     "return { setup = function() return {} end,\n"
                     "  draw = function()\n"
                     "    local f = function()\n" +
                         placement.function +
                         "\n    end\n"
                         "    local ok, e = " +
                         call +
                         "\n"
                         "    ch.gfx.text(0, 0, tostring(ok) .. ' ' .. tostring(e), 'small', 'black') end }"},
                    {"bomb", "local t = {} for i = 1, 1e7 do t[i] = i end\nreturn t"},
                    {"m", placement.module}});
        const std::string what = std::string(closable.before) + " | " + closable.after + " | " +
                                 (placement.function.empty() ? "in module m" : placement.function) + " | " + call;
        DirectGame game(arena, frames, sources, ports, canvas);
        ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
        const uint32_t before = frames.frameGen();
        EXPECT_EQ(game.draw(), Outcome::ScriptError) << what << " -> " << frontText();
        EXPECT_STREQ(game.errorMessage(), "not enough memory") << what;
        EXPECT_EQ(game.callGuard().fault(), Fault::Memory) << what;
        EXPECT_EQ(frames.frameGen(), before) << what;
      }
    }
  }
}

// On Lua's own allocation path (luaM), a refused allocation is retried after an
// emergency collection; only a memory error Lua then throws is the heap cap. A
// script whose garbage reaches the cap many times over, with the collector
// stopped, runs on with no fault. Its garbage is tables grown by assignment (no
// library string buffer, which the next test covers), so the path is the same
// whatever the word size.
TEST_F(LuaGameTest, ARefusalLuaRecoversFromIsNotAFault) {
  useSource("main",
            "return { setup = function() return {} end,\n"
            "  draw = function()\n"
            "    collectgarbage('stop')\n"
            "    for i = 1, 1000 do local t = {} for j = 1, 64 do t[j] = j end end\n"
            "    collectgarbage('restart')\n"
            "    ch.gfx.text(0, 0, 'ran', 'small', 'black') end }");
  DirectGame game(arena, frames, sources, ports, canvas);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  const size_t refusalsBefore = arena.luaCapRefusals();
  ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
  EXPECT_GT(arena.luaCapRefusals(), refusalsBefore);  // the cap did refuse, and Lua recovered
  EXPECT_EQ(game.callGuard().fault(), Fault::None);
  EXPECT_EQ(frontText(), "ran");
}

// A library string buffer past LUAL_BUFFERSIZE (lauxlib's resizebox) calls the
// allocator directly and raises at the first refusal, with no emergency
// collection (as in stock Lua): near the cap, with plenty of garbage a collection
// would free, a 4 KiB string.rep still ends the game.
TEST_F(LuaGameTest, ALibraryStringBufferRefusedAtTheCapStopsTheGame) {
  useSource("main",
            "return { setup = function() return {} end,\n"
            "  draw = function()\n"
            "    collectgarbage('stop')\n"
            "    while collectgarbage('count') < 254 do local t = {} for j = 1, 8 do t[j] = j end end\n"
            "    local ok = pcall(string.rep, 'x', 4096)\n"
            "    ch.gfx.text(0, 0, tostring(ok), 'small', 'black') end }");
  DirectGame game(arena, frames, sources, ports, canvas);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(game.draw(), Outcome::ScriptError);
  EXPECT_STREQ(game.errorMessage(), "not enough memory");
  EXPECT_EQ(game.callGuard().fault(), Fault::Memory);
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
