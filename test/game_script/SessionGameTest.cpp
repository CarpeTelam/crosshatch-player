#include <gtest/gtest.h>
#include <pthread.h>

#include <climits>
#include <cstdint>
#include <functional>
#include <iostream>
#include <lua.hpp>
#include <string>
#include <vector>

#include "Codec.h"
#include "LuaGameFixture.h"

// The game contract through a solo Session over a LuaGame, as the GameVM task runs
// it (AD-8, AD-9, AD-10, AD-11).

using namespace GameScript;
using GameScriptTestSupport::DirectGame;
using GameScriptTestSupport::LuaGameTest;
using GameScriptTestSupport::readFixture;

namespace {

class SessionGameTest : public LuaGameTest {
 protected:
  // A game whose functions are the given Lua expressions; the rest are minimal.
  static std::string gameWith(const std::string& fields) {
    return "local g = { setup = function() return {} end, status = function() return { turn = 1 } end,\n"
           "  apply = function(s) return s end, draw = function() end, input = function() return {} end }\n"
           "local extra = {" +
           fields +
           "}\n"
           "for k, v in pairs(extra) do g[k] = v end\n"
           "return g";
  }
};

TEST_F(SessionGameTest, TheTracerPlaysToGameOver) {
  useSource("main", readFixture("tracer/main.lua"));
  SessionGame game(*this);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(game.session->ver(), 1u);
  EXPECT_TRUE(contains(frontText().c_str(), "Taps: 0 of 5")) << frontText();
  EXPECT_TRUE(contains(frontText().c_str(), "solo, 1 seat, api 1")) << frontText();  // ctx

  // A tap on the banner is rejected; the event carries apply's reason to input.
  ASSERT_EQ(game.tap(100, 50), Outcome::Ok) << game.errorMessage();
  EXPECT_TRUE(contains(frontText().c_str(), "Not on the banner")) << frontText();
  EXPECT_EQ(game.session->ver(), 1u);

  for (int i = 1; i <= 5; ++i) {
    ASSERT_EQ(game.tap(100, static_cast<int16_t>(200 + i)), Outcome::Ok) << game.errorMessage();
    EXPECT_TRUE(contains(frontText().c_str(), "Taps: " + std::to_string(i) + " of 5")) << frontText();
  }
  EXPECT_EQ(game.session->ver(), 6u);
  EXPECT_TRUE(game.session->status().over);
  EXPECT_EQ(game.session->status().winners, 1u);
  EXPECT_TRUE(contains(frontText().c_str(), "Game over")) << frontText();
  EXPECT_TRUE(contains(frontText().c_str(), "Over events: 1")) << frontText();
  const DrawCommand marker = frontCommands().back();
  EXPECT_EQ(marker.op, Op::Rect);
  EXPECT_EQ(marker.y, 205 - 20);  // from the state, not ui

  // After game over, taps still reach input but their moves are discarded.
  ASSERT_EQ(game.tap(100, 300), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(game.session->ver(), 6u);
  EXPECT_TRUE(contains(frontText().c_str(), "Over events: 1")) << frontText();
  EXPECT_EQ(game.session->discardedMoves(), 1u);
}

// Play again (AD-21) as GameVM runs it: Session::start() and draw() on the same
// Session; ver keeps counting and the new round delivers `over` once more.
TEST_F(SessionGameTest, TheTracerPlaysAgainAfterGameOver) {
  useSource("main", readFixture("tracer/main.lua"));
  SessionGame game(*this);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  for (int i = 1; i <= 5; ++i) ASSERT_EQ(game.tap(100, 200), Outcome::Ok) << game.errorMessage();
  ASSERT_TRUE(game.session->status().over);
  ASSERT_EQ(game.session->ver(), 6u);

  ASSERT_EQ(game.session->start(), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.session->draw(), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(game.session->ver(), 7u);
  EXPECT_FALSE(game.session->status().over);
  EXPECT_TRUE(contains(frontText().c_str(), "Taps: 0 of 5")) << frontText();

  for (int i = 1; i <= 5; ++i) ASSERT_EQ(game.tap(100, 200), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(game.session->ver(), 12u);
  EXPECT_TRUE(game.session->status().over);
  // ui lives across rounds, so it has now counted one `over` per round.
  EXPECT_TRUE(contains(frontText().c_str(), "Over events: 2")) << frontText();
}

// Each band of fixtures/limits (the simulator's and the device's fault game for
// entries 8 to 10) ends the session with a ScriptError.
TEST_F(SessionGameTest, EveryLimitsFixtureBandIsAScriptError) {
  constexpr int TOP = 100;
  constexpr int BAND_HEIGHT = 110;
  const char* const messages[] = {
      "apply: state is too large (over 1400 bytes)",
      "input: move is too large (over 256 bytes)",
      "ch.store.set: the store is too large",
      "status.turn is 2, not a seat in 1..1",
      "frame is full",
      "boom",
  };
  for (int band = 0; band < 6; ++band) {
    useSource("main", readFixture("limits/main.lua"));
    SessionGame game(*this);
    ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
    const auto y = static_cast<int16_t>(TOP + band * BAND_HEIGHT + BAND_HEIGHT / 2);
    EXPECT_EQ(game.tap(100, y), Outcome::ScriptError) << "band " << band + 1;
    EXPECT_TRUE(contains(game.errorMessage(), messages[band])) << "band " << band + 1 << ": " << game.errorMessage();
  }
}

TEST_F(SessionGameTest, ChangesMadeOutsideApplyAreDiscardedAndUiPersists) {
  useSource("main", gameWith(R"(
    setup = function() return { n = 0 } end,
    status = function(s) s.n = 99 return { turn = 1 } end,
    apply = function(s) s.seen = s.n s.n = s.n + 1 return s end,
    draw = function(s, seat, ui)
      ch.gfx.text(0, 0, s.n .. ' ' .. tostring(s.seen) .. ' ' .. (ui.taps or 0), 'small', 'black')
      s.n = 1000
    end,
    input = function(s, seat, ui) s.n = 500 ui.taps = (ui.taps or 0) + 1 return {} end)"));
  SessionGame game(*this);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(frontText(), "0 nil 0");
  ASSERT_EQ(game.session->draw(), Outcome::Ok);
  EXPECT_EQ(frontText(), "0 nil 0");  // draw's change is gone
  ASSERT_EQ(game.tap(1, 1), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(frontText(), "1 0 1");  // apply saw neither input's nor status's change
  ASSERT_EQ(game.tap(1, 1), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(frontText(), "2 1 2");
}

TEST_F(SessionGameTest, ARejectionReachesInputAsAnEvent) {
  struct Case {
    const char* reason;  // Lua expression
    std::string expected;
  };
  std::string cut = "a";
  for (int i = 0; i < 31; ++i) cut += "\xC3\xA9";  // 63 bytes: the 64th would split an e-acute
  const Case cases[] = {
      {"'That square is taken'", "That square is taken"},
      {"nil", ""},
      {"42", "42"},
      {"string.rep('x', 70)", std::string(64, 'x')},
      {"'a' .. string.rep('\\u{e9}', 40)", cut},
  };
  for (const auto& c : cases) {
    useSource("main", gameWith(std::string("apply = function() return nil, ") + c.reason +
                               " end,\n"
                               "draw = function(s, seat, ui) ch.gfx.text(0, 0, ui.m or '-', 'small', 'black') end,\n"
                               "input = function(s, seat, ui, ev)\n"
                               "  if ev.kind == 'rejected' then ui.m = '[' .. ev.reason .. ']' return {} end\n"
                               "  return {} end"));
    SessionGame game(*this);
    ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
    ASSERT_EQ(game.tap(1, 1), Outcome::Ok) << c.reason << ": " << game.errorMessage();
    EXPECT_EQ(frontText(), "[" + c.expected + "]") << c.reason;
    EXPECT_EQ(game.session->ver(), 1u);
    EXPECT_EQ(game.session->discardedMoves(), 1u);  // the move returned for `rejected`
    EXPECT_FALSE(game.session->pending());
  }
  useSource("main", gameWith("apply = function() return nil, {} end"));
  SessionGame game(*this);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(game.tap(1, 1), Outcome::ScriptError);
  EXPECT_STREQ(game.errorMessage(), "apply: a rejection's reason must be a string, not a table");
}

TEST_F(SessionGameTest, AnInvalidStatusIsAScriptError) {
  struct Case {
    const char* status;
    const char* message;
  };
  const Case cases[] = {
      {"{ turn = 2 }", "status.turn is 2, not a seat in 1..1"},
      {"{ turn = 0 }", "status.turn is 0, not a seat in 1..1"},
      {"{}", "status.turn must be a seat in 1..1, not a nil"},
      {"{ turn = 1.5 }", "status.turn must be a seat in 1..1, not a number"},
      {"{ turn = '1' }", "status.turn must be a seat in 1..1, not a string"},
      {"7", "status must return a table, not a number"},
      {"{ over = true }", "status.winners must be a list of seats, not a nil"},
      {"{ over = true, winners = { 0 } }", "status.winners[1] is 0, not a seat in 1..1"},
      {"{ over = true, winners = { 1, 1 } }", "status.winners lists 2 seats; this match has 1"},
  };
  for (const auto& c : cases) {
    useSource("main", gameWith(std::string("status = function() return ") + c.status + " end"));
    SessionGame game(*this);
    EXPECT_EQ(game.start(), Outcome::ScriptError) << c.status;
    EXPECT_STREQ(game.errorMessage(), c.message) << c.status;
  }
  // Valid forms: a draw (no winners), and a turn given as an integral float.
  for (const char* status : {"{ over = true, winners = {} }", "{ turn = 1.0 }", "{ over = false, turn = 1 }"}) {
    useSource("main", gameWith(std::string("status = function() return ") + status + " end"));
    SessionGame game(*this);
    EXPECT_EQ(game.start(), Outcome::Ok) << status << ": " << game.errorMessage();
  }
}

TEST_F(SessionGameTest, StatesAndMovesOverTheirLimitsAreScriptErrors) {
  // {s = <n bytes>} encodes as 9 + n bytes when 128 <= n < 16384.
  const std::string atStateLimit = "{ s = string.rep('x', 1391) }";  // 1,400 B
  const std::string overStateLimit = "{ s = string.rep('x', 1392) }";
  const std::string atMoveLimit = "{ s = string.rep('x', 247) }";  // 256 B
  const std::string overMoveLimit = "{ s = string.rep('x', 248) }";
  struct Case {
    std::string fields;
    Outcome start;
    Outcome tap;
    const char* message;
  };
  const Case cases[] = {
      {"setup = function() return " + atStateLimit + " end", Outcome::Ok, Outcome::Ok, ""},
      {"setup = function() return " + overStateLimit + " end", Outcome::ScriptError, Outcome::Ok,
       "setup: state is too large (over 1400 bytes)"},
      {"apply = function() return " + overStateLimit + " end", Outcome::Ok, Outcome::ScriptError,
       "apply: state is too large (over 1400 bytes)"},
      {"input = function() return " + atMoveLimit + " end", Outcome::Ok, Outcome::Ok, ""},
      {"input = function() return " + overMoveLimit + " end", Outcome::Ok, Outcome::ScriptError,
       "input: move is too large (over 256 bytes)"},
  };
  for (const auto& c : cases) {
    useSource("main", gameWith(c.fields));
    SessionGame game(*this);
    ASSERT_EQ(game.start(), c.start) << c.fields << ": " << game.errorMessage();
    if (c.start == Outcome::Ok) {
      EXPECT_EQ(game.tap(1, 1), c.tap) << c.fields << ": " << game.errorMessage();
    }
    if (*c.message) {
      EXPECT_STREQ(game.errorMessage(), c.message) << c.fields;
    }
  }
}

TEST_F(SessionGameTest, ValuesTheContractRefusesAreScriptErrors) {
  struct Case {
    const char* fields;
    const char* message;
  };
  const Case cases[] = {
      {"setup = function() return 5 end", "setup must return a state table, not a number"},
      {"setup = function() return { f = print } end", "setup: state cannot be encoded (bad_type)"},
      {"setup = function() local t = {} t.t = t return t end", "setup: state cannot be encoded (cycle)"},
      {"setup = function() return setmetatable({}, {}) end", "setup: state cannot be encoded (metatable)"},
      {"setup = function() local s = {} local t = s for i = 2, 17 do t.c = {} t = t.c end return s end",
       "setup: state cannot be encoded (too_deep)"},
      {"apply = function() return 7 end", "apply must return a state table, or nil and a reason, not a number"},
      {"input = function() return true end", "input must return a move table or nil, not a boolean"},
      {"input = function() return { f = print } end", "input: move cannot be encoded (bad_type)"},
      {"status = 0", "game.status is not a function"},
      {"apply = false", "game.apply is not a function"},
  };
  for (const auto& c : cases) {
    useSource("main", gameWith(c.fields));
    SessionGame game(*this);
    Outcome outcome = game.start();
    if (outcome == Outcome::Ok) outcome = game.tap(1, 1);
    EXPECT_EQ(outcome, Outcome::ScriptError) << c.fields;
    EXPECT_STREQ(game.errorMessage(), c.message) << c.fields;
  }
  // A 16-deep state is fine, and so is a move of nil (no move).
  useSource("main",
            gameWith("setup = function() local s = {} local t = s for i = 2, 16 do t.c = {} t = t.c end return s end,"
                     "input = function() return nil end"));
  SessionGame game(*this);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.tap(1, 1), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(game.session->ver(), 1u);
  EXPECT_EQ(game.session->discardedMoves(), 0u);
}

TEST_F(SessionGameTest, TheSessionAndScratchAreTakenFromTheArenaBeforeLua) {
  useSource("main", gameWith(R"(
    setup = function()
      -- Fill the Lua heap to its cap, then drop it: encoding still has its scratch.
      local t = {}
      pcall(function() while true do t[#t + 1] = string.rep('x', 1000) .. #t end end)
      HELD = #t
      t = nil
      return { n = HELD }
    end,
    draw = function(s) ch.gfx.text(0, 0, tostring(s.n), 'small', 'black') end)"));
  SessionGame game(*this);
  ASSERT_NE(game.session, nullptr);
  const auto* base = arenaBlock.data();
  const auto* session = reinterpret_cast<const uint8_t*>(game.session);
  EXPECT_GE(session, base);
  EXPECT_LT(session, base + arenaBlock.size());
  const size_t beforeLua = arena.bytesInUse();
  EXPECT_GE(beforeLua, sizeof(GameCore::Session));
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  EXPECT_GT(std::stoi(frontText()), 200);  // about 250 strings of 1 KB filled the cap
  EXPECT_GE(arena.peakBytes(), LUA_HEAP_BYTES);
  EXPECT_LE(arena.peakBytes(), ARENA_BYTES);
  game.game.close();
  EXPECT_EQ(arena.bytesInUse(), beforeLua);  // the scratch went back with the state
}

// Runs fn on a thread whose stack is painted, and returns the bytes it touched.
size_t stackBytesUsed(const std::function<void()>& fn) {
  constexpr size_t STACK_BYTES = 512 * 1024;
  std::vector<uint8_t> stack(STACK_BYTES, 0xA5);
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstack(&attr, stack.data(), stack.size());
  pthread_t thread;
  auto body = [](void* arg) -> void* {
    (*static_cast<const std::function<void()>*>(arg))();
    return nullptr;
  };
  EXPECT_EQ(pthread_create(&thread, &attr, body, const_cast<std::function<void()>*>(&fn)), 0);
  pthread_join(thread, nullptr);
  pthread_attr_destroy(&attr);
  size_t untouched = 0;
  while (untouched < stack.size() && stack[untouched] == 0xA5) ++untouched;
  return stack.size() - untouched;
}

TEST_F(SessionGameTest, MeasuresTheCodecStackAtItsDepthLimit) {
  // Encode and decode alone, as LuaGame runs them inside the trampoline, for a
  // 16-deep state against a flat one; the difference is the codec's recursion.
  const auto run = [](int depth) {
    lua_State* L = luaL_newstate();
    const std::string build =
        "local s = {} local t = s for i = 2, " + std::to_string(depth) + " do t.c = { n = i } t = t.c end return s";
    EXPECT_EQ(luaL_dostring(L, build.c_str()), LUA_OK);
    std::vector<uint8_t> scratch(LuaGame::SCRATCH_BYTES);
    const size_t used = stackBytesUsed([&] {
      const Codec::Encoded encoded = Codec::encode(L, -1, Codec::SNAPSHOT_LIMIT, scratch.data(), scratch.size());
      EXPECT_EQ(encoded.error, Codec::Error::None);
      EXPECT_EQ(Codec::decode(L, encoded.data, encoded.length, Codec::SNAPSHOT_LIMIT), Codec::Error::None);
    });
    lua_close(L);
    return used;
  };
  // The thread's own start-up (and glibc's descriptor and TLS at the stack's top).
  const size_t idle = stackBytesUsed([] {});
  // A flat state stays under that start-up, so `deep` is a lower bound on the
  // codec's own use at depth 16.
  const auto beyondIdle = [idle](const size_t used) { return used > idle ? used - idle : 0; };
  const size_t flat = beyondIdle(run(1));
  const size_t deep = beyondIdle(run(Codec::MAX_DEPTH));
  const size_t perLevel = (deep - flat) / (Codec::MAX_DEPTH - 1);
  std::cout << "[stack] codec at depth 16: at least " << deep << " B, about " << perLevel
            << " B a level on this host\n";
  RecordProperty("codec_depth16_bytes", static_cast<int>(deep));
  RecordProperty("codec_bytes_per_level", static_cast<int>(perLevel));
  EXPECT_GT(deep, flat);
  // It runs in the trampoline, a few KB above the task stack's base, so it must stay
  // well inside the 16 KiB with room for the rest of the call.
  EXPECT_LT(deep, 6u * 1024);
}

}  // namespace
