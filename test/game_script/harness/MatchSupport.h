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
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "FakeRtos.h"
#include "GameIconDraw.h"
#include "GameMarkBitmaps.h"
#include "GameVM.h"
#include "HarnessSupport.h"
#include "Logging.h"
#include "MappedInputManager.h"
#include "RenderLockProbe.h"
#include "activities/games/GameMatchActivity.h"
#include "activities/games/GameSplashLayout.h"
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

// A game whose tap handler spins in Lua, reading the clock each pass (so a suspended task parks
// there at once): a call that is busy in Lua and never returns until cancelled.
inline const char* const SPIN_GAME = R"(
local game = {}
function game.setup(ctx) return {} end
function game.status(state) return { turn = 1 } end
function game.apply(state, seat, move) return state end
function game.draw(state, seat, ui) ch.gfx.clear("white") end
function game.input(state, seat, ui, ev)
  if ev.kind == "tap" then
    while true do ch.time.ms() end
  end
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

// True once the VM has logged that its first round started (it logs after publishing the first
// frame, so a gate armed earlier can be met by that line instead of the one a test means).
inline bool roundStarted() { return fakelog::anyLine("Round started"); }

// Polls `done` (sleeping 1 ms between) until it is true or the timeout passes.
inline bool waitFor(const std::function<bool()>& done, const int timeoutMs = 10000) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (!done()) {
    if (std::chrono::steady_clock::now() > end) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return true;
}

// The resume.bin files on the fake card now.
inline std::set<std::string> resumeFilesOnCard() {
  std::set<std::string> found;
  for (const auto& entry : fakesd::sim().entries) {
    const std::string suffix = "/resume.bin";
    if (!entry.dead && !entry.isDir && entry.path.size() > suffix.size() &&
        entry.path.compare(entry.path.size() - suffix.size(), suffix.size(), suffix) == 0) {
      found.insert(entry.path);
    }
  }
  return found;
}

// What becomes of the resume.bin files a match writes on its way out (the forced exit writes the last snapshot).
enum class Saves : uint8_t {
  Discard,  // taken off the card: the game's title screen opened next has no Continue row for them
  Keep,     // left on the card, as production leaves them: a game that was left mid-round offers Continue
};

// Lets go of the matches a launcher test started (`drop` does it, as the manager would), the same way every run.
// A match's VM publishes its first snapshot a moment after it logs "Started <id>", and the match writes its last
// snapshot as resume.bin on the way out, so a test that dropped it at once got a save on some runs and none on others,
// and the title screen opened next offered Continue or not. This waits until every match that started has logged its
// first round, then drops. `saves` says what happens to the saves the drop wrote: Discard takes them away (a test that
// means a save puts it there before, and one that means the state production reaches, a game left mid-round that
// offers Continue, says Keep). Discard takes away only the files the drop itself created: a save that was on the card
// before it (one the test placed, or one the match's own loop wrote) stays.
inline void letStartedMatchesGo(const std::function<void()>& drop, const Saves saves) {
  waitFor([] { return fakelog::countLines("GAME: Started ") <= fakelog::countLines("Round started"); }, 3000);
  const std::set<std::string> before = resumeFilesOnCard();
  drop();
  if (saves == Saves::Discard) {
    for (const std::string& path : resumeFilesOnCard()) {
      if (before.count(path) == 0) fakesd::removeEntry(path);
    }
  }
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

// The texts the renderer recorded since its last forget(), one string a ch.gfx.text: FrameReplay draws one code point
// a drawText call, so the calls of one text (consecutive, on one line) are joined back.
inline std::vector<std::string> drawnTexts(const GfxRenderer& renderer) {
  std::vector<std::string> texts;
  bool joining = false;
  int lineY = 0;
  for (const auto& call : renderer.calls) {
    if (call.kind != GfxRenderer::Kind::DrawText) {
      joining = false;
      continue;
    }
    if (!joining || call.y != lineY) texts.emplace_back();
    texts.back() += call.text;
    joining = true;
    lineY = call.y;
  }
  return texts;
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

// GameSplashLayout::rowRect's rects of the splash menu's rows 0 to count - 1, as a screen built on `renderer` measures
// them: read through a host of its own, which lays out with the theme the title screen's and the match's hosts use (the
// double's: resetUi binds none). Its target is gone when this returns, so the screens' own targets are read as before.
inline std::vector<freeink::ui::Rect> splashMenuRows(const GfxRenderer& renderer, const int count = 2) {
  struct Probe {
    const GfxRenderer* renderer = nullptr;
    int count = 0;
    std::vector<freeink::ui::Rect> rows;
    freeink::ui::ListProps scratch;
  };
  auto probe = std::make_unique<Probe>();
  probe->renderer = &renderer;
  probe->count = count;
  {
    auto host = std::make_unique<UiAppHost>(renderer);
    host->app.setScreen(
        [](UiAppHost::UiScreen& screen, void* user) {
          auto* self = static_cast<Probe*>(user);
          for (int i = 0; i < self->count; ++i)
            self->rows.push_back(GameSplashLayout::rowRect(screen, *self->renderer, i, self->scratch));
        },
        probe.get());
    host->renderUi();
  }
  return probe->rows;
}

// The middle of the hidden hand-off screen's "I'm ready" button, which fills the splash menu's second row: where the
// suites tap it (HiddenPassTest pins the button to that row).
inline freeink::ui::Point readyButtonMiddle(const GfxRenderer& renderer) {
  const freeink::ui::Rect ready = splashMenuRows(renderer, 2)[1];
  return freeink::ui::Point{static_cast<int16_t>(ready.x + ready.width / 2),
                            static_cast<int16_t>(ready.y + ready.height / 2)};
}

// ---- the hidden hand-off screen as a push holds it (GameMatchTest's HiddenPassTest, ResumeMatchTest's PassResumeTest)
// ----

// The middle of the splash band on the logical screen: it starts under the header, where the title screen's does
// (GameSplashLayout::bandTop), and is 480 px tall.
inline int bandMiddleY(const GfxRenderer& renderer) {
  return GameSplashLayout::bandTop(renderer) + GameSplashLayout::BAND / 2;
}

// The fills `icon` makes at 128 px with its middle at the band's (240, bandMiddleY), in its fill weight when `fill`,
// drawn as drawGameIcon draws it there.
inline std::vector<GfxRenderer::Call> iconFills(const char* icon, const bool fill = false) {
  GfxRenderer expected(480, 800);
  EXPECT_TRUE(drawGameIcon(expected, icon, 240 - 64, bandMiddleY(expected) - 64, 128, true, fill)) << icon;
  std::vector<GfxRenderer::Call> fills;
  for (const GfxRenderer::Call& call : expected.calls) {
    if (call.kind == GfxRenderer::Kind::FillRect) fills.push_back(call);
  }
  return fills;
}

// The fills the Crosshatch mark makes at 128 px with its middle at the band's (240, bandMiddleY): one fill per run of
// ink in a row of GameMark::HERO_128 (bit 0 = ink, MSB first), top to bottom, as the default icon is drawn.
inline std::vector<GfxRenderer::Call> markFills() {
  GfxRenderer expected(480, 800);
  const int left = 240 - 64;
  const int top = bandMiddleY(expected) - 64;
  std::vector<GfxRenderer::Call> fills;
  for (int y = 0; y < 128; ++y) {
    int x = 0;
    while (x < 128) {
      const auto ink = [y](const int column) {
        return ((GameMark::HERO_128[y * 16 + column / 8] >> (7 - column % 8)) & 1) == 0;
      };
      if (!ink(x)) {
        ++x;
        continue;
      }
      const int start = x;
      while (x < 128 && ink(x)) ++x;
      expected.fillRect(left + start, top + y, x - start, 1, true);
    }
  }
  for (const GfxRenderer::Call& call : expected.calls) {
    if (call.kind == GfxRenderer::Kind::FillRect) fills.push_back(call);
  }
  return fills;
}

// Whether `drawn` is exactly the fills `want`, call by call.
inline void expectSameFills(const std::vector<GfxRenderer::Call>& drawn, const std::vector<GfxRenderer::Call>& want) {
  ASSERT_FALSE(want.empty());
  ASSERT_EQ(drawn.size(), want.size()) << "the hand-off screen drew more or less than the game's icon";
  for (size_t i = 0; i < want.size(); ++i) {
    EXPECT_EQ(drawn[i].kind, GfxRenderer::Kind::FillRect) << i;
    EXPECT_EQ(drawn[i].x, want[i].x) << i;
    EXPECT_EQ(drawn[i].y, want[i].y) << i;
    EXPECT_EQ(drawn[i].w, want[i].w) << i;
    EXPECT_EQ(drawn[i].h, want[i].h) << i;
    EXPECT_EQ(drawn[i].black, want[i].black) << i;
  }
}

// Black pixels the renderer holds in (x, y, w, h).
inline size_t blackIn(const GfxRenderer& renderer, const int x, const int y, const int w, const int h) {
  size_t black = 0;
  for (int py = y; py < y + h; ++py)
    for (int px = x; px < x + w; ++px)
      if (renderer.pixel(px, py) == GfxRenderer::PixelBlack) ++black;
  return black;
}

// Whether the band shows `white` (a page of the band's full size) pixel for pixel, sampled every 7 px, and nothing
// black is drawn on the renderer outside the band (the turn line and the button are FreeInkUI's, which the recording
// target does not paint).
inline void expectBandShows(const GfxRenderer& renderer, const std::function<bool(int, int)>& white) {
  const int top = GameSplashLayout::bandTop(renderer);
  int wrong = 0;
  for (int y = 0; y < GameSplashLayout::BAND && wrong < 5; y += 7) {
    for (int x = 0; x < GameSplashLayout::BAND && wrong < 5; x += 7) {
      const auto want = white(x, y) ? GfxRenderer::PixelWhite : GfxRenderer::PixelBlack;
      if (renderer.pixel(x, top + y) != want) {
        ADD_FAILURE() << "band pixel " << x << "," << y;
        ++wrong;
      }
    }
  }
  EXPECT_EQ(blackIn(renderer, 0, 0, 480, 800), blackIn(renderer, 0, top, 480, GameSplashLayout::BAND))
      << "ink outside the band";
}

// What the framebuffer held at a push: every drawing call since the last clearScreen before it (`callsBefore`,
// GfxRenderer::Shown::callsBefore), and whether a clearScreen came before them at all.
struct Held {
  bool cleared = false;
  std::vector<GfxRenderer::Call> drawn;
};
inline Held heldAt(const GfxRenderer& renderer, const size_t callsBefore) {
  Held held;
  for (size_t i = 0; i < callsBefore && i < renderer.calls.size(); ++i) {
    const GfxRenderer::Call& call = renderer.calls[i];
    if (call.kind == GfxRenderer::Kind::ClearScreen) {
      held.cleared = true;
      held.drawn.clear();
    }
    if (call.kind == GfxRenderer::Kind::FillRect || call.kind == GfxRenderer::Kind::DrawText ||
        call.kind == GfxRenderer::Kind::FillRectDither || call.kind == GfxRenderer::Kind::DrawLine) {
      held.drawn.push_back(call);
    }
  }
  return held;
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
    UITheme::getInstance().getTheme().reset();
    UITheme::getInstance().coverGridHome = false;
    gpio.swipe = HalGPIO::Swipe{};
    gpio.touchHeldMs = 0;
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
