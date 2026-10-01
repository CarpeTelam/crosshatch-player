#include <I18n.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "HostCapsScript.h"
#include "InstallerScript.h"
#include "MatchSupport.h"
#include "RemoveScript.h"
#include "activities/games/GameMatchActivity.h"
#include "activities/games/GameModeActivity.h"
#include "activities/games/GamesLauncherActivity.h"
#include "util/ButtonNavigator.h"

// Entry 10 of epic-install-and-launcher on the launcher: removing a game (a long-press or a hold of Confirm, the
// confirmation, the refreshed list, a failure), the list paging by whole pages, and the launcher coming back on the
// page of the game last opened. The real GamesLauncherActivity, GameRegistry, UiListActivity, and FreeInkUI over the
// screen doubles, the installer scripted (InstallerScript.h for the inbox, RemoveScript.h for remove). The real remove,
// over the fake card, is GameRemoveTest's. The fixture is GamesLauncherTest's, cut to what these tests use.

namespace {

using Button = MappedInputManager::Button;
using GamePackageInstaller::Error;

const std::string PKG = "v1\n0530a15766e91bf1\n";

std::string manifestJson(const std::string& id, const std::string& name, const std::string& modes, const int api,
                         const int seatsMin, const int seatsMax) {
  return "{\"id\":\"" + id + "\",\"name\":\"" + name + "\",\"version\":\"1.0.0\",\"api\":" + std::to_string(api) +
         ",\"seats\":{\"min\":" + std::to_string(seatsMin) + ",\"max\":" + std::to_string(seatsMax) + "},\"modes\":[" +
         modes + "]}";
}

std::string idOf(const int number) {
  char id[16];
  std::snprintf(id, sizeof(id), "game-%02d", number);
  return id;
}
std::string nameOf(const int number) {
  char name[16];
  std::snprintf(name, sizeof(name), "Game %02d", number);
  return name;
}

// A message that wrapped over lines, read as one line of words.
std::string flat(std::string text) {
  for (char& c : text)
    if (c == '\n') c = ' ';
  return text;
}

class RemoveListTest : public match::ScreenTest {
 protected:
  void SetUp() override {
    ScreenTest::SetUp();
    installerscript::reset();
    removescript::reset();
    hostcaps::reset();
    GamesLauncherActivity::forgetOpenedGame();  // a fresh boot
    ButtonNavigator::setMappedInputManager(*input);
    // The real remove takes the game's folder off the card; a test that wants it to fail says so.
    removescript::script().onRemove = [this] { takeGameOffTheCard(removescript::script().ids.back()); };
  }

  void TearDown() override {
    fakertos::release();
    if (list) {
      activityManager.exitHolding(*list);
      activityManager.destroyHolding(list);
    }
    dropMatch();
    GamesLauncherActivity::forgetOpenedGame();
    ScreenTest::TearDown();
  }

  void dropMatch() {
    for (auto& replacement : activityManager.replacements) {
      if (!replacement) continue;
      if (replacement.get() == entered) activityManager.exitHolding(*replacement);
      activityManager.destroyHolding(replacement);
    }
    activityManager.replacements.clear();
    entered = nullptr;
  }

  // ---- the card ----

  static void addGame(const std::string& id, const std::string& name, const std::string& modes = "\"solo\"",
                      const int api = 1, const int seatsMin = 1, const int seatsMax = 1) {
    const std::string dir = "/.games/" + id;
    fakesd::addFile(dir + "/manifest.json", manifestJson(id, name, modes, api, seatsMin, seatsMax));
    fakesd::addFile(dir + "/main.lua", std::string("return {}\n"));
    fakesd::addFile(dir + "/.pkg", PKG);
  }
  static void addGames(const int count) {
    for (int i = 1; i <= count; ++i) addGame(idOf(i), nameOf(i));
  }
  static void placeData(const std::string& id) {
    fakesd::addFile("/.games-data/" + id + "/store.bin", std::string("store of ") + id);
    fakesd::addFile("/.games-data/" + id + "/resume.bin", std::string("resume of ") + id);
  }
  static void takeGameOffTheCard(const std::string& id) {
    // What the real remove does: the marker, then the folder.
    fakesd::removeEntry("/.games/" + id + "/.pkg");
    for (const char* file : {"manifest.json", "main.lua"}) fakesd::removeEntry("/.games/" + id + "/" + file);
    fakesd::removeEntry("/.games/" + id);
  }

  // ---- the screen ----

  Activity& activity() { return *list; }
  void open() {
    list = std::make_unique<GamesLauncherActivity>(*renderer, *input);
    activity().onEnter();
    render();
  }
  // A second launcher, as goToGames() builds one after a match ends or is left.
  void reopen() {
    match::letStartedMatchesGo([this] { dropMatch(); }, match::Saves::Discard);
    activityManager.exitHolding(*list);
    activityManager.destroyHolding(list);
    activityManager.reset();
    UITheme::getInstance().getTheme().reset();
    open();
  }
  void render() {
    if (screen::RecordingTarget::newest()) screen::RecordingTarget::newest()->forget();
    UITheme::getInstance().getTheme().calls.clear();
    activity().render(RenderLock(*list));
    activityManager.markRendered();
  }
  // One pass of the main loop, then the frame's input is over, and the screen is drawn again as the manager would.
  void frame() {
    // ActivityManager::loop's first act: the release that ends a fired long press is not for the screen.
    if (!input->consumeSuppressedRelease()) activity().loop();
    input->clear();
    if (activityManager.updateRequested()) render();
  }
  screen::RecordingTarget& ui() { return *screen::RecordingTarget::newest(); }

  const screen::DrawnText* find(const std::string& text) {
    for (const screen::DrawnText& drawn : ui().drawn)
      if (drawn.text == text) return &drawn;
    return nullptr;
  }
  void tapText(const std::string& text) {
    const screen::DrawnText* drawn = find(text);
    ASSERT_NE(drawn, nullptr) << "nothing drawn says \"" << text << "\": " << ui().joined();
    input->tap(drawn->rect.x + drawn->rect.width / 2, drawn->rect.y + drawn->rect.height / 2);
    frame();
  }
  void longPressText(const std::string& text) {
    const screen::DrawnText* drawn = find(text);
    ASSERT_NE(drawn, nullptr) << "nothing drawn says \"" << text << "\": " << ui().joined();
    input->longPress(drawn->rect.x + drawn->rect.width / 2, drawn->rect.y + drawn->rect.height / 2);
    frame();
  }
  void key(const Button button, const int times = 1) {
    for (int i = 0; i < times; ++i) {
      input->click(button);
      frame();
    }
  }

  // The names of the games the list drew, top to bottom.
  std::vector<std::string> shown(const int total) {
    std::vector<std::string> found;
    for (const screen::DrawnText& drawn : ui().drawn)
      for (int i = 1; i <= total; ++i)
        if (drawn.text == nameOf(i)) found.push_back(drawn.text);
    return found;
  }
  bool dialogUp() { return ui().drewLine(tr(STR_GAMES_REMOVE_TITLE)); }
  // Whether the note over the list says `text` (the note wraps, so the lines are read as one).
  bool noteSays(const std::string& text) { return flat(ui().joined()).find(text) != std::string::npos; }
  void swipeUp() {
    input->swipeDirection(MappedInputManager::SwipeDir::Up);
    frame();
  }
  void swipeDown() {
    input->swipeDirection(MappedInputManager::SwipeDir::Down);
    frame();
  }

  // Entry 7 of epic-pass-and-play: a game's own row pushes its title screen (GameModeActivity) instead of starting the
  // match. A case that needs the row's match opens the pushed screen as the manager would, taps its first New row (and
  // New game when the screen asks first, over a save), and lets the screen go as the manager lets a replaced one go, so
  // the match is the replacement the case reads, as when the row started it.
  void startNewOnTitle() {
    ASSERT_EQ(activityManager.pushedActivities.size(), 1u) << "the row pushes the game's title screen";
    Activity& title = *activityManager.pushedActivities.back();
    ASSERT_NE(dynamic_cast<GameModeActivity*>(&title), nullptr);
    title.onEnter();
    screen::RecordingTarget& target = *screen::RecordingTarget::newest();  // the title screen's: built last
    const auto tapFirst = [&](const std::vector<std::string>& labels) {
      target.forget();
      title.render(RenderLock(title));
      for (const screen::DrawnText& drawn : target.drawn) {
        if (std::find(labels.begin(), labels.end(), drawn.text) == labels.end()) continue;
        input->tap(drawn.rect.x + drawn.rect.width / 2, drawn.rect.y + drawn.rect.height / 2);
        title.loop();
        input->clear();
        return true;
      }
      return false;
    };
    ASSERT_TRUE(tapFirst({tr(STR_GAMES_MODE_SOLO), tr(STR_GAMES_MODE_PASS), tr(STR_GAMES_MODE_NEARBY)}))
        << target.joined();
    if (activityManager.replacements.empty()) ASSERT_TRUE(tapFirst({tr(STR_GAMES_NEW_GAME)})) << target.joined();
    activityManager.exitHolding(title);
    activityManager.destroyHolding(activityManager.pushedActivities.back());
    activityManager.pushedActivities.pop_back();
  }

  GameMatchActivity* enterReplacement() {
    if (activityManager.replacements.empty()) return nullptr;
    auto* match = dynamic_cast<GameMatchActivity*>(activityManager.replacements.back().get());
    if (match) {
      match->onEnter();
      entered = match;
    }
    return match;
  }

  std::unique_ptr<GamesLauncherActivity> list;
  Activity* entered = nullptr;
};

}  // namespace

// ---- removing a game ----

TEST_F(RemoveListTest, ALongPressAsksBeforeRemovingAndNamesTheGame) {
  addGames(3);
  open();
  longPressText("Game 02");
  EXPECT_TRUE(dialogUp());
  EXPECT_TRUE(ui().drewLine("Game 02")) << "the confirmation names the game";
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_REMOVE_KEPT)));
  EXPECT_TRUE(ui().drewLine(tr(STR_CANCEL)));
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_REMOVE)));
  EXPECT_FALSE(ui().drewLine("Game 01")) << "the list is not drawn under it";
  EXPECT_TRUE(removescript::script().ids.empty()) << "nothing is removed until the person says so";
  EXPECT_TRUE(activityManager.replacements.empty());
  EXPECT_EQ(activityManager.asks.pushed, 0);
}

TEST_F(RemoveListTest, RemoveDeletesTheGameKeepsItsDataAndRefreshesTheList) {
  addGames(3);
  placeData("game-02");
  open();
  longPressText("Game 02");
  tapText(tr(STR_GAMES_REMOVE));

  const std::vector<std::string> ids{"game-02"};
  EXPECT_EQ(removescript::script().ids, ids);
  EXPECT_TRUE(removescript::script().popupWhenRemoving) << "\"Removing...\" is up while the card works";
  EXPECT_TRUE(removescript::script().lockWhenRemoving) << "the render task is held off the listing while it changes";
  EXPECT_FALSE(dialogUp());
  const std::vector<std::string> expected{"Game 01", "Game 03"};
  EXPECT_EQ(shown(3), expected) << "the list is the registry's, read again";
  EXPECT_FALSE(fakesd::has("/.games/game-02"));
  EXPECT_TRUE(fakesd::has("/.games-data/game-02/store.bin"));
  EXPECT_TRUE(fakesd::has("/.games-data/game-02/resume.bin"));
  EXPECT_EQ(ui().bitmaps, 2) << "the icon cache was rebuilt for the two rows";
  EXPECT_FALSE(ui().drewLine(tr(STR_GAMES_REMOVE_FAILED)));

  // The next game took the removed row's place in the selection: Confirm opens Game 03.
  key(Button::Confirm);
  startNewOnTitle();
  ASSERT_EQ(activityManager.replacements.size(), 1u);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started game-03"));
}

TEST_F(RemoveListTest, RemovingTheLastRowSelectsThePreviousGame) {
  addGames(3);
  open();
  longPressText("Game 03");
  tapText(tr(STR_GAMES_REMOVE));
  key(Button::Confirm);
  startNewOnTitle();
  ASSERT_EQ(activityManager.replacements.size(), 1u);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started game-02"));
}

TEST_F(RemoveListTest, RemovingTheOnlyGameShowsTheEmptyList) {
  addGames(1);
  open();
  longPressText("Game 01");
  tapText(tr(STR_GAMES_REMOVE));
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_EMPTY)));
  key(Button::Confirm);
  input->tap(240, 300);
  frame();
  EXPECT_TRUE(activityManager.replacements.empty());
  EXPECT_EQ(activityManager.asks.pushed, 0);
}

TEST_F(RemoveListTest, CancelBackAndAConfirmOnTheDefaultButtonKeepTheGame) {
  addGames(3);
  open();
  longPressText("Game 02");
  tapText(tr(STR_CANCEL));
  EXPECT_FALSE(dialogUp());
  EXPECT_EQ(shown(3).size(), 3u);

  longPressText("Game 02");
  ASSERT_TRUE(dialogUp());
  key(Button::Back);
  EXPECT_FALSE(dialogUp());
  EXPECT_EQ(activityManager.asks.goHome, 0) << "Back closes the confirmation, not the launcher";

  longPressText("Game 02");
  ASSERT_TRUE(dialogUp());
  key(Button::Confirm);  // the focus starts on Cancel
  EXPECT_FALSE(dialogUp());

  EXPECT_TRUE(removescript::script().ids.empty());
  EXPECT_TRUE(fakesd::has("/.games/game-02/.pkg"));
  EXPECT_EQ(shown(3).size(), 3u);
  // The launcher is as it was: Confirm opens the game that was long-pressed (a long-press selects its row).
  key(Button::Confirm);
  startNewOnTitle();
  ASSERT_EQ(activityManager.replacements.size(), 1u);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started game-02"));
}

TEST_F(RemoveListTest, TheDirectionKeysMoveTheFocusAndConfirmRemovesOnlyFromRemove) {
  addGames(3);
  open();
  longPressText("Game 01");
  key(Button::NavNext);      // Remove
  key(Button::NavPrevious);  // back to Cancel
  key(Button::Confirm);
  EXPECT_TRUE(removescript::script().ids.empty());

  longPressText("Game 01");
  key(Button::NavNext);
  key(Button::Confirm);
  const std::vector<std::string> ids{"game-01"};
  EXPECT_EQ(removescript::script().ids, ids);
  EXPECT_FALSE(dialogUp());
  EXPECT_FALSE(ui().drewLine("Game 01"));
}

TEST_F(RemoveListTest, TheConfirmationOpenedAgainOnAnotherRowAsksAboutThatRow) {
  addGames(3);
  open();
  longPressText("Game 01");
  tapText(tr(STR_CANCEL));
  longPressText("Game 03");
  EXPECT_TRUE(ui().drewLine("Game 03"));
  EXPECT_FALSE(ui().drewLine("Game 01"));
  tapText(tr(STR_GAMES_REMOVE));
  const std::vector<std::string> ids{"game-03"};
  EXPECT_EQ(removescript::script().ids, ids);
}

// The hit rects are the last render's until the next one: after the confirmation opens, a tap that arrives before it is
// drawn still routes to a row of the list underneath.
TEST_F(RemoveListTest, ATapOnARowBeforeTheConfirmationIsDrawnOpensNothing) {
  hostcaps::script().pass = true;
  addGame("alpha", "Alpha");
  addGame("beta", "Beta", "\"solo\",\"pass\"", 1, 1, 2);
  addGame("gamma", "Gamma");
  open();
  const screen::DrawnText* alpha = find("Alpha");
  const screen::DrawnText* beta = find("Beta");
  const screen::DrawnText* gamma = find("Gamma");
  ASSERT_TRUE(alpha && beta && gamma);
  const int betaX = beta->rect.x + beta->rect.width / 2;
  const int betaY = beta->rect.y + beta->rect.height / 2;
  const int gammaX = gamma->rect.x + gamma->rect.width / 2;
  const int gammaY = gamma->rect.y + gamma->rect.height / 2;

  input->longPress(alpha->rect.x + 5, alpha->rect.y + 5);
  activity().loop();  // the confirmation is open; nothing has been drawn since
  input->clear();
  input->tap(gammaX, gammaY);
  activity().loop();
  input->clear();
  EXPECT_EQ(activityManager.asks.replaced, 0) << "the tap opened Gamma under the confirmation";
  input->tap(betaX, betaY);  // two modes: this would push the picker
  activity().loop();
  input->clear();
  EXPECT_EQ(activityManager.asks.pushed, 0);
  render();
  EXPECT_TRUE(dialogUp());
  EXPECT_TRUE(ui().drewLine("Alpha")) << "still about the game that was long-pressed";
  EXPECT_TRUE(removescript::script().ids.empty());
  // A long-press on another row in that window does not retarget it either.
  input->longPress(gammaX, gammaY);
  frame();
  EXPECT_TRUE(ui().drewLine("Alpha"));
  EXPECT_FALSE(ui().drewLine("Gamma"));
}

// The install note is over the list: a row action under it must not move the selection or ask about removing, and a
// hold of Confirm must not open the confirmation over it.
TEST_F(RemoveListTest, UnderTheNoteAHoldOrALongPressOpensNothingAndMovesNothing) {
  addGames(3);
  installerscript::script().report.failed = 1;
  installerscript::script().report.firstError = Error::NotAPackage;
  open();
  ASSERT_TRUE(noteSays(tr(STR_GAMES_INSTALL_NOT_A_PACKAGE)));
  input->holdLong(Button::Confirm);
  frame();
  EXPECT_FALSE(dialogUp());
  longPressText("Game 03");
  EXPECT_FALSE(dialogUp());
  EXPECT_TRUE(noteSays(tr(STR_GAMES_INSTALL_NOT_A_PACKAGE))) << "the note stays until it is dismissed";
  // Dismissed, the selection is still Game 01: the long-press did not move it.
  key(Button::Confirm);
  EXPECT_FALSE(noteSays(tr(STR_GAMES_INSTALL_NOT_A_PACKAGE)));
  key(Button::Confirm);
  startNewOnTitle();
  ASSERT_EQ(activityManager.replacements.size(), 1u);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started game-01"));
}

// A finger down on a row arms the app's tap flash; the long-press that follows opens the confirmation, and the
// confirmation and the list after Remove must not carry it. (The recording target discards paint, so what is pinned
// here is the sequence: the flash cannot be seen from the harness, and the simulator shows the pressed look.)
TEST_F(RemoveListTest, ALongPressThatFollowsATouchDownOpensAndRemovesCleanly) {
  addGames(3);
  open();
  const screen::DrawnText* row = find("Game 02");
  ASSERT_NE(row, nullptr);
  const int x = row->rect.x + row->rect.width / 2;
  const int y = row->rect.y + row->rect.height / 2;
  input->touch.down = true;  // the finger lands
  input->touch.x = x;
  input->touch.y = y;
  frame();
  input->longPress(x, y);
  frame();
  ASSERT_TRUE(dialogUp());
  tapText(tr(STR_GAMES_REMOVE));
  const std::vector<std::string> ids{"game-02"};
  EXPECT_EQ(removescript::script().ids, ids);
  const std::vector<std::string> expected{"Game 01", "Game 03"};
  EXPECT_EQ(shown(3), expected);
  key(Button::Confirm);  // the selection took the next game's place
  startNewOnTitle();
  ASSERT_EQ(activityManager.replacements.size(), 1u);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started game-03"));
}

// Retro deferral 4.10: REMOVE_HOLD_MS is what the code asks the manager, so a hold one millisecond short of it opens
// nothing and a hold at it opens the confirmation, once; the release that ends the hold is not the confirmation's.
TEST_F(RemoveListTest, ConfirmHeldToRemoveHoldMsAsksOnceAndItsReleaseIsSwallowed) {
  addGames(3);
  open();
  key(Button::NavNext);  // Game 02
  input->hold(Button::Confirm, GamesLauncherActivity::REMOVE_HOLD_MS - 1);
  frame();
  EXPECT_FALSE(dialogUp()) << "one millisecond short of the hold";
  input->hold(Button::Confirm, GamesLauncherActivity::REMOVE_HOLD_MS);
  frame();
  ASSERT_TRUE(dialogUp());
  EXPECT_TRUE(ui().drewLine("Game 02"));
  input->hold(Button::Confirm, GamesLauncherActivity::REMOVE_HOLD_MS + 500);  // still held: it fires once
  frame();
  EXPECT_TRUE(dialogUp());
  // The finger lifts. That release must neither choose an option of the confirmation nor dismiss it.
  input->release(Button::Confirm);
  frame();
  EXPECT_TRUE(dialogUp());
  EXPECT_TRUE(removescript::script().ids.empty());
  EXPECT_EQ(activityManager.asks.replaced, 0);
  EXPECT_EQ(activityManager.asks.pushed, 0);
  // A later press of Confirm is the dialog's own: its focused option is the safe one, Cancel, so the game stays.
  key(Button::Confirm);
  EXPECT_FALSE(dialogUp());
  EXPECT_TRUE(removescript::script().ids.empty());
  const std::vector<std::string> all{"Game 01", "Game 02", "Game 03"};
  EXPECT_EQ(shown(3), all);
}

// An ordinary press of Confirm, held for less than the hold, is a click: it opens the selected game.
TEST_F(RemoveListTest, ConfirmReleasedBeforeTheHoldOpensTheGameAndAsksNothing) {
  addGames(3);
  open();
  input->hold(Button::Confirm, GamesLauncherActivity::REMOVE_HOLD_MS - 1);
  input->release(Button::Confirm);
  frame();
  EXPECT_FALSE(dialogUp());
  startNewOnTitle();
  ASSERT_EQ(activityManager.replacements.size(), 1u);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started game-01"));
}

TEST_F(RemoveListTest, HoldingConfirmAsksAboutTheSelectedGame) {
  addGames(3);
  open();
  key(Button::NavNext);  // Game 02
  input->holdLong(Button::Confirm);
  frame();
  EXPECT_TRUE(dialogUp());
  EXPECT_TRUE(ui().drewLine("Game 02"));
  EXPECT_EQ(activityManager.asks.replaced, 0) << "the hold does not open the game";
  EXPECT_EQ(activityManager.asks.pushed, 0);
}

TEST_F(RemoveListTest, TouchOutsideTheButtonsAndTheOtherKeysDoNothingWhileItIsOpen) {
  addGames(3);
  open();
  longPressText("Game 02");
  input->tap(5, 5);
  frame();
  input->swipeDirection(MappedInputManager::SwipeDir::Up);
  frame();
  EXPECT_TRUE(dialogUp());
  EXPECT_EQ(activityManager.asks.replaced, 0);
  EXPECT_EQ(activityManager.asks.pushed, 0);
  EXPECT_TRUE(removescript::script().ids.empty());
}

TEST_F(RemoveListTest, AGameThisHostCannotStartCanBeRemoved) {
  addGame("alpha", "Alpha");
  addGame("too-new", "TooNew", "\"solo\"", 2);
  open();
  longPressText("TooNew");
  EXPECT_TRUE(dialogUp());
  tapText(tr(STR_GAMES_REMOVE));
  const std::vector<std::string> ids{"too-new"};
  EXPECT_EQ(removescript::script().ids, ids);
  EXPECT_FALSE(ui().drewLine("TooNew"));
  EXPECT_TRUE(ui().drewLine("Alpha"));
}

TEST_F(RemoveListTest, ADeleteThatFailsIsReportedAndTheListMatchesTheCard) {
  addGames(3);
  open();
  // The card refuses the marker: nothing changed, the game is still listed.
  removescript::script().onRemove = nullptr;
  removescript::script().result = Error::SdCard;
  longPressText("Game 02");
  tapText(tr(STR_GAMES_REMOVE));
  EXPECT_TRUE(noteSays("Game 02: " + std::string(tr(STR_GAMES_REMOVE_FAILED)))) << ui().joined();
  EXPECT_EQ(shown(3).size(), 3u) << "the game is still on the card, so it is still a row";

  // A tap dismisses the note, as it does the install note.
  input->tap(240, 400);
  frame();
  EXPECT_FALSE(noteSays(tr(STR_GAMES_REMOVE_FAILED)));
}

// A dismissed install note's "and N more" must not come back under a later note: the failed remove writes its own.
TEST_F(RemoveListTest, AFailedRemoveAfterASeveralFailureInstallNoteShowsNoMoreLine) {
  addGames(3);
  installerscript::script().report.failed = 3;
  installerscript::script().report.firstError = Error::NotAPackage;
  open();
  ASSERT_TRUE(noteSays("and 2 more")) << ui().joined();
  key(Button::Confirm);  // dismisses the install note
  ASSERT_FALSE(noteSays("and 2 more"));
  removescript::script().onRemove = nullptr;
  removescript::script().result = Error::SdCard;
  longPressText("Game 02");
  tapText(tr(STR_GAMES_REMOVE));
  EXPECT_TRUE(noteSays("Game 02: " + std::string(tr(STR_GAMES_REMOVE_FAILED)))) << ui().joined();
  EXPECT_FALSE(noteSays("more")) << "the remove note has no more line: " << ui().joined();
}

// ---- installing the inbox after a remove (AI-2, the retro's rev-3) ----

// The note's "remove one first" is followed at once: the package that waited for room installs after the remove, in
// the same visit, and is listed before the person leaves the screen.
TEST_F(RemoveListTest, ARemoveAtTheLimitInstallsTheWaitingPackageAndListsIt) {
  addGames(64);
  installerscript::script().inbox = true;
  installerscript::script().report.failed = 1;
  installerscript::script().report.waiting = 1;
  installerscript::script().report.firstError = Error::TooManyGames;
  std::snprintf(installerscript::script().report.firstFile, sizeof(installerscript::script().report.firstFile),
                "extra.chgame");
  open();
  ASSERT_TRUE(noteSays("extra.chgame: " + std::string(tr(STR_GAMES_INSTALL_TOO_MANY_GAMES)))) << ui().joined();
  key(Button::Confirm);  // dismisses the note
  ASSERT_EQ(installerscript::script().installCalls(), 1);

  installerscript::script().report = GamePackageInstaller::Report{};
  installerscript::script().report.installed = 1;
  installerscript::script().onInstall = [] {
    EXPECT_FALSE(fakesd::has("/.games/game-02")) << "the install runs after the remove";
    addGame("extra", "Extra");  // sorts first: on the page the list shows
  };
  installerscript::script().popupWhenInstalling = false;
  longPressText("Game 02");
  tapText(tr(STR_GAMES_REMOVE));

  EXPECT_EQ(removescript::script().ids, std::vector<std::string>{"game-02"});
  EXPECT_EQ(installerscript::script().installCalls(), 2) << "one install on entering, one after the remove";
  const std::vector<std::string> tail(installerscript::script().order.end() - 2, installerscript::script().order.end());
  EXPECT_EQ(tail, (std::vector<std::string>{"hasInbox", "installAll"}));
  EXPECT_TRUE(installerscript::script().popupWhenInstalling) << "\"Installing\" was drawn before the installer ran";
  EXPECT_FALSE(installerscript::script().lockWhenInstalling) << "the render task is not held off for an install";
  EXPECT_TRUE(removescript::script().lockWhenRemoving) << "the remove still runs under the lock";
  EXPECT_EQ(fakelock::selfDeadlocks.load(), 0) << "no scope took the lock twice";
  EXPECT_TRUE(ui().drewLine("Extra")) << "the installed game is listed: " << ui().joined();
  EXPECT_FALSE(ui().drewLine("Game 02"));
  EXPECT_FALSE(noteSays(tr(STR_GAMES_INSTALL_TOO_MANY_GAMES))) << "nothing failed, so no note: " << ui().joined();
}

TEST_F(RemoveListTest, AnInstallThatFailsAfterARemoveShowsItsNoteWithTheKindsOfTheOthers) {
  addGames(3);
  open();
  installerscript::script().inbox = true;
  installerscript::script().report.installed = 1;
  installerscript::script().report.failed = 3;
  installerscript::script().report.waiting = 2;
  installerscript::script().report.firstError = Error::TooManyGames;
  std::snprintf(installerscript::script().report.firstFile, sizeof(installerscript::script().report.firstFile),
                "second.chgame");
  installerscript::script().onInstall = [] { addGame("first", "First"); };
  longPressText("Game 02");
  tapText(tr(STR_GAMES_REMOVE));

  EXPECT_TRUE(noteSays("second.chgame: " + std::string(tr(STR_GAMES_INSTALL_TOO_MANY_GAMES)))) << ui().joined();
  EXPECT_TRUE(ui().drewLine("1 more waiting for room")) << ui().joined();
  EXPECT_TRUE(ui().drewLine("and 1 more not installed")) << ui().joined();
  EXPECT_TRUE(ui().drewLine("First")) << "the one that did install is listed under the note: " << ui().joined();
  EXPECT_FALSE(ui().drewLine("Game 02"));
  EXPECT_FALSE(noteSays(tr(STR_GAMES_REMOVE_FAILED)));

  key(Button::Back);  // dismisses the note, and only the note
  EXPECT_FALSE(noteSays("more")) << ui().joined();
  EXPECT_EQ(activityManager.asks.goHome, 0);
}

TEST_F(RemoveListTest, TheNoteOfAnInstallAfterARemoveKeepsNoLineOfTheEarlierNote) {
  addGames(3);
  installerscript::script().report.failed = 3;
  installerscript::script().report.waiting = 3;
  installerscript::script().report.firstError = Error::TooManyGames;
  open();
  ASSERT_TRUE(ui().drewLine("2 more waiting for room")) << ui().joined();
  key(Button::Confirm);  // dismisses the install note

  installerscript::script().inbox = true;
  installerscript::script().report = GamePackageInstaller::Report{};
  installerscript::script().report.failed = 2;
  installerscript::script().report.firstError = Error::BadCrc;
  longPressText("Game 02");
  tapText(tr(STR_GAMES_REMOVE));
  EXPECT_TRUE(noteSays(tr(STR_GAMES_INSTALL_BAD_CRC))) << ui().joined();
  EXPECT_TRUE(ui().drewLine("and 1 more")) << ui().joined();
  EXPECT_FALSE(noteSays("waiting")) << "the entry's note is gone with its lines: " << ui().joined();
}

TEST_F(RemoveListTest, ARemoveWithAnEmptyInboxInstallsNothing) {
  addGames(3);
  open();
  installerscript::script().order.clear();
  longPressText("Game 02");
  // The tap by hand, so the theme's calls are read before the next render clears them.
  const screen::DrawnText* remove = find(tr(STR_GAMES_REMOVE));
  ASSERT_NE(remove, nullptr) << ui().joined();
  input->tap(remove->rect.x + remove->rect.width / 2, remove->rect.y + remove->rect.height / 2);
  if (!input->consumeSuppressedRelease()) activity().loop();  // frame()'s pass, without its render
  input->clear();
  auto& theme = UITheme::getInstance().getTheme();
  EXPECT_TRUE(theme.drew("drawPopup", tr(STR_GAMES_REMOVING))) << "the tap removed the game";
  EXPECT_FALSE(theme.drew("drawPopup", tr(STR_GAMES_INSTALLING))) << "nothing to install, so no \"Installing\"";
  render();
  // Asked, and nothing to install: no installAll.
  EXPECT_EQ(installerscript::script().order, std::vector<std::string>{"hasInbox"});
  EXPECT_EQ(shown(3), (std::vector<std::string>{"Game 01", "Game 03"}));
}

TEST_F(RemoveListTest, AFailedRemoveInstallsNothing) {
  addGames(3);
  open();
  installerscript::script().order.clear();
  installerscript::script().inbox = true;
  removescript::script().onRemove = nullptr;
  removescript::script().result = Error::SdCard;
  longPressText("Game 02");
  tapText(tr(STR_GAMES_REMOVE));
  // Installs only after a remove that reports None. A remove that reports the card's fault after the .pkg went (the
  // game is then unlisted, a place freed) installs nothing either: the waiting package installs on the next visit.
  EXPECT_TRUE(installerscript::script().order.empty()) << "a failed remove does not even ask about the inbox";
  EXPECT_TRUE(noteSays("Game 02: " + std::string(tr(STR_GAMES_REMOVE_FAILED)))) << ui().joined();
}

// A remove that reports the card's fault after the .pkg went: the game is unlisted, a place is free, and still nothing
// installs until the next visit (the install follows only a remove that reports None).
TEST_F(RemoveListTest, AFailedRemoveThatTookThePkgInstallsNothingEither) {
  addGames(3);
  open();
  installerscript::script().order.clear();
  installerscript::script().inbox = true;
  removescript::script().onRemove = [] { fakesd::removeEntry("/.games/game-02/.pkg"); };
  removescript::script().result = Error::SdCard;
  longPressText("Game 02");
  tapText(tr(STR_GAMES_REMOVE));
  EXPECT_TRUE(installerscript::script().order.empty()) << "the inbox is not asked about";
  EXPECT_TRUE(noteSays("Game 02: " + std::string(tr(STR_GAMES_REMOVE_FAILED)))) << ui().joined();
  EXPECT_EQ(shown(3), (std::vector<std::string>{"Game 01", "Game 03"})) << "a game without its .pkg is not listed";
}

TEST_F(RemoveListTest, AFailedRemoveAfterANoteWithBothMoreLinesShowsNeither) {
  addGames(3);
  installerscript::script().report.failed = 4;
  installerscript::script().report.waiting = 3;
  installerscript::script().report.firstError = Error::TooManyGames;
  open();
  ASSERT_TRUE(ui().drewLine("2 more waiting for room")) << ui().joined();
  ASSERT_TRUE(ui().drewLine("and 1 more not installed")) << ui().joined();
  key(Button::Confirm);  // dismisses the install note
  removescript::script().onRemove = nullptr;
  removescript::script().result = Error::SdCard;
  longPressText("Game 02");
  tapText(tr(STR_GAMES_REMOVE));
  EXPECT_TRUE(noteSays("Game 02: " + std::string(tr(STR_GAMES_REMOVE_FAILED)))) << ui().joined();
  EXPECT_FALSE(noteSays("more")) << "the remove note has no more-line: " << ui().joined();
}

TEST_F(RemoveListTest, ADeleteThatStopsAfterTheMarkerIsReportedAndTheGameIsNoLongerListed) {
  addGames(3);
  placeData("game-02");
  open();
  removescript::script().onRemove = [] { fakesd::removeEntry("/.games/game-02/.pkg"); };
  removescript::script().result = Error::SdCard;
  longPressText("Game 02");
  tapText(tr(STR_GAMES_REMOVE));
  EXPECT_TRUE(noteSays("Game 02: " + std::string(tr(STR_GAMES_REMOVE_FAILED)))) << ui().joined();
  const std::vector<std::string> expected{"Game 01", "Game 03"};
  EXPECT_EQ(shown(3), expected) << "a folder without its marker is not listed";
  EXPECT_TRUE(fakesd::has("/.games-data/game-02/store.bin"));
  EXPECT_TRUE(logHas("Cannot remove game-02"));
}

TEST_F(RemoveListTest, ALongPressWhileTheNoteIsUpOpensNothing) {
  addGames(2);
  installerscript::script().report.failed = 1;
  installerscript::script().report.firstError = Error::NotAPackage;
  open();
  ASSERT_TRUE(noteSays(tr(STR_GAMES_INSTALL_NOT_A_PACKAGE))) << ui().joined();
  longPressText("Game 01");
  EXPECT_FALSE(dialogUp());
}

// ---- the list pages by whole pages ----

class PagingTest : public RemoveListTest {
 protected:
  // Opens the list of `count` games and checks every page: it starts at the row after the previous page's last,
  // the last page repeats nothing and holds what is left, and swiping past the last page stays on it.
  void expectWholePages(const int count) {
    addGames(count);
    open();
    const std::vector<std::string> first = shown(count);
    const int page = static_cast<int>(first.size());
    ASSERT_GE(page, 2);
    ASSERT_LT(page, count) << "the list must not fit one screen";
    ASSERT_EQ(first.front(), nameOf(1));
    const int pages = (count + page - 1) / page;
    for (int p = 0; p < pages; ++p) {
      if (p > 0) {
        swipeUp();
      }
      const int from = p * page + 1;
      const int to = std::min(count, (p + 1) * page);
      std::vector<std::string> expected;
      for (int i = from; i <= to; ++i) expected.push_back(nameOf(i));
      EXPECT_EQ(shown(count), expected) << "page " << p + 1 << " of " << pages;
      EXPECT_EQ(ui().bitmaps, static_cast<int>(expected.size())) << "one icon per game; padding draws none";
    }
    // Past the last page: still the last page.
    swipeUp();
    const int lastFrom = (pages - 1) * page + 1;
    EXPECT_EQ(shown(count).front(), nameOf(lastFrom));
    EXPECT_EQ(shown(count).back(), nameOf(count));
    // And back, a whole page at a time.
    for (int p = pages - 2; p >= 0; --p) {
      swipeDown();
      EXPECT_EQ(shown(count).front(), nameOf(p * page + 1)) << "back to page " << p + 1;
    }
    swipeDown();
    EXPECT_EQ(shown(count).front(), nameOf(1));
  }

  int pageSize(const int count) {
    addGames(count);
    open();
    return static_cast<int>(shown(count).size());
  }
};

TEST_F(PagingTest, NineGamesPageWholeAndTheLastPageRepeatsNoRow) { expectWholePages(9); }
TEST_F(PagingTest, SixteenGamesPageWhole) { expectWholePages(16); }
TEST_F(PagingTest, TwentyFiveGamesPageWhole) { expectWholePages(25); }

TEST_F(PagingTest, TheBlankRowsThatPadTheLastPageCannotBeTappedLongPressedOrSelected) {
  const int count = 9;
  const int page = pageSize(count);
  ASSERT_GT(page, 1);
  ASSERT_NE(count % page, 0) << "this page size leaves no padding to test";
  const int lastPageGames = count % page;
  swipeUp();
  for (int p = 1; (p + 1) * page < count; ++p) swipeUp();
  const std::vector<std::string> last = shown(count);
  ASSERT_EQ(static_cast<int>(last.size()), lastPageGames);
  // The row after the last game, in the pitch of the rows above it.
  const screen::DrawnText* a = find(last.front());
  const screen::DrawnText* b = last.size() > 1 ? find(last[1]) : nullptr;
  const int pitch = b ? b->rect.y - a->rect.y : 80 + 8;
  const screen::DrawnText* end = find(last.back());
  const int x = end->rect.x + end->rect.width / 2;
  const int y = end->rect.y + end->rect.height / 2 + pitch;

  input->tap(x, y);
  frame();
  EXPECT_EQ(activityManager.asks.replaced, 0) << "a tap on a blank row opens nothing";
  EXPECT_EQ(activityManager.asks.pushed, 0);
  input->longPress(x, y);
  frame();
  EXPECT_FALSE(dialogUp());
  // The selection never left Game 01 (the swipes scroll the viewport only): Confirm opens it.
  key(Button::Confirm);
  startNewOnTitle();
  ASSERT_EQ(activityManager.replacements.size(), 1u);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started game-01"));

  // The control: the same taps one row up land on the last game, so the blank row's spot really was empty.
  reopen();
  while (!ui().drewLine(nameOf(count))) swipeUp();
  const screen::DrawnText* lastGame = find(nameOf(count));
  ASSERT_NE(lastGame, nullptr);
  input->tap(lastGame->rect.x + lastGame->rect.width / 2, lastGame->rect.y + lastGame->rect.height / 2);
  frame();
  startNewOnTitle();
  ASSERT_EQ(activityManager.replacements.size(), 1u);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started game-09"));
}

TEST_F(PagingTest, AListThatFillsWholePagesHasNoBlankRowAndOneThatFitsOnePageDoesNotScroll) {
  const int page = pageSize(25);
  ASSERT_GT(page, 2);
  ASSERT_LT(2 * page, 25);
  for (int i = 2 * page + 1; i <= 25; ++i) takeGameOffTheCard(idOf(i));
  reopen();
  const int count = 2 * page;
  ASSERT_EQ(static_cast<int>(shown(count).size()), page);
  swipeUp();
  EXPECT_EQ(shown(count).front(), nameOf(page + 1));
  EXPECT_EQ(static_cast<int>(shown(count).size()), page) << "the last page is full";
  swipeUp();
  EXPECT_EQ(shown(count).front(), nameOf(page + 1)) << "and there is nothing past it";

  // Three games fit one screen: nothing is padded, and a swipe leaves the list where it is.
  for (int i = 4; i <= count; ++i) takeGameOffTheCard(idOf(i));
  reopen();
  const std::vector<std::string> three{nameOf(1), nameOf(2), nameOf(3)};
  EXPECT_EQ(shown(3), three);
  EXPECT_EQ(ui().bitmaps, 3);
  swipeUp();
  EXPECT_EQ(shown(3), three);
}

TEST_F(PagingTest, RemovingTheLastGameOfTheLastPageDropsThatPage) {
  const int page = pageSize(25);
  for (int i = page + 2; i <= 25; ++i) takeGameOffTheCard(idOf(i));
  reopen();
  const int count = page + 1;  // a page and one game
  swipeUp();
  ASSERT_EQ(shown(count).size(), 1u);
  longPressText(nameOf(count));
  tapText(tr(STR_GAMES_REMOVE));
  EXPECT_EQ(static_cast<int>(shown(count).size()), page) << "one whole page is left";
  EXPECT_EQ(shown(count).front(), nameOf(1));
  EXPECT_EQ(ui().bitmaps, page);
}

TEST_F(PagingTest, TheKeysNeverLandOnABlankRowAndAStepPastAPageShowsTheWholeNextPage) {
  const int count = 9;
  const int page = pageSize(count);
  ASSERT_GT(page, 1);
  // A step past the page's last row shows the whole next page, with the selection on its first row.
  key(Button::NavNext, page);
  EXPECT_EQ(shown(count).front(), nameOf(page + 1));
  key(Button::NavPrevious);
  EXPECT_EQ(shown(count).front(), nameOf(1)) << "and a step back shows the whole page before";

  // Down to the last game and one more wraps to the first: the blank rows are not on the way.
  key(Button::NavNext, count - 1 - (page - 1));
  EXPECT_EQ(shown(count).back(), nameOf(count));
  key(Button::NavNext);
  EXPECT_EQ(shown(count).front(), nameOf(1)) << "past the last game the selection wraps to the first";
  key(Button::NavPrevious);
  key(Button::Confirm);
  startNewOnTitle();
  ASSERT_EQ(activityManager.replacements.size(), 1u);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started game-09")) << "back from the first is the last game, not a blank row";
}

// ---- the launcher comes back on the page of the game last opened ----

class ReturnTest : public RemoveListTest {
 protected:
  int pageOf(const int number, const int count) {
    const int page = static_cast<int>(shown(count).size());
    return (number - 1) / page * page + 1;  // the first game on the page holding `number`
  }
};

TEST_F(ReturnTest, TheNewLauncherShowsTheWholePageHoldingTheGameThatWasOpenedAndSelectsIt) {
  addGames(25);
  open();
  const int page = static_cast<int>(shown(25).size());
  ASSERT_GT(page, 2);
  key(Button::NavNext, 19);  // Game 20
  key(Button::Confirm);
  startNewOnTitle();
  ASSERT_EQ(activityManager.replacements.size(), 1u);

  reopen();  // Leave, or the match ended: goToGames() builds a fresh launcher
  const int first = pageOf(20, 25);
  ASSERT_GT(first, 1) << "game 20 is not on the first page";
  EXPECT_EQ(shown(25).front(), nameOf(first)) << "the whole page, from its first row";
  EXPECT_TRUE(ui().drewLine("Game 20"));
  key(Button::Confirm);  // the selection is on it
  startNewOnTitle();
  ASSERT_EQ(activityManager.replacements.size(), 1u);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started game-20"));
}

TEST_F(ReturnTest, ATapOpensTheGameAndTheReturnAlsoWorksForTheLastPage) {
  addGames(25);
  open();
  for (int swipes = 0; swipes < 10 && !ui().drewLine("Game 25"); ++swipes) swipeUp();
  tapText("Game 25");
  startNewOnTitle();
  ASSERT_EQ(activityManager.replacements.size(), 1u);
  reopen();
  EXPECT_TRUE(ui().drewLine("Game 25"));
  EXPECT_EQ(shown(25).back(), "Game 25");
  EXPECT_FALSE(ui().drewLine("Game 01"));
}

TEST_F(ReturnTest, AGameOpenedThroughTheModePickerIsRememberedToo) {
  hostcaps::script().pass = true;
  for (int i = 1; i <= 24; ++i) addGame(idOf(i), nameOf(i));
  addGame("zz-both", "Game 99", "\"solo\",\"pass\"", 1, 1, 2);  // sorts last, on a later page
  open();
  for (int swipes = 0; swipes < 10 && !ui().drewLine("Game 99"); ++swipes) swipeUp();
  tapText("Game 99");
  EXPECT_EQ(activityManager.asks.pushed, 1) << "two modes: the picker first";
  EXPECT_TRUE(activityManager.replacements.empty());
  reopen();  // the picker replaced itself with the match, and the match was left
  EXPECT_TRUE(ui().drewLine("Game 99"));
  EXPECT_FALSE(ui().drewLine("Game 01"));
}

TEST_F(ReturnTest, AGameThatIsGoneReturnsToTheTopAndSoDoesAFreshBoot) {
  addGames(25);
  open();
  key(Button::NavNext, 19);
  key(Button::Confirm);
  startNewOnTitle();
  ASSERT_EQ(activityManager.replacements.size(), 1u);
  takeGameOffTheCard("game-20");  // removed elsewhere (a computer), or the listing changed
  reopen();
  EXPECT_EQ(shown(24).front(), "Game 01");
  key(Button::Confirm);
  startNewOnTitle();
  ASSERT_EQ(activityManager.replacements.size(), 1u);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started game-01")) << "the selection is the first row";

  // A fresh boot: nothing was opened.
  dropMatch();
  GamesLauncherActivity::forgetOpenedGame();
  fakelog::clearLines();
  reopen();
  EXPECT_EQ(shown(24).front(), "Game 01");
}

TEST_F(ReturnTest, RemovingTheRememberedGameSendsTheNextLauncherToTheTop) {
  addGames(25);
  open();
  key(Button::NavNext, 19);
  key(Button::Confirm);
  reopen();
  ASSERT_TRUE(ui().drewLine("Game 20"));
  // Remove the remembered game itself (the long-press selects its row).
  longPressText("Game 20");
  tapText(tr(STR_GAMES_REMOVE));
  EXPECT_EQ(removescript::script().ids.back(), "game-20");
  // The same id installed again: only a fingerprint that the remove cleared sends the launcher to the top (a game
  // that is merely gone would show the top whether or not it was cleared).
  addGame("game-20", "Game 20");
  reopen();
  EXPECT_TRUE(ui().drewLine("Game 01")) << "the remove cleared the memory: the top";
  EXPECT_FALSE(ui().drewLine("Game 20"));
}

TEST_F(ReturnTest, RemovingAnotherGameKeepsTheMemory) {
  addGames(25);
  open();
  key(Button::NavNext, 19);
  key(Button::Confirm);
  reopen();
  longPressText("Game 21");
  ASSERT_TRUE(dialogUp());
  tapText(tr(STR_GAMES_REMOVE));
  reopen();
  EXPECT_TRUE(ui().drewLine("Game 20")) << "still on the page of Game 20";
  EXPECT_NE(shown(24).front(), "Game 01");
}

TEST_F(ReturnTest, AFailedRemoveOfTheRememberedGameKeepsTheMemory) {
  addGames(25);
  open();
  key(Button::NavNext, 19);
  key(Button::Confirm);
  reopen();
  removescript::script().onRemove = nullptr;
  removescript::script().result = Error::SdCard;
  longPressText("Game 20");
  tapText(tr(STR_GAMES_REMOVE));
  reopen();
  EXPECT_TRUE(ui().drewLine("Game 20"));
  EXPECT_NE(shown(25).front(), "Game 01");
}
