#include <I18n.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "InstallerScript.h"
#include "MatchSupport.h"
#include "activities/games/GameMatchActivity.h"
#include "activities/games/GamesListActivity.h"
#include "util/ButtonNavigator.h"

// The real GamesListActivity, GameRegistry, UiListActivity, and FreeInkUI over the screen doubles
// (screen_stubs/), the installer scripted (list_stubs/InstallerScript.h). Entry 5 of
// epic-install-and-launcher. Entries 8 to 12 copy this file for the launcher, the mode picker, and
// the installer screens: the fixture below opens a screen, draws it, and finds a row by the text it drew.
// Each test names the deferred-work item it pins.

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

// A message that wrapped over lines, read as one line of words.
std::string flat(std::string text) {
  for (char& c : text)
    if (c == '\n') c = ' ';
  return text;
}

// Whether the test's table knows an Error: a switch over the whole enum, so a new value must be added here.
constexpr bool listed(const Error error) {
  switch (error) {
    case Error::None:
    case Error::SdCard:
    case Error::OutOfMemory:
    case Error::NotAPackage:
    case Error::BadManifest:
    case Error::BadMember:
    case Error::NoMain:
    case Error::TooManyMembers:
    case Error::BadImage:
    case Error::PackageTooBig:
    case Error::MemberTooBig:
    case Error::ImagesTooBig:
    case Error::BadSize:
    case Error::BadCrc:
    case Error::BinaryLua:
    case Error::Unsupported:
    case Error::BadDirectory:
    case Error::SourcesTooBig:
    case Error::UnknownIcon:
      return true;
  }
  return false;
}

class ListTest : public match::ScreenTest {
 protected:
  void SetUp() override {
    ScreenTest::SetUp();
    installerscript::reset();
    ButtonNavigator::setMappedInputManager(*input);
  }

  void TearDown() override {
    fakertos::release();  // a test that failed while holding the VM must not leave it held
    // What the manager does when a screen goes: onExit under the lock, then the destructor under it.
    if (list) {
      activityManager.exitHolding(*list);
      activityManager.destroyHolding(list);
    }
    dropMatch();
    ScreenTest::TearDown();
  }

  // The match the list opened goes as the manager would let it go.
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

  // A game folder as the installer leaves it: manifest.json, a main.lua, and the .pkg commit marker.
  static void addGame(const std::string& id, const std::string& name, const std::string& modes = "\"solo\"",
                      const int api = 1, const int seatsMin = 1, const int seatsMax = 1, const std::string& folder = "",
                      const std::string& pkg = PKG) {
    const std::string dir = "/.games/" + (folder.empty() ? id : folder);
    fakesd::addFile(dir + "/manifest.json", manifestJson(id, name, modes, api, seatsMin, seatsMax));
    fakesd::addFile(dir + "/main.lua", std::string("return {}\n"));
    if (!pkg.empty()) fakesd::addFile(dir + "/.pkg", pkg);
  }

  // The fixture game `name` (test/game_script/fixtures) as an installed game: its files and a .pkg.
  static void installFixtureWithPkg(const std::string& name) {
    match::installFixture(name);
    fakesd::addFile("/.games/" + name + "/.pkg", PKG);
  }

  // ---- the screen ----

  // The list through the base the manager drives it by (its own onEnter and render are private).
  Activity& activity() { return *list; }

  // Opens the list as the manager would (onEnter), and draws it.
  void open() {
    list = std::make_unique<GamesListActivity>(*renderer, *input);
    activity().onEnter();
    render();
  }
  // A second visit: the list goes, the manager's record and the theme's start over, and the list opens again.
  // The script (InstallerScript.h) and the log are the test's to change before it calls this.
  void reopen() {
    dropMatch();
    activityManager.exitHolding(*list);
    activityManager.destroyHolding(list);
    activityManager.reset();
    theme().reset();
    open();
  }
  // Draws the screen afresh: what the record holds afterwards is that one frame.
  void render() {
    if (screen::RecordingTarget::newest()) screen::RecordingTarget::newest()->forget();
    UITheme::getInstance().getTheme().calls.clear();
    activity().render(RenderLock(*list));
    activityManager.markRendered();
  }
  // One pass of the main loop, then the frame's input is over.
  void frame() {
    activity().loop();
    input->clear();
  }
  screen::RecordingTarget& ui() {
    if (!screen::RecordingTarget::newest()) {
      std::fprintf(stderr, "no screen has been drawn: the list has no UI target yet\n");
      std::abort();
    }
    return *screen::RecordingTarget::newest();
  }
  ThemeDouble& theme() { return UITheme::getInstance().getTheme(); }

  // Taps the row drawn under `label`, in the middle of its text, as a finger would.
  void tapRow(const std::string& label) {
    for (const screen::DrawnText& drawn : ui().drawn) {
      if (drawn.text != label) continue;
      input->tap(drawn.rect.x + drawn.rect.width / 2, drawn.rect.y + drawn.rect.height / 2);
      frame();
      return;
    }
    FAIL() << "the list does not draw a row \"" << label << "\": " << ui().joined();
  }

  // The rows the list drew, top to bottom (its lines but for the note's).
  std::vector<std::string> rows(const std::vector<std::string>& among) {
    std::vector<std::string> found;
    for (const screen::DrawnText& drawn : ui().drawn)
      for (const std::string& name : among)
        if (drawn.text == name) found.push_back(name);
    return found;
  }

  // Runs the match the list replaced itself with, so the game it was given is the one that starts.
  GameMatchActivity* enterReplacement() {
    if (activityManager.replacements.empty()) return nullptr;
    auto* match = dynamic_cast<GameMatchActivity*>(activityManager.replacements.back().get());
    if (match) {
      match->onEnter();
      entered = match;
    }
    return match;
  }

  std::unique_ptr<GamesListActivity> list;
  Activity* entered = nullptr;  // the replacement enterReplacement() started, which needs its onExit
};

// ---- opening: the inbox first, then the installed games (## 4.4: GamesListActivity's filters) ----

TEST_F(ListTest, OpeningInstallsTheInboxOnceAndThenListsTheGames) {
  addGame("alpha", "Alpha");
  open();
  const std::vector<std::string> expected{"hasInbox", "installAll"};
  EXPECT_EQ(installerscript::script().order, expected);
  EXPECT_TRUE(ui().drewLine("Alpha"));
  render();  // a repaint is not a visit
  frame();
  EXPECT_EQ(installerscript::script().installCalls(), 1);
}

TEST_F(ListTest, AGameTheInstallerJustInstalledIsInTheSameVisitsList) {
  // The list installs before it reads the registry (installInbox, then loadGames): a package put in the inbox
  // shows in the list it was installed by, not the next one.
  installerscript::script().inbox = true;
  installerscript::script().onInstall = [] { addGame("fresh", "Fresh"); };
  open();
  EXPECT_TRUE(ui().drewLine("Fresh"));
}

TEST_F(ListTest, TheInstallingPopupIsShownBeforeTheInstallOnlyWhenTheInboxHoldsAFile) {
  installerscript::script().inbox = true;
  open();
  EXPECT_TRUE(installerscript::script().popupWhenInstalling) << "the popup must be up while the installer works";

  installerscript::reset();
  reopen();
  EXPECT_FALSE(installerscript::script().popupWhenInstalling);
  EXPECT_FALSE(theme().drew("drawPopup", tr(STR_GAMES_INSTALLING)));
}

TEST_F(ListTest, TheHeaderNamesTheScreenAndTheFooterHintsBackSelectAndTheDirections) {
  addGame("alpha", "Alpha");
  open();
  EXPECT_TRUE(theme().drew("drawHeader", tr(STR_GAMES_TITLE)));
  ASSERT_FALSE(theme().hints.empty());
  EXPECT_EQ(theme().hints.back().btn1, tr(STR_BACK));
  EXPECT_EQ(theme().hints.back().btn2, tr(STR_SELECT));
  EXPECT_EQ(theme().hints.back().btn3, tr(STR_DIR_UP));
  EXPECT_EQ(theme().hints.back().btn4, tr(STR_DIR_DOWN));
}

TEST_F(ListTest, NoGamesShowsTheEmptyMessage) {
  open();
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_EMPTY)));
  fakesd::addDir("/.games");  // a card with an empty games folder is the same
  reopen();
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_EMPTY)));
}

TEST_F(ListTest, AConfirmOrATapOnAnEmptyListDoesNothing) {
  open();
  input->click(Button::Confirm);
  frame();
  input->tap(240, 300);
  frame();
  EXPECT_TRUE(activityManager.replacements.empty());
  EXPECT_EQ(activityManager.asks.replaced, 0);
}

TEST_F(ListTest, GamesAreListedByNameWhateverOrderTheFoldersAreIn) {
  addGame("zulu", "zebra");
  addGame("mike", "Mango");
  addGame("alpha", "apple");
  open();
  const std::vector<std::string> expected{"apple", "Mango", "zebra"};
  EXPECT_EQ(rows({"apple", "Mango", "zebra"}), expected);
}

// ## 4.4 (the tracer item): the dir/id filter. A folder is a game only when it holds a .pkg and a manifest.json
// that parses and names the folder's own id. (GameRegistry also skips a folder whose name begins with a dot, but
// no manifest id can begin with one, so that clause is unobservable and has no case here.)
TEST_F(ListTest, OnlyAFolderWithAValidPkgAndAManifestOfItsOwnIdIsListed) {
  addGame("good", "Good");
  addGame("half-installed", "NoPkg", "\"solo\"", 1, 1, 1, "", "");              // no .pkg: not installed
  addGame("bad-pkg", "BadPkg", "\"solo\"", 1, 1, 1, "", "v9\nnot-a-hash\n");    // a .pkg that is not one
  addGame("real-id", "WrongFolder", "\"solo\"", 1, 1, 1, "other-name");         // the folder is not the id
  fakesd::addFile("/.games/no-manifest/main.lua", std::string("return {}\n"));  // nothing to read
  fakesd::addFile("/.games/no-manifest/.pkg", PKG);
  fakesd::addFile("/.games/broken/manifest.json", std::string("{not json"));
  fakesd::addFile("/.games/broken/.pkg", PKG);
  open();
  EXPECT_TRUE(ui().drewLine("Good"));
  for (const char* absent : {"NoPkg", "BadPkg", "WrongFolder"}) EXPECT_FALSE(ui().drewLine(absent)) << absent;
  const std::vector<std::string> expected{"Good"};
  EXPECT_EQ(rows({"Good", "NoPkg", "BadPkg", "WrongFolder"}), expected);
  // A folder with no readable manifest has no name to draw, so the registry's own account of it is what shows it
  // was seen and left out: the folders it skipped, and the one game it found.
  EXPECT_TRUE(logHas("Skipping half-installed: no valid .pkg"));
  EXPECT_TRUE(logHas("Skipping bad-pkg: no valid .pkg"));
  EXPECT_TRUE(logHas("Skipping no-manifest: no manifest.json"));
  EXPECT_TRUE(logHas("Skipping broken: "));
  EXPECT_TRUE(logHas("Skipping other-name: manifest id is real-id"));
  EXPECT_TRUE(logHas("Found 1 games"));
  // Opening the only row opens Good, not one of the folders that were skipped.
  tapRow("Good");
  ASSERT_EQ(activityManager.replacements.size(), 1u);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started good"));
}

// ## 4.4 (the host-caps item): the Manifest::check filter. Only a game that can start solo on this host is listed.
TEST_F(ListTest, AGameThisHostCannotStartSoloIsNotListed) {
  addGame("solo-game", "SoloGame");
  addGame("solo-and-pass", "SoloAndPass", "\"solo\",\"pass\"", 1, 1, 2);        // solo starts; pass is not offered yet
  addGame("solo-and-nearby", "SoloAndNearby", "\"solo\",\"nearby\"", 1, 1, 2);  // solo starts; there is no radio
  addGame("too-new", "TooNew", "\"solo\"", 2);                                  // api 2 on an api 1 host
  addGame("pass-only", "PassOnly", "\"pass\"", 1, 2, 2);                        // no pass capability on this host
  addGame("nearby-only", "NearbyOnly", "\"nearby\"", 1, 2, 2);                  // no nearby capability on this host
  addGame("too-many-seats", "TooManySeats", "\"pass\"", 1, 3, 4);               // more seats than the host has
  open();
  const std::vector<std::string> all{"SoloGame", "SoloAndPass", "SoloAndNearby", "TooNew",
                                     "PassOnly", "NearbyOnly",  "TooManySeats"};
  const std::vector<std::string> expected{"SoloAndNearby", "SoloAndPass", "SoloGame"};
  EXPECT_EQ(rows(all), expected);
  // The reason is logged for each one left out, from the check's own verdict.
  EXPECT_TRUE(logHas("Not listing too-new: "));
  EXPECT_TRUE(logHas("Not listing pass-only: "));
  EXPECT_TRUE(logHas("Not listing nearby-only: "));
  EXPECT_TRUE(logHas("Not listing too-many-seats: "));
  EXPECT_FALSE(logHas("Not listing solo-game"));
}

// ---- opening a game: a tap or Confirm replaces the list with the match (## 4.4: the tracer item) ----

TEST_F(ListTest, ATapOnARowReplacesTheListWithThatGamesMatch) {
  installFixtureWithPkg("tracer");
  installFixtureWithPkg("timer");
  open();
  ASSERT_TRUE(ui().drewLine("Timer"));
  ASSERT_TRUE(ui().drewLine("Tracer"));
  tapRow("Tracer");
  EXPECT_EQ(activityManager.asks.replaced, 1);
  ASSERT_EQ(activityManager.replacements.size(), 1u);
  ASSERT_NE(enterReplacement(), nullptr) << "what the list opens is a GameMatchActivity";
  EXPECT_TRUE(logHas("Started tracer"));
  EXPECT_FALSE(logHas("Started timer"));
  EXPECT_EQ(activityManager.asks.goHome, 0);
}

TEST_F(ListTest, ConfirmOpensTheSelectedRowAndTheNextPreviousKeysMoveTheSelection) {
  installFixtureWithPkg("counter");  // Counter, Timer, Tracer, in that order
  installFixtureWithPkg("timer");
  installFixtureWithPkg("tracer");
  open();
  input->click(Button::NavNext);
  frame();
  input->click(Button::NavNext);
  frame();
  input->click(Button::NavPrevious);
  frame();
  input->click(Button::Confirm);
  frame();
  ASSERT_EQ(activityManager.replacements.size(), 1u);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started timer"));  // 0 -> 1 -> 2 -> 1

  // Confirm with nothing moved opens the first row.
  fakelog::clearLines();
  reopen();
  input->click(Button::Confirm);
  frame();
  ASSERT_EQ(activityManager.replacements.size(), 1u);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started counter"));
}

// ---- Back goes Home (## 3.6's neighbour: the tab the list is opened from is the row Home reselects) ----

TEST_F(ListTest, BackGoesStraightHomeAndDoesNotPopTheStack) {
  addGame("alpha", "Alpha");
  open();
  input->click(Button::Back);
  frame();
  EXPECT_EQ(activityManager.asks.goHome, 1);
  EXPECT_EQ(activityManager.asks.popped, 0);  // not finish(): Home reselects the Games row by the screen's name
  EXPECT_EQ(activityManager.asks.replaced, 0);
}

// ---- the install failure notice (## 4.6: reasonText) ----

TEST_F(ListTest, AFailedFileIsExplainedOnceInAPopupOverTheListAndBackDismissesIt) {
  addGame("alpha", "Alpha");
  installerscript::script().report.failed = 1;
  installerscript::script().report.firstError = Error::NoMain;
  std::snprintf(installerscript::script().report.firstFile, sizeof(installerscript::script().report.firstFile),
                "broken.cpgame");
  open();
  const std::string shown = flat(ui().joined());
  EXPECT_NE(shown.find(std::string("broken.cpgame: ") + tr(STR_GAMES_INSTALL_NO_MAIN)), std::string::npos) << shown;

  // Back dismisses the note and only the note: the list is still up and nothing left it.
  input->click(Button::Back);
  frame();
  EXPECT_EQ(activityManager.asks.goHome, 0);
  EXPECT_TRUE(activityManager.updateRequested());
  render();
  EXPECT_EQ(flat(ui().joined()).find("broken.cpgame"), std::string::npos);
  EXPECT_TRUE(ui().drewLine("Alpha"));

  // Then Back means Back.
  input->click(Button::Back);
  frame();
  EXPECT_EQ(activityManager.asks.goHome, 1);
}

TEST_F(ListTest, ConfirmAndATapAlsoDismissTheNoteWithoutOpeningARow) {
  addGame("alpha", "Alpha");
  installerscript::script().report.failed = 1;
  installerscript::script().report.firstError = Error::BadCrc;
  open();
  ASSERT_NE(flat(ui().joined()).find(tr(STR_GAMES_INSTALL_BAD_CRC)), std::string::npos) << "the note is up";
  input->click(Button::Confirm);
  frame();
  EXPECT_TRUE(activityManager.replacements.empty())
      << "the Confirm that dismisses the note must not open the game under it";
  render();
  EXPECT_EQ(flat(ui().joined()).find(tr(STR_GAMES_INSTALL_BAD_CRC)), std::string::npos);

  reopen();  // the same failure, a new visit: shown again, once
  ASSERT_NE(flat(ui().joined()).find(tr(STR_GAMES_INSTALL_BAD_CRC)), std::string::npos);
  tapRow("Alpha");  // a tap on the row under the note dismisses the note; it does not open the row
  EXPECT_TRUE(activityManager.replacements.empty());
  render();
  EXPECT_EQ(flat(ui().joined()).find(tr(STR_GAMES_INSTALL_BAD_CRC)), std::string::npos);
}

TEST_F(ListTest, AFailureWithNoFileNameShowsTheReasonAlone) {
  installerscript::script().report.failed = 1;
  installerscript::script().report.firstError = Error::SdCard;
  open();
  const std::string shown = flat(ui().joined());
  EXPECT_NE(shown.find(tr(STR_GAMES_INSTALL_STORAGE)), std::string::npos) << shown;
  EXPECT_EQ(shown.find(": "), std::string::npos) << "no file name, so no \"name: \" before the reason";
}

TEST_F(ListTest, NoFailureShowsNoNote) {
  installerscript::script().report.installed = 1;
  addGame("alpha", "Alpha");
  open();
  EXPECT_EQ(ui().drawn.size(), 1u) << ui().joined();  // just the one row
}

// Every Error the installer can report reads as its own words (reasonText): a swapped label would
// tell a person the wrong thing to fix. The table is written out here, not derived from the source.
TEST_F(ListTest, EveryInstallErrorMapsToItsOwnReason) {
  struct Case {
    Error error;
    const char* key;  // for the failure message
    std::string text;
  };
  const std::vector<Case> cases = {
      {Error::SdCard, "STR_GAMES_INSTALL_STORAGE", tr(STR_GAMES_INSTALL_STORAGE)},
      {Error::OutOfMemory, "STR_GAMES_OUT_OF_MEMORY", tr(STR_GAMES_OUT_OF_MEMORY)},
      {Error::NotAPackage, "STR_GAMES_INSTALL_NOT_A_PACKAGE", tr(STR_GAMES_INSTALL_NOT_A_PACKAGE)},
      {Error::BadManifest, "STR_GAMES_INSTALL_BAD_MANIFEST", tr(STR_GAMES_INSTALL_BAD_MANIFEST)},
      {Error::BadMember, "STR_GAMES_INSTALL_BAD_MEMBER", tr(STR_GAMES_INSTALL_BAD_MEMBER)},
      {Error::NoMain, "STR_GAMES_INSTALL_NO_MAIN", tr(STR_GAMES_INSTALL_NO_MAIN)},
      {Error::TooManyMembers, "STR_GAMES_INSTALL_TOO_MANY", tr(STR_GAMES_INSTALL_TOO_MANY)},
      {Error::BadImage, "STR_GAMES_BAD_IMAGE", tr(STR_GAMES_BAD_IMAGE)},
      {Error::PackageTooBig, "STR_GAMES_INSTALL_PACKAGE_TOO_BIG", tr(STR_GAMES_INSTALL_PACKAGE_TOO_BIG)},
      {Error::MemberTooBig, "STR_GAMES_INSTALL_MEMBER_TOO_BIG", tr(STR_GAMES_INSTALL_MEMBER_TOO_BIG)},
      {Error::ImagesTooBig, "STR_GAMES_INSTALL_IMAGES_TOO_BIG", tr(STR_GAMES_INSTALL_IMAGES_TOO_BIG)},
      {Error::BadSize, "STR_GAMES_INSTALL_BAD_SIZE", tr(STR_GAMES_INSTALL_BAD_SIZE)},
      {Error::BadCrc, "STR_GAMES_INSTALL_BAD_CRC", tr(STR_GAMES_INSTALL_BAD_CRC)},
      {Error::BinaryLua, "STR_GAMES_INSTALL_BINARY_LUA", tr(STR_GAMES_INSTALL_BINARY_LUA)},
      {Error::Unsupported, "STR_GAMES_INSTALL_UNSUPPORTED", tr(STR_GAMES_INSTALL_UNSUPPORTED)},
      {Error::BadDirectory, "STR_GAMES_INSTALL_BAD_LIST", tr(STR_GAMES_INSTALL_BAD_LIST)},
      {Error::SourcesTooBig, "STR_GAMES_SOURCES_TOO_LARGE", tr(STR_GAMES_SOURCES_TOO_LARGE)},
      {Error::UnknownIcon, "STR_GAMES_INSTALL_UNKNOWN_ICON", tr(STR_GAMES_INSTALL_UNKNOWN_ICON)},
  };
  // Every value but None has a case. A value added anywhere is a compile error in listed() below (a switch with
  // no default, and -Werror=switch on this target), so it cannot slip past the table.
  ASSERT_EQ(cases.size(), 18u);
  for (const Case& c : cases) EXPECT_TRUE(listed(c.error)) << c.key;

  // Two reasons that read alike would let a swap through.
  for (size_t i = 0; i < cases.size(); ++i)
    for (size_t j = i + 1; j < cases.size(); ++j)
      EXPECT_NE(cases[i].text, cases[j].text) << cases[i].key << " and " << cases[j].key << " read the same";

  for (const Case& c : cases) {
    SCOPED_TRACE(c.key);
    installerscript::reset();
    installerscript::script().report.failed = 1;
    installerscript::script().report.firstError = c.error;
    std::snprintf(installerscript::script().report.firstFile, sizeof(installerscript::script().report.firstFile),
                  "a.cpgame");
    if (list) {
      reopen();
    } else {
      open();
    }
    const std::string shown = flat(ui().joined());
    EXPECT_NE(shown.find("a.cpgame: " + c.text), std::string::npos) << "shown: " << shown;
    for (const Case& other : cases) {
      if (&other == &c) continue;
      EXPECT_EQ(shown.find(other.text), std::string::npos) << "it also says " << other.key << ": " << shown;
    }
  }
}

}  // namespace
