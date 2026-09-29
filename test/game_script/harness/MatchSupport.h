#pragma once

// The rig the match suites (GameVmTest, GameMatchTest) share: a game placed on the fake
// card, the renderer and input a screen is given, waiting on a game running on its own
// thread, and a read of the match's state through what it logs and draws. The doubles
// themselves are in screen_stubs/ (entry 5 and entry 11 build on them).

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "FakeRtos.h"
#include "GameVM.h"
#include "HarnessSupport.h"
#include "Logging.h"
#include "MappedInputManager.h"
#include "RenderLockProbe.h"
#include "activities/games/GameMatchActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHost.h"
#include "fontIds.h"

namespace match {

using harness::Bytes;

// The fixture games (test/game_script/fixtures), the ones a package can hold: a folder of
// manifest.json, Lua, and images is copied onto the fake card as /.games/<name>/.
inline void installFixture(const std::string& name) {
  const std::filesystem::path root = std::filesystem::path(MATCH_FIXTURES_DIR) / name;
  if (!std::filesystem::is_directory(root)) {
    ADD_FAILURE() << "no fixture game " << root;
    return;
  }
  for (const auto& entry : std::filesystem::directory_iterator(root)) {
    if (!entry.is_regular_file()) continue;  // solo/screenshots: the loader ignores a folder too
    std::ifstream in(entry.path(), std::ios::binary);
    const Bytes bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    fakesd::addFile("/.games/" + name + "/" + entry.path().filename().string(), bytes);
  }
}

// A game of the test's own: `main.lua` is `source`.
inline void installGame(const std::string& id, const std::string& source) {
  fakesd::addFile("/.games/" + id + "/main.lua", source);
}

// A game whose round ends at its first tap, saving `taps = 1` on the way, so the end of a
// round and the store can be checked with no timing at all.
inline const char* const ONE_TAP_GAME = R"(
local game = {}
function game.setup(ctx) return { taps = 0 } end
function game.status(state)
  if state.taps >= 1 then return { over = true, winners = { 1 } } end
  return { turn = 1 }
end
function game.apply(state, seat, move)
  state.taps = state.taps + 1
  ch.store.set({ taps = state.taps })
  return state
end
function game.draw(state, seat, ui) ch.gfx.clear("white") end
function game.input(state, seat, ui, ev)
  if ev.kind == "tap" then return { tap = true } end
end
return game
)";

// A game that logs every tap, long press, and swipe it is given, with the canvas point and the
// swipe's direction ("-" for the others): what readGesture and GameTouch made of the input.
inline const char* const EVENTS_GAME = R"(
local game = {}
function game.setup(ctx) return {} end
function game.status(state) return { turn = 1 } end
function game.apply(state, seat, move) return state end
function game.draw(state, seat, ui) ch.gfx.clear("white") end
function game.input(state, seat, ui, ev)
  if ev.kind == "tap" or ev.kind == "long_press" or ev.kind == "swipe" then
    ch.log("event", ev.kind, ev.x, ev.y, ev.dir or "-")
    return { seen = true }
  end
end
return game
)";

// A game whose tap handler reads the clock (so a held call can be released) and then fails.
inline const char* const LATE_ERROR_GAME = R"(
local game = {}
function game.setup(ctx) return {} end
function game.status(state) return { turn = 1 } end
function game.apply(state, seat, move) return state end
function game.draw(state, seat, ui) ch.gfx.clear("white") end
function game.input(state, seat, ui, ev)
  if ev.kind == "tap" then
    ch.time.ms()
    error("late boom")
  end
end
return game
)";

// A game that draws one fixed frame however often it is asked to, so a frame identical to
// the one on screen can be published on demand (every input redraws).
inline const char* const STILL_GAME = R"(
local game = {}
function game.setup(ctx) return {} end
function game.status(state) return { turn = 1 } end
function game.apply(state, seat, move) return state end
function game.draw(state, seat, ui)
  ch.gfx.clear("white")
  ch.gfx.rect(10, 10, 50, 50, "black", true)
end
function game.input(state, seat, ui, ev)
  if ev.kind == "tap" then return { tap = true } end
end
return game
)";

// A game whose tap handler logs, then draws: the VM can be held inside ch.log (a locked
// binding) or, with the clock read first, outside one.
inline const char* const LOGGING_GAME = R"(
local game = {}
function game.setup(ctx) return {} end
function game.status(state) return { turn = 1 } end
function game.apply(state, seat, move) return state end
function game.draw(state, seat, ui) ch.gfx.clear("white") end
function game.input(state, seat, ui, ev)
  if ev.kind == "tap" then
    ch.log("input entered")
    return { tap = true }
  end
end
return game
)";

// The tracer's round with `goal` taps, made to be held: a tap's input reads the clock, and setup
// reads it from the second round on, so a test's gate (fakertos::arm) can hold a step in flight, or
// the new round's setup, at the moment it wants. A tap below y = 130 moves; the square is drawn at
// the last tap.
inline std::string gatedGame(const int goal) {
  return std::string(R"(
local game = {}
local GOAL = )") +
         std::to_string(goal) + R"(
local round = 0
function game.setup(ctx)
  round = round + 1
  if round > 1 then ch.time.ms() end
  return { taps = 0, round = round }
end
function game.status(state)
  if state.taps >= GOAL then return { over = true, winners = { 1 } } end
  return { turn = 1 }
end
function game.apply(state, seat, move)
  state.taps = state.taps + 1
  state.x, state.y = move.x, move.y
  return state
end
function game.draw(state, seat, ui)
  ch.gfx.clear("white")
  ch.gfx.text(40, 50, "Round " .. state.round .. ", taps: " .. state.taps, "medium", "black")
  if state.x then ch.gfx.rect(state.x - 20, state.y - 20, 40, 40, "black", true) end
end
function game.input(state, seat, ui, ev)
  if ev.kind == "tap" then
    ch.log("tap", ev.x, ev.y)
    ch.time.ms()
    return { x = ev.x, y = ev.y }
  end
end
return game
)";
}

// Polls `done` (sleeping 1 ms between) until it is true or the timeout passes.
inline bool waitFor(const std::function<bool()>& done, const int timeoutMs = 10000) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (!done()) {
    if (std::chrono::steady_clock::now() > end) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return true;
}

// The manifest of a game the fixtures hold (what the launcher passes the match).
inline GameCore::Manifest manifestOf(const std::string& id, const std::string& name = "") {
  GameCore::Manifest manifest;
  std::snprintf(manifest.id, sizeof(manifest.id), "%s", id.c_str());
  std::snprintf(manifest.name, sizeof(manifest.name), "%s", name.empty() ? id.c_str() : name.c_str());
  std::snprintf(manifest.version, sizeof(manifest.version), "1.0.0");
  manifest.api = 1;
  manifest.seatsMin = 1;
  manifest.seatsMax = 1;
  manifest.modes = GameCore::Manifest::MODE_SOLO;
  return manifest;
}

// The three built-in fonts FrameReplay::loadFonts reads, as the double models them: 9, 11, and 17
// px a glyph (small, medium, large), on lines 20, 24, and 30 px high. None equals the stand-in
// advance the replay measures with before loadFonts (8, 10, 14), so a game that measures text
// tells which metrics it was given.
constexpr int SMALL_ADVANCE = 9;
constexpr int MEDIUM_ADVANCE = 11;
constexpr int LARGE_ADVANCE = 17;
inline void addFonts(GfxRenderer& renderer) {
  renderer.addFont(UI_10_FONT_ID, 20, SMALL_ADVANCE);
  renderer.addFont(UI_12_FONT_ID, 24, MEDIUM_ADVANCE);
  renderer.addFont(NOTOSANS_18_FONT_ID, 30, LARGE_ADVANCE);
}

// The state every test starts from: an empty card and log, no PSRAM blocks, the fake clock
// at 1000 ms, the manager's asks and the lock's counts at zero, and a renderer, the
// scripted input, and the theme's hint record. The end checks what no test should leave: a
// task still running, a notify to an ended task, a second RenderLock by its holder (12cc816).
class ScreenTest : public harness::HarnessTest {
 protected:
  void SetUp() override {
    HarnessTest::SetUp();
    fakertos::reset();
    fakelog::hook() = nullptr;
    activityManager.reset();
    UITheme::getInstance().getTheme().hints.clear();
    gpio.swipe = HalGPIO::Swipe{};
    renderer = std::make_unique<GfxRenderer>(480, 800);
    addFonts(*renderer);
    input = std::make_unique<MappedInputManager>(gpio, *renderer);
  }

  void TearDown() override {
    fakertos::release();  // a test that failed while holding a task must not leave it held
    fakelog::hook() = nullptr;
    // A test that ends with a task still up would have it write the fake log and PSRAM
    // counters under the next test.
    EXPECT_TRUE(fakertos::waitNoTasks()) << "a VM task was still running at the end of the test";
    EXPECT_EQ(fakertos::S().deadNotifies.load(), 0) << "a notify reached a task that had ended";
    if (expectNoSelfDeadlock) {
      EXPECT_EQ(fakelock::selfDeadlocks.load(), 0) << "RenderLock was taken by the thread that held it";
    }
    HarnessTest::TearDown();
  }

  bool logHas(const std::string& part) const { return fakelog::anyLine(part); }

  std::unique_ptr<GfxRenderer> renderer;
  std::unique_ptr<MappedInputManager> input;
  bool expectNoSelfDeadlock = true;
};

}  // namespace match
