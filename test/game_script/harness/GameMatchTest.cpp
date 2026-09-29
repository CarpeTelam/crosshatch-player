#include <I18n.h>
#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "ArenaSize.h"
#include "GameAssets.h"
#include "GameSaveStore.h"
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

  void enter(const std::string& id, const std::string& name = "") {
    gameId = id;
    activity = std::make_unique<GameMatchActivity>(*renderer, *input, match::manifestOf(id, name));
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

  activity = std::make_unique<GameMatchActivity>(*renderer, *input, match::manifestOf("counter"));
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

}  // namespace
