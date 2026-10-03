#include <GameIcons.h>
#include <I18n.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <vector>

#include "GameMarkBitmaps.h"
#include "GamePaths.h"
#include "GameRegistry.h"
#include "GameRowIcon.h"
#include "HostCapsScript.h"
#include "InstallerScript.h"
#include "LauncherSupport.h"
#include "MatchSupport.h"
#include "RemoveScript.h"
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

// The Crosshatch mark's 64 px rows, from the generated header directly (no GameRowIcon).
std::vector<uint8_t> markRows() {
  return std::vector<uint8_t>(std::begin(GameMark::ROW_64), std::end(GameMark::ROW_64));
}

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
    removescript::reset();
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

  // A match left in the manager's record goes as the manager would let it go. (Since entry 8 no case here starts one:
  // a row opens the game's title screen, which openedTitle lets go.)
  void dropMatch() {
    for (auto& replacement : activityManager.replacements) {
      if (replacement) activityManager.destroyHolding(replacement);
    }
    activityManager.replacements.clear();
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

  // The title screen the last row opened pushed, entered, drawn, and let go (LauncherSupport.h): its header, the game's
  // name.
  std::string openedTitle() { return launcher::openedTitle(*list); }

  std::unique_ptr<GamesLauncherActivity> list;
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
  EXPECT_EQ(activityManager.asks.pushed, 0);
}

// The registry sizes its Entry[] to the folders it found. Entry holds no member with a destructor, so new[] asks for
// exactly count * sizeof(Entry) (no array cookie), which is the size the two cases below make fail.
static_assert(std::is_trivially_destructible_v<GameRegistry::Entry>);

// A load that runs out of memory lists nothing, as an empty card does, but says why: "No games found" would tell a
// person whose games are on the card that they are gone. The registry logs the allocation that failed.
TEST_F(ListTest, ARegistryLoadThatRunsOutOfMemoryIsReportedNotShownAsNoGames) {
  addGame("alpha", "Alpha");
  addGame("bravo", "Bravo");
  oom::failSize = 2 * sizeof(GameRegistry::Entry);
  open();
  EXPECT_TRUE(logHas("OOM: manifest reader"));
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_OUT_OF_MEMORY))) << ui().joined();
  EXPECT_FALSE(ui().drewLine(tr(STR_GAMES_EMPTY)));
  EXPECT_FALSE(ui().drewLine("Alpha"));
  input->click(Button::Confirm);
  frame();
  input->tap(240, 300);
  frame();
  EXPECT_EQ(activityManager.asks.pushed, 0);
  EXPECT_EQ(activityManager.asks.replaced, 0);

  // With the memory back, the next visit lists the games again.
  oom::failSize = 0;
  reopen();
  EXPECT_TRUE(ui().drewLine("Alpha"));
  EXPECT_FALSE(ui().drewLine(tr(STR_GAMES_OUT_OF_MEMORY)));
}

// The listing is read again after a remove; a reload that runs out of memory is reported the same way.
TEST_F(ListTest, AReloadAfterARemoveThatRunsOutOfMemoryIsReported) {
  removescript::script().onRemove = [] {
    // What the real remove does to the card: the marker, then the folder.
    for (const char* file : {".pkg", "manifest.json", "main.lua"})
      fakesd::removeEntry(std::string("/.games/bravo/") + file);
    fakesd::removeEntry("/.games/bravo");
  };
  addGame("alpha", "Alpha");
  addGame("bravo", "Bravo");
  addGame("charlie", "Charlie");
  open();
  const screen::DrawnText* bravo = nullptr;
  for (const screen::DrawnText& drawn : ui().drawn)
    if (drawn.text == "Bravo") bravo = &drawn;
  ASSERT_NE(bravo, nullptr) << ui().joined();
  input->longPress(bravo->rect.x + bravo->rect.width / 2, bravo->rect.y + bravo->rect.height / 2);
  frame();
  render();
  oom::failSize = 2 * sizeof(GameRegistry::Entry);  // the two folders left after the remove
  tapRow(tr(STR_GAMES_REMOVE));
  EXPECT_EQ(removescript::script().ids, std::vector<std::string>{"bravo"});
  EXPECT_TRUE(logHas("OOM: manifest reader"));
  render();
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_OUT_OF_MEMORY))) << ui().joined();
  EXPECT_FALSE(ui().drewLine(tr(STR_GAMES_EMPTY)));
  EXPECT_FALSE(ui().drewLine("Alpha"));
  input->click(Button::Confirm);
  frame();
  input->tap(240, 300);
  frame();
  EXPECT_EQ(activityManager.asks.pushed, 0);
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
  EXPECT_EQ(openedTitle(), "Good");
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
  hostcaps::script().pass = false;  // a host without pass, so a pass-only game has no mode here
  addGame("solo-game", "SoloGame");
  addGame("too-new", "TooNew", "\"solo\"", 2);                     // api 2 on an api 1 host
  addGame("pass-only", "PassOnly", "\"pass\"", 1, 2, 2);           // no pass capability on this host
  addGame("too-many-seats", "TooManySeats", "\"pass\"", 1, 3, 4);  // more seats than the host has
  addGame("bad-solo", "BadSolo", "\"solo\"", 1, 2, 2);             // solo needs one seat: the manifest breaks its rules
  open();
  const std::vector<std::string> all{"SoloGame", "TooNew", "PassOnly", "TooManySeats", "BadSolo"};
  const std::vector<std::string> byName{"BadSolo", "PassOnly", "SoloGame", "TooManySeats", "TooNew"};
  ASSERT_EQ(rows(all), byName) << "every registry game is a row, in the registry's order";
  // Each reason is under its own game, in place of the modes line the game that can start has.
  EXPECT_EQ(lineAfter(ui(), "TooNew"), tr(STR_GAMES_UNAVAILABLE_NEWER));
  EXPECT_EQ(lineAfter(ui(), "TooManySeats"), tr(STR_GAMES_UNAVAILABLE_SEATS));
  EXPECT_EQ(lineAfter(ui(), "PassOnly"), tr(STR_GAMES_UNAVAILABLE_MODE));
  EXPECT_EQ(lineAfter(ui(), "BadSolo"), tr(STR_GAMES_UNAVAILABLE_INVALID));
  EXPECT_EQ(lineAfter(ui(), "SoloGame"), tr(STR_GAMES_MODE_SOLO));
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
  EXPECT_EQ(activityManager.asks.pushed, 0);
}

TEST_F(ListTest, AnUnavailableRowIsNotStartedByATapOrByConfirm) {
  addGame("alpha", "Alpha");
  addGame("too-new", "TooNew", "\"solo\"", 2);
  open();
  tapRow("TooNew");
  EXPECT_TRUE(activityManager.replacements.empty());
  EXPECT_EQ(activityManager.asks.replaced, 0);
  EXPECT_EQ(activityManager.asks.pushed, 0);
  EXPECT_TRUE(logHas("Not starting too-new: "));

  // The tap moved the selection to TooNew; Confirm acts on it.
  input->click(Button::Confirm);
  frame();
  EXPECT_TRUE(activityManager.replacements.empty());
  EXPECT_EQ(activityManager.asks.replaced, 0);
  EXPECT_EQ(activityManager.asks.pushed, 0);
  EXPECT_EQ(activityManager.asks.goHome, 0) << "the row's own screen stays up";

  // The available row beside it still opens.
  tapRow("Alpha");
  EXPECT_EQ(openedTitle(), "Alpha");
}

// ## 4.8 (the mode-step item), resolved by entry 9: a game whose check is Ok has a mode to start, so with `pass` on a
// pass-only game is listed without a reason. Since entry 7 of epic-pass-and-play every row of a game the host can
// start, one mode or two, opens the game's title screen, which ModePickerTest.cpp tests. Since entry 12 its second line
// names the modes this host can start.
TEST_F(ListTest, AGameOnlyAnotherModeCanStartHasNoReasonAndEveryStartableRowOpensItsTitleScreen) {
  hostcaps::script().pass = true;
  addGame("pass-only", "PassOnly", "\"pass\"", 1, 2, 2);
  addGame("solo-and-pass", "SoloAndPass", "\"solo\",\"pass\"", 1, 1, 2);
  open();
  const std::vector<std::string> expected{"PassOnly", "SoloAndPass"};
  EXPECT_EQ(rows({"PassOnly", "SoloAndPass"}), expected);
  EXPECT_EQ(lineAfter(ui(), "PassOnly"), tr(STR_GAMES_MODE_PASS)) << "its modes line, no reason";
  EXPECT_EQ(lineAfter(ui(), "SoloAndPass"),
            std::string(tr(STR_GAMES_MODE_SOLO)) + " \xC2\xB7 " + tr(STR_GAMES_MODE_PASS));
  tapRow("PassOnly");
  EXPECT_EQ(activityManager.asks.pushed, 1);
  EXPECT_EQ(openedTitle(), "PassOnly") << "one mode: its title screen";
  reopen();
  tapRow("SoloAndPass");
  EXPECT_EQ(activityManager.asks.pushed, 1);
  EXPECT_EQ(openedTitle(), "SoloAndPass") << "two modes: its title screen too";
  EXPECT_TRUE(activityManager.replacements.empty()) << "no match until a choice on the title screen";
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
  EXPECT_EQ(openedTitle(), "Game 25");
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
  EXPECT_EQ(openedTitle(), "Game 25");
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
  EXPECT_EQ(openedTitle(), "Game 01");
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
  EXPECT_EQ(openedTitle(), "Game 01") << "Confirm opened nothing: the selection was on a blank row";
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
  EXPECT_EQ(openedTitle(), "Game 02") << "Confirm opened nothing: the selection was on a blank row";
}

// ---- the row icon (R7, AD-24): icon.bmp, else the manifest icon in its weight, else the Crosshatch mark ----

TEST_F(ListTest, ARowsIconComesFromIconBmpElseTheManifestIconElseTheCrosshatchMark) {
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
  EXPECT_TRUE(logHas("Icon for e-none: fallback crosshatch mark"));
  EXPECT_TRUE(logHas("Icon for f-unknown: fallback crosshatch mark"));
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
  EXPECT_TRUE(logHas("Icon for i-small-none: fallback crosshatch mark"));
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
  EXPECT_EQ(drawn[4].data, markRows());
  EXPECT_EQ(drawn[5].data, markRows());
  EXPECT_NE(drawn[4].data, libraryRows("game-controller", false)) << "the controller is no longer the default";
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
  EXPECT_EQ(openedTitle(), "B Second") << "the row still opens";
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
  EXPECT_EQ(ui().bitmapsDrawn[1].data, markRows());
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

// ---- opening a game: a tap or Confirm pushes the game's title screen (## 4.4: the tracer item) ----

TEST_F(ListTest, ATapOnARowPushesThatGamesTitleScreen) {
  installFixtureWithPkg("tracer");
  installFixtureWithPkg("timer");
  open();
  ASSERT_TRUE(ui().drewLine("Timer"));
  ASSERT_TRUE(ui().drewLine("Tracer"));
  tapRow("Tracer");
  EXPECT_EQ(activityManager.asks.pushed, 1) << "pushed, so Back returns to the list as it was";
  EXPECT_EQ(activityManager.asks.replaced, 0) << "no match until a choice on the title screen";
  EXPECT_EQ(openedTitle(), "Tracer");
  EXPECT_EQ(activityManager.asks.goHome, 0);
  render();  // Back from the title screen: the list as it was
  EXPECT_TRUE(ui().drewLine("Timer"));
  EXPECT_TRUE(ui().drewLine("Tracer"));
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
  EXPECT_EQ(openedTitle(), "Timer");  // 0 -> 1 -> 2 -> 1

  // Confirm with nothing moved opens the first row (of a launcher that has no game to return to).
  GamesLauncherActivity::forgetOpenedGame();
  reopen();
  input->click(Button::Confirm);
  frame();
  EXPECT_EQ(openedTitle(), "Counter");
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
                "broken.chgame");
  open();
  const std::string shown = flat(ui().joined());
  EXPECT_NE(shown.find(std::string("broken.chgame: ") + tr(STR_GAMES_INSTALL_NO_MAIN)), std::string::npos) << shown;

  // Back dismisses the note and only the note: the list is still up and nothing left it.
  input->click(Button::Back);
  frame();
  EXPECT_EQ(activityManager.asks.goHome, 0);
  EXPECT_TRUE(activityManager.updateRequested());
  render();
  EXPECT_EQ(flat(ui().joined()).find("broken.chgame"), std::string::npos);
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
  EXPECT_EQ(activityManager.asks.pushed, 0);
  render();
  EXPECT_EQ(flat(ui().joined()).find(tr(STR_GAMES_INSTALL_BAD_CRC)), std::string::npos);

  reopen();  // the same failure, a new visit: shown again, once
  ASSERT_NE(flat(ui().joined()).find(tr(STR_GAMES_INSTALL_BAD_CRC)), std::string::npos);
  tapRow("Alpha");  // a tap on the row under the note dismisses the note; it does not open the row
  EXPECT_TRUE(activityManager.replacements.empty());
  EXPECT_EQ(activityManager.asks.pushed, 0);
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

// ---- "and N more" (A22): Report.failed counts every failure, the note names only the first ----

TEST_F(ListTest, OneFailureShowsNoMoreLine) {
  installerscript::script().report.failed = 1;
  installerscript::script().report.firstError = Error::NoMain;
  std::snprintf(installerscript::script().report.firstFile, sizeof(installerscript::script().report.firstFile),
                "broken.chgame");
  open();
  const std::string shown = flat(ui().joined());
  EXPECT_NE(shown.find("broken.chgame: " + std::string(tr(STR_GAMES_INSTALL_NO_MAIN))), std::string::npos) << shown;
  EXPECT_EQ(shown.find("more"), std::string::npos) << shown;
}

TEST_F(ListTest, TwoFailuresShowTheFirstReasonAndAndOneMoreOnItsOwnLine) {
  installerscript::script().report.failed = 2;
  installerscript::script().report.firstError = Error::BadCrc;
  std::snprintf(installerscript::script().report.firstFile, sizeof(installerscript::script().report.firstFile),
                "first.chgame");
  open();
  const std::string shown = flat(ui().joined());
  EXPECT_NE(shown.find(std::string("first.chgame: ") + tr(STR_GAMES_INSTALL_BAD_CRC) + " and 1 more"),
            std::string::npos)
      << shown;
  EXPECT_TRUE(ui().drewLine("and 1 more")) << "a line of its own: " << ui().joined();
  EXPECT_EQ(shown.find("and 2 more"), std::string::npos) << "N is the failures after the first: " << shown;
}

TEST_F(ListTest, TheMoreLineIsTheFailureCountLessTheFirstEvenWhenTheFirstWaitsForRoom) {
  // The installer counts a package that waits for room in `failed` (GamePackageInstallerTest pins it), so the note
  // does.
  installerscript::script().report.failed = 7;
  installerscript::script().report.firstError = Error::TooManyGames;
  open();
  const std::string shown = flat(ui().joined());
  EXPECT_NE(shown.find(std::string(tr(STR_GAMES_INSTALL_TOO_MANY_GAMES)) + " and 6 more"), std::string::npos) << shown;
  EXPECT_TRUE(ui().drewLine("and 6 more")) << ui().joined();
}

TEST_F(ListTest, ASaturatedFailureCountReadsAsItsFloorNotAWrappedNumber) {
  // Report.failed stops at 255 (uint8_t): the line says 254, and never 0, -1, or 255.
  installerscript::script().report.failed = std::numeric_limits<uint8_t>::max();
  installerscript::script().report.firstError = Error::SdCard;
  std::snprintf(installerscript::script().report.firstFile, sizeof(installerscript::script().report.firstFile), "%s",
                std::string(GamePaths::INBOX_NAME_BYTES - 1, 'n').c_str());  // the longest file name
  open();
  const std::string shown = flat(ui().joined());
  EXPECT_NE(shown.find(std::string(tr(STR_GAMES_INSTALL_STORAGE)) + " and 254 more"), std::string::npos) << shown;
  EXPECT_TRUE(ui().drewLine("and 254 more")) << ui().joined();
}

TEST_F(ListTest, TheLongestReasonWithTheLongestFileNameIsDrawnWholeAboveTheMoreLine) {
  // The longest file name with the longest reason still draws whole (note[128], four lines), and the more line follows.
  installerscript::script().report.failed = 12;
  std::string longest;
  for (int value = 0; value < 256; ++value) {
    const char* text = expectedText(static_cast<Error>(value));
    if (text && longest.size() < std::strlen(text)) {
      longest = text;
      installerscript::script().report.firstError = static_cast<Error>(value);
    }
  }
  std::snprintf(installerscript::script().report.firstFile, sizeof(installerscript::script().report.firstFile), "%s",
                std::string(GamePaths::INBOX_NAME_BYTES - 1, 'n').c_str());
  open();
  const std::string shown = flat(ui().joined());
  EXPECT_NE(shown.find(longest + " and 11 more"), std::string::npos) << shown;
  EXPECT_TRUE(ui().drewLine("and 11 more")) << ui().joined();
}

TEST_F(ListTest, TheMoreLineIsDrawnUnderTheReasonAndCenteredWithIt) {
  installerscript::script().report.failed = 2;
  installerscript::script().report.firstError = Error::BadCrc;
  std::snprintf(installerscript::script().report.firstFile, sizeof(installerscript::script().report.firstFile),
                "first.chgame");
  open();
  const auto& drawn = ui().drawn;
  size_t more = drawn.size();
  for (size_t i = 0; i < drawn.size(); ++i)
    if (drawn[i].text == "and 1 more") more = i;
  ASSERT_LT(more, drawn.size()) << ui().joined();
  ASSERT_GT(more, 0u);
  const screen::DrawnText& reason = drawn[more - 1];  // the reason's last line, drawn just before
  EXPECT_NE(reason.text.find("damaged"), std::string::npos) << ui().joined();
  EXPECT_EQ(drawn[more].rect.y, reason.rect.y + reason.rect.height) << "the more line starts where the reason ends";
  EXPECT_NEAR(drawn[more].rect.x + drawn[more].rect.width / 2.0, reason.rect.x + reason.rect.width / 2.0, 1.0)
      << "centered with the reason, not left in the panel's padding";
}

// The note's panel: the smallest 2 px stroke that contains the drawn line (the bordered popup), or none.
struct Panel {
  bool found = false;
  freeink::ui::Rect rect;
  int lines = 0;  // drawn lines inside it
  int widest = 0;
};
Panel panelAround(const screen::RecordingTarget& ui, const screen::DrawnText& line) {
  const auto contains = [](const freeink::ui::Rect& outer, const freeink::ui::Rect& inner) {
    return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.width <= outer.x + outer.width &&
           inner.y + inner.height <= outer.y + outer.height;
  };
  Panel panel;
  for (const auto& stroke : ui.strokeRects) {
    if (stroke.width != 2 || !contains(stroke.rect, line.rect)) continue;
    if (panel.found && stroke.rect.width * stroke.rect.height >= panel.rect.width * panel.rect.height) continue;
    panel.found = true;
    panel.rect = stroke.rect;
  }
  for (const auto& drawn : ui.drawn) {
    // The empty list's message sits behind the popup, and may or may not fall inside it.
    if (!panel.found || !contains(panel.rect, drawn.rect) || drawn.text == tr(STR_GAMES_EMPTY)) continue;
    ++panel.lines;
    panel.widest = std::max<int>(panel.widest, drawn.rect.width);
  }
  return panel;
}

// The device wraps a text() call on spaces only, so a '\n' inside a call does not start a line there. The recording
// target lays text out with layoutText, which does, so the line breaks are asserted on the calls as passed.
TEST_F(ListTest, TheMoreLineIsATextCallOfItsOwnAndNoCallHoldsANewline) {
  installerscript::script().report.failed = 2;
  installerscript::script().report.firstError = Error::BadCrc;
  std::snprintf(installerscript::script().report.firstFile, sizeof(installerscript::script().report.firstFile),
                "first.chgame");
  open();
  int reasonCalls = 0;
  int moreCalls = 0;
  for (const screen::DrawnText& call : ui().textCalls) {
    EXPECT_EQ(call.text.find('\n'), std::string::npos) << "a '\\n' does not break a line on the device: " << call.text;
    reasonCalls += call.text == std::string("first.chgame: ") + tr(STR_GAMES_INSTALL_BAD_CRC);
    moreCalls += call.text == "and 1 more";
  }
  EXPECT_EQ(reasonCalls, 1) << "the reason is one call, and not joined with the more line";
  EXPECT_EQ(moreCalls, 1);
}

TEST_F(ListTest, TheMoreLineAddsOneLineToThePanelAndNothingElse) {
  installerscript::script().report.failed = 1;
  installerscript::script().report.firstError = Error::BadCrc;
  std::snprintf(installerscript::script().report.firstFile, sizeof(installerscript::script().report.firstFile),
                "first.chgame");
  open();
  const screen::DrawnText* reason = nullptr;
  for (const screen::DrawnText& drawn : ui().drawn)
    if (drawn.text.find("first.chgame") == 0) reason = &drawn;
  ASSERT_NE(reason, nullptr) << ui().joined();
  const Panel one = panelAround(ui(), *reason);
  ASSERT_TRUE(one.found) << "a bordered (2 px) panel around the note";
  // Padding 12 above and below, 16 left and right (PopupProps): the panel is its text plus that.
  EXPECT_EQ(one.rect.height, one.lines * screen::RecordingTarget::LINE_HEIGHT + 24);
  EXPECT_EQ(one.rect.width, one.widest + 32);

  installerscript::script().report.failed = 2;
  reopen();
  reason = nullptr;
  for (const screen::DrawnText& drawn : ui().drawn)
    if (drawn.text.find("first.chgame") == 0) reason = &drawn;
  ASSERT_NE(reason, nullptr) << ui().joined();
  const Panel two = panelAround(ui(), *reason);
  ASSERT_TRUE(two.found) << "the panel keeps its border";
  EXPECT_EQ(two.lines, one.lines + 1);
  EXPECT_EQ(two.rect.height, one.rect.height + screen::RecordingTarget::LINE_HEIGHT);
  EXPECT_EQ(two.rect.width, one.rect.width);
  EXPECT_EQ(two.rect.x, one.rect.x);
  // Centered on the screen as the one-line panel is: it grows equally up and down.
  EXPECT_EQ(2 * two.rect.y + two.rect.height, 2 * one.rect.y + one.rect.height);
}

TEST_F(ListTest, TheMoreLineIsShownOncePerVisitAndDismissedWithTheNote) {
  addGame("alpha", "Alpha");
  installerscript::script().report.failed = 3;
  installerscript::script().report.firstError = Error::NoMain;
  open();
  ASSERT_NE(flat(ui().joined()).find("and 2 more"), std::string::npos) << "the note is up on entering";
  EXPECT_EQ(installerscript::script().installCalls(), 1);

  input->click(Button::Back);
  frame();
  render();
  EXPECT_EQ(flat(ui().joined()).find("and 2 more"), std::string::npos) << "the dismissed note stays dismissed";
  render();
  EXPECT_EQ(flat(ui().joined()).find("and 2 more"), std::string::npos) << "and a later frame does not bring it back";
  EXPECT_EQ(installerscript::script().installCalls(), 1) << "one install, so one note, per visit";

  reopen();  // a new visit installs again and explains again, once
  EXPECT_EQ(installerscript::script().installCalls(), 2);
  EXPECT_NE(flat(ui().joined()).find("and 2 more"), std::string::npos);
}

// ---- the kind of the others (AI-2, the retro's Q2): those that wait for room apart from the rest ----

// The scripted report: `failed` failures, `waiting` of them packages that wait for room (the first included when it is
// one), the first `first`, in first.chgame.
void scriptReport(const int failed, const int waiting, const Error first) {
  GamePackageInstaller::Report& report = installerscript::script().report;
  report.failed = static_cast<uint8_t>(failed);
  report.waiting = static_cast<uint8_t>(waiting);
  report.firstError = first;
  std::snprintf(report.firstFile, sizeof(report.firstFile), "first.chgame");
}

// The note's text() calls but its reason, in the order drawn: its more-lines, each as passed. The empty list's message
// is behind the note, not part of it.
std::vector<std::string> moreCalls(const screen::RecordingTarget& ui) {
  std::vector<std::string> out;
  for (const screen::DrawnText& call : ui.textCalls)
    if (call.text.rfind("first.chgame", 0) != 0 && call.text != tr(STR_GAMES_EMPTY)) out.push_back(call.text);
  return out;
}

TEST_F(ListTest, OneFailureShowsNoMoreLineWhateverItsKindOrTheWaitingCount) {
  // The matrix's "1, any, any": with no other failure, no count of waiting packages makes a line, even one the real
  // installer cannot report (more waiting than failed), so the note never claims more than `failed`.
  for (const int waiting : {0, 1, 255}) {
    for (const Error first : {Error::TooManyGames, Error::BadCrc}) {
      SCOPED_TRACE(std::to_string(waiting) + (first == Error::TooManyGames ? " waiting, a first that waits"
                                                                           : " waiting, a first that does not wait"));
      scriptReport(1, waiting, first);
      if (list) {
        reopen();
      } else {
        open();
      }
      EXPECT_EQ(moreCalls(ui()), std::vector<std::string>{}) << ui().joined();
    }
  }
}

TEST_F(ListTest, WithNoneOfTheOthersWaitingTheLineStaysAndNMore) {
  // The matrix's "others, none waiting": a first that waits is not one of the others.
  const std::vector<std::string> expected{"and 2 more"};
  scriptReport(3, 0, Error::BadCrc);
  open();
  EXPECT_EQ(moreCalls(ui()), expected) << ui().joined();
  scriptReport(3, 1, Error::TooManyGames);
  reopen();
  EXPECT_EQ(moreCalls(ui()), expected) << ui().joined();
  EXPECT_TRUE(ui().drewLine("and 2 more"));
}

TEST_F(ListTest, WhenEveryOtherWaitsOneLineSaysSoAndNothingSaysNotInstalled) {
  scriptReport(3, 3, Error::TooManyGames);  // the retro's d-03: three valid packages over the limit
  open();
  EXPECT_EQ(moreCalls(ui()), std::vector<std::string>{"2 more waiting for room"}) << ui().joined();
  EXPECT_TRUE(ui().drewLine("2 more waiting for room"));
  EXPECT_NE(flat(ui().joined()).find(std::string(tr(STR_GAMES_INSTALL_TOO_MANY_GAMES)) + " 2 more waiting for room"),
            std::string::npos)
      << ui().joined();

  scriptReport(2, 1, Error::BadImage);  // a first of another kind: the one that waits is still named
  reopen();
  EXPECT_EQ(moreCalls(ui()), std::vector<std::string>{"1 more waiting for room"}) << ui().joined();
}

TEST_F(ListTest, WaitingAndOtherFailuresAreTwoLinesTheWaitingFirst) {
  scriptReport(4, 3, Error::TooManyGames);
  open();
  const std::vector<std::string> expected{"2 more waiting for room", "and 1 more not installed"};
  EXPECT_EQ(moreCalls(ui()), expected) << ui().joined();
  EXPECT_NE(flat(ui().joined())
                .find(std::string(tr(STR_GAMES_INSTALL_TOO_MANY_GAMES)) +
                      " 2 more waiting for room and 1 more not installed"),
            std::string::npos)
      << ui().joined();
}

TEST_F(ListTest, OneOfEachKindReadsWithoutAVerbToAgree) {
  scriptReport(3, 2, Error::TooManyGames);
  open();
  const std::vector<std::string> expected{"1 more waiting for room", "and 1 more not installed"};
  EXPECT_EQ(moreCalls(ui()), expected) << ui().joined();
}

TEST_F(ListTest, ASaturatedWaitingCountReadsAsItsFloor) {
  scriptReport(std::numeric_limits<uint8_t>::max(), std::numeric_limits<uint8_t>::max(), Error::TooManyGames);
  open();
  EXPECT_EQ(moreCalls(ui()), std::vector<std::string>{"254 more waiting for room"}) << ui().joined();
}

// The installer never reports more waiting than other failures (Report::waiting), but the note does not rely on it: a
// report that did, with a first that does not wait, still names no more waiting than there are others.
TEST_F(ListTest, TheWaitingLineNeverCountsMoreThanTheOthers) {
  scriptReport(std::numeric_limits<uint8_t>::max(), std::numeric_limits<uint8_t>::max(), Error::BadCrc);
  open();
  EXPECT_EQ(moreCalls(ui()), std::vector<std::string>{"254 more waiting for room"}) << ui().joined();
}

TEST_F(ListTest, TheTwoMoreLinesStackUnderTheReasonCenteredWithIt) {
  scriptReport(4, 2, Error::BadCrc);
  open();
  const auto& drawn = ui().drawn;
  size_t waiting = drawn.size();
  size_t more = drawn.size();
  for (size_t i = 0; i < drawn.size(); ++i) {
    if (drawn[i].text == "2 more waiting for room") waiting = i;
    if (drawn[i].text == "and 1 more not installed") more = i;
  }
  ASSERT_LT(waiting, drawn.size()) << ui().joined();
  ASSERT_LT(more, drawn.size()) << ui().joined();
  ASSERT_GT(waiting, 0u);
  const screen::DrawnText& reason = drawn[waiting - 1];  // the reason's last line, drawn just before
  EXPECT_NE(reason.text.find("damaged"), std::string::npos) << ui().joined();
  EXPECT_EQ(drawn[waiting].rect.y, reason.rect.y + reason.rect.height) << "the first starts where the reason ends";
  EXPECT_EQ(drawn[more].rect.y, drawn[waiting].rect.y + drawn[waiting].rect.height) << "the second under the first";
  for (const size_t line : {waiting, more}) {
    EXPECT_NEAR(drawn[line].rect.x + drawn[line].rect.width / 2.0, reason.rect.x + reason.rect.width / 2.0, 1.0)
        << drawn[line].text << " is centered with the reason";
  }
}

TEST_F(ListTest, EachMoreLineIsATextCallOfItsOwnAndNoCallHoldsANewline) {
  scriptReport(4, 3, Error::TooManyGames);
  open();
  int reasonCalls = 0;
  for (const screen::DrawnText& call : ui().textCalls) {
    EXPECT_EQ(call.text.find('\n'), std::string::npos) << "a '\\n' does not break a line on the device: " << call.text;
    reasonCalls += call.text == std::string("first.chgame: ") + tr(STR_GAMES_INSTALL_TOO_MANY_GAMES);
  }
  EXPECT_EQ(reasonCalls, 1) << "the reason is one call, joined with neither more-line";
  const std::vector<std::string> expected{"2 more waiting for room", "and 1 more not installed"};
  EXPECT_EQ(moreCalls(ui()), expected);
}

TEST_F(ListTest, TwoMoreLinesAddTwoLinesToThePanelAndNothingElse) {
  scriptReport(1, 0, Error::BadCrc);
  open();
  const auto reasonLine = [this]() -> const screen::DrawnText* {
    for (const screen::DrawnText& drawn : ui().drawn)
      if (drawn.text.find("first.chgame") == 0) return &drawn;
    return nullptr;
  };
  ASSERT_NE(reasonLine(), nullptr) << ui().joined();
  const Panel one = panelAround(ui(), *reasonLine());
  ASSERT_TRUE(one.found) << "a bordered (2 px) panel around the note";

  scriptReport(4, 2, Error::BadCrc);
  reopen();
  ASSERT_NE(reasonLine(), nullptr) << ui().joined();
  const Panel three = panelAround(ui(), *reasonLine());
  ASSERT_TRUE(three.found) << "the panel keeps its border";
  EXPECT_EQ(three.lines, one.lines + 2);
  EXPECT_EQ(three.rect.height, one.rect.height + 2 * screen::RecordingTarget::LINE_HEIGHT);
  EXPECT_EQ(three.rect.width, three.widest + 32) << "as wide as its widest line needs";
  EXPECT_EQ(three.rect.height, three.lines * screen::RecordingTarget::LINE_HEIGHT + 24);
  // Centered on the screen as the one-line panel is: it grows equally up and down, and left and right.
  EXPECT_EQ(2 * three.rect.y + three.rect.height, 2 * one.rect.y + one.rect.height);
  EXPECT_EQ(2 * three.rect.x + three.rect.width, 2 * one.rect.x + one.rect.width);
}

TEST_F(ListTest, TheLongestReasonWithTheLongestFileNameIsDrawnWholeAboveBothMoreLines) {
  installerscript::script().report.failed = 12;
  installerscript::script().report.waiting = 6;
  std::string longest;
  for (int value = 0; value < 256; ++value) {
    const char* text = expectedText(static_cast<Error>(value));
    if (text && longest.size() < std::strlen(text)) {
      longest = text;
      installerscript::script().report.firstError = static_cast<Error>(value);
    }
  }
  std::snprintf(installerscript::script().report.firstFile, sizeof(installerscript::script().report.firstFile), "%s",
                std::string(GamePaths::INBOX_NAME_BYTES - 1, 'n').c_str());
  open();
  // Six wait; the first is one of them only when it waits itself.
  const int waiting = installerscript::script().report.firstError == Error::TooManyGames ? 5 : 6;
  const std::string lines =
      std::to_string(waiting) + " more waiting for room and " + std::to_string(11 - waiting) + " more not installed";
  const std::string shown = flat(ui().joined());
  EXPECT_NE(shown.find(longest + " " + lines), std::string::npos) << shown;
}

TEST_F(ListTest, NoFailureShowsNoNote) {
  installerscript::script().report.installed = 1;
  addGame("alpha", "Alpha");
  open();
  const std::vector<std::string> row{"Alpha", tr(STR_GAMES_MODE_SOLO)};
  std::vector<std::string> drawn;
  for (const screen::DrawnText& line : ui().drawn) drawn.push_back(line.text);
  EXPECT_EQ(drawn, row) << "just the one row, its name and modes line";
}

// Row 9 of entry 3's changes (DESIGN.md, Launcher game row): the second line of a game this host can start names the
// modes it can start here, in solo, pass, nearby order, joined by " · ", from the manifest's check; a mode the manifest
// lists that this host cannot start (nearby with no radio, pass with no pass) is not named.
TEST_F(ListTest, TheModesLineNamesTheModesThisHostCanStartInOrder) {
  hostcaps::script().pass = true;
  addGame("all-three", "AllThree", "\"nearby\",\"pass\",\"solo\"", 1, 1, 2);
  addGame("pass-nearby", "PassNearby", "\"pass\",\"nearby\"", 1, 2, 2);
  open();
  const std::string dot = " \xC2\xB7 ";
  EXPECT_EQ(lineAfter(ui(), "AllThree"), std::string(tr(STR_GAMES_MODE_SOLO)) + dot + tr(STR_GAMES_MODE_PASS))
      << "no nearby: the host has no radio";
  EXPECT_EQ(lineAfter(ui(), "PassNearby"), tr(STR_GAMES_MODE_PASS));
  hostcaps::script().pass = false;
  reopen();
  EXPECT_EQ(lineAfter(ui(), "AllThree"), tr(STR_GAMES_MODE_SOLO));
  EXPECT_EQ(lineAfter(ui(), "PassNearby"), tr(STR_GAMES_UNAVAILABLE_MODE))
      << "a game with no mode here gives its reason";
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
                  "a.chgame");
    if (list) {
      reopen();
    } else {
      open();
    }
    const std::string shown = flat(ui().joined());
    EXPECT_NE(shown.find("a.chgame: " + text), std::string::npos) << "shown: " << shown;
    for (const int other : withText) {
      if (other == value) continue;
      EXPECT_EQ(shown.find(expectedText(static_cast<Error>(other))), std::string::npos)
          << "it also says Error " << other << "'s: " << shown;
    }
  }
}

}  // namespace
