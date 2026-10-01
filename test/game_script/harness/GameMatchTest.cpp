#include <I18n.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "ArenaSize.h"
#include "GameAssets.h"
#include "GameIconDraw.h"
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
  EXPECT_FALSE(logHas("blank hand-off screen pushed"));
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

// The same gate at the start of a match: the Games list is on screen until the first round's first frame is drawn, so a
// tap before then (the one that opened the game, lifting late, or an impatient second one) is dropped.
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

// An open pass match never hands off (entry 4's Never): no Result, no HandOff, and no blank push.
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
  EXPECT_FALSE(logHas("blank hand-off screen pushed"));
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

// ---- a hidden pass match (epic-pass-and-play entry 4): pass-hidden, the hand-off between seats ----

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

  // The forced exit pushed the blank once, and logged it: a half refresh of a cleared screen holding the eye-closed
  // icon drawn as FrameReplay::drawBlank draws it (the same fills as TheBlankIsOnlyTheEyeClosedIconCentredOnTheCanvas
  // expects) and nothing else, so a seat's frame drawn without text cannot pass as the blank. Callers wrap it in
  // ASSERT_NO_FATAL_FAILURE, since they read pushes[0] after it.
  void expectOneBlankPush(const ExitRecord& record) {
    EXPECT_EQ(fakelock::selfDeadlocks.load(), 0) << "onExit took the RenderLock the manager holds (12cc816)";
    ASSERT_EQ(record.pushes.size(), 1u);
    EXPECT_EQ(record.pushes[0].mode, HalDisplay::HALF_REFRESH);
    EXPECT_TRUE(record.pushes[0].texts.empty()) << record.pushes[0].texts.front();
    EXPECT_EQ(fakelog::countLines(gameId + ": forced exit: blank hand-off screen pushed (half refresh)"), 1u);
    // What the framebuffer held at the push: every drawing call since the last clearScreen before it.
    std::vector<GfxRenderer::Call> drawn;
    for (size_t i = 0; i < record.pushes[0].callsBefore && i < renderer->calls.size(); ++i) {
      const GfxRenderer::Call& call = renderer->calls[i];
      if (call.kind == GfxRenderer::Kind::ClearScreen) drawn.clear();
      if (call.kind == GfxRenderer::Kind::FillRect || call.kind == GfxRenderer::Kind::DrawText ||
          call.kind == GfxRenderer::Kind::FillRectDither || call.kind == GfxRenderer::Kind::DrawLine) {
        drawn.push_back(call);
      }
    }
    GfxRenderer expected(480, 800);
    expected.clearScreen();
    // The canvas is 474 x 788 at (3, 6).
    ASSERT_TRUE(drawGameIcon(expected, "eye-closed", 3 + (474 - 128) / 2, 6 + (788 - 128) / 2, 128, true));
    std::vector<GfxRenderer::Call> icon;
    for (const GfxRenderer::Call& call : expected.calls) {
      if (call.kind == GfxRenderer::Kind::FillRect) icon.push_back(call);
    }
    ASSERT_EQ(drawn.size(), icon.size()) << "the forced exit's push held more or less than the blank";
    for (size_t i = 0; i < icon.size(); ++i) {
      EXPECT_EQ(drawn[i].kind, GfxRenderer::Kind::FillRect) << i;
      EXPECT_EQ(drawn[i].x, icon[i].x) << i;
      EXPECT_EQ(drawn[i].y, icon[i].y) << i;
      EXPECT_EQ(drawn[i].w, icon[i].w) << i;
      EXPECT_EQ(drawn[i].black, icon[i].black) << i;
    }
  }

  // HIDDEN_STORE_GAME in Result after seat 1's two moves, both stored, and the store not yet flushed.
  void reachResultWithADirtyStore() {
    installGame("hidden-store", HIDDEN_STORE_GAME);
    enterHidden("hidden-store");
    expectBlank();
    tapScreen();
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

  // A tap at the middle of the screen: on the blank or the Result banner's screen, it passes the device on.
  void tapScreen() {
    input->tap(240, 400);
    frame();
  }

  // The push the last render made.
  const GfxRenderer::Shown& lastPush() const { return renderer->shown.back(); }
  // Whether some text of `push` holds `part`.
  static bool holds(const GfxRenderer::Shown& push, const std::string& part) {
    return std::any_of(push.texts.begin(), push.texts.end(),
                       [&](const std::string& text) { return text.find(part) != std::string::npos; });
  }

  // The blank is asked for and drawn: a full refresh, and no text on the panel.
  void expectBlank() {
    ASSERT_TRUE(activityManager.updateRequested());
    const size_t pushes = renderer->shown.size();
    render();
    ASSERT_EQ(renderer->shown.size(), pushes + 1);
    EXPECT_EQ(lastPush().mode, HalDisplay::FULL_REFRESH);
    EXPECT_TRUE(lastPush().texts.empty()) << lastPush().texts.front();
  }

  // From the blank on screen: the tap, then the turn seat's frame, drawn in full once the VM has published it.
  void showSeat(const int seat) {
    tapScreen();
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

TEST_F(HiddenPassTest, EachSeatIsShownOnlyAfterABlankAndNoSecretCrossesIt) {
  enterHidden();
  EXPECT_EQ(state(), "HandOff");
  EXPECT_TRUE(logHas("pass-hidden: Starting -> HandOff on Started"));
  expectBlank();
  showSeat(1);
  EXPECT_TRUE(holds(lastPush(), "Moves: 0"));
  moveAndPass(1, 2);
  tapScreen();
  EXPECT_EQ(state(), "HandOff");
  expectBlank();
  const size_t seat2At = renderer->shown.size();
  showSeat(2);
  EXPECT_TRUE(holds(lastPush(), "river"));
  EXPECT_TRUE(holds(lastPush(), "Moves: 1"));
  moveAndPass(2, 1);
  tapScreen();
  expectBlank();
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
  expectBlank();
  showSeat(1);
  moveAndPass(1, 2);
  tapScreen();
  expectBlank();
  showSeat(2);
  moveAndPass(2, 1);
  tapScreen();
  expectBlank();
  showSeat(1);
  moveAndPass(1, 2);
  tapScreen();
  expectBlank();
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
  expectBlank();
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
  expectBlank();
  showSeat(1);
  EXPECT_TRUE(holds(lastPush(), "Moves: 0"));
  EXPECT_EQ(fakelog::countLines("Round started"), 2u);
}

// The blank's tap zone is published only after the blank is pushed: a tap or Confirm before it, or one made while
// displayBuffer is still running, is dropped, so neither can skip the blank.
TEST_F(HiddenPassTest, ATapBeforeTheBlankIsOnThePanelIsDropped) {
  enterHidden();
  tapScreen();
  input->click(Button::Confirm);
  frame();
  EXPECT_EQ(state(), "HandOff") << "a tap before the blank was drawn passed it";
  bool ran = false;
  renderer->onDisplay = [&] {
    ran = true;
    tapScreen();
  };
  render();
  renderer->onDisplay = nullptr;
  ASSERT_TRUE(ran);
  EXPECT_EQ(state(), "HandOff") << "a tap while the blank was being pushed passed it";
  input->click(Button::Confirm);  // once it is up, Confirm passes it as a tap does
  frame();
  EXPECT_EQ(state(), "Playing");
}

TEST_F(HiddenPassTest, PauseFromResultOrTheBlankReturnsThereAndTheBlanksPauseMenuSitsOnNoFrame) {
  enterHidden();
  expectBlank();
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
  expectBlank();
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

  tapScreen();
  ASSERT_EQ(state(), "HandOff");
  expectBlank();
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
  expectBlank();
  showSeat(2);
}

// A tap that seat 1 makes right after its turn-passing move, queued behind it, is seat 1's: its input runs, and the
// move it returns is discarded (seat 1 is no longer the turn seat), so nothing of seat 2 is drawn or played.
TEST_F(HiddenPassTest, ATapQueuedBehindTheTurnPassingMoveReachesTheMoverAndIsNeverApplied) {
  enterHidden();
  expectBlank();
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
  expectBlank();
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
  tapScreen();
  ASSERT_EQ(state(), "HandOff");
  expectBlank();
  tapScreen();  // asks for seat 2 while C is still queued
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
  expectBlank();
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
  tapScreen();
  ASSERT_EQ(state(), "HandOff");
  expectBlank();
  tapScreen();  // asks for seat 2, while B is still seat 1's
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
  expectBlank();
  showSeat(1);
  moveAndPass(1, 2);  // seat 1's tap re-armed the 5 s timer at 1000 ms
  tapScreen();
  ASSERT_EQ(state(), "HandOff");
  expectBlank();
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

// Result's tap passes only once its banner is on the panel: a tap before its render, or one made while its
// displayBuffer is still running, is dropped, so a double tap on the move cannot skip the banner.
TEST_F(HiddenPassTest, ATapBeforeResultsBannerIsOnThePanelIsDropped) {
  enterHidden();
  expectBlank();
  showSeat(1);
  tapCanvas(100, 300);
  frame();
  ASSERT_TRUE(pump([&] { return state() == "Result"; }));
  tapScreen();
  EXPECT_EQ(state(), "Result") << "a tap before the banner was drawn passed it";
  bool ran = false;
  renderer->onDisplay = [&] {
    ran = true;
    tapScreen();
    input->click(Button::Confirm);
    frame();
  };
  render();
  renderer->onDisplay = nullptr;
  ASSERT_TRUE(ran);
  EXPECT_EQ(state(), "Result") << "a tap while the banner was being pushed passed it";
  tapScreen();
  EXPECT_EQ(state(), "HandOff");
}

// The loop watches the VM in Result as in a menu: a call stuck there ends in the error view.
TEST_F(HiddenPassTest, ACallStuckInResultIsStoppedIntoTheErrorView) {
  enterHidden();
  expectBlank();
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
  EXPECT_FALSE(logHas("blank hand-off screen pushed"));
}

// ---- the forced exit's blank hand-off (epic-pass-and-play entry 6; AD-12, AD-20) ----

// Sleep on a seat's frame: the exit pushes the blank once, a half refresh with no text, after the VM's stop and
// before any SD op, so the store flush comes after it. The double's push moves no clock; the device's half refresh
// is measured by entry 11.
TEST_F(HiddenPassTest, TheForcedExitOnASeatsFramePushesTheBlankAfterTheJoinAndBeforeTheStore) {
  installGame("hidden-store", HIDDEN_STORE_GAME);
  enterHidden("hidden-store");
  expectBlank();
  tapScreen();
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
  expectBlank();
  showSeat(1);
  moveAndPass(1, 2);
  ASSERT_EQ(state(), "Result");
  const ExitRecord record = sleepRecordingPushes();
  ASSERT_NO_FATAL_FAILURE(expectOneBlankPush(record));
  EXPECT_FALSE(record.pushes[0].abandonLogged);
}

// Sleep on the blank (HandOff): the blank is pushed again.
TEST_F(HiddenPassTest, TheForcedExitOnTheBlankPushesTheBlankAgain) {
  enterHidden();
  expectBlank();
  showSeat(1);
  moveAndPass(1, 2);
  tapScreen();
  ASSERT_EQ(state(), "HandOff");
  expectBlank();
  const ExitRecord record = sleepRecordingPushes();
  ASSERT_NO_FATAL_FAILURE(expectOneBlankPush(record));
}

// A VM held inside ch.log (a locked binding) neither joins nor can be deleted: the blank goes up once the stop's wait
// has run out, before the abandon begins its own wait, and the whole exit stays within the bound.
TEST_F(HiddenPassTest, TheForcedExitWithAStuckVmPushesTheBlankBetweenTheJoinAndTheAbandon) {
  enterHidden();
  expectBlank();
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
  expectBlank();
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
  expectBlank();
  tapScreen();
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
  expectBlank();
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
  EXPECT_EQ(fakelog::countLines("pass-hidden: leave: blank hand-off screen pushed (half refresh)"), 1u);
  const ExitRecord record = sleepRecordingPushes();
  EXPECT_TRUE(record.pushes.empty()) << "the exit after Leave pushed again";
  EXPECT_FALSE(logHas("forced exit: blank hand-off screen pushed"));
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
    expectBlank();
    if (fromOver) {
      for (int turn = 0; turn < 3; ++turn) {
        const int mover = turn % 2 + 1;
        showSeat(mover);
        moveAndPass(mover, 3 - mover);
        tapScreen();
        expectBlank();
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
    EXPECT_FALSE(logHas("blank hand-off screen pushed"));
    EXPECT_EQ(fakelock::selfDeadlocks.load(), 0);
  }
}

// A render in HandOff with the blank already on the panel (the light panel closed, a status repaint) repaints it with
// a fast refresh and no text (cross-story review row 6); the blank entered from any other screen is a full refresh.
TEST_F(HiddenPassTest, ARepaintOfTheBlankIsAFastRefreshAndEnteringItIsFull) {
  enterHidden();
  expectBlank();  // from the screen before the match: full
  render();
  EXPECT_EQ(lastPush().mode, HalDisplay::FAST_REFRESH);
  EXPECT_TRUE(lastPush().texts.empty());
  render();
  EXPECT_EQ(lastPush().mode, HalDisplay::FAST_REFRESH);
  showSeat(1);
  moveAndPass(1, 2);
  tapScreen();
  ASSERT_EQ(state(), "HandOff");
  expectBlank();  // from Result's banner over seat 1's frame: full
  render();
  EXPECT_EQ(lastPush().mode, HalDisplay::FAST_REFRESH);
  EXPECT_TRUE(lastPush().texts.empty());
  input->click(Button::Back);
  frame();
  ASSERT_EQ(state(), "Paused");
  renderView();
  input->click(Button::Back);
  frame();
  ASSERT_EQ(state(), "HandOff");
  expectBlank();  // from the pause menu: full
}

// A call stuck on a seat's frame frees the VM on the way to Error; a forced exit before the error view is drawn finds
// no VM but a seat's frame on the panel, and pushes the blank before any SD step (cross-story review row 1). Nothing is
// pushed after it.
TEST_F(HiddenPassTest, AForcedExitWithNoVmOverASeatsFramePushesTheBlankAndNothingAfter) {
  enterHidden();
  expectBlank();
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
  expectBlank();
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
  tapScreen();
  ASSERT_EQ(state(), "HandOff");
  const size_t blankAt = renderer->shown.size();
  expectBlank();
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
  expectBlank();
  showSeat(2);
  ASSERT_GT(renderer->shown.size(), blankAt);
  for (size_t i = blankAt; i < renderer->shown.size(); ++i) {
    EXPECT_FALSE(holds(renderer->shown[i], "apple")) << "push " << i << " after the blank holds seat 1's frame";
  }
  EXPECT_FALSE(logHas("tap for seat 2")) << "B reached seat 2";
}

// The blank is the eye-closed library icon at 128 px, black, centred on the canvas, and nothing else: drawn as
// drawGameIcon draws it there, on a cleared screen.
TEST_F(HiddenPassTest, TheBlankIsOnlyTheEyeClosedIconCentredOnTheCanvas) {
  enterHidden();
  renderer->forget();
  expectBlank();
  GfxRenderer expected(480, 800);
  expected.clearScreen();
  // The canvas is 474 x 788 at (3, 6).
  ASSERT_TRUE(drawGameIcon(expected, "eye-closed", 3 + (474 - 128) / 2, 6 + (788 - 128) / 2, 128, true));
  std::vector<GfxRenderer::Call> drawn;
  for (const GfxRenderer::Call& call : renderer->calls) {
    if (call.kind == GfxRenderer::Kind::ClearScreen) drawn.clear();
    if (call.kind == GfxRenderer::Kind::FillRect || call.kind == GfxRenderer::Kind::DrawText ||
        call.kind == GfxRenderer::Kind::FillRectDither || call.kind == GfxRenderer::Kind::DrawLine) {
      drawn.push_back(call);
    }
  }
  std::vector<GfxRenderer::Call> icon;
  for (const GfxRenderer::Call& call : expected.calls) {
    if (call.kind == GfxRenderer::Kind::FillRect) icon.push_back(call);
  }
  ASSERT_FALSE(icon.empty());
  ASSERT_EQ(drawn.size(), icon.size()) << "the blank draws more or less than the icon";
  for (size_t i = 0; i < icon.size(); ++i) {
    EXPECT_EQ(drawn[i].kind, GfxRenderer::Kind::FillRect) << i;
    EXPECT_EQ(drawn[i].x, icon[i].x) << i;
    EXPECT_EQ(drawn[i].y, icon[i].y) << i;
    EXPECT_EQ(drawn[i].w, icon[i].w) << i;
    EXPECT_EQ(drawn[i].black, icon[i].black) << i;
  }
}

}  // namespace
