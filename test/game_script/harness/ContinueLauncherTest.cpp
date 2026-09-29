#include <GameIcons.h>
#include <I18n.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <set>
#include <string>
#include <vector>

#include "FrameReplay.h"
#include "GameHash.h"
#include "GameRowIcon.h"
#include "GameSaveStore.h"
#include "HostCapsScript.h"
#include "InstallerScript.h"
#include "MatchSupport.h"
#include "RemoveScript.h"
#include "activities/games/GameMatchActivity.h"
#include "activities/games/GameModeActivity.h"
#include "activities/games/GamesLauncherActivity.h"
#include "util/ButtonNavigator.h"

// Ticket 12 of epic-install-and-launcher on the launcher: the Continue rows. The real GamesLauncherActivity,
// GameRegistry, GameSaveStore::peek, and GameMatchActivity over the screen doubles and the fake card, the installer
// scripted (InstallerScript.h, RemoveScript.h) as GameRemoveLauncherTest does. A save is placed on the card as
// GameSaveStore lays it out (ResumeMatchTest's helper, copied: that file keeps its own in an anonymous namespace).

// The next nothrow array allocation of exactly this many bytes fails, once: how a test makes the launcher's
// makeUniqueNoThrow<T[]> return null (GamesLauncherTest does the same for the icon cache). Everything else is malloc.
namespace oom {
std::size_t failSize = 0;
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
  if (oom::failSize != 0 && size == oom::failSize) {
    oom::failSize = 0;
    return nullptr;
  }
  return std::malloc(size);
}

namespace {

// A library icon's rows decoded without GameRowIcon or GameIconBlit (as GamesLauncherTest's helper).
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
using harness::Bytes;

// The package hash "v1\n0530a15766e91bf1\n" holds, and one that differs from it in the last byte.
const std::string PKG_A = "v1\n0530a15766e91bf1\n";
const std::string PKG_B = "v1\n0530a15766e91bf2\n";
const uint8_t HASH_A[GamePkg::HASH_BYTES] = {0x05, 0x30, 0xa1, 0x57, 0x66, 0xe9, 0x1b, 0xf1};
const uint8_t HASH_B[GamePkg::HASH_BYTES] = {0x05, 0x30, 0xa1, 0x57, 0x66, 0xe9, 0x1b, 0xf2};

// {taps = n}: the codec bytes the counting game's snapshot has.
Bytes snapshotOf(const uint8_t taps) {
  return Bytes{0x06, 0x00, 0x01, 0x05, 0x04, 't', 'a', 'p', 's', 0x03, static_cast<uint8_t>(taps << 1)};
}

// resume.bin as GameSaveStore lays it out for a solo match of the package `hash`.
Bytes resumeBytes(const uint8_t taps, const uint16_t ver, const uint8_t (&hash)[GamePkg::HASH_BYTES] = HASH_A) {
  Bytes out = {'C', 'H', 'R', 'S', 1, 1};
  out.insert(out.end(), hash, hash + GamePkg::HASH_BYTES);
  out.push_back(0);
  out.push_back(1);
  out.push_back(static_cast<uint8_t>(ver & 0xFF));
  out.push_back(static_cast<uint8_t>(ver >> 8));
  const Bytes snapshot = snapshotOf(taps);
  out.insert(out.end(), snapshot.begin(), snapshot.end());
  return out;
}

// A game of five taps that logs its setup and every draw, so a resumed match shows which snapshot it drew and whether
// setup ran.
const char* const COUNTING_GAME = R"(
local game = {}
function game.setup(ctx)
  ch.log("setup ran")
  return { taps = 0 }
end
function game.status(state)
  if state.taps >= 5 then return { over = true, winners = { 1 } } end
  return { turn = 1 }
end
function game.apply(state, seat, move)
  state.taps = state.taps + 1
  return state
end
function game.draw(state, seat, ui)
  ch.log("draw", state.taps)
  ch.gfx.clear("white")
end
function game.input(state, seat, ui, ev)
  if ev.kind == "tap" then return { tap = true } end
end
return game
)";

std::string manifestJson(const std::string& id, const std::string& name, const std::string& modes, const int api,
                         const int seatsMax, const std::string& extra = "") {
  return "{\"id\":\"" + id + "\",\"name\":\"" + name + "\",\"version\":\"1.0.0\",\"api\":" + std::to_string(api) +
         ",\"seats\":{\"min\":1,\"max\":" + std::to_string(seatsMax) + "},\"modes\":[" + modes + "]" + extra + "}";
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

std::string resumePath(const std::string& id) { return "/.games-data/" + id + "/resume.bin"; }

// One row of the list as drawn: the game's name, and whether "Continue" is written under it.
struct Row {
  std::string name;
  bool isContinue;
  bool operator==(const Row& other) const { return name == other.name && isContinue == other.isContinue; }
};

std::ostream& operator<<(std::ostream& out, const Row& row) {
  return out << row.name << (row.isContinue ? " (Continue)" : "");
}

class ContinueTest : public match::ScreenTest {
 protected:
  void SetUp() override {
    ScreenTest::SetUp();
    oom::failSize = 0;
    installerscript::reset();
    removescript::reset();
    hostcaps::reset();
    GamesLauncherActivity::forgetOpenedGame();  // a fresh boot
    ButtonNavigator::setMappedInputManager(*input);
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

  // Installs a game of the counting Lua; its .pkg holds PKG_A unless `pkg` says otherwise.
  static void addGame(const std::string& id, const std::string& name, const std::string& modes = "\"solo\"",
                      const int api = 1, const int seatsMax = 1, const std::string& pkg = PKG_A,
                      const std::string& extra = "") {
    const std::string dir = "/.games/" + id;
    fakesd::addFile(dir + "/manifest.json", manifestJson(id, name, modes, api, seatsMax, extra));
    fakesd::addFile(dir + "/main.lua", std::string(COUNTING_GAME));
    fakesd::addFile(dir + "/.pkg", pkg);
  }
  static void addGames(const int count) {
    for (int i = 1; i <= count; ++i) addGame(idOf(i), nameOf(i));
  }
  static void save(const std::string& id, const uint8_t taps = 2, const uint16_t ver = 3,
                   const uint8_t (&hash)[GamePkg::HASH_BYTES] = HASH_A) {
    fakesd::addFile(resumePath(id), resumeBytes(taps, ver, hash));
  }
  static void takeGameOffTheCard(const std::string& id) {
    fakesd::removeEntry("/.games/" + id + "/.pkg");
    for (const char* file : {"manifest.json", "main.lua"}) fakesd::removeEntry("/.games/" + id + "/" + file);
    fakesd::removeEntry("/.games/" + id);
  }
  // How many times the card was asked to open any resume.bin (peek reads the whole file once per save).
  static size_t resumeOpens() {
    size_t total = 0;
    for (const std::string& op : fakesd::sim().ops)
      if (op.rfind("open /.games-data/", 0) == 0 && op.find("/resume.bin") != std::string::npos) ++total;
    return total;
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
    dropMatch();
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
  void frame() {
    activity().loop();
    input->clear();
    if (activityManager.updateRequested()) render();
  }
  screen::RecordingTarget& ui() { return *screen::RecordingTarget::newest(); }

  // Every drawn text equal to `text`, top to bottom.
  std::vector<const screen::DrawnText*> findAll(const std::string& text) {
    std::vector<const screen::DrawnText*> found;
    for (const screen::DrawnText& drawn : ui().drawn)
      if (drawn.text == text) found.push_back(&drawn);
    std::sort(found.begin(), found.end(),
              [](const screen::DrawnText* a, const screen::DrawnText* b) { return a->rect.y < b->rect.y; });
    return found;
  }
  void tapAt(const screen::DrawnText* drawn) {
    ASSERT_NE(drawn, nullptr);
    input->tap(drawn->rect.x + drawn->rect.width / 2, drawn->rect.y + drawn->rect.height / 2);
    frame();
  }
  void longPressAt(const screen::DrawnText* drawn) {
    ASSERT_NE(drawn, nullptr);
    input->longPress(drawn->rect.x + drawn->rect.width / 2, drawn->rect.y + drawn->rect.height / 2);
    frame();
  }
  // The Continue line of the `n`th Continue row drawn (0 is the top one).
  const screen::DrawnText* continueLine(const size_t n = 0) {
    const auto lines = findAll(tr(STR_GAMES_CONTINUE));
    return n < lines.size() ? lines[n] : nullptr;
  }
  // The name line of the game's own row: the last one drawn with that name, since a Continue row's name is above it.
  const screen::DrawnText* gameRowOf(const std::string& name) {
    const auto lines = findAll(name);
    return lines.empty() ? nullptr : lines.back();
  }
  void key(const Button button, const int times = 1) {
    for (int i = 0; i < times; ++i) {
      input->click(button);
      frame();
    }
  }
  void swipeUp() {
    input->swipeDirection(MappedInputManager::SwipeDir::Up);
    frame();
  }
  bool dialogUp() { return ui().drewLine(tr(STR_GAMES_REMOVE_TITLE)); }

  // The rows drawn, top to bottom, among the games whose names are `names`.
  std::vector<Row> rows(const std::set<std::string>& names) {
    std::vector<const screen::DrawnText*> named;
    for (const screen::DrawnText& drawn : ui().drawn)
      if (names.count(drawn.text) != 0) named.push_back(&drawn);
    std::sort(named.begin(), named.end(),
              [](const screen::DrawnText* a, const screen::DrawnText* b) { return a->rect.y < b->rect.y; });
    // A Continue line belongs to the name nearest above it: the row's own, whatever the row height.
    std::set<const screen::DrawnText*> continued;
    for (const screen::DrawnText* subtitle : findAll(tr(STR_GAMES_CONTINUE))) {
      const screen::DrawnText* owner = nullptr;
      for (const screen::DrawnText* name : named)
        if (name->rect.y < subtitle->rect.y) owner = name;  // `named` is top to bottom
      if (owner) continued.insert(owner);
    }
    std::vector<Row> found;
    for (const screen::DrawnText* name : named) found.push_back({name->text, continued.count(name) != 0});
    return found;
  }
  std::vector<Row> rows(const int total) {
    std::set<std::string> names;
    for (int i = 1; i <= total; ++i) names.insert(nameOf(i));
    return rows(names);
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
  // Runs the match's loop until the log says `part`.
  bool pumpMatchTo(const std::string& part) {
    return match::waitFor([&] {
      entered->loop();
      input->clear();
      return logHas(part);
    });
  }

  std::unique_ptr<GamesLauncherActivity> list;
  Activity* entered = nullptr;
};

}  // namespace

// ---- which games get a row ----

TEST_F(ContinueTest, OneContinueRowForEachValidSaveComesFirstAndInNameOrder) {
  addGame("bravo", "Bravo");
  addGame("charlie", "Charlie");
  addGame("alpha", "Alpha");
  save("charlie");
  save("alpha");
  open();
  const std::vector<Row> expected{
      {"Alpha", true}, {"Charlie", true}, {"Alpha", false}, {"Bravo", false}, {"Charlie", false}};
  EXPECT_EQ(rows({"Alpha", "Bravo", "Charlie"}), expected);
  EXPECT_EQ(findAll(tr(STR_GAMES_CONTINUE)).size(), 2u);
}

TEST_F(ContinueTest, WithNoSaveTheListIsTheGamesAlone) {
  addGames(3);
  fakesd::addFile("/.games-data/game-02/store.bin", std::string("only a store"));
  open();
  const std::vector<Row> expected{{"Game 01", false}, {"Game 02", false}, {"Game 03", false}};
  EXPECT_EQ(rows(3), expected);
  EXPECT_TRUE(findAll(tr(STR_GAMES_CONTINUE)).empty());
}

TEST_F(ContinueTest, ASaveOfAChangedPackageShowsNoRowAndIsLeftOnTheCard) {
  addGames(2);
  save("game-01");
  save("game-02", 2, 3, HASH_B);  // written for the package as it was before the game was reinstalled
  open();
  const std::vector<Row> expected{{"Game 01", true}, {"Game 01", false}, {"Game 02", false}};
  EXPECT_EQ(rows(2), expected);
  EXPECT_TRUE(fakesd::has(resumePath("game-02"))) << "the launcher only looks";

  // The installer replaces game-01 with a build whose package hash differs: the launcher built next has no row for it.
  fakesd::addFile("/.games/game-01/.pkg", PKG_B);
  reopen();
  const std::vector<Row> after{{"Game 01", false}, {"Game 02", false}};
  EXPECT_EQ(rows(2), after);
  EXPECT_TRUE(fakesd::has(resumePath("game-01")));
}

TEST_F(ContinueTest, AGameThisHostCannotStartHasNoRowAndItsSaveIsNotRead) {
  addGame("alpha", "Alpha");
  addGame("too-new", "TooNew", "\"solo\"", 2);
  save("alpha");
  save("too-new");
  open();
  const std::vector<Row> expected{{"Alpha", true}, {"Alpha", false}, {"TooNew", false}};
  EXPECT_EQ(rows({"Alpha", "TooNew"}), expected);
  EXPECT_EQ(fakesd::countOps("open " + resumePath("too-new")), 0u);
}

TEST_F(ContinueTest, ASaveThatIsNotAValidResumeShowsNoRow) {
  addGames(3);
  fakesd::addFile(resumePath("game-01"), std::string("resume of game-01"));
  Bytes truncated = resumeBytes(2, 3);
  truncated.resize(12);
  fakesd::addFile(resumePath("game-02"), truncated);
  save("game-03");
  open();
  const std::vector<Row> expected{{"Game 03", true}, {"Game 01", false}, {"Game 02", false}, {"Game 03", false}};
  EXPECT_EQ(rows(3), expected);
}

TEST_F(ContinueTest, ASaveWhoseRenameWasInterruptedStillOffersContinue) {
  addGames(1);
  fakesd::addFile(resumePath("game-01") + ".tmp", resumeBytes(2, 3));  // resume.bin.tmp alone: peek accepts it
  open();
  const std::vector<Row> expected{{"Game 01", true}, {"Game 01", false}};
  EXPECT_EQ(rows(1), expected);
}

// ---- starting from a Continue row ----

TEST_F(ContinueTest, ATapOnContinueResumesTheSavedMatchWithNoModeStep) {
  hostcaps::script().pass = true;
  addGame("alpha", "Alpha", "\"solo\",\"pass\"", 1, 2);  // two modes: its game row opens the picker
  save("alpha");
  open();
  tapAt(continueLine());
  EXPECT_EQ(activityManager.asks.pushed, 0) << "no mode picker";
  ASSERT_EQ(activityManager.replacements.size(), 1u);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started alpha"));
  ASSERT_TRUE(pumpMatchTo("Round started at ver 3"));
  EXPECT_TRUE(logHas("Resuming at ver 3"));
  EXPECT_TRUE(logHas("draw\t2")) << "the saved snapshot is what is drawn";
  EXPECT_FALSE(logHas("setup ran"));
}

TEST_F(ContinueTest, ConfirmOnTheSelectedContinueRowResumes) {
  addGames(2);
  save("game-02");
  open();  // the selection starts on the first row, the Continue row
  key(Button::Confirm);
  ASSERT_EQ(activityManager.replacements.size(), 1u);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started game-02"));
  ASSERT_TRUE(pumpMatchTo("Resuming at ver 3"));
}

TEST_F(ContinueTest, TheGamesOwnRowStillStartsANewMatchAndTwoModesStillAskWhichOne) {
  hostcaps::script().pass = true;
  addGame("alpha", "Alpha");
  addGame("beta", "Beta", "\"solo\",\"pass\"", 1, 2);
  save("alpha");
  save("beta");
  open();
  tapAt(gameRowOf("Alpha"));
  ASSERT_EQ(activityManager.replacements.size(), 1u);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_FALSE(logHas("Resuming"));
  ASSERT_TRUE(pumpMatchTo("setup ran"));

  reopen();
  tapAt(gameRowOf("Beta"));
  EXPECT_EQ(activityManager.asks.pushed, 1) << "a game row of a game with two modes asks";
  EXPECT_TRUE(activityManager.replacements.empty());
}

TEST_F(ContinueTest, ASaveThatWentBadBetweenTheListingAndTheTapStartsANewMatch) {
  addGames(1);
  save("game-01");
  open();
  fakesd::addFile(resumePath("game-01"), std::string("garbage"));
  tapAt(continueLine());
  ASSERT_EQ(activityManager.replacements.size(), 1u);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("no usable resume.bin; starting a new match"));
  ASSERT_TRUE(pumpMatchTo("setup ran"));
}

// ---- long-press, stale taps ----

TEST_F(ContinueTest, ALongPressOrAHoldOnAContinueRowDoesNothingAndTheGamesOwnRowStillAsksToRemove) {
  addGames(2);
  save("game-01");
  open();
  longPressAt(continueLine());
  EXPECT_FALSE(dialogUp());
  EXPECT_EQ(activityManager.asks.replaced, 0);
  input->holdLong(Button::Confirm);  // the selection is on the Continue row
  frame();
  EXPECT_FALSE(dialogUp());
  EXPECT_EQ(activityManager.asks.replaced, 0);

  longPressAt(gameRowOf("Game 01"));
  EXPECT_TRUE(dialogUp());
  EXPECT_TRUE(ui().drewLine("Game 01"));
}

TEST_F(ContinueTest, ATapOnAContinueRowUnderTheConfirmationOrTheNoteStartsNothing) {
  addGames(2);
  save("game-01");
  installerscript::script().report.failed = 1;
  installerscript::script().report.firstError = GamePackageInstaller::Error::NotAPackage;
  open();
  const screen::DrawnText* line = continueLine();
  ASSERT_NE(line, nullptr);
  const int x = line->rect.x + line->rect.width / 2;
  const int y = line->rect.y + line->rect.height / 2;
  input->tap(x, y);  // under the install note
  frame();
  EXPECT_TRUE(activityManager.replacements.empty());
  input->tap(240, 400);  // dismisses the note
  frame();

  longPressAt(gameRowOf("Game 02"));
  ASSERT_TRUE(dialogUp());
  input->tap(x, y);  // the confirmation is drawn: the tap is outside its buttons
  frame();
  EXPECT_TRUE(activityManager.replacements.empty());
  EXPECT_TRUE(dialogUp());
  tapAt(findAll(tr(STR_CANCEL)).back());

  // A long-press opens the confirmation, and a tap arrives before it is drawn: the hit rects are still the list's.
  input->longPress(gameRowOf("Game 02")->rect.x + 5, gameRowOf("Game 02")->rect.y + 5);
  activity().loop();
  input->clear();
  input->tap(x, y);
  activity().loop();
  input->clear();
  EXPECT_TRUE(activityManager.replacements.empty()) << "the tap opened a resume under the confirmation";
  render();
  EXPECT_TRUE(dialogUp());
}

// ---- paging and selection ----

TEST_F(ContinueTest, ContinueRowsPageWithTheGamesAndNoRowRepeats) {
  const int games = 10;
  addGames(games);
  for (int i = 1; i <= 5; ++i) save(idOf(i));
  open();
  const size_t page = rows(games).size();
  ASSERT_GE(page, 2u);
  const size_t total = 5 + games;
  ASSERT_LT(page, total) << "the list must not fit one screen";
  std::vector<Row> expected;
  for (int i = 1; i <= 5; ++i) expected.push_back({nameOf(i), true});
  for (int i = 1; i <= games; ++i) expected.push_back({nameOf(i), false});

  std::vector<Row> seen;
  const size_t pages = (total + page - 1) / page;
  for (size_t p = 0; p < pages; ++p) {
    if (p > 0) swipeUp();
    const std::vector<Row> shownRows = rows(games);
    const size_t wanted = std::min(page, total - p * page);
    EXPECT_EQ(shownRows.size(), wanted) << "page " << p + 1 << " holds what is left and nothing twice";
    seen.insert(seen.end(), shownRows.begin(), shownRows.end());
  }
  EXPECT_EQ(seen, expected);
}

TEST_F(ContinueTest, TheKeysWalkContinueRowsThenGamesAndWrap) {
  addGames(3);
  save("game-02");
  save("game-03");
  open();  // rows: Continue 02, Continue 03, Game 01, 02, 03
  key(Button::NavNext, 2);
  key(Button::Confirm);  // Game 01's own row
  ASSERT_EQ(activityManager.replacements.size(), 1u);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started game-01"));
  EXPECT_FALSE(logHas("Resuming"));

  reopen();
  // The game opened last (Game 01) is selected again, on its own row: one step back is Continue 03, another Continue
  // 02, and a third wraps to the last game.
  fakelog::clearLines();
  key(Button::NavPrevious, 3);
  key(Button::Confirm);
  ASSERT_EQ(activityManager.replacements.size(), 1u);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started game-03"));
}

TEST_F(ContinueTest, AStepBackFromTheFirstRowWrapsToTheLastGameNotToABlankPaddingRow) {
  const int games = 10;
  addGames(games);
  for (int i = 1; i <= 5; ++i) save(idOf(i));
  open();
  ASSERT_LT(rows(games).size(), static_cast<size_t>(5 + games));
  ASSERT_NE((5 + games) % rows(games).size(), 0u) << "this page size leaves padding to test";
  key(Button::NavPrevious);
  key(Button::Confirm);
  ASSERT_EQ(activityManager.replacements.size(), 1u);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started game-10"));
  EXPECT_FALSE(logHas("Resuming"));
}

TEST_F(ContinueTest, LeavingAMatchStartedFromContinueReturnsToThePageHoldingThatGame) {
  addGames(25);
  save("game-20");
  open();
  const size_t page = rows(25).size();
  ASSERT_GT(page, 2u);
  key(Button::Confirm);  // Continue Game 20
  ASSERT_EQ(activityManager.replacements.size(), 1u);
  ASSERT_NE(enterReplacement(), nullptr);
  ASSERT_TRUE(pumpMatchTo("Resuming at ver 3"));

  reopen();  // Leave: goToGames() builds a fresh launcher
  fakelog::clearLines();
  const size_t gameRow = 20;  // row 0 is Continue; game k is row k
  const size_t first = gameRow / page * page;
  const std::vector<Row> shownRows = rows(25);
  ASSERT_FALSE(shownRows.empty());
  ASSERT_GE(first, 1u);
  EXPECT_EQ(shownRows.front(), (Row{nameOf(static_cast<int>(first)), false})) << "the whole page, from its first row";
  EXPECT_TRUE(ui().drewLine("Game 20"));
  key(Button::Confirm);  // the selection is on Game 20's own row
  ASSERT_EQ(activityManager.replacements.size(), 1u);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started game-20"));
  EXPECT_FALSE(logHas("Resuming")) << "the game's row starts a new match";
}

// ---- remove, and what the launcher asks the card ----

TEST_F(ContinueTest, RemovingAGameDropsItsContinueRowAndKeepsTheOthers) {
  addGames(3);
  save("game-01");
  save("game-02");
  save("game-03");
  fakesd::addFile("/.games-data/game-02/store.bin", std::string("store of game-02"));
  open();
  const size_t opens = resumeOpens();
  EXPECT_EQ(opens, 3u) << "one read of each save when the launcher is built";

  longPressAt(gameRowOf("Game 02"));
  ASSERT_TRUE(dialogUp());
  tapAt(findAll(tr(STR_GAMES_REMOVE)).back());
  const std::vector<Row> expected{{"Game 01", true}, {"Game 03", true}, {"Game 01", false}, {"Game 03", false}};
  EXPECT_EQ(rows(3), expected);
  EXPECT_EQ(resumeOpens(), opens + 2) << "the rebuilt list reads the two saves of the games left, and no more";
  EXPECT_TRUE(fakesd::has(resumePath("game-02"))) << "the removed game's data is kept";

  // The selection took the next game's place, on its own row: Confirm opens Game 03 as a new match.
  key(Button::Confirm);
  ASSERT_EQ(activityManager.replacements.size(), 1u);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started game-03"));
  EXPECT_FALSE(logHas("Resuming"));
}

TEST_F(ContinueTest, ACardWithTwentyFiveGamesCostsOneReadPerSaveAndNoneWhileTheListIsDrawnOrScrolled) {
  addGames(25);
  for (const int n : {3, 11, 24}) save(idOf(n));
  open();
  EXPECT_EQ(resumeOpens(), 3u) << "25 games, 3 saves: 3 whole-file reads (the other 22 only ask whether a file exists)";
  const size_t opens = fakesd::countOps("open ");
  for (int i = 0; i < 3; ++i) {
    swipeUp();
    render();
  }
  EXPECT_EQ(fakesd::countOps("open "), opens) << "drawing and scrolling read nothing";
}

TEST_F(ContinueTest, NoGamesAndNoSavesAreAsBefore) {
  open();
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_EMPTY)));
  fakesd::addFile(resumePath("ghost"), resumeBytes(2, 3));  // a save whose game is not installed has no row
  reopen();
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_EMPTY)));
  EXPECT_TRUE(findAll(tr(STR_GAMES_CONTINUE)).empty());
}

TEST_F(ContinueTest, RemovingAGameWithNoSaveKeepsTheOtherGamesContinueRowsAndTheSelectionOnTheNextGame) {
  addGames(4);
  save("game-01");
  save("game-04");
  open();  // Continue 01, Continue 04, Game 01 .. 04
  longPressAt(gameRowOf("Game 02"));
  tapAt(findAll(tr(STR_GAMES_REMOVE)).back());
  const std::vector<Row> expected{
      {"Game 01", true}, {"Game 04", true}, {"Game 01", false}, {"Game 03", false}, {"Game 04", false}};
  EXPECT_EQ(rows(4), expected) << "a game with no save gained no row, and none of the others lost theirs";
  key(Button::Confirm);  // the selection took Game 02's place on Game 03's own row
  ASSERT_EQ(activityManager.replacements.size(), 1u);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started game-03"));
  EXPECT_FALSE(logHas("Resuming"));
}

TEST_F(ContinueTest, ARemoveThatFailsKeepsTheContinueRowsAndSaysSo) {
  addGames(3);
  save("game-02");
  open();
  removescript::script().onRemove = nullptr;
  removescript::script().result = GamePackageInstaller::Error::SdCard;
  longPressAt(gameRowOf("Game 03"));
  tapAt(findAll(tr(STR_GAMES_REMOVE)).back());
  std::string joined = ui().joined();  // the note wraps over lines
  std::replace(joined.begin(), joined.end(), '\n', ' ');
  EXPECT_NE(joined.find("Game 03: " + std::string(tr(STR_GAMES_REMOVE_FAILED))), std::string::npos) << joined;
  const std::vector<Row> expected{{"Game 02", true}, {"Game 01", false}, {"Game 02", false}, {"Game 03", false}};
  EXPECT_EQ(rows(3), expected);
}

TEST_F(ContinueTest, RemovingTheOnlyGameLeavesTheEmptyListWithoutItsContinueRow) {
  addGames(1);
  save("game-01");
  open();
  longPressAt(gameRowOf("Game 01"));
  tapAt(findAll(tr(STR_GAMES_REMOVE)).back());
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_EMPTY)));
  EXPECT_TRUE(findAll(tr(STR_GAMES_CONTINUE)).empty());
  key(Button::Confirm);
  EXPECT_TRUE(activityManager.replacements.empty());
}

TEST_F(ContinueTest, AContinueRowDrawsItsOwnGamesIcon) {
  addGame("a-pkg", "A Pkg", "\"solo\"", 1, 1, PKG_A, ",\"icon\":\"boat\"");
  fakesd::addFile("/.games/a-pkg/icon.bmp",
                  harness::bmpFile(64, 64, [](const int x, const int y) { return harness::speckle(x, y, 1); }));
  addGame("b-dice", "B Dice", "\"solo\"", 1, 1, PKG_A, ",\"icon\":\"dice-six\",\"icon_weight\":\"fill\"");
  addGame("c-none", "C None");
  save("c-none");
  save("a-pkg");
  open();
  // Continue A Pkg, Continue C None, then the games' own rows A Pkg, B Dice, C None.
  const std::vector<Row> expected{
      {"A Pkg", true}, {"C None", true}, {"A Pkg", false}, {"B Dice", false}, {"C None", false}};
  ASSERT_EQ(rows({"A Pkg", "B Dice", "C None"}), expected);
  const auto& drawn = ui().bitmapsDrawn;
  ASSERT_EQ(drawn.size(), 5u);
  const std::vector<uint8_t> pkg =
      harness::rowsOf(harness::bmpFile(64, 64, [](const int x, const int y) { return harness::speckle(x, y, 1); }));
  EXPECT_EQ(drawn[0].data, pkg) << "Continue A Pkg: the game's icon.bmp";
  EXPECT_EQ(drawn[1].data, libraryRows("game-controller", false)) << "Continue C None: the fallback";
  EXPECT_EQ(drawn[2].data, pkg);
  EXPECT_EQ(drawn[3].data, libraryRows("dice-six", true));
  EXPECT_EQ(drawn[4].data, libraryRows("game-controller", false));
}

TEST_F(ContinueTest, WhenTheContinueListCannotBeAllocatedTheGamesAreListedAndRemoveStillWorks) {
  addGames(3);
  save("game-02");
  oom::failSize = 3 * sizeof(uint16_t);  // loadContinue's array, the first allocation of this size
  open();
  EXPECT_TRUE(logHas("OOM: 6 Continue slots"));
  const std::vector<Row> expected{{"Game 01", false}, {"Game 02", false}, {"Game 03", false}};
  EXPECT_EQ(rows(3), expected);
  key(Button::Confirm);  // a game row, since there are no Continue rows
  ASSERT_EQ(activityManager.replacements.size(), 1u);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_FALSE(logHas("Resuming"));

  reopen();  // the next visit is allocated again
  ASSERT_EQ(rows(3).size(), 4u);
  EXPECT_TRUE(rows(3).front().isContinue);
}
