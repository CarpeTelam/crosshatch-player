#include <I18n.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ArenaSize.h"
#include "GameAssets.h"
#include "GameIconDraw.h"
#include "GameRowIcon.h"
#include "GameSaveStore.h"
#include "GameViewIcons.h"
#include "MatchSupport.h"
#include "Session.h"
#include "activities/games/GameMatchActivity.h"
#include "components/UiAppHost.h"

// The real GameMatchActivity, GameVM, FrameReplay, and FreeInkUI, over the screen doubles
// (screen_stubs/, among them a copy of Activity's surface): its transitions and what each does, the load and start
// failures and the words they show, the Play-again gap, the watchdog and abandon, and the store. Entry 4 of
// epic-install-and-launcher. Each test names the deferred-work item it pins.

namespace {

using match::bandMiddleY;
using match::blackIn;
using match::expectBandShows;
using match::expectSameFills;
using match::iconFills;
using match::installFixture;
using match::installGame;
using match::waitFor;
using Button = MappedInputManager::Button;

// Where a screen tap lands on canvas point (x, y): the canvas is 474 x 788 at (3, 6).
constexpr int CANVAS_X = 3;
constexpr int CANVAS_Y = 6;

class MatchTest : public match::ScreenTest {
 protected:
  void TearDown() override {
    fakertos::release();  // a test that failed while holding the VM must not leave it held
    fakearena::reserveBytes = GameScript::SCRATCH_RESERVE_BYTES;
    if (activity) {
      // What the manager does when the screen goes: onExit under the lock, then the destructor under it.
      if (!exited) activityManager.exitHolding(*activity);
      activityManager.destroyHolding(activity);
    }
    ScreenTest::TearDown();
  }

  void enter(const std::string& id, const std::string& name = "",
             const GameCore::Roster& roster = GameCore::Roster::solo()) {
    gameId = id;
    activity = std::make_unique<GameMatchActivity>(*renderer, *input, match::manifestOf(id, name), roster);
    activity->onEnter();
  }

  // One pass of the main loop, then the frame's input is over.
  void frame() {
    input->update();  // as main.cpp does before the activity's loop: the pass's touch sample
    activity->loop();
    input->clear();
  }
  // Loops until `done`, as the main loop would, for at most `timeoutMs`.
  bool pump(const std::function<bool()>& done, const int timeoutMs = 10000) {
    return waitFor(
        [&] {
          frame();
          return done();
        },
        timeoutMs);
  }
  // Loops until the match has asked for a render (a frame was published).
  bool pumpToRender() {
    return pump([&] { return activityManager.updateRequested(); });
  }
  void render() {
    activity->render(RenderLock(*activity));
    activityManager.markRendered();
  }
  // Loops until a frame is asked for, and draws it.
  void showFrame() {
    ASSERT_TRUE(pumpToRender());
    render();
  }

  void tapCanvas(const int x, const int y) { input->tap(CANVAS_X + x, CANVAS_Y + y); }

  // The match's state as its last transition logged it ("<id>: A -> B on E"); Starting before any.
  std::string state() const {
    std::string found = "Starting";
    // The match's own lines only ("INF GAME: <id>: ..."), not a game's ch.log lines, which carry the id as origin.
    const std::string prefix = "INF GAME: " + gameId + ": ";
    for (const std::string& line : fakelog::snapshot()) {
      if (line.rfind(prefix, 0) != 0) continue;
      const size_t arrow = line.find(" -> ");
      if (arrow == std::string::npos || line.find(": ") == std::string::npos) continue;
      const size_t on = line.find(" on ", arrow);
      if (on == std::string::npos) continue;
      found = line.substr(arrow + 4, on - arrow - 4);
    }
    return found;
  }

  // The lines the last view drew through its FreeInkUI target.
  screen::RecordingTarget& ui() {
    if (!screen::RecordingTarget::newest()) {
      std::fprintf(stderr, "no view has been drawn: the screen has no UI target yet\n");
      std::abort();
    }
    return *screen::RecordingTarget::newest();
  }

  // Draws the view the match is in (a fresh record of what its dialog draws first).
  void renderView() {
    ui().forget();
    render();
  }

  // Taps the option a view draws under `label`, at the middle of its text, as a finger would.
  void tapOption(const std::string& label) {
    for (const screen::DrawnText& drawn : ui().drawn) {
      if (drawn.text != label) continue;
      input->tap(drawn.rect.x + drawn.rect.width / 2, drawn.rect.y + drawn.rect.height / 2);
      frame();
      return;
    }
    FAIL() << "the view does not draw an option \"" << label << "\": " << ui().joined();
  }

  // The words the error view shows: its headline, and whether its message says `detail`.
  void expectErrorView(const std::string& headline, const std::string& detail) {
    renderView();
    EXPECT_TRUE(ui().drewLine(headline)) << ui().joined();
    std::string flat = ui().joined();
    for (char& c : flat)
      if (c == '\n') c = ' ';
    EXPECT_NE(flat.find(detail), std::string::npos) << "\"" << detail << "\" is not in: " << flat;
  }

  // Enters game `id` and expects the load or start to have failed with `reason` under the "could not start" headline.
  void expectStartFailure(const std::string& id, const std::string& reason) {
    enter(id);
    EXPECT_EQ(state(), "Error");
    expectErrorView(tr(STR_GAMES_START_FAILED), reason);
  }

  static std::string storePath(const std::string& id) { return "/.games-data/" + id + "/store.bin"; }

  std::unique_ptr<GameMatchActivity> activity;
  std::string gameId;
  bool exited = false;  // the test ran onExit itself
};

// ---- entering, the canvas, and the pause and end-of-round menus (## e3r-x, ## 3.7: the match's transitions) ----

TEST_F(MatchTest, EnteringStartsTheGameAndItsFirstFrameIsDrawnInFull) {
  installFixture("tracer");
  enter("tracer", "Tracer");
  EXPECT_EQ(state(), "Playing");
  EXPECT_TRUE(logHas("Started tracer"));
  showFrame();
  ASSERT_EQ(renderer->shown.size(), 1u);
  EXPECT_EQ(renderer->shown[0].mode, HalDisplay::FULL_REFRESH);
}

TEST_F(MatchTest, ATapOnTheCanvasReachesTheGameAtTheCanvasPoint) {
  installFixture("tracer");
  enter("tracer");
  showFrame();
  tapCanvas(100, 200);
  frame();
  showFrame();
  EXPECT_EQ(renderer->pixel(CANVAS_X + 100, CANVAS_Y + 200), GfxRenderer::PixelBlack);
  EXPECT_EQ(renderer->pixel(CANVAS_X + 300, CANVAS_Y + 200), GfxRenderer::PixelWhite);
}

TEST_F(MatchTest, BackPausesTheRoundAndResumeReturnsToTheCanvasOnAClearedScreen) {
  installFixture("tracer");
  enter("tracer", "Tracer");
  showFrame();
  input->click(Button::Back);
  frame();
  EXPECT_EQ(state(), "Paused");
  EXPECT_TRUE(activityManager.updateRequested());
  renderView();
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_PAUSED)));
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_RESUME)));
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_LEAVE)));
  EXPECT_TRUE(ui().drewLine("Tracer"));  // the game's name is the dialog's title
  ASSERT_FALSE(UITheme::getInstance().getTheme().hints.empty());
  EXPECT_EQ(UITheme::getInstance().getTheme().hints.back().btn1, tr(STR_GAMES_RESUME));
  EXPECT_EQ(renderer->shown.back().mode, HalDisplay::FAST_REFRESH);

  const size_t clears = renderer->count(GfxRenderer::Kind::ClearScreen);
  tapOption(tr(STR_GAMES_RESUME));
  EXPECT_EQ(state(), "Playing");
  ASSERT_TRUE(activityManager.updateRequested());  // a resumed round is redrawn
  render();
  EXPECT_GT(renderer->count(GfxRenderer::Kind::ClearScreen), clears);
  EXPECT_EQ(renderer->shown.back().mode, HalDisplay::FULL_REFRESH);  // the menu sat over it: a full refresh
}

TEST_F(MatchTest, TheKeysMoveAroundAMenuAndConfirmChoosesTheFocusedOption) {
  installFixture("tracer");
  enter("tracer");
  showFrame();
  input->click(Button::Back);
  frame();
  renderView();
  // Option 0 (Resume) is focused. Previous wraps to the last (Leave), Next wraps back to the
  // first, and Confirm chooses what is focused.
  input->press(Button::NavPrevious);
  frame();
  input->press(Button::NavNext);
  frame();
  input->click(Button::Confirm);
  frame();
  EXPECT_EQ(state(), "Playing");  // Resume
  input->click(Button::Back);
  frame();
  renderView();
  input->press(Button::NavPrevious);
  frame();
  input->click(Button::Confirm);
  frame();
  EXPECT_EQ(state(), "Leaving");  // Leave
  EXPECT_EQ(activityManager.asks.goToGames, 1);
}

TEST_F(MatchTest, TheEndOfARoundOpensTheOverMenuAndWritesTheStoreAtOnce) {
  installGame("onetap", match::ONE_TAP_GAME);
  enter("onetap", "One tap");
  showFrame();
  EXPECT_FALSE(fakesd::has(storePath("onetap")));
  tapCanvas(50, 50);
  frame();
  ASSERT_TRUE(pump([&] { return state() == "Over"; }));
  // No time has passed, so the periodic flush is not what wrote it.
  EXPECT_TRUE(fakesd::has(storePath("onetap")));
  renderView();
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_OVER)));
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_PLAY_AGAIN)));
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_LEAVE)));
  // Back does nothing in the end-of-round menu, and its hint says so.
  EXPECT_EQ(UITheme::getInstance().getTheme().hints.back().btn1, "");
  input->click(Button::Back);
  frame();
  EXPECT_EQ(state(), "Over");
}

// ---- leaving, the forced exit, and Home (the solo-lifecycle item; 12cc816) ----

TEST_F(MatchTest, LeavingStopsTheVmAndWritesTheStoreBeforeItGoesToGames) {
  installFixture("counter");
  enter("counter");
  showFrame();
  tapCanvas(100, 200);
  frame();
  showFrame();  // the count is drawn: the tap's apply has run, so ch.store holds it
  input->click(Button::Back);
  frame();
  renderView();
  int tasksThen = -1;
  bool storeThen = false;
  activityManager.onGoToGames = [&] {
    // The VM has been joined, so its thread is about to return; give it that moment.
    tasksThen = fakertos::waitNoTasks(1000) ? 0 : 1;
    storeThen = fakesd::has(storePath("counter"));
  };
  // The stop wakes the VM from the loop task: the RenderLock must be held then (the render task may
  // be inside renderCanvas, reading the frames the stop may free).
  std::vector<bool> lockHeld;
  fakertos::S().onLoopNotify = [&] { lockHeld.push_back(fakelock::held()); };
  tapOption(tr(STR_GAMES_LEAVE));
  EXPECT_EQ(state(), "Leaving");
  EXPECT_EQ(activityManager.asks.goToGames, 1);
  EXPECT_EQ(tasksThen, 0) << "goToGames was asked with the VM still running";
  EXPECT_TRUE(storeThen) << "goToGames was asked before ch.store was written";
  ASSERT_FALSE(lockHeld.empty());
  for (const bool held : lockHeld) EXPECT_TRUE(held) << "Leave stopped the VM without the RenderLock";
  // The exit that follows finds the VM gone and the store clean: it writes nothing more.
  const size_t writes = fakesd::countOps("rename");
  exited = true;
  activityManager.exitHolding(*activity);
  EXPECT_EQ(fakesd::countOps("rename"), writes);
}

TEST_F(MatchTest, AForcedExitStopsTheVmAndWritesTheStoreWithoutTakingTheRenderLock) {
  installFixture("counter");
  enter("counter");
  showFrame();
  tapCanvas(100, 200);
  frame();
  showFrame();
  EXPECT_FALSE(fakesd::has(storePath("counter")));
  exited = true;
  activityManager.exitHolding(*activity);  // the manager holds the lock, as it does when a screen is replaced
  EXPECT_EQ(fakelock::selfDeadlocks.load(), 0) << "onExit took the RenderLock the manager holds (12cc816)";
  EXPECT_EQ(state(), "Leaving");
  EXPECT_TRUE(fakertos::waitNoTasks(1000));
  EXPECT_TRUE(fakesd::has(storePath("counter")));
  EXPECT_EQ(activityManager.asks.goToGames, 0);  // a forced exit does not navigate
}

// A solo match's forced exit pushes nothing: the blank is a hidden pass match's only (epic-pass-and-play entry 6).
TEST_F(MatchTest, AForcedExitOfASoloMatchPushesNothing) {
  installFixture("tracer");
  enter("tracer");
  showFrame();
  const size_t pushes = renderer->shown.size();
  exited = true;
  activityManager.exitHolding(*activity);
  EXPECT_EQ(fakelock::selfDeadlocks.load(), 0);
  EXPECT_EQ(renderer->shown.size(), pushes);
  EXPECT_FALSE(logHas("blank screen pushed"));
}

TEST_F(MatchTest, HomePausesARoundInPlayAndIsIgnoredWhileAMenuIsUpUntilTheMatchLetsGo) {
  installFixture("tracer");
  enter("tracer");
  showFrame();
  EXPECT_TRUE(activity->handleHomeGesture());  // consumed: it paused the round instead of leaving
  EXPECT_EQ(state(), "Paused");
  EXPECT_TRUE(activity->handleHomeGesture());  // still consumed, and ignored
  EXPECT_EQ(state(), "Paused");
  EXPECT_TRUE(logHas("ignored in Paused"));
  renderView();
  tapOption(tr(STR_GAMES_LEAVE));
  EXPECT_EQ(state(), "Leaving");
  EXPECT_FALSE(activity->handleHomeGesture());  // a match that let go lets Home go Home
}

TEST_F(MatchTest, BackFromTheErrorViewLeaves) {
  installGame("boom", "local game = {}\nfunction game.setup() error('boom') end\nreturn game\n");
  enter("boom");
  ASSERT_TRUE(pump([&] { return state() == "Error"; }));
  input->click(Button::Back);
  frame();
  EXPECT_EQ(state(), "Leaving");
  EXPECT_EQ(activityManager.asks.goToGames, 1);
  EXPECT_TRUE(fakertos::waitNoTasks(1000));
}

// ---- the load-failure mapping (## 3.2: BadImage to STR_GAMES_BAD_IMAGE, and every other LoadResult) ----

TEST_F(MatchTest, AMissingFolderSaysSo) { expectStartFailure("gone", tr(STR_GAMES_FOLDER_MISSING)); }

TEST_F(MatchTest, AFolderWithNoLuaSaysSo) {
  fakesd::addFile("/.games/empty/readme.txt", "hello");
  expectStartFailure("empty", tr(STR_GAMES_NO_SOURCES));
}

TEST_F(MatchTest, ALuaFileWithAnInvalidNameSaysSo) {
  fakesd::addFile("/.games/named/Bad-Name.lua", "return 1");
  expectStartFailure("named", tr(STR_GAMES_BAD_SOURCE_NAME));
}

TEST_F(MatchTest, LuaFilesOverTheCapSayTheyAreTooLarge) {
  fakesd::addFile("/.games/big/main.lua", std::string(GameAssets::MAX_SOURCE_BYTES + 1, '-'));
  expectStartFailure("big", tr(STR_GAMES_SOURCES_TOO_LARGE));
}

TEST_F(MatchTest, AFolderTheCardWillNotOpenSaysItCannotBeRead) {
  installGame("locked", match::STILL_GAME);
  fakesd::sim().failOpen.insert("/.games/locked");
  expectStartFailure("locked", tr(STR_GAMES_CANNOT_READ));
}

TEST_F(MatchTest, AnImageHeaderTheCardCannotReadIsNotADamagedImage) {
  // f27dcefd: a read error is the card's, and says so; a bad header is the image's.
  installGame("logo", match::STILL_GAME);
  fakesd::addFile("/.games/logo/logo.bmp", harness::bmpFile(16, 16, [](int, int) { return true; }));
  fakesd::sim().failReadAt["/.games/logo/logo.bmp"] = 0;
  expectStartFailure("logo", tr(STR_GAMES_CANNOT_READ));
}

TEST_F(MatchTest, NoRoomForTheGamesFilesSaysOutOfMemory) {
  installGame("crowded", match::STILL_GAME);
  // After the store's block is taken, the listing's first rewind is where the loader starts:
  // the next PSRAM request, the game's own block, is refused.
  fakesd::sim().onRewind = [](const std::string& dir, int) {
    if (dir == "/.games/crowded") fakepsram::failNext = true;
  };
  expectStartFailure("crowded", tr(STR_GAMES_OUT_OF_MEMORY));
}

TEST_F(MatchTest, ADamagedImageSaysSoAndNamesTheImageReason) {
  installFixture("bad-image");
  expectStartFailure("bad-image", tr(STR_GAMES_BAD_IMAGE));
}

TEST_F(MatchTest, AGameThatCannotBeStartedInMemoryShowsTheOutOfMemoryReason) {
  installFixture("tracer");
  // The store's slot.
  fakepsram::failNext = true;
  expectStartFailure("tracer", tr(STR_GAMES_OUT_OF_MEMORY));
  EXPECT_TRUE(logHas("OOM: ch.store slot"));
}

TEST_F(MatchTest, NoRoomForTheVmsArenaIsAStartFailureThatLeaksNothing) {
  installFixture("tracer");
  fakepsram::failAbove = 300000;  // the arena is the one request over it
  expectStartFailure("tracer", tr(STR_GAMES_OUT_OF_MEMORY));
  EXPECT_TRUE(logHas("OOM: "));
}

TEST_F(MatchTest, ATaskThatCannotBeCreatedIsAStartFailure) {
  installFixture("tracer");
  fakertos::S().failNextCreate = true;
  expectStartFailure("tracer", tr(STR_GAMES_OUT_OF_MEMORY));
  EXPECT_TRUE(logHas("Cannot create the GameVM task"));
}

// ---- the failures a running VM reports (## 3.10: vmHealthy's headline, vmFailureText's words) ----

TEST_F(MatchTest, AScriptErrorSaysTheGameStoppedWithLuasMessage) {
  installGame("boom", "local game = {}\nfunction game.setup() error('boom') end\nreturn game\n");
  enter("boom");
  ASSERT_TRUE(pump([&] { return state() == "Error"; }));
  expectErrorView(tr(STR_GAMES_ERROR), "boom");
}

TEST_F(MatchTest, ASessionThatDoesNotFitSaysTheGameCouldNotStartInTheHostsWords) {
  installFixture("tracer");
  fakearena::reserveBytes = 0;
  enter("tracer");
  ASSERT_TRUE(pump([&] { return state() == "Error"; }));
  expectErrorView(tr(STR_GAMES_START_FAILED), tr(STR_GAMES_OUT_OF_MEMORY));
}

TEST_F(MatchTest, ScratchThatDoesNotFitSaysTheSameInTheHostsWords) {
  installFixture("tracer");
  fakearena::reserveBytes = sizeof(GameCore::Session) + 64;
  enter("tracer");
  ASSERT_TRUE(pump([&] { return state() == "Error"; }));
  expectErrorView(tr(STR_GAMES_START_FAILED), tr(STR_GAMES_OUT_OF_MEMORY));
}

// ---- the watchdog and abandon (the ch.timer item's abandonVm leak; the solo-lifecycle item's stop) ----

TEST_F(MatchTest, ACallRunningPastThreeSecondsIsStoppedAndAbandonedIntoTheErrorView) {
  installFixture("timer");
  enter("timer");
  showFrame();
  fakertos::arm();  // the tap's ch.timer.after reads the clock: the call is held inside Lua
  tapCanvas(100, 200);
  frame();
  ASSERT_TRUE(fakertos::waitParked());
  frame();  // the first poll that sees the call starts its clock
  fakertos::advance(3000);
  frame();
  EXPECT_EQ(state(), "Playing") << "a call at exactly the limit is not yet stuck";
  fakertos::advance(1);
  // The stop and the abandon that follow run under the RenderLock, as render reads what they free.
  std::vector<bool> lockHeld;
  fakertos::S().onLoopNotify = [&] { lockHeld.push_back(fakelock::held()); };
  frame();
  EXPECT_EQ(state(), "Error");
  ASSERT_FALSE(lockHeld.empty());
  for (const bool held : lockHeld) EXPECT_TRUE(held) << "the watchdog stopped the VM without the RenderLock";
  expectErrorView(tr(STR_GAMES_ERROR), tr(STR_GAMES_NOT_RESPONDING));
  EXPECT_TRUE(logHas("Abandoned the stuck VM"));
  EXPECT_EQ(fakepsram::liveBlocks, 1u);  // the VM's blocks are gone; the store slot is the match's
  fakertos::release();
}

TEST_F(MatchTest, AVmThatWillNotStopKeepsTheStoreSlotForTheTaskThatMayStillPostToIt) {
  installGame("logger", match::LOGGING_GAME);
  enter("logger");
  showFrame();
  ASSERT_TRUE(waitFor(match::roundStarted));  // the task's own log line comes after the first frame: arm past it
  expectCleanPsram = false;                   // the VM, and the slot it may post to, are leaked on purpose
  fakertos::arm(fakertos::At::Log);
  tapCanvas(100, 200);
  frame();
  ASSERT_TRUE(fakertos::waitParked());
  frame();
  fakertos::advance(3001);
  frame();
  EXPECT_EQ(state(), "Error");
  EXPECT_TRUE(logHas("VM stuck and not safely deletable"));
  EXPECT_TRUE(logHas("The ch.store slot stays with the leaked VM"));
  expectErrorView(tr(STR_GAMES_ERROR), tr(STR_GAMES_NOT_RESPONDING));
  fakertos::release();  // the leaked task ends at its next hook: the cancel flag is set
  EXPECT_TRUE(fakertos::waitNoTasks());
  // The screen goes: the slot and its block stay, with the leaked VM's arena, frames, and sources.
  exited = true;
  activityManager.exitHolding(*activity);
  activityManager.destroyHolding(activity);
  EXPECT_EQ(fakepsram::liveBlocks, 4u);
}

// ---- the Play-again gap (## 3.7's render gate, ## e3r-2's gesture drop, ## e3r-x's skip) ----

// Retro R8 (e) and R9 (g): each library icon in a view takes the ink its label beside it is drawn in, and the view's
// own icon sits on the white panel in black. FreeInkApp's default theme draws a focused row in black too (its selected
// state is the inverted one), so this pins the wiring, that the icon reads the label's resolved ink and not a constant;
// `GameViewIconsTest` pins the rule for the inverted and dithered cases a theme can have.
TEST_F(MatchTest, ThePauseViewsIconsTakeTheInkOfTheLabelsBesideThem) {
  installFixture("tracer");
  enter("tracer");
  showFrame();
  input->click(Button::Back);
  frame();
  ASSERT_EQ(state(), "Paused");
  renderer->forgetAll();
  renderView();
  const screen::DrawnText* resume = nullptr;
  const screen::DrawnText* leave = nullptr;
  for (const screen::DrawnText& drawn : ui().drawn) {
    if (drawn.text == tr(STR_GAMES_RESUME)) resume = &drawn;
    if (drawn.text == tr(STR_GAMES_LEAVE)) leave = &drawn;
  }
  ASSERT_NE(resume, nullptr);
  ASSERT_NE(leave, nullptr);
  // The icons are runs of one-pixel-high fillRects (GameIconBlit); the canvas below them draws by fillRectDither.
  std::vector<bool> resumeInk, leaveInk, otherInk;
  for (const auto& call : renderer->calls) {
    if (call.kind != GfxRenderer::Kind::FillRect || call.h != 1 || call.w > GameViewIcons::VIEW_PIXELS) continue;
    const auto near = [&](const screen::DrawnText* label) {
      const int mid = label->rect.y + label->rect.height / 2;
      return call.x < label->rect.x && call.y >= mid - GameViewIcons::ROW_PIXELS &&
             call.y <= mid + GameViewIcons::ROW_PIXELS;
    };
    (near(resume) ? resumeInk : near(leave) ? leaveInk : otherInk).push_back(call.black);
  }
  ASSERT_FALSE(resumeInk.empty());
  ASSERT_FALSE(leaveInk.empty());
  ASSERT_FALSE(otherInk.empty()) << "the view's own icon";
  const auto same = [](const std::vector<bool>& inks, const bool expected) {
    return std::all_of(inks.begin(), inks.end(), [&](const bool ink) { return ink == expected; });
  };
  EXPECT_TRUE(same(resumeInk, resume->color != freeink::ui::Color::White)) << "Resume's icon follows its label";
  EXPECT_TRUE(same(leaveInk, leave->color != freeink::ui::Color::White)) << "Leave's icon follows its label";
  EXPECT_TRUE(same(otherInk, true)) << "the view's icon is black on the white panel";
}

// The gate opens when displayBuffer has returned, not before it: a loop pass run inside displayBuffer (the render task
// is in the panel driver, the loop task keeps going) still drops a tap. A store of roundsDisplayed ahead of the draw or
// the display would let it through.
TEST_F(MatchTest, ATapWhileDisplayBufferIsStillRunningIsDropped) {
  installGame("gated", match::gatedGame(5));
  enter("gated");
  ASSERT_TRUE(pumpToRender());
  bool ran = false;
  renderer->onDisplay = [&] {
    ran = true;
    tapCanvas(210, 310);
    frame();
  };
  render();
  renderer->onDisplay = nullptr;
  ASSERT_TRUE(ran);
  tapCanvas(150, 250);  // after the render: it reaches the game, and the VM takes events in order
  frame();
  ASSERT_TRUE(pump([&] { return fakelog::countLines("tap\t150\t250") == 1u; }));
  EXPECT_EQ(fakelog::countLines("tap\t210\t310"), 0u) << "the tap during displayBuffer reached the game";
}

// The same gate at the start of a match: the title screen is on screen until the first round's first frame is drawn, so
// a tap before then (the one that opened the game, lifting late, or an impatient second one) is dropped.
TEST_F(MatchTest, ATapBeforeTheFirstFrameIsDrawnIsDroppedAndOneAfterItReachesTheGame) {
  installGame("gated", match::gatedGame(5));
  enter("gated");
  ASSERT_TRUE(pumpToRender());  // the first frame is published, and not yet drawn
  tapCanvas(210, 310);
  frame();
  render();
  tapCanvas(150, 250);
  frame();
  ASSERT_TRUE(pump([&] { return fakelog::countLines("tap\t150\t250") == 1u; }));
  EXPECT_EQ(fakelog::countLines("tap\t210\t310"), 0u) << "the tap before the first frame reached the game";
}

// A tap before the game has published anything is dropped as well.
TEST_F(MatchTest, ATapBeforeAnyFrameIsPublishedNeverReachesTheGame) {
  installGame("gated", match::gatedGame(5));
  enter("gated");
  tapCanvas(210, 310);
  frame();  // the VM task may not have published yet; either way the first frame is not on the panel
  ASSERT_TRUE(pumpToRender());
  render();
  EXPECT_EQ(fakelog::countLines("tap\t210\t310"), 0u);
  tapCanvas(150, 250);
  frame();
  ASSERT_TRUE(pump([&] { return fakelog::countLines("tap\t150\t250") == 1u; }));
}

// Round 1 ends with the first tap; a second tap is queued behind it, and the VM is held inside that
// second step when the end-of-round menu's Play again is chosen. Then the step finishes and publishes
// a frame of the last round, and the VM is held again at round 2's setup: the gap.
class PlayAgainGapTest : public MatchTest {
 protected:
  void SetUp() override {
    MatchTest::SetUp();
    installGame("gated", match::gatedGame(1));
    enter("gated");
    showFrame();
    fakertos::arm();
    tapCanvas(100, 200);  // A: ends the round, held at its clock read
    frame();
    ASSERT_TRUE(fakertos::waitParked());
    tapCanvas(300, 400);  // B: queued behind A
    frame();
    fakertos::pass();  // A finishes and the round ends; B is held at its own clock read
    ASSERT_TRUE(fakertos::waitParked());
    ASSERT_TRUE(pump([&] { return state() == "Over"; }));
  }

  void TearDown() override {
    fakertos::release();
    MatchTest::TearDown();
  }

  // Play again from the menu on screen (or the focused option, when it was never drawn).
  void playAgainByTouch() {
    renderView();
    tapOption(tr(STR_GAMES_PLAY_AGAIN));
  }

  // B's step finishes and publishes its (late) frame; the VM restarts and is held at round 2's setup.
  void enterTheGap() {
    fakertos::pass();
    ASSERT_TRUE(fakertos::waitParked());
    activityManager.markRendered();
  }
};

TEST_F(PlayAgainGapTest, TheLoopAsksForNoRenderWhileTheLastRoundsLateFrameIsTheNewest) {
  playAgainByTouch();
  ASSERT_EQ(state(), "Playing");
  enterTheGap();
  for (int i = 0; i < 20; ++i) frame();
  EXPECT_FALSE(activityManager.updateRequested()) << "a render was asked for in the Play-again gap";
}

TEST_F(PlayAgainGapTest, ATapInTheGapIsDroppedAndNeverReachesTheNewRound) {
  playAgainByTouch();
  enterTheGap();
  tapCanvas(200, 300);
  frame();
  fakertos::release();
  ASSERT_TRUE(pumpToRender());  // round 2's first frame
  render();
  EXPECT_EQ(state(), "Playing");
  // A tap once the round is up does reach it. The game logs each tap it is given, and the VM takes
  // its events in order, so the dropped one, had it been posted, is logged before this one.
  tapCanvas(150, 250);
  frame();
  ASSERT_TRUE(pump([&] { return state() == "Over"; }));
  EXPECT_EQ(fakelog::countLines("tap\t150\t250"), 1u);
  EXPECT_EQ(fakelog::countLines("tap\t200\t300"), 0u) << "the tap in the gap reached the new round";
}

// Retro deferral e3r-2: the gate opens when the new round's first frame has been drawn and handed to the panel, not
// when it is published, so a tap in between (the panel still refreshing to it) is not aimed at that round either.
TEST_F(PlayAgainGapTest, ATapBetweenTheNewRoundsFirstFramePublishingAndItsBeingDrawnIsDropped) {
  playAgainByTouch();
  enterTheGap();
  fakertos::release();
  ASSERT_TRUE(pumpToRender());  // round 2's first frame is published; no render has drawn it
  tapCanvas(210, 310);
  frame();
  EXPECT_EQ(fakelog::countLines("tap\t210\t310"), 0u);
  render();  // drawn and handed to the panel: the gate opens
  tapCanvas(150, 250);
  frame();
  ASSERT_TRUE(pump([&] { return state() == "Over"; }));
  EXPECT_EQ(fakelog::countLines("tap\t150\t250"), 1u);
  EXPECT_EQ(fakelog::countLines("tap\t210\t310"), 0u) << "the tap during the refresh reached the new round";
}

TEST_F(PlayAgainGapTest, ARenderInTheGapDrawsNothingAndTheNewRoundsFirstFrameIsDrawnInFullOnAClearedScreen) {
  playAgainByTouch();
  enterTheGap();
  const size_t shown = renderer->shown.size();
  const size_t clears = renderer->count(GfxRenderer::Kind::ClearScreen);
  render();
  EXPECT_EQ(renderer->shown.size(), shown) << "the gap drew the last round's frame";
  EXPECT_EQ(renderer->count(GfxRenderer::Kind::ClearScreen), clears);
  fakertos::release();
  ASSERT_TRUE(pumpToRender());
  render();
  ASSERT_EQ(renderer->shown.size(), shown + 1);
  EXPECT_EQ(renderer->shown.back().mode, HalDisplay::FULL_REFRESH);
  EXPECT_GT(renderer->count(GfxRenderer::Kind::ClearScreen), clears);
  // Round 2 has no square yet: neither round 1's tap nor B's (late) one is on the screen.
  EXPECT_EQ(renderer->pixel(CANVAS_X + 100, CANVAS_Y + 200), GfxRenderer::PixelWhite);
  EXPECT_EQ(renderer->pixel(CANVAS_X + 300, CANVAS_Y + 400), GfxRenderer::PixelWhite);
}

TEST_F(PlayAgainGapTest, PauseThenResumeInTheGapKeepsThePauseMenuUntilTheNewRoundsFirstFrame) {
  playAgainByTouch();
  enterTheGap();
  input->click(Button::Back);
  frame();
  ASSERT_EQ(state(), "Paused");
  renderView();
  tapOption(tr(STR_GAMES_RESUME));
  ASSERT_EQ(state(), "Playing");
  ASSERT_TRUE(activityManager.updateRequested());  // a resume asks for a redraw
  const size_t shown = renderer->shown.size();
  render();
  EXPECT_EQ(renderer->shown.size(), shown) << "Resume in the gap redrew the last round's frame over the menu";
  fakertos::release();
  ASSERT_TRUE(pumpToRender());
  render();
  ASSERT_EQ(renderer->shown.size(), shown + 1);
  EXPECT_EQ(renderer->shown.back().mode, HalDisplay::FULL_REFRESH);
}

TEST_F(PlayAgainGapTest, AGapRenderMarksTheScreenAsNotHoldingTheCanvasEvenWhenNoMenuWasDrawn) {
  // The end-of-round menu was never drawn (viewOnScreen is still false); Play again by key.
  input->click(Button::Confirm);
  frame();
  ASSERT_EQ(state(), "Playing");
  enterTheGap();
  render();  // skipped: marks the screen as not holding the canvas
  fakertos::release();
  ASSERT_TRUE(pumpToRender());
  render();
  ASSERT_FALSE(renderer->shown.empty());
  EXPECT_EQ(renderer->shown.back().mode, HalDisplay::FULL_REFRESH);
}

// The pause menu says why Resume shows nothing new in the gap, and is drawn without the line once the round has
// started.
TEST_F(PlayAgainGapTest, ThePauseMenuInTheGapSaysTheNextRoundIsStartingUntilItHas) {
  playAgainByTouch();
  enterTheGap();
  input->click(Button::Back);
  frame();
  ASSERT_EQ(state(), "Paused");
  renderView();
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_PAUSED)));
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_NEXT_ROUND_STARTING))) << ui().joined();
  for (int i = 0; i < 5; ++i) frame();
  EXPECT_FALSE(activityManager.updateRequested()) << "the menu was redrawn while the gap was still open";
  fakertos::release();
  ASSERT_TRUE(pump([&] { return activityManager.updateRequested(); })) << "the round started and the menu stayed";
  EXPECT_EQ(state(), "Paused");
  renderView();
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_PAUSED)));
  EXPECT_FALSE(ui().drewLine(tr(STR_GAMES_NEXT_ROUND_STARTING))) << ui().joined();
}

// The render task runs beside the loop task on the device; the tests above render between loop
// passes. This one renders from a second thread, continuously, while Play again is chosen and the gap
// is held open, to see that the gap draws nothing whichever way the two interleave. A render that
// began before the choice is over by the time the RenderLock is had, so what is counted after that is
// what the gap drew. It is a smoke test: it cannot pin the order of the two stores in `handle`
// (M7b), which a render sees differently only in a window of a few instructions.
TEST_F(PlayAgainGapTest, TheGapDrawsNothingWhileTheRenderTaskRendersBesideTheLoop) {
  renderView();  // the end-of-round menu, drawn here once so its option's rectangle is known
  int x = -1;
  int y = -1;
  for (const screen::DrawnText& drawn : ui().drawn) {
    if (drawn.text != tr(STR_GAMES_PLAY_AGAIN)) continue;
    x = drawn.rect.x + drawn.rect.width / 2;
    y = drawn.rect.y + drawn.rect.height / 2;
  }
  ASSERT_GE(x, 0);
  std::atomic<bool> stop{false};
  std::thread renderTask([&] {
    while (!stop.load()) {
      activity->render(RenderLock(*activity));
      std::this_thread::yield();
    }
  });
  struct Joiner {
    std::atomic<bool>& stop;
    std::thread& thread;
    ~Joiner() {
      stop = true;
      thread.join();
    }
  } joiner{stop, renderTask};

  input->tap(x, y);
  frame();
  ASSERT_EQ(state(), "Playing");
  enterTheGap();
  size_t drawn = 0;
  {
    RenderLock lock(*activity);
    drawn = renderer->shown.size();
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  {
    RenderLock lock(*activity);
    EXPECT_EQ(renderer->shown.size(), drawn) << "a render beside the loop drew in the Play-again gap";
  }
  fakertos::release();
  EXPECT_TRUE(waitFor([&] {
    frame();
    RenderLock lock(*activity);
    return renderer->shown.size() > drawn;
  })) << "the new round's first frame was never drawn";
}

// ---- the other gestures on the canvas (readGesture, and GameTouch's edge rule) ----

TEST_F(MatchTest, ALongPressOnTheCanvasReachesTheGameAsALongPressAtTheCanvasPoint) {
  installGame("events", match::EVENTS_GAME);
  enter("events");
  showFrame();
  input->longPress(CANVAS_X + 100, CANVAS_Y + 200);
  frame();
  ASSERT_TRUE(waitFor([&] { return logHas("event\tlong_press\t100\t200\t-"); }));
  EXPECT_EQ(fakelog::countLines("event\ttap"), 0u);  // the contact's lift is no tap
}

TEST_F(MatchTest, ASwipeOnTheCanvasReachesTheGameAtItsStartWithItsDirection) {
  installGame("events", match::EVENTS_GAME);
  enter("events");
  showFrame();
  // Up, from the middle of the screen (an up swipe from the bottom 14% is the system's Home).
  input->swipe(CANVAS_X + 200, CANVAS_Y + 500, CANVAS_X + 200, CANVAS_Y + 400);
  frame();
  ASSERT_TRUE(waitFor([&] { return logHas("event\tswipe\t200\t500\tup"); }));
  // Right, from the middle (a right swipe from the left 25% is the system's Back).
  input->swipe(CANVAS_X + 200, CANVAS_Y + 300, CANVAS_X + 400, CANVAS_Y + 320);
  frame();
  ASSERT_TRUE(waitFor([&] { return logHas("event\tswipe\t200\t300\tright"); }));
}

TEST_F(MatchTest, ASystemEdgeSwipeNeverReachesTheGame) {
  installGame("events", match::EVENTS_GAME);
  enter("events");
  showFrame();
  input->swipe(10, 400, 300, 400);  // right from the left 25%: Back
  frame();
  input->swipe(240, 780, 240, 500);  // up from the bottom 14%: Home
  frame();
  // Events reach the VM in order: the tap after them is logged once the swipes would have been.
  tapCanvas(50, 60);
  frame();
  ASSERT_TRUE(waitFor([&] { return logHas("event\ttap\t50\t60"); }));
  EXPECT_EQ(fakelog::countLines("event\tswipe"), 0u);
}

// ---- the watchdog's stop that works (stopStuckVm's first branch) ----

// A held call that the match's stop finds ended: the log line "stopping the VM" comes before its
// cancel, so the test releases the held task there and, when `untilItEnds`, waits for the task
// to end on its own first (a Lua error in the released call), all before the stop asks.
class WatchdogStopTest : public MatchTest {
 protected:
  // Holds the tap's call at its clock read and lets the watchdog find it 3001 ms in.
  void holdATap(const bool untilItEnds) {
    fakertos::arm();
    tapCanvas(100, 200);
    frame();
    ASSERT_TRUE(fakertos::waitParked());
    frame();  // the first poll that sees the call starts its clock
    fakertos::advance(3001);
    if (untilItEnds) {
      fakelog::hook() = [](const std::string& line) {
        if (line.find("stopping the VM") == std::string::npos) return;
        fakertos::release();
        fakertos::waitNoTasks();
      };
    } else {
      fakelog::hook() = [](const std::string& line) {
        if (line.find("stopping the VM") != std::string::npos) fakertos::release();
      };
    }
  }
};

TEST_F(WatchdogStopTest, ACallThatReturnsWhenTheStopIsAskedIsStoppedWithoutAnAbandon) {
  installFixture("timer");
  enter("timer");
  showFrame();
  holdATap(false);
  frame();
  EXPECT_EQ(state(), "Error");
  EXPECT_FALSE(logHas("did not stop within")) << "the VM was abandoned though it stopped";
  EXPECT_FALSE(logHas("Abandoned the stuck VM"));
  EXPECT_TRUE(fakertos::waitNoTasks(1000));
  expectErrorView(tr(STR_GAMES_ERROR), tr(STR_GAMES_NOT_RESPONDING));
}

TEST_F(WatchdogStopTest, ACallThatEndsInALuaErrorMeanwhileShowsThatErrorNotTheWatchdogs) {
  installGame("late", match::LATE_ERROR_GAME);
  enter("late");
  showFrame();
  holdATap(true);
  frame();
  EXPECT_EQ(state(), "Error");
  EXPECT_FALSE(logHas("did not stop within"));
  renderView();
  std::string flat = ui().joined();
  EXPECT_NE(flat.find("late boom"), std::string::npos) << flat;
  EXPECT_EQ(flat.find(tr(STR_GAMES_NOT_RESPONDING)), std::string::npos);
}

// ---- the timer poll, and the watchdog in a menu (the loop's own calls) ----

TEST_F(MatchTest, ADueTimerIsPolledByTheLoopAndDeliveredToTheGame) {
  installFixture("timer");
  enter("timer");
  showFrame();  // setup armed a 3000 ms timer at 1000 ms
  fakertos::advance(3000);
  frame();  // loopPlaying polls the timer: due now
  ASSERT_TRUE(waitFor([&] { return logHas("tick at"); })) << "the loop never polled the timer";
}

TEST_F(MatchTest, AVmThatHangsWhilePausedIsFoundByTheWatchdogInTheMenu) {
  installFixture("timer");
  enter("timer");
  showFrame();
  fakertos::arm();  // the tap's ch.timer.after reads the clock: held there, inside Lua
  tapCanvas(100, 200);
  frame();
  ASSERT_TRUE(fakertos::waitParked());
  frame();  // the first poll that sees the call starts its clock
  input->click(Button::Back);
  frame();
  ASSERT_EQ(state(), "Paused");
  fakertos::advance(3001);
  frame();  // loopView: the VM may hang, fail, or set ch.store while a menu is up
  EXPECT_EQ(state(), "Error");
  expectErrorView(tr(STR_GAMES_ERROR), tr(STR_GAMES_NOT_RESPONDING));
  fakertos::release();
}

// ---- the render task's side of a frame (the framereplay item's device-side wiring) ----

TEST_F(MatchTest, TheLoopDoesNotAskAgainForAFrameARenderAlreadyTook) {
  installFixture("tracer");
  enter("tracer");
  // A render someone else asked for (an overlay closed) takes the first frame before the loop asked.
  ASSERT_TRUE(waitFor([&] {
    render();
    return !renderer->shown.empty();
  }));
  activityManager.markRendered();
  frame();
  EXPECT_FALSE(activityManager.updateRequested());
}

TEST_F(MatchTest, ARenderWithNoNewFrameRedrawsInFull) {
  installFixture("tracer");
  enter("tracer");
  showFrame();
  ASSERT_EQ(renderer->shown.size(), 1u);
  render();  // no new frame: a repaint after something else drew
  ASSERT_EQ(renderer->shown.size(), 2u);
  EXPECT_EQ(renderer->shown.back().mode, HalDisplay::FULL_REFRESH);
}

TEST_F(MatchTest, AFrameIdenticalToTheOneOnScreenIsNotRefreshed) {
  installGame("still", match::STILL_GAME);
  enter("still");
  showFrame();
  tapCanvas(100, 200);  // the game redraws the same frame
  frame();
  ASSERT_TRUE(pumpToRender());
  const size_t shown = renderer->shown.size();
  render();
  EXPECT_EQ(renderer->shown.size(), shown);
}

TEST_F(MatchTest, TheGameSeesTheScreenAndTheFontsTheMatchGaveIt) {
  installGame("probe", R"(
local game = {}
function game.setup(ctx)
  ch.log("screen", ch.screen.w, ch.screen.h, "abc", ch.text_width("abc", "medium"))
  return {}
end
function game.status(state) return { turn = 1 } end
function game.apply(state, seat, move) return state end
function game.draw(state, seat, ui) ch.gfx.clear("white") end
function game.input(state, seat, ui, ev) end
return game
)");
  enter("probe");
  showFrame();
  EXPECT_TRUE(logHas("screen\t474\t788\tabc\t" + std::to_string(3 * match::MEDIUM_ADVANCE)));
}

// ---- ch.store: restored when the match starts, written periodically, and at each end ----

TEST_F(MatchTest, AStoreOnTheCardIsRestoredIntoTheGamesStoreWhenTheMatchStarts) {
  installFixture("counter");
  enter("counter");
  showFrame();
  tapCanvas(100, 200);
  frame();
  showFrame();
  exited = true;
  activityManager.exitHolding(*activity);  // writes store.bin
  activityManager.destroyHolding(activity);
  ASSERT_TRUE(fakesd::has(storePath("counter")));
  fakelog::clearLines();

  activity =
      std::make_unique<GameMatchActivity>(*renderer, *input, match::manifestOf("counter"), GameCore::Roster::solo());
  exited = false;
  activity->onEnter();
  showFrame();
  EXPECT_TRUE(logHas("opened with\t1\tsaved taps"));
}

TEST_F(MatchTest, ADirtyStoreIsWrittenOnceTheFlushIntervalPassesWhilePlaying) {
  installFixture("counter");
  enter("counter");
  showFrame();
  tapCanvas(100, 200);
  frame();
  showFrame();
  fakertos::advance(GameSaveStore::FLUSH_INTERVAL_MS - 1);
  frame();
  EXPECT_FALSE(fakesd::has(storePath("counter")));
  fakertos::advance(1);
  frame();
  EXPECT_TRUE(fakesd::has(storePath("counter")));
}

TEST_F(MatchTest, ADirtyStoreIsWrittenOnceTheFlushIntervalPassesWhilePaused) {
  installFixture("counter");
  enter("counter");
  showFrame();
  tapCanvas(100, 200);
  frame();
  showFrame();
  input->click(Button::Back);
  frame();
  ASSERT_EQ(state(), "Paused");
  fakertos::advance(GameSaveStore::FLUSH_INTERVAL_MS - 1);
  frame();
  EXPECT_FALSE(fakesd::has(storePath("counter")));
  fakertos::advance(1);
  frame();
  EXPECT_TRUE(fakesd::has(storePath("counter")));
}

// ---- an open pass match (epic-pass-and-play entry 1): pass-open through the real match, VM, and Session ----

class PassMatchTest : public MatchTest {
 protected:
  // The canvas point at the middle of pass-open's cell `cell` (1..9, row by row: 140 px squares from (27, 200)).
  static int cellX(const int cell) { return 27 + (cell - 1) % 3 * 140 + 70; }
  static int cellY(const int cell) { return 200 + (cell - 1) / 3 * 140 + 70; }

  // Taps `cell` and draws the frame the VM publishes after it, recording only that frame's calls.
  void tapCell(const int cell) {
    tapCanvas(cellX(cell), cellY(cell));
    frame();
    renderer->forget();
    showFrame();
  }

  // What the renderer drew since its last forget() holds the text `text`.
  bool drew(const std::string& text) const {
    const std::vector<std::string> texts = match::drawnTexts(*renderer);
    return std::find(texts.begin(), texts.end(), text) != texts.end();
  }

  // Where the first log line holding `part` is, or the log's size when none does.
  static size_t lineOf(const std::string& part) {
    const std::vector<std::string> lines = fakelog::snapshot();
    for (size_t i = 0; i < lines.size(); ++i) {
      if (lines[i].find(part) != std::string::npos) return i;
    }
    return lines.size();
  }
};

TEST_F(PassMatchTest, TwoSeatsAlternateAndTheEndOfRoundMenuSitsOverTheFrameForEveryone) {
  installFixture("pass-open");
  fakesd::addFile("/.games/pass-open/.pkg", "v1\n0530a15766e91bf1\n");  // a package that keeps resume.bin
  enter("pass-open", "Pass open", GameCore::Roster::pass(2));
  renderer->forget();
  showFrame();
  EXPECT_TRUE(drew("Player 1 (X) to move")) << "seat 1 is drawn first";

  tapCell(1);
  EXPECT_TRUE(drew("Player 2 (O) to move"));
  EXPECT_FALSE(drew("That square is taken"));

  // Seat 2 taps seat 1's square: the rejection reaches seat 2's input and ui, and it is still seat 2's turn.
  tapCell(1);
  EXPECT_TRUE(logHas("apply seat 2 cell 1"));
  EXPECT_TRUE(drew("Player 2 (O) to move"));
  EXPECT_TRUE(drew("That square is taken"));

  tapCell(2);
  EXPECT_TRUE(drew("Player 1 (X) to move"));
  EXPECT_FALSE(drew("That square is taken")) << "the rejection was seat 2's, so seat 1's ui never had it";
  tapCell(4);
  EXPECT_TRUE(drew("Player 2 (O) to move"));
  tapCell(3);
  EXPECT_TRUE(drew("Player 1 (X) to move"));

  // X completes 1-4-7: the round ends, over reaches each seat once in seat order, and every answer is dropped.
  tapCanvas(cellX(7), cellY(7));
  frame();
  ASSERT_TRUE(pump([&] { return state() == "Over"; }));
  EXPECT_EQ(fakelog::countLines("over for seat 1"), 1u);
  EXPECT_EQ(fakelog::countLines("over for seat 2"), 1u);
  EXPECT_LT(lineOf("over for seat 1"), lineOf("over for seat 2"));
  EXPECT_EQ(fakelog::countLines("apply seat "), 6u) << "five moves and the rejected one; no answer to over";
  EXPECT_FALSE(logHas("cell 5")) << "each seat answered over with cell 5, which never reached apply";
  for (const char* move : {"apply seat 1 cell 1", "apply seat 2 cell 2", "apply seat 1 cell 4", "apply seat 2 cell 3",
                           "apply seat 1 cell 7"}) {
    EXPECT_EQ(fakelog::countLines(move), 1u) << move;
  }

  renderer->forget();
  renderView();
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_OVER)));
  EXPECT_TRUE(drew("Everyone: Player 1 wins")) << "the menu sits over seat 0's frame";
  EXPECT_FALSE(drew("Player 1 (X): Player 1 wins"));

  // Play again: a new round on the same pass match, seat 1 first.
  tapOption(tr(STR_GAMES_PLAY_AGAIN));
  EXPECT_EQ(state(), "Playing");
  ASSERT_TRUE(pump([&] { return fakelog::countLines("Round started") >= 2; }));
  renderer->forget();
  render();
  EXPECT_TRUE(drew("Player 1 (X) to move"));

  // Leave keeps the pass save: its mode (byte 14) is pass and its seat count (byte 15) two.
  input->click(Button::Back);
  frame();
  ASSERT_EQ(state(), "Paused");
  renderView();
  tapOption(tr(STR_GAMES_LEAVE));
  EXPECT_EQ(state(), "Leaving");
  const std::string savePath = "/.games-data/pass-open/resume.bin";
  ASSERT_EQ(match::resumeFilesOnCard(), std::set<std::string>{savePath});
  const auto saved = fakesd::bytesOf(savePath);
  ASSERT_GT(saved.size(), 15u);
  EXPECT_EQ(saved[14], 1u) << "mode pass";
  EXPECT_EQ(saved[15], 2u) << "two seats";
}

// The tag is the frame render drew, read under the frame mutex (cross-story fix review F2): when the VM publishes the
// next seat's first frame after render read the frame count and before drawFront took the front, the panel shows the
// next seat's frame, and that seat's tap is played, not dropped as one made under the last seat's frame. The seam is
// renderCanvas's view clear, which comes after its frameGen() read: that order is load-bearing for this test (with the
// read below the clear, the publish would land before it and the test would pass whatever frameDisplayed stored). The
// steady path's window, inside drawFront, is covered by construction: the number is read under the frame mutex.
TEST_F(PassMatchTest, AFramePublishedWhileRenderDrawsIsTheOneTheNextTapIsMadeUnder) {
  installFixture("pass-open");
  enter("pass-open", "Pass open", GameCore::Roster::pass(2));
  showFrame();
  ASSERT_TRUE(waitFor(match::roundStarted));  // the task's own log line comes after the first frame: arm past it
  fakertos::arm(fakertos::At::Log);
  tapCanvas(cellX(1), cellY(1));  // seat 1's move: held at its "tap for seat 1"
  frame();
  ASSERT_TRUE(fakertos::waitParked());
  tapCanvas(cellX(2), cellY(2));  // seat 1's second tap, queued: dropped after the move, with a log line
  frame();
  input->click(Button::Back);
  frame();
  ASSERT_EQ(state(), "Paused");
  renderView();
  input->click(Button::Back);  // Resume: the next render clears the screen and draws the canvas again
  frame();
  ASSERT_EQ(state(), "Playing");
  bool published = false;
  bool cleared = false;
  renderer->onClear = [&] {
    if (cleared) return;  // render's own clear only, before drawFront takes the frame mutex
    cleared = true;
    fakertos::release();  // the move is played, seat 2's first frame published, the queued tap dropped
    published = waitFor([] { return fakelog::anyLine("Dropped a touch"); });
  };
  render();
  renderer->onClear = nullptr;
  ASSERT_TRUE(published);
  const std::vector<std::string>& pushed = renderer->shown.back().texts;
  EXPECT_NE(std::find(pushed.begin(), pushed.end(), "Player 2 (O) to move"), pushed.end()) << "seat 2's frame";
  tapCanvas(cellX(5), cellY(5));  // seat 2's move, made under the frame on the panel
  frame();
  ASSERT_TRUE(
      waitFor([] { return fakelog::anyLine("apply seat 2 cell 5") || fakelog::countLines("Dropped a touch") >= 2u; }));
  EXPECT_TRUE(logHas("apply seat 2 cell 5")) << "seat 2's tap was dropped as one made under seat 1's frame";
  EXPECT_EQ(fakelog::countLines("Dropped a touch"), 1u);
}

// The loop posts each touch with the frame on the panel (cross-story review row 5): seat 1's second tap, made under its
// frame while its turn-passing move is still being played, is dropped by the VM, never seat 2's move.
TEST_F(PassMatchTest, ATapMadeUnderSeatOnesFrameBehindItsTurnPassingMoveNeverReachesSeatTwo) {
  installFixture("pass-open");
  enter("pass-open", "Pass open", GameCore::Roster::pass(2));
  showFrame();
  ASSERT_TRUE(waitFor(match::roundStarted));  // the task's own log line comes after the first frame: arm past it
  fakertos::arm(fakertos::At::Log);
  tapCanvas(cellX(1), cellY(1));  // seat 1's move: held at its "tap for seat 1"
  frame();
  ASSERT_TRUE(fakertos::waitParked());
  tapCanvas(cellX(2), cellY(2));  // seat 1's second tap, under the same frame
  frame();
  fakertos::release();
  ASSERT_TRUE(waitFor([] { return fakelog::anyLine("Dropped a touch") || fakelog::anyLine("tap for seat 2"); }));
  EXPECT_TRUE(logHas("Dropped a touch"));
  EXPECT_FALSE(logHas("tap for seat 2")) << "seat 1's tap became seat 2's input";
  EXPECT_FALSE(logHas("apply seat 2"));
  EXPECT_EQ(fakelog::countLines("apply seat 1 cell 1"), 1u);
}

// A contact begun under seat 1's frame and lifted after seat 2's frame was pushed carries the frame of its touch-down
// (fix-review G2), so it is dropped rather than played as seat 2's move.
TEST_F(PassMatchTest, AContactBegunUnderSeatOnesFrameAndLiftedUnderSeatTwosIsDropped) {
  installFixture("pass-open");
  enter("pass-open", "Pass open", GameCore::Roster::pass(2));
  showFrame();
  tapCanvas(cellX(1), cellY(1));  // seat 1's move
  frame();
  // The next finger comes down while seat 1's frame is still the one on the panel (no render since the move), and is
  // held: the next pass samples and latches it.
  input->holdTouch(CANVAS_X + cellX(2), CANVAS_Y + cellY(2));
  fakertos::advance(100);
  frame();
  renderer->forget();
  showFrame();  // seat 2's frame is pushed while the finger is down
  ASSERT_TRUE(drew("Player 2 (O) to move"));
  frame();  // a pass of the hold under seat 2's frame, which must not re-latch
  frame();
  // The finger lifts, under seat 2's frame; any drop logged from here on is this contact's.
  fakelog::clearLines();
  input->liftTouch();
  frame();
  ASSERT_TRUE(waitFor([] { return fakelog::anyLine("Dropped a touch") || fakelog::anyLine("tap for seat 2"); }));
  EXPECT_EQ(fakelog::countLines("Dropped a touch"), 1u) << "the lift is dropped, tagged with seat 1's frame";
  EXPECT_FALSE(logHas("tap for seat 2")) << "a contact begun under seat 1's frame became seat 2's input";
  // Seat 2's own tap, begun under its frame, is played.
  tapCanvas(cellX(5), cellY(5));
  frame();
  ASSERT_TRUE(waitFor([] { return fakelog::anyLine("apply seat 2 cell 5"); }));
}

// A contact latched under seat 1's frame that ends where the match's loop never reads it (the light panel's swipe,
// which ActivityManager takes first): the next pass with no finger down frees the latch, so seat 2's tap after its
// frame carries seat 2's frame and is played (fix review H1).
TEST_F(PassMatchTest, ALatchWhoseContactEndedUnseenIsFreedForTheNextTap) {
  installFixture("pass-open");
  enter("pass-open", "Pass open", GameCore::Roster::pass(2));
  showFrame();
  tapCanvas(cellX(1), cellY(1));  // seat 1's move
  frame();
  input->holdTouch(CANVAS_X + cellX(4), CANVAS_Y + cellY(4));
  frame();  // latched under seat 1's frame, still on the panel
  // The finger lifted under the light panel: no frame of the match read the release.
  input->contact = {};
  renderer->forget();
  showFrame();  // seat 2's frame; no gesture is read on the way
  ASSERT_TRUE(drew("Player 2 (O) to move"));
  fakertos::advance(200);
  fakelog::clearLines();
  input->quickTap(CANVAS_X + cellX(5), CANVAS_Y + cellY(5));
  frame();
  ASSERT_TRUE(waitFor([] { return fakelog::anyLine("apply seat 2 cell 5") || fakelog::anyLine("Dropped a touch"); }));
  EXPECT_TRUE(logHas("apply seat 2 cell 5"));
  EXPECT_FALSE(logHas("Dropped a touch")) << "seat 2's tap carried the unseen contact's latch";
}

// The window back-dating cannot close (fix review J1; game-canvas.md, What remains): a finger that came down under
// seat 1's frame while the loop task was blocked (an SD write) is first sampled after the block, so when seat 2's push
// completed meanwhile, its tap carries seat 2's frame and reaches seat 2. Closing it needs touch sampled off the loop
// task (upstream input code; entry 11).
TEST_F(PassMatchTest, ATapWhoseFingerCameDownWhileTheLoopWasBlockedReachesTheSeatPushedMeanwhile) {
  installFixture("pass-open");
  enter("pass-open", "Pass open", GameCore::Roster::pass(2));
  showFrame();
  tapCanvas(cellX(1), cellY(1));  // seat 1's move
  frame();
  ASSERT_TRUE(pumpToRender());  // seat 2's frame is published and asked for
  fakertos::advance(100);
  input->holdTouch(CANVAS_X + cellX(2), CANVAS_Y + cellY(2));  // down under seat 1's frame; no update samples it
  fakertos::advance(30);
  renderer->forget();
  render();  // seat 2's frame is pushed, completing now
  ASSERT_TRUE(drew("Player 2 (O) to move"));
  fakertos::advance(30);
  frame();  // the first pass after the block samples the finger, under seat 2's frame
  fakertos::advance(30);
  fakelog::clearLines();
  input->liftTouch();
  frame();
  ASSERT_TRUE(waitFor([] { return fakelog::anyLine("Dropped a touch") || fakelog::anyLine("tap for seat 2"); }));
  EXPECT_TRUE(logHas("tap for seat 2")) << "the window is documented as open; update game-canvas.md if it closed";
  EXPECT_FALSE(logHas("Dropped a touch"));
}

// The same-pass race back-dating covers: the update that samples the finger runs while seat 2's push is still going,
// and the pass's gesture read (here, the lift's pass) comes after the push completed, so frameDisplayed already names
// seat 2's frame. The tap is back-dated by its held time to before the push completed and dropped. The push takes
// time here (onDisplay), so this pins that render notes the completion after displayBuffer returns (fix reviews H7,
// J6, K1).
TEST_F(PassMatchTest, ATapSampledDuringTheNextSeatsPushIsBackDatedToTheFrameBeforeIt) {
  installFixture("pass-open");
  enter("pass-open", "Pass open", GameCore::Roster::pass(2));
  showFrame();
  tapCanvas(cellX(1), cellY(1));  // seat 1's move
  frame();
  ASSERT_TRUE(pumpToRender());
  bool pushed = false;
  renderer->onDisplay = [this, &pushed] {
    if (pushed) return;
    pushed = true;
    input->holdTouch(CANVAS_X + cellX(2), CANVAS_Y + cellY(2));
    fakertos::advance(100);
    input->update();         // the loop task's update samples the finger while the panel refreshes
    fakertos::advance(200);  // the refresh goes on
  };
  renderer->forget();
  render();
  renderer->onDisplay = nullptr;
  ASSERT_TRUE(drew("Player 2 (O) to move"));
  fakertos::advance(30);
  fakelog::clearLines();
  input->liftTouch();  // the gesture read after the push completed, standing for the same pass's latch
  frame();
  ASSERT_TRUE(waitFor([] { return fakelog::anyLine("Dropped a touch") || fakelog::anyLine("tap for seat 2"); }));
  EXPECT_TRUE(logHas("Dropped a touch")) << "the tap was not back-dated to before the push completed";
  EXPECT_FALSE(logHas("tap for seat 2"));
}

// A tap read on a pass with a button edge is back-dated by its own held time, not the button's (MappedInputManager's
// getHeldTime answers the button's then), so seat 2's quick tap is played (fix review J2).
TEST_F(PassMatchTest, ATapReadWithAButtonEdgeIsBackDatedByItsOwnHeldTime) {
  installFixture("pass-open");
  enter("pass-open", "Pass open", GameCore::Roster::pass(2));
  showFrame();
  tapCanvas(cellX(1), cellY(1));  // seat 1's move
  frame();
  renderer->forget();
  showFrame();
  ASSERT_TRUE(drew("Player 2 (O) to move"));
  fakertos::advance(200);
  fakelog::clearLines();
  input->hold(MappedInputManager::Button::Left, 5000);  // a side button held 5 s, released on the tap's pass
  input->release(MappedInputManager::Button::Left);
  input->quickTap(CANVAS_X + cellX(5), CANVAS_Y + cellY(5));
  frame();
  ASSERT_TRUE(waitFor([] { return fakelog::anyLine("apply seat 2 cell 5") || fakelog::anyLine("Dropped a touch"); }));
  EXPECT_TRUE(logHas("apply seat 2 cell 5"));
  EXPECT_FALSE(logHas("Dropped a touch")) << "the tap was back-dated by the button's 5 s";
}

// The same contact held until it is a long press (500 ms) after seat 2's frame was pushed: the long press carries seat
// 1's frame and is dropped, and the lift after it is no tap (the long press suppressed the rest of the contact).
TEST_F(PassMatchTest, AContactBegunUnderSeatOnesFrameThatBecomesALongPressUnderSeatTwosIsDropped) {
  installFixture("pass-open");
  enter("pass-open", "Pass open", GameCore::Roster::pass(2));
  showFrame();
  tapCanvas(cellX(1), cellY(1));  // seat 1's move
  frame();
  input->holdTouch(CANVAS_X + cellX(2), CANVAS_Y + cellY(2));
  fakertos::advance(100);
  frame();  // latched under seat 1's frame
  renderer->forget();
  showFrame();  // seat 2's frame is pushed while the finger is down
  ASSERT_TRUE(drew("Player 2 (O) to move"));
  fakelog::clearLines();
  fakertos::advance(500);  // the hold becomes a long press
  frame();
  ASSERT_TRUE(waitFor([] { return fakelog::anyLine("Dropped a touch") || fakelog::anyLine("for seat 2"); }));
  EXPECT_EQ(fakelog::countLines("Dropped a touch"), 1u) << "the long press is dropped, tagged with seat 1's frame";
  input->liftTouch();
  frame();
  frame();
  // Seat 2's own tap, made after its frame, is played: everything queued before it has been handled.
  fakertos::advance(200);
  tapCanvas(cellX(5), cellY(5));
  frame();
  ASSERT_TRUE(waitFor([] { return fakelog::anyLine("apply seat 2 cell 5"); }));
  EXPECT_EQ(fakelog::countLines("Dropped a touch"), 1u) << "the lift after a long press is no tap";
  EXPECT_EQ(fakelog::countLines("tap for seat 2"), 1u) << "only seat 2's own tap reached seat 2";
}

// A tap shorter than 90 ms reports no touch-down (the device clears its press on the release update), so it carries the
// frame on the panel when its lift is read, never an earlier contact's latch: seat 2's quick tap after seat 1's held
// move, and after a contact of seat 1's that ended with no gesture, is played.
TEST_F(PassMatchTest, AQuickTapWithNoTouchDownCarriesTheFrameAtItsLift) {
  installFixture("pass-open");
  enter("pass-open", "Pass open", GameCore::Roster::pass(2));
  showFrame();
  // A contact of seat 1's that ends with no gesture (it slid past the tap slop): nothing is posted.
  input->holdTouch(CANVAS_X + cellX(4), CANVAS_Y + cellY(4));
  fakertos::advance(100);
  frame();
  input->liftWithoutTap();
  frame();
  EXPECT_FALSE(logHas("tap for seat 1"));
  // Seat 1 moves with a held tap (latched under its own frame), and seat 2's frame is drawn.
  input->holdTouch(CANVAS_X + cellX(1), CANVAS_Y + cellY(1));
  fakertos::advance(100);
  frame();
  input->liftTouch();
  frame();
  ASSERT_TRUE(waitFor([] { return fakelog::anyLine("apply seat 1 cell 1"); }));
  renderer->forget();
  showFrame();
  ASSERT_TRUE(drew("Player 2 (O) to move"));
  fakertos::advance(200);  // seat 2's tap begins after its frame's push completed
  fakelog::clearLines();
  input->quickTap(CANVAS_X + cellX(5), CANVAS_Y + cellY(5));
  frame();
  ASSERT_TRUE(waitFor([] { return fakelog::anyLine("apply seat 2 cell 5") || fakelog::anyLine("Dropped a touch"); }));
  EXPECT_TRUE(logHas("apply seat 2 cell 5"));
  EXPECT_FALSE(logHas("Dropped a touch"));
}

// The same behind the move that ends the round (cross-story review row 4): the tap never reaches seat 0, which is a
// frame, never an input seat.
TEST_F(PassMatchTest, ATapMadeBehindTheRoundEndingMoveNeverReachesSeatZero) {
  installFixture("pass-open");
  enter("pass-open", "Pass open", GameCore::Roster::pass(2));
  showFrame();
  for (const int cell : {1, 4, 2, 5}) {
    ASSERT_NO_FATAL_FAILURE(tapCell(cell));
  }
  ASSERT_TRUE(drew("Player 1 (X) to move"));
  fakertos::arm(fakertos::At::Log);
  tapCanvas(cellX(3), cellY(3));  // X completes 1-2-3: held at its "tap for seat 1"
  frame();
  ASSERT_TRUE(fakertos::waitParked());
  tapCanvas(cellX(9), cellY(9));  // made under seat 1's frame, queued behind the winning move
  frame();
  fakertos::release();
  ASSERT_TRUE(waitFor([] { return fakelog::anyLine("Dropped a touch") || fakelog::anyLine("tap for seat 0"); }));
  EXPECT_TRUE(logHas("Dropped a touch"));
  EXPECT_FALSE(logHas("tap for seat 0")) << "seat 0 is a frame, never an input seat";
  ASSERT_TRUE(pump([&] { return state() == "Over"; }));
  EXPECT_FALSE(logHas("Script error"));
}

// An open pass match never hands off (entry 4's Never): no Result, no HandOff, and no blank push.
TEST_F(PassMatchTest, AnOpenPassMatchNeverEntersResultOrHandOffNorPushesABlank) {
  installFixture("pass-open");
  enter("pass-open", "Pass open", GameCore::Roster::pass(2));
  showFrame();
  tapCell(1);
  tapCell(2);
  tapCell(4);
  EXPECT_TRUE(drew("Player 2 (O) to move"));
  EXPECT_FALSE(logHas("-> Result")) << fakelog::snapshot().back();
  EXPECT_FALSE(logHas("-> HandOff"));
  ASSERT_FALSE(renderer->shown.empty());
  for (const GfxRenderer::Shown& push : renderer->shown) EXPECT_FALSE(push.texts.empty()) << "a blank was pushed";
}

// The forced exit's blank is a hidden pass match's only (entry 6): an open pass match's exit pushes nothing.
TEST_F(PassMatchTest, TheForcedExitOfAnOpenPassMatchPushesNothing) {
  installFixture("pass-open");
  enter("pass-open", "Pass open", GameCore::Roster::pass(2));
  showFrame();
  tapCell(1);
  const size_t pushes = renderer->shown.size();
  exited = true;
  activityManager.exitHolding(*activity);
  EXPECT_EQ(fakelock::selfDeadlocks.load(), 0);
  EXPECT_EQ(renderer->shown.size(), pushes);
  EXPECT_FALSE(logHas("blank screen pushed"));
}

// ---- the renderer double's push (screen_stubs/GfxRenderer.h, entry 4): what the device's displayBuffer shows ----

// The device pushes the whole framebuffer, which clearScreen wipes and both the canvas and FreeInkUI draw into: a push
// holds every text drawn since the last clearScreen, in the order drawn, and none from before it.
TEST_F(MatchTest, APushHoldsTheCanvasAndUiTextsDrawnSinceTheLastClearScreenAndNoneBeforeIt) {
  renderer->drawText(UI_12_FONT_ID, 10, 10, "o");
  renderer->noteUiText("before");
  renderer->clearScreen();
  renderer->drawText(UI_12_FONT_ID, 10, 20, "a");
  renderer->drawText(UI_12_FONT_ID, 20, 20, "b");
  renderer->noteUiText("menu");
  renderer->drawText(UI_12_FONT_ID, 10, 40, "c");
  renderer->displayBuffer(HalDisplay::FAST_REFRESH);
  ASSERT_EQ(renderer->shown.size(), 1u);
  EXPECT_EQ(renderer->shown[0].texts, (std::vector<std::string>{"ab", "menu", "c"}));
  renderer->noteUiText("late");
  renderer->displayBuffer(HalDisplay::FAST_REFRESH);  // nothing cleared: the first push's texts are still there
  EXPECT_EQ(renderer->shown[1].texts, (std::vector<std::string>{"ab", "menu", "c", "late"}));
  renderer->clearScreen();
  renderer->displayBuffer(HalDisplay::FULL_REFRESH);
  EXPECT_TRUE(renderer->shown[2].texts.empty());
  renderer->forget();  // forgets the UI notes with the calls they were numbered by
  renderer->displayBuffer(HalDisplay::FULL_REFRESH);
  EXPECT_TRUE(renderer->shown[3].texts.empty());
}

// The input double's held contact, pinned to the device's touch path (src/MappedInputManager.cpp, freeink-sdk's
// InputManager.cpp): the touch-down time is the first sample of the contact (a read, or its lift), never the scripted
// put-down; the touch-down is a level, reported on every frame once the still finger has been down 90 ms and not
// before; the finger reads as held while down; at 500 ms a long press is reported on every read of that frame, and
// reading it suppresses the rest of the contact (no touch-down, held contact, release, or tap after it), while one not
// read on its frame is lost and the contact goes on; a lift reports the release and the tap but no touch-down, with
// the touch-only held time; getHeldTime() answers a button's hold on a frame with a button edge.
TEST(InputDoubleTest, AHeldContactFollowsTheDevicesTouchPath) {
  fakertos::reset();
  HalGPIO gpio;
  const GfxRenderer renderer(480, 800);
  MappedInputManager input(gpio, renderer);
  int x = 0;
  int y = 0;
  input.holdTouch(12, 34);
  fakertos::advance(1000);  // unsampled: the put-down itself stamps nothing
  EXPECT_TRUE(input.isScreenTouchHeld(x, y)) << "held from its first sample";
  EXPECT_FALSE(input.wasScreenTouchDown(x, y)) << "no touch-down before 90 ms from the first sample";
  fakertos::advance(89);
  EXPECT_FALSE(input.wasScreenTouchDown(x, y)) << "89 ms";
  input.clear();
  fakertos::advance(1);
  for (int frame = 0; frame < 3; ++frame) {
    ASSERT_TRUE(input.wasScreenTouchDown(x, y)) << "frame " << frame << ", kept across clear()";
    EXPECT_EQ(x, 12);
    EXPECT_EQ(y, 34);
    EXPECT_FALSE(input.wasScreenTapped(x, y)) << "no release while held";
    EXPECT_FALSE(input.wasScreenLongPress(x, y)) << "no long press before 500 ms";
    input.clear();
  }
  input.liftTouch();
  EXPECT_TRUE(input.wasScreenTapped(x, y));
  EXPECT_EQ(x, 12);
  EXPECT_TRUE(input.wasScreenTouchReleased());
  EXPECT_FALSE(input.wasScreenTouchDown(x, y)) << "the release frame reports no touch-down";
  EXPECT_FALSE(input.isScreenTouchHeld(x, y));
  EXPECT_EQ(input.getHeldTime(), 90u);
  EXPECT_EQ(gpio.lastTouchHeldMs(), 90u);
  input.clear();
  // A contact no update sampled before its lift (it came and went while the loop task was blocked) is never seen.
  input.holdTouch(1, 1);
  fakertos::advance(300);
  input.liftTouch();
  EXPECT_FALSE(input.wasScreenTapped(x, y));
  EXPECT_FALSE(input.wasScreenTouchReleased());
  EXPECT_EQ(gpio.lastTouchHeldMs(), 90u) << "unchanged";
  input.clear();
  // update() samples a held contact whether or not anything reads it.
  input.holdTouch(2, 2);
  input.update();
  fakertos::advance(120);
  EXPECT_TRUE(input.wasScreenTouchDown(x, y)) << "stamped by update(), 120 ms ago";
  input.liftTouch();
  EXPECT_EQ(gpio.lastTouchHeldMs(), 120u);
  input.clear();
  // A long press: reported on every read of its frame, none on the next, and then nothing more of the contact.
  input.holdTouch(5, 6);
  EXPECT_TRUE(input.isScreenTouchHeld(x, y));
  fakertos::advance(500);
  EXPECT_TRUE(input.wasScreenLongPress(x, y));
  EXPECT_TRUE(input.wasScreenLongPress(x, y)) << "every read of its frame";
  EXPECT_FALSE(input.wasScreenTouchDown(x, y)) << "suppressed";
  EXPECT_FALSE(input.isScreenTouchHeld(x, y)) << "suppressed";
  input.clear();
  EXPECT_FALSE(input.wasScreenLongPress(x, y)) << "not on the next frame";
  input.liftTouch();
  EXPECT_FALSE(input.wasScreenTapped(x, y)) << "the lift after a long press is no tap";
  EXPECT_FALSE(input.wasScreenTouchReleased()) << "a suppressed contact's release is not reported";
  input.clear();
  // A long press not read on the frame it falls due is lost: the contact is not suppressed, and its lift taps.
  input.holdTouch(9, 9);
  EXPECT_TRUE(input.isScreenTouchHeld(x, y));
  fakertos::advance(500);
  input.clear();  // the frame ends unread
  EXPECT_FALSE(input.wasScreenLongPress(x, y)) << "lost";
  EXPECT_TRUE(input.isScreenTouchHeld(x, y)) << "not suppressed";
  input.liftTouch();
  EXPECT_TRUE(input.wasScreenTapped(x, y)) << "its lift taps";
  input.clear();
  // A quick tap: the release and the tap on one frame, no touch-down, its held time.
  input.quickTap(7, 8, 40);
  EXPECT_TRUE(input.wasScreenTapped(x, y));
  EXPECT_FALSE(input.wasScreenTouchDown(x, y));
  EXPECT_EQ(input.getHeldTime(), 40u);
  EXPECT_EQ(gpio.lastTouchHeldMs(), 40u);
  // With a button released on the same frame, getHeldTime() answers the button's hold; the touch's stays on gpio.
  input.hold(MappedInputManager::Button::Left, 5000);
  input.release(MappedInputManager::Button::Left);
  EXPECT_EQ(input.getHeldTime(), 5000u);
  EXPECT_EQ(gpio.lastTouchHeldMs(), 40u);
  input.clear();
  // A lift with no gesture: a raw release only.
  input.holdTouch(3, 4);
  input.liftWithoutTap();
  EXPECT_TRUE(input.wasScreenTouchReleased());
  EXPECT_FALSE(input.wasScreenTapped(x, y));
  input.clear();
  EXPECT_FALSE(input.isScreenTouchHeld(x, y));
  // A lift with no held contact scripts nothing.
  input.liftTouch();
  EXPECT_FALSE(input.wasScreenTapped(x, y));
}

// The screen renderer double's clear hook (fix review F2's seam): it runs after the screen is cleared, inside the call,
// standing in for the VM task publishing on the other core while render draws.
TEST(ScreenRendererDoubleTest, ClearScreenRunsTheHookAfterTheScreenIsCleared) {
  GfxRenderer renderer(480, 800);
  renderer.fillRect(0, 0, 10, 10, true);
  int ran = 0;
  renderer.onClear = [&] {
    ++ran;
    EXPECT_EQ(renderer.pixel(5, 5), GfxRenderer::PixelWhite) << "the hook ran before the clear";
  };
  renderer.clearScreen();
  EXPECT_EQ(ran, 1);
  ASSERT_FALSE(renderer.calls.empty());
  EXPECT_EQ(renderer.calls.back().kind, GfxRenderer::Kind::ClearScreen);
}

// ---- a hidden pass match (epic-pass-and-play entry 4): pass-hidden, the hand-off between seats ----
//
// Since entry 12 the hand-off screen (HandOff) shows the game's icon, "Player N's turn", and the "I'm ready" button,
// and only that button or Confirm passes it; only the banner or Confirm passes Result. Where a test name or comment
// below still says "the blank" for HandOff's screen, it means that hand-off screen; the forced exit's and Leave's push
// is the plain white blank (FrameReplay::drawBlank, expectOneBlankPush).

// A hidden pass game that writes ch.store on every move, for the forced exit's order against the store flush (entry
// 6). Each seat makes two moves a turn, so the first move leaves the match in Playing with the store dirty and the
// second passes the turn (Result). Seat 1's frame shows its secret, "apple".
const char* const HIDDEN_STORE_GAME = R"(
local game = {}
function game.setup(ctx) return { seats = ctx.seats, moves = 0, turn = 1 } end
function game.status(state) return { turn = state.turn } end
function game.apply(state, seat, move)
  state.moves = state.moves + 1
  if state.moves % 2 == 0 then state.turn = state.turn % state.seats + 1 end
  ch.store.set({ moves = state.moves })
  ch.log("stored " .. state.moves)
  return state
end
function game.input(state, seat, ui, ev)
  if ev.kind == "tap" then return { tap = true } end
end
function game.draw(state, seat, ui)
  ch.gfx.clear("white")
  if seat == 1 then ch.gfx.text(40, 120, "Player 1's secret: apple", "medium", "black") end
  if seat == 2 then ch.gfx.text(40, 120, "Player 2's secret: river", "medium", "black") end
end
return game
)";

// A hidden pass game whose first move's input fails with a script error (the error view then keeps the VM).
const char* const HIDDEN_ERROR_GAME = R"(
local game = {}
function game.setup(ctx) return { seats = ctx.seats } end
function game.status(state) return { turn = 1 } end
function game.apply(state, seat, move) return state end
function game.input(state, seat, ui, ev)
  if ev.kind == "tap" then error("hidden boom") end
end
function game.draw(state, seat, ui)
  ch.gfx.clear("white")
  ch.gfx.text(40, 120, "Player " .. seat .. "'s secret: apple", "medium", "black")
end
return game
)";

class HiddenPassTest : public MatchTest {
 protected:
  void SetUp() override {
    MatchTest::SetUp();
    installFixture("pass-hidden");
  }

  // Enters game `id` (pass-hidden unless named) as the launcher would start a hidden pass match: its manifest says
  // hidden, and pass(2) plays it.
  void enterHidden(const std::string& id = "pass-hidden") {
    gameId = id;
    GameCore::Manifest manifest = match::manifestOf(id, "Pass hidden");
    manifest.seatsMin = 2;
    manifest.seatsMax = 2;
    manifest.modes = GameCore::Manifest::MODE_PASS;
    manifest.hidden = true;
    activity = std::make_unique<GameMatchActivity>(*renderer, *input, manifest, GameCore::Roster::pass(2));
    activity->onEnter();
  }

  // The screen goes (sleep, or any Replace): onExit under the RenderLock the manager holds, as ActivityManager does it.
  void sleep() {
    exited = true;
    activityManager.exitHolding(*activity);
  }

  // One push made during the forced exit, and what had happened by then, recorded inside its displayBuffer.
  struct ExitPush {
    HalDisplay::RefreshMode mode;
    std::vector<std::string> texts;
    uint64_t sinceExitMs;  // the clock since onExit() began
    size_t sdOps;          // the fake card's ops so far
    bool abandonLogged;    // "did not stop within" was logged
    bool storeOnCard;      // store.bin had been written
    size_t callsBefore;    // the renderer calls made before it (GfxRenderer::Shown::callsBefore)
  };
  struct ExitRecord {
    std::vector<ExitPush> pushes;
    size_t sdOpsBefore = 0;
    uint64_t tookMs = 0;  // the clock across the whole exit
  };

  // sleep(), recording each push the exit makes; `during` runs inside each push, after it is recorded. The double's
  // displayBuffer moves no clock by itself: a device's half refresh takes time this does not model (entry 11 measures
  // it), so a test that needs the push to cost time moves the clock in `during`.
  ExitRecord sleepRecordingPushes(const std::function<void()>& during = nullptr) {
    ExitRecord record;
    record.sdOpsBefore = fakesd::sim().ops.size();
    const uint64_t began = fakertos::S().nowMs.load();
    renderer->onDisplay = [&] {
      const GfxRenderer::Shown& push = renderer->shown.back();
      record.pushes.push_back({push.mode, push.texts, fakertos::S().nowMs.load() - began, fakesd::sim().ops.size(),
                               logHas("did not stop within"), fakesd::has(storePath(gameId)), push.callsBefore});
      if (during) during();
    };
    sleep();
    renderer->onDisplay = nullptr;
    record.tookMs = fakertos::S().nowMs.load() - began;
    return record;
  }

  // What the framebuffer held at a push (match::heldAt).
  using Held = match::Held;
  Held heldAt(const size_t callsBefore) const { return match::heldAt(*renderer, callsBefore); }

  // The forced exit pushed the blank once, and logged it: a half refresh of a cleared screen with nothing drawn on it
  // (FrameReplay::drawBlank, plain white: no icon, no text, never the game's page), so a seat's frame drawn without
  // text cannot pass as the blank, and a sleep screen drawn over it shows nothing of the match. Callers wrap it in
  // ASSERT_NO_FATAL_FAILURE, since they read pushes[0] after it.
  void expectOneBlankPush(const ExitRecord& record) {
    EXPECT_EQ(fakelock::selfDeadlocks.load(), 0) << "onExit took the RenderLock the manager holds (12cc816)";
    ASSERT_EQ(record.pushes.size(), 1u);
    EXPECT_EQ(record.pushes[0].mode, HalDisplay::HALF_REFRESH);
    EXPECT_TRUE(record.pushes[0].texts.empty()) << record.pushes[0].texts.front();
    EXPECT_EQ(fakelog::countLines(gameId + ": forced exit: blank screen pushed (half refresh)"), 1u);
    const Held held = heldAt(record.pushes[0].callsBefore);
    EXPECT_TRUE(held.cleared) << "the forced exit's push did not clear the screen";
    EXPECT_TRUE(held.drawn.empty()) << "the forced exit's push held " << held.drawn.size() << " drawing calls";
  }

  // pass-hidden in Result: seat 1's move passed the turn, and its frame under the banner is drawn.
  void reachResult() {
    enterHidden();
    expectHandOff(1);
    showSeat(1);
    tapCanvas(100, 300);
    frame();
    ASSERT_TRUE(pump([&] { return state() == "Result"; }));
    render();
  }

  // pass-hidden with seat 1's move made and Result asked for, but not yet drawn.
  void reachResultUndrawn() {
    enterHidden();
    expectHandOff(1);
    showSeat(1);
    tapCanvas(100, 300);
    frame();
    ASSERT_TRUE(pump([&] { return state() == "Result"; }));
  }

  // `confirm` scripts a Confirm release on a pass; made inside Result's push and then the hand-off screen's, each
  // passes at once.
  void expectConfirmPassesDuringPushes(const std::function<void()>& confirm) {
    ASSERT_NO_FATAL_FAILURE(reachResultUndrawn());
    renderer->onDisplay = [&] {
      confirm();
      frame();
    };
    render();
    ASSERT_EQ(state(), "HandOff") << "a Confirm while the banner was being pushed did not pass it";
    ASSERT_TRUE(renderHandOff());
    renderer->onDisplay = nullptr;
    EXPECT_EQ(state(), "Playing") << "a Confirm while the hand-off screen was being pushed did not pass it";
  }

  // HIDDEN_STORE_GAME in Result after seat 1's two moves, both stored, and the store not yet flushed.
  void reachResultWithADirtyStore() {
    installGame("hidden-store", HIDDEN_STORE_GAME);
    enterHidden("hidden-store");
    expectHandOff();
    tapToPass();
    showFrame();
    tapCanvas(100, 300);
    frame();
    ASSERT_TRUE(pump([&] { return logHas("stored 1") && activityManager.updateRequested(); }));
    render();
    tapCanvas(100, 300);  // the second move passes the turn
    frame();
    ASSERT_TRUE(pump([&] { return state() == "Result"; }));
    ASSERT_TRUE(logHas("stored 2"));
    render();
    ASSERT_FALSE(fakesd::has(storePath("hidden-store")));
  }

  // Time passes with the loop running, `ms` in all: it only advances the clock, a pass every 10 ms (or less), as the
  // device's main loop runs one every few milliseconds while nothing blocks it. The 10 ms stepping is no longer needed
  // for hand-off timing (ticket 5.13 removed the time guard that dated a press across passes, so one jump of the clock
  // passes a screen the same); it is kept so each wait still gives the loop its passes (timers, store flushes).
  void idle(const uint32_t ms) {
    for (uint32_t left = ms; left > 0;) {
      const uint32_t step = std::min<uint32_t>(left, 10);
      fakertos::advance(step);
      left -= step;
      frame();
    }
  }

  // A tap on the hand-off screen's "I'm ready" button (in the splash menu's second row, as the title screen lays it
  // out), or on Result's banner (at the bottom: TheReadyButtonFillsTheSecondMenuRowAndTheBannerIsAtTheBottom pins both
  // places); each passes the device on once its screen is on the panel. tapToPass taps the one the match's state shows.
  static constexpr int BANNER_X = 240;
  static constexpr int BANNER_Y = 740;
  void tapReady() {
    const freeink::ui::Point ready = match::readyButtonMiddle(*renderer);
    input->tap(ready.x, ready.y);
    frame();
  }
  void tapBanner() {
    input->tap(BANNER_X, BANNER_Y);
    frame();
  }
  void tapToPass() {
    if (state() == "Result") {
      tapBanner();
    } else {
      tapReady();
    }
  }

  // Renders the hand-off screen the match asked for. It names the round's first turn seat, so at a round's start its
  // render waits for the VM to name it (GameVM::turnAnnouncements): a render before pushes nothing, and the loop asks
  // again once it has. True when a push was made.
  bool renderHandOff() {
    const size_t pushes = renderer->shown.size();
    ui().forget();
    render();
    if (renderer->shown.size() > pushes) return true;
    if (!pumpToRender()) return false;
    ui().forget();
    render();
    return renderer->shown.size() > pushes;
  }

  // The push the last render made.
  const GfxRenderer::Shown& lastPush() const { return renderer->shown.back(); }
  // Whether some text of `push` holds `part`.
  static bool holds(const GfxRenderer::Shown& push, const std::string& part) {
    return std::any_of(push.texts.begin(), push.texts.end(),
                       [&](const std::string& text) { return text.find(part) != std::string::npos; });
  }

  // The hand-off screen is asked for and drawn: a full refresh of the runtime's own screen, "Player N's turn" and the
  // "I'm ready" button, and nothing of any seat's frame. `seat` (1 on) is the N it names; 0 leaves it unchecked.
  void expectHandOff(const int seat = 0) {
    ASSERT_TRUE(activityManager.updateRequested());
    const size_t pushes = renderer->shown.size();
    ASSERT_TRUE(renderHandOff()) << "no hand-off screen was pushed";
    ASSERT_EQ(renderer->shown.size(), pushes + 1);
    EXPECT_EQ(lastPush().mode, HalDisplay::FULL_REFRESH);
    expectHandOffTexts(lastPush(), seat);
  }
  // The texts of a push of the hand-off screen: "Player N's turn" (N is `seat`, unless 0) and "I'm ready", only.
  static void expectHandOffTexts(const GfxRenderer::Shown& push, const int seat) {
    ASSERT_EQ(push.texts.size(), 2u) << (push.texts.empty() ? "no text" : push.texts.front());
    if (seat > 0) {
      EXPECT_EQ(push.texts[0], "Player " + std::to_string(seat) + "'s turn");
    } else {
      EXPECT_NE(push.texts[0].find("'s turn"), std::string::npos) << push.texts[0];
    }
    EXPECT_EQ(push.texts[1], tr(STR_GAMES_READY));
  }

  // From the hand-off screen naming `seat`: its button, then the turn seat's frame, drawn in full once the VM has
  // published it.
  void showSeat(const int seat) {
    EXPECT_TRUE(holds(lastPush(), "Player " + std::to_string(seat) + "'s turn")) << "the hand-off named another seat";
    tapReady();
    ASSERT_EQ(state(), "Playing");
    showFrame();
    EXPECT_EQ(lastPush().mode, HalDisplay::FULL_REFRESH);
    EXPECT_TRUE(holds(lastPush(), "Player " + std::to_string(seat) + "'s secret: ")) << seat;
  }

  // The seat on screen moves (a tap on the canvas); the turn passes, so the match shows Result, drawn.
  void moveAndPass(const int mover, const int next) {
    tapCanvas(100, 300);
    frame();
    ASSERT_TRUE(pump([&] { return state() == "Result"; }));
    ASSERT_TRUE(activityManager.updateRequested());
    const size_t hints = UITheme::getInstance().getTheme().hints.size();
    render();
    EXPECT_EQ(UITheme::getInstance().getTheme().hints.size(), hints) << "Result drew button hints";
    EXPECT_EQ(lastPush().mode, HalDisplay::FAST_REFRESH);
    EXPECT_TRUE(holds(lastPush(), "Player " + std::to_string(mover) + "'s secret: ")) << "the mover's own frame";
    EXPECT_TRUE(holds(lastPush(), "Tap to pass to player " + std::to_string(next)));
  }
};

// A contact of seat 1's latched in Playing and lifted in Result, where the match's loop does not read gestures (as the
// light panel's swipe also ends a contact the loop never sees): the first Playing pass with no finger down frees the
// latch, so seat 2's quick tap carries seat 2's frame and is played (fix review H1).
TEST_F(HiddenPassTest, ALatchFromAContactThatEndedOutsidePlayingIsFreedForTheNextSeat) {
  enterHidden();
  expectHandOff();
  showSeat(1);
  ASSERT_TRUE(waitFor(match::roundStarted));
  fakertos::arm(fakertos::At::Log);
  tapCanvas(100, 300);  // seat 1's move: held at its "tap for seat 1"
  frame();
  ASSERT_TRUE(fakertos::waitParked());
  input->holdTouch(CANVAS_X + 100, CANVAS_Y + 300);  // another finger of seat 1's, under its frame
  fakertos::advance(100);
  frame();  // latched under seat 1's frame
  fakertos::release();
  ASSERT_TRUE(pump([&] { return state() == "Result"; }));
  render();            // the banner over the mover's frame
  input->liftTouch();  // lifted in Result, off the banner: a tap the match reads there and that passes nothing
  frame();
  ASSERT_EQ(state(), "Result");
  tapBanner();
  ASSERT_EQ(state(), "HandOff");
  expectHandOff(2);
  showSeat(2);
  fakertos::advance(200);  // seat 2's tap begins after its frame's push completed
  fakelog::clearLines();
  input->quickTap(CANVAS_X + 100, CANVAS_Y + 300);
  frame();
  ASSERT_TRUE(waitFor([] { return fakelog::anyLine("tap for seat 2") || fakelog::anyLine("Dropped a touch"); }));
  EXPECT_TRUE(logHas("tap for seat 2"));
  EXPECT_FALSE(logHas("Dropped a touch")) << "seat 2's tap carried seat 1's latched frame";
}

// A finger of seat 1's latched in Playing and held through Result and HandOff (passed on with Confirm) into seat 2's
// first Playing pass, after seat 2's frame is on the panel: it keeps its latch, so its long press is dropped. A latch
// reset on every state change would let it reach seat 2 (fix reviews H4, J7).
TEST_F(HiddenPassTest, AFingerHeldFromSeatOnesTurnIntoSeatTwosKeepsItsLatchAndIsDropped) {
  enterHidden();
  expectHandOff();
  showSeat(1);
  ASSERT_TRUE(waitFor(match::roundStarted));
  fakertos::arm(fakertos::At::Log);
  tapCanvas(100, 300);  // seat 1's move: held at its "tap for seat 1"
  frame();
  ASSERT_TRUE(fakertos::waitParked());
  input->holdTouch(CANVAS_X + 100, CANVAS_Y + 300);
  frame();  // sampled and latched under seat 1's frame
  fakertos::release();
  ASSERT_TRUE(pump([&] { return state() == "Result"; }));
  render();  // the banner
  input->click(MappedInputManager::Button::Confirm);
  frame();
  ASSERT_EQ(state(), "HandOff");
  expectHandOff();
  input->click(MappedInputManager::Button::Confirm);
  frame();  // the hand-off passes; no Playing pass has run yet
  ASSERT_EQ(state(), "Playing");
  ASSERT_TRUE(waitFor([] { return fakelog::anyLine("draw for seat 2"); }));
  renderer->forget();
  render();  // seat 2's frame is on the panel before the first Playing pass
  ASSERT_TRUE(match::drawnTexts(*renderer).size() > 0);
  fakelog::clearLines();
  fakertos::advance(500);  // the held finger becomes a long press on the first Playing pass
  frame();
  ASSERT_TRUE(waitFor([] { return fakelog::anyLine("Dropped a touch") || fakelog::anyLine("VM stopped"); }, 2000));
  EXPECT_TRUE(logHas("Dropped a touch")) << "the held finger's long press reached seat 2";
}

TEST_F(HiddenPassTest, EachSeatIsShownOnlyAfterABlankAndNoSecretCrossesIt) {
  enterHidden();
  EXPECT_EQ(state(), "HandOff");
  EXPECT_TRUE(logHas("pass-hidden: Starting -> HandOff on Started"));
  expectHandOff();
  showSeat(1);
  EXPECT_TRUE(holds(lastPush(), "Moves: 0"));
  moveAndPass(1, 2);
  tapToPass();
  EXPECT_EQ(state(), "HandOff");
  expectHandOff();
  const size_t seat2At = renderer->shown.size();
  showSeat(2);
  EXPECT_TRUE(holds(lastPush(), "river"));
  EXPECT_TRUE(holds(lastPush(), "Moves: 1"));
  moveAndPass(2, 1);
  tapToPass();
  expectHandOff();
  const size_t seat1AgainAt = renderer->shown.size();
  showSeat(1);

  // Every push in order: seat 2's secret never before seat 2's first push after the blank, and seat 1's never from then
  // until the next hand-off's blank.
  for (size_t i = 0; i < renderer->shown.size(); ++i) {
    const GfxRenderer::Shown& push = renderer->shown[i];
    if (i < seat2At) {
      EXPECT_FALSE(holds(push, "river")) << "push " << i;
    }
    if (i >= seat2At && i < seat1AgainAt) {
      EXPECT_FALSE(holds(push, "apple")) << "push " << i;
    }
  }
  EXPECT_EQ(fakelog::countLines("Result on TurnChanged"), 2u);
  EXPECT_EQ(fakelog::countLines("HandOff on Tap"), 2u);
  EXPECT_EQ(fakelog::countLines("draw for seat 2"), 2u) << "seat 2's first frame and its own frame after its move";
}

TEST_F(HiddenPassTest, TheRoundEndsInOverForEveryoneAndPlayAgainStartsOnTheBlank) {
  enterHidden();
  expectHandOff();
  showSeat(1);
  moveAndPass(1, 2);
  tapToPass();
  expectHandOff();
  showSeat(2);
  moveAndPass(2, 1);
  tapToPass();
  expectHandOff();
  showSeat(1);
  moveAndPass(1, 2);
  tapToPass();
  expectHandOff();
  showSeat(2);
  // The fourth move ends the round: over reaches each seat once, and the match goes to Over, never through Result.
  tapCanvas(100, 300);
  frame();
  ASSERT_TRUE(pump([&] { return state() == "Over"; }));
  EXPECT_EQ(fakelog::countLines("Result on TurnChanged"), 3u);
  EXPECT_TRUE(logHas("pass-hidden: Playing -> Over on RoundOver"));
  EXPECT_EQ(fakelog::countLines("over for seat 1"), 1u);
  EXPECT_EQ(fakelog::countLines("over for seat 2"), 1u);
  renderView();
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_OVER)));
  EXPECT_TRUE(holds(lastPush(), "Everyone: the secrets were apple and river")) << "the menu sits over seat 0's frame";

  // Play again: the new round begins on the blank, and its first frame is seat 1's, after the tap.
  tapOption(tr(STR_GAMES_PLAY_AGAIN));
  EXPECT_EQ(state(), "HandOff");
  EXPECT_TRUE(logHas("pass-hidden: Over -> HandOff on PlayAgain"));
  expectHandOff();
  // Paused on that blank: Resume draws the blank again, not an inert menu, so the menu has no "next round" line.
  input->click(Button::Back);
  frame();
  ASSERT_EQ(state(), "Paused");
  renderView();
  EXPECT_FALSE(ui().drewLine(tr(STR_GAMES_NEXT_ROUND_STARTING)));
  EXPECT_FALSE(holds(lastPush(), "Everyone")) << "the menu from the blank sat over the last round's frame";
  input->click(Button::Back);
  frame();
  ASSERT_EQ(state(), "HandOff");
  expectHandOff();
  showSeat(1);
  EXPECT_TRUE(holds(lastPush(), "Moves: 0"));
  EXPECT_EQ(fakelog::countLines("Round started"), 2u);
}

TEST_F(HiddenPassTest, PauseFromResultOrTheBlankReturnsThereAndTheBlanksPauseMenuSitsOnNoFrame) {
  enterHidden();
  expectHandOff();
  // The new match's first blank: no round has been played, so the pause menu says nothing of a next one.
  input->click(Button::Back);
  frame();
  ASSERT_EQ(state(), "Paused");
  renderView();
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_PAUSED)));
  EXPECT_FALSE(ui().drewLine(tr(STR_GAMES_NEXT_ROUND_STARTING)));
  input->click(Button::Back);
  frame();
  ASSERT_EQ(state(), "HandOff");
  expectHandOff();
  showSeat(1);
  moveAndPass(1, 2);

  input->click(Button::Back);
  frame();
  ASSERT_EQ(state(), "Paused");
  renderView();
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_PAUSED)));
  EXPECT_TRUE(holds(lastPush(), "apple")) << "the mover's frame stays under the menu";
  tapOption(tr(STR_GAMES_RESUME));
  EXPECT_EQ(state(), "Result");
  render();
  EXPECT_TRUE(holds(lastPush(), "Tap to pass to player 2"));

  tapToPass();
  ASSERT_EQ(state(), "HandOff");
  expectHandOff();
  EXPECT_TRUE(activity->handleHomeGesture());  // Home pauses the blank too
  ASSERT_EQ(state(), "Paused");
  renderView();
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_PAUSED)));
  EXPECT_FALSE(ui().drewLine(tr(STR_GAMES_NEXT_ROUND_STARTING))) << "the pause menu of a hand-off is no gap";
  EXPECT_FALSE(holds(lastPush(), "apple")) << "a pause menu from the blank showed seat 1's frame";
  EXPECT_FALSE(holds(lastPush(), "river"));
  EXPECT_TRUE(holds(lastPush(), tr(STR_GAMES_PAUSED)));
  input->click(Button::Back);  // Back in the pause menu resumes
  frame();
  EXPECT_EQ(state(), "HandOff");
  expectHandOff();
  showSeat(2);
}

// A tap that seat 1 makes right after its turn-passing move, queued behind it, is seat 1's: its input runs, and the
// move it returns is discarded (seat 1 is no longer the turn seat), so nothing of seat 2 is drawn or played.
TEST_F(HiddenPassTest, ATapQueuedBehindTheTurnPassingMoveReachesTheMoverAndIsNeverApplied) {
  enterHidden();
  expectHandOff();
  showSeat(1);
  ASSERT_TRUE(waitFor(match::roundStarted));  // the task's own log line comes after the first frame: arm past it
  fakertos::arm(fakertos::At::Log);           // seat 1's input logs first: held there
  tapCanvas(100, 300);
  frame();
  ASSERT_TRUE(fakertos::waitParked());
  tapCanvas(200, 300);  // queued behind the move, while the match still shows seat 1's canvas
  frame();
  fakertos::pass();  // the move's "apply seat 1"
  ASSERT_TRUE(fakertos::waitParked());
  fakertos::pass();  // its "draw for seat 1"
  ASSERT_TRUE(fakertos::waitParked());
  fakertos::pass();  // its frame is published and the turn counted; the queued tap is held at its "tap for seat 1"
  ASSERT_TRUE(fakertos::waitParked());
  ASSERT_TRUE(pump([&] { return state() == "Result"; }));
  render();
  EXPECT_TRUE(holds(lastPush(), "Tap to pass to player 2"));
  activityManager.markRendered();
  fakertos::release();
  // The queued tap's frame, seat 1's again, is newer than the one under the banner: Result asks to draw it.
  ASSERT_TRUE(pump([&] { return fakelog::countLines("draw for seat 1") == 3u && activityManager.updateRequested(); }));
  EXPECT_EQ(fakelog::countLines("tap for seat 1"), 2u);
  EXPECT_EQ(fakelog::countLines("apply seat 1"), 1u) << "the queued tap's move was applied";
  EXPECT_FALSE(logHas("apply seat 2"));
  EXPECT_FALSE(logHas("tap for seat 2"));
  EXPECT_FALSE(logHas("draw for seat 2"));
  render();
  EXPECT_TRUE(holds(lastPush(), "Tap to pass to player 2"));
  EXPECT_TRUE(holds(lastPush(), "Moves: 1"));
}

// Two taps queued behind seat 1's turn-passing move: the first is still being played, and the second still waits in
// the queue, when the hand-off's tap asks for seat 2. The VM plays what is queued under the view it was meant for
// before it shows seat 2, so the second tap reaches seat 1 too (its move discarded), never seat 2.
TEST_F(HiddenPassTest, ATapStillQueuedWhenTheNextSeatIsAskedForReachesTheMoverNotTheNextSeat) {
  enterHidden();
  expectHandOff();
  showSeat(1);
  ASSERT_TRUE(waitFor(match::roundStarted));
  fakertos::arm(fakertos::At::Log);
  tapCanvas(100, 300);  // A, seat 1's move: held at its "tap for seat 1"
  frame();
  ASSERT_TRUE(fakertos::waitParked());
  tapCanvas(200, 300);  // B
  frame();
  tapCanvas(300, 300);  // C
  frame();
  fakertos::pass();  // A's "apply seat 1"
  ASSERT_TRUE(fakertos::waitParked());
  fakertos::pass();  // A's "draw for seat 1"
  ASSERT_TRUE(fakertos::waitParked());
  fakertos::pass();  // A's frame is published and the turn counted; B is held at its "tap for seat 1", C queued
  ASSERT_TRUE(fakertos::waitParked());
  ASSERT_TRUE(pump([&] { return state() == "Result"; }));
  render();
  tapToPass();
  ASSERT_EQ(state(), "HandOff");
  expectHandOff();
  tapToPass();  // asks for seat 2 while C is still queued
  ASSERT_EQ(state(), "Playing");
  fakertos::release();
  ASSERT_TRUE(pumpToRender());
  render();
  EXPECT_TRUE(holds(lastPush(), "Player 2's secret: river"));
  EXPECT_EQ(fakelog::countLines("tap for seat 1"), 3u) << "C reached another seat than the one that made it";
  EXPECT_FALSE(logHas("tap for seat 2"));
  EXPECT_EQ(fakelog::countLines("apply seat 1"), 1u);
  EXPECT_FALSE(logHas("apply seat 2")) << "seat 1's queued tap was applied as seat 2's move";
  EXPECT_LT(fakelog::countLines("draw for seat 2"), 2u);
}

// The seat gate: seat 1's tap queued behind its move is still being played when the hand-off's tap asks for seat 2, so
// seat 1's late frame is the newest one published until seat 2's is. A render then draws nothing and the loop asks for
// none; seat 2's frame is the first one after the blank.
TEST_F(HiddenPassTest, SeatOnesLateFrameIsNeverDrawnAfterTheBlank) {
  enterHidden();
  expectHandOff();
  showSeat(1);
  ASSERT_TRUE(waitFor(match::roundStarted));  // the task's own log line comes after the first frame: arm past it
  fakertos::arm(fakertos::At::Log);
  tapCanvas(100, 300);  // A, seat 1's move: held at its "tap for seat 1"
  frame();
  ASSERT_TRUE(fakertos::waitParked());
  tapCanvas(200, 300);  // B: queued behind A
  frame();
  fakertos::pass();  // A's "apply seat 1"
  ASSERT_TRUE(fakertos::waitParked());
  fakertos::pass();  // A's "draw for seat 1"
  ASSERT_TRUE(fakertos::waitParked());
  fakertos::pass();  // A's frame is published and the turn counted; B is held at its "tap for seat 1"
  ASSERT_TRUE(fakertos::waitParked());
  ASSERT_TRUE(pump([&] { return state() == "Result"; }));
  render();
  tapToPass();
  ASSERT_EQ(state(), "HandOff");
  expectHandOff();
  tapToPass();  // asks for seat 2, while B is still seat 1's
  ASSERT_EQ(state(), "Playing");
  fakertos::pass();  // B's "draw for seat 1"
  ASSERT_TRUE(fakertos::waitParked());
  fakertos::pass();  // B's frame is published; the VM is held at seat 2's "draw for seat 2"
  ASSERT_TRUE(fakertos::waitParked());
  ASSERT_EQ(fakelog::countLines("draw for seat 1"), 3u);
  // Paused here, before seat 2's frame: the menu sits on no frame, never seat 1's late one.
  input->click(Button::Back);
  frame();
  ASSERT_EQ(state(), "Paused");
  renderView();
  EXPECT_FALSE(holds(lastPush(), "apple")) << "the pause menu sat over seat 1's frame while seat 2 had the device";
  input->click(Button::Back);  // resumes into the gate
  frame();
  ASSERT_EQ(state(), "Playing");
  activityManager.markRendered();
  for (int i = 0; i < 10; ++i) frame();
  EXPECT_FALSE(activityManager.updateRequested()) << "the loop asked to render seat 1's late frame";
  const size_t pushes = renderer->shown.size();
  render();
  EXPECT_EQ(renderer->shown.size(), pushes) << "a render after the blank pushed seat 1's late frame";
  fakertos::release();
  ASSERT_TRUE(pumpToRender());
  render();
  ASSERT_EQ(renderer->shown.size(), pushes + 1);
  EXPECT_EQ(lastPush().mode, HalDisplay::FULL_REFRESH);
  EXPECT_TRUE(holds(lastPush(), "Player 2's secret: river"));
  EXPECT_FALSE(holds(lastPush(), "apple"));
}

// A timer that falls due during the hand-off is polled and held, and reaches the next seat right after its first frame.
TEST_F(HiddenPassTest, ATimerDueOnTheBlankReachesTheNextSeatAfterItsFirstFrame) {
  enterHidden();
  expectHandOff();
  showSeat(1);
  moveAndPass(1, 2);  // seat 1's tap re-armed the 5 s timer at 1000 ms
  tapToPass();
  ASSERT_EQ(state(), "HandOff");
  expectHandOff();
  fakertos::advance(5000);
  frame();  // the blank's loop polls the timer: due now, and held by the VM
  showSeat(2);
  ASSERT_TRUE(waitFor([&] { return logHas("timer for seat 2"); }));
  const std::vector<std::string> lines = fakelog::snapshot();
  const auto at = [&](const std::string& part) {
    return std::find_if(lines.begin(), lines.end(),
                        [&](const std::string& line) { return line.find(part) != std::string::npos; }) -
           lines.begin();
  };
  EXPECT_LT(at("draw for seat 2"), at("timer for seat 2"));
  EXPECT_FALSE(logHas("timer for seat 1")) << "the timer reached the seat that had passed the device on";
}

// The loop watches the VM in Result as in a menu: a call stuck there ends in the error view.
TEST_F(HiddenPassTest, ACallStuckInResultIsStoppedIntoTheErrorView) {
  enterHidden();
  expectHandOff();
  showSeat(1);
  ASSERT_TRUE(waitFor(match::roundStarted));
  fakertos::arm();      // the clock: seat 1's input reads it in ch.timer.after
  tapCanvas(100, 300);  // the move: held at its clock read
  frame();
  ASSERT_TRUE(fakertos::waitParked());
  tapCanvas(200, 300);  // queued behind it
  frame();
  fakertos::pass();  // the move ends and the turn passes; the queued tap is held at its clock read, inside Lua
  ASSERT_TRUE(fakertos::waitParked());
  ASSERT_TRUE(pump([&] { return state() == "Result"; }));
  frame();  // the first poll that sees the call starts its clock
  fakertos::advance(3001);
  frame();
  EXPECT_EQ(state(), "Error");
  expectErrorView(tr(STR_GAMES_ERROR), tr(STR_GAMES_NOT_RESPONDING));
  fakertos::release();
  // Sleep from the error view: the VM is gone, so no seat's frame is on the panel, and the exit pushes nothing.
  ASSERT_TRUE(fakertos::waitNoTasks());
  const ExitRecord record = sleepRecordingPushes();
  EXPECT_EQ(fakelock::selfDeadlocks.load(), 0);
  EXPECT_TRUE(record.pushes.empty()) << "the forced exit from the error view pushed the blank";
  EXPECT_FALSE(logHas("blank screen pushed"));
}

// ---- the forced exit's blank hand-off (epic-pass-and-play entry 6; AD-12, AD-20) ----

// Sleep on a seat's frame: the exit pushes the blank once, a half refresh with no text, after the VM's stop and
// before any SD op, so the store flush comes after it. The double's push moves no clock; the device's half refresh
// is measured by entry 11.
TEST_F(HiddenPassTest, TheForcedExitOnASeatsFramePushesTheBlankAfterTheJoinAndBeforeTheStore) {
  installGame("hidden-store", HIDDEN_STORE_GAME);
  enterHidden("hidden-store");
  expectHandOff();
  tapToPass();
  ASSERT_EQ(state(), "Playing");
  showFrame();
  EXPECT_TRUE(holds(lastPush(), "Player 1's secret: apple"));
  tapCanvas(100, 300);  // the first of seat 1's two moves: the store is dirty, and the turn stays
  frame();
  ASSERT_TRUE(pump([&] { return logHas("stored 1") && activityManager.updateRequested(); }));
  render();
  ASSERT_EQ(state(), "Playing");
  EXPECT_TRUE(holds(lastPush(), "Player 1's secret: apple")) << "seat 1's frame is on the panel";
  ASSERT_FALSE(fakesd::has(storePath("hidden-store")));

  const ExitRecord record = sleepRecordingPushes();
  ASSERT_NO_FATAL_FAILURE(expectOneBlankPush(record));
  EXPECT_EQ(record.pushes[0].sdOps, record.sdOpsBefore) << "an SD op ran before the blank was pushed";
  EXPECT_FALSE(record.pushes[0].storeOnCard) << "the store was flushed before the blank was pushed";
  EXPECT_TRUE(fakesd::has(storePath("hidden-store"))) << "the store flush after the push";
  EXPECT_FALSE(logHas("skipped"));
  EXPECT_EQ(state(), "Leaving");
  EXPECT_EQ(activityManager.asks.goToGames, 0);  // a forced exit does not navigate
}

// Sleep in Result (the banner over the mover's frame): the same push, which holds nothing of the mover's frame.
TEST_F(HiddenPassTest, TheForcedExitInResultPushesTheBlank) {
  enterHidden();
  expectHandOff();
  showSeat(1);
  moveAndPass(1, 2);
  ASSERT_EQ(state(), "Result");
  const ExitRecord record = sleepRecordingPushes();
  ASSERT_NO_FATAL_FAILURE(expectOneBlankPush(record));
  EXPECT_FALSE(record.pushes[0].abandonLogged);
}

// Sleep on the hand-off screen (HandOff): the plain white blank is pushed over it.
TEST_F(HiddenPassTest, TheForcedExitOnTheBlankPushesTheBlankAgain) {
  enterHidden();
  expectHandOff();
  showSeat(1);
  moveAndPass(1, 2);
  tapToPass();
  ASSERT_EQ(state(), "HandOff");
  expectHandOff();
  const ExitRecord record = sleepRecordingPushes();
  ASSERT_NO_FATAL_FAILURE(expectOneBlankPush(record));
}

// A VM held inside ch.log (a locked binding) neither joins nor can be deleted: the blank goes up once the stop's wait
// has run out, before the abandon begins its own wait, and the whole exit stays within the bound.
TEST_F(HiddenPassTest, TheForcedExitWithAStuckVmPushesTheBlankBetweenTheJoinAndTheAbandon) {
  enterHidden();
  expectHandOff();
  showSeat(1);
  ASSERT_TRUE(waitFor(match::roundStarted));  // the task's own log line comes after the first frame: arm past it
  expectCleanPsram = false;                   // the abandon leaks the VM and the store slot on purpose
  fakertos::arm(fakertos::At::Log);           // seat 1's input logs first: held there
  tapCanvas(100, 300);
  frame();
  ASSERT_TRUE(fakertos::waitParked());

  const ExitRecord record = sleepRecordingPushes();
  ASSERT_NO_FATAL_FAILURE(expectOneBlankPush(record));
  EXPECT_GE(record.pushes[0].sinceExitMs, 500u) << "the blank was pushed before the stop's wait ran out";
  EXPECT_LT(record.pushes[0].sinceExitMs, 1000u);
  EXPECT_FALSE(record.pushes[0].abandonLogged) << "the abandon began before the blank was pushed";
  EXPECT_TRUE(logHas("did not stop within"));
  // docs/crosshatch/game-canvas.md, The forced exit's bound: two waits of 500 ms, each late by at most an iteration.
  EXPECT_LE(record.tookMs, 1030u);
  fakertos::release();  // the leaked task ends at its next hook: the cancel flag is set
  EXPECT_TRUE(fakertos::waitNoTasks());
}

// The push is not an SD step, but its time counts against the SD steps' deadline. The double's push moves no clock, so
// these tests move it by hand inside the push: one that ends 1 ms short of FORCED_EXIT_DEADLINE_MS after onExit()
// began leaves the store flush in time, and one that ends on it skips the flush, as any step that would start late.
TEST_F(HiddenPassTest, TheBlanksRefreshCountsAgainstTheSdStepsDeadline) {
  reachResultWithADirtyStore();
  ASSERT_FALSE(HasFatalFailure());
  const uint64_t began = fakertos::S().nowMs.load();
  const ExitRecord record = sleepRecordingPushes([] { fakertos::advance(GameMatchActivity::FORCED_EXIT_DEADLINE_MS); });
  ASSERT_NO_FATAL_FAILURE(expectOneBlankPush(record));
  EXPECT_GE(fakertos::S().nowMs.load() - began, uint64_t{GameMatchActivity::FORCED_EXIT_DEADLINE_MS});
  EXPECT_TRUE(logHas("skipped the ch.store flush"));
  EXPECT_FALSE(fakesd::has(storePath("hidden-store")));
}

TEST_F(HiddenPassTest, ABlankPushThatEndsInsideTheDeadlineLeavesTheStoreFlushInTime) {
  reachResultWithADirtyStore();
  ASSERT_FALSE(HasFatalFailure());
  const uint64_t began = fakertos::S().nowMs.load();
  const ExitRecord record = sleepRecordingPushes([began] {
    const uint64_t spent = fakertos::S().nowMs.load() - began;
    fakertos::advance(GameMatchActivity::FORCED_EXIT_DEADLINE_MS - 1 - spent);
  });
  ASSERT_NO_FATAL_FAILURE(expectOneBlankPush(record));
  EXPECT_FALSE(logHas("skipped"));
  EXPECT_TRUE(fakesd::has(storePath("hidden-store"))) << "the store flush started 1 ms before the deadline";
}

// The deadline gates the SD steps only: a stop that has already run past it (a slow card from the stop's wake on) still
// pushes the blank, and only the store flush after it is skipped.
TEST_F(HiddenPassTest, TheBlankIsPushedEvenPastTheDeadline) {
  reachResultWithADirtyStore();
  ASSERT_FALSE(HasFatalFailure());
  bool slow = false;
  fakertos::S().onLoopNotify = [&] {
    if (!slow) fakertos::advance(GameMatchActivity::FORCED_EXIT_DEADLINE_MS + 100);
    slow = true;
  };
  const ExitRecord record = sleepRecordingPushes();
  fakertos::S().onLoopNotify = nullptr;
  ASSERT_TRUE(slow) << "the stop did not wake the VM";
  ASSERT_NO_FATAL_FAILURE(expectOneBlankPush(record));
  EXPECT_GT(record.pushes[0].sinceExitMs, uint64_t{GameMatchActivity::FORCED_EXIT_DEADLINE_MS});
  EXPECT_TRUE(logHas("skipped the ch.store flush"));
  EXPECT_FALSE(fakesd::has(storePath("hidden-store")));
}

// Sleep with the pause menu over a seat's frame: the blank replaces both.
TEST_F(HiddenPassTest, TheForcedExitFromThePauseMenuOverASeatsFramePushesTheBlank) {
  enterHidden();
  expectHandOff();
  showSeat(1);
  input->click(Button::Back);
  frame();
  ASSERT_EQ(state(), "Paused");
  renderView();
  ASSERT_TRUE(holds(lastPush(), "apple")) << "the pause menu sits over seat 1's frame";
  const ExitRecord record = sleepRecordingPushes();
  ASSERT_NO_FATAL_FAILURE(expectOneBlankPush(record));
}

// A script error keeps the VM (only a stuck call frees it on the way to Error), so a forced exit from that error view
// still pushes the blank: the rule is any forced exit of a hidden pass match with a VM, whatever the panel shows.
TEST_F(HiddenPassTest, TheForcedExitFromAScriptErrorWithItsVmStillPushesTheBlank) {
  installGame("hidden-error", HIDDEN_ERROR_GAME);
  enterHidden("hidden-error");
  expectHandOff();
  tapToPass();
  showFrame();
  tapCanvas(100, 300);
  frame();
  ASSERT_TRUE(pump([&] { return state() == "Error"; }));
  expectErrorView(tr(STR_GAMES_ERROR), "hidden boom");
  const ExitRecord record = sleepRecordingPushes();
  ASSERT_NO_FATAL_FAILURE(expectOneBlankPush(record));
}

// A user Leave from a pause menu over a seat's frame (cross-story review row 1) pushes the blank, a half refresh with
// no text, after the VM's stop and before it asks for Games: if Games then ran out of memory, this match would stay
// current with no VM, and the seat's frame must not stay on the panel. The exit after it finds the blank there and
// pushes nothing.
TEST_F(HiddenPassTest, LeavingFromASeatsFramePushesTheBlankBeforeGamesAndTheExitAfterItNothing) {
  enterHidden();
  expectHandOff();
  showSeat(1);
  input->click(Button::Back);
  frame();
  ASSERT_EQ(state(), "Paused");
  renderView();
  ASSERT_TRUE(holds(lastPush(), "apple")) << "the pause menu sits over seat 1's frame";
  const size_t pushes = renderer->shown.size();
  size_t pushesAtGames = 0;
  bool vmStoppedAtGames = false;
  activityManager.onGoToGames = [&] {
    pushesAtGames = renderer->shown.size();
    vmStoppedAtGames = logHas("VM stopped");
  };
  tapOption(tr(STR_GAMES_LEAVE));
  activityManager.onGoToGames = nullptr;
  ASSERT_EQ(state(), "Leaving");
  EXPECT_EQ(activityManager.asks.goToGames, 1);
  EXPECT_EQ(pushesAtGames, pushes + 1) << "the blank was not pushed before Games was asked for";
  EXPECT_TRUE(vmStoppedAtGames);
  ASSERT_EQ(renderer->shown.size(), pushes + 1);
  EXPECT_EQ(lastPush().mode, HalDisplay::HALF_REFRESH);
  EXPECT_TRUE(lastPush().texts.empty()) << lastPush().texts.front();
  EXPECT_EQ(fakelog::countLines("pass-hidden: leave: blank screen pushed (half refresh)"), 1u);
  const ExitRecord record = sleepRecordingPushes();
  EXPECT_TRUE(record.pushes.empty()) << "the exit after Leave pushed again";
  EXPECT_FALSE(logHas("forced exit: blank screen pushed"));
  EXPECT_EQ(fakelock::selfDeadlocks.load(), 0);
}

// Cross-story row 1's trigger (fix review F10): Games cannot open. The manager double's goToGames() replaces nothing,
// as the device's does when makeUniqueNoThrow<GamesLauncherActivity> fails, so the match stays current in Leaving with
// no VM. The blank Leave pushed stays the last push, a render pushes nothing, Home goes Home, and a later sleep pushes
// nothing.
TEST_F(HiddenPassTest, ALeaveWhoseGamesScreenCannotOpenLeavesTheBlankAndASleepPushesNothing) {
  enterHidden();
  expectHandOff();
  showSeat(1);
  input->click(Button::Back);
  frame();
  ASSERT_EQ(state(), "Paused");
  renderView();
  ASSERT_TRUE(holds(lastPush(), "apple"));
  tapOption(tr(STR_GAMES_LEAVE));
  ASSERT_EQ(state(), "Leaving");
  EXPECT_EQ(activityManager.asks.goToGames, 1);
  EXPECT_TRUE(activityManager.replacements.empty()) << "Games opened: the refusal was not staged";
  EXPECT_EQ(lastPush().mode, HalDisplay::HALF_REFRESH);
  EXPECT_TRUE(lastPush().texts.empty()) << "the seat's frame is still on the panel";
  const size_t pushes = renderer->shown.size();
  for (int i = 0; i < 5; ++i) frame();
  render();
  EXPECT_EQ(renderer->shown.size(), pushes) << "a match that let go pushed again";
  EXPECT_FALSE(activity->handleHomeGesture()) << "Home goes Home from a match that let go";
  const ExitRecord record = sleepRecordingPushes();
  EXPECT_TRUE(record.pushes.empty()) << "the sleep pushed over the blank";
  EXPECT_EQ(fakelock::selfDeadlocks.load(), 0);
}

// A Leave from a screen that holds no seat's private frame pushes nothing: the Over menu sits over seat 0's frame,
// which is everyone's, and the blank's pause menu sits on no frame. The exit after it pushes nothing either.
TEST_F(HiddenPassTest, LeavingFromOverOrTheBlanksPauseMenuPushesNothing) {
  for (const bool fromOver : {true, false}) {
    SCOPED_TRACE(fromOver ? "from Over" : "from the blank's pause menu");
    if (activity) {
      if (!exited) activityManager.exitHolding(*activity);
      activityManager.destroyHolding(activity);
      ASSERT_TRUE(fakertos::waitNoTasks());
    }
    exited = false;
    fakelog::clearLines();
    enterHidden();
    expectHandOff();
    if (fromOver) {
      for (int turn = 0; turn < 3; ++turn) {
        const int mover = turn % 2 + 1;
        showSeat(mover);
        moveAndPass(mover, 3 - mover);
        tapToPass();
        expectHandOff();
      }
      showSeat(2);
      tapCanvas(100, 300);  // the fourth move ends the round
      frame();
      ASSERT_TRUE(pump([&] { return state() == "Over"; }));
      renderView();
      ASSERT_TRUE(holds(lastPush(), "Everyone: the secrets were apple and river"));
    } else {
      input->click(Button::Back);
      frame();
      ASSERT_EQ(state(), "Paused");
      renderView();
      ASSERT_FALSE(holds(lastPush(), "apple"));
    }
    const size_t pushes = renderer->shown.size();
    tapOption(tr(STR_GAMES_LEAVE));
    ASSERT_EQ(state(), "Leaving");
    EXPECT_EQ(renderer->shown.size(), pushes) << "Leave pushed a screen";
    const ExitRecord record = sleepRecordingPushes();
    EXPECT_TRUE(record.pushes.empty());
    EXPECT_FALSE(logHas("blank screen pushed"));
    EXPECT_EQ(fakelock::selfDeadlocks.load(), 0);
  }
}

// A render in HandOff with the hand-off screen already on the panel (the light panel closed, a status repaint) repaints
// it, the same screen, with a fast refresh (cross-story review row 6); the hand-off screen entered from any other
// screen is a full refresh.
TEST_F(HiddenPassTest, ARepaintOfTheBlankIsAFastRefreshAndEnteringItIsFull) {
  enterHidden();
  expectHandOff(1);  // from the screen before the match: full
  render();
  EXPECT_EQ(lastPush().mode, HalDisplay::FAST_REFRESH);
  expectHandOffTexts(lastPush(), 1);
  render();
  EXPECT_EQ(lastPush().mode, HalDisplay::FAST_REFRESH);
  showSeat(1);
  moveAndPass(1, 2);
  tapToPass();
  ASSERT_EQ(state(), "HandOff");
  expectHandOff(2);  // from Result's banner over seat 1's frame: full
  render();
  EXPECT_EQ(lastPush().mode, HalDisplay::FAST_REFRESH);
  expectHandOffTexts(lastPush(), 2);
  input->click(Button::Back);
  frame();
  ASSERT_EQ(state(), "Paused");
  renderView();
  input->click(Button::Back);
  frame();
  ASSERT_EQ(state(), "HandOff");
  expectHandOff();  // from the pause menu: full
}

// A call stuck on a seat's frame frees the VM on the way to Error; a forced exit before the error view is drawn finds
// no VM but a seat's frame on the panel, and pushes the blank before any SD step (cross-story review row 1). Nothing is
// pushed after it.
TEST_F(HiddenPassTest, AForcedExitWithNoVmOverASeatsFramePushesTheBlankAndNothingAfter) {
  enterHidden();
  expectHandOff();
  showSeat(1);
  ASSERT_TRUE(waitFor(match::roundStarted));
  fakertos::arm();      // the clock: seat 1's input reads it in ch.timer.after
  tapCanvas(100, 300);  // the move: held at its clock read, inside Lua
  frame();
  ASSERT_TRUE(fakertos::waitParked());
  frame();  // the first poll that sees the call starts its clock
  fakertos::advance(3001);
  frame();
  ASSERT_EQ(state(), "Error");
  EXPECT_TRUE(logHas("a call ran over"));
  fakertos::release();
  ASSERT_TRUE(fakertos::waitNoTasks());
  // The error view is asked for but not yet drawn: seat 1's frame is still on the panel.
  ASSERT_TRUE(holds(lastPush(), "apple"));
  const ExitRecord record = sleepRecordingPushes();
  EXPECT_EQ(fakelock::selfDeadlocks.load(), 0) << "onExit's no-VM push took the RenderLock the manager holds (12cc816)";
  ASSERT_NO_FATAL_FAILURE(expectOneBlankPush(record));
  EXPECT_EQ(record.pushes[0].sdOps, record.sdOpsBefore) << "an SD step ran before the blank";
  const size_t pushes = renderer->shown.size();
  render();
  EXPECT_EQ(renderer->shown.size(), pushes) << "a push after the forced exit";
}

// The VM's view lags the match (cross-story review row 9): a tap queued behind seat 1's turn-passing move is played,
// under the mover's view, while the match is already on the blank. Its frame (seat 1's again) never reaches the panel:
// no push after the blank holds seat 1's text until seat 2's frame, whatever is drawn meanwhile (a repaint, the pause
// menu).
TEST_F(HiddenPassTest, AMoversFramePlayedWhileTheMatchIsOnTheBlankNeverReachesThePanel) {
  enterHidden();
  expectHandOff();
  showSeat(1);
  ASSERT_TRUE(waitFor(match::roundStarted));  // the task's own log line comes after the first frame: arm past it
  fakertos::arm(fakertos::At::Log);
  tapCanvas(100, 300);  // A, seat 1's move: held at its "tap for seat 1"
  frame();
  ASSERT_TRUE(fakertos::waitParked());
  tapCanvas(200, 300);  // B: queued behind A, under seat 1's frame
  frame();
  fakertos::pass();  // A's "apply seat 1"
  ASSERT_TRUE(fakertos::waitParked());
  fakertos::pass();  // A's "draw for seat 1"
  ASSERT_TRUE(fakertos::waitParked());
  fakertos::pass();  // A's frame is published and the turn counted; B is held at its "tap for seat 1"
  ASSERT_TRUE(fakertos::waitParked());
  ASSERT_TRUE(pump([&] { return state() == "Result"; }));
  render();
  tapToPass();
  ASSERT_EQ(state(), "HandOff");
  const size_t blankAt = renderer->shown.size();
  expectHandOff();
  fakertos::release();  // B plays to the mover and seat 1's frame is published again, the match on the blank
  ASSERT_TRUE(waitFor([] { return fakelog::countLines("draw for seat 1") == 3u; }));
  for (int i = 0; i < 10; ++i) frame();
  render();  // a repaint on the blank
  input->click(Button::Back);
  frame();
  ASSERT_EQ(state(), "Paused");
  renderView();
  input->click(Button::Back);
  frame();
  ASSERT_EQ(state(), "HandOff");
  expectHandOff();
  showSeat(2);
  ASSERT_GT(renderer->shown.size(), blankAt);
  for (size_t i = blankAt; i < renderer->shown.size(); ++i) {
    EXPECT_FALSE(holds(renderer->shown[i], "apple")) << "push " << i << " after the blank holds seat 1's frame";
  }
  EXPECT_FALSE(logHas("tap for seat 2")) << "B reached seat 2";
}

// ---- the hand-off screen (epic-pass-and-play entry 12, as the owner redesigned it 2026-10-02; DESIGN.md
// hand-off-screen, ready-button) ----

// The drawn line that is exactly `text`, or null.
const screen::DrawnText* drawnLine(const screen::RecordingTarget& target, const std::string& text) {
  for (const screen::DrawnText& drawn : target.drawn)
    if (drawn.text == text) return &drawn;
  return nullptr;
}

// The hand-off screen mirrors the title screen: on a cleared screen with no header and no status strip, the splash
// band where the title screen has it, here with the game's own icon (pass-hidden has no handoff.png, no title.png, no
// icon.png, and names no library icon, so game-controller, as its launcher row shows) at 128 px, black, centred in the
// band, and nothing else drawn on the renderer (no eye-closed icon, no game command); "Player 1's turn", plain text
// centred in the title screen's first menu row; and the framed "I'm ready" button filling its second row
// (GameSplashLayout::rowRect, which ModePickerTest pins to the rows the title screen's list draws).
TEST_F(HiddenPassTest, TheHandOffScreenIsTheTitleScreensBandWithTheTurnLineAndTheReadyButtonInItsRows) {
  enterHidden();
  renderer->forget();
  UITheme::getInstance().getTheme().reset();
  expectHandOff(1);
  EXPECT_TRUE(UITheme::getInstance().getTheme().calls.empty()) << "a header or hints were drawn";
  const Held held = heldAt(lastPush().callsBefore);
  EXPECT_TRUE(held.cleared);
  expectSameFills(held.drawn, iconFills(GameRowIcon::FALLBACK_NAME));
  const std::vector<freeink::ui::Rect> rows = match::splashMenuRows(*renderer, 2);
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[0].y, GameSplashLayout::bandTop(*renderer) + GameSplashLayout::BAND) << "the rows follow the band";
  const screen::DrawnText* turn = drawnLine(ui(), "Player 1's turn");
  const screen::DrawnText* ready = drawnLine(ui(), tr(STR_GAMES_READY));
  ASSERT_NE(turn, nullptr) << ui().joined();
  ASSERT_NE(ready, nullptr) << ui().joined();
  // The turn line, centred in the first row, both ways.
  EXPECT_GE(turn->rect.y, rows[0].y);
  EXPECT_LE(turn->rect.bottom(), rows[0].bottom());
  EXPECT_LE(std::abs(turn->rect.x + turn->rect.width / 2 - (rows[0].x + rows[0].width / 2)), 1);
  EXPECT_LE(std::abs(turn->rect.y + turn->rect.height / 2 - (rows[0].y + rows[0].height / 2)), 1);
  // The button's frame, the one stroke on the screen, is the second row; its label is inside it.
  ASSERT_EQ(ui().strokeRects.size(), 1u) << "the turn line is no button";
  const freeink::ui::Rect frame = ui().strokeRects[0].rect;
  EXPECT_EQ(frame.x, rows[1].x);
  EXPECT_EQ(frame.y, rows[1].y);
  EXPECT_EQ(frame.width, rows[1].width);
  EXPECT_EQ(frame.height, rows[1].height);
  EXPECT_TRUE(rows[1].contains(ready->rect.x, ready->rect.y));
  EXPECT_LE(std::abs(ready->rect.x + ready->rect.width / 2 - (rows[1].x + rows[1].width / 2)), 1);
}

// Result's banner sits at the bottom of the screen and the hand-off's button in the title screen's second two-line row
// (DESIGN.md): pins the two places the helpers tap (readyButtonMiddle, BANNER_Y). The two overlap on the X4 Pro (and
// here; game-canvas.md, Taps), so a stray second tap on the banner's spot can pass the hand-off too, which the owner
// accepts (2026-10-03): one tap passes a screen at once, with no time guard (the plain tap tests below).
TEST_F(HiddenPassTest, TheReadyButtonFillsTheSecondMenuRowAndTheBannerIsAtTheBottom) {
  enterHidden();
  expectHandOff(1);
  ASSERT_EQ(ui().strokeRects.size(), 1u);
  const freeink::ui::Rect ready = ui().strokeRects[0].rect;
  const freeink::ui::Point middle = match::readyButtonMiddle(*renderer);
  EXPECT_TRUE(ready.contains(middle.x, middle.y));
  showSeat(1);
  tapCanvas(100, 300);
  frame();
  ASSERT_TRUE(pump([&] { return state() == "Result"; }));
  ui().forget();
  render();
  ASSERT_EQ(ui().strokeRects.size(), 1u);
  const freeink::ui::Rect banner = ui().strokeRects[0].rect;
  EXPECT_TRUE(banner.contains(BANNER_X, BANNER_Y));
  int top = 0, right = 0, bottom = 0, left = 0;
  renderer->getOrientedViewableTRBL(&top, &right, &bottom, &left);
  EXPECT_EQ(banner.bottom(), 800 - bottom - freeink::ui::ThemeTokens{}.spaceLg) << "at the bottom of the safe area";
  EXPECT_EQ(banner.width, (480 - left - right) * 4 / 5);
}

// Only the button passes the hand-off screen, and only the banner passes Result: a tap anywhere else on either (the
// band, the turn line, under the button) does nothing (R4 as amended 2026-10-02), and Confirm still passes both.
TEST_F(HiddenPassTest, ATapOffTheButtonOrOffTheBannerPassesNothing) {
  enterHidden();
  expectHandOff(1);
  const std::vector<freeink::ui::Rect> rows = match::splashMenuRows(*renderer, 2);
  const std::vector<std::pair<int, int>> offTheButton{
      {240, bandMiddleY(*renderer)}, {240, rows[0].y + rows[0].height / 2}, {240, rows[1].bottom() + 4}, {240, 20}};
  for (const auto& [x, y] : offTheButton) {
    input->tap(x, y);
    frame();
    EXPECT_EQ(state(), "HandOff") << "a tap at " << x << "," << y << " passed the hand-off screen";
  }
  input->click(Button::Confirm);
  frame();
  ASSERT_EQ(state(), "Playing");
  showFrame();
  tapCanvas(100, 300);
  frame();
  ASSERT_TRUE(pump([&] { return state() == "Result"; }));
  render();
  for (const auto& [x, y] : std::vector<std::pair<int, int>>{{240, 400}, {240, 100}, {10, BANNER_Y}}) {
    input->tap(x, y);
    frame();
    EXPECT_EQ(state(), "Result") << "a tap at " << x << "," << y << " passed the banner";
  }
  EXPECT_EQ(fakelog::countLines("apply seat 1"), 1u) << "the mover's late taps were applied";
  input->click(Button::Confirm);
  frame();
  EXPECT_EQ(state(), "HandOff");
  expectHandOff(2);
}

// ---- plain tap targets (Owner Decision 2026-10-03): the hand-off's button, Result's banner and Confirm act on
// release, even while their screen is being pushed, and the first move after "I'm ready" is taken during its frame's
// push ----
//
// The input double stands in for the device's touch path: input->tap() is a contact whose release update reports its
// tap with a touch-only held time of 0 (its finger came down now), and holdTouch()/liftTouch() a finger the loop's
// update() first samples, then lifts, reporting the held time since that first sample (HalGPIO::lastTouchHeldMs, set
// on the release update as the device's InputManager sets it). The fake clock moves only when a test moves it, and the
// renderer double's push costs no time. Confirm is the double's hold(Confirm, ms) then release; click() holds it 0 ms.

// A tap on the banner made while Result's displayBuffer is running passes it, and one on "I'm ready" made while the
// hand-off screen is being pushed passes that: both screens publish their routing before the push.
TEST_F(HiddenPassTest, ATapDuringTheBannersOrTheHandOffsPushPassesAtOnce) {
  ASSERT_NO_FATAL_FAILURE(reachResultUndrawn());
  bool ran = false;
  renderer->onDisplay = [&] {
    ran = true;
    tapBanner();
  };
  render();
  renderer->onDisplay = nullptr;
  ASSERT_TRUE(ran);
  EXPECT_EQ(state(), "HandOff") << "a tap while the banner was being pushed did not pass it";
  ran = false;
  renderer->onDisplay = [&] {
    ran = true;
    tapReady();
  };
  ASSERT_TRUE(renderHandOff());
  renderer->onDisplay = nullptr;
  ASSERT_TRUE(ran);
  EXPECT_EQ(state(), "Playing") << "a tap while the hand-off screen was being pushed did not pass it";
}

// Confirm released during either push passes the screen too: the front button, the home key's action, and the X4 Pro's
// power click (no button edge, a stale press time), none of which any longer waits for its screen's push.
TEST_F(HiddenPassTest, AFrontConfirmDuringTheBannersOrTheHandOffsPushPassesAtOnce) {
  expectConfirmPassesDuringPushes([&] { input->click(Button::Confirm); });
}
TEST_F(HiddenPassTest, AHomeKeyConfirmDuringTheBannersOrTheHandOffsPushPassesAtOnce) {
  expectConfirmPassesDuringPushes([&] { input->homeKey(HomeButtonAction::Confirm); });
}
TEST_F(HiddenPassTest, APowerClickConfirmDuringTheBannersOrTheHandOffsPushPassesAtOnce) {
  expectConfirmPassesDuringPushes([&] { input->powerConfirmClick(100); });
}

// The first move after "I'm ready", tapped while that seat's frame is being pushed, reaches the game as that seat's
// move; the same tap on a later frame's push is posted as before (the frame it was made under).
TEST_F(HiddenPassTest, TheFirstMoveTapDuringTheFirstFramesPushReachesTheGame) {
  enterHidden();
  expectHandOff(1);
  tapReady();
  ASSERT_EQ(state(), "Playing");
  ASSERT_TRUE(pumpToRender());
  bool ran = false;
  renderer->onDisplay = [&] {
    if (ran) return;
    ran = true;
    tapCanvas(100, 300);
    frame();
  };
  render();
  renderer->onDisplay = nullptr;
  ASSERT_TRUE(ran);
  ASSERT_TRUE(pump([&] { return state() == "Result"; })) << "the move made during the push never reached the game";
  EXPECT_EQ(fakelog::countLines("tap for seat 1"), 1u);
  EXPECT_EQ(fakelog::countLines("apply seat 1"), 1u);
}

// A tap the loop never saw down (no latch) is dated by its touch-only held time: released during the first frame's
// push with a held time that reaches back before the hand-off passed, it is no move.
TEST_F(HiddenPassTest, ATapNeverSeenDownWhoseHeldTimeReachesBeforeTheHandOffIsDropped) {
  enterHidden();
  expectHandOff(1);
  fakertos::advance(100);
  tapReady();
  ASSERT_EQ(state(), "Playing");
  ASSERT_TRUE(pumpToRender());
  bool ran = false;
  renderer->onDisplay = [&] {
    if (ran) return;
    ran = true;
    fakertos::advance(20);
    input->quickTap(CANVAS_X + 100, CANVAS_Y + 300, 500);  // began 500 ms ago: before the transition
    frame();
  };
  render();
  renderer->onDisplay = nullptr;
  ASSERT_TRUE(ran);
  EXPECT_TRUE(logHas("dropped a touch that began")) << "the held time did not date the tap";
  EXPECT_FALSE(logHas("tap for seat 1"));
}

// handle() closes the old screen's routing: a tap made after the banner passed and before the hand-off screen is
// drawn routes nothing.
TEST_F(HiddenPassTest, ATapBetweenTheBannerPassingAndTheHandOffsRenderRoutesNothing) {
  ASSERT_NO_FATAL_FAILURE(reachResult());
  tapBanner();
  ASSERT_EQ(state(), "HandOff");
  tapReady();
  EXPECT_EQ(state(), "HandOff");
}

// The owner accepts that the banner's and the button's places overlap: once the hand-off screen has published its
// routing, a tap on the ready row's spot passes it, even before its push returns.
TEST_F(HiddenPassTest, ATapOnTheReadyRowDuringTheHandOffsPushPassesItWhereTheBannerWas) {
  ASSERT_NO_FATAL_FAILURE(reachResult());
  tapBanner();
  ASSERT_EQ(state(), "HandOff");
  bool ran = false;
  renderer->onDisplay = [&] {
    ran = true;
    tapReady();
  };
  ASSERT_TRUE(renderHandOff());
  renderer->onDisplay = nullptr;
  ASSERT_TRUE(ran);
  EXPECT_EQ(state(), "Playing");
}

// The contact that began before "I'm ready" passed (a finger down on the screen, the hand-off passed by Confirm) and
// lifts during the first frame's push is not that seat's move; a fresh tap after it is.
TEST_F(HiddenPassTest, AContactBegunBeforeTheHandOffPassedAndLiftedDuringTheFirstFramesPushIsDropped) {
  enterHidden();
  expectHandOff(1);
  input->holdTouch(CANVAS_X + 100, CANVAS_Y + 300);
  frame();  // sampled on the hand-off screen: the touch begins now
  fakertos::advance(50);
  input->click(Button::Confirm);
  frame();
  ASSERT_EQ(state(), "Playing");
  ASSERT_TRUE(pumpToRender());
  bool ran = false;
  renderer->onDisplay = [&] {
    if (ran) return;
    ran = true;
    fakertos::advance(20);
    input->liftTouch();
    frame();
  };
  render();
  renderer->onDisplay = nullptr;
  ASSERT_TRUE(ran);
  EXPECT_TRUE(logHas("dropped a touch that began")) << "the late-lifted contact was not dropped";
  fakertos::advance(10);
  tapCanvas(100, 300);  // a fresh tap, once the frame is on the panel
  frame();
  ASSERT_TRUE(pump([&] { return state() == "Result"; }));
  EXPECT_EQ(fakelog::countLines("tap for seat 1"), 1u) << "the late-lifted contact also became a move";
}

// A contact the loop saw go down in Playing, whose pass then returned early because the turn passed (TurnChanged, with
// the finger still down), must not leave its latch for the next contact: the finger lifts on the Result banner, the
// device passes on, and the first move tapped during the next seat's first frame is accepted. The game's own timer
// passes the turn, so no contact of the test's is a move.
const char* const HIDDEN_TIMER_PASS_GAME = R"(
local game = {}
function game.setup(ctx)
  ch.timer.after(1000)
  return { seats = ctx.seats, moves = 0 }
end
function game.status(state)
  if state.moves >= 4 then return { over = true, winners = {} } end
  return { turn = state.moves % state.seats + 1 }
end
function game.apply(state, seat, move)
  ch.log("apply seat " .. seat)
  state.moves = state.moves + 1
  return state
end
function game.input(state, seat, ui, ev)
  if ev.kind == "tap" then
    ch.log("tap for seat " .. seat)
    return { tap = true }
  elseif ev.kind == "timer" then
    return { tap = true }
  end
end
function game.draw(state, seat, ui)
  ch.log("draw for seat " .. seat)
  ch.gfx.clear("white")
  ch.gfx.text(40, 120, "Player " .. seat .. "'s secret: x", "medium", "black")
end
return game
)";

TEST_F(HiddenPassTest, AContactLatchedBeforeTheTurnPassedDoesNotDropTheNextSeatsFirstMove) {
  installGame("hidden-timer-pass", HIDDEN_TIMER_PASS_GAME);
  enterHidden("hidden-timer-pass");
  expectHandOff(1);
  showSeat(1);
  fakertos::advance(1000);  // the game's timer is due, polled at the end of the next pass
  input->holdTouch(CANVAS_X + 100, CANVAS_Y + 300);
  frame();  // the loop sees the finger go down (latched, under seat 1's frame), then posts the timer
  ASSERT_TRUE(pump([&] { return state() == "Result"; })) << "the turn did not pass with the finger down";
  fakertos::advance(50);
  input->liftTouch();
  frame();  // the finger lifts on Result, a pass loopPlaying never sees
  render();
  tapBanner();
  ASSERT_EQ(state(), "HandOff");
  ASSERT_TRUE(renderHandOff());
  tapReady();
  ASSERT_EQ(state(), "Playing");
  // No pass between the transition and the fresh tap: the next pass in Playing sees the tap first, where a pass without
  // a contact would have freed a stale latch. The VM publishes seat 2's frame on its own task.
  ASSERT_TRUE(waitFor([&] { return logHas("draw for seat 2"); }));
  bool ran = false;
  renderer->onDisplay = [&] {
    if (ran) return;
    ran = true;
    fakertos::advance(20);
    tapCanvas(100, 300);
    frame();
  };
  render();  // not pumpToRender: its passes would free the latch
  renderer->onDisplay = nullptr;
  ASSERT_TRUE(ran);
  EXPECT_FALSE(logHas("dropped a touch that began")) << "the stale latch dated the fresh tap before the hand-off";
  EXPECT_TRUE(pump([&] { return logHas("tap for seat 2"); })) << "the first move of the next seat never arrived";
}

// A contact that goes down after "I'm ready", is seen down by the loop (latched), and lifts during the first frame's
// push is that seat's move.
TEST_F(HiddenPassTest, AContactBegunAfterTheHandOffPassedAndLiftedDuringTheFirstFramesPushIsAccepted) {
  enterHidden();
  expectHandOff(1);
  tapReady();
  ASSERT_EQ(state(), "Playing");
  ASSERT_TRUE(pumpToRender());
  fakertos::advance(10);
  input->holdTouch(CANVAS_X + 100, CANVAS_Y + 300);
  frame();  // the finger is seen going down, before the first frame is drawn
  bool ran = false;
  renderer->onDisplay = [&] {
    if (ran) return;
    ran = true;
    fakertos::advance(20);
    input->liftTouch();
    frame();
  };
  render();
  renderer->onDisplay = nullptr;
  ASSERT_TRUE(ran);
  EXPECT_FALSE(logHas("dropped a touch that began"));
  ASSERT_TRUE(pump([&] { return state() == "Result"; })) << "the contact was not that seat's move";
  EXPECT_EQ(fakelog::countLines("tap for seat 1"), 1u);
}

// A hidden game that logs its setup, so a test can hold the VM there (fakertos::arm(At::Log)) before it has begun the
// round and named its first turn seat. Seat 1's frame shows "apple"; the second move ends the round.
const char* const HIDDEN_SLOW_SETUP_GAME = R"(
local game = {}
function game.setup(ctx)
  ch.log("setup")
  return { seats = ctx.seats, moves = 0 }
end
function game.status(state)
  if state.moves >= 2 then return { over = true, winners = {} } end
  return { turn = state.moves % state.seats + 1 }
end
function game.apply(state, seat, move)
  state.moves = state.moves + 1
  return state
end
function game.input(state, seat, ui, ev)
  if ev.kind == "tap" then return { tap = true } end
end
function game.draw(state, seat, ui)
  ch.gfx.clear("white")
  if seat == 1 then ch.gfx.text(40, 120, "Player 1's secret: apple", "medium", "black") end
  if seat == 2 then ch.gfx.text(40, 120, "Player 2's secret: river", "medium", "black") end
end
return game
)";

// A hidden round already over when it begins announces seat 0, which is no player's turn: the hand-off screen draws no
// "Player N's turn" line for it (never "Player 0's turn").
TEST_F(HiddenPassTest, AHandOffForARoundOverAtItsStartNamesNoSeat) {
  installGame("over-at-once", R"(
local game = {}
function game.setup(ctx) return { moves = 0 } end
function game.status(state) return { over = true, winners = {} } end
function game.apply(state, seat, move) return state end
function game.input(state, seat, ui, ev) return nil end
function game.draw(state, seat, ui) ch.gfx.clear("white") end
return game
)");
  enterHidden("over-at-once");
  for (int i = 0; i < 50 && renderer->shown.empty(); ++i) {
    pumpToRender();
    ui().forget();
    render();
  }
  ASSERT_FALSE(renderer->shown.empty()) << "nothing was pushed";
  EXPECT_EQ(state(), "HandOff");
  for (const GfxRenderer::Shown& push : renderer->shown)
    for (const std::string& text : push.texts) EXPECT_EQ(text.find("'s turn"), std::string::npos) << text;
  EXPECT_EQ(renderer->shown.back().texts, std::vector<std::string>{tr(STR_GAMES_READY)}) << "the button alone";
}

// The hand-off screen names the round's first turn seat, which the VM knows only once setup has run: until then a
// render pushes nothing (the screen before stays: the title screen, or the end-of-round menu after Play again) and
// neither the button's place nor Confirm passes it; once the VM names it, the loop asks for the render, which shows it.
TEST_F(HiddenPassTest, TheHandOffScreenWaitsForTheRoundsFirstTurnSeat) {
  installGame("hidden-slow", HIDDEN_SLOW_SETUP_GAME);
  fakertos::arm(fakertos::At::Log);  // held at setup's ch.log, or at a log line of the VM task's before it
  enterHidden("hidden-slow");
  ASSERT_TRUE(fakertos::waitParked());
  ASSERT_EQ(state(), "HandOff");
  ASSERT_TRUE(activityManager.updateRequested());
  render();
  EXPECT_TRUE(renderer->shown.empty()) << "the hand-off screen was pushed before the VM named a seat";
  tapReady();
  input->click(Button::Confirm);
  frame();
  EXPECT_EQ(state(), "HandOff") << "a tap passed a hand-off screen that was never pushed";
  for (int i = 0; i < 5; ++i) frame();
  EXPECT_FALSE(activityManager.updateRequested());
  fakertos::release();
  ASSERT_TRUE(pumpToRender()) << "the loop never asked for the hand-off screen";
  ui().forget();
  render();
  ASSERT_EQ(renderer->shown.size(), 1u);
  expectHandOffTexts(lastPush(), 1);
  showSeat(1);
  // The round: seat 1 and seat 2 move; the second move ends it.
  tapCanvas(100, 300);
  frame();
  ASSERT_TRUE(pump([&] { return state() == "Result"; }));
  render();
  tapBanner();
  expectHandOff(2);
  showSeat(2);
  tapCanvas(100, 300);
  frame();
  ASSERT_TRUE(pump([&] { return state() == "Over"; }));
  renderView();

  // Play again: the new round's setup is held, so its hand-off screen waits again over the end-of-round menu.
  fakertos::arm(fakertos::At::Log);
  const size_t pushes = renderer->shown.size();
  tapOption(tr(STR_GAMES_PLAY_AGAIN));
  ASSERT_EQ(state(), "HandOff");
  ASSERT_TRUE(fakertos::waitParked());
  render();
  EXPECT_EQ(renderer->shown.size(), pushes) << "Play again's hand-off screen was pushed before the VM named a seat";
  input->click(Button::Confirm);
  frame();
  EXPECT_EQ(state(), "HandOff");
  fakertos::release();
  ASSERT_TRUE(pumpToRender());
  ui().forget();
  render();
  ASSERT_EQ(renderer->shown.size(), pushes + 1);
  expectHandOffTexts(lastPush(), 1);
  EXPECT_EQ(lastPush().mode, HalDisplay::FULL_REFRESH);
  showSeat(1);
}

// A converted handoff.bmp (480 x 480, the largest allowed) in the game's folder fills the band, centred and clipped as
// the title screen draws its splash, ahead of the game's title.bmp, which is not drawn; the turn line and the button
// are under it as on any hand-off screen, and the button passes it.
TEST_F(HiddenPassTest, AHandoffPageFillsTheBandAheadOfTheTitlePageWithTheTurnLineAndTheButtonUnderIt) {
  // Black where (x / 40 + y / 40) is odd: a checkerboard of 40 px squares, so a shifted page shows.
  const auto white = [](const int x, const int y) { return (x / 40 + y / 40) % 2 == 0; };
  fakesd::addFile("/.games/pass-hidden/handoff.bmp", harness::bmpFile(480, 480, white));
  fakesd::addFile("/.games/pass-hidden/title.bmp", harness::bmpFile(480, 480, [](int, int) { return false; }));
  enterHidden();
  renderer->forget();
  ASSERT_TRUE(activityManager.updateRequested());
  ASSERT_TRUE(renderHandOff());
  EXPECT_EQ(lastPush().mode, HalDisplay::FULL_REFRESH);
  expectHandOffTexts(lastPush(), 1);
  expectBandShows(*renderer, white);
  ASSERT_EQ(ui().strokeRects.size(), 1u) << "the button";
  EXPECT_TRUE(logHas("Page /.games/pass-hidden/handoff.bmp: 480x480"));
  EXPECT_FALSE(logHas("title.bmp")) << "title.bmp was read beside handoff.bmp";
  tapReady();  // the button passes it as on the default screen
  ASSERT_EQ(state(), "Playing");
  showFrame();
  EXPECT_TRUE(holds(lastPush(), "Player 1's secret: apple"));
}

// With no handoff.bmp, the band shows the game's title.bmp, the title screen's own splash (centred: a smaller page sits
// in the band's middle).
TEST_F(HiddenPassTest, WithNoHandoffPageTheBandShowsTheTitlePage) {
  const auto white = [](const int x, const int y) { return !harness::speckle(x, y, 3); };
  fakesd::addFile("/.games/pass-hidden/title.bmp", harness::bmpFile(100, 60, white));
  enterHidden();
  renderer->forget();
  ASSERT_TRUE(renderHandOff());
  expectHandOffTexts(lastPush(), 1);
  const int left = (480 - 100) / 2;
  const int top = GameSplashLayout::bandTop(*renderer) + (480 - 60) / 2;
  int wrong = 0;
  for (int y = 0; y < 60; ++y)
    for (int x = 0; x < 100; ++x)
      if ((renderer->pixel(left + x, top + y) == GfxRenderer::PixelBlack) == white(x, y)) ++wrong;
  EXPECT_EQ(wrong, 0);
  EXPECT_EQ(blackIn(*renderer, 0, 0, 480, 800), blackIn(*renderer, left, top, 100, 60)) << "the page alone: no icon";
  EXPECT_FALSE(logHas("handoff.bmp")) << "a missing handoff.bmp is no fault";
}

// A handoff.bmp the screen cannot use (one pixel over 480 x 480 either way) is skipped, logged, for the title.bmp;
// with no usable title.bmp either, the game's icon. The turn line and the button are drawn whichever it is.
TEST_F(HiddenPassTest, AnUnusableHandoffPageFallsBackToTheTitlePageThenToTheIconLogged) {
  fakesd::addFile("/.games/pass-hidden/handoff.bmp", harness::bmpFile(481, 480, [](int, int) { return false; }));
  fakesd::addFile("/.games/pass-hidden/title.bmp", harness::bmpFile(480, 480, [](int, int) { return false; }));
  enterHidden();
  EXPECT_TRUE(logHas("/.games/pass-hidden/handoff.bmp is 481x480, over 480x480; skipped"));
  renderer->forget();
  ASSERT_TRUE(renderHandOff());
  expectHandOffTexts(lastPush(), 1);
  expectBandShows(*renderer, [](int, int) { return false; });
  showSeat(1);
}

TEST_F(HiddenPassTest, AnUnusableHandoffPageAndNoTitlePageFallBackToTheIconLogged) {
  fakesd::addFile("/.games/pass-hidden/handoff.bmp", harness::bmpFile(480, 481, [](int, int) { return false; }));
  enterHidden();
  EXPECT_TRUE(logHas("/.games/pass-hidden/handoff.bmp is 480x481, over 480x480; skipped"));
  EXPECT_FALSE(logHas("Page /.games/")) << "a page was loaded";
  renderer->forget();
  expectHandOff(1);
  expectSameFills(heldAt(lastPush().callsBefore).drawn, iconFills(GameRowIcon::FALLBACK_NAME));
  showSeat(1);
}

// The pass-art fixture (test/game_script/fixtures/README.md) prints the mode and the settings its match was started
// with, which the device run reads off its frames: here solo with Level "Easy" and Board "Large", and, as a hidden pass
// match, the same on each seat's frame after the hand-off screen (the fixture's handoff.png and title.png are converted
// only by the installer, so this folder copy shows the icon in the band, the spade the manifest names).
TEST_F(MatchTest, ThePassArtFixturePrintsTheModeAndTheSettingsItWasStartedWith) {
  installFixture("pass-art");
  GameCore::Manifest manifest = match::manifestOf("pass-art", "Pass art");
  manifest.seatsMax = 2;
  manifest.modes = GameCore::Manifest::MODE_SOLO | GameCore::Manifest::MODE_PASS;
  manifest.hidden = true;
  std::snprintf(manifest.icon, sizeof(manifest.icon), "spade");
  manifest.iconWeight = GameCore::Manifest::ICON_FILL;
  GameCore::SettingValues settings;
  for (const auto& [id, value] :
       std::vector<std::pair<const char*, const char*>>{{"level", "Easy"}, {"board", "Large"}}) {
    GameCore::SettingValues::Entry& entry = settings.entries[settings.count++];
    std::snprintf(entry.id, sizeof(entry.id), "%s", id);
    std::snprintf(entry.value, sizeof(entry.value), "%s", value);
  }
  gameId = "pass-art";
  activity = std::make_unique<GameMatchActivity>(*renderer, *input, manifest, GameCore::Roster::solo(),
                                                 GameMatchActivity::Start::New, settings);
  activity->onEnter();
  ASSERT_EQ(state(), "Playing");
  renderer->forget();
  showFrame();
  const std::vector<std::string> solo = match::drawnTexts(*renderer);
  for (const char* text : {"Player 1's secret: lantern", "Mode: solo", "Level: Easy", "Board: Large", "Moves: 0"}) {
    EXPECT_NE(std::find(solo.begin(), solo.end(), text), solo.end()) << text;
  }
  EXPECT_TRUE(logHas("setup solo level Easy board Large"));

  activityManager.exitHolding(*activity);
  activityManager.destroyHolding(activity);
  ASSERT_TRUE(fakertos::waitNoTasks());
  fakelog::clearLines();
  activity = std::make_unique<GameMatchActivity>(*renderer, *input, manifest, GameCore::Roster::pass(2),
                                                 GameMatchActivity::Start::New, settings);
  activity->onEnter();
  ASSERT_EQ(state(), "HandOff");
  ASSERT_TRUE(waitFor([&] { return logHas("setup pass level Easy board Large"); }));
  // The hand-off screen draws once the VM has named the first turn seat, just after setup: a render before that pushes
  // nothing, and the loop asks again.
  renderer->forget();
  const size_t pushes = renderer->shown.size();
  render();
  if (renderer->shown.size() == pushes) {
    ASSERT_TRUE(pumpToRender());
    render();
  }
  ASSERT_EQ(renderer->shown.size(), pushes + 1);
  EXPECT_EQ(renderer->shown.back().texts, (std::vector<std::string>{"Player 1's turn", tr(STR_GAMES_READY)}));
  std::vector<GfxRenderer::Call> fills;
  for (const GfxRenderer::Call& call : renderer->calls) {
    if (call.kind == GfxRenderer::Kind::FillRect) fills.push_back(call);
  }
  expectSameFills(fills, iconFills("spade", true));
  const freeink::ui::Point ready = match::readyButtonMiddle(*renderer);
  input->tap(ready.x, ready.y);
  frame();
  ASSERT_EQ(state(), "Playing");
  renderer->forget();
  showFrame();
  const std::vector<std::string> pass = match::drawnTexts(*renderer);
  for (const char* text : {"Player 1's secret: lantern", "Mode: pass", "Level: Easy", "Board: Large"}) {
    EXPECT_NE(std::find(pass.begin(), pass.end(), text), pass.end()) << text;
  }
}

// The Play-again gap's pause menu: "Starting the next round" is centred under "Paused", as the caption and the headline
// are (DESIGN.md pause-menu gap-line).
TEST_F(PlayAgainGapTest, TheGapLineIsCentredUnderTheHeadline) {
  playAgainByTouch();
  enterTheGap();
  input->click(Button::Back);
  frame();
  ASSERT_EQ(state(), "Paused");
  renderView();
  const screen::DrawnText* headline = nullptr;
  const screen::DrawnText* gap = nullptr;
  for (const screen::DrawnText& drawn : ui().drawn) {
    if (drawn.text == tr(STR_GAMES_PAUSED)) headline = &drawn;
    if (drawn.text == tr(STR_GAMES_NEXT_ROUND_STARTING)) gap = &drawn;
  }
  ASSERT_NE(headline, nullptr) << ui().joined();
  ASSERT_NE(gap, nullptr) << ui().joined();
  EXPECT_GT(gap->rect.y, headline->rect.y);
  EXPECT_LE(std::abs((gap->rect.x + gap->rect.width / 2) - (headline->rect.x + headline->rect.width / 2)), 1)
      << "the gap line is not centred like the headline";
}

}  // namespace
