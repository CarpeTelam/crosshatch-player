#include <PauseClock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <climits>
#include <ctime>
#include <string>
#include <vector>

#include "GameInput.h"
#include "GameTimer.h"
#include "LuaGameFixture.h"

using namespace GameScript;
using GameScriptTestSupport::DirectGame;
using GameScriptTestSupport::FixedRandom;

// Counts every time() call in this test binary, Lua's included: the executable's
// definition wins over the C library's. It still answers with the real time.
namespace {
std::atomic<int> timeCalls{0};
}  // namespace

extern "C" time_t time(time_t* out) noexcept {
  timeCalls.fetch_add(1);
  timespec now{};
  clock_gettime(CLOCK_REALTIME, &now);
  if (out) *out = now.tv_sec;
  return now.tv_sec;
}

namespace {

// Draws ui.text (or state.text) as the frame's one text command.
constexpr const char* DRAW_UI_TEXT =
    "  draw = function(s, seat, ui) ch.gfx.text(0, 0, tostring(ui.text or s.text), 'small', 'black') end,\n";

class HostBindingsTest : public GameScriptTestSupport::LuaGameTest {
 protected:
  // GameVM::pollTimer: queues the Timer event takeDueEvent makes, if the timer is
  // due now.
  bool pollTimer(LuaGame& game, InputQueue& queue) {
    InputEvent event;
    if (!game.timer().takeDueEvent(clock.nowMs(), event)) return false;
    queue.push(event);
    return true;
  }

  // GameVM::run's handling of one queued event (MatchRounds::step, which drops a
  // stale timer event); false when the queue was empty or the event was stale.
  bool deliverNext(SessionGame& game, InputQueue& queue) {
    InputEvent event;
    if (!queue.pop(event)) return false;
    const bool fresh = game.game.timer().accepts(event);
    EXPECT_EQ(game.step(event), Outcome::Ok) << game.errorMessage();
    return fresh;
  }

  // Runs setup, then input with a tap whose handler is `body`, then draw; returns
  // the drawn text or "error: <message>".
  std::string tapWith(const std::string& body) {
    useSource("main", "return { setup = function() return {} end,\n" + std::string(DRAW_UI_TEXT) +
                          "  input = function(s, seat, ui, ev)\n" + body + "\n  end }");
    DirectGame game(arena, frames, sources, ports, canvas);
    modelTaskStack(game);
    Outcome outcome = game.start();
    if (outcome == Outcome::Ok) outcome = game.input(InputEvent{InputKind::Tap, 1, 1});
    if (outcome == Outcome::Ok) outcome = game.draw();
    if (outcome != Outcome::Ok) return std::string("error: ") + game.errorMessage();
    return frontText();
  }
};

TEST_F(HostBindingsTest, ChApiIsTheApiLevel) {
  EXPECT_EQ(tapWith("ui.text = ch.api .. ' ' .. math.type(ch.api)"), "1 integer");
}

TEST_F(HostBindingsTest, TimeMsCountsFromLoad) {
  useSource("main",
            "local atLoad = ch.time.ms()\n"
            "return { setup = function() return {} end,\n" +
                std::string(DRAW_UI_TEXT) +
                "  input = function(s, seat, ui) ui.text = atLoad .. ' ' .. ch.time.ms() end }");
  DirectGame game(arena, frames, sources, ports, canvas);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  clock.advance(1234);
  ASSERT_EQ(game.input(InputEvent{InputKind::Tap, 1, 1}), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(frontText(), "0 1234");
}

// setup arms 1500 ms; a tap runs `onTap`; a timer event returns a move that apply
// counts. draw shows the count.
constexpr const char* TIMER_GAME =
    "return {\n"
    "  setup = function() ch.timer.after(1500) return { n = 0 } end,\n"
    "  status = function(s) return { turn = 1 } end,\n"
    "  apply = function(s, seat, m) s.n = s.n + 1 return s end,\n"
    "  draw = function(s) ch.gfx.text(0, 0, 'ticks ' .. s.n, 'small', 'black') end,\n"
    "  input = function(s, seat, ui, ev)\n"
    "    if ev.kind == 'timer' then return { tick = true } end\n"
    "    if ev.kind == 'tap' then ON_TAP end\n"
    "  end }";

std::string timerGame(const std::string& onTap) {
  std::string text = TIMER_GAME;
  text.replace(text.find("ON_TAP"), 6, onTap);
  return text;
}

// ch.time.ms is play time: the match's pause ledger (HostPorts::paused) takes the Paused intervals out, and ch.timer
// stays on the raw clock.
class PlayTimeTest : public HostBindingsTest {
 protected:
  // A game that shows ch.time.ms on a tap.
  void useClockGame() {
    useSource("main", "return { setup = function() return {} end,\n" + std::string(DRAW_UI_TEXT) +
                          "  input = function(s, seat, ui) ui.text = ch.time.ms() end }");
  }
  // Taps and returns what the game drew for ch.time.ms.
  std::string readClock(DirectGame& game) {
    EXPECT_EQ(game.input(InputEvent{InputKind::Tap, 1, 1}), Outcome::Ok) << game.errorMessage();
    EXPECT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
    return frontText();
  }

  GameCore::PauseClock pauses;
};

TEST_F(PlayTimeTest, ThePausedIntervalIsLeftOutOfTimeMs) {
  useClockGame();
  ports.paused = &pauses;
  DirectGame game(arena, frames, sources, ports, canvas);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  clock.advance(5000);
  pauses.enter(clock.nowMs());
  clock.advance(30000);
  pauses.leave(clock.nowMs());
  clock.advance(2000);
  EXPECT_EQ(readClock(game), "7000");
}

TEST_F(PlayTimeTest, AReadWhilePausedIsFrozenAtTheValueWhenThePauseBegan) {
  useClockGame();
  ports.paused = &pauses;
  DirectGame game(arena, frames, sources, ports, canvas);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  clock.advance(5000);
  pauses.enter(clock.nowMs());
  clock.advance(30000);
  EXPECT_EQ(readClock(game), "5000");
}

TEST_F(PlayTimeTest, TwoPausesBothLeaveTheirIntervalOut) {
  useClockGame();
  ports.paused = &pauses;
  DirectGame game(arena, frames, sources, ports, canvas);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  clock.advance(1000);
  pauses.enter(clock.nowMs());
  clock.advance(20000);
  pauses.leave(clock.nowMs());
  clock.advance(1000);
  pauses.enter(clock.nowMs());
  clock.advance(10000);
  pauses.leave(clock.nowMs());
  clock.advance(1000);
  EXPECT_EQ(readClock(game), "3000");
}

TEST_F(PlayTimeTest, WithALedgerAndNoPauseTimeMsStillCountsFromLoad) {
  useClockGame();
  ports.paused = &pauses;
  DirectGame game(arena, frames, sources, ports, canvas);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(readClock(game), "0");
  clock.advance(250);
  EXPECT_EQ(readClock(game), "250");
}

TEST_F(PlayTimeTest, ATimerSetBeforeAPauseFiresOnTheRawClock) {
  useSource("main", timerGame(""));  // setup arms 1500 ms
  ports.paused = &pauses;
  SessionGame game(*this);
  InputQueue& queue = game.queue;
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  clock.advance(1000);
  pauses.enter(clock.nowMs());
  clock.advance(499);
  EXPECT_FALSE(pollTimer(game.game, queue));
  clock.advance(1);
  ASSERT_TRUE(pollTimer(game.game, queue));  // due at 1500 raw ms, the match paused or not
  ASSERT_TRUE(deliverNext(game, queue));
  EXPECT_EQ(frontText(), "ticks 1");
}

TEST_F(PlayTimeTest, WithNoLedgerTimeMsIsTheRawElapsedTime) {
  useClockGame();
  ASSERT_EQ(ports.paused, nullptr);
  DirectGame game(arena, frames, sources, ports, canvas);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  clock.advance(5000);
  pauses.enter(clock.nowMs());  // a ledger the host did not hand over
  clock.advance(30000);
  pauses.leave(clock.nowMs());
  clock.advance(2000);
  EXPECT_EQ(readClock(game), "37000");
}

TEST_F(HostBindingsTest, ATimerReachesInputThroughTheQueueOnce) {
  useSource("main", timerGame(""));
  SessionGame game(*this);
  InputQueue& queue = game.queue;
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  clock.advance(1499);
  EXPECT_FALSE(pollTimer(game.game, queue));
  clock.advance(1);
  ASSERT_TRUE(pollTimer(game.game, queue));
  ASSERT_TRUE(deliverNext(game, queue));
  EXPECT_EQ(frontText(), "ticks 1");  // the timer's move went through apply
  clock.advance(10000);
  EXPECT_FALSE(pollTimer(game.game, queue));
}

// pollTimer disarms the timer as it queues the event, so a burst of taps that
// overflows the queue must drop taps, not the event (the retro's R2).
TEST_F(HostBindingsTest, ATimerEventSurvivesATapBurstThatFillsTheQueue) {
  useSource("main", timerGame(""));
  SessionGame game(*this);
  InputQueue& queue = game.queue;
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  queue.push(InputEvent{InputKind::Tap, 1, 1});
  clock.advance(1500);
  ASSERT_TRUE(pollTimer(game.game, queue));
  EXPECT_FALSE(game.game.timer().pending());
  int dropped = 0;
  for (int i = 0; i < static_cast<int>(INPUT_QUEUE_DEPTH) * 2; ++i) {
    dropped += queue.push(InputEvent{InputKind::Tap, 1, 1}) ? 1 : 0;
  }
  EXPECT_EQ(dropped, static_cast<int>(INPUT_QUEUE_DEPTH) + 2);
  int delivered = 0;
  while (deliverNext(game, queue)) ++delivered;
  EXPECT_EQ(delivered, static_cast<int>(INPUT_QUEUE_DEPTH));
  EXPECT_EQ(frontText(), "ticks 1");  // the timer's move went through apply
}

TEST_F(HostBindingsTest, ANewTimerReplacesThePendingOne) {
  useSource("main", timerGame("ch.timer.after(1500)"));
  SessionGame game(*this);
  InputQueue& queue = game.queue;
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();  // due at 1500
  clock.advance(1000);
  ASSERT_EQ(game.tap(1, 1), Outcome::Ok) << game.errorMessage();  // now due at 2500
  clock.advance(1499);
  EXPECT_FALSE(pollTimer(game.game, queue));
  clock.advance(1);
  ASSERT_TRUE(pollTimer(game.game, queue));
  ASSERT_TRUE(deliverNext(game, queue));
  clock.advance(10000);
  EXPECT_FALSE(pollTimer(game.game, queue));
  EXPECT_EQ(frontText(), "ticks 1");
}

TEST_F(HostBindingsTest, CancelClearsThePendingTimer) {
  useSource("main", timerGame("ch.timer.cancel()"));
  SessionGame game(*this);
  InputQueue& queue = game.queue;
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  ASSERT_EQ(game.tap(1, 1), Outcome::Ok) << game.errorMessage();
  clock.advance(10000);
  EXPECT_FALSE(pollTimer(game.game, queue));
  EXPECT_EQ(frontText(), "ticks 0");
}

TEST_F(HostBindingsTest, AnEventFiredBeforeACancelIsDropped) {
  useSource("main", timerGame("ch.timer.cancel()"));
  SessionGame game(*this);
  InputQueue& queue = game.queue;
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  clock.advance(1500);
  ASSERT_TRUE(pollTimer(game.game, queue));  // fired and queued...
  ASSERT_EQ(game.tap(1, 1), Outcome::Ok);    // ...but a tap ahead of it cancels
  EXPECT_FALSE(deliverNext(game, queue));    // so input never sees it
  ASSERT_EQ(game.session->draw(1), Outcome::Ok);
  EXPECT_EQ(frontText(), "ticks 0");
}

TEST_F(HostBindingsTest, TheTimerFixtureTicksThreeTimesAndSaves) {
  useSource("main", GameScriptTestSupport::readFixture("timer/main.lua"));
  SessionGame game(*this);
  InputQueue& queue = game.queue;
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  EXPECT_TRUE(hasText(frontCommands(), "Ticks: 0 of 3"));
  for (int tick = 1; tick <= 3; ++tick) {
    clock.advance(2999);
    EXPECT_FALSE(pollTimer(game.game, queue)) << tick;
    clock.advance(1);
    ASSERT_TRUE(pollTimer(game.game, queue)) << tick;
    ASSERT_TRUE(deliverNext(game, queue)) << tick;
    EXPECT_TRUE(hasText(frontCommands(), "Ticks: " + std::to_string(tick) + " of 3")) << frontText();
    EXPECT_TRUE(hasText(frontCommands(), "Saved ticks: " + std::to_string(tick))) << frontText();
  }
  EXPECT_TRUE(game.session->status().over);
  clock.advance(10000);
  EXPECT_FALSE(pollTimer(game.game, queue));
  EXPECT_EQ(log.lines.front(), "armed\t3000\tms; api\t1");
  EXPECT_EQ(log.lines.back(), "tick at\t9000\tms");
}

// The counter fixture reads what GameSaveStore restored (here {taps = 3}) in setup,
// and each tap posts the next count.
TEST_F(HostBindingsTest, TheCounterFixtureStartsFromTheRestoredStore) {
  const std::vector<uint8_t> saved = {0x06, 0x00, 0x01, 0x05, 0x04, 't', 'a', 'p', 's', 0x03, 0x06};
  ASSERT_TRUE(store.restore(saved));
  useSource("main", GameScriptTestSupport::readFixture("counter/main.lua"));
  SessionGame game(*this);
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  EXPECT_TRUE(hasText(frontCommands(), "Taps: 3")) << frontText();
  EXPECT_TRUE(hasText(frontCommands(), "Saved taps: 3")) << frontText();
  EXPECT_FALSE(store.dirty());

  ASSERT_EQ(game.tap(10, 10), Outcome::Ok) << game.errorMessage();
  EXPECT_TRUE(hasText(frontCommands(), "Saved taps: 4")) << frontText();
  std::vector<uint8_t> out(GameScript::Codec::STORE_LIMIT);
  const std::vector<uint8_t> four = {0x06, 0x00, 0x01, 0x05, 0x04, 't', 'a', 'p', 's', 0x03, 0x08};
  ASSERT_EQ(store.takeIfDirty(out), four.size());
  EXPECT_EQ(std::vector<uint8_t>(out.begin(), out.begin() + four.size()), four);
}

TEST_F(HostBindingsTest, DelaysUnderASecondAreScriptErrors) {
  for (const char* delay : {"999", "0", "-1"}) {
    EXPECT_EQ(tapWith(std::string("ch.timer.after(") + delay + ")"),
              "error: main.lua:4: bad argument #1 to 'after' (at least 1000 ms)")
        << delay;
  }
  EXPECT_EQ(tapWith("ch.timer.after(1.5)"),
            "error: main.lua:4: bad argument #1 to 'after' (number has no integer representation)");
  EXPECT_EQ(tapWith("ch.timer.after(1000) ui.text = 'ok'"), "ok");
}

TEST_F(HostBindingsTest, TheStoreRoundTripsAndStartsEmpty) {
  EXPECT_EQ(tapWith("ui.text = tostring(next(ch.store.get()))"), "nil");
  EXPECT_EQ(tapWith("ch.store.set({ best = 7, name = 'ann', list = { 1, 2 } })\n"
                    "local a, b = ch.store.get(), ch.store.get()\n"
                    "a.best = 0\n"
                    "ui.text = b.best .. ' ' .. b.name .. ' ' .. #b.list .. ' ' .. tostring(a ~= b)"),
            "7 ann 2 true");
}

TEST_F(HostBindingsTest, TheStoreIsDirtyOnlyWhenItsBytesChange) {
  std::vector<uint8_t> out(Codec::STORE_LIMIT);
  EXPECT_EQ(tapWith("ch.store.set({ n = 1 }) ch.store.set({ n = 1 }) ui.text = 'ok'"), "ok");
  EXPECT_GT(store.takeIfDirty(out), 0u);
  EXPECT_EQ(tapWith("ch.store.set({ n = 1 }) ui.text = 'ok'"), "ok");
  EXPECT_FALSE(store.dirty());
  EXPECT_EQ(tapWith("ch.store.set({ n = 2 }) ui.text = ch.store.get().n"), "2");
  EXPECT_TRUE(store.dirty());
}

TEST_F(HostBindingsTest, AStoreOverFourKilobytesIsAStickyScriptError) {
  // {s = <n bytes>} encodes as 9 + n bytes for n in 128..16383.
  EXPECT_EQ(tapWith("ch.store.set({ s = string.rep('x', 4087) }) ui.text = 'fits'"), "fits");
  std::vector<uint8_t> out(Codec::STORE_LIMIT);
  store.takeIfDirty(out);
  constexpr const char* TOO_LARGE = "error: main.lua:4: ch.store.set: the store is too large (over 4096 bytes)";
  EXPECT_EQ(tapWith("ch.store.set({ s = string.rep('x', 4088) })"), TOO_LARGE);
  EXPECT_EQ(tapWith("pcall(ch.store.set, { s = string.rep('x', 5000) }) ui.text = 'kept going'"),
            "error: ch.store.set: the store is too large (over 4096 bytes)");
  EXPECT_EQ(tapWith("pcall(function() ch.store.set({ s = string.rep('x', 5000) }) end) ui.text = 'kept going'"),
            TOO_LARGE);
  EXPECT_FALSE(store.dirty());
}

TEST_F(HostBindingsTest, OtherBadStoresAreErrors) {
  EXPECT_EQ(tapWith("ch.store.set(5)"), "error: main.lua:4: bad argument #1 to 'set' (table expected, got number)");
  EXPECT_EQ(tapWith("pcall(ch.store.set, { f = print }) ui.text = 'kept going'"),
            "error: ch.store.set: the store cannot be encoded (bad_type)");
  EXPECT_FALSE(store.dirty());
}

TEST_F(HostBindingsTest, PrintAndLogWriteOneBoundedLine) {
  EXPECT_EQ(tapWith("print('a', 1, nil, true) ch.log(setmetatable({}, { __tostring = function() return 'T' end }))\n"
                    "ch.log() ch.log('two\\nlines\\0!') ui.text = 'ok'"),
            "ok");
  EXPECT_EQ(log.lines, (std::vector<std::string>{"a\t1\tnil\ttrue", "T", "", "two lines !"}));

  log.lines.clear();
  EXPECT_EQ(tapWith("print(string.rep('x', 300)) ch.log(string.rep('x', 159) .. '\\u{E9}') ch.log('a', string.rep('y', "
                    "400), 'b') ui.text = 'ok'"),
            "ok");
  ASSERT_EQ(log.lines.size(), 3u);
  EXPECT_EQ(log.lines[0], std::string(LOG_LINE_BYTES, 'x'));
  EXPECT_EQ(log.lines[1], std::string(159, 'x'));  // the 2-byte character does not fit
  EXPECT_EQ(log.lines[2], "a\t" + std::string(LOG_LINE_BYTES - 2, 'y'));
}

TEST_F(HostBindingsTest, TheLogIsWrittenInsideALockedSection) {
  useSource("main", "print('at load')\nreturn { setup = function() ch.log('in setup') return {} end }");
  DirectGame game(arena, frames, sources, ports, canvas);
  log.watched = &game;
  ASSERT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
  EXPECT_EQ(log.lockedAtWrite, (std::vector<bool>{true, true}));
  EXPECT_FALSE(game.inLockedBinding());
}

TEST_F(HostBindingsTest, LogAndStoreNeedStackHeadroom) {
  // Each level nests a pcall (about 800 B of C stack), so the depth where these
  // bindings refuse (under 4 KiB free) comes before the guard's 2 KiB floor. The
  // refusal is a guard fault: the script's pcall cannot catch it, so the tap ends
  // in a ScriptError instead of drawing the message. Its text is a literal, with
  // no chunk and line: nothing is formatted on the short stack.
  struct Case {
    const char* call;
    const char* message;
  };
  const Case cases[] = {
      {"print(d)", "ch.log: script recursion too deep to call it"},
      {"ch.store.get()", "ch.store.get: script recursion too deep to call it"},
      {"ch.store.set({})", "ch.store.set: script recursion too deep to call it"},
  };
  for (const auto& [call, message] : cases) {
    const std::string body = std::string(
                                 "local function dive(d)\n"
                                 "  local ok, e = pcall(function() ") +
                             call +
                             " end)\n"
                             "  if not ok then return e end\n"
                             "  return select(2, pcall(dive, d + 1))\n"
                             "end\n"
                             "ui.text = dive(1)";
    EXPECT_EQ(tapWith(body), std::string("error: ") + message) << call;
  }
}

TEST_F(HostBindingsTest, RandomseedWithoutArgumentsNeverCallsTime) {
  useSource("main", "return { setup = function() return {} end,\n" + std::string(DRAW_UI_TEXT) +
                        "  input = function(s, seat, ui)\n"
                        "    math.randomseed()\n"
                        "    ui.text = math.random(1, 1000000000) end }");
  auto seededDraw = [&](GameCore::IRandom& rng, int& callsDuringInput) {
    const HostPorts withRng{rng, clock, log, store};
    DirectGame game(arena, frames, sources, withRng, canvas);
    const int atStart = timeCalls.load();
    EXPECT_EQ(game.start(), Outcome::Ok) << game.errorMessage();
    // luaopen_math seeds itself with time() once, in load(), before game code: this
    // also shows the counter sees Lua's calls.
    EXPECT_EQ(timeCalls.load() - atStart, 1);
    const int before = timeCalls.load();
    EXPECT_EQ(game.input(InputEvent{InputKind::Tap, 1, 1}), Outcome::Ok) << game.errorMessage();
    callsDuringInput = timeCalls.load() - before;
    EXPECT_EQ(game.draw(), Outcome::Ok) << game.errorMessage();
    return frontText();
  };
  FixedRandom one(1);
  FixedRandom again(1);
  FixedRandom other(2);
  int calls = -1;
  const std::string first = seededDraw(one, calls);
  EXPECT_EQ(calls, 0);
  EXPECT_EQ(seededDraw(again, calls), first);  // the seed came from IRandom
  EXPECT_NE(seededDraw(other, calls), first);

  // With an argument it is Lua's: the seeds are returned and the sequence repeats.
  EXPECT_EQ(tapWith("local a, b = math.randomseed(42) local x = math.random(1000)\n"
                    "math.randomseed(42) ui.text = a .. ' ' .. b .. ' ' .. tostring(x == math.random(1000))"),
            "42 0 true");
}

}  // namespace
