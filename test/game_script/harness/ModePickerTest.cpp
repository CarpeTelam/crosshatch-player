#include <I18n.h>
#include <Manifest.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "HostCapsScript.h"
#include "InstallerScript.h"
#include "MatchSupport.h"
#include "activities/games/GameMatchActivity.h"
#include "activities/games/GameModeActivity.h"
#include "activities/games/GamesLauncherActivity.h"
#include "util/ButtonNavigator.h"

// The real GameModeActivity and the real GamesLauncherActivity that opens it (entry 9 of epic-install-and-launcher),
// over the screen doubles (screen_stubs/) and the scripted installer and host caps (list_stubs/), on the pattern of
// GamesLauncherTest.cpp: the fixture opens a screen, draws it, and finds a row by the text it drew. What the mode
// step promises (spine AD-22): a game with one startable mode goes straight to its match, a game with two or more asks,
// a pick starts the match, and Back returns to the launcher.

namespace {

using Button = MappedInputManager::Button;
using GameCore::Manifest;

const std::string PKG = "v1\n0530a15766e91bf1\n";

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

class PickerTest : public match::ScreenTest {
 protected:
  void SetUp() override {
    ScreenTest::SetUp();
    installerscript::reset();
    hostcaps::reset();
    ButtonNavigator::setMappedInputManager(*input);
  }

  void TearDown() override {
    fakertos::release();  // a test that failed while holding the VM must not leave it held
    // What the manager does when a screen goes: onExit under the lock, then the destructor under it.
    for (auto& pushed : activityManager.pushedActivities) {
      if (pushed.get() == picker) activityManager.exitHolding(*pushed);
      activityManager.destroyHolding(pushed);
    }
    activityManager.pushedActivities.clear();
    picker = nullptr;
    if (list) {
      activityManager.exitHolding(*list);
      activityManager.destroyHolding(list);
    }
    dropMatch();
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

  // The counter fixture as an installed game whose manifest offers `modes` for `seatsMax` seats.
  static void installCounter(const std::string& modes, const int seatsMax = 1) {
    match::installFixture("counter");
    fakesd::addFile("/.games/counter/manifest.json", manifestJson("counter", "Counter", modes, 1, seatsMax));
    fakesd::addFile("/.games/counter/.pkg", PKG);
  }

  // ---- the screens ----

  void openLauncher() {
    list = std::make_unique<GamesLauncherActivity>(*renderer, *input);
    current = list.get();
    current->onEnter();  // through the base: the launcher's own onEnter is private
    render();
  }

  // The picker as it is built for a game offering `modes`, opened as the manager would (onEnter), and drawn.
  void openPickerFor(const uint8_t modes) {
    auto made = std::make_unique<GameModeActivity>(*renderer, *input,
                                                   parsed(manifestJson("counter", "Counter", "\"solo\"", 1, 1)), modes);
    picker = made.get();
    activityManager.pushedActivities.push_back(std::move(made));
    startPicker();
  }

  // The picker the launcher pushed.
  void openPushedPicker() {
    ASSERT_EQ(activityManager.pushedActivities.size(), 1u);
    picker = dynamic_cast<GameModeActivity*>(activityManager.pushedActivities.back().get());
    ASSERT_NE(picker, nullptr) << "what the launcher pushes is a GameModeActivity";
    startPicker();
  }

  void startPicker() {
    picker->onEnter();
    current = picker;
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
      frame();
      return;
    }
    FAIL() << "the screen does not draw a row \"" << label << "\": " << ui().joined();
  }

  // The mode rows the screen drew, top to bottom.
  std::vector<std::string> modeRows() {
    static const std::vector<std::string> names{tr(STR_GAMES_MODE_SOLO), tr(STR_GAMES_MODE_PASS),
                                                tr(STR_GAMES_MODE_NEARBY)};
    std::vector<std::string> found;
    for (const screen::DrawnText& drawn : ui().drawn)
      for (const std::string& name : names)
        if (drawn.text == name) found.push_back(name);
    return found;
  }

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

  std::unique_ptr<GamesLauncherActivity> list;
  GameModeActivity* picker = nullptr;  // owned by activityManager.pushedActivities
  Activity* current = nullptr;         // the screen frame() and render() drive
  Activity* entered = nullptr;         // the replacement enterReplacement() started, which needs its onExit
};

// ---- the launcher: the mode step is skipped with one mode ----

TEST_F(PickerTest, AGameWithOneModeGoesStraightToItsMatch) {
  installCounter("\"solo\"");
  openLauncher();
  tapRow("Counter");
  EXPECT_EQ(activityManager.asks.pushed, 0) << "no picker for a single mode";
  EXPECT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started counter"));
}

TEST_F(PickerTest, ASoloAndPassGameHasOneModeWhileTheHostHasNoPassAndSkipsThePicker) {
  installCounter("\"solo\",\"pass\"", 2);  // the real host: pass is off, so Manifest::check leaves solo
  openLauncher();
  tapRow("Counter");
  EXPECT_EQ(activityManager.asks.pushed, 0);
  EXPECT_EQ(activityManager.asks.replaced, 1);
}

// The solo start of a game with no solo mode is a placeholder: epic-pass-and-play passes the mode in, and this test
// changes with it.
TEST_F(PickerTest, AGameWhoseOnlyStartableModeIsPassAlsoGoesStraightToItsMatch) {
  hostcaps::script().pass = true;
  installCounter("\"pass\"", 2);
  openLauncher();
  tapRow("Counter");
  EXPECT_EQ(activityManager.asks.pushed, 0);
  EXPECT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started counter")) << "today's match plays solo whatever the mode";
  EXPECT_TRUE(logHas("counter has no solo mode: the match plays solo until"));
}

TEST_F(PickerTest, AGameThisHostCannotStartOpensNeitherTheMatchNorThePicker) {
  hostcaps::script().pass = true;
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

// ---- the launcher: two modes open the picker ----

TEST_F(PickerTest, AGameWithTwoModesOpensThePickerAboveTheLauncher) {
  hostcaps::script().pass = true;
  installCounter("\"solo\",\"pass\"", 2);
  openLauncher();
  tapRow("Counter");
  EXPECT_EQ(activityManager.asks.pushed, 1) << "pushed, so Back returns to the launcher as it was";
  EXPECT_EQ(activityManager.asks.replaced, 0);
  EXPECT_EQ(activityManager.asks.goHome, 0);
  ASSERT_NO_FATAL_FAILURE(openPushedPicker());
  EXPECT_EQ(NameOf::of(*picker), std::string(GameModeActivity::NAME));
  const std::vector<std::string> expected{tr(STR_GAMES_MODE_SOLO), tr(STR_GAMES_MODE_PASS)};
  EXPECT_EQ(modeRows(), expected) << "solo first, then pass; no nearby row: the host cannot start it";
}

TEST_F(PickerTest, ConfirmOnTheLauncherRowOpensThePickerToo) {
  hostcaps::script().pass = true;
  installCounter("\"solo\",\"pass\"", 2);
  openLauncher();
  input->click(Button::Confirm);
  frame();
  EXPECT_EQ(activityManager.asks.pushed, 1);
  EXPECT_EQ(activityManager.asks.replaced, 0);
}

// ---- the picker ----

TEST_F(PickerTest, ThePickerNamesItsScreenAndEachRowSaysWhatItDoes) {
  installCounter("\"solo\",\"pass\"", 2);
  openPickerFor(SOLO | PASS);
  EXPECT_TRUE(theme().drew("drawHeader", tr(STR_GAMES_MODE_TITLE)));
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_MODE_SOLO_DESC)));
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_MODE_PASS_DESC)));
  EXPECT_FALSE(ui().drewLine(tr(STR_GAMES_MODE_NEARBY_DESC)));
  ASSERT_FALSE(theme().hints.empty());
  EXPECT_EQ(theme().hints.back().btn1, tr(STR_BACK));
  EXPECT_EQ(theme().hints.back().btn2, tr(STR_SELECT));
}

TEST_F(PickerTest, AllThreeModesAreThreeRowsInTheOrderSoloPassNearby) {
  openPickerFor(SOLO | PASS | NEARBY);
  const std::vector<std::string> expected{tr(STR_GAMES_MODE_SOLO), tr(STR_GAMES_MODE_PASS), tr(STR_GAMES_MODE_NEARBY)};
  EXPECT_EQ(modeRows(), expected);
}

TEST_F(PickerTest, AOneModePickerIsOneRowAndStartsTheMatch) {
  installCounter("\"solo\"");
  openPickerFor(SOLO);
  const std::vector<std::string> expected{tr(STR_GAMES_MODE_SOLO)};
  EXPECT_EQ(modeRows(), expected);
  tapRow(tr(STR_GAMES_MODE_SOLO));
  EXPECT_EQ(activityManager.asks.replaced, 1);
}

TEST_F(PickerTest, OnlyTheModesGivenAreRows) {
  openPickerFor(PASS | NEARBY);
  const std::vector<std::string> expected{tr(STR_GAMES_MODE_PASS), tr(STR_GAMES_MODE_NEARBY)};
  EXPECT_EQ(modeRows(), expected);
}

TEST_F(PickerTest, ATapOnSoloReplacesThePickerWithTheMatch) {
  installCounter("\"solo\",\"pass\"", 2);
  openPickerFor(SOLO | PASS);
  tapRow(tr(STR_GAMES_MODE_SOLO));
  EXPECT_EQ(activityManager.asks.replaced, 1);
  EXPECT_EQ(activityManager.asks.popped, 0);
  ASSERT_NE(enterReplacement(), nullptr) << "what the picker opens is a GameMatchActivity";
  EXPECT_TRUE(logHas("Started counter"));
  EXPECT_TRUE(logHas("Mode solo picked for counter"));
  EXPECT_FALSE(logHas("plays solo until"));
}

// A placeholder like the test above: epic-pass-and-play passes the picked mode into the match.
TEST_F(PickerTest, ATapOnPassStartsTheSoloMatchAndSaysSo) {
  installCounter("\"solo\",\"pass\"", 2);
  openPickerFor(SOLO | PASS);
  tapRow(tr(STR_GAMES_MODE_PASS));
  EXPECT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started counter"));
  EXPECT_TRUE(logHas("Mode pass picked for counter: the match plays solo until"));
}

TEST_F(PickerTest, ConfirmStartsTheSelectedRowAndNextMovesTheSelection) {
  installCounter("\"solo\",\"pass\"", 2);
  openPickerFor(SOLO | PASS);
  input->click(Button::NavNext);
  frame();
  input->click(Button::Confirm);
  frame();
  EXPECT_EQ(activityManager.asks.replaced, 1);
  EXPECT_TRUE(logHas("Mode pass picked"));
}

TEST_F(PickerTest, ConfirmWithNothingMovedStartsTheFirstRow) {
  installCounter("\"solo\",\"pass\"", 2);
  openPickerFor(SOLO | PASS);
  input->click(Button::Confirm);
  frame();
  EXPECT_EQ(activityManager.asks.replaced, 1);
  EXPECT_TRUE(logHas("Mode solo picked"));
}

TEST_F(PickerTest, BackPopsToTheLauncherAndDoesNotGoHomeOrStartAnything) {
  installCounter("\"solo\",\"pass\"", 2);
  openPickerFor(SOLO | PASS);
  input->click(Button::Back);
  frame();
  EXPECT_EQ(activityManager.asks.popped, 1);
  EXPECT_EQ(activityManager.asks.goHome, 0);
  EXPECT_EQ(activityManager.asks.replaced, 0);
}

TEST_F(PickerTest, ThePickerIsNamedByTheConstantGoHomeMapsToTheGamesRow) {
  openPickerFor(SOLO | PASS);
  EXPECT_EQ(NameOf::of(*picker), std::string(GameModeActivity::NAME));
  EXPECT_STRNE(GameModeActivity::NAME, GamesLauncherActivity::NAME);
}

// ---- how many modes make the picker necessary ----

TEST(ModeCount, CountsTheThreeModeBitsAndNothingElse) {
  EXPECT_EQ(GameModeActivity::modeCount(0), 0);
  EXPECT_EQ(GameModeActivity::modeCount(SOLO), 1);
  EXPECT_EQ(GameModeActivity::modeCount(PASS), 1);
  EXPECT_EQ(GameModeActivity::modeCount(NEARBY), 1);
  EXPECT_EQ(GameModeActivity::modeCount(SOLO | PASS), 2);
  EXPECT_EQ(GameModeActivity::modeCount(SOLO | NEARBY), 2);
  EXPECT_EQ(GameModeActivity::modeCount(SOLO | PASS | NEARBY), 3);
  EXPECT_EQ(GameModeActivity::modeCount(0xF8), 0) << "bits past the three are not modes";
  EXPECT_EQ(GameModeActivity::modeCount(0xFF), 3);
}

TEST(ModeCount, ThePickerIsNeededFromTwoModes) {
  EXPECT_FALSE(GameModeActivity::needed(0));
  EXPECT_FALSE(GameModeActivity::needed(SOLO));
  EXPECT_FALSE(GameModeActivity::needed(PASS));
  EXPECT_TRUE(GameModeActivity::needed(SOLO | PASS));
  EXPECT_TRUE(GameModeActivity::needed(SOLO | PASS | NEARBY));
}

}  // namespace
