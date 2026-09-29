#include <GameIcons.h>
#include <I18n.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "GameRowIcon.h"
#include "HostCapsScript.h"
#include "InstallerScript.h"
#include "MatchSupport.h"
#include "activities/games/GameMatchActivity.h"
#include "activities/games/GamesLauncherActivity.h"
#include "util/ButtonNavigator.h"

// The real GamesLauncherActivity, GameRegistry, GameRowIcon, UiListActivity, and FreeInkUI over the
// screen doubles (screen_stubs/), the installer scripted (list_stubs/InstallerScript.h). Entry 5 of
// epic-install-and-launcher built it for the minimal Games list; entry 8 made it the launcher's. Entries
// 9 to 12 copy this file for the mode picker and the installer screens: the fixture below opens a screen,
// draws it, and finds a row by the text it drew. Each test names the deferred-work item it pins.

// A nothrow array allocation of exactly this many bytes fails while it is non-zero: how a test makes the launcher's
// makeUniqueNoThrow<T[]> return null (test/font_cache_manager does the same to count allocations). Every other
// allocation is malloc, as the default operator's, so nothing else changes.
namespace oom {
std::size_t failSize = 0;
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
  if (oom::failSize != 0 && size == oom::failSize) return nullptr;
  return std::malloc(size);
}

namespace {

// Reads the protected name an Activity was constructed with.
struct NameOf : Activity {
  static const std::string& of(const Activity& activity) { return activity.*(&NameOf::name); }
};

// A library icon's rows decoded without GameRowIcon or GameIconBlit: the packed bitmap's PackBits, from the top.
std::vector<uint8_t> libraryRows(const char* name, const bool fill) {
  const GameIcons::PackedBitmap bitmap =
      GameIcons::ICONS[GameIcons::find(name, std::strlen(name))].medium[fill ? 1 : 0];
  const size_t length = bitmap.data[0] | (static_cast<size_t>(bitmap.data[1]) << 8);
  std::vector<uint8_t> rows;
  for (size_t pos = 2; pos < 2 + length;) {
    const uint8_t control = bitmap.data[pos++];
    if (control < 128) {
      for (int i = 0; i <= control; ++i) rows.push_back(bitmap.data[pos++]);
    } else {
      const uint8_t value = bitmap.data[pos++];
      for (int i = 0; i < 257 - control; ++i) rows.push_back(value);
    }
  }
  return rows;
}

using Button = MappedInputManager::Button;
using GamePackageInstaller::Error;

const std::string PKG = "v1\n0530a15766e91bf1\n";

// `extra` is more members of the manifest object, each with its leading comma (`,"icon":"dice-six"`).
std::string manifestJson(const std::string& id, const std::string& name, const std::string& modes, const int api,
                         const int seatsMin, const int seatsMax, const std::string& extra = "") {
  return "{\"id\":\"" + id + "\",\"name\":\"" + name + "\",\"version\":\"1.0.0\",\"api\":" + std::to_string(api) +
         ",\"seats\":{\"min\":" + std::to_string(seatsMin) + ",\"max\":" + std::to_string(seatsMax) + "},\"modes\":[" +
         modes + "]" + extra + "}";
}

// A 64 x 64 icon.bmp as the installer writes it, its pixels a pattern that differs by seed.
harness::Bytes iconBmp(const int seed) {
  return harness::bmpFile(64, 64, [seed](const int x, const int y) { return harness::speckle(x, y, seed); });
}

// A message that wrapped over lines, read as one line of words.
std::string flat(std::string text) {
  for (char& c : text)
    if (c == '\n') c = ' ';
  return text;
}

// What a person should be told for each installer Error: written out here, not derived from the source. A switch
// over the whole enum with no default and -Werror=switch on this target (games_launcher.cmake): a new value does not
// compile until it has a case, and the test below then shows the screen every value with a text is given, so a
// value reasonText does not handle fails too. Copies of this test (entries 9 to 12) must keep both.
// Null: no text (None is not a failure, and the values past the last are none).
const char* expectedText(const Error error) {
  switch (error) {
    case Error::None:
      return nullptr;
    case Error::SdCard:
      return tr(STR_GAMES_INSTALL_STORAGE);
    case Error::OutOfMemory:
      return tr(STR_GAMES_OUT_OF_MEMORY);
    case Error::NotAPackage:
      return tr(STR_GAMES_INSTALL_NOT_A_PACKAGE);
    case Error::BadManifest:
      return tr(STR_GAMES_INSTALL_BAD_MANIFEST);
    case Error::BadMember:
      return tr(STR_GAMES_INSTALL_BAD_MEMBER);
    case Error::NoMain:
      return tr(STR_GAMES_INSTALL_NO_MAIN);
    case Error::TooManyMembers:
      return tr(STR_GAMES_INSTALL_TOO_MANY);
    case Error::BadImage:
      return tr(STR_GAMES_BAD_IMAGE);
    case Error::PackageTooBig:
      return tr(STR_GAMES_INSTALL_PACKAGE_TOO_BIG);
    case Error::MemberTooBig:
      return tr(STR_GAMES_INSTALL_MEMBER_TOO_BIG);
    case Error::ImagesTooBig:
      return tr(STR_GAMES_INSTALL_IMAGES_TOO_BIG);
    case Error::BadSize:
      return tr(STR_GAMES_INSTALL_BAD_SIZE);
    case Error::BadCrc:
      return tr(STR_GAMES_INSTALL_BAD_CRC);
    case Error::BinaryLua:
      return tr(STR_GAMES_INSTALL_BINARY_LUA);
    case Error::Unsupported:
      return tr(STR_GAMES_INSTALL_UNSUPPORTED);
    case Error::BadDirectory:
      return tr(STR_GAMES_INSTALL_BAD_LIST);
    case Error::SourcesTooBig:
      return tr(STR_GAMES_SOURCES_TOO_LARGE);
    case Error::UnknownIcon:
      return tr(STR_GAMES_INSTALL_UNKNOWN_ICON);
    case Error::TooManyGames:
      return tr(STR_GAMES_INSTALL_TOO_MANY_GAMES);
  }
  return nullptr;
}

class ListTest : public match::ScreenTest {
 protected:
  void SetUp() override {
    ScreenTest::SetUp();
    installerscript::reset();
    hostcaps::reset();
    GamesLauncherActivity::forgetOpenedGame();  // the launcher remembers the game it opened: each test starts fresh
    ButtonNavigator::setMappedInputManager(*input);
  }

  void TearDown() override {
    oom::failSize = 0;
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

  // An installed game whose manifest names a library icon (and, when `weight` is not empty, its weight).
  static void addIconGame(const std::string& id, const std::string& name, const std::string& icon,
                          const std::string& weight = "") {
    std::string extra = ",\"icon\":\"" + icon + "\"";
    if (!weight.empty()) extra += ",\"icon_weight\":\"" + weight + "\"";
    const std::string dir = "/.games/" + id;
    fakesd::addFile(dir + "/manifest.json", manifestJson(id, name, "\"solo\"", 1, 1, 1, extra));
    fakesd::addFile(dir + "/main.lua", std::string("return {}\n"));
    fakesd::addFile(dir + "/.pkg", PKG);
  }
  static void addIconFile(const std::string& id, const harness::Bytes& file) {
    fakesd::addFile("/.games/" + id + "/icon.bmp", file);
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
    list = std::make_unique<GamesLauncherActivity>(*renderer, *input);
    activity().onEnter();
    render();
  }
  // A second visit: the list goes, the manager's record and the theme's start over, and the list opens again.
  // The script (InstallerScript.h) and the log are the test's to change before it calls this.
  void reopen() {
    match::letStartedMatchesGo([this] { dropMatch(); }, match::Saves::Discard);
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

  std::unique_ptr<GamesLauncherActivity> list;
  Activity* entered = nullptr;  // the replacement enterReplacement() started, which needs its onExit
};

// ---- opening: the inbox first, then the installed games (## 4.4: GamesLauncherActivity's filters) ----

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

// ---- a game this host cannot start (R7): listed, with its reason, and never started ----

// The line drawn right after `label`: a row draws its name and then, if it has one, its reason under it.
std::string lineAfter(const screen::RecordingTarget& target, const std::string& label) {
  for (size_t i = 0; i < target.drawn.size(); ++i)
    if (target.drawn[i].text == label) return i + 1 < target.drawn.size() ? target.drawn[i + 1].text : "";
  return "<no such row>";
}

// ## 4.4 (the host-caps item): what Manifest::check says about a game is on its row; it is not left off the list.
TEST_F(ListTest, AGameThisHostCannotStartIsListedWithItsOwnReasonAndDoesNotStart) {
  addGame("solo-game", "SoloGame");
  addGame("too-new", "TooNew", "\"solo\"", 2);                     // api 2 on an api 1 host
  addGame("pass-only", "PassOnly", "\"pass\"", 1, 2, 2);           // no pass capability on this host
  addGame("too-many-seats", "TooManySeats", "\"pass\"", 1, 3, 4);  // more seats than the host has
  addGame("bad-solo", "BadSolo", "\"solo\"", 1, 2, 2);             // solo needs one seat: the manifest breaks its rules
  open();
  const std::vector<std::string> all{"SoloGame", "TooNew", "PassOnly", "TooManySeats", "BadSolo"};
  const std::vector<std::string> byName{"BadSolo", "PassOnly", "SoloGame", "TooManySeats", "TooNew"};
  ASSERT_EQ(rows(all), byName) << "every registry game is a row, in the registry's order";
  // Each reason is under its own game, and the game that can start has none: the next line is the next name.
  EXPECT_EQ(lineAfter(ui(), "TooNew"), tr(STR_GAMES_UNAVAILABLE_NEWER));
  EXPECT_EQ(lineAfter(ui(), "TooManySeats"), tr(STR_GAMES_UNAVAILABLE_SEATS));
  EXPECT_EQ(lineAfter(ui(), "PassOnly"), tr(STR_GAMES_UNAVAILABLE_MODE));
  EXPECT_EQ(lineAfter(ui(), "BadSolo"), tr(STR_GAMES_UNAVAILABLE_INVALID));
  EXPECT_EQ(lineAfter(ui(), "SoloGame"), "TooManySeats");
  // The reason is logged for each one, from the check's own verdict.
  EXPECT_TRUE(logHas("Unavailable too-new: "));
  EXPECT_TRUE(logHas("Unavailable pass-only: "));
  EXPECT_TRUE(logHas("Unavailable too-many-seats: "));
  EXPECT_FALSE(logHas("Unavailable solo-game"));
}

TEST_F(ListTest, AGameWrittenForAnOlderApiThanTheHostKeepsSaysSo) {
  hostcaps::script().minApi = 2;  // this host has dropped api 1
  addGame("old-game", "OldGame");
  addGame("new-game", "NewGame", "\"solo\"", 2);
  open();
  EXPECT_EQ(lineAfter(ui(), "OldGame"), tr(STR_GAMES_UNAVAILABLE_OLDER));
  EXPECT_TRUE(logHas("Unavailable old-game: "));
  tapRow("OldGame");
  EXPECT_TRUE(activityManager.replacements.empty());
}

TEST_F(ListTest, AnUnavailableRowIsNotStartedByATapOrByConfirm) {
  addGame("alpha", "Alpha");
  addGame("too-new", "TooNew", "\"solo\"", 2);
  open();
  tapRow("TooNew");
  EXPECT_TRUE(activityManager.replacements.empty());
  EXPECT_EQ(activityManager.asks.replaced, 0);
  EXPECT_TRUE(logHas("Not starting too-new: "));

  // The tap moved the selection to TooNew; Confirm acts on it.
  input->click(Button::Confirm);
  frame();
  EXPECT_TRUE(activityManager.replacements.empty());
  EXPECT_EQ(activityManager.asks.replaced, 0);
  EXPECT_EQ(activityManager.asks.goHome, 0) << "the row's own screen stays up";

  // The available row beside it still opens.
  tapRow("Alpha");
  ASSERT_EQ(activityManager.replacements.size(), 1u);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started alpha"));
}

// ## 4.8 (the mode-step item), resolved by entry 9: a game whose check is Ok has a mode to start, so with `pass` on a
// pass-only game is listed without a reason and opens (straight to the match: one mode), and a game that offers solo
// and pass opens the mode picker. ModePickerTest.cpp tests the picker itself.
TEST_F(ListTest, AGameOnlyAnotherModeCanStartOpensItsMatchAndAGameWithTwoModesOpensThePicker) {
  hostcaps::script().pass = true;
  addGame("pass-only", "PassOnly", "\"pass\"", 1, 2, 2);
  addGame("solo-and-pass", "SoloAndPass", "\"solo\",\"pass\"", 1, 1, 2);
  open();
  const std::vector<std::string> expected{"PassOnly", "SoloAndPass"};
  EXPECT_EQ(rows({"PassOnly", "SoloAndPass"}), expected);
  EXPECT_EQ(lineAfter(ui(), "PassOnly"), "SoloAndPass") << "a game the host can start has no reason under it";
  EXPECT_EQ(lineAfter(ui(), "SoloAndPass"), "");
  tapRow("PassOnly");
  ASSERT_EQ(activityManager.replacements.size(), 1u) << "one mode: the match, without a picker";
  EXPECT_EQ(activityManager.asks.pushed, 0);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started pass-only"));
  reopen();
  tapRow("SoloAndPass");
  EXPECT_TRUE(activityManager.replacements.empty()) << "two modes: the picker first";
  EXPECT_EQ(activityManager.asks.pushed, 1);
  EXPECT_FALSE(logHas("Started solo-and-pass"));
}

// ---- paging (R7): the list pages past one screen ----

TEST_F(ListTest, ManyGamesPageAndEveryDrawnRowDrawsOneIcon) {
  std::vector<std::string> names;
  for (int i = 1; i <= 25; ++i) {
    char name[16];
    std::snprintf(name, sizeof(name), "Game %02d", i);
    names.push_back(name);
    char id[16];
    std::snprintf(id, sizeof(id), "game-%02d", i);
    addGame(id, name);
  }
  open();
  std::vector<std::string> page = rows(names);
  ASSERT_FALSE(page.empty());
  EXPECT_EQ(page.front(), "Game 01");
  ASSERT_LT(page.size(), names.size()) << "25 games must not fit one screen";
  EXPECT_FALSE(ui().drewLine("Game 25"));
  EXPECT_EQ(ui().bitmaps, static_cast<int>(page.size())) << "one icon for each row drawn";

  // A swipe up brings the next rows; enough of them reach the last game.
  const std::string firstPageLast = page.back();
  input->swipeDirection(MappedInputManager::SwipeDir::Up);
  frame();
  render();
  page = rows(names);
  ASSERT_FALSE(page.empty());
  EXPECT_FALSE(ui().drewLine("Game 01"));
  size_t firstPageLastIndex = 0;
  while (names[firstPageLastIndex] != firstPageLast) ++firstPageLastIndex;
  EXPECT_EQ(page.front(), names[firstPageLastIndex + 1]) << "the second page starts where the first ended";
  EXPECT_EQ(ui().bitmaps, static_cast<int>(page.size()));
  for (int swipes = 0; swipes < 10 && !ui().drewLine("Game 25"); ++swipes) {
    input->swipeDirection(MappedInputManager::SwipeDir::Up);
    frame();
    render();
  }
  EXPECT_TRUE(ui().drewLine("Game 25"));
  // The rows on the last page open the game they name.
  tapRow("Game 25");
  ASSERT_EQ(activityManager.replacements.size(), 1u);
  EXPECT_TRUE(activityManager.replacements.back() != nullptr);
}

TEST_F(ListTest, PageKeysMoveTheSelectionPastTheFirstScreen) {
  for (int i = 1; i <= 25; ++i) {
    char id[16];
    std::snprintf(id, sizeof(id), "game-%02d", i);
    char name[16];
    std::snprintf(name, sizeof(name), "Game %02d", i);
    addGame(id, name);
  }
  open();
  for (int step = 0; step < 24; ++step) {
    input->click(Button::NavNext);
    frame();
  }
  render();
  EXPECT_TRUE(ui().drewLine("Game 25")) << "the last row is drawn once the selection reaches it";
  EXPECT_FALSE(ui().drewLine("Game 01"));
  input->click(Button::Confirm);
  frame();
  ASSERT_EQ(activityManager.replacements.size(), 1u);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started game-25"));
}

// Retro deferral 4.10: the held-button paths. A button held past ButtonNavigator's start (500 ms) steps by a whole page
// every interval, and the release that ends the hold is not one more step.
TEST_F(ListTest, ANextKeyHeldPagesTheListAndItsReleaseIsNotOneStepMore) {
  std::vector<std::string> names;
  for (int i = 1; i <= 25; ++i) {
    char id[16];
    std::snprintf(id, sizeof(id), "game-%02d", i);
    char name[16];
    std::snprintf(name, sizeof(name), "Game %02d", i);
    names.push_back(name);
    addGame(id, name);
  }
  open();
  const size_t page = rows(names).size();
  ASSERT_GT(page, 1u);
  ASSERT_LT(page, names.size());
  // Held for less than the start delay: nothing moves.
  fakertos::advance(2000);
  input->hold(Button::NavNext, 400);
  frame();
  render();
  EXPECT_EQ(rows(names).front(), "Game 01");
  // Held past it: a whole page down, and again only after the interval.
  input->hold(Button::NavNext, 600);
  frame();
  render();
  ASSERT_EQ(rows(names).front(), names[page]) << "one page down, to its first row";
  input->hold(Button::NavNext, 700);
  frame();
  render();
  EXPECT_EQ(rows(names).front(), names[page]) << "the interval has not passed";
  fakertos::advance(600);
  input->hold(Button::NavNext, 1300);
  frame();
  render();
  EXPECT_EQ(rows(names).front(), names[std::min(names.size() - 1, 2 * page)]) << "a second page down";
  // Let go: the release that ends a hold is not a single step.
  input->release(Button::NavNext);
  frame();
  render();
  EXPECT_EQ(rows(names).front(), names[std::min(names.size() - 1, 2 * page)]);
  // Held until the last page, the key wraps to the first, and the selection is always a game.
  for (int step = 0; step < 10; ++step) {
    fakertos::advance(600);
    input->hold(Button::NavNext, 600);
    frame();
    render();
    if (rows(names).front() == "Game 01") break;
  }
  EXPECT_EQ(rows(names).front(), "Game 01");
  input->release(Button::NavNext);
  frame();
  input->click(Button::Confirm);
  frame();
  ASSERT_EQ(activityManager.replacements.size(), 1u);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started game-01"));
}

// A list of two games: a held key steps through the games alone, wrapping from the last game to the first, and never
// rests on a row past them, where Confirm would open nothing. (The launcher pads a list with blank rows to a whole
// page; the harness draws as many rows as the page holds, so the padded count and the game count page alike here.)
TEST_F(ListTest, AKeyHeldOverAShortListWrapsWithinTheGames) {
  addGame("game-01", "Game 01");
  addGame("game-02", "Game 02");
  open();
  // Two held steps go round the two games. Each is drawn before the next: a step moves the selection as the list is
  // built, and a second step read before that would start from the old one.
  for (int step = 0; step < 2; ++step) {
    fakertos::advance(600);
    input->hold(Button::NavNext, 600);
    frame();
    render();
  }
  input->release(Button::NavNext);
  frame();
  input->click(Button::Confirm);
  frame();
  ASSERT_EQ(activityManager.replacements.size(), 1u) << "Confirm opened nothing: the selection was on a blank row";
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started game-01"));
}

TEST_F(ListTest, APreviousKeyHeldFromTheFirstGameWrapsToTheLastGame) {
  addGame("game-01", "Game 01");
  addGame("game-02", "Game 02");
  open();
  fakertos::advance(600);
  input->hold(Button::NavPrevious, 600);
  frame();
  render();
  input->release(Button::NavPrevious);
  frame();
  input->click(Button::Confirm);
  frame();
  ASSERT_EQ(activityManager.replacements.size(), 1u) << "Confirm opened nothing: the selection was on a blank row";
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started game-02"));
}

// ---- the row icon (R7, AD-24): icon.bmp, else the manifest icon in its weight, else game-controller ----

TEST_F(ListTest, ARowsIconComesFromIconBmpElseTheManifestIconElseGameController) {
  addIconGame("a-both", "A Both", "dice-six", "fill");  // icon.bmp wins over the manifest's icon
  addIconFile("a-both", iconBmp(1));
  addIconGame("b-fill", "B Fill", "dice-six", "fill");
  addIconGame("c-regular", "C Regular", "dice-six", "regular");
  addIconGame("d-default", "D Default", "dice-six");  // no icon_weight: regular
  addGame("e-none", "E None");
  addIconGame("f-unknown", "F Unknown", "not-in-the-library");  // hand-copied: the installer would refuse it
  open();
  EXPECT_TRUE(logHas("Icon for a-both: icon.bmp"));
  EXPECT_TRUE(logHas("Icon for b-fill: library dice-six fill"));
  EXPECT_TRUE(logHas("Icon for c-regular: library dice-six regular"));
  EXPECT_TRUE(logHas("Icon for d-default: library dice-six regular"));
  EXPECT_TRUE(logHas("Icon for e-none: fallback game-controller"));
  EXPECT_TRUE(logHas("Icon for f-unknown: fallback game-controller"));
  // Every row drawn has its icon, whichever the source.
  const std::vector<std::string> names{"A Both", "B Fill", "C Regular", "D Default", "E None", "F Unknown"};
  EXPECT_EQ(ui().bitmaps, static_cast<int>(rows(names).size()));
}

TEST_F(ListTest, AnIconBmpThatIsNotUsableFallsBackToTheNextSource) {
  harness::Bytes truncated = iconBmp(2);
  truncated.resize(80);
  addIconGame("g-truncated", "G Truncated", "boat");
  addIconFile("g-truncated", truncated);
  addIconGame("h-small", "H Small", "boat", "fill");  // a valid image, but 32 x 32
  addIconFile("h-small", harness::bmpFile(32, 32, [](int x, int y) { return harness::speckle(x, y); }));
  addGame("i-small-none", "I Small None");
  addIconFile("i-small-none", harness::bmpFile(64, 32, [](int x, int y) { return harness::speckle(x, y); }));
  open();
  EXPECT_TRUE(logHas("Icon for g-truncated: library boat regular"));
  EXPECT_TRUE(logHas("Icon for h-small: library boat fill"));
  EXPECT_TRUE(logHas("Icon for i-small-none: fallback game-controller"));
  EXPECT_TRUE(logHas("/.games/g-truncated/icon.bmp is not a usable icon"));
  EXPECT_TRUE(logHas("/.games/h-small/icon.bmp is 32x32"));
}

// The bits handed to the list for each row (RecordingTarget::bitmapsDrawn, in draw order: one per row, top to bottom).
TEST_F(ListTest, EachRowDrawsItsOwnBitsAs64x64Mask1) {
  addIconGame("a-first", "A First", "dice-six", "fill");  // icon.bmp wins, seed 1
  addIconFile("a-first", iconBmp(1));
  addIconGame("b-second", "B Second", "boat");  // icon.bmp wins, a different seed
  addIconFile("b-second", iconBmp(2));
  addIconGame("c-fill", "C Fill", "dice-six", "fill");
  addIconGame("d-regular", "D Regular", "dice-six");
  addGame("e-fallback", "E Fallback");
  addIconGame("f-unknown", "F Unknown", "not-in-the-library");
  open();
  const std::vector<std::string> names{"A First", "B Second", "C Fill", "D Regular", "E Fallback", "F Unknown"};
  ASSERT_EQ(rows(names), names) << "all six rows fit the page";
  const auto& drawn = ui().bitmapsDrawn;
  ASSERT_EQ(drawn.size(), names.size());
  for (const screen::DrawnBitmap& bitmap : drawn) {
    EXPECT_EQ(bitmap.format, freeink::ui::BitmapFormat::Mask1);
    EXPECT_EQ(bitmap.width, 64);
    EXPECT_EQ(bitmap.height, 64);
  }
  EXPECT_EQ(drawn[0].data, harness::rowsOf(iconBmp(1)));
  EXPECT_EQ(drawn[1].data, harness::rowsOf(iconBmp(2)));
  EXPECT_NE(drawn[0].data, drawn[1].data);
  EXPECT_EQ(drawn[2].data, libraryRows("dice-six", true));
  EXPECT_EQ(drawn[3].data, libraryRows("dice-six", false));
  EXPECT_NE(drawn[2].data, drawn[3].data);
  EXPECT_EQ(drawn[4].data, libraryRows("game-controller", false));
  EXPECT_EQ(drawn[5].data, libraryRows("game-controller", false));
}

// The two allocations loadIcons makes: when either fails the rows are still listed, each with its library icon.
TEST_F(ListTest, WhenTheIconCacheCannotBeAllocatedTheRowsUseLibraryIcons) {
  addIconGame("a-first", "A First", "dice-six", "fill");
  addIconFile("a-first", iconBmp(1));
  addIconGame("b-second", "B Second", "boat");
  addIconFile("b-second", iconBmp(2));
  oom::failSize = 2 * GameRowIcon::BYTES;
  open();
  EXPECT_TRUE(logHas("OOM: 1024 B of package icons"));
  const std::vector<std::string> names{"A First", "B Second"};
  EXPECT_EQ(rows(names), names);
  ASSERT_EQ(ui().bitmapsDrawn.size(), 2u);
  EXPECT_EQ(ui().bitmapsDrawn[0].data, libraryRows("dice-six", true));
  EXPECT_EQ(ui().bitmapsDrawn[1].data, libraryRows("boat", false));
  EXPECT_TRUE(logHas("Icon for a-first: library dice-six fill"));
  tapRow("B Second");
  EXPECT_EQ(activityManager.replacements.size(), 1u) << "the row still opens";
}

TEST_F(ListTest, WhenTheSlotArrayCannotBeAllocatedTheRowsUseLibraryIcons) {
  addIconGame("a-first", "A First", "dice-six");
  addIconFile("a-first", iconBmp(1));
  addGame("b-second", "B Second");
  oom::failSize = 2 * sizeof(int16_t);
  open();
  EXPECT_TRUE(logHas("OOM: 4 icon slots"));
  const std::vector<std::string> names{"A First", "B Second"};
  EXPECT_EQ(rows(names), names);
  ASSERT_EQ(ui().bitmapsDrawn.size(), 2u);
  EXPECT_EQ(ui().bitmapsDrawn[0].data, libraryRows("dice-six", false));
  EXPECT_EQ(ui().bitmapsDrawn[1].data, libraryRows("game-controller", false));
}

// ActivityManager::goHome selects Home's Games row by this name (ledger row 5): the screen is named by the constant.
TEST_F(ListTest, TheLauncherIsNamedByTheConstantGoHomeMapsToTheGamesRow) {
  open();
  EXPECT_EQ(NameOf::of(*list), std::string(GamesLauncherActivity::NAME));
}

TEST_F(ListTest, IconBmpIsReadOncePerVisitNotOncePerFrame) {
  addGame("alpha", "Alpha");
  addIconFile("alpha", iconBmp(3));
  open();
  render();
  render();
  frame();
  render();
  EXPECT_EQ(fakesd::countOps("open /.games/alpha/icon.bmp"), 1u);
  reopen();
  EXPECT_EQ(fakesd::countOps("open /.games/alpha/icon.bmp"), 2u) << "a new visit reads it again";
  EXPECT_EQ(ui().bitmaps, 1);
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

  // Confirm with nothing moved opens the first row (of a launcher that has no game to return to).
  fakelog::clearLines();
  GamesLauncherActivity::forgetOpenedGame();
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
  // Every value a uint8_t can hold, so an Error added to the enum is met whatever its number.
  std::vector<int> withText;
  for (int value = 0; value < 256; ++value)
    if (expectedText(static_cast<Error>(value))) withText.push_back(value);
  ASSERT_GE(withText.size(), 19u) << "the nineteen reasons that exist today";

  // Two reasons that read alike would let a swap through.
  for (size_t i = 0; i < withText.size(); ++i)
    for (size_t j = i + 1; j < withText.size(); ++j)
      EXPECT_STRNE(expectedText(static_cast<Error>(withText[i])), expectedText(static_cast<Error>(withText[j])))
          << "Error " << withText[i] << " and " << withText[j] << " read the same";

  for (const int value : withText) {
    SCOPED_TRACE("Error " + std::to_string(value));
    const std::string text = expectedText(static_cast<Error>(value));
    installerscript::reset();
    installerscript::script().report.failed = 1;
    installerscript::script().report.firstError = static_cast<Error>(value);
    std::snprintf(installerscript::script().report.firstFile, sizeof(installerscript::script().report.firstFile),
                  "a.cpgame");
    if (list) {
      reopen();
    } else {
      open();
    }
    const std::string shown = flat(ui().joined());
    EXPECT_NE(shown.find("a.cpgame: " + text), std::string::npos) << "shown: " << shown;
    for (const int other : withText) {
      if (other == value) continue;
      EXPECT_EQ(shown.find(expectedText(static_cast<Error>(other))), std::string::npos)
          << "it also says Error " << other << "'s: " << shown;
    }
  }
}

}  // namespace
