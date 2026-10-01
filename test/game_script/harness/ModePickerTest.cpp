#include <I18n.h>
#include <Manifest.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "ApiLevel.h"
#include "GameHostCaps.h"
#include "GameRegistry.h"
#include "HostCapsScript.h"
#include "InstallerScript.h"
#include "MatchSupport.h"
#include "activities/games/GameMatchActivity.h"
#include "activities/games/GameModeActivity.h"
#include "activities/games/GamesLauncherActivity.h"
#include "util/ButtonNavigator.h"

// The real GameModeActivity, a game's title screen (entry 7 of epic-pass-and-play), and the real GamesLauncherActivity
// whose game rows push it, over the screen doubles (screen_stubs/), the fake card, and the scripted installer and host
// caps (list_stubs/), on the pattern of GamesLauncherTest.cpp: the fixture opens a screen, draws it, and finds a row by
// the text it drew. What the title screen promises: its header is the game's name; a Continue row first when the card
// holds a save (Valid or Unreadable); then one New row per mode the host can start, solo, pass, nearby; a New over a
// save asks first, Cancel focused; a Continue on a pass save, or on a two-mode game's Unreadable save, never starts a
// solo match that would replace the save; Back returns to the launcher.

// A nothrow allocation of exactly this many bytes fails while it is non-zero: how a test makes the title screen's
// makeUniqueNoThrow<GameMatchActivity> return null (GamesLauncherTest does the same for arrays). Every other allocation
// is malloc, as the default operator's, so nothing else changes.
namespace oom {
std::size_t failSize = 0;
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  if (oom::failSize != 0 && size == oom::failSize) return nullptr;
  return std::malloc(size);
}

namespace {

using Button = MappedInputManager::Button;
using GameCore::Manifest;
using harness::Bytes;

const std::string PKG = "v1\n0530a15766e91bf1\n";
// The package hash PKG holds.
const uint8_t HASH[GamePkg::HASH_BYTES] = {0x05, 0x30, 0xa1, 0x57, 0x66, 0xe9, 0x1b, 0xf1};

// Reads the protected name an Activity was constructed with.
struct NameOf : Activity {
  static const std::string& of(const Activity& activity) { return activity.*(&NameOf::name); }
};

std::string manifestJson(const std::string& id, const std::string& name, const std::string& modes, const int seatsMin,
                         const int seatsMax) {
  return "{\"id\":\"" + id + "\",\"name\":\"" + name +
         "\",\"version\":\"1.0.0\",\"api\":1,\"seats\":{\"min\":" + std::to_string(seatsMin) +
         ",\"max\":" + std::to_string(seatsMax) + "},\"modes\":[" + modes + "]}";
}

Manifest parsed(const std::string& json) {
  Manifest manifest;
  EXPECT_EQ(Manifest::parse(json, manifest), GameCore::ManifestError::None);
  return manifest;
}

constexpr uint8_t SOLO = Manifest::MODE_SOLO;
constexpr uint8_t PASS = Manifest::MODE_PASS;
constexpr uint8_t NEARBY = Manifest::MODE_NEARBY;

// resume.bin's mode byte (docs/crosshatch/formats.md): 0 solo, 1 pass.
constexpr uint8_t SAVED_SOLO = 0;
constexpr uint8_t SAVED_PASS = 1;

// {taps = n}: the codec bytes the counting game's snapshot has.
Bytes snapshotOf(const uint8_t taps) {
  return Bytes{0x06, 0x00, 0x01, 0x05, 0x04, 't', 'a', 'p', 's', 0x03, static_cast<uint8_t>(taps << 1)};
}

// resume.bin as GameSaveStore lays it out for a match of PKG's package in `mode` with `seats` seats
// (ContinueLauncherTest's helper, which fixes them at solo and one).
Bytes resumeBytes(const uint8_t taps, const uint16_t ver, const uint8_t mode = SAVED_SOLO, const uint8_t seats = 1) {
  Bytes out = {'C', 'H', 'R', 'S', 1, 1};
  out.insert(out.end(), HASH, HASH + GamePkg::HASH_BYTES);
  out.push_back(mode);
  out.push_back(seats);
  out.push_back(static_cast<uint8_t>(ver & 0xFF));
  out.push_back(static_cast<uint8_t>(ver >> 8));
  const Bytes snapshot = snapshotOf(taps);
  out.insert(out.end(), snapshot.begin(), snapshot.end());
  return out;
}

// A game of five taps that logs its setup and every draw, so a resumed match shows which snapshot it drew and whether
// setup ran (ContinueLauncherTest's).
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

std::string resumePath(const std::string& id) { return "/.games-data/" + id + "/resume.bin"; }

class TitleScreenTest : public match::ScreenTest {
 protected:
  void SetUp() override {
    ScreenTest::SetUp();
    installerscript::reset();
    hostcaps::reset();
    GamesLauncherActivity::forgetOpenedGame();
    ButtonNavigator::setMappedInputManager(*input);
  }

  void TearDown() override {
    oom::failSize = 0;
    fakertos::release();  // a test that failed while holding the VM must not leave it held
    dropTitle();
    if (list) {
      activityManager.exitHolding(*list);
      activityManager.destroyHolding(list);
    }
    dropMatch();
    GamesLauncherActivity::forgetOpenedGame();
    ScreenTest::TearDown();
  }

  // What the manager does when a screen goes: onExit under the lock, then the destructor under it.
  void dropTitle() {
    for (auto& pushed : activityManager.pushedActivities) {
      if (pushed.get() == title) activityManager.exitHolding(*pushed);
      activityManager.destroyHolding(pushed);
    }
    activityManager.pushedActivities.clear();
    title = nullptr;
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

  // The counter fixture as an installed game whose manifest offers `modes` for 1..`seatsMax` seats. Two seats by
  // default, so a manifest that offers pass is valid (Manifest::check: pass needs seats.max 2).
  static void installCounter(const std::string& modes, const int seatsMax = 2) {
    match::installFixture("counter");
    fakesd::addFile("/.games/counter/manifest.json", manifestJson("counter", "Counter", modes, 1, seatsMax));
    fakesd::addFile("/.games/counter/.pkg", PKG);
  }
  // An installed game of the counting Lua.
  static void addCountingGame(const std::string& id, const std::string& name, const std::string& modes,
                              const int seatsMin = 1, const int seatsMax = 2) {
    const std::string dir = "/.games/" + id;
    fakesd::addFile(dir + "/manifest.json", manifestJson(id, name, modes, seatsMin, seatsMax));
    fakesd::addFile(dir + "/main.lua", std::string(COUNTING_GAME));
    fakesd::addFile(dir + "/.pkg", PKG);
  }
  // A save of game `id`'s match, {taps = 2} at ver 3, in `mode` with `seats` seats.
  static void save(const std::string& id, const uint8_t mode = SAVED_SOLO, const uint8_t seats = 1) {
    fakesd::addFile(resumePath(id), resumeBytes(2, 3, mode, seats));
  }

  // ---- the screens ----

  void openLauncher() {
    list = std::make_unique<GamesLauncherActivity>(*renderer, *input);
    current = list.get();
    current->onEnter();  // through the base: the launcher's own onEnter is private
    render();
  }

  // The title screen as the launcher builds it, for game `id` (seats 1..`seatsMax`) whose check left `modes` and whose
  // package is PKG's, opened as the manager would (onEnter), and drawn. The modes are given rather than checked, so a
  // nearby row (which a host without the radio never leaves) can be drawn.
  void openTitleFor(const uint8_t modes, const int seatsMax = 2, const std::string& id = "counter",
                    const std::string& name = "Counter") {
    GameRegistry::Entry game;
    game.manifest = parsed(manifestJson(id, name, "\"solo\"", 1, seatsMax));
    game.check = GameCore::CheckResult{GameCore::CheckStatus::Ok, GameCore::CheckReason::None, modes};
    std::copy(HASH, HASH + GamePkg::HASH_BYTES, game.pkgHash);
    auto made = std::make_unique<GameModeActivity>(*renderer, *input, game);
    title = made.get();
    activityManager.pushedActivities.push_back(std::move(made));
    startTitle();
  }

  // The title screen the launcher pushed.
  void openPushedTitle() {
    ASSERT_EQ(activityManager.pushedActivities.size(), 1u);
    title = dynamic_cast<GameModeActivity*>(activityManager.pushedActivities.back().get());
    ASSERT_NE(title, nullptr) << "what the launcher pushes is a GameModeActivity";
    startTitle();
  }

  // The launcher, a tap on `game`'s own row, and the title screen it pushed, opened. The own row is the last one drawn
  // with the game's name: the launcher's Continue row of a solo save (until entry 8) is drawn above it.
  void openTitleThroughLauncher(const std::string& game) {
    openLauncher();
    const screen::DrawnText* own = nullptr;
    for (const screen::DrawnText& drawn : ui().drawn)
      if (drawn.text == game) own = &drawn;
    ASSERT_NE(own, nullptr) << "the launcher does not draw a row \"" << game << "\": " << ui().joined();
    input->tap(own->rect.x + own->rect.width / 2, own->rect.y + own->rect.height / 2);
    ++taps;
    frame();
    ASSERT_NO_FATAL_FAILURE(openPushedTitle());
  }

  void startTitle() {
    current = title;
    current->onEnter();  // through the base: the title screen's own onEnter is private
    render();
  }

  void render() {
    if (screen::RecordingTarget::newest()) screen::RecordingTarget::newest()->forget();
    UITheme::getInstance().getTheme().calls.clear();
    current->render(RenderLock(*current));
    activityManager.markRendered();
  }
  void frame() {
    current->loop();
    input->clear();
  }
  screen::RecordingTarget& ui() {
    if (!screen::RecordingTarget::newest()) {
      std::fprintf(stderr, "no screen has been drawn\n");
      std::abort();
    }
    return *screen::RecordingTarget::newest();
  }
  ThemeDouble& theme() { return UITheme::getInstance().getTheme(); }

  void tapRow(const std::string& label) {
    for (const screen::DrawnText& drawn : ui().drawn) {
      if (drawn.text != label) continue;
      input->tap(drawn.rect.x + drawn.rect.width / 2, drawn.rect.y + drawn.rect.height / 2);
      ++taps;
      frame();
      return;
    }
    FAIL() << "the screen does not draw a row \"" << label << "\": " << ui().joined();
  }

  // The rows the screen drew, top to bottom: Continue and the mode names.
  std::vector<std::string> rowsDrawn() {
    static const std::vector<std::string> names{tr(STR_GAMES_CONTINUE), tr(STR_GAMES_MODE_SOLO),
                                                tr(STR_GAMES_MODE_PASS), tr(STR_GAMES_MODE_NEARBY)};
    std::vector<std::string> found;
    for (const screen::DrawnText& drawn : ui().drawn)
      for (const std::string& name : names)
        if (drawn.text == name) found.push_back(name);
    return found;
  }
  bool dialogUp() { return ui().drewLine(tr(STR_GAMES_NEW_OVER_SAVE_TITLE)); }

  // Runs the match a screen replaced itself with, so the game it was given is the one that starts.
  GameMatchActivity* enterReplacement() {
    if (activityManager.replacements.empty()) return nullptr;
    auto* match = dynamic_cast<GameMatchActivity*>(activityManager.replacements.back().get());
    if (match) {
      match->onEnter();
      entered = match;
    }
    return match;
  }

  // Waits for the started match's first round, then draws the match: true when its frame drew `text`.
  bool matchDrew(GameMatchActivity& match, const std::string& text) {
    if (!match::waitFor([] { return match::roundStarted(); })) return false;
    renderer->forget();
    match.render(RenderLock(match));
    const std::vector<std::string> texts = match::drawnTexts(*renderer);
    return std::find(texts.begin(), texts.end(), text) != texts.end();
  }

  // Taps canvas point (x, y) of the started match (the canvas sits at (3, 6) on the harness screen) and runs its loop
  // until it asks to draw the frame the VM published after the tap.
  bool tapMatchCanvas(GameMatchActivity& match, const int x, const int y) {
    activityManager.markRendered();
    input->tap(3 + x, 6 + y);
    return match::waitFor([&] {
      match.loop();
      input->clear();
      return activityManager.updateRequested();
    });
  }

  // Runs the entered match's loop until the log says `part`.
  bool pumpMatchTo(const std::string& part) {
    return match::waitFor([&] {
      entered->loop();
      input->clear();
      return logHas(part);
    });
  }

  // Plays the entered counting game to its end: five taps, each once the frame before it is drawn, then the match's
  // Over.
  bool playCountingGameToOver() {
    for (int tap = 1; tap <= 5; ++tap) {
      // The match drops a tap until its frame is on the panel.
      const bool frameDue = match::waitFor([&] {
        entered->loop();
        input->clear();
        return activityManager.updateRequested();
      });
      if (!frameDue) return false;
      entered->render(RenderLock(*entered));
      activityManager.markRendered();
      input->tap(3 + 50, 6 + 50);
      if (!pumpMatchTo("draw\t" + std::to_string(tap))) return false;
    }
    return pumpMatchTo("-> Over");
  }

  std::unique_ptr<GamesLauncherActivity> list;
  GameModeActivity* title = nullptr;  // owned by activityManager.pushedActivities
  Activity* current = nullptr;        // the screen frame() and render() drive
  Activity* entered = nullptr;        // the replacement enterReplacement() started, which needs its onExit
  int taps = 0;                       // the taps tapRow made
};

// ---- the launcher: every startable game row opens the title screen ----

TEST_F(TitleScreenTest, ASoloGameRowPushesItsTitleScreen) {
  installCounter("\"solo\"", 1);
  openLauncher();
  tapRow("Counter");
  EXPECT_EQ(activityManager.asks.pushed, 1) << "pushed, so Back returns to the launcher as it was";
  EXPECT_EQ(activityManager.asks.replaced, 0) << "no match yet: a game with one mode has its title screen too";
  EXPECT_EQ(activityManager.asks.goHome, 0);
  ASSERT_NO_FATAL_FAILURE(openPushedTitle());
  EXPECT_EQ(NameOf::of(*title), std::string(GameModeActivity::NAME));
  EXPECT_TRUE(theme().drew("drawHeader", "Counter")) << "the screen is that game's: its name heads it";
}

TEST_F(TitleScreenTest, ConfirmOnTheLauncherRowPushesTheTitleScreenToo) {
  installCounter("\"solo\",\"pass\"");
  openLauncher();
  input->click(Button::Confirm);
  frame();
  EXPECT_EQ(activityManager.asks.pushed, 1);
  EXPECT_EQ(activityManager.asks.replaced, 0);
}

TEST_F(TitleScreenTest, APassOnlyGameRowPushesItsTitleScreenWithItsOnePassRow) {
  installCounter("\"pass\"");
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  const std::vector<std::string> expected{tr(STR_GAMES_MODE_PASS)};
  EXPECT_EQ(rowsDrawn(), expected);
}

TEST_F(TitleScreenTest, TheRowsAreTheModesCheckLeavesForThisHost) {
  installCounter("\"solo\",\"pass\",\"nearby\"");  // nearby stays off: the host has no radio
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  const std::vector<std::string> expected{tr(STR_GAMES_MODE_SOLO), tr(STR_GAMES_MODE_PASS)};
  EXPECT_EQ(rowsDrawn(), expected) << "solo first, then pass; no nearby row: the host cannot start it";
}

TEST_F(TitleScreenTest, ASoloAndPassGameIsOneSoloRowWhileTheHostHasNoPass) {
  hostcaps::script().pass = false;  // a host without pass: Manifest::check leaves solo
  installCounter("\"solo\",\"pass\"");
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  const std::vector<std::string> expected{tr(STR_GAMES_MODE_SOLO)};
  EXPECT_EQ(rowsDrawn(), expected);
}

// A pass-only game with one seat is an invalid manifest (pass needs seats.max 2): its row says why, and a tap opens
// nothing and repaints the list.
TEST_F(TitleScreenTest, APassOnlyGameWithOneSeatOpensNothingFromTheLauncher) {
  installCounter("\"pass\"", 1);
  openLauncher();
  EXPECT_TRUE(logHas("Unavailable counter: pass and nearby need seats.max 2 or more"));
  const std::vector<screen::DrawnText>& drawn = ui().drawn;
  const auto row =
      std::find_if(drawn.begin(), drawn.end(), [](const screen::DrawnText& d) { return d.text == "Counter"; });
  ASSERT_NE(row, drawn.end()) << ui().joined();
  ASSERT_NE(row + 1, drawn.end()) << ui().joined();
  EXPECT_EQ((row + 1)->text, tr(STR_GAMES_UNAVAILABLE_INVALID));
  tapRow("Counter");
  EXPECT_EQ(activityManager.asks.replaced, 0);
  EXPECT_EQ(activityManager.asks.pushed, 0);
  EXPECT_TRUE(logHas("Not starting counter: pass and nearby need seats.max 2 or more"));
  EXPECT_TRUE(activityManager.updateRequested());
}

TEST_F(TitleScreenTest, AGameThisHostCannotStartOpensNeitherTheMatchNorTheTitleScreen) {
  match::installFixture("counter");
  fakesd::addFile("/.games/counter/manifest.json",
                  "{\"id\":\"counter\",\"name\":\"Counter\",\"version\":\"1.0.0\",\"api\":2,"
                  "\"seats\":{\"min\":1,\"max\":2},\"modes\":[\"solo\",\"pass\"]}");
  fakesd::addFile("/.games/counter/.pkg", PKG);
  openLauncher();
  tapRow("Counter");
  EXPECT_EQ(activityManager.asks.pushed, 0);
  EXPECT_EQ(activityManager.asks.replaced, 0);
  EXPECT_TRUE(logHas("Not starting counter: "));
}

// The launcher's Continue rows stay until entry 8: a tap on one resumes at once, with no title screen.
TEST_F(TitleScreenTest, TheLaunchersContinueRowStillResumesDirectly) {
  addCountingGame("alpha", "Alpha", "\"solo\"", 1, 1);
  save("alpha");
  openLauncher();
  tapRow(tr(STR_GAMES_CONTINUE));
  EXPECT_EQ(activityManager.asks.pushed, 0);
  ASSERT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);
  ASSERT_TRUE(pumpMatchTo("Resuming at ver 3"));
}

// Home → Games is one tap; the game's row and a start are the other two.
TEST_F(TitleScreenTest, ASoloGameAndAPassGameEachStartInTwoTapsFromTheLauncher) {
  addCountingGame("one-solo", "One solo", "\"solo\"", 1, 1);
  addCountingGame("two-pass", "Two pass", "\"pass\"", 2, 2);
  openLauncher();
  tapRow("One solo");
  ASSERT_NO_FATAL_FAILURE(openPushedTitle());
  tapRow(tr(STR_GAMES_MODE_SOLO));
  EXPECT_EQ(taps, 2);
  ASSERT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started one-solo"));
  EXPECT_TRUE(logHas("Mode solo picked for one-solo"));

  // The second game, from a launcher built afresh (goToGames() after the match is left).
  match::letStartedMatchesGo([this] { dropMatch(); }, match::Saves::Discard);
  dropTitle();
  activityManager.exitHolding(*list);
  activityManager.destroyHolding(list);
  activityManager.reset();
  theme().reset();
  taps = 0;
  openLauncher();
  tapRow("Two pass");
  ASSERT_NO_FATAL_FAILURE(openPushedTitle());
  tapRow(tr(STR_GAMES_MODE_PASS));
  EXPECT_EQ(taps, 2);
  ASSERT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started two-pass"));
  EXPECT_TRUE(logHas("Mode pass picked for two-pass: 2 seats"));
  // The match was given the pass roster: its first snapshot is saved as mode 1, n 2.
  ASSERT_TRUE(match::waitFor([&] {
    entered->loop();
    input->clear();
    return fakesd::bytesOf(resumePath("two-pass")).size() > 15;
  }));
  const Bytes firstSave = fakesd::bytesOf(resumePath("two-pass"));
  EXPECT_EQ(firstSave[14], SAVED_PASS);
  EXPECT_EQ(firstSave[15], 2u);
}

// ---- the screen: its header and rows ----

TEST_F(TitleScreenTest, WithNoSaveTheHeaderIsTheGamesNameAndTheRowsAreItsModes) {
  installCounter("\"solo\",\"pass\"");
  openTitleFor(SOLO | PASS);
  EXPECT_TRUE(theme().drew("drawHeader", "Counter"));
  EXPECT_FALSE(theme().drew("drawHeader", tr(STR_GAMES_MODE_TITLE))) << "\"Choose a mode\" goes";
  const std::vector<std::string> expected{tr(STR_GAMES_MODE_SOLO), tr(STR_GAMES_MODE_PASS)};
  EXPECT_EQ(rowsDrawn(), expected) << "no Continue row: there is no save";
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_MODE_SOLO_DESC)));
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_MODE_PASS_DESC)));
  EXPECT_FALSE(ui().drewLine(tr(STR_GAMES_MODE_NEARBY_DESC)));
  ASSERT_FALSE(theme().hints.empty());
  EXPECT_EQ(theme().hints.back().btn1, tr(STR_BACK));
  EXPECT_EQ(theme().hints.back().btn2, tr(STR_SELECT));
}

TEST_F(TitleScreenTest, AllThreeModesAreThreeRowsInTheOrderSoloPassNearby) {
  openTitleFor(SOLO | PASS | NEARBY);
  const std::vector<std::string> expected{tr(STR_GAMES_MODE_SOLO), tr(STR_GAMES_MODE_PASS), tr(STR_GAMES_MODE_NEARBY)};
  EXPECT_EQ(rowsDrawn(), expected);
}

TEST_F(TitleScreenTest, OnlyTheModesGivenAreRows) {
  openTitleFor(PASS | NEARBY);
  const std::vector<std::string> expected{tr(STR_GAMES_MODE_PASS), tr(STR_GAMES_MODE_NEARBY)};
  EXPECT_EQ(rowsDrawn(), expected);
}

TEST_F(TitleScreenTest, ASoloSaveIsAContinueRowFirstAndSaysWhatItDoes) {
  addCountingGame("counter", "Counter", "\"solo\",\"pass\"");
  save("counter");
  openTitleFor(SOLO | PASS);
  const std::vector<std::string> expected{tr(STR_GAMES_CONTINUE), tr(STR_GAMES_MODE_SOLO), tr(STR_GAMES_MODE_PASS)};
  EXPECT_EQ(rowsDrawn(), expected);
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_CONTINUE_DESC)));
  EXPECT_TRUE(theme().drew("drawHeader", "Counter"));
}

TEST_F(TitleScreenTest, TheSaveIsPeekedOnceWhenTheScreenOpensAndNotWhileItIsDrawn) {
  addCountingGame("counter", "Counter", "\"solo\"", 1, 1);
  save("counter");
  const std::string opens = "open " + resumePath("counter");
  const size_t before = fakesd::countOps(opens);
  openTitleFor(SOLO, 1);
  const size_t entered = fakesd::countOps(opens);
  EXPECT_EQ(entered, before + 1) << "the save was read once when the screen opened";
  for (int i = 0; i < 3; ++i) render();
  input->click(Button::NavNext);
  frame();
  render();
  EXPECT_EQ(fakesd::countOps(opens), entered) << "no read while the rows are drawn or the selection moves";
}

// ---- New with no save ----

TEST_F(TitleScreenTest, ATapOnSoloReplacesTheScreenWithANewSoloMatch) {
  installCounter("\"solo\",\"pass\"");
  openTitleFor(SOLO | PASS);
  tapRow(tr(STR_GAMES_MODE_SOLO));
  EXPECT_EQ(activityManager.asks.replaced, 1);
  EXPECT_EQ(activityManager.asks.popped, 0);
  ASSERT_NE(enterReplacement(), nullptr) << "what the title screen opens is a GameMatchActivity";
  EXPECT_TRUE(logHas("Started counter"));
  EXPECT_TRUE(logHas("Mode solo picked for counter"));
  EXPECT_FALSE(logHas("the match plays solo"));
}

// Rows are looked up by their place on the screen, not by the mode bit's place among all three.
TEST_F(TitleScreenTest, TheFirstRowOfAPassAndNearbyScreenStartsPass) {
  installCounter("\"pass\",\"nearby\"");
  openTitleFor(PASS | NEARBY);
  tapRow(tr(STR_GAMES_MODE_PASS));
  EXPECT_TRUE(logHas("Mode pass picked"));
  EXPECT_FALSE(logHas("Mode nearby picked"));
}

TEST_F(TitleScreenTest, TheSecondRowOfAPassAndNearbyScreenStartsNearbyAsSolo) {
  installCounter("\"pass\",\"nearby\"");
  openTitleFor(PASS | NEARBY);
  tapRow(tr(STR_GAMES_MODE_NEARBY));
  EXPECT_TRUE(logHas("Mode nearby picked for counter: the match plays solo until it can run nearby"));
  EXPECT_FALSE(logHas("Mode pass picked"));
}

// The Pass row starts an open pass match with the fewest seats it can have (two), and seat 1 is drawn first.
TEST_F(TitleScreenTest, ATapOnPassStartsATwoSeatPassMatchThatDrawsSeatOneFirst) {
  match::installFixture("pass-open");
  fakesd::addFile("/.games/pass-open/.pkg", PKG);
  openTitleFor(SOLO | PASS, 2, "pass-open", "Pass open");
  tapRow(tr(STR_GAMES_MODE_PASS));
  EXPECT_EQ(activityManager.asks.replaced, 1);
  GameMatchActivity* match = enterReplacement();
  ASSERT_NE(match, nullptr);
  EXPECT_TRUE(logHas("Mode pass picked for pass-open: 2 seats"));
  EXPECT_TRUE(logHas("Started pass-open"));
  EXPECT_FALSE(logHas("plays solo"));
  EXPECT_TRUE(matchDrew(*match, "Player 1 (X) to move"));
  // Seat 1 takes cell 1 (centred at canvas (97, 270)): seat 2's board follows, which no solo match draws.
  ASSERT_TRUE(tapMatchCanvas(*match, 97, 270));
  EXPECT_TRUE(matchDrew(*match, "Player 2 (O) to move"));
}

// ## 5.2: the pass row's seat guard (passSeats 0), reached on a one-seat host, where Manifest::check still leaves pass
// for a 1..2 game: the tap starts nothing, is logged, and repaints the screen.
TEST_F(TitleScreenTest, APassThatCannotFitStartsNothing) {
  hostcaps::script().maxSeats = 1;
  installCounter("\"solo\",\"pass\"");
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  const std::vector<std::string> expected{tr(STR_GAMES_MODE_SOLO), tr(STR_GAMES_MODE_PASS)};
  ASSERT_EQ(rowsDrawn(), expected) << "check leaves pass on a one-seat host";
  activityManager.markRendered();
  tapRow(tr(STR_GAMES_MODE_PASS));
  EXPECT_EQ(activityManager.asks.replaced, 0);
  EXPECT_TRUE(activityManager.replacements.empty());
  EXPECT_TRUE(logHas("Cannot start counter in pass: seats 1..2 leave no pass match on this host"));
  EXPECT_FALSE(logHas("Mode pass picked"));
  EXPECT_TRUE(activityManager.updateRequested());
}

TEST_F(TitleScreenTest, ConfirmStartsTheSelectedRowAndNextMovesTheSelection) {
  installCounter("\"solo\",\"pass\"");
  openTitleFor(SOLO | PASS);
  input->click(Button::NavNext);
  frame();
  input->click(Button::Confirm);
  frame();
  EXPECT_EQ(activityManager.asks.replaced, 1);
  EXPECT_TRUE(logHas("Mode pass picked"));
}

TEST_F(TitleScreenTest, ConfirmWithNothingMovedStartsTheFirstRow) {
  installCounter("\"solo\",\"pass\"");
  openTitleFor(SOLO | PASS);
  input->click(Button::Confirm);
  frame();
  EXPECT_EQ(activityManager.asks.replaced, 1);
  EXPECT_TRUE(logHas("Mode solo picked"));
}

TEST_F(TitleScreenTest, AMatchThatCannotBeAllocatedStartsNothingAndRepaints) {
  installCounter("\"solo\"", 1);
  openTitleFor(SOLO, 1);
  oom::failSize = sizeof(GameMatchActivity);
  activityManager.markRendered();
  tapRow(tr(STR_GAMES_MODE_SOLO));
  EXPECT_EQ(activityManager.asks.replaced, 0);
  EXPECT_TRUE(logHas("OOM: "));
  EXPECT_TRUE(activityManager.updateRequested()) << "the tap flash was cleared: the screen is drawn again";
}

// ---- Continue ----

TEST_F(TitleScreenTest, ConfirmWithNothingMovedResumesTheSoloSave) {
  addCountingGame("counter", "Counter", "\"solo\"", 1, 1);
  save("counter");
  openTitleFor(SOLO, 1);
  input->click(Button::Confirm);  // the selection starts on Continue: a stray Confirm never starts New over the save
  frame();
  ASSERT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Continue counter: a solo match"));
  ASSERT_TRUE(pumpMatchTo("Round started at ver 3"));
  EXPECT_TRUE(logHas("Resuming at ver 3"));
  EXPECT_TRUE(logHas("draw\t2")) << "the saved snapshot is what is drawn";
  EXPECT_FALSE(logHas("setup ran"));
}

// A game that can start solo and pass resumes solo when the solo-only peek finds a valid solo save.
TEST_F(TitleScreenTest, ATapOnContinueResumesATwoModeGamesSoloSaveSolo) {
  addCountingGame("counter", "Counter", "\"solo\",\"pass\"");
  save("counter");
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  tapRow(tr(STR_GAMES_CONTINUE));
  ASSERT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Continue counter: a solo match (solo-only peek: valid)"));
  ASSERT_TRUE(pumpMatchTo("Resuming at ver 3"));
  EXPECT_FALSE(logHas("pass match"));
}

// A pass save (mode 1, n 2) gets its Continue row, and the tap resumes it as a pass match: the saved snapshot is drawn,
// setup never runs, and the save is unchanged through the match and its exit (a restored snapshot is not written
// again).
TEST_F(TitleScreenTest, ContinueOnAPassSaveResumesItAsAPassMatch) {
  addCountingGame("counter", "Counter", "\"solo\",\"pass\"");
  save("counter", SAVED_PASS, 2);
  const Bytes saved = fakesd::bytesOf(resumePath("counter"));
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  const std::vector<std::string> expected{tr(STR_GAMES_CONTINUE), tr(STR_GAMES_MODE_SOLO), tr(STR_GAMES_MODE_PASS)};
  ASSERT_EQ(rowsDrawn(), expected) << "the pass save is offered";
  tapRow(tr(STR_GAMES_CONTINUE));
  ASSERT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Continue counter: a 2-seat pass match (solo-only peek: none)"));
  EXPECT_TRUE(logHas("counter: resuming the save's roster: pass, 2 seat(s)"));
  ASSERT_TRUE(pumpMatchTo("Round started at ver 3"));
  EXPECT_TRUE(logHas("Resuming at ver 3"));
  EXPECT_TRUE(logHas("draw\t2")) << "the saved snapshot is what is drawn";
  EXPECT_FALSE(logHas("setup ran"));
  for (int i = 0; i < 20; ++i) {
    entered->loop();
    input->clear();
  }
  EXPECT_EQ(fakesd::bytesOf(resumePath("counter")), saved) << "through play";
  match::letStartedMatchesGo([this] { dropMatch(); }, match::Saves::Keep);  // Leave: the forced exit
  ASSERT_TRUE(fakesd::has(resumePath("counter")));
  EXPECT_EQ(fakesd::bytesOf(resumePath("counter")), saved) << "and after the exit";
}

// A pass-only game's pass save resumes as a pass match too.
TEST_F(TitleScreenTest, ContinueOnAPassOnlyGamesSaveResumesIt) {
  addCountingGame("counter", "Counter", "\"pass\"", 2, 2);
  save("counter", SAVED_PASS, 2);
  const Bytes saved = fakesd::bytesOf(resumePath("counter"));
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  tapRow(tr(STR_GAMES_CONTINUE));
  ASSERT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Continue counter: a 2-seat pass match (solo-only peek: -)"));
  ASSERT_TRUE(pumpMatchTo("Resuming at ver 3"));
  EXPECT_TRUE(logHas("counter: resuming the save's roster: pass, 2 seat(s)"));
  EXPECT_FALSE(logHas("setup ran"));
  match::letStartedMatchesGo([this] { dropMatch(); }, match::Saves::Keep);
  EXPECT_EQ(fakesd::bytesOf(resumePath("counter")), saved);
}

// The carried guard (## 4.12): a save the card will not read keeps its Continue row, and a solo-only game's tap ends in
// the match's error view, never a new match over it.
TEST_F(TitleScreenTest, ASoloGamesUnreadableSaveIsOfferedAndItsContinueEndsInTheErrorView) {
  addCountingGame("counter", "Counter", "\"solo\"", 1, 1);
  save("counter");
  const Bytes saved = fakesd::bytesOf(resumePath("counter"));
  fakesd::sim().failOpen.insert(resumePath("counter"));
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  const std::vector<std::string> expected{tr(STR_GAMES_CONTINUE), tr(STR_GAMES_MODE_SOLO)};
  ASSERT_EQ(rowsDrawn(), expected);
  tapRow(tr(STR_GAMES_CONTINUE));
  ASSERT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Continue counter: a solo match"));
  EXPECT_TRUE(logHas("resume.bin could not be read; not starting a new match over it"));
  EXPECT_FALSE(logHas("setup ran"));
  EXPECT_EQ(fakesd::bytesOf(resumePath("counter")), saved);
}

// A two-mode game's Unreadable save may be a pass save, which a solo match would replace: its Continue starts a pass
// match, which stops in the error view as any save that will not read does, and the file's bytes are unchanged through
// the match and its exit.
TEST_F(TitleScreenTest, ATwoModeGamesUnreadableSaveEndsInTheErrorViewAndIsLeftAlone) {
  addCountingGame("counter", "Counter", "\"solo\",\"pass\"");
  save("counter", SAVED_PASS, 2);
  const Bytes saved = fakesd::bytesOf(resumePath("counter"));
  fakesd::sim().failReadAt[resumePath("counter")] = 0;
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  const std::vector<std::string> expected{tr(STR_GAMES_CONTINUE), tr(STR_GAMES_MODE_SOLO), tr(STR_GAMES_MODE_PASS)};
  ASSERT_EQ(rowsDrawn(), expected);
  tapRow(tr(STR_GAMES_CONTINUE));
  ASSERT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Continue counter: a 2-seat pass match (solo-only peek: unreadable)"));
  EXPECT_FALSE(logHas("a solo match"));
  EXPECT_TRUE(logHas("resume.bin could not be read; not starting a new match over it"));
  EXPECT_TRUE(logHas("counter: Starting -> Error on ScriptError"));
  for (int i = 0; i < 20; ++i) {
    entered->loop();
    input->clear();
  }
  EXPECT_FALSE(logHas("setup ran")) << "no game ran";
  match::letStartedMatchesGo([this] { dropMatch(); }, match::Saves::Keep);
  EXPECT_EQ(fakesd::bytesOf(resumePath("counter")), saved);
}

// ---- New over a save: a second confirm, Cancel focused ----

TEST_F(TitleScreenTest, ANewRowOverASaveAsksFirstAndATapOnCancelKeepsTheSave) {
  addCountingGame("counter", "Counter", "\"solo\",\"pass\"");
  save("counter");
  const Bytes saved = fakesd::bytesOf(resumePath("counter"));
  openTitleFor(SOLO | PASS);
  tapRow(tr(STR_GAMES_MODE_SOLO));
  EXPECT_EQ(activityManager.asks.replaced, 0) << "nothing starts before the answer";
  render();
  ASSERT_TRUE(dialogUp());
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_NEW_OVER_SAVE)));
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_NEW_GAME)));
  EXPECT_TRUE(ui().drewLine(tr(STR_CANCEL)));
  tapRow(tr(STR_CANCEL));
  render();
  EXPECT_FALSE(dialogUp());
  const std::vector<std::string> expected{tr(STR_GAMES_CONTINUE), tr(STR_GAMES_MODE_SOLO), tr(STR_GAMES_MODE_PASS)};
  EXPECT_EQ(rowsDrawn(), expected) << "the screen is as it was";
  EXPECT_EQ(activityManager.asks.replaced, 0);
  EXPECT_EQ(fakesd::bytesOf(resumePath("counter")), saved);
}

TEST_F(TitleScreenTest, BackAndAConfirmOnTheFocusedCancelCloseTheQuestionAndKeepTheSave) {
  addCountingGame("counter", "Counter", "\"solo\"", 1, 1);
  save("counter");
  openTitleFor(SOLO, 1);
  tapRow(tr(STR_GAMES_MODE_SOLO));
  render();
  ASSERT_TRUE(dialogUp());
  input->click(Button::Back);
  frame();
  render();
  EXPECT_FALSE(dialogUp());
  EXPECT_EQ(activityManager.asks.popped, 0) << "Back closes the question, not the screen";

  tapRow(tr(STR_GAMES_MODE_SOLO));
  render();
  ASSERT_TRUE(dialogUp());
  input->click(Button::Confirm);  // the focus starts on Cancel
  frame();
  render();
  EXPECT_FALSE(dialogUp());
  EXPECT_EQ(activityManager.asks.replaced, 0);
  EXPECT_TRUE(fakesd::has(resumePath("counter")));
}

TEST_F(TitleScreenTest, NewGameStartsANewMatchInTheModeTapped) {
  addCountingGame("counter", "Counter", "\"solo\",\"pass\"");
  save("counter");
  openTitleFor(SOLO | PASS);
  tapRow(tr(STR_GAMES_MODE_PASS));
  render();
  ASSERT_TRUE(dialogUp());
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_MODE_PASS))) << "the question names the mode";
  tapRow(tr(STR_GAMES_NEW_GAME));
  ASSERT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Mode pass picked for counter: 2 seats"));
  EXPECT_FALSE(logHas("Continue counter"));
}

TEST_F(TitleScreenTest, TheDirectionKeysMoveTheFocusAndConfirmOnNewGameStartsNew) {
  addCountingGame("counter", "Counter", "\"solo\"", 1, 1);
  save("counter");
  openTitleFor(SOLO, 1);
  input->click(Button::NavNext);  // the Solo row, under Continue
  frame();
  input->click(Button::Confirm);
  frame();
  render();
  ASSERT_TRUE(dialogUp());
  input->click(Button::Down);
  frame();
  input->click(Button::Up);  // back to Cancel
  frame();
  input->click(Button::Down);
  frame();
  input->click(Button::Confirm);
  frame();
  ASSERT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Mode solo picked for counter"));
  ASSERT_TRUE(pumpMatchTo("setup ran")) << "a New match";
}

// The hit rects are the last render's: a tap on a row after the question opens, before it is drawn, starts nothing.
TEST_F(TitleScreenTest, ATapOnARowBeforeTheQuestionIsDrawnStartsNothing) {
  addCountingGame("counter", "Counter", "\"solo\",\"pass\"");
  save("counter");
  openTitleFor(SOLO | PASS);
  tapRow(tr(STR_GAMES_MODE_SOLO));  // opens the question; the screen is not drawn again
  tapRow(tr(STR_GAMES_MODE_PASS));
  tapRow(tr(STR_GAMES_CONTINUE));
  EXPECT_EQ(activityManager.asks.replaced, 0);
  render();
  EXPECT_TRUE(dialogUp()) << "still asking";
  // The stale taps moved no selection: closed, a Confirm asks about Solo again (the question names its mode).
  input->click(Button::Back);
  frame();
  input->click(Button::Confirm);
  frame();
  EXPECT_EQ(activityManager.asks.replaced, 0);
  render();
  ASSERT_TRUE(dialogUp());
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_MODE_SOLO)));
  EXPECT_FALSE(ui().drewLine(tr(STR_GAMES_MODE_PASS)));
}

// An Unreadable save asks too: it may be a good save.
TEST_F(TitleScreenTest, ANewRowOverAnUnreadableSaveAsksFirst) {
  addCountingGame("counter", "Counter", "\"solo\"", 1, 1);
  save("counter");
  fakesd::sim().failOpen.insert(resumePath("counter"));
  openTitleFor(SOLO, 1);
  tapRow(tr(STR_GAMES_MODE_SOLO));
  EXPECT_EQ(activityManager.asks.replaced, 0);
  render();
  EXPECT_TRUE(dialogUp());
}

// The Continue tap's pass seat guard: a two-mode game's Unreadable save on a one-seat host (where check still leaves
// pass for a 1..2 game) has no pass match to start. Nothing starts, the screen is drawn again, and the save is kept.
TEST_F(TitleScreenTest, AContinueThatCannotFitAPassMatchStartsNothing) {
  hostcaps::script().maxSeats = 1;
  addCountingGame("counter", "Counter", "\"solo\",\"pass\"");
  save("counter");
  const Bytes saved = fakesd::bytesOf(resumePath("counter"));
  fakesd::sim().failOpen.insert(resumePath("counter"));
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  activityManager.markRendered();
  tapRow(tr(STR_GAMES_CONTINUE));
  EXPECT_EQ(activityManager.asks.replaced, 0);
  EXPECT_TRUE(logHas("Cannot continue counter in pass: seats 1..2 leave no pass match on this host"));
  EXPECT_TRUE(activityManager.updateRequested());
  EXPECT_EQ(fakesd::bytesOf(resumePath("counter")), saved);
}

TEST_F(TitleScreenTest, ATitleScreenThatCannotBeAllocatedOpensNothingAndRepaintsTheLauncher) {
  installCounter("\"solo\"", 1);
  openLauncher();
  oom::failSize = sizeof(GameModeActivity);
  activityManager.markRendered();
  tapRow("Counter");
  EXPECT_EQ(activityManager.asks.pushed, 0);
  EXPECT_EQ(activityManager.asks.replaced, 0);
  EXPECT_TRUE(logHas("OOM: " + std::to_string(sizeof(GameModeActivity)) + " byte title screen"));
  EXPECT_TRUE(activityManager.updateRequested());
}

TEST_F(TitleScreenTest, AContinueWhoseMatchCannotBeAllocatedStartsNothingAndKeepsTheSave) {
  addCountingGame("counter", "Counter", "\"solo\"", 1, 1);
  save("counter");
  const Bytes saved = fakesd::bytesOf(resumePath("counter"));
  openTitleFor(SOLO, 1);
  oom::failSize = sizeof(GameMatchActivity);
  activityManager.markRendered();
  tapRow(tr(STR_GAMES_CONTINUE));
  EXPECT_EQ(activityManager.asks.replaced, 0);
  EXPECT_TRUE(logHas("OOM: "));
  EXPECT_TRUE(activityManager.updateRequested());
  EXPECT_EQ(fakesd::bytesOf(resumePath("counter")), saved);
}

// The question through the launcher: a game with a save, its own row, and its title screen's New row.
TEST_F(TitleScreenTest, ANewRowReachedFromTheLauncherAsksFirstOverASave) {
  addCountingGame("counter", "Counter", "\"solo\"", 1, 1);
  save("counter");
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  tapRow(tr(STR_GAMES_MODE_SOLO));
  EXPECT_EQ(activityManager.asks.replaced, 0);
  render();
  EXPECT_TRUE(dialogUp());
}

// ---- Back ----

TEST_F(TitleScreenTest, BackPopsToTheLauncherAndDoesNotGoHomeOrStartAnything) {
  installCounter("\"solo\",\"pass\"");
  openTitleFor(SOLO | PASS);
  input->click(Button::Back);
  frame();
  EXPECT_EQ(activityManager.asks.popped, 1);
  EXPECT_EQ(activityManager.asks.goHome, 0);
  EXPECT_EQ(activityManager.asks.replaced, 0);
}

TEST_F(TitleScreenTest, TheScreenIsNamedGameModeAndNotTheLaunchersName) {
  openTitleFor(SOLO | PASS);
  EXPECT_EQ(NameOf::of(*title), std::string(GameModeActivity::NAME));
  EXPECT_STREQ(GameModeActivity::NAME, "GameMode") << "ledger row 5: goHome maps this name to Home's Games row";
  EXPECT_STRNE(GameModeActivity::NAME, GamesLauncherActivity::NAME);
}

// ---- the host-caps double (list_stubs/GameHostCapsDouble.cpp) ----

// The double stands in for the device's gameHostCaps(): after a reset it must answer what the device answers
// (GameHostCapsTest pins the real function to the same HostCapsValues), so a suite here sees the device's host.
TEST(HostCapsDouble, DefaultsAreTheDevicesValues) {
  hostcaps::script().pass = !HostCapsValues::PASS;
  hostcaps::script().minApi = API_MIN_LEVEL + 1;
  hostcaps::script().maxSeats = 1;
  hostcaps::reset();
  const GameCore::HostCaps caps = gameHostCaps();
  EXPECT_EQ(caps.api, API_LEVEL);
  EXPECT_EQ(caps.minApi, API_MIN_LEVEL);
  EXPECT_EQ(caps.maxSeats, HostCapsValues::MAX_SEATS);
  EXPECT_EQ(caps.nearby, HostCapsValues::NEARBY_BUILT);
  EXPECT_EQ(caps.pass, HostCapsValues::PASS);
}

}  // namespace
