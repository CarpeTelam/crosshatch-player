// The round file's reader (C1): a returned table becomes a Round, and every break of a rule is an error that names the
// key.

#include <Codec.h>
#include <Manifest.h>
#include <Memory.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <lua.hpp>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "RoundFile.h"
#include "ScriptVm.h"
#include "TestSupport.h"

namespace {

using games_check::GameFacts;
using games_check::loadRound;
using games_check::ModuleText;
using games_check::OwnedVm;
using games_check::resolveSteps;
using games_check::Round;

std::string fixtureText(const std::string& relative) {
  std::ifstream in(std::string(GAME_SCRIPT_FIXTURES_DIR) + "/" + relative, std::ios::binary);
  EXPECT_TRUE(in.good()) << relative;
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

// A manifest.json of the fixtures, parsed as the installer's registry parses it, with the settings it declares.
struct Game {
  explicit Game(const std::string& fixture) : reader(makeUniqueNoThrow<GameCore::ManifestReader>()) {
    EXPECT_TRUE(reader);
    if (!reader) return;
    const std::string json = fixtureText(fixture + "/manifest.json");
    reader->begin();
    reader->feed(json.data(), json.size());
    EXPECT_EQ(reader->finish(manifest), GameCore::ManifestError::None);
    facts.manifest = &manifest;
    facts.settings = &reader->settings();
    facts.hostMaxSeats = 2;
    // What gameHostCaps() lets a game start: solo and pass, not nearby.
    facts.hostModes =
        static_cast<uint8_t>(manifest.modes & (GameCore::Manifest::MODE_SOLO | GameCore::Manifest::MODE_PASS));
  }
  GameCore::Manifest manifest;
  std::unique_ptr<GameCore::ManifestReader> reader;
  GameFacts facts;
};

class RoundFileTest : public ::testing::Test {
 protected:
  // The round `text` evaluates to, against `game`'s manifest; the error when it is none.
  bool load(const std::string& text, Round& round, const Game& game, std::string& error,
            const std::vector<ModuleText>& modules = {}) {
    auto vm = OwnedVm::create(GameScript::GameSources{}, modules, GameCore::NO_IMAGES, 1, error,
                              games_check::CANVAS_474, games_check::VmLimits::check());
    EXPECT_TRUE(vm) << error;
    return vm && loadRound(std::move(vm), "sample", text, game.facts, round, error);
  }

  // Loads `text` expecting a failure whose message holds `key`.
  void expectError(const std::string& text, const std::string& key, const Game& game) {
    Round round;
    std::string error;
    EXPECT_FALSE(load(text, round, game, error)) << text;
    EXPECT_NE(error.find(key), std::string::npos)
        << "the error should name '" << key << "': " << error << "\nfor: " << text;
    EXPECT_NE(error.find("rounds/sample.lua"), std::string::npos) << error;
  }

  Game passArt{"pass-art"};  // solo and pass, hidden, settings level and board
  Game tracer{"tracer"};     // solo only
};

TEST_F(RoundFileTest, AListOfStepsAndItsWinnersAreRead) {
  Round round;
  std::string error;
  ASSERT_TRUE(load(R"lua(
return { mode = "pass", settings = { level = "Easy" }, seed = 9,
  steps = { { seat = 1, x = 120, y = 300 },
            { seat = 2, x = 10, y = 20, wait = 250, move = false, shows = "That square is taken" } },
  winners = { 2 } }
)lua",
                   round, passArt, error))
      << error;
  EXPECT_EQ(round.name, "sample");
  EXPECT_EQ(round.mode, GameCore::Mode::Pass);
  EXPECT_EQ(round.seed, 9u);
  ASSERT_EQ(round.settings.size(), 1u);
  EXPECT_EQ(round.settings[0].first, "level");
  EXPECT_EQ(round.settings[0].second, "Easy");
  EXPECT_FALSE(round.stepsFromFunction);
  EXPECT_FALSE(round.unfinished);
  EXPECT_EQ(round.winners, 1u << 1);
  ASSERT_EQ(round.steps.size(), 2u);
  EXPECT_EQ(round.steps[0].seat, 1);
  EXPECT_EQ(round.steps[0].x, 120);
  EXPECT_EQ(round.steps[0].y, 300);
  EXPECT_TRUE(round.steps[0].moves);
  EXPECT_EQ(round.steps[0].waitMs, 0u);
  EXPECT_FALSE(round.steps[0].hasShows);
  EXPECT_EQ(round.steps[1].waitMs, 250u);
  EXPECT_FALSE(round.steps[1].moves);
  EXPECT_TRUE(round.steps[1].hasShows);
  EXPECT_EQ(round.steps[1].shows, "That square is taken");
}

TEST_F(RoundFileTest, ADrawIsEmptyWinnersAndUnfinishedIsItsOwnEnding) {
  Round draw;
  Round open;
  std::string error;
  ASSERT_TRUE(
      load("return { mode = 'solo', steps = { { seat = 1, x = 1, y = 2 } }, winners = {} }", draw, passArt, error))
      << error;
  EXPECT_FALSE(draw.unfinished);
  EXPECT_EQ(draw.winners, 0u);
  EXPECT_EQ(draw.seed, 1u) << "no seed: 1";
  ASSERT_TRUE(
      load("return { mode = 'solo', steps = { { seat = 1, x = 1, y = 2 } }, unfinished = true }", open, passArt, error))
      << error;
  EXPECT_TRUE(open.unfinished);
}

TEST_F(RoundFileTest, TheFileRunsInTheSandboxOnTheDeviceCanvasWithTheCompanionsModules) {
  Round round;
  std::string error;
  // ch.screen is a device canvas (here the default, the Sticky's 474 x 788; the games check plays the X4 Pro's 466 x
  // 788 too), and require finds a companion module.
  ASSERT_TRUE(load(R"lua(
local helpers = require("helpers")
assert(ch.screen.w == 474 and ch.screen.h == 788, "canvas")
return { mode = "solo", steps = { { seat = 1, x = helpers.centre(ch.screen.w), y = 300 } }, unfinished = true }
)lua",
                   round, passArt, error, {ModuleText{"helpers", "return { centre = function(w) return w // 2 end }"}}))
      << error;
  EXPECT_EQ(round.steps[0].x, 237);
}

TEST_F(RoundFileTest, AStepsFunctionIsKeptAndCalledOnceWithTheDecodedState) {
  Round round;
  std::string error;
  ASSERT_TRUE(load(R"lua(
return { mode = "solo", unfinished = true,
  steps = function(state)
    assert(#state.cells == 3, "the cells")
    return { { seat = 1, x = state.cells[2] + 40, y = 5 }, { seat = 1, x = 7, y = 8 } }
  end }
)lua",
                   round, passArt, error))
      << error;
  EXPECT_TRUE(round.stepsFromFunction);
  EXPECT_TRUE(round.steps.empty()) << "the function runs after the round begins";
  const std::vector<uint8_t> state = games_check::test::encodeState("{ cells = { 0, 2, 0 } }");
  ASSERT_FALSE(state.empty());
  ASSERT_TRUE(resolveSteps(round, state, passArt.facts, error)) << error;
  ASSERT_EQ(round.steps.size(), 2u);
  EXPECT_EQ(round.steps[0].x, 42);
}

TEST_F(RoundFileTest, AStepsFunctionThatRaisesFaultsOrReturnsNoListFails) {
  const std::vector<uint8_t> state = games_check::test::encodeState("{ cells = {} }");
  struct Case {
    const char* body;
    const char* expected;
  };
  for (const Case& test :
       {Case{"error('no steps today')", "no steps today"}, Case{"while true do end", "instruction budget exceeded"},
        Case{"return 5", "must be a list of steps"}, Case{"return {}", "is empty"},
        Case{"return { { seat = 1, x = 1 } }", "steps(state)'s result[1].y is missing"},
        Case{"return { { seat = 3, x = 1, y = 2 } }", "result[1].seat must be a seat in 1..2"}}) {
    Round round;
    std::string error;
    ASSERT_TRUE(
        load(std::string("return { mode = 'pass', unfinished = true, steps = function(state) ") + test.body + " end }",
             round, passArt, error))
        << error;
    EXPECT_FALSE(resolveSteps(round, state, passArt.facts, error)) << test.body;
    EXPECT_NE(error.find(test.expected), std::string::npos) << test.body << " -> " << error;
    EXPECT_NE(error.find("steps(state)"), std::string::npos) << error;
  }
}

TEST_F(RoundFileTest, AFileThatRaisesFaultsOrReturnsNoTableFails) {
  expectError("error('the file broke')", "the file broke", passArt);
  expectError("while true do end", "instruction budget exceeded", passArt);
  expectError("return 5", "must return a table", passArt);
  expectError("return", "must return a table", passArt);
  expectError("this is not lua", "rounds/sample.lua", passArt);
}

TEST_F(RoundFileTest, EveryBrokenRuleNamesItsKey) {
  const std::string ok = "steps = { { seat = 1, x = 1, y = 2 } }, winners = { 1 }";
  // An unknown key.
  expectError("return { mode = 'solo', " + ok + ", colour = 'red' }", "'colour'", passArt);
  expectError("return { mode = 'solo', " + ok + ", [1] = 5 }", "key that is a number", passArt);
  // Wrong types and missing keys.
  expectError("return { mode = 5, " + ok + " }", "mode must be", passArt);
  expectError("return { " + ok + " }", "mode is missing", passArt);
  expectError("return { mode = 'solo', winners = { 1 } }", "steps is missing", passArt);
  expectError("return { mode = 'solo', steps = 'tap', winners = { 1 } }", "steps must be a list", passArt);
  expectError("return { mode = 'solo', steps = {}, winners = { 1 } }", "steps is empty", passArt);
  expectError("return { mode = 'solo', steps = { 1, 2 }, winners = { 1 } }", "steps[1] must be a table", passArt);
  expectError("return { mode = 'solo', steps = { { seat = 1, x = 1, y = 2 }, [4] = {} }, winners = { 1 } }", "steps",
              passArt);
  // Both or neither of winners and unfinished.
  expectError("return { mode = 'solo', steps = { { seat = 1, x = 1, y = 2 } }, winners = { 1 }, unfinished = true }",
              "both set", passArt);
  expectError("return { mode = 'solo', steps = { { seat = 1, x = 1, y = 2 } } }", "neither winners nor unfinished",
              passArt);
  expectError("return { mode = 'solo', steps = { { seat = 1, x = 1, y = 2 } }, unfinished = false }",
              "neither winners nor unfinished", passArt);
  expectError("return { mode = 'solo', steps = { { seat = 1, x = 1, y = 2 } }, unfinished = 'yes' }",
              "unfinished must be", passArt);
  expectError("return { mode = 'solo', steps = { { seat = 1, x = 1, y = 2 } }, winners = 1 }", "winners must be a list",
              passArt);
  expectError("return { mode = 'solo', steps = { { seat = 1, x = 1, y = 2 } }, winners = { 2 } }", "winners holds 2",
              passArt);
  expectError("return { mode = 'solo', steps = { { seat = 1, x = 1, y = 2 } }, winners = { 'a' } }",
              "winners must hold", passArt);
  // winners is a list of distinct seats: positions 1..n, each in range, none twice.
  expectError("return { mode = 'pass', steps = { { seat = 1, x = 1, y = 2 } }, winners = { a = 1 } }",
              "winners must be a list of seats", passArt);
  expectError("return { mode = 'pass', steps = { { seat = 1, x = 1, y = 2 } }, winners = { [2] = 1 } }",
              "winners must be a list of seats", passArt);
  expectError("return { mode = 'pass', steps = { { seat = 1, x = 1, y = 2 } }, winners = { 1, [3] = 2 } }",
              "winners must be a list of seats", passArt);
  expectError("return { mode = 'pass', steps = { { seat = 1, x = 1, y = 2 } }, winners = { 1, 1 } }",
              "winners names seat 1 twice", passArt);
  expectError("return { mode = 'pass', steps = { { seat = 1, x = 1, y = 2 } }, winners = { 0 } }", "winners holds 0",
              passArt);
  expectError("return { mode = 'pass', steps = { { seat = 1, x = 1, y = 2 } }, winners = { 1, 3 } }", "winners holds 3",
              passArt);
  // The seed is an integer in 0..4294967295.
  expectError("return { mode = 'solo', seed = 1.5, " + ok + " }", "seed must be an integer", passArt);
  expectError("return { mode = 'solo', seed = '1', " + ok + " }", "seed must be an integer", passArt);
  expectError("return { mode = 'solo', seed = -1, " + ok + " }", "seed must be an integer in 0..4294967295", passArt);
  expectError("return { mode = 'solo', seed = 4294967296, " + ok + " }", "seed must be an integer in 0..4294967295",
              passArt);
  // The step keys.
  expectError("return { mode = 'solo', steps = { { x = 1, y = 2 } }, winners = { 1 } }", "steps[1].seat is missing",
              passArt);
  expectError("return { mode = 'solo', steps = { { seat = 1, y = 2 } }, winners = { 1 } }", "steps[1].x is missing",
              passArt);
  expectError("return { mode = 'solo', steps = { { seat = 1, x = 1 } }, winners = { 1 } }", "steps[1].y is missing",
              passArt);
  expectError("return { mode = 'solo', steps = { { seat = 'a', x = 1, y = 2 } }, winners = { 1 } }",
              "steps[1].seat must be an integer", passArt);
  expectError("return { mode = 'solo', steps = { { seat = 2, x = 1, y = 2 } }, winners = { 1 } }",
              "steps[1].seat must be a seat in 1..1", passArt);
  expectError("return { mode = 'pass', steps = { { seat = 3, x = 1, y = 2 } }, winners = { 1 } }",
              "steps[1].seat must be a seat in 1..2", passArt);
  expectError("return { mode = 'solo', steps = { { seat = 1, x = 1.5, y = 2 } }, winners = { 1 } }",
              "steps[1].x must be an integer", passArt);
  expectError("return { mode = 'solo', steps = { { seat = 1, x = 40000, y = 2 } }, winners = { 1 } }",
              "steps[1].x must be a canvas", passArt);
  expectError("return { mode = 'solo', steps = { { seat = 1, x = 1, y = 2, wait = -5 } }, winners = { 1 } }",
              "steps[1].wait", passArt);
  expectError("return { mode = 'solo', steps = { { seat = 1, x = 1, y = 2, move = 'no' } }, winners = { 1 } }",
              "steps[1].move", passArt);
  expectError("return { mode = 'solo', steps = { { seat = 1, x = 1, y = 2, shows = 5 } }, winners = { 1 } }",
              "steps[1].shows", passArt);
  expectError("return { mode = 'solo', steps = { { seat = 1, x = 1, y = 2, shows = '' } }, winners = { 1 } }",
              "steps[1].shows", passArt);
  expectError("return { mode = 'solo', steps = { { seat = 1, x = 1, y = 2, tap = 1 } }, winners = { 1 } }",
              "steps[1].tap", passArt);
}

TEST_F(RoundFileTest, TheModeIsSoloOrPassAndOneTheManifestDeclares) {
  const std::string rest = ", steps = { { seat = 1, x = 1, y = 2 } }, winners = { 1 }";
  expectError("return { mode = 'nearby'" + rest + " }", "'nearby' is not solo or pass", passArt);
  expectError("return { mode = 'sideways'" + rest + " }", "'sideways' is not solo or pass", passArt);
  // The tracer is a solo-only game.
  expectError("return { mode = 'pass'" + rest + " }", "mode 'pass' is not one the manifest declares", tracer);
  // A mode the manifest declares but this host cannot start.
  Game noPass("pass-art");
  noPass.facts.hostModes = GameCore::Manifest::MODE_SOLO;
  expectError("return { mode = 'pass'" + rest + " }", "declared but this host cannot start it", noPass);
  Round round;
  std::string error;
  EXPECT_TRUE(load("return { mode = 'solo'" + rest + " }", round, tracer, error)) << error;
}

TEST_F(RoundFileTest, ASettingIdOrValueTheManifestLacksIsAnError) {
  const std::string rest = ", steps = { { seat = 1, x = 1, y = 2 } }, winners = { 1 }";
  expectError("return { mode = 'solo', settings = { speed = 'Fast' }" + rest + " }", "settings.speed", passArt);
  expectError("return { mode = 'solo', settings = { level = 'Impossible' }" + rest + " }", "settings.level", passArt);
  expectError("return { mode = 'solo', settings = { level = 3 }" + rest + " }", "settings.level must be a string",
              passArt);
  expectError("return { mode = 'solo', settings = 'Easy'" + rest + " }", "settings must be a table", passArt);
  // The tracer declares none.
  expectError("return { mode = 'solo', settings = { level = 'Easy' }" + rest + " }", "settings.level", tracer);
  Round round;
  std::string error;
  EXPECT_TRUE(load("return { mode = 'solo', settings = { level = 'Easy', board = 'Large' }" + rest + " }", round,
                   passArt, error))
      << error;
  EXPECT_EQ(round.settings.size(), 2u);
}

TEST_F(RoundFileTest, SeatsFollowTheModeAndTheHost) {
  EXPECT_EQ(passArt.facts.seatsOf(GameCore::Mode::Solo), 1);
  EXPECT_EQ(passArt.facts.seatsOf(GameCore::Mode::Pass), 2);
  EXPECT_EQ(tracer.facts.seatsOf(GameCore::Mode::Pass), 0) << "a one-seat game has no pass match";
  EXPECT_EQ(games_check::winnersText(0), "{}");
  EXPECT_EQ(games_check::winnersText(0b101), "{1,3}");
}

}  // namespace
