#include <I18n.h>
#include <Manifest.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "ApiLevel.h"
#include "GameHostCaps.h"
#include "GameRegistry.h"
#include "HostCapsScript.h"
#include "InstallerScript.h"
#include "MatchSupport.h"
#include "RemoveScript.h"
#include "activities/games/GameMatchActivity.h"
#include "activities/games/GameModeActivity.h"
#include "activities/games/GameOptionsActivity.h"
#include "activities/games/GamePicture.h"
#include "activities/games/GameSplashLayout.h"
#include "activities/games/GamesLauncherActivity.h"
#include "games/GameSaveStore.h"
#include "util/ButtonNavigator.h"

// The real GameModeActivity, a game's title screen (entry 7 of epic-pass-and-play), and the real GamesLauncherActivity
// whose game rows push it, over the screen doubles (screen_stubs/), the fake card, and the scripted installer and host
// caps (list_stubs/), on the pattern of GamesLauncherTest.cpp: the fixture opens a screen, draws it, and finds a row by
// the text it drew. What the title screen promises (entry 12 of epic-pass-and-play, the approved design): its header is
// the game's name; under it a 480 x 480 splash band with title.bmp or the game's icon at 128 px; a Continue row first
// when the card holds a save (Valid or Unreadable); then New game, in the current mode and settings, which its second
// line names; then Options, when there is a mode or setting to choose; a New game over a save asks first, Cancel
// focused, a save this host cannot start (Unstartable) included, which gets no Continue; a Continue on a pass save, or
// on a two-mode game's Unreadable save, never starts a solo match that would replace the save, and one that finds no
// usable save starts the first mode the host can start; the choices are remembered in prefs.bin; Back returns to the
// launcher.

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

// `extra` is more of the manifest's members, each after a comma (",\"default_mode\":\"pass\"").
std::string manifestJson(const std::string& id, const std::string& name, const std::string& modes, const int seatsMin,
                         const int seatsMax, const std::string& extra = "") {
  return "{\"id\":\"" + id + "\",\"name\":\"" + name +
         "\",\"version\":\"1.0.0\",\"api\":1,\"seats\":{\"min\":" + std::to_string(seatsMin) +
         ",\"max\":" + std::to_string(seatsMax) + "},\"modes\":[" + modes + "]" + extra + "}";
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
// (the helper of the Continue rows' suite that entry 8 retired, which fixed them at solo and one).
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
// setup ran (from the Continue rows' suite that entry 8 retired).
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

// A game of the counting Lua's rules whose setup logs ctx.settings, sorted ("settings\t[board=Small,level=Hard]").
const char* const SETTINGS_GAME = R"(
local game = {}
function game.setup(ctx)
  local keys = {}
  for k, v in pairs(ctx.settings) do keys[#keys + 1] = k .. "=" .. v end
  table.sort(keys)
  ch.log("settings", "[" .. table.concat(keys, ",") .. "]")
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
  ch.gfx.clear("white")
end
function game.input(state, seat, ui, ev)
  if ev.kind == "tap" then return { tap = true } end
end
return game
)";

// Two settings, in manifest order: level (Easy, Hard; default Hard) and board (Small, Large; default Small).
const std::string TWO_SETTINGS =
    ",\"settings\":[{\"id\":\"level\",\"name\":\"Level\",\"values\":[\"Easy\",\"Hard\"],\"default\":\"Hard\"},"
    "{\"id\":\"board\",\"name\":\"Board\",\"values\":[\"Small\",\"Large\"]}]";

std::string resumePath(const std::string& id) { return "/.games-data/" + id + "/resume.bin"; }
std::string prefsPath(const std::string& id) { return "/.games-data/" + id + "/prefs.bin"; }

// What prefs.bin holds for game `id`, read as the title screen reads it (mode 0 and no settings for no usable file).
GameSaveStore::Prefs prefsOf(const std::string& id) {
  GameSaveStore::Prefs prefs;
  GameSaveStore::loadPrefs(id.c_str(), prefs);
  return prefs;
}

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
    // Options first, as the manager lets a stack go: the current screen, then the one under it.
    for (auto pushed = activityManager.pushedActivities.rbegin(); pushed != activityManager.pushedActivities.rend();
         ++pushed) {
      if (pushed->get() == title || pushed->get() == options) activityManager.exitHolding(**pushed);
      activityManager.destroyHolding(*pushed);
    }
    activityManager.pushedActivities.clear();
    title = nullptr;
    options = nullptr;
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
  // An installed game of the counting Lua (or `lua`), its manifest given `extra` members (manifestJson).
  static void addCountingGame(const std::string& id, const std::string& name, const std::string& modes,
                              const int seatsMin = 1, const int seatsMax = 2, const std::string& extra = "",
                              const char* lua = COUNTING_GAME) {
    const std::string dir = "/.games/" + id;
    fakesd::addFile(dir + "/manifest.json", manifestJson(id, name, modes, seatsMin, seatsMax, extra));
    fakesd::addFile(dir + "/main.lua", std::string(lua));
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

  // The launcher, a tap on `game`'s row, and the title screen it pushed, opened. A game has one row (entry 8 took the
  // launcher's Continue rows away), the one drawn with its name.
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

  // The rows the screen drew, top to bottom: Continue, New game, Options.
  std::vector<std::string> rowsDrawn() {
    static const std::vector<std::string> names{tr(STR_GAMES_CONTINUE), tr(STR_GAMES_NEW_GAME), tr(STR_GAMES_OPTIONS)};
    std::vector<std::string> found;
    for (const screen::DrawnText& drawn : ui().drawn)
      for (const std::string& name : names)
        if (drawn.text == name) found.push_back(name);
    return found;
  }
  bool dialogUp() { return ui().drewLine(tr(STR_GAMES_NEW_OVER_SAVE_TITLE)); }
  // The line drawn right after `label` (a row's second line), or "" when there is none.
  std::string lineUnder(const std::string& label) {
    const std::vector<screen::DrawnText>& drawn = ui().drawn;
    for (size_t i = 0; i + 1 < drawn.size(); ++i)
      if (drawn[i].text == label) return drawn[i + 1].text;
    return "";
  }
  // New game's second line: the current mode and settings.
  std::string newGameLine() { return lineUnder(tr(STR_GAMES_NEW_GAME)); }

  // prefs.bin for game `id` remembering `mode` (a Manifest::Mode bit) and no setting, as a title screen wrote it.
  static void remember(const std::string& id, const uint8_t mode) {
    GameSaveStore::Prefs prefs;
    prefs.mode = mode;
    ASSERT_TRUE(GameSaveStore::savePrefs(id.c_str(), prefs));
  }

  // Taps Options on the title screen and opens the Options screen it pushed, as the manager would, and draws it.
  void openOptions() {
    const size_t before = activityManager.pushedActivities.size();
    tapRow(tr(STR_GAMES_OPTIONS));
    ASSERT_EQ(activityManager.pushedActivities.size(), before + 1) << "Options is pushed over the title screen";
    options = dynamic_cast<GameOptionsActivity*>(activityManager.pushedActivities.back().get());
    ASSERT_NE(options, nullptr) << "what the title screen pushes is a GameOptionsActivity";
    current = options;
    current->onEnter();
    render();
  }
  // Back on Options, then the manager's Pop: Options goes and the title screen's result handler runs; the title
  // screen is drawn again.
  void closeOptions() {
    ASSERT_NE(options, nullptr);
    const int popped = activityManager.asks.popped;
    input->click(Button::Back);
    frame();
    ASSERT_EQ(activityManager.asks.popped, popped + 1) << "Back finishes Options";
    activityManager.popForResult(*title, *options);
    options = nullptr;
    current = title;
    render();
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

  // A Continue on a two-mode game's pass save that was removed (`removed`) or went bad after the screen peeked it.
  void continueAfterTheSaveChanged(bool removed);
  // The title screen of "counter" over a save this host cannot start: `rows` drawn, and a tap on `newRow` asks first.
  void expectAnUnstartableSaveAsksBeforeNew(const std::string& newRow, const std::vector<std::string>& rows);

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
  GameModeActivity* title = nullptr;       // owned by activityManager.pushedActivities
  GameOptionsActivity* options = nullptr;  // the same, while it is open
  Activity* current = nullptr;             // the screen frame() and render() drive
  Activity* entered = nullptr;             // the replacement enterReplacement() started, which needs its onExit
  int taps = 0;                            // the taps tapRow made
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

TEST_F(TitleScreenTest, APassOnlyGameRowPushesItsTitleScreenWhoseNewGameIsPass) {
  installCounter("\"pass\"");
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  const std::vector<std::string> expected{tr(STR_GAMES_NEW_GAME)};
  EXPECT_EQ(rowsDrawn(), expected) << "one mode and no settings: no Options";
  EXPECT_EQ(newGameLine(), tr(STR_GAMES_MODE_PASS)) << "a pass-only game's current mode is pass";
}

TEST_F(TitleScreenTest, TheModesOptionsCyclesAreTheOnesCheckLeavesForThisHost) {
  installCounter("\"solo\",\"pass\",\"nearby\"");  // nearby stays off: the host has no radio
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  const std::vector<std::string> expected{tr(STR_GAMES_NEW_GAME), tr(STR_GAMES_OPTIONS)};
  EXPECT_EQ(rowsDrawn(), expected);
  EXPECT_EQ(newGameLine(), tr(STR_GAMES_MODE_SOLO)) << "no remembered mode and no default_mode: solo";
  ASSERT_NO_FATAL_FAILURE(openOptions());
  EXPECT_EQ(lineUnder(tr(STR_GAMES_MODE)), tr(STR_GAMES_MODE_SOLO));
  tapRow(tr(STR_GAMES_MODE));
  render();
  EXPECT_EQ(lineUnder(tr(STR_GAMES_MODE)), tr(STR_GAMES_MODE_PASS));
  tapRow(tr(STR_GAMES_MODE));
  render();
  EXPECT_EQ(lineUnder(tr(STR_GAMES_MODE)), tr(STR_GAMES_MODE_SOLO)) << "wraps past pass: the host cannot start nearby";
}

// e5-r7: a game installed before the pass seat rule that lists solo and pass with one seat still opens, in solo, with
// no Options row (pass is not offered); the load logs why.
TEST_F(TitleScreenTest, ASoloAndPassGameWithOneSeatOpensItsTitleScreenInSolo) {
  installCounter("\"solo\",\"pass\"", 1);
  openLauncher();
  EXPECT_TRUE(logHas("counter: pass and nearby need seats.max 2 or more; its other modes still work"));
  EXPECT_FALSE(logHas("Invalid counter"));
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  const std::vector<std::string> expected{tr(STR_GAMES_NEW_GAME)};
  EXPECT_EQ(rowsDrawn(), expected);
  EXPECT_EQ(newGameLine(), tr(STR_GAMES_MODE_SOLO));
}

TEST_F(TitleScreenTest, ASoloAndPassGameIsSoloWithNoOptionsWhileTheHostHasNoPass) {
  hostcaps::script().pass = false;  // a host without pass: Manifest::check leaves solo
  installCounter("\"solo\",\"pass\"");
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  const std::vector<std::string> expected{tr(STR_GAMES_NEW_GAME)};
  EXPECT_EQ(rowsDrawn(), expected);
  EXPECT_EQ(newGameLine(), tr(STR_GAMES_MODE_SOLO));
}

// A pass-only game with one seat has no mode left (pass needs seats.max 2) and is invalid: its row says why, and a tap
// opens nothing and repaints the list.
TEST_F(TitleScreenTest, APassOnlyGameWithOneSeatOpensNothingFromTheLauncher) {
  installCounter("\"pass\"", 1);
  openLauncher();
  EXPECT_TRUE(logHas("Invalid counter: pass and nearby need seats.max 2 or more"));
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

// Entry 8: the launcher has one row per game and no Continue row; a game with a save offers it on the title screen its
// row opens, first, where the selection starts.
TEST_F(TitleScreenTest, AGameWithASaveHasOneRowThatOpensItsTitleScreenWithContinueFirst) {
  addCountingGame("alpha", "Alpha", "\"solo\"", 1, 1);
  save("alpha");
  openLauncher();
  EXPECT_FALSE(ui().drewLine(tr(STR_GAMES_CONTINUE))) << ui().joined();
  EXPECT_EQ(std::count_if(ui().drawn.begin(), ui().drawn.end(),
                          [](const screen::DrawnText& drawn) { return drawn.text == "Alpha"; }),
            1)
      << "one row: " << ui().joined();
  tapRow("Alpha");
  EXPECT_EQ(activityManager.asks.pushed, 1);
  EXPECT_EQ(activityManager.asks.replaced, 0);
  ASSERT_NO_FATAL_FAILURE(openPushedTitle());
  EXPECT_TRUE(theme().drew("drawHeader", "Alpha"));
  const std::vector<std::string> expected{tr(STR_GAMES_CONTINUE), tr(STR_GAMES_NEW_GAME)};
  EXPECT_EQ(rowsDrawn(), expected);
  input->click(Button::Confirm);
  frame();
  ASSERT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);
  ASSERT_TRUE(pumpMatchTo("Resuming at ver 3"));
}

// Home → Games is one tap; the game's row and a start (New game, in the current mode) are the other two (AD-22).
TEST_F(TitleScreenTest, ASoloGameAndAPassGameEachStartInTwoTapsFromTheLauncher) {
  addCountingGame("one-solo", "One solo", "\"solo\"", 1, 1);
  addCountingGame("two-pass", "Two pass", "\"pass\"", 2, 2);
  openLauncher();
  tapRow("One solo");
  ASSERT_NO_FATAL_FAILURE(openPushedTitle());
  tapRow(tr(STR_GAMES_NEW_GAME));
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
  tapRow(tr(STR_GAMES_NEW_GAME));
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

TEST_F(TitleScreenTest, WithNoSaveTheHeaderIsTheGamesNameAndTheRowsAreNewGameAndOptions) {
  installCounter("\"solo\",\"pass\"");
  openTitleFor(SOLO | PASS);
  EXPECT_TRUE(theme().drew("drawHeader", "Counter"));
  const std::vector<std::string> expected{tr(STR_GAMES_NEW_GAME), tr(STR_GAMES_OPTIONS)};
  EXPECT_EQ(rowsDrawn(), expected) << "no Continue row: there is no save";
  EXPECT_EQ(newGameLine(), tr(STR_GAMES_MODE_SOLO)) << "the current mode, and no setting";
  EXPECT_FALSE(ui().drewLine(tr(STR_GAMES_MODE_PASS))) << "one New game row, not one row per mode";
  EXPECT_EQ(lineUnder(tr(STR_GAMES_OPTIONS)), "") << "Options is one line";
  ASSERT_FALSE(theme().hints.empty());
  EXPECT_EQ(theme().hints.back().btn1, tr(STR_BACK));
  EXPECT_EQ(theme().hints.back().btn2, tr(STR_SELECT));
}

TEST_F(TitleScreenTest, OptionsCyclesAllThreeModesInTheOrderSoloPassNearby) {
  openTitleFor(SOLO | PASS | NEARBY);
  EXPECT_EQ(newGameLine(), tr(STR_GAMES_MODE_SOLO));
  ASSERT_NO_FATAL_FAILURE(openOptions());
  EXPECT_TRUE(theme().drew("drawHeader", tr(STR_GAMES_OPTIONS))) << "headed Options";
  std::vector<std::string> seen{lineUnder(tr(STR_GAMES_MODE))};
  for (int i = 0; i < 3; ++i) {
    tapRow(tr(STR_GAMES_MODE));
    render();
    seen.push_back(lineUnder(tr(STR_GAMES_MODE)));
  }
  const std::vector<std::string> expected{tr(STR_GAMES_MODE_SOLO), tr(STR_GAMES_MODE_PASS), tr(STR_GAMES_MODE_NEARBY),
                                          tr(STR_GAMES_MODE_SOLO)};
  EXPECT_EQ(seen, expected) << "the next value in place, wrapping after the last";
}

TEST_F(TitleScreenTest, WithoutSoloTheCurrentModeIsTheFirstOfTheModesGiven) {
  openTitleFor(PASS | NEARBY);
  const std::vector<std::string> expected{tr(STR_GAMES_NEW_GAME), tr(STR_GAMES_OPTIONS)};
  EXPECT_EQ(rowsDrawn(), expected);
  EXPECT_EQ(newGameLine(), tr(STR_GAMES_MODE_PASS));
}

TEST_F(TitleScreenTest, ASoloSaveIsAContinueRowFirstAndSaysWhatItDoes) {
  addCountingGame("counter", "Counter", "\"solo\",\"pass\"");
  save("counter");
  openTitleFor(SOLO | PASS);
  const std::vector<std::string> expected{tr(STR_GAMES_CONTINUE), tr(STR_GAMES_NEW_GAME), tr(STR_GAMES_OPTIONS)};
  EXPECT_EQ(rowsDrawn(), expected);
  EXPECT_EQ(lineUnder(tr(STR_GAMES_CONTINUE)), tr(STR_GAMES_CONTINUE_DESC));
  EXPECT_EQ(std::string(tr(STR_GAMES_CONTINUE_DESC)), "Load the previous game");
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

TEST_F(TitleScreenTest, ATapOnNewGameReplacesTheScreenWithANewSoloMatch) {
  installCounter("\"solo\",\"pass\"");
  openTitleFor(SOLO | PASS);
  tapRow(tr(STR_GAMES_NEW_GAME));
  EXPECT_EQ(activityManager.asks.replaced, 1);
  EXPECT_EQ(activityManager.asks.popped, 0);
  ASSERT_NE(enterReplacement(), nullptr) << "what the title screen opens is a GameMatchActivity";
  EXPECT_TRUE(logHas("Started counter"));
  EXPECT_TRUE(logHas("Mode solo picked for counter"));
  EXPECT_FALSE(logHas("the match plays solo"));
}

TEST_F(TitleScreenTest, NewGameOfAPassAndNearbyGameStartsPass) {
  installCounter("\"pass\",\"nearby\"");
  openTitleFor(PASS | NEARBY);
  tapRow(tr(STR_GAMES_NEW_GAME));
  EXPECT_TRUE(logHas("Mode pass picked"));
  EXPECT_FALSE(logHas("Mode nearby picked"));
}

TEST_F(TitleScreenTest, NewGameInARememberedNearbyStartsNearbyAsSolo) {
  installCounter("\"pass\",\"nearby\"");
  remember("counter", NEARBY);
  openTitleFor(PASS | NEARBY);
  EXPECT_EQ(newGameLine(), tr(STR_GAMES_MODE_NEARBY));
  tapRow(tr(STR_GAMES_NEW_GAME));
  EXPECT_TRUE(logHas("Mode nearby picked for counter: the match plays solo until it can run nearby"));
  EXPECT_FALSE(logHas("Mode pass picked"));
}

// New game in pass starts an open pass match with the fewest seats it can have (two), and seat 1 is drawn first.
TEST_F(TitleScreenTest, ATapOnPassStartsATwoSeatPassMatchThatDrawsSeatOneFirst) {
  match::installFixture("pass-open");
  fakesd::addFile("/.games/pass-open/.pkg", PKG);
  remember("pass-open", PASS);
  openTitleFor(SOLO | PASS, 2, "pass-open", "Pass open");
  EXPECT_EQ(newGameLine(), tr(STR_GAMES_MODE_PASS));
  tapRow(tr(STR_GAMES_NEW_GAME));
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

// ## 5.2: pass's seat guard (passSeats 0), reached on a one-seat host, where Manifest::check still leaves pass for a
// 1..2 game: New game in pass starts nothing, is logged, and repaints the screen.
// A remembered pass on a host with no pass seat count falls back to a mode it can start (solo), so New game starts,
// and prefs.bin keeps pass for a host that can start it. Options does not offer pass here (one mode, no settings: no
// Options row).
TEST_F(TitleScreenTest, ARememberedPassThisHostHasNoSeatsForFallsBackToSolo) {
  hostcaps::script().maxSeats = 1;
  installCounter("\"solo\",\"pass\"");
  remember("counter", PASS);
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  EXPECT_EQ(newGameLine(), tr(STR_GAMES_MODE_SOLO));
  EXPECT_EQ(rowsDrawn(), (std::vector<std::string>{tr(STR_GAMES_NEW_GAME)}));
  tapRow(tr(STR_GAMES_NEW_GAME));
  ASSERT_EQ(activityManager.asks.replaced, 1);
  EXPECT_TRUE(logHas("Mode solo picked for counter"));
  EXPECT_EQ(prefsOf("counter").mode, PASS) << "the remembered pass was written over";
}

// The same for a game's default_mode.
TEST_F(TitleScreenTest, ADefaultPassThisHostHasNoSeatsForFallsBackToSolo) {
  hostcaps::script().maxSeats = 1;
  addCountingGame("counter", "Counter", "\"solo\",\"pass\"", 1, 2, ",\"default_mode\":\"pass\"");
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  EXPECT_EQ(newGameLine(), tr(STR_GAMES_MODE_SOLO));
}

// A pass-only game on a one-seat host (check leaves pass there): pass is all it has, so New game names it, and a tap
// starts nothing, logged with the seat count, and repaints.
TEST_F(TitleScreenTest, APassThatCannotFitStartsNothing) {
  hostcaps::script().maxSeats = 1;
  installCounter("\"pass\"");
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  ASSERT_EQ(rowsDrawn(), (std::vector<std::string>{tr(STR_GAMES_NEW_GAME)})) << "check leaves pass on a one-seat host";
  ASSERT_EQ(newGameLine(), tr(STR_GAMES_MODE_PASS));
  activityManager.markRendered();
  tapRow(tr(STR_GAMES_NEW_GAME));
  EXPECT_EQ(activityManager.asks.replaced, 0);
  EXPECT_TRUE(activityManager.replacements.empty());
  EXPECT_TRUE(logHas("Cannot start counter in pass: seats 1..2 leave no pass match on this host"));
  EXPECT_FALSE(logHas("Mode pass picked"));
  EXPECT_TRUE(activityManager.updateRequested());
}

TEST_F(TitleScreenTest, ConfirmActsOnTheSelectedRowAndNextMovesTheSelectionToOptions) {
  installCounter("\"solo\",\"pass\"");
  openTitleFor(SOLO | PASS);
  input->click(Button::NavNext);
  frame();
  input->click(Button::Confirm);
  frame();
  EXPECT_EQ(activityManager.asks.replaced, 0) << "Options starts no match";
  ASSERT_EQ(activityManager.pushedActivities.size(), 2u);
  EXPECT_NE(dynamic_cast<GameOptionsActivity*>(activityManager.pushedActivities.back().get()), nullptr);
  EXPECT_EQ(NameOf::of(*activityManager.pushedActivities.back()), std::string(GameOptionsActivity::NAME));
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
  tapRow(tr(STR_GAMES_NEW_GAME));
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
  EXPECT_TRUE(logHas("counter: resuming the save's roster: solo, 1 seat(s)"));
  ASSERT_TRUE(pumpMatchTo("Round started at ver 3"));
  EXPECT_TRUE(logHas("Resuming at ver 3"));
  EXPECT_TRUE(logHas("draw\t2")) << "the saved snapshot is what is drawn";
  EXPECT_FALSE(logHas("setup ran"));
}

// A game that can start solo and pass resumes its solo save solo: the match plays the roster the save records.
TEST_F(TitleScreenTest, ATapOnContinueResumesATwoModeGamesSoloSaveSolo) {
  addCountingGame("counter", "Counter", "\"solo\",\"pass\"");
  save("counter");
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  tapRow(tr(STR_GAMES_CONTINUE));
  ASSERT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Continue counter: a solo roster of 1 seat(s) unless the save says otherwise"));
  EXPECT_TRUE(logHas("counter: resuming the save's roster: solo, 1 seat(s)"));
  ASSERT_TRUE(pumpMatchTo("Resuming at ver 3"));
  EXPECT_FALSE(logHas("resuming the save's roster: pass"));
}

// A pass save (mode 1, n 2) gets its Continue row, and the tap resumes it as a pass match: the saved snapshot is drawn,
// setup never runs, and the save is unchanged through the match and its exit (a restored snapshot is not written
// again).
TEST_F(TitleScreenTest, ContinueOnAPassSaveResumesItAsAPassMatch) {
  addCountingGame("counter", "Counter", "\"solo\",\"pass\"");
  save("counter", SAVED_PASS, 2);
  const Bytes saved = fakesd::bytesOf(resumePath("counter"));
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  const std::vector<std::string> expected{tr(STR_GAMES_CONTINUE), tr(STR_GAMES_NEW_GAME), tr(STR_GAMES_OPTIONS)};
  ASSERT_EQ(rowsDrawn(), expected) << "the pass save is offered";
  tapRow(tr(STR_GAMES_CONTINUE));
  ASSERT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);
  // Continue passes the first startable mode's roster, solo; the save's pass roster wins over it.
  EXPECT_TRUE(logHas("Continue counter: a solo roster of 1 seat(s) unless the save says otherwise"));
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
  EXPECT_TRUE(logHas("Continue counter: a pass roster of 2 seat(s) unless the save says otherwise"));
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
  const std::vector<std::string> expected{tr(STR_GAMES_CONTINUE), tr(STR_GAMES_NEW_GAME)};
  ASSERT_EQ(rowsDrawn(), expected);
  tapRow(tr(STR_GAMES_CONTINUE));
  ASSERT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Continue counter: a solo roster of 1 seat(s) unless the save says otherwise"));
  EXPECT_TRUE(logHas("resume.bin could not be read; not starting a new match over it"));
  EXPECT_FALSE(logHas("setup ran"));
  EXPECT_EQ(fakesd::bytesOf(resumePath("counter")), saved);
}

// A two-mode game's Unreadable save may be a pass save: its Continue passes the first startable mode's roster (solo),
// and the match stops in the error view as on any save that will not read, whatever roster it was passed, so the file's
// bytes are unchanged through the match and its exit.
TEST_F(TitleScreenTest, ATwoModeGamesUnreadableSaveEndsInTheErrorViewAndIsLeftAlone) {
  addCountingGame("counter", "Counter", "\"solo\",\"pass\"");
  save("counter", SAVED_PASS, 2);
  const Bytes saved = fakesd::bytesOf(resumePath("counter"));
  fakesd::sim().failReadAt[resumePath("counter")] = 0;
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  const std::vector<std::string> expected{tr(STR_GAMES_CONTINUE), tr(STR_GAMES_NEW_GAME), tr(STR_GAMES_OPTIONS)};
  ASSERT_EQ(rowsDrawn(), expected);
  tapRow(tr(STR_GAMES_CONTINUE));
  ASSERT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Continue counter: a solo roster of 1 seat(s) unless the save says otherwise"));
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

// ---- a save this host cannot start (cross-story review rows 2 and 13) ----

// A pass save on a host without Pass and Play (or with fewer seats than the save) is a save all the same: no Continue
// row, since this host cannot resume it, but New game asks before it replaces the file, and Cancel keeps its bytes.
void TitleScreenTest::expectAnUnstartableSaveAsksBeforeNew(const std::string& newRow,
                                                           const std::vector<std::string>& rows) {
  const Bytes saved = fakesd::bytesOf(resumePath("counter"));
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  EXPECT_TRUE(logHas("Title screen of counter: save unstartable"));
  EXPECT_EQ(rowsDrawn(), rows) << "no Continue for a save this host cannot start";
  tapRow(newRow);
  EXPECT_EQ(activityManager.asks.replaced, 0) << "New started over the save without asking";
  render();
  ASSERT_TRUE(dialogUp());
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_NEW_OVER_SAVE)));
  tapRow(tr(STR_CANCEL));
  render();
  EXPECT_FALSE(dialogUp());
  EXPECT_EQ(rowsDrawn(), rows);
  EXPECT_EQ(activityManager.asks.replaced, 0);
  EXPECT_EQ(fakesd::bytesOf(resumePath("counter")), saved);
}

TEST_F(TitleScreenTest, APassSaveOnAHostWithoutPassOffersNoContinueAndNewAsksFirst) {
  hostcaps::script().pass = false;
  addCountingGame("counter", "Counter", "\"solo\",\"pass\"");
  save("counter", SAVED_PASS, 2);
  expectAnUnstartableSaveAsksBeforeNew(tr(STR_GAMES_NEW_GAME), {tr(STR_GAMES_NEW_GAME)});
}

TEST_F(TitleScreenTest, AThreeSeatSaveOnATwoSeatHostOffersNoContinueAndNewAsksFirst) {
  ASSERT_EQ(hostcaps::script().maxSeats, 2);
  addCountingGame("counter", "Counter", "\"solo\",\"pass\"", 1, 3);
  save("counter", SAVED_PASS, 3);
  expectAnUnstartableSaveAsksBeforeNew(tr(STR_GAMES_NEW_GAME), {tr(STR_GAMES_NEW_GAME), tr(STR_GAMES_OPTIONS)});
}

// ---- pass-hidden end to end (cross-story review row 12): its own manifest.json (hidden: true) from the card ----

// From the launcher's row through the title screen's New game (pass, the game's one mode) to the match: the first
// screen is the hand-off screen ("Player 1's turn", "I'm ready"), a full refresh, and no push before the first seat's
// frame holds anything of a seat's frame; the round's four moves go through Result (passed by its banner) and the
// hand-off screen (passed by its button) to Over, and Play again starts the next round on the hand-off screen. (The
// test's name predates entry 12, when that screen was a blank.)
TEST_F(TitleScreenTest, PassHiddenFromItsManifestPlaysThroughTheBlankToOverAndPlayAgain) {
  match::installFixture("pass-hidden");
  fakesd::addFile("/.games/pass-hidden/.pkg", PKG);
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Pass hidden"));
  const std::vector<std::string> rows{tr(STR_GAMES_NEW_GAME)};
  ASSERT_EQ(rowsDrawn(), rows);
  ASSERT_EQ(newGameLine(), tr(STR_GAMES_MODE_PASS));
  tapRow(tr(STR_GAMES_NEW_GAME));
  ASSERT_EQ(activityManager.asks.replaced, 1);
  EXPECT_TRUE(logHas("Mode pass picked for pass-hidden: 2 seats"));
  ASSERT_NE(enterReplacement(), nullptr);
  current = entered;  // frame(), render(), and tapRow() now drive the match
  EXPECT_TRUE(logHas("pass-hidden: Starting -> HandOff on Started")) << "the manifest's hidden: true reached the match";
  const auto holds = [](const GfxRenderer::Shown& push, const std::string& part) {
    return std::any_of(push.texts.begin(), push.texts.end(),
                       [&](const std::string& text) { return text.find(part) != std::string::npos; });
  };
  // The hand-off screen naming `seat`, pushed in full: it waits for the VM to name a round's first turn seat, so a
  // render before that pushes nothing and the loop asks again.
  const auto expectHandOff = [&](const int seat) {
    const size_t pushes = renderer->shown.size();
    render();
    if (renderer->shown.size() == pushes) {
      ASSERT_TRUE(match::waitFor([&] {
        frame();
        return activityManager.updateRequested();
      }));
      render();
    }
    ASSERT_EQ(renderer->shown.size(), pushes + 1);
    EXPECT_EQ(renderer->shown.back().mode, HalDisplay::FULL_REFRESH);
    EXPECT_EQ(renderer->shown.back().texts,
              (std::vector<std::string>{"Player " + std::to_string(seat) + "'s turn", tr(STR_GAMES_READY)}));
  };
  // The one tap target of the screen on the panel: Result's banner at the bottom, or the hand-off screen's "I'm ready"
  // in the splash menu's second row (HiddenPassTest.TheReadyButtonFillsTheSecondMenuRowAndTheBannerIsAtTheBottom pins
  // both places).
  const auto tapBanner = [&] {
    input->tap(240, 740);
    frame();
  };
  const auto tapReady = [&] {
    const freeink::ui::Point ready = match::readyButtonMiddle(*renderer);
    input->tap(ready.x, ready.y);
    frame();
  };
  const auto pumpTo = [&](const std::string& part, const size_t times) {
    return match::waitFor([&] {
      frame();
      return fakelog::countLines(part) >= times;
    });
  };
  const size_t matchFrom = renderer->shown.size();
  ASSERT_NO_FATAL_FAILURE(expectHandOff(1));
  for (size_t move = 1; move <= 4; ++move) {
    const int seat = static_cast<int>((move - 1) % 2 + 1);
    tapReady();
    ASSERT_EQ(fakelog::countLines("pass-hidden: HandOff -> Playing on Tap"), move);
    ASSERT_TRUE(match::waitFor([&] {
      frame();
      return activityManager.updateRequested();
    }));
    render();
    EXPECT_EQ(renderer->shown.back().mode, HalDisplay::FULL_REFRESH);
    EXPECT_TRUE(holds(renderer->shown.back(), "Player " + std::to_string(seat) + "'s secret: ")) << move;
    if (move == 1) {
      // Every push of the match before the first seat's frame was the hand-off screen: nothing of a seat's frame.
      for (size_t i = matchFrom; i + 1 < renderer->shown.size(); ++i) {
        EXPECT_FALSE(holds(renderer->shown[i], "secret")) << "push " << i << " before the first seat's frame";
        EXPECT_TRUE(holds(renderer->shown[i], tr(STR_GAMES_READY))) << "push " << i << " before the first seat's frame";
      }
    }
    activityManager.markRendered();
    input->tap(3 + 100, 6 + 300);
    frame();
    if (move == 4) break;
    ASSERT_TRUE(pumpTo("pass-hidden: Playing -> Result on TurnChanged", move));
    render();
    EXPECT_TRUE(holds(renderer->shown.back(), "Tap to pass to player " + std::to_string(3 - seat)));
    tapBanner();
    ASSERT_EQ(fakelog::countLines("pass-hidden: Result -> HandOff on Tap"), move);
    ASSERT_NO_FATAL_FAILURE(expectHandOff(3 - seat));
  }
  ASSERT_TRUE(pumpTo("pass-hidden: Playing -> Over on RoundOver", 1));
  render();
  EXPECT_TRUE(holds(renderer->shown.back(), "Everyone: the secrets were apple and river"));
  tapRow(tr(STR_GAMES_PLAY_AGAIN));
  EXPECT_TRUE(logHas("pass-hidden: Over -> HandOff on PlayAgain"));
  ASSERT_NO_FATAL_FAILURE(expectHandOff(1));
  tapReady();
  ASSERT_TRUE(match::waitFor([&] {
    frame();
    return activityManager.updateRequested();
  }));
  render();
  EXPECT_TRUE(holds(renderer->shown.back(), "Player 1's secret: apple"));
  EXPECT_TRUE(holds(renderer->shown.back(), "Moves: 0")) << "the next round's first frame";
}

// ---- New over a save: a second confirm, Cancel focused ----

TEST_F(TitleScreenTest, NewGameOverASaveAsksFirstAndATapOnCancelKeepsTheSave) {
  addCountingGame("counter", "Counter", "\"solo\",\"pass\"");
  save("counter");
  const Bytes saved = fakesd::bytesOf(resumePath("counter"));
  openTitleFor(SOLO | PASS);
  tapRow(tr(STR_GAMES_NEW_GAME));
  EXPECT_EQ(activityManager.asks.replaced, 0) << "nothing starts before the answer";
  render();
  ASSERT_TRUE(dialogUp());
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_MODE_SOLO))) << "the second line is the current mode's name";
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_NEW_OVER_SAVE)));
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_NEW_GAME)));
  EXPECT_TRUE(ui().drewLine(tr(STR_CANCEL)));
  EXPECT_FALSE(ui().drewLine(tr(STR_GAMES_OPTIONS))) << "the list is not built under the question";
  tapRow(tr(STR_CANCEL));
  render();
  EXPECT_FALSE(dialogUp());
  const std::vector<std::string> expected{tr(STR_GAMES_CONTINUE), tr(STR_GAMES_NEW_GAME), tr(STR_GAMES_OPTIONS)};
  EXPECT_EQ(rowsDrawn(), expected) << "the screen is as it was";
  EXPECT_EQ(activityManager.asks.replaced, 0);
  EXPECT_EQ(fakesd::bytesOf(resumePath("counter")), saved);
}

TEST_F(TitleScreenTest, BackAndAConfirmOnTheFocusedCancelCloseTheQuestionAndKeepTheSave) {
  addCountingGame("counter", "Counter", "\"solo\"", 1, 1);
  save("counter");
  openTitleFor(SOLO, 1);
  tapRow(tr(STR_GAMES_NEW_GAME));
  render();
  ASSERT_TRUE(dialogUp());
  input->click(Button::Back);
  frame();
  render();
  EXPECT_FALSE(dialogUp());
  EXPECT_EQ(activityManager.asks.popped, 0) << "Back closes the question, not the screen";

  tapRow(tr(STR_GAMES_NEW_GAME));
  render();
  ASSERT_TRUE(dialogUp());
  input->click(Button::Confirm);  // the focus starts on Cancel
  frame();
  render();
  EXPECT_FALSE(dialogUp());
  EXPECT_EQ(activityManager.asks.replaced, 0);
  EXPECT_TRUE(fakesd::has(resumePath("counter")));
}

TEST_F(TitleScreenTest, NewGameOverASaveStartsANewMatchInTheCurrentMode) {
  addCountingGame("counter", "Counter", "\"solo\",\"pass\"");
  save("counter");
  remember("counter", PASS);
  openTitleFor(SOLO | PASS);
  tapRow(tr(STR_GAMES_NEW_GAME));
  render();
  ASSERT_TRUE(dialogUp());
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_MODE_PASS))) << "the question names the current mode";
  EXPECT_FALSE(ui().drewLine(tr(STR_GAMES_MODE_SOLO)));
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
  input->click(Button::NavNext);  // New game, under Continue
  frame();
  input->click(Button::Confirm);
  frame();
  render();
  ASSERT_TRUE(dialogUp());
  input->click(Button::Right);  // New game
  frame();
  input->click(Button::Left);  // back to Cancel
  frame();
  input->click(Button::Confirm);
  frame();
  render();
  EXPECT_FALSE(dialogUp()) << "Left took the focus back to Cancel";
  EXPECT_EQ(activityManager.asks.replaced, 0);
  input->click(Button::Confirm);  // New game, still selected: the question again, Cancel focused
  frame();
  render();
  ASSERT_TRUE(dialogUp());
  input->click(Button::Down);
  frame();
  input->click(Button::Up);  // back to Cancel
  frame();
  input->click(Button::Right);  // New game
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
  tapRow(tr(STR_GAMES_NEW_GAME));  // opens the question; the screen is not drawn again
  tapRow(tr(STR_GAMES_OPTIONS));
  tapRow(tr(STR_GAMES_CONTINUE));
  EXPECT_EQ(activityManager.asks.replaced, 0);
  EXPECT_EQ(activityManager.pushedActivities.size(), 1u) << "no Options";
  render();
  EXPECT_TRUE(dialogUp()) << "still asking";
  // The stale taps moved no selection: closed, a Confirm asks about New game again (the question names its mode).
  input->click(Button::Back);
  frame();
  input->click(Button::Confirm);
  frame();
  EXPECT_EQ(activityManager.asks.replaced, 0);
  EXPECT_EQ(activityManager.pushedActivities.size(), 1u);
  render();
  ASSERT_TRUE(dialogUp());
  EXPECT_TRUE(ui().drewLine(tr(STR_GAMES_MODE_SOLO)));
}

// An Unreadable save asks too: it may be a good save.
TEST_F(TitleScreenTest, NewGameOverAnUnreadableSaveAsksFirst) {
  addCountingGame("counter", "Counter", "\"solo\"", 1, 1);
  save("counter");
  fakesd::sim().failOpen.insert(resumePath("counter"));
  openTitleFor(SOLO, 1);
  tapRow(tr(STR_GAMES_NEW_GAME));
  EXPECT_EQ(activityManager.asks.replaced, 0);
  render();
  EXPECT_TRUE(dialogUp());
}

// The Continue tap's pass seat guard: a pass-only game's Unreadable save on a one-seat host (where check still leaves
// pass for a 1..2 game) has no pass match to start. Nothing starts, the screen is drawn again, and the save is kept.
TEST_F(TitleScreenTest, AContinueThatCannotFitAPassMatchStartsNothing) {
  hostcaps::script().maxSeats = 1;
  addCountingGame("counter", "Counter", "\"pass\"");
  save("counter", SAVED_PASS, 2);
  const Bytes saved = fakesd::bytesOf(resumePath("counter"));
  fakesd::sim().failOpen.insert(resumePath("counter"));
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  activityManager.markRendered();
  tapRow(tr(STR_GAMES_CONTINUE));
  EXPECT_EQ(activityManager.asks.replaced, 0);
  EXPECT_FALSE(logHas("Cannot start counter")) << "the search logs only its own failure";
  EXPECT_TRUE(logHas("Cannot continue counter: no mode this host can start"));
  EXPECT_TRUE(activityManager.updateRequested());
  EXPECT_EQ(fakesd::bytesOf(resumePath("counter")), saved);
}

// On a host with no pass seat count for the game (one seat), the title screen drops pass beside another mode
// (onEnter), so a pass-and-nearby game is left with nearby alone: no Options row, and Continue passes nearby's roster
// (solo until epic-play-nearby). It does not reach startResume's skip of a mode rosterFor refuses, since onEnter leaves
// pass only when it is the game's only mode, where Continue starts nothing
// (AContinueThatCannotFitAPassMatchStartsNothing).
TEST_F(TitleScreenTest, APassThisHostCannotSeatIsDroppedBesideNearbyAndContinuePassesNearbysRoster) {
  hostcaps::script().maxSeats = 1;
  addCountingGame("counter", "Counter", "\"pass\"");
  save("counter", SAVED_PASS, 2);
  fakesd::sim().failOpen.insert(resumePath("counter"));
  openTitleFor(PASS | NEARBY);
  // Pass, which this host fits no seat count for, is not offered beside nearby: one mode, so no Options row.
  const std::vector<std::string> expected{tr(STR_GAMES_CONTINUE), tr(STR_GAMES_NEW_GAME)};
  ASSERT_EQ(rowsDrawn(), expected);
  tapRow(tr(STR_GAMES_CONTINUE));
  EXPECT_FALSE(logHas("Cannot start counter")) << "a row the search skips is no error";
  EXPECT_FALSE(logHas("Cannot continue counter"));
  EXPECT_TRUE(logHas("Continue counter: a solo roster of 1 seat(s) unless the save says otherwise"));
  EXPECT_EQ(activityManager.asks.replaced, 1);
}

// The same host and save for a game that also starts solo: its Continue passes the solo roster, and the match stops in
// its error view on the save it cannot read, the file kept.
TEST_F(TitleScreenTest, AContinueWithNoPassSeatsOfAGameThatStartsSoloPassesTheSoloRoster) {
  hostcaps::script().maxSeats = 1;
  addCountingGame("counter", "Counter", "\"solo\",\"pass\"");
  save("counter");
  const Bytes saved = fakesd::bytesOf(resumePath("counter"));
  fakesd::sim().failOpen.insert(resumePath("counter"));
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  tapRow(tr(STR_GAMES_CONTINUE));
  ASSERT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Continue counter: a solo roster of 1 seat(s) unless the save says otherwise"));
  EXPECT_FALSE(logHas("Cannot continue counter"));
  EXPECT_TRUE(logHas("resume.bin could not be read; not starting a new match over it"));
  EXPECT_FALSE(logHas("setup ran"));
  EXPECT_EQ(fakesd::bytesOf(resumePath("counter")), saved);
}

// A two-mode game's pass save that went bad, or went, after the screen peeked it (cross-story review row 3): the new
// match Continue starts plays the first startable mode's roster, solo, never a pass match nobody chose, whatever the
// current mode (pass, remembered, here), and its first
// snapshot is saved as mode 0, n 1.
void TitleScreenTest::continueAfterTheSaveChanged(const bool removed) {
  addCountingGame("counter", "Counter", "\"solo\",\"pass\"");
  save("counter", SAVED_PASS, 2);
  remember("counter", PASS);
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  ASSERT_EQ(newGameLine(), tr(STR_GAMES_MODE_PASS));
  const std::vector<std::string> expected{tr(STR_GAMES_CONTINUE), tr(STR_GAMES_NEW_GAME), tr(STR_GAMES_OPTIONS)};
  ASSERT_EQ(rowsDrawn(), expected);
  if (removed) {
    fakesd::removeEntry(resumePath("counter"));
  } else {
    fakesd::addFile(resumePath("counter"), std::string("garbage"));
  }
  tapRow(tr(STR_GAMES_CONTINUE));
  ASSERT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Continue counter: a solo roster of 1 seat(s) unless the save says otherwise"));
  EXPECT_TRUE(logHas("no usable resume.bin; starting a new match"));
  EXPECT_TRUE(logHas("counter: Starting -> Playing on Started")) << "a solo match: no hand-off";
  ASSERT_TRUE(match::waitFor([&] {
    entered->loop();
    input->clear();
    return fakesd::bytesOf(resumePath("counter")).size() > 15;
  }));
  const Bytes firstSave = fakesd::bytesOf(resumePath("counter"));
  EXPECT_EQ(firstSave[14], SAVED_SOLO);
  EXPECT_EQ(firstSave[15], 1u);
}

TEST_F(TitleScreenTest, ATwoModeGamesSaveThatWentBadContinuesAsANewSoloMatch) { continueAfterTheSaveChanged(false); }

TEST_F(TitleScreenTest, ATwoModeGamesSaveRemovedAfterThePeekContinuesAsANewSoloMatch) {
  continueAfterTheSaveChanged(true);
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

// The question through the launcher: a game with a save, its own row, and its title screen's New game.
TEST_F(TitleScreenTest, NewGameReachedFromTheLauncherAsksFirstOverASave) {
  addCountingGame("counter", "Counter", "\"solo\"", 1, 1);
  save("counter");
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  tapRow(tr(STR_GAMES_NEW_GAME));
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

// ---- the splash band (DESIGN.md, Title-screen splash): title.bmp, else the icon at 128 px ----

// The band is 480 x 480 under the header (topPadding 5 and headerHeight 84 of the base metrics, on the harness's
// portrait screen whose safe area starts at 0), so the icon's 128 x 128 box is centred at (240, 89 + 240).
constexpr int BAND_TOP = 5 + 84;
constexpr int ICON_LEFT = 240 - 64;
constexpr int ICON_TOP = BAND_TOP + 240 - 64;

// Black pixels the renderer holds in (x, y, w, h).
size_t blackIn(const GfxRenderer& renderer, const int x, const int y, const int w, const int h) {
  size_t black = 0;
  for (int py = y; py < y + h; ++py)
    for (int px = x; px < x + w; ++px)
      if (renderer.pixel(px, py) == GfxRenderer::PixelBlack) ++black;
  return black;
}

TEST_F(TitleScreenTest, WithNoTitleImageTheBandShowsTheLibraryIconAt128CentredAndTheRowsFollowIt) {
  installCounter("\"solo\"", 1);
  openTitleFor(SOLO, 1);
  const size_t inBox = blackIn(*renderer, ICON_LEFT, ICON_TOP, 128, 128);
  EXPECT_GT(inBox, 0u) << "game-controller, the fallback, at 128 px";
  EXPECT_EQ(blackIn(*renderer, 0, BAND_TOP, 480, 480), inBox) << "nothing else in the band";
  ASSERT_FALSE(ui().drawn.empty());
  const auto row = std::find_if(ui().drawn.begin(), ui().drawn.end(),
                                [](const screen::DrawnText& d) { return d.text == tr(STR_GAMES_NEW_GAME); });
  ASSERT_NE(row, ui().drawn.end()) << ui().joined();
  EXPECT_GE(row->rect.y, BAND_TOP + GameSplashLayout::BAND) << "the rows start under the band";
}

TEST_F(TitleScreenTest, APackageIconIsDrawnAt128WithEachOfItsPixelsTwoByTwo) {
  installCounter("\"solo\"", 1);
  const auto white = [](const int x, const int y) { return !harness::speckle(x, y); };
  fakesd::addFile("/.games/counter/icon.bmp", harness::bmpFile(64, 64, white));
  openTitleFor(SOLO, 1);
  int wrong = 0;
  for (int y = 0; y < 128; ++y)
    for (int x = 0; x < 128; ++x) {
      const bool black = renderer->pixel(ICON_LEFT + x, ICON_TOP + y) == GfxRenderer::PixelBlack;
      if (black == white(x / 2, y / 2)) ++wrong;
    }
  EXPECT_EQ(wrong, 0);
  EXPECT_EQ(blackIn(*renderer, 0, BAND_TOP, 480, 480), blackIn(*renderer, ICON_LEFT, ICON_TOP, 128, 128));
}

TEST_F(TitleScreenTest, ATitleImageIsDrawnCentredInTheBandInsteadOfTheIcon) {
  installCounter("\"solo\"", 1);
  const auto white = [](const int x, const int y) { return !harness::speckle(x, y, 3); };
  fakesd::addFile("/.games/counter/title.bmp", harness::bmpFile(100, 60, white));
  openTitleFor(SOLO, 1);
  const int left = (480 - 100) / 2;
  const int top = BAND_TOP + (480 - 60) / 2;
  int wrong = 0;
  for (int y = 0; y < 60; ++y)
    for (int x = 0; x < 100; ++x)
      if ((renderer->pixel(left + x, top + y) == GfxRenderer::PixelBlack) == white(x, y)) ++wrong;
  EXPECT_EQ(wrong, 0);
  EXPECT_EQ(blackIn(*renderer, 0, BAND_TOP, 480, 480), blackIn(*renderer, left, top, 100, 60))
      << "the image alone: no icon, no text";
  EXPECT_EQ(fakepsram::liveBlocks, 1u) << "its rows are held in PSRAM";
}

TEST_F(TitleScreenTest, AFullSizeTitleImageFillsTheBandExactly) {
  installCounter("\"solo\"", 1);
  fakesd::addFile("/.games/counter/title.bmp", harness::bmpFile(480, 480, [](int, int) { return false; }));
  openTitleFor(SOLO, 1);
  EXPECT_EQ(blackIn(*renderer, 0, BAND_TOP, 480, 480), 480u * 480u);
  EXPECT_EQ(blackIn(*renderer, 0, BAND_TOP - 1, 480, 1), 0u) << "the header is left alone";
  EXPECT_EQ(blackIn(*renderer, 0, BAND_TOP + 480, 480, 1), 0u) << "and the rows";
}

// GameSplashLayout::rowRect, which places the hidden hand-off screen's "Player N's turn" and "I'm ready", is where the
// title screen's list lays out its two-line rows (Continue and New game, over a save): the list's own row fills (each
// row's background, the selected one's dither) are the helper's rects, row for row, and the rows start right under the
// band. The hand-off screen's suite (HiddenPassTest) pins its text and button to the same helper.
TEST_F(TitleScreenTest, TheSplashLayoutsRowRectsAreTheRowsTheTitleScreensListDraws) {
  installCounter("\"solo\",\"pass\"");
  save("counter");
  openTitleFor(SOLO | PASS);
  ASSERT_TRUE(ui().drewLine(tr(STR_GAMES_CONTINUE))) << ui().joined();
  ASSERT_TRUE(ui().drewLine(tr(STR_GAMES_OPTIONS))) << ui().joined();
  const std::vector<freeink::ui::Rect> rows = match::splashMenuRows(*renderer, 2);
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[0].y, BAND_TOP + GameSplashLayout::BAND) << "the first row starts right under the band";
  std::vector<freeink::ui::Rect> drawn;
  for (const freeink::ui::Rect& fill : ui().fillRects)
    if (fill.y >= BAND_TOP + GameSplashLayout::BAND) drawn.push_back(fill);
  ASSERT_GE(drawn.size(), 2u) << "the list drew no rows under the band";
  for (size_t i = 0; i < 2; ++i) {
    SCOPED_TRACE(i);
    EXPECT_EQ(drawn[i].x, rows[i].x);
    EXPECT_EQ(drawn[i].y, rows[i].y);
    EXPECT_EQ(drawn[i].width, rows[i].width);
    EXPECT_EQ(drawn[i].height, rows[i].height);
  }
  // And the rows' texts sit inside them: Continue's in row 0, New game's in row 1.
  for (const screen::DrawnText& text : ui().drawn) {
    if (text.text == tr(STR_GAMES_CONTINUE)) {
      EXPECT_TRUE(rows[0].contains(text.rect.x, text.rect.y)) << text.text;
    }
    if (text.text == tr(STR_GAMES_NEW_GAME)) {
      EXPECT_TRUE(rows[1].contains(text.rect.x, text.rect.y)) << text.text;
    }
  }
}

// A title.bmp the screen cannot use is the icon, logged: one larger than 480 x 480 (only a hand-copied folder holds
// one: the installer refuses it), one that will not open, and one that is not the converter's layout.
TEST_F(TitleScreenTest, ATitleImageItCannotUseFallsBackToTheIconLogged) {
  const auto expectIcon = [&](const std::string& why) {
    SCOPED_TRACE(why);
    openTitleFor(SOLO, 1);
    EXPECT_GT(blackIn(*renderer, ICON_LEFT, ICON_TOP, 128, 128), 0u);
    EXPECT_EQ(blackIn(*renderer, 0, BAND_TOP, 480, 480), blackIn(*renderer, ICON_LEFT, ICON_TOP, 128, 128));
    EXPECT_TRUE(logHas("title.bmp"));
    EXPECT_TRUE(logHas("; skipped"));
    EXPECT_EQ(fakepsram::liveBlocks, 0u) << "nothing held";
    dropTitle();
    fakelog::clearLines();
  };
  installCounter("\"solo\"", 1);
  fakesd::addFile("/.games/counter/title.bmp", harness::bmpFile(481, 10, [](int, int) { return false; }));
  expectIcon("481 wide");
  fakesd::addFile("/.games/counter/title.bmp", harness::bmpFile(10, 481, [](int, int) { return false; }));
  expectIcon("481 high");
  fakesd::addFile("/.games/counter/title.bmp", harness::bmpFile(480, 480, [](int, int) { return false; }));
  fakesd::sim().failOpen.insert("/.games/counter/title.bmp");
  expectIcon("will not open");
  fakesd::sim().failOpen.clear();
  fakesd::addFile("/.games/counter/title.bmp", std::string("not a bitmap at all, and long enough to be read as one"));
  expectIcon("not a BMP");
}

TEST_F(TitleScreenTest, TheTitleImageAndThePrefsAreReadOnceWhenTheScreenOpens) {
  installCounter("\"solo\",\"pass\"");
  fakesd::addFile("/.games/counter/title.bmp", harness::bmpFile(48, 48, [](int, int) { return false; }));
  remember("counter", PASS);
  // "open <path>" exactly: prefs.bin.tmp, which remember() wrote, is another file.
  const auto opens = [](const std::string& path) {
    return static_cast<size_t>(std::count(fakesd::sim().ops.begin(), fakesd::sim().ops.end(), "open " + path));
  };
  const size_t titleBefore = opens("/.games/counter/title.bmp");
  const size_t prefsBefore = opens(prefsPath("counter"));
  openTitleFor(SOLO | PASS);
  const size_t title = opens("/.games/counter/title.bmp");
  const size_t prefs = opens(prefsPath("counter"));
  EXPECT_EQ(title, titleBefore + 1);
  EXPECT_EQ(prefs, prefsBefore + 1);
  for (int i = 0; i < 3; ++i) render();
  input->click(Button::NavNext);
  frame();
  render();
  EXPECT_EQ(opens("/.games/counter/title.bmp"), title) << "no read while the screen is drawn";
  EXPECT_EQ(opens(prefsPath("counter")), prefs);
}

// The question is drawn as on a plain list screen: no splash band behind it.
TEST_F(TitleScreenTest, TheSplashBandIsNotDrawnUnderTheNewOverSaveQuestion) {
  addCountingGame("counter", "Counter", "\"solo\"", 1, 1);
  save("counter");
  fakesd::addFile("/.games/counter/title.bmp", harness::bmpFile(480, 480, [](int, int) { return false; }));
  openTitleFor(SOLO, 1);
  ASSERT_EQ(blackIn(*renderer, 0, BAND_TOP, 480, 480), 480u * 480u) << "the band, before the question";
  tapRow(tr(STR_GAMES_NEW_GAME));
  render();
  ASSERT_TRUE(dialogUp());
  EXPECT_EQ(blackIn(*renderer, 0, BAND_TOP, 480, 480), 0u);
}

// ---- the current mode and the settings (Manifest::startMode, GameSaveStore::resolvePrefs) ----

TEST_F(TitleScreenTest, DefaultModeIsTheCurrentModeUntilAnotherIsRemembered) {
  addCountingGame("counter", "Counter", "\"solo\",\"pass\"", 1, 2, ",\"default_mode\":\"pass\"");
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  EXPECT_EQ(newGameLine(), tr(STR_GAMES_MODE_PASS)) << "the developer's default";
  dropTitle();
  activityManager.exitHolding(*list);
  activityManager.destroyHolding(list);
  activityManager.reset();
  remember("counter", SOLO);
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  EXPECT_EQ(newGameLine(), tr(STR_GAMES_MODE_SOLO)) << "the remembered mode wins";
}

TEST_F(TitleScreenTest, TheNewGameLineNamesTheModeAndEachSettingsValueAndTheGameGetsThemAsCtxSettings) {
  addCountingGame("counter", "Counter", "\"solo\"", 1, 1, TWO_SETTINGS, SETTINGS_GAME);
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  const std::vector<std::string> rows{tr(STR_GAMES_NEW_GAME), tr(STR_GAMES_OPTIONS)};
  EXPECT_EQ(rowsDrawn(), rows) << "one mode, but settings: Options";
  EXPECT_EQ(newGameLine(), std::string(tr(STR_GAMES_MODE_SOLO)) + " \xC2\xB7 Hard \xC2\xB7 Small")
      << "the defaults, in manifest order";
  tapRow(tr(STR_GAMES_NEW_GAME));
  ASSERT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);
  ASSERT_TRUE(pumpMatchTo("settings\t[board=Small,level=Hard]"));
}

// The story's first acceptance criterion: a save and a remembered pass mode with settings. The rows are Continue
// (selected), New game in the remembered mode and the settings' values, and Options; Confirm from there resumes the
// save, three taps from Home (Games, the game, Continue).
TEST_F(TitleScreenTest, ASaveAndARememberedPassModeWithSettingsAreContinueSelectedNewGameAndOptions) {
  addCountingGame("counter", "Counter", "\"solo\",\"pass\"", 1, 2, TWO_SETTINGS);
  save("counter");
  remember("counter", PASS);
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  const std::vector<std::string> rows{tr(STR_GAMES_CONTINUE), tr(STR_GAMES_NEW_GAME), tr(STR_GAMES_OPTIONS)};
  EXPECT_EQ(rowsDrawn(), rows);
  EXPECT_EQ(newGameLine(), std::string(tr(STR_GAMES_MODE_PASS)) + " \xC2\xB7 Hard \xC2\xB7 Small");
  input->click(Button::Confirm);  // the selection starts on Continue
  frame();
  EXPECT_EQ(taps, 1) << "the game's row, then the start: with Games, three";
  ASSERT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("counter: resuming the save's roster: solo, 1 seat(s)"))
      << "the save's roster, not the current mode";
  ASSERT_TRUE(pumpMatchTo("Resuming at ver 3"));
}

// prefs.bin out of date with the manifest falls back value by value: a mode this host cannot start takes the default,
// a value the setting no longer has takes the setting's default, an id the manifest no longer declares is dropped, and
// a value still declared is kept.
TEST_F(TitleScreenTest, AStaleRememberedChoiceFallsBackValueByValue) {
  addCountingGame("counter", "Counter", "\"solo\",\"pass\"", 1, 2, ",\"default_mode\":\"pass\"" + TWO_SETTINGS,
                  SETTINGS_GAME);
  GameSaveStore::Prefs prefs;
  prefs.mode = NEARBY;  // the host has no radio
  const std::pair<const char*, const char*> entries[] = {{"level", "Impossible"}, {"gone", "Yes"}, {"board", "Large"}};
  for (const auto& [id, value] : entries) {
    GameCore::SettingValues::Entry& entry = prefs.settings.entries[prefs.settings.count++];
    std::snprintf(entry.id, sizeof(entry.id), "%s", id);
    std::snprintf(entry.value, sizeof(entry.value), "%s", value);
  }
  ASSERT_TRUE(GameSaveStore::savePrefs("counter", prefs));
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  EXPECT_EQ(newGameLine(), std::string(tr(STR_GAMES_MODE_PASS)) + " \xC2\xB7 Hard \xC2\xB7 Large");
}

TEST_F(TitleScreenTest, ALongNewGameLineIsOneLineEndingInAnEllipsis) {
  std::string settings = ",\"settings\":[";
  for (int i = 0; i < 4; ++i) {
    settings += std::string(i ? "," : "") + "{\"id\":\"s" + std::to_string(i) + "\",\"name\":\"Setting " +
                std::to_string(i) + "\",\"values\":[\"abcdefghijklmnop\",\"b\"]}";
  }
  settings += "]";
  addCountingGame("counter", "Counter", "\"solo\"", 1, 1, settings);
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  const std::string full = std::string(tr(STR_GAMES_MODE_SOLO)) +
                           " \xC2\xB7 abcdefghijklmnop \xC2\xB7 abcdefghijklmnop \xC2\xB7 abcdefghijklmnop \xC2\xB7 "
                           "abcdefghijklmnop";
  const std::string line = newGameLine();
  EXPECT_NE(line, full);
  EXPECT_EQ(line.rfind(tr(STR_GAMES_MODE_SOLO), 0), 0u) << line;
  EXPECT_TRUE(line.size() >= 3 &&
              (line.compare(line.size() - 3, 3, "...") == 0 || line.compare(line.size() - 3, 3, "\xE2\x80\xA6") == 0))
      << line;
  EXPECT_EQ(rowsDrawn(), (std::vector<std::string>{tr(STR_GAMES_NEW_GAME), tr(STR_GAMES_OPTIONS)}));
}

// ---- Options (EXPERIENCE.md, Options row) ----

TEST_F(TitleScreenTest, OptionsOfOneModeWithSettingsIsASettingRowEachAndNoModeRow) {
  addCountingGame("counter", "Counter", "\"solo\"", 1, 1, TWO_SETTINGS);
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  ASSERT_NO_FATAL_FAILURE(openOptions());
  EXPECT_TRUE(theme().drew("drawHeader", tr(STR_GAMES_OPTIONS)));
  EXPECT_FALSE(ui().drewLine(tr(STR_GAMES_MODE)));
  EXPECT_EQ(lineUnder("Level"), "Hard");
  EXPECT_EQ(lineUnder("Board"), "Small");
}

// A tap cycles the value in place and wraps; Back returns to the title screen with the Options row selected and New
// game's line showing the choice, and prefs.bin holds it; the next New game starts with it.
TEST_F(TitleScreenTest, OptionsCyclesInPlaceAndBackRemembersTheChoiceAndSelectsOptions) {
  addCountingGame("counter", "Counter", "\"solo\",\"pass\"", 1, 2, TWO_SETTINGS, SETTINGS_GAME);
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  ASSERT_NO_FATAL_FAILURE(openOptions());
  const std::vector<std::string> labels{tr(STR_GAMES_MODE), "Level", "Board"};
  std::vector<std::string> drawn;
  for (const screen::DrawnText& line : ui().drawn)
    if (std::find(labels.begin(), labels.end(), line.text) != labels.end()) drawn.push_back(line.text);
  EXPECT_EQ(drawn, labels) << "Mode first, then the settings in manifest order";
  tapRow("Level");
  render();
  EXPECT_EQ(lineUnder("Level"), "Easy");
  tapRow("Level");
  render();
  EXPECT_EQ(lineUnder("Level"), "Hard") << "wraps after the last";
  tapRow("Level");
  tapRow("Board");
  input->click(Button::NavPrevious);  // up to Mode, by buttons
  frame();
  input->click(Button::NavPrevious);
  frame();
  input->click(Button::Confirm);
  frame();
  render();
  EXPECT_EQ(lineUnder(tr(STR_GAMES_MODE)), tr(STR_GAMES_MODE_PASS));
  EXPECT_EQ(lineUnder("Level"), "Easy");
  EXPECT_EQ(lineUnder("Board"), "Large");
  EXPECT_FALSE(fakesd::has(prefsPath("counter"))) << "nothing is written while Options is open";

  ASSERT_NO_FATAL_FAILURE(closeOptions());
  EXPECT_EQ(newGameLine(), std::string(tr(STR_GAMES_MODE_PASS)) + " \xC2\xB7 Easy \xC2\xB7 Large");
  const GameSaveStore::Prefs remembered = prefsOf("counter");
  EXPECT_EQ(remembered.mode, PASS);
  ASSERT_EQ(remembered.settings.count, 2u);
  EXPECT_STREQ(remembered.settings.entries[0].id, "level");
  EXPECT_STREQ(remembered.settings.entries[0].value, "Easy");
  EXPECT_STREQ(remembered.settings.entries[1].id, "board");
  EXPECT_STREQ(remembered.settings.entries[1].value, "Large");
  // The Options row is selected: Confirm opens Options again.
  input->click(Button::Confirm);
  frame();
  ASSERT_EQ(activityManager.pushedActivities.size(), 2u);
  EXPECT_NE(dynamic_cast<GameOptionsActivity*>(activityManager.pushedActivities.back().get()), nullptr);
  activityManager.popForResult(*title, *activityManager.pushedActivities.back());
  current = title;
  render();

  tapRow(tr(STR_GAMES_NEW_GAME));
  ASSERT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Mode pass picked for counter: 2 seats"));
  ASSERT_TRUE(pumpMatchTo("settings\t[board=Large,level=Easy]"));
}

// Options left by a Replace (the Home gesture, sleep) runs no result handler: the real manager exits Options, then
// calls onExit() of each screen stacked under it, RenderLock held. prefs.bin is never written from onExit() (AD-17), so
// the change is not remembered (deferred-work.md ## 5.12) and nothing touches the card.
TEST_F(TitleScreenTest, OptionsLeftByAReplaceWritesNothing) {
  installCounter("\"solo\",\"pass\"");
  openTitleFor(SOLO | PASS);
  ASSERT_NO_FATAL_FAILURE(openOptions());
  tapRow(tr(STR_GAMES_MODE));
  // ActivityManager's Replace (goHome): the current screen exits and goes, then the stacked one exits.
  auto& pushed = activityManager.pushedActivities;
  ASSERT_EQ(pushed.back().get(), options);
  activityManager.exitHolding(*options);
  activityManager.destroyHolding(pushed.back());
  pushed.pop_back();
  options = nullptr;
  activityManager.exitHolding(*title);
  EXPECT_FALSE(fakesd::has(prefsPath("counter")));
  EXPECT_EQ(fakelock::selfDeadlocks.load(), 0) << "onExit takes no RenderLock";
}

// The Mode row cycles from solo when the current mode is none of the host's, as nextMode documents.
TEST(TitleScreenModes, NextModeStartsAtSoloWhenTheCurrentModeIsNoneOfThem) {
  EXPECT_EQ(GameModeActivity::nextMode(0, SOLO | PASS), SOLO);
  EXPECT_EQ(GameModeActivity::nextMode(NEARBY, SOLO | PASS), SOLO);
  EXPECT_EQ(GameModeActivity::nextMode(0, PASS | NEARBY), PASS);
  EXPECT_EQ(GameModeActivity::nextMode(SOLO, SOLO | PASS), PASS);
  EXPECT_EQ(GameModeActivity::nextMode(PASS, SOLO | PASS), SOLO);
  EXPECT_EQ(GameModeActivity::nextMode(SOLO, 0), 0);
}

// Settings the title screen could not read would be written as none, wiping every remembered value, and a match would
// start without the ctx.settings its manifest declares: while they are not read, New game and Continue start nothing
// (logged, the screen repainted), an Options change is not written, and prefs.bin stays as it was.
TEST_F(TitleScreenTest, WhileTheSettingsCouldNotBeReadPrefsBinIsLeftAlone) {
  addCountingGame("counter", "Counter", "\"solo\",\"pass\"", 1, 2, TWO_SETTINGS);
  GameSaveStore::Prefs prefs;
  prefs.mode = NEARBY;  // not startable here: New game starts another mode, which would write the file
  GameCore::SettingValues::Entry& entry = prefs.settings.entries[prefs.settings.count++];
  std::snprintf(entry.id, sizeof(entry.id), "level");
  std::snprintf(entry.value, sizeof(entry.value), "Easy");
  ASSERT_TRUE(GameSaveStore::savePrefs("counter", prefs));
  const Bytes before = fakesd::bytesOf(prefsPath("counter"));
  openLauncher();
  tapRow("Counter");
  fakesd::sim().failOpen.insert("/.games/counter/manifest.json");  // the title screen's re-read fails
  ASSERT_NO_FATAL_FAILURE(openPushedTitle());
  EXPECT_TRUE(logHas("Cannot read the settings of counter; it does not start"));
  activityManager.markRendered();
  tapRow(tr(STR_GAMES_NEW_GAME));
  EXPECT_EQ(activityManager.asks.replaced, 0) << "a match started without its settings";
  EXPECT_TRUE(activityManager.replacements.empty());
  EXPECT_TRUE(logHas("Cannot start counter: its settings were not read"));
  EXPECT_TRUE(activityManager.updateRequested());
  EXPECT_EQ(fakesd::bytesOf(prefsPath("counter")), before);
  // An Options change (Mode: there are two) is not written either.
  ASSERT_NO_FATAL_FAILURE(openOptions());
  tapRow(tr(STR_GAMES_MODE));
  ASSERT_NO_FATAL_FAILURE(closeOptions());
  EXPECT_TRUE(logHas("The choices for counter are not remembered: its settings were not read"));
  EXPECT_EQ(fakesd::bytesOf(prefsPath("counter")), before);
  tapRow(tr(STR_GAMES_NEW_GAME));
  EXPECT_EQ(activityManager.asks.replaced, 0);
}

// The same with a save: Continue starts nothing either (a Play again after it would run setup without the settings).
TEST_F(TitleScreenTest, WhileTheSettingsCouldNotBeReadContinueStartsNothing) {
  addCountingGame("counter", "Counter", "\"solo\"", 1, 1, TWO_SETTINGS);
  save("counter");
  const Bytes saved = fakesd::bytesOf(resumePath("counter"));
  openLauncher();
  tapRow("Counter");
  fakesd::sim().failOpen.insert("/.games/counter/manifest.json");  // the title screen's re-read fails
  ASSERT_NO_FATAL_FAILURE(openPushedTitle());
  activityManager.markRendered();
  tapRow(tr(STR_GAMES_CONTINUE));
  EXPECT_EQ(activityManager.asks.replaced, 0);
  EXPECT_TRUE(logHas("Cannot start counter: its settings were not read"));
  EXPECT_TRUE(activityManager.updateRequested());
  EXPECT_EQ(fakesd::bytesOf(resumePath("counter")), saved);
}

// Settings that could not be read when the screen opened stay unread until the screen opens again, even once the card
// fault has passed: New game starts nothing, logged, and the mode Options chose meanwhile is not written
// (rememberChoices skips while the settings are unread). A read again at the tap costs +80 B of flash the share does
// not have (deferred-work.md ## 5.12, with G7).
TEST_F(TitleScreenTest, SettingsThatCouldNotBeReadAtOpenAreNotReadAgainWhenAMatchStarts) {
  addCountingGame("counter", "Counter", "\"solo\",\"pass\"", 1, 2, TWO_SETTINGS, SETTINGS_GAME);
  GameSaveStore::Prefs prefs;
  prefs.mode = SOLO;
  ASSERT_TRUE(GameSaveStore::savePrefs("counter", prefs));
  const Bytes before = fakesd::bytesOf(prefsPath("counter"));
  openLauncher();
  tapRow("Counter");
  fakesd::sim().failOpen.insert("/.games/counter/manifest.json");  // the title screen's read fails
  ASSERT_NO_FATAL_FAILURE(openPushedTitle());
  ASSERT_TRUE(logHas("Cannot read the settings of counter; it does not start"));
  ASSERT_NO_FATAL_FAILURE(openOptions());
  tapRow(tr(STR_GAMES_MODE));  // solo -> pass
  ASSERT_NO_FATAL_FAILURE(closeOptions());
  fakesd::sim().failOpen.clear();  // the fault has passed
  tapRow(tr(STR_GAMES_NEW_GAME));
  EXPECT_EQ(activityManager.asks.replaced, 0);
  EXPECT_EQ(fakelog::countLines("Cannot read the settings of counter"), 1u) << "read again at the tap";
  EXPECT_TRUE(logHas("Cannot start counter: its settings were not read"));
  EXPECT_EQ(fakesd::bytesOf(prefsPath("counter")), before);
}

// A prefs.bin that would not read (a card fault) may be a good one: New game does not replace it with defaults, but
// an Options change, which the player made, does.
TEST_F(TitleScreenTest, AnUnreadablePrefsBinIsNotReplacedByNewGame) {
  installCounter("\"solo\",\"pass\"");
  remember("counter", PASS);
  const Bytes before = fakesd::bytesOf(prefsPath("counter"));
  fakesd::sim().failOpen.insert(prefsPath("counter"));
  openTitleFor(SOLO | PASS);
  EXPECT_EQ(newGameLine(), tr(STR_GAMES_MODE_SOLO)) << "unreadable: the defaults";
  const size_t writes = fakesd::countOps("rename " + prefsPath("counter") + ".tmp");
  tapRow(tr(STR_GAMES_NEW_GAME));
  ASSERT_EQ(activityManager.asks.replaced, 1);
  EXPECT_EQ(fakesd::countOps("rename " + prefsPath("counter") + ".tmp"), writes);
  EXPECT_EQ(fakesd::bytesOf(prefsPath("counter")), before);
}

// Still unreadable when Options closes with a change: the choices are written as they are (the defaults but the one
// the player changed).
TEST_F(TitleScreenTest, AnUnreadablePrefsBinIsReplacedByAnOptionsChange) {
  installCounter("\"solo\",\"pass\"");
  remember("counter", SOLO);
  fakesd::sim().failOpen.insert(prefsPath("counter"));
  openTitleFor(SOLO | PASS);
  ASSERT_NO_FATAL_FAILURE(openOptions());
  tapRow(tr(STR_GAMES_MODE));
  const int opens = fakesd::sim().opens[prefsPath("counter")];
  ASSERT_NO_FATAL_FAILURE(closeOptions());
  EXPECT_EQ(fakesd::sim().opens[prefsPath("counter")], opens + 1) << "not read again before the write";
  fakesd::sim().failOpen.clear();
  EXPECT_EQ(prefsOf("counter").mode, PASS);
}

// A prefs.bin that would not read when the screen opened (a card fault that has passed) and reads when Options closes
// with a change: the change is merged into what it holds. The player's own changes win over the file (the mode, solo to
// pass, over the file's solo; level, Hard to Easy, over the file's Hard), and the setting left as the screen opened it
// (board, cycled away and back) keeps the file's value, not the default the screen opened with; the New game line
// shows the merge.
TEST_F(TitleScreenTest, AnUnreadablePrefsBinThatReadsAgainKeepsItsValuesForWhatOptionsLeftAlone) {
  addCountingGame("counter", "Counter", "\"solo\",\"pass\"", 1, 2, TWO_SETTINGS, SETTINGS_GAME);
  GameSaveStore::Prefs prefs;
  prefs.mode = SOLO;
  const std::pair<const char*, const char*> entries[] = {{"level", "Hard"}, {"board", "Large"}};
  for (const auto& [id, value] : entries) {
    GameCore::SettingValues::Entry& entry = prefs.settings.entries[prefs.settings.count++];
    std::snprintf(entry.id, sizeof(entry.id), "%s", id);
    std::snprintf(entry.value, sizeof(entry.value), "%s", value);
  }
  ASSERT_TRUE(GameSaveStore::savePrefs("counter", prefs));
  fakesd::sim().failOpen.insert(prefsPath("counter"));
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  EXPECT_EQ(newGameLine(), std::string(tr(STR_GAMES_MODE_SOLO)) + " \xC2\xB7 Hard \xC2\xB7 Small")
      << "unreadable: the defaults";
  ASSERT_NO_FATAL_FAILURE(openOptions());
  tapRow(tr(STR_GAMES_MODE));      // solo -> pass: the player's change
  tapRow("Level");                 // Hard -> Easy: the player's change
  tapRow("Board");                 // Small -> Large
  tapRow("Board");                 // Large -> Small: back, as the screen opened it
  fakesd::sim().failOpen.clear();  // the fault has passed
  // The merge rewrites the choices the render task reads: onOptionsClosed holds RenderLock around it, through the
  // write.
  bool savedUnderLock = false;
  bool saved = false;
  fakelog::hook() = [&](const std::string& line) {
    if (line.find("counter: saved prefs.bin") == std::string::npos) return;
    saved = true;
    savedUnderLock = fakelock::held();
  };
  closeOptions();
  fakelog::hook() = nullptr;  // before any ASSERT: the hook refers to this frame's locals
  ASSERT_FALSE(HasFatalFailure());
  ASSERT_TRUE(saved) << "no prefs.bin write logged";
  EXPECT_TRUE(savedUnderLock) << "the merge and write ran outside RenderLock";
  const GameSaveStore::Prefs remembered = prefsOf("counter");
  EXPECT_EQ(remembered.mode, PASS) << "the player's mode";
  ASSERT_EQ(remembered.settings.count, 2u);
  EXPECT_STREQ(remembered.settings.entries[0].value, "Easy") << "the player's level";
  EXPECT_STREQ(remembered.settings.entries[1].value, "Large") << "the setting left as opened keeps the file's";
  EXPECT_EQ(newGameLine(), std::string(tr(STR_GAMES_MODE_PASS)) + " \xC2\xB7 Easy \xC2\xB7 Large");
}

// The mode is merged as the settings are: a prefs.bin holding pass, unreadable when the screen opened (so the screen
// opened in the game's default, solo) and readable when Options closes after only Level changed, keeps pass.
TEST_F(TitleScreenTest, AnUnreadablePrefsBinThatReadsAgainKeepsItsModeWhenOptionsLeftItAlone) {
  addCountingGame("counter", "Counter", "\"solo\",\"pass\"", 1, 2, TWO_SETTINGS, SETTINGS_GAME);
  GameSaveStore::Prefs prefs;
  prefs.mode = PASS;
  ASSERT_TRUE(GameSaveStore::savePrefs("counter", prefs));
  fakesd::sim().failOpen.insert(prefsPath("counter"));
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  EXPECT_EQ(newGameLine(), std::string(tr(STR_GAMES_MODE_SOLO)) + " \xC2\xB7 Hard \xC2\xB7 Small")
      << "unreadable: the defaults";
  ASSERT_NO_FATAL_FAILURE(openOptions());
  tapRow("Level");                 // Hard -> Easy: the only change
  fakesd::sim().failOpen.clear();  // the fault has passed
  ASSERT_NO_FATAL_FAILURE(closeOptions());
  const GameSaveStore::Prefs remembered = prefsOf("counter");
  EXPECT_EQ(remembered.mode, PASS) << "the mode Options left alone took the default the screen opened with";
  ASSERT_EQ(remembered.settings.count, 2u);
  EXPECT_STREQ(remembered.settings.entries[0].value, "Easy");
  EXPECT_EQ(newGameLine(), std::string(tr(STR_GAMES_MODE_PASS)) + " \xC2\xB7 Easy \xC2\xB7 Small");
}

TEST_F(TitleScreenTest, OptionsClosedWithNoChangeWritesNothing) {
  installCounter("\"solo\",\"pass\"");
  openTitleFor(SOLO | PASS);
  ASSERT_NO_FATAL_FAILURE(openOptions());
  ASSERT_NO_FATAL_FAILURE(closeOptions());
  EXPECT_FALSE(fakesd::has(prefsPath("counter")));
  EXPECT_EQ(newGameLine(), tr(STR_GAMES_MODE_SOLO));
  EXPECT_EQ(rowsDrawn(), (std::vector<std::string>{tr(STR_GAMES_NEW_GAME), tr(STR_GAMES_OPTIONS)}));
}

// A write that fails is logged, and the choice lasts until the title screen closes: New game starts with it, and the
// next title screen has the defaults again.
TEST_F(TitleScreenTest, AFailedPrefsWriteIsLoggedAndTheChoiceLastsUntilTheScreenCloses) {
  addCountingGame("counter", "Counter", "\"solo\"", 1, 1, TWO_SETTINGS, SETTINGS_GAME);
  fakesd::sim().failOpenWrite.insert(prefsPath("counter") + ".tmp");
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  ASSERT_NO_FATAL_FAILURE(openOptions());
  tapRow("Level");
  ASSERT_NO_FATAL_FAILURE(closeOptions());
  EXPECT_TRUE(logHas("The choices for counter are not remembered"));
  EXPECT_FALSE(fakesd::has(prefsPath("counter")));
  EXPECT_EQ(newGameLine(), std::string(tr(STR_GAMES_MODE_SOLO)) + " \xC2\xB7 Easy \xC2\xB7 Small");
  tapRow(tr(STR_GAMES_NEW_GAME));
  ASSERT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);
  ASSERT_TRUE(pumpMatchTo("settings\t[board=Small,level=Easy]"));
  match::letStartedMatchesGo([this] { dropMatch(); }, match::Saves::Discard);
  dropTitle();
  activityManager.exitHolding(*list);
  activityManager.destroyHolding(list);
  activityManager.reset();
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Counter"));
  EXPECT_EQ(newGameLine(), std::string(tr(STR_GAMES_MODE_SOLO)) + " \xC2\xB7 Hard \xC2\xB7 Small");
}

// prefs.bin is written when New game starts a mode other than the one it holds (a missing file holds none), and only
// then.
TEST_F(TitleScreenTest, NewGameInAModeOtherThanTheRememberedOneRemembersIt) {
  installCounter("\"solo\",\"pass\"");
  openTitleFor(SOLO | PASS);
  tapRow(tr(STR_GAMES_NEW_GAME));
  ASSERT_EQ(activityManager.asks.replaced, 1);
  EXPECT_EQ(prefsOf("counter").mode, SOLO) << "a missing file holds no mode";
  dropMatch();
  dropTitle();
  activityManager.reset();

  const auto prefsWrites = [] { return fakesd::countOps("rename " + prefsPath("counter") + ".tmp"); };
  const size_t writes = prefsWrites();
  openTitleFor(SOLO | PASS);
  tapRow(tr(STR_GAMES_NEW_GAME));
  ASSERT_EQ(activityManager.asks.replaced, 1);
  EXPECT_EQ(prefsWrites(), writes) << "the mode the file holds: nothing written";
}

TEST_F(TitleScreenTest, ContinueWritesNoPrefs) {
  addCountingGame("counter", "Counter", "\"solo\"", 1, 1);
  save("counter");
  openTitleFor(SOLO, 1);
  tapRow(tr(STR_GAMES_CONTINUE));
  ASSERT_EQ(activityManager.asks.replaced, 1);
  EXPECT_FALSE(fakesd::has(prefsPath("counter")));
}

TEST_F(TitleScreenTest, AnOptionsScreenThatCannotBeAllocatedOpensNothingAndRepaints) {
  installCounter("\"solo\",\"pass\"");
  openTitleFor(SOLO | PASS);
  oom::failSize = sizeof(GameOptionsActivity);
  activityManager.markRendered();
  tapRow(tr(STR_GAMES_OPTIONS));
  EXPECT_EQ(activityManager.pushedActivities.size(), 1u);
  EXPECT_TRUE(logHas("OOM: " + std::to_string(sizeof(GameOptionsActivity)) + " byte Options screen"));
  EXPECT_TRUE(activityManager.updateRequested());
}

// ---- GamePicture on its own: what the title screen's band and the hand-off page draw with ----

class GamePictureTest : public match::ScreenTest {};

// A page larger than the rect it is drawn in shows its middle, clipped to the rect: nothing is drawn outside it.
TEST_F(GamePictureTest, APageLargerThanItsRectShowsItsMiddleClippedToIt) {
  const auto white = [](const int x, const int y) { return !harness::speckle(x, y, 1); };
  fakesd::addFile("/.games/demo/handoff.bmp", harness::bmpFile(100, 90, white));
  GamePicture picture;
  ASSERT_TRUE(picture.loadPage("demo", "handoff", 100, 90));
  renderer->clearScreen();
  picture.drawPage(*renderer, 10, 20, 40, 30);
  int wrong = 0;
  for (int y = 0; y < 30; ++y)
    for (int x = 0; x < 40; ++x)
      if ((renderer->pixel(10 + x, 20 + y) == GfxRenderer::PixelBlack) == white(x + 30, y + 30)) ++wrong;
  EXPECT_EQ(wrong, 0) << "the image's (30, 30) is the rect's top-left";
  EXPECT_EQ(renderer->fillsOutside(10, 20, 40, 30), 0u);
}

TEST_F(GamePictureTest, APageOverItsLimitOrWithNoMemoryIsNoPageAndLogged) {
  fakesd::addFile("/.games/demo/handoff.bmp", harness::bmpFile(40, 30, [](int, int) { return false; }));
  GamePicture picture;
  EXPECT_FALSE(picture.loadPage("demo", "handoff", 39, 30));
  EXPECT_TRUE(logHas("/.games/demo/handoff.bmp is 40x30, over 39x30"));
  EXPECT_FALSE(picture.loadPage("demo", "handoff", 40, 29));
  fakepsram::failNext = true;
  EXPECT_FALSE(picture.loadPage("demo", "handoff", 40, 30));
  EXPECT_TRUE(logHas("OOM: "));
  EXPECT_FALSE(picture.hasPage());
  ASSERT_TRUE(picture.loadPage("demo", "handoff", 40, 30));
  EXPECT_TRUE(picture.hasPage());
  fakelog::clearLines();
  EXPECT_FALSE(picture.loadPage("demo", "title", 480, 480)) << "no title.bmp";
  EXPECT_FALSE(picture.hasPage()) << "a load releases the page before it";
  EXPECT_FALSE(logHas("title.bmp")) << "a page the package does not ship is no error";
}

TEST_F(GamePictureTest, TheIconIsTheLaunchersChoice) {
  GameCore::Manifest manifest = parsed(manifestJson("demo", "Demo", "\"solo\"", 1, 1, ",\"icon\":\"dice-five\""));
  GamePicture picture;
  picture.loadIcon(manifest);
  EXPECT_EQ(picture.iconSource(), GameRowIcon::Source::Library);
  fakesd::addFile("/.games/demo/icon.bmp", harness::bmpFile(64, 64, [](int, int) { return true; }));
  picture.loadIcon(manifest);
  EXPECT_EQ(picture.iconSource(), GameRowIcon::Source::PackageBmp);
  renderer->clearScreen();
  picture.drawIcon(*renderer, 240, 400);
  EXPECT_EQ(renderer->pixelCount(GfxRenderer::PixelBlack), 0u) << "an all-white icon.bmp draws no ink";
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

// ---- one launcher row per game (entry 8): the launcher reads no save, and a game's row opens its title screen ----

// The cases ContinueLauncherTest held for the launcher's Continue rows (ticket 12 of epic-install-and-launcher), moved
// here when entry 8 took those rows away: a save is now found, offered, and resumed by the title screen a game's one
// row opens. The fixture adds to TitleScreenTest what the moved cases need: many games, a remove, and the launcher
// built again after a match is left, as goToGames() builds it.

std::string idOf(const int number) {
  char id[24];
  std::snprintf(id, sizeof(id), "game-%02d", number);
  return id;
}
std::string nameOf(const int number) {
  char name[24];
  std::snprintf(name, sizeof(name), "Game %02d", number);
  return name;
}

class OneRowPerGameTest : public TitleScreenTest {
 protected:
  void SetUp() override {
    TitleScreenTest::SetUp();
    removescript::reset();
    // The real remove takes the game's folder off the card.
    removescript::script().onRemove = [] { takeGameOffTheCard(removescript::script().ids.back()); };
  }
  void TearDown() override {
    TitleScreenTest::TearDown();
    removescript::reset();
  }

  // Solo games of the counting Lua, Game 01 to Game `count`.
  static void addGames(const int count) {
    for (int i = 1; i <= count; ++i) addCountingGame(idOf(i), nameOf(i), "\"solo\"", 1, 1);
  }
  static void takeGameOffTheCard(const std::string& id) {
    for (const char* file : {".pkg", "manifest.json", "main.lua"}) fakesd::removeEntry("/.games/" + id + "/" + file);
    fakesd::removeEntry("/.games/" + id);
  }
  // Every card operation on a path under /.games-data/ (an existence check, an open, a listing), where the saves are.
  static size_t gamesDataOps() {
    size_t total = 0;
    for (const std::string& op : fakesd::sim().ops)
      if (op.find(" /.games-data/") != std::string::npos) ++total;
    return total;
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
    render();
  }
  // The games of Game 01 to Game `total` whose names the screen drew, top to bottom.
  std::vector<std::string> shown(const int total) {
    std::vector<std::string> found;
    for (const screen::DrawnText& drawn : ui().drawn)
      for (int i = 1; i <= total; ++i)
        if (drawn.text == nameOf(i)) found.push_back(drawn.text);
    return found;
  }
  // The launcher's note, its wrapped lines read as one.
  std::string flatText() {
    std::string text = ui().joined();
    std::replace(text.begin(), text.end(), '\n', ' ');
    return text;
  }

  // Every screen goes as the manager lets it go: the match (`saves` says what becomes of the save its exit writes), the
  // title screen, and the launcher.
  void closeScreens(const match::Saves saves) {
    match::letStartedMatchesGo([this] { dropMatch(); }, saves);
    dropTitle();
    if (list) {
      activityManager.exitHolding(*list);
      activityManager.destroyHolding(list);
    }
    activityManager.reset();
    theme().reset();
    taps = 0;
  }
  // Leave, or the match ended: goToGames() builds a fresh launcher.
  void reopenLauncher(const match::Saves saves) {
    closeScreens(saves);
    openLauncher();
  }
};

// The launcher with saves on the card: one row for each game, no Continue line, and not one card operation under
// /.games-data/ when it opens, scrolls, or reloads after a remove (the title screen peeks a game's save when it opens).
TEST_F(OneRowPerGameTest, WithSavesOnTheCardTheLauncherIsOneRowPerGameAndReadsNoSave) {
  addCountingGame("bravo", "Bravo", "\"solo\"", 1, 1);
  addCountingGame("charlie", "Charlie", "\"solo\"", 1, 1);
  addCountingGame("alpha", "Alpha", "\"solo\"", 1, 1);
  save("alpha");
  save("charlie");
  const size_t before = gamesDataOps();
  openLauncher();
  const std::vector<std::string> names{"Alpha", "Bravo", "Charlie"};
  std::vector<std::string> drawn;
  for (const screen::DrawnText& line : ui().drawn)
    if (std::find(names.begin(), names.end(), line.text) != names.end()) drawn.push_back(line.text);
  EXPECT_EQ(drawn, names) << "one row a game: " << ui().joined();
  EXPECT_FALSE(ui().drewLine(tr(STR_GAMES_CONTINUE)));
  EXPECT_EQ(gamesDataOps(), before) << "entry";

  swipeUp();
  render();
  EXPECT_EQ(gamesDataOps(), before) << "a redraw (three games fit one page; TwentyFiveGames... scrolls)";

  const screen::DrawnText* bravo = nullptr;
  for (const screen::DrawnText& line : ui().drawn)
    if (line.text == "Bravo") bravo = &line;
  ASSERT_NE(bravo, nullptr) << ui().joined();
  input->longPress(bravo->rect.x + bravo->rect.width / 2, bravo->rect.y + bravo->rect.height / 2);
  frame();
  render();
  tapRow(tr(STR_GAMES_REMOVE));
  render();
  EXPECT_EQ(removescript::script().ids, std::vector<std::string>{"bravo"});
  EXPECT_TRUE(ui().drewLine("Alpha"));
  EXPECT_FALSE(ui().drewLine("Bravo"));
  EXPECT_TRUE(ui().drewLine("Charlie"));
  EXPECT_FALSE(ui().drewLine(tr(STR_GAMES_CONTINUE)));
  EXPECT_EQ(gamesDataOps(), before) << "the reload after a remove";
  EXPECT_TRUE(fakesd::has(resumePath("alpha")));
  EXPECT_TRUE(fakesd::has(resumePath("charlie")));

  // The control: the title screen a row opens does peek the save, and the count sees it.
  tapRow("Alpha");
  ASSERT_NO_FATAL_FAILURE(openPushedTitle());
  EXPECT_GT(gamesDataOps(), before);
}

TEST_F(OneRowPerGameTest, TwentyFiveGamesWithSavesPageAsOneRowPerGame) {
  addGames(25);
  for (const int n : {3, 11, 24}) save(idOf(n));
  openLauncher();
  const size_t page = shown(25).size();
  ASSERT_GE(page, 2u);
  ASSERT_LT(page, 25u) << "the list must not fit one screen";
  std::vector<std::string> seen;
  const size_t pages = (25 + page - 1) / page;
  for (size_t p = 0; p < pages; ++p) {
    if (p > 0) swipeUp();
    const std::vector<std::string> rows = shown(25);
    EXPECT_EQ(rows.size(), std::min(page, 25 - p * page)) << "page " << p + 1 << " holds what is left, once";
    EXPECT_FALSE(ui().drewLine(tr(STR_GAMES_CONTINUE))) << "page " << p + 1;
    seen.insert(seen.end(), rows.begin(), rows.end());
  }
  std::vector<std::string> all;
  for (int i = 1; i <= 25; ++i) all.push_back(nameOf(i));
  EXPECT_EQ(seen, all);
  EXPECT_EQ(gamesDataOps(), 0u);
}

// #9 of ContinueLauncherTest: a Confirm on the game's row opens its title screen, and a Confirm there resumes.
TEST_F(OneRowPerGameTest, ConfirmOnTheGamesRowThenConfirmResumesItsSave) {
  addGames(2);
  save("game-02");
  openLauncher();
  key(Button::NavNext);  // Game 02's row
  key(Button::Confirm);
  EXPECT_EQ(activityManager.asks.pushed, 1);
  EXPECT_EQ(activityManager.asks.replaced, 0);
  ASSERT_NO_FATAL_FAILURE(openPushedTitle());
  EXPECT_TRUE(theme().drew("drawHeader", "Game 02"));
  key(Button::Confirm);  // Continue: the first row, where the selection starts
  ASSERT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started game-02"));
  ASSERT_TRUE(pumpMatchTo("Resuming at ver 3"));
  EXPECT_FALSE(logHas("setup ran"));
}

// #3: a save written for the package as it was before the game was reinstalled is no save of the game installed now.
TEST_F(OneRowPerGameTest, ASaveOfAChangedPackageOffersNoContinueAndIsLeftOnTheCard) {
  addGames(2);
  save("game-01");
  Bytes stale = resumeBytes(2, 3);
  stale[6 + GamePkg::HASH_BYTES - 1] ^= 0x03;  // the package hash follows the 6-byte header
  fakesd::addFile(resumePath("game-02"), stale);
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Game 02"));
  const std::vector<std::string> newOnly{tr(STR_GAMES_NEW_GAME)};
  EXPECT_EQ(rowsDrawn(), newOnly);
  EXPECT_EQ(fakesd::bytesOf(resumePath("game-02")), stale) << "the screens only look";

  // The installer replaces Game 01 with a build whose package hash differs: its title screen has no Continue either.
  fakesd::addFile("/.games/game-01/.pkg", std::string("v1\n0530a15766e91bf2\n"));
  closeScreens(match::Saves::Discard);  // no match ran
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Game 01"));
  EXPECT_EQ(rowsDrawn(), newOnly);
  EXPECT_EQ(fakesd::bytesOf(resumePath("game-01")), resumeBytes(2, 3));
}

// #5: a file that is not a resume (text, or cut short) offers no Continue; a valid one beside it does.
TEST_F(OneRowPerGameTest, ASaveThatIsNotAValidResumeOffersNoContinue) {
  addGames(3);
  fakesd::addFile(resumePath("game-01"), std::string("resume of game-01"));
  Bytes truncated = resumeBytes(2, 3);
  truncated.resize(12);
  fakesd::addFile(resumePath("game-02"), truncated);
  save("game-03");
  const std::vector<std::string> newOnly{tr(STR_GAMES_NEW_GAME)};
  const std::vector<std::string> withSave{tr(STR_GAMES_CONTINUE), tr(STR_GAMES_NEW_GAME)};
  const std::vector<std::pair<std::string, std::vector<std::string>>> cases{
      {"Game 01", newOnly}, {"Game 02", newOnly}, {"Game 03", withSave}};
  for (const auto& [name, expected] : cases) {
    SCOPED_TRACE(name);
    closeScreens(match::Saves::Discard);
    ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher(name));
    EXPECT_EQ(rowsDrawn(), expected);
  }
}

// #7: resume.bin.tmp alone (the rename to resume.bin was cut off) is a save the title screen offers.
TEST_F(OneRowPerGameTest, ASaveWhoseRenameWasInterruptedStillOffersContinue) {
  addGames(1);
  fakesd::addFile(resumePath("game-01") + ".tmp", resumeBytes(2, 3));
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Game 01"));
  const std::vector<std::string> expected{tr(STR_GAMES_CONTINUE), tr(STR_GAMES_NEW_GAME)};
  EXPECT_EQ(rowsDrawn(), expected);
}

// #11: the save was valid when the title screen opened and is not when Continue is tapped: the match finds no usable
// resume and starts a new one (a save that will not read is the error view: ASoloGamesUnreadableSave...).
TEST_F(OneRowPerGameTest, ASaveThatWentBadBetweenTheScreenAndTheTapStartsANewMatch) {
  addGames(1);
  save("game-01");
  ASSERT_NO_FATAL_FAILURE(openTitleThroughLauncher("Game 01"));
  const std::vector<std::string> expected{tr(STR_GAMES_CONTINUE), tr(STR_GAMES_NEW_GAME)};
  ASSERT_EQ(rowsDrawn(), expected);
  fakesd::addFile(resumePath("game-01"), std::string("garbage"));
  tapRow(tr(STR_GAMES_CONTINUE));
  ASSERT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("no usable resume.bin; starting a new match"));
  ASSERT_TRUE(pumpMatchTo("setup ran"));
}

// #18: after a match resumed from Continue is left, the next launcher shows the whole page holding the game's row and
// selects it, so a Confirm opens its title screen and another resumes.
TEST_F(OneRowPerGameTest, LeavingAMatchStartedFromContinueSelectsTheGamesRowOnThePageHoldingIt) {
  addGames(25);
  // Saves of the games above Game 20 too: they once put Continue rows ahead of its row, and now shift no row.
  for (int i = 1; i <= 10; ++i) save(idOf(i));
  save("game-20");
  openLauncher();
  const size_t page = shown(25).size();
  ASSERT_GT(page, 2u);
  ASSERT_LT(page, 20u) << "Game 20 is not on the first page";
  key(Button::NavNext, 19);  // Game 20's row
  key(Button::Confirm);
  ASSERT_NO_FATAL_FAILURE(openPushedTitle());
  key(Button::Confirm);  // Continue
  ASSERT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);
  ASSERT_TRUE(pumpMatchTo("Resuming at ver 3"));

  reopenLauncher(match::Saves::Keep);  // Leave: Game 20's save is on the card, as it was
  fakelog::clearLines();
  const size_t first = 19 / page * page;  // the first row of the page holding row 19, Game 20's
  ASSERT_GE(first, 1u);
  const std::vector<std::string> rows = shown(25);
  ASSERT_FALSE(rows.empty());
  EXPECT_EQ(rows.front(), nameOf(static_cast<int>(first) + 1)) << "the whole page holding the row, from its first row";
  EXPECT_EQ(rows.size(), std::min(page, 25 - first));
  EXPECT_FALSE(ui().drewLine(tr(STR_GAMES_CONTINUE)));
  key(Button::Confirm);  // the selection is on Game 20's row
  ASSERT_NO_FATAL_FAILURE(openPushedTitle());
  EXPECT_TRUE(theme().drew("drawHeader", "Game 20"));
  key(Button::Confirm);
  ASSERT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started game-20"));
  ASSERT_TRUE(pumpMatchTo("Resuming at ver 3"));
}

// #19 (A12): after a Continue match is left, two Confirms resume it again, and none starts a New match over the save.
TEST_F(OneRowPerGameTest, TwoConfirmsAfterLeavingAContinueMatchResumeAgainAndLeaveTheSaveAlone) {
  addGames(3);
  save("game-01");
  save("game-03");
  openLauncher();
  key(Button::NavNext, 2);  // Game 03
  key(Button::Confirm);
  ASSERT_NO_FATAL_FAILURE(openPushedTitle());
  key(Button::Confirm);  // Continue
  ASSERT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);

  reopenLauncher(match::Saves::Keep);  // Leave: Game 03's save stays, rewritten by the forced exit
  fakelog::clearLines();
  key(Button::Confirm);  // the first press, with nothing chosen since: Game 03's row
  ASSERT_NO_FATAL_FAILURE(openPushedTitle());
  EXPECT_TRUE(theme().drew("drawHeader", "Game 03"));
  key(Button::Confirm);  // the second: Continue
  ASSERT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started game-03"));
  ASSERT_TRUE(pumpMatchTo("Round started at ver 3"));
  EXPECT_TRUE(logHas("Resuming at ver 3"));
  EXPECT_FALSE(logHas("setup ran")) << "not a New match";
  for (int i = 0; i < 20; ++i) {
    entered->loop();
    input->clear();
  }
  EXPECT_EQ(fakesd::bytesOf(resumePath("game-03")), resumeBytes(2, 3)) << "the save is as it was";
}

// #21, the everyday path: a New match, one move, Leave. The forced exit writes resume.bin, the next launcher selects
// the game's row, and its Confirm and the title screen's Confirm on Continue resume the saved move.
TEST_F(OneRowPerGameTest, ANewMatchLeftAfterAMoveIsResumedByConfirmOnItsRowAndConfirmOnContinue) {
  addGames(3);
  openLauncher();        // no saves
  key(Button::NavNext);  // Game 02
  key(Button::Confirm);
  ASSERT_NO_FATAL_FAILURE(openPushedTitle());
  ASSERT_EQ(rowsDrawn(), std::vector<std::string>{tr(STR_GAMES_NEW_GAME)});
  key(Button::Confirm);  // New game: with no save, a New match at once
  ASSERT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);
  ASSERT_TRUE(pumpMatchTo("setup ran"));
  // The match drops a tap until its first frame is on the panel.
  ASSERT_TRUE(match::waitFor([&] {
    entered->loop();
    input->clear();
    return activityManager.updateRequested();
  }));
  entered->render(RenderLock(*entered));
  activityManager.markRendered();
  input->tap(3 + 50, 6 + 50);  // the canvas sits at (3, 6)
  ASSERT_TRUE(pumpMatchTo("draw\t1")) << "the tap was applied and drawn";

  reopenLauncher(match::Saves::Keep);  // Leave: the forced exit writes the round in progress, and it stays
  EXPECT_EQ(fakesd::bytesOf(resumePath("game-02")), resumeBytes(1, 2)) << "the round's last snapshot: one tap, ver 2";
  const std::vector<std::string> games{"Game 01", "Game 02", "Game 03"};
  EXPECT_EQ(shown(3), games) << "still one row a game";
  EXPECT_FALSE(ui().drewLine(tr(STR_GAMES_CONTINUE)));
  fakelog::clearLines();
  key(Button::Confirm);  // Game 02's row, selected
  ASSERT_NO_FATAL_FAILURE(openPushedTitle());
  EXPECT_TRUE(theme().drew("drawHeader", "Game 02"));
  const std::vector<std::string> withSave{tr(STR_GAMES_CONTINUE), tr(STR_GAMES_NEW_GAME)};
  EXPECT_EQ(rowsDrawn(), withSave);
  key(Button::Confirm);  // Continue
  ASSERT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("Started game-02"));
  ASSERT_TRUE(pumpMatchTo("Round started at ver 2"));
  EXPECT_TRUE(logHas("Resuming at ver 2"));
  EXPECT_TRUE(logHas("draw\t1")) << "the saved move is what is drawn";
  EXPECT_FALSE(logHas("setup ran")) << "not a New match";
}

// rev-11: under the install note, the first Confirm dismisses it and the second opens the selected game's title screen,
// whose Continue is first; nothing starts a match until a choice is made there.
TEST_F(OneRowPerGameTest, UnderTheInstallNoteConfirmsOpenTheTitleScreenAndNeverANewMatch) {
  addGames(2);
  save("game-01");
  installerscript::script().report.failed = 1;
  installerscript::script().report.firstError = GamePackageInstaller::Error::NotAPackage;
  openLauncher();
  ASSERT_NE(flatText().find(tr(STR_GAMES_INSTALL_NOT_A_PACKAGE)), std::string::npos) << ui().joined();
  key(Button::Confirm);  // dismisses the note
  EXPECT_EQ(activityManager.asks.pushed, 0);
  EXPECT_EQ(activityManager.asks.replaced, 0);
  render();
  EXPECT_EQ(flatText().find(tr(STR_GAMES_INSTALL_NOT_A_PACKAGE)), std::string::npos);
  key(Button::Confirm);  // Game 01's row
  EXPECT_EQ(activityManager.asks.pushed, 1);
  EXPECT_EQ(activityManager.asks.replaced, 0) << "the launcher starts no match";
  ASSERT_NO_FATAL_FAILURE(openPushedTitle());
  EXPECT_TRUE(theme().drew("drawHeader", "Game 01"));
  const std::vector<std::string> withSave{tr(STR_GAMES_CONTINUE), tr(STR_GAMES_NEW_GAME)};
  EXPECT_EQ(rowsDrawn(), withSave);
  EXPECT_EQ(activityManager.asks.replaced, 0) << "no match until a choice on the title screen";
  key(Button::Confirm);  // that choice: Continue
  ASSERT_EQ(activityManager.asks.replaced, 1);
  ASSERT_NE(enterReplacement(), nullptr);
  EXPECT_TRUE(logHas("game-01: resuming the save's roster: solo, 1 seat(s)"));
  ASSERT_TRUE(pumpMatchTo("Resuming at ver 3"));
}

}  // namespace
