// The games check's own tests: its negative and positive cases over scratch trees the test writes into the build folder
// (GAMES_CHECK_SCRATCH_DIR), copies and edits of the fixture games (test/game_script/fixtures: pass-open, pass-hidden,
// pass-art) and a few games of the tests' own, each packed by the real packer through pack_games.py. They never write
// to games/ or to first_party/, and need no nested build: the same functions GamesCheckTest runs over games/ run here
// over the scratch roots, so a check that always passed would fail here.

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <vector>

#include "GameCheck.h"
#include "HalDisplay.h"
#include "ScriptVm.h"
#include "TestSupport.h"

// The converter reads the panel the installer's stubs describe.
HalDisplay display;

namespace fs = std::filesystem;

namespace {

using games_check::PlayOptions;
using games_check::Report;
using games_check::Roots;
using games_check::RoundDetails;

// pass-open: seat 1 (X) takes the top row, cells 1, 2, 3, against seat 2's cells 4 and 5 (a 140 px cell from x 27, y
// 200).
constexpr const char* X_WINS_IN_PASS = R"lua(
return { mode = "pass", seed = 1,
  steps = {
    { seat = 1, x = 97, y = 270 }, { seat = 2, x = 97, y = 410 }, { seat = 1, x = 237, y = 270 },
    { seat = 2, x = 237, y = 410 }, { seat = 1, x = 377, y = 270 } },
  winners = { 1 } }
)lua";

// The same moves alone: solo places X and O in turn from seat 1.
constexpr const char* X_WINS_IN_SOLO = R"lua(
return { mode = "solo",
  steps = {
    { seat = 1, x = 97, y = 270 }, { seat = 1, x = 97, y = 410 }, { seat = 1, x = 237, y = 270 },
    { seat = 1, x = 237, y = 410 }, { seat = 1, x = 377, y = 270 } },
  winners = { 1 } }
)lua";

// One tap that moves, then an unfinished round: the shortest round of the scratch games below.
constexpr const char* ONE_TAP = R"lua(
return { mode = "solo", steps = { { seat = 1, x = 100, y = 300 } }, unfinished = true }
)lua";

// A solo game of the tests' own whose state is one string of `pad` bytes, so its snapshot has a size the test sets.
std::string padGame(const size_t pad) {
  return "local PAD = " + std::to_string(pad) + R"lua(
local game = {}
function game.setup(ctx) return { pad = string.rep("x", PAD) } end
function game.status(state) return { turn = 1 } end
function game.apply(state, seat, move) return state end
function game.input(state, seat, ui, ev) if ev.kind == "tap" then return { tap = true } end end
function game.draw(state, seat, ui) ch.gfx.clear("white") end
return game
)lua";
}

// A solo game of the tests' own that draws one math.random value its setup took, and logs it.
constexpr const char* DRAW_GAME = R"lua(
local game = {}
function game.setup(ctx) return { r = math.random(1000000000) } end
function game.status(state) return { turn = 1 } end
function game.apply(state, seat, move) return state end
function game.input(state, seat, ui, ev) if ev.kind == "tap" then return { tap = true } end end
function game.draw(state, seat, ui)
  ch.log("r " .. state.r)
  ch.gfx.clear("white")
end
return game
)lua";

// A solo game of the tests' own whose main.lua is `top` (statements, run when main loads) and then a game table that
// draws a blank frame; `setupBody` is the body of its setup, for a require made later than the load.
std::string loadingGame(const std::string& top, const std::string& setupBody = "") {
  return top + R"lua(
local game = {}
function game.setup(ctx) )lua" +
         setupBody + R"lua( return { n = 0 } end
function game.status(state) return { turn = 1 } end
function game.apply(state, seat, move) return state end
function game.input(state, seat, ui, ev) if ev.kind == "tap" then return { tap = true } end end
function game.draw(state, seat, ui) ch.gfx.clear("white") end
return game
)lua";
}

std::string soloManifest(const std::string& id, const std::string& modes = R"(["solo"])",
                         const std::string& seats = R"({"min": 1, "max": 1})") {
  return "{\"id\": \"" + id + "\", \"name\": \"" + id + "\", \"version\": \"1.0.0\", \"api\": 1, \"seats\": " + seats +
         ", \"modes\": " + modes + "}\n";
}

class GamesCheckEngineTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    root = fs::path(GAMES_CHECK_SCRATCH_DIR) / (std::string(info->test_suite_name()) + "_" + info->name());
    fs::remove_all(root);
    fs::create_directories(games());
    fs::create_directories(companion());
    fs::create_directories(packed());
  }

  fs::path games() const { return root / "games"; }
  fs::path companion() const { return root / "companion"; }
  fs::path packed() const { return root / "packed"; }
  Roots roots() const { return Roots{games().string(), companion().string(), packed().string()}; }

  static void write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
  }

  // A fixture game as games/<id>/, its manifest's id and name made <id>.
  void copyGame(const std::string& fixture, const std::string& id) {
    fs::copy(fs::path(GAMES_CHECK_FIXTURES_DIR) / fixture, games() / id, fs::copy_options::recursive);
    if (id == fixture) return;
    std::string manifest = games_check::test::readTextFile((games() / id / "manifest.json").string());
    const size_t at = manifest.find("\"id\": \"" + fixture + "\"");
    ASSERT_NE(at, std::string::npos);
    manifest.replace(at, std::string("\"id\": \"" + fixture + "\"").size(), "\"id\": \"" + id + "\"");
    write(games() / id / "manifest.json", manifest);
  }

  void writeGame(const std::string& id, const std::string& manifest, const std::string& main) {
    write(games() / id / "manifest.json", manifest);
    write(games() / id / "main.lua", main);
  }

  // Replaces the first `from` in games/<id>/<file> with `to`; the edit must find it.
  void edit(const std::string& id, const std::string& file, const std::string& from, const std::string& to) {
    const fs::path path = games() / id / file;
    std::string text = games_check::test::readTextFile(path.string());
    const size_t at = text.find(from);
    ASSERT_NE(at, std::string::npos) << file << " has no '" << from << "'";
    text.replace(at, from.size(), to);
    write(path, text);
  }

  void round(const std::string& id, const std::string& name, const std::string& text) {
    write(companion() / id / "rounds" / (name + ".lua"), text);
  }
  void module(const std::string& id, const std::string& name, const std::string& text) {
    write(companion() / id / (name + ".lua"), text);
  }
  void checks(const std::string& id, const std::string& text) { module(id, "checks", text); }

  // pack_games.py over the games root, the way the build runs it.
  void pack(const std::vector<std::string>& ids) {
    std::string command = std::string("'") + PYTHON_EXECUTABLE + "' '" + PACK_GAMES_PY + "' '" + PACK_GAME_PY + "' '" +
                          games().string() + "' '" + packed().string() + "'";
    for (const std::string& id : ids) command += " '" + id + "'";
    command += " > '" + (root / "pack.log").string() + "' 2>&1";
    ASSERT_EQ(std::system(command.c_str()), 0) << command << "\n"
                                               << games_check::test::readTextFile((root / "pack.log").string());
  }

  static bool mentions(const Report& report, const std::string& part) {
    return std::any_of(report.failures.begin(), report.failures.end(),
                       [&](const std::string& failure) { return failure.find(part) != std::string::npos; });
  }
  static bool noted(const Report& report, const std::string& part) {
    return std::any_of(report.notes.begin(), report.notes.end(),
                       [&](const std::string& note) { return note.find(part) != std::string::npos; });
  }

  // Red: the report failed and every part is in some failure.
  static void expectRed(const Report& report, std::initializer_list<const char*> parts) {
    EXPECT_FALSE(report.ok()) << "the check passed; it should fail\n" << report.text();
    for (const char* part : parts)
      EXPECT_TRUE(mentions(report, part)) << "no failure mentions '" << part << "'\n" << report.text();
  }
  static void expectGreen(const Report& report) { EXPECT_TRUE(report.ok()) << report.text(); }

  static const games_check::RoundReport* detail(const RoundDetails& details, const std::string& name) {
    for (const auto& [round, report] : details) {
      if (round == name) return &report;
    }
    return nullptr;
  }

  // The game's log lines that start with `prefix`, in order.
  static std::vector<std::string> logLines(const games_check::RoundReport& report, const std::string& prefix) {
    std::vector<std::string> lines;
    for (const std::string& line : report.log) {
      if (line.compare(0, prefix.size(), prefix) == 0) lines.push_back(line);
    }
    return lines;
  }

  fs::path root;
};

// ---- the package ----

TEST_F(GamesCheckEngineTest, AGreenGamePassesAllThreeChecks) {
  copyGame("pass-open", "pass-open");
  round("pass-open", "x-wins-in-pass", X_WINS_IN_PASS);
  round("pass-open", "x-wins-in-solo", X_WINS_IN_SOLO);
  pack({"pass-open"});
  expectGreen(games_check::checkPackage(roots(), "pass-open"));
  expectGreen(games_check::runGameChecks(roots(), "pass-open"));
  const Report rounds = games_check::playRounds(roots(), "pass-open");
  expectGreen(rounds);
  EXPECT_TRUE(noted(rounds, "round 'x-wins-in-pass' (pass) played to its end")) << rounds.text();
  EXPECT_TRUE(noted(rounds, "round 'x-wins-in-solo' (solo) played to its end")) << rounds.text();
  EXPECT_TRUE(noted(games_check::runGameChecks(roots(), "pass-open"), "no checks.lua"));
  expectGreen(games_check::companionHasGame(roots(), "pass-open"));
}

TEST_F(GamesCheckEngineTest, APackFailureFailsThatIdsPackageTestWithThePackersStderrAndTheOtherIdsStillRun) {
  copyGame("pass-open", "pass-open");
  copyGame("pass-open", "no-main");
  fs::remove(games() / "no-main" / "main.lua");
  round("pass-open", "x-wins-in-pass", X_WINS_IN_PASS);
  pack({"no-main", "pass-open"});
  const Report bad = games_check::checkPackage(roots(), "no-main");
  expectRed(bad, {"scripts/pack_game.py refused games/no-main/", "error:"});
  // The game's other checks fail the same way, and name the packer, not a missing file.
  expectRed(games_check::runGameChecks(roots(), "no-main"), {"pack_game.py refused"});
  expectRed(games_check::playRounds(roots(), "no-main"), {"pack_game.py refused"});
  // The id packed after it is untouched.
  expectGreen(games_check::checkPackage(roots(), "pass-open"));
  expectGreen(games_check::playRounds(roots(), "pass-open"));
}

TEST_F(GamesCheckEngineTest, AnIdThePackerWasNeverRunForHasNoPackage) {
  copyGame("pass-open", "pass-open");
  pack(std::vector<std::string>{});
  expectRed(games_check::checkPackage(roots(), "pass-open"), {"no package pass-open.chgame"});
}

TEST_F(GamesCheckEngineTest, APackageTheInstallerAcceptsIsListedWithThePackersHash) {
  copyGame("pass-art", "pass-art");
  pack({"pass-art"});
  expectGreen(games_check::checkPackage(roots(), "pass-art"));
  // A hash that is not the packer's is a failure.
  write(packed() / "pass-art.hash", "0123456789abcdef\n");
  expectRed(games_check::checkPackage(roots(), "pass-art"), {"is not the packer's 0123456789abcdef"});
}

TEST_F(GamesCheckEngineTest, AGameTheHostCannotStartFailsThePackageTest) {
  // GameRegistry lists a package the host cannot start as unavailable; the check must not read that as a pass.
  copyGame("pass-open", "too-many-seats");
  edit("too-many-seats", "manifest.json", "\"seats\": { \"min\": 1, \"max\": 2 }",
       "\"seats\": { \"min\": 9, \"max\": 9 }");
  edit("too-many-seats", "manifest.json", "\"modes\": [\"solo\", \"pass\"]", "\"modes\": [\"pass\"]");
  pack({"too-many-seats"});
  expectRed(games_check::checkPackage(roots(), "too-many-seats"), {"unavailable on this host"});
}

TEST_F(GamesCheckEngineTest, APackOverAnOldFailureStartsCleanSoAFixedGameIsGreenAndItsPackErrorIsGone) {
  copyGame("pass-open", "pass-open");
  const fs::path main = games() / "pass-open" / "main.lua";
  const std::string source = games_check::test::readTextFile(main.string());
  fs::remove(main);
  pack({"pass-open"});
  expectRed(games_check::checkPackage(roots(), "pass-open"), {"pack_game.py refused"});
  ASSERT_TRUE(fs::exists(packed() / "pass-open.packerror"));
  // The same output folder, packed again after the fix: the old error must not outlive it.
  write(main, source);
  pack({"pass-open"});
  EXPECT_FALSE(fs::exists(packed() / "pass-open.packerror"));
  expectGreen(games_check::checkPackage(roots(), "pass-open"));
}

// ---- module loading (the device refuses a module's load nested two deep inside main's) ----

TEST_F(GamesCheckEngineTest, AModuleThatLoadsInsideAModuleThatLoadsInsideMainFailsThePackageTestNamingTheChain) {
  writeGame("nested", soloManifest("nested"), loadingGame("require(\"a\")\n"));
  write(games() / "nested" / "a.lua", "require(\"b\")\nreturn { a = true }\n");
  write(games() / "nested" / "b.lua", "return { b = true }\n");
  pack({"nested"});
  const Report report = games_check::checkPackage(roots(), "nested");
  expectRed(report,
            {"main > a > b", "3 deep, at most 2", "script recursion too deep to load a module",
             "Require 'b' from main.lua before 'a' (deepest first, so each later require finds its module loaded), "
             "or from a function body"});
  EXPECT_EQ(report.failures.size(), 1u) << report.text();
  // The chain is the deepest one, whatever the rest of main does.
  writeGame("deeper", soloManifest("deeper"), loadingGame("require(\"a\")\n"));
  write(games() / "deeper" / "a.lua", "require(\"b\")\nreturn {}\n");
  write(games() / "deeper" / "b.lua", "require(\"c\")\nreturn {}\n");
  write(games() / "deeper" / "c.lua", "return {}\n");
  pack({"deeper"});
  expectRed(games_check::checkPackage(roots(), "deeper"),
            {"main > a > b > c", "4 deep", "require 'c': script recursion too deep",
             "Require 'c', then 'b' from main.lua before 'a'"});
  // The advice works: main requiring c, then b, then a loads each from main alone.
  writeGame("fixed", soloManifest("fixed"), loadingGame("require(\"c\")\nrequire(\"b\")\nrequire(\"a\")\n"));
  write(games() / "fixed" / "a.lua", "require(\"b\")\nreturn {}\n");
  write(games() / "fixed" / "b.lua", "require(\"c\")\nreturn {}\n");
  write(games() / "fixed" / "c.lua", "return {}\n");
  pack({"fixed"});
  expectGreen(games_check::checkPackage(roots(), "fixed"));
}

TEST_F(GamesCheckEngineTest, ModulesRequiredFlatOrAlreadyLoadedAreGreenAndSoIsALazyRequire) {
  // main requires both: each loads from main alone.
  writeGame("flat", soloManifest("flat"), loadingGame("require(\"a\")\nrequire(\"b\")\n"));
  write(games() / "flat" / "a.lua", "return { a = true }\n");
  write(games() / "flat" / "b.lua", "return { b = true }\n");
  // main requires b, then a, and a requires b: b is already loaded, so a's load nests nothing.
  writeGame("cached", soloManifest("cached"), loadingGame("require(\"b\")\nrequire(\"a\")\n"));
  write(games() / "cached" / "a.lua", "local b = require(\"b\")\nreturn { a = b }\n");
  write(games() / "cached" / "b.lua", "return { b = true }\n");
  // a function body of main requires a, which requires b: not probed (only main's own load is), documented and pinned.
  writeGame("lazy", soloManifest("lazy"), loadingGame("", "require(\"a\")"));
  write(games() / "lazy" / "a.lua", "require(\"b\")\nreturn { a = true }\n");
  write(games() / "lazy" / "b.lua", "return { b = true }\n");
  // main's own load raising is no nesting finding, and a chain read before it raised still is one.
  writeGame("raises", soloManifest("raises"), loadingGame("error(\"main broke\")\n"));
  // A guard fault in main's own load (the instruction budget) is no nesting finding either: the rounds name it.
  writeGame("faults", soloManifest("faults"), loadingGame("while true do end\n"));
  writeGame("raises-late", soloManifest("raises-late"), loadingGame("require(\"a\")\nerror(\"main broke\")\n"));
  write(games() / "raises-late" / "a.lua", "require(\"b\")\nreturn {}\n");
  write(games() / "raises-late" / "b.lua", "return {}\n");
  pack({"flat", "cached", "lazy", "raises", "faults", "raises-late"});
  for (const char* id : {"flat", "cached", "lazy", "raises", "faults"}) {
    expectGreen(games_check::checkPackage(roots(), id));
  }
  expectRed(games_check::checkPackage(roots(), "raises-late"), {"main > a > b"});
}

TEST_F(GamesCheckEngineTest, AProbeThatCannotReadAChainFailsThePackageTestInsteadOfPassingIt) {
  // main removes `require` and locks the globals, so the probe's own `require = real` raises outside its pcall: the
  // chunk fails, which is the probe's failure and not main's, and must not read as "no nesting".
  writeGame(
      "locked", soloManifest("locked"),
      loadingGame("require = nil\nsetmetatable(_G, { __newindex = function() error(\"globals are locked\") end })\n"));
  pack({"locked"});
  expectRed(games_check::checkPackage(roots(), "locked"), {"the module-loading probe failed", "globals are locked"});
}

// ---- faults ----

TEST_F(GamesCheckEngineTest, ALuaErrorFailsTheRoundNamingRoundStepAndMessageAndLaterRoundsStillRun) {
  copyGame("pass-open", "pass-open");
  edit("pass-open", "main.lua", "ch.log(\"apply seat \" .. seat .. \" cell \" .. tostring(move.cell))",
       "error(\"boom in apply\")");
  round("pass-open", "a-breaks", X_WINS_IN_PASS);
  // A round that never moves does not reach apply: it still plays, after the one that failed.
  round("pass-open", "b-never-taps-a-square", R"lua(
return { mode = "pass", steps = { { seat = 1, x = 5, y = 5, move = false } }, unfinished = true }
)lua");
  pack({"pass-open"});
  const Report report = games_check::playRounds(roots(), "pass-open");
  expectRed(report, {"round 'a-breaks', step 1", "boom in apply", "main.lua:"});
  EXPECT_EQ(report.failures.size(), 1u) << report.text();
  EXPECT_TRUE(noted(report, "round 'b-never-taps-a-square' (pass) played to its end")) << report.text();
}

TEST_F(GamesCheckEngineTest, ASetupErrorFailsTheRoundAtItsBeginning) {
  copyGame("pass-open", "pass-open");
  edit("pass-open", "main.lua", "function game.setup(ctx)\n", "function game.setup(ctx)\n  error(\"setup broke\")\n");
  round("pass-open", "x-wins-in-pass", X_WINS_IN_PASS);
  pack({"pass-open"});
  expectRed(games_check::playRounds(roots(), "pass-open"), {"round 'x-wins-in-pass', begin", "setup broke"});
}

TEST_F(GamesCheckEngineTest, AStatusOrDrawErrorOnALaterStepFailsThatStep) {
  copyGame("pass-open", "status-breaks");
  copyGame("pass-open", "draw-breaks");
  edit("status-breaks", "main.lua", "function game.status(state)\n",
       "function game.status(state)\n  if state.moves == 2 then error(\"status broke\") end\n");
  edit("draw-breaks", "main.lua", "function game.draw(state, seat, ui)\n",
       "function game.draw(state, seat, ui)\n  if state.moves == 3 then error(\"draw broke\") end\n");
  round("status-breaks", "x-wins-in-pass", X_WINS_IN_PASS);
  round("draw-breaks", "x-wins-in-pass", X_WINS_IN_PASS);
  pack({"status-breaks", "draw-breaks"});
  expectRed(games_check::playRounds(roots(), "status-breaks"), {"round 'x-wins-in-pass', step 2", "status broke"});
  expectRed(games_check::playRounds(roots(), "draw-breaks"), {"round 'x-wins-in-pass', step 3", "draw broke"});
}

TEST_F(GamesCheckEngineTest, AGameThatDoesNotLoadFailsEveryRound) {
  writeGame("broken", soloManifest("broken"), "return 5\n");
  round("broken", "one", ONE_TAP);
  round("broken", "two", ONE_TAP);
  pack({"broken"});
  const Report report = games_check::playRounds(roots(), "broken");
  expectRed(report,
            {"round 'one', begin", "round 'two', begin", "the game did not load", "main.lua must return a table"});
  EXPECT_EQ(report.failures.size(), 2u) << report.text();
}

TEST_F(GamesCheckEngineTest, AnInstructionBudgetFaultFailsTheRound) {
  copyGame("pass-open", "pass-open");
  edit("pass-open", "main.lua", "  if ev.kind == \"tap\" then\n    ch.log(\"tap for seat \" .. seat)",
       "  if ev.kind == \"tap\" then\n    while true do end\n    ch.log(\"tap for seat \" .. seat)");
  round("pass-open", "spins", X_WINS_IN_PASS);
  pack({"pass-open"});
  expectRed(games_check::playRounds(roots(), "pass-open"), {"round 'spins', step 1", "instruction budget exceeded"});
}

TEST_F(GamesCheckEngineTest, AFrameOverTheCommandLimitFailsTheRound) {
  copyGame("pass-open", "pass-open");
  edit("pass-open", "main.lua", "function game.draw(state, seat, ui)\n  ch.gfx.clear(\"white\")",
       "function game.draw(state, seat, ui)\n  ch.gfx.clear(\"white\")\n  for i = 1, 2100 do ch.gfx.rect(0, 0, 1, 1, "
       "\"black\", true) end");
  round("pass-open", "fills-the-frame", X_WINS_IN_PASS);
  pack({"pass-open"});
  expectRed(games_check::playRounds(roots(), "pass-open"),
            {"round 'fills-the-frame', begin", "draw for seat 1 failed", "frame is full"});
}

TEST_F(GamesCheckEngineTest, ASnapshotOf701BytesFailsAndOneOf700BytesPasses) {
  // The snapshot is the codec's bytes of the state, so its size is a length the test finds out rather than assumes.
  const auto sizeFor = [](const size_t pad) {
    return games_check::test::encodeState("{ pad = string.rep('x', " + std::to_string(pad) + ") }").size();
  };
  const size_t overhead = sizeFor(300) - 300;
  const size_t pad700 = 700 - overhead;
  ASSERT_EQ(sizeFor(pad700), 700u);
  ASSERT_EQ(sizeFor(pad700 + 1), 701u);

  writeGame("green-700", soloManifest("green-700"), padGame(pad700));
  writeGame("red-701", soloManifest("red-701"), padGame(pad700 + 1));
  round("green-700", "one-tap", ONE_TAP);
  round("red-701", "one-tap", ONE_TAP);
  pack({"green-700", "red-701"});
  RoundDetails details;
  expectGreen(games_check::playRounds(roots(), "green-700", &details));
  ASSERT_EQ(details.size(), 1u);
  EXPECT_EQ(details[0].second.maxSnapshotBytes, 700u);
  const Report red = games_check::playRounds(roots(), "red-701");
  expectRed(red, {"round 'one-tap', begin", "the snapshot is 701 B", "over the 700 B"});
}

TEST_F(GamesCheckEngineTest, ASnapshotThatGrowsPastTheLimitDuringAStepFailsThatStep) {
  writeGame("grows", soloManifest("grows"), R"lua(
local game = {}
function game.setup(ctx) return { pad = "" } end
function game.status(state) return { turn = 1 } end
function game.apply(state, seat, move) state.pad = state.pad .. string.rep("x", 400) return state end
function game.input(state, seat, ui, ev) if ev.kind == "tap" then return { tap = true } end end
function game.draw(state, seat, ui) ch.gfx.clear("white") end
return game
)lua");
  round("grows", "three-taps", R"lua(
return { mode = "solo", unfinished = true,
  steps = { { seat = 1, x = 1, y = 1 }, { seat = 1, x = 1, y = 1 }, { seat = 1, x = 1, y = 1 } } }
)lua");
  pack({"grows"});
  expectRed(games_check::playRounds(roots(), "grows"), {"round 'three-taps', step 2", "the snapshot is "});
}

// ---- outcomes ----

TEST_F(GamesCheckEngineTest, AWrongOutcomeFailsNamingExpectedAndActualWinners) {
  copyGame("pass-open", "pass-open");
  round(
      "pass-open", "a-wrong-winner",
      std::string(X_WINS_IN_PASS).replace(std::string(X_WINS_IN_PASS).find("winners = { 1 }"), 15, "winners = { 2 }"));
  round("pass-open", "b-a-draw",
        std::string(X_WINS_IN_PASS).replace(std::string(X_WINS_IN_PASS).find("winners = { 1 }"), 15, "winners = {}"));
  round("pass-open", "c-says-unfinished",
        std::string(X_WINS_IN_PASS)
            .replace(std::string(X_WINS_IN_PASS).find("winners = { 1 }"), 15, "unfinished = true"));
  round("pass-open", "d-ends-early", R"lua(
return { mode = "pass", steps = { { seat = 1, x = 97, y = 270 } }, winners = { 1 } }
)lua");
  round("pass-open", "e-right", X_WINS_IN_PASS);
  pack({"pass-open"});
  const Report report = games_check::playRounds(roots(), "pass-open");
  expectRed(report,
            {"round 'a-wrong-winner', step 5: expected winners {2}, got {1}",
             "round 'b-a-draw', step 5: expected winners {}, got {1}",
             "round 'c-says-unfinished', step 5: the round is over (winners {1}), but the round file says unfinished",
             "round 'd-ends-early', step 1: the round is not over (seat 2 to move)"});
  EXPECT_EQ(report.failures.size(), 4u) << report.text();
  EXPECT_TRUE(noted(report, "round 'e-right' (pass) played to its end"));
}

// ---- rounds and modes ----

TEST_F(GamesCheckEngineTest, AGameWithNoRoundsFails) {
  copyGame("pass-open", "no-folder");
  copyGame("pass-open", "empty-folder");
  copyGame("pass-open", "only-a-readme");
  fs::create_directories(companion() / "empty-folder" / "rounds");
  write(companion() / "only-a-readme" / "rounds" / "README.md", "not a round");
  pack({"no-folder", "empty-folder", "only-a-readme"});
  for (const char* id : {"no-folder", "empty-folder", "only-a-readme"}) {
    expectRed(games_check::playRounds(roots(), id), {"no rounds"});
    // The package itself is fine: the game's other tests are not affected.
    expectGreen(games_check::checkPackage(roots(), id));
  }
}

TEST_F(GamesCheckEngineTest, ACompanionFolderWithoutAGameFails) {
  round("ghost", "one", ONE_TAP);
  expectRed(games_check::companionHasGame(roots(), "ghost"), {"has no game", "ghost"});
  copyGame("pass-open", "pass-open");
  expectGreen(games_check::companionHasGame(roots(), "pass-open"));
}

TEST_F(GamesCheckEngineTest, ARoundForAnUndeclaredModeFails) {
  writeGame("solo-only", soloManifest("solo-only"), padGame(10));
  round("solo-only", "a-pass", R"lua(
return { mode = "pass", steps = { { seat = 1, x = 1, y = 1 } }, unfinished = true }
)lua");
  round("solo-only", "b-solo", ONE_TAP);
  pack({"solo-only"});
  const Report report = games_check::playRounds(roots(), "solo-only");
  expectRed(report, {"rounds/a-pass.lua", "mode 'pass' is not one the manifest declares"});
  EXPECT_EQ(report.failures.size(), 1u) << report.text();
  EXPECT_TRUE(noted(report, "round 'b-solo' (solo) played to its end"));
}

TEST_F(GamesCheckEngineTest, ARoundForAModeOtherThanSoloOrPassFailsEvenWhenTheManifestDeclaresIt) {
  copyGame("pass-open", "pass-open");
  edit("pass-open", "manifest.json", "\"modes\": [\"solo\", \"pass\"]", "\"modes\": [\"solo\", \"pass\", \"nearby\"]");
  round("pass-open", "a-nearby", R"lua(
return { mode = "nearby", steps = { { seat = 1, x = 1, y = 1 } }, unfinished = true }
)lua");
  round("pass-open", "b-pass", X_WINS_IN_PASS);
  pack({"pass-open"});
  const Report report = games_check::playRounds(roots(), "pass-open");
  expectRed(report, {"rounds/a-nearby.lua", "mode 'nearby' is not solo or pass"});
  EXPECT_EQ(report.failures.size(), 1u) << report.text();
}

TEST_F(GamesCheckEngineTest, ADeclaredNearbyIsSkippedAndLoggedNeverAFailure) {
  copyGame("pass-open", "pass-open");
  edit("pass-open", "manifest.json", "\"modes\": [\"solo\", \"pass\"]", "\"modes\": [\"solo\", \"pass\", \"nearby\"]");
  round("pass-open", "x-wins-in-pass", X_WINS_IN_PASS);
  pack({"pass-open"});
  expectGreen(games_check::checkPackage(roots(), "pass-open"));
  const Report report = games_check::playRounds(roots(), "pass-open");
  expectGreen(report);
  EXPECT_TRUE(noted(report, "declared mode nearby skipped: this host cannot start it")) << report.text();
}

TEST_F(GamesCheckEngineTest, AMalformedRoundFileFailsNamingTheKeyAndTheOtherRoundsStillRun) {
  copyGame("pass-open", "pass-open");
  round("pass-open", "a-unknown-key",
        "return { mode = 'solo', colour = 'red', steps = { { seat = 1, x = 1, y = 1 } }, unfinished = true }");
  round("pass-open", "b-wrong-type",
        "return { mode = 'solo', steps = { { seat = 1, x = 'left', y = 1 } }, unfinished = true }");
  round("pass-open", "c-no-steps", "return { mode = 'solo', steps = {}, unfinished = true }");
  round("pass-open", "d-both",
        "return { mode = 'solo', steps = { { seat = 1, x = 1, y = 1 } }, unfinished = true, winners = {} }");
  round("pass-open", "e-neither", "return { mode = 'solo', steps = { { seat = 1, x = 1, y = 1 } } }");
  round("pass-open", "f-seed",
        "return { mode = 'solo', seed = 2.5, steps = { { seat = 1, x = 1, y = 1 } }, unfinished = true }");
  round("pass-open", "g-setting",
        "return { mode = 'solo', settings = { speed = 'Fast' }, steps = { { seat = 1, x = 1, y = 1 } }, unfinished = "
        "true }");
  round("pass-open", "h-raises", "error('this file raises')");
  round("pass-open", "i-right", X_WINS_IN_PASS);
  pack({"pass-open"});
  const Report report = games_check::playRounds(roots(), "pass-open");
  expectRed(report, {"a-unknown-key.lua: 'colour'", "b-wrong-type.lua: steps[1].x must be an integer",
                     "c-no-steps.lua: steps is empty", "d-both.lua: winners and unfinished are both set",
                     "e-neither.lua: neither winners nor unfinished", "f-seed.lua: seed must be an integer",
                     "g-setting.lua: settings.speed", "h-raises.lua: rounds/h-raises.lua:1: this file raises"});
  EXPECT_EQ(report.failures.size(), 8u) << report.text();
  EXPECT_TRUE(noted(report, "round 'i-right' (pass) played to its end"));
}

// ---- the game's own checks (C2) ----

TEST_F(GamesCheckEngineTest, AFailingChecksLuaFailsThatCheckByNameAndTheRestRun) {
  copyGame("pass-open", "pass-open");
  checks("pass-open", R"lua(
return {
  { name = "adds", run = function() assert(1 + 1 == 2) end },
  { name = "the board is wrong", run = function() error("cell 3 is empty") end },
  { name = "reads the game", run = function()
      local game = require("main")
      local status = game.status({ seats = 2, moves = 3, cells = { 1, 1, 1, 0, 0, 0, 0, 0, 0 } })
      assert(status.over and status.winners[1] == 1, "the top row wins")
    end },
}
)lua");
  pack({"pass-open"});
  const Report report = games_check::runGameChecks(roots(), "pass-open");
  expectRed(report, {"check 'the board is wrong' failed", "cell 3 is empty"});
  EXPECT_EQ(report.failures.size(), 1u) << report.text();
  EXPECT_TRUE(noted(report, "2 of 3 checks passed")) << report.text();
}

TEST_F(GamesCheckEngineTest, APassingChecksLuaPassesEvenWhenItsChecksTogetherPassTheBudget) {
  copyGame("pass-open", "pass-open");
  // Each check spins about 1.2 million instructions: three of them are past one 2 M budget, and each has its own.
  checks("pass-open", R"lua(
local function spin() for i = 1, 1200000 do end end
return {
  { name = "first", run = spin },
  { name = "second", run = spin },
  { name = "third", run = spin },
}
)lua");
  pack({"pass-open"});
  const Report report = games_check::runGameChecks(roots(), "pass-open");
  expectGreen(report);
  EXPECT_TRUE(noted(report, "3 of 3 checks passed")) << report.text();
}

TEST_F(GamesCheckEngineTest, ACheckThatExceedsTheBudgetFailsAndStopsTheRemainingChecksCounted) {
  copyGame("pass-open", "pass-open");
  checks("pass-open", R"lua(
return {
  { name = "fine", run = function() end },
  { name = "spins", run = function() while true do end end },
  { name = "never runs", run = function() error("it ran") end },
  { name = "never runs either", run = function() error("it ran") end },
}
)lua");
  pack({"pass-open"});
  const Report report = games_check::runGameChecks(roots(), "pass-open");
  expectRed(report, {"check 'spins' faulted", "instruction budget exceeded",
                     "the remaining 2 of the game's 4 checks were not run"});
  EXPECT_EQ(report.failures.size(), 1u) << "the checks after the fault must not run: " << report.text();
  EXPECT_FALSE(mentions(report, "it ran"));
}

TEST_F(GamesCheckEngineTest, AMissingOrEmptyOrMalformedListFailsAndALoadErrorOrFaultFails) {
  copyGame("pass-open", "pass-open");
  pack({"pass-open"});
  struct Case {
    const char* checks;
    const char* expected;
  };
  for (const Case& test :
       {Case{"return {}", "must return a non-empty list"},
        Case{"return 5", "must return a list of {name, run}, not a number"},
        Case{"return { 5 }", "check #1 must be a table {name, run}"},
        Case{"return { { name = 'x' } }",
             "check #1 must be a table with a non-empty string `name` and a function `run`"},
        Case{"return { { run = function() end } }", "check #1 must be a table with a non-empty string `name`"},
        Case{"error('load broke')", "load broke"}, Case{"while true do end", "instruction budget exceeded"}}) {
    checks("pass-open", test.checks);
    expectRed(games_check::runGameChecks(roots(), "pass-open"), {"checks.lua", test.expected});
  }
}

TEST_F(GamesCheckEngineTest, ChecksLuaRunsWithMathRandomSeededOne) {
  copyGame("pass-open", "pass-open");
  pack({"pass-open"});
  const auto firstDraw = [](const uint32_t seed) {
    std::string error;
    auto vm = games_check::OwnedVm::create(GameScript::GameSources{}, {}, GameCore::NO_IMAGES, seed, error);
    int ref = games_check::ScriptVm::NO_REF;
    int64_t value = -1;
    if (vm && vm->vm().runChunk("return math.random(1000000000)", "@draw.lua", ref).ok()) {
      vm->vm().inspect(
          ref, [](lua_State* L, void* out) { *static_cast<int64_t*>(out) = lua_tointeger(L, -1); }, &value);
    }
    return value;
  };
  const int64_t seedOne = firstDraw(1);
  ASSERT_NE(seedOne, firstDraw(2)) << "the seed must show in the draw, or this test proves nothing";
  checks("pass-open", "return { { name = 'seeded', run = function() assert(math.random(1000000000) == " +
                          std::to_string(seedOne) + ", 'math.random is not seeded 1') end } }");
  expectGreen(games_check::runGameChecks(roots(), "pass-open"));
  checks("pass-open", "return { { name = 'seeded', run = function() assert(math.random(1000000000) == " +
                          std::to_string(firstDraw(2)) + ", 'math.random is not seeded 1') end } }");
  expectRed(games_check::runGameChecks(roots(), "pass-open"), {"math.random is not seeded 1"});
}

TEST_F(GamesCheckEngineTest, NoChecksLuaRunsNoChecks) {
  copyGame("pass-open", "pass-open");
  pack({"pass-open"});
  const Report report = games_check::runGameChecks(roots(), "pass-open");
  expectGreen(report);
  EXPECT_TRUE(noted(report, "no checks.lua")) << report.text();
}

TEST_F(GamesCheckEngineTest, ACompanionModuleCanBeRequiredByRoundsAndChecksAndAClashWithTheGamesOwnFails) {
  copyGame("pass-open", "pass-open");
  module(
      "pass-open", "grid",
      "return { x = function(col) return 27 + col * 140 + 70 end, y = function(row) return 200 + row * 140 + 70 end }");
  round("pass-open", "uses-grid", R"lua(
local grid = require("grid")
return { mode = "solo", steps = { { seat = 1, x = grid.x(0), y = grid.y(0) } }, unfinished = true }
)lua");
  checks("pass-open",
         "local grid = require('grid') return { { name = 'grid', run = function() assert(grid.x(1) == 237) end } }");
  pack({"pass-open"});
  expectGreen(games_check::playRounds(roots(), "pass-open"));
  expectGreen(games_check::runGameChecks(roots(), "pass-open"));
  // A companion `main.lua` has the name of the game's own module: the folder is wrong, whatever it holds.
  module("pass-open", "main", "return {}");
  expectRed(games_check::playRounds(roots(), "pass-open"),
            {"main.lua", "has the name of one of the game's own modules"});
  expectRed(games_check::runGameChecks(roots(), "pass-open"),
            {"checks.lua", "has the name of one of the game's own modules"});
  fs::remove(companion() / "pass-open" / "main.lua");
  module("pass-open", "Bad-Name", "return {}");
  expectRed(games_check::playRounds(roots(), "pass-open"), {"Bad-Name.lua", "no module name"});
}

// ---- taps ----

TEST_F(GamesCheckEngineTest, ARejectedTapLeavesVerAndShowsItsReasonAndAMovingTapAddsOne) {
  copyGame("pass-open", "pass-open");
  round("pass-open", "rejects", R"lua(
return { mode = "pass", unfinished = true, steps = {
  { seat = 1, x = 97, y = 270 },                                                  -- moves: ver + 1
  { seat = 2, x = 97, y = 270, move = false, shows = "That square is taken" },    -- rejected: ver unchanged, seat 2 is told
  { seat = 2, x = 5, y = 5, move = false },                                       -- off the board: no move
  { seat = 2, x = 237, y = 270, wait = 5000 },                                    -- moves, after 5 s on the clock
} }
)lua");
  pack({"pass-open"});
  expectGreen(games_check::playRounds(roots(), "pass-open"));
}

TEST_F(GamesCheckEngineTest, ATapThatBreaksItsStepsMoveRuleOrShowsRuleFails) {
  copyGame("pass-open", "pass-open");
  round("pass-open", "a-rejected-without-move-false", R"lua(
return { mode = "pass", unfinished = true, steps = { { seat = 1, x = 97, y = 270 }, { seat = 2, x = 97, y = 270 } } }
)lua");
  round("pass-open", "b-moves-though-move-false", R"lua(
return { mode = "pass", unfinished = true, steps = { { seat = 1, x = 97, y = 270, move = false } } }
)lua");
  round("pass-open", "c-off-the-board-without-move-false", R"lua(
return { mode = "solo", unfinished = true, steps = { { seat = 1, x = 5, y = 5 } } }
)lua");
  round("pass-open", "d-shows-the-wrong-text", R"lua(
return { mode = "pass", unfinished = true, steps = {
  { seat = 1, x = 97, y = 270 }, { seat = 2, x = 97, y = 270, move = false, shows = "Nothing like this" } } }
)lua");
  pack({"pass-open"});
  const Report report = games_check::playRounds(roots(), "pass-open");
  expectRed(report, {"round 'a-rejected-without-move-false', step 2: the tap did not move",
                     "round 'b-moves-though-move-false', step 1: the tap moved",
                     "round 'c-off-the-board-without-move-false', step 1: the tap did not move",
                     "round 'd-shows-the-wrong-text', step 2: seat 2's next frame does not show 'Nothing like this'"});
  EXPECT_EQ(report.failures.size(), 4u) << report.text();
  // The failure says what the frame did show.
  EXPECT_TRUE(mentions(report, "'That square is taken'")) << report.text();
}

TEST_F(GamesCheckEngineTest, AStepForTheWrongSeatFailsBecauseTheDeviceDeliversInputToTheShownSeatOnly) {
  copyGame("pass-open", "pass-open");
  round("pass-open", "a-seat-2-first", R"lua(
return { mode = "pass", unfinished = true, steps = { { seat = 2, x = 97, y = 270 } } }
)lua");
  round("pass-open", "b-seat-1-twice", R"lua(
return { mode = "pass", unfinished = true, steps = { { seat = 1, x = 97, y = 270 }, { seat = 1, x = 237, y = 270 } } }
)lua");
  round("pass-open", "c-after-the-end", R"lua(
return { mode = "pass", winners = { 1 }, steps = {
  { seat = 1, x = 97, y = 270 }, { seat = 2, x = 97, y = 410 }, { seat = 1, x = 237, y = 270 },
  { seat = 2, x = 237, y = 410 }, { seat = 1, x = 377, y = 270 }, { seat = 1, x = 377, y = 410 } } }
)lua");
  pack({"pass-open"});
  expectRed(
      games_check::playRounds(roots(), "pass-open"),
      {"round 'a-seat-2-first', step 1: the step is for seat 2, but the device shows seat 1 and reads input for that "
       "seat only",
       "round 'b-seat-1-twice', step 2: the step is for seat 1, but the device shows seat 2",
       "round 'c-after-the-end', step 6: the round is already over (winners {1}); a step cannot follow its end"});
}

// ---- the hidden flow (D2) ----

constexpr const char* HIDDEN_FOUR_TAPS = R"lua(
return { mode = "pass", steps = {
  { seat = 1, x = 100, y = 200 }, { seat = 2, x = 100, y = 200 }, { seat = 1, x = 100, y = 200 }, { seat = 2, x = 100, y = 200 } },
  winners = {} }
)lua";

TEST_F(GamesCheckEngineTest, TheHiddenFlowPlaysPassHiddenAndEveryLocalSeatIsStillDrawnAfterEachStep) {
  copyGame("pass-hidden", "pass-hidden");
  round("pass-hidden", "four-taps", HIDDEN_FOUR_TAPS);
  pack({"pass-hidden"});
  RoundDetails details;
  expectGreen(games_check::playRounds(roots(), "pass-hidden", &details));
  const games_check::RoundReport* played = detail(details, "four-taps");
  ASSERT_NE(played, nullptr);
  EXPECT_TRUE(played->over);
  EXPECT_EQ(played->winners, 0u);
  // After the round begins only seat 1 is shown (the hand-off first), but both local seats are drawn, seat 2 included.
  ASSERT_GE(played->frames.size(), 3u);
  EXPECT_EQ(played->frames[0].seat, 1);
  EXPECT_EQ(played->frames[1].seat, 1);
  EXPECT_EQ(played->frames[2].seat, 2);
  EXPECT_NE(std::find(played->frames[2].texts.begin(), played->frames[2].texts.end(), "Player 2's secret: river"),
            played->frames[2].texts.end());
  // Seat 0 is drawn once the round is over.
  EXPECT_EQ(played->frames.back().seat, 0);
  // Four moves: setup made ver 1, and each move one more.
  EXPECT_EQ(logLines(*played, "apply seat").size(), 4u);
  EXPECT_EQ(played->ver, 5u);
}

TEST_F(GamesCheckEngineTest, WithDrawEveryLocalSeatOffTheHiddenFlowDrawsExactlyTheSeatsTheDeviceShows) {
  copyGame("pass-hidden", "pass-hidden");
  round("pass-hidden", "four-taps", HIDDEN_FOUR_TAPS);
  pack({"pass-hidden"});
  RoundDetails details;
  PlayOptions options;
  options.drawEveryLocalSeat = false;
  expectGreen(games_check::playRounds(roots(), "pass-hidden", &details, options));
  const games_check::RoundReport* played = detail(details, "four-taps");
  ASSERT_NE(played, nullptr);
  // GameVM's hidden flow (GamesCheckFlowTest pins it): seat 1 shown, a tap, its own frame again under the banner, then
  // the hand-off and seat 2's frame, and so on; the move that ends the round draws seat 0, after Over reached both
  // seats.
  const std::vector<std::string> expected = {
      "draw for seat 1", "tap for seat 1",  "apply seat 1",    "draw for seat 1", "draw for seat 2", "tap for seat 2",
      "apply seat 2",    "draw for seat 2", "draw for seat 1", "tap for seat 1",  "apply seat 1",    "draw for seat 1",
      "draw for seat 2", "tap for seat 2",  "apply seat 2",    "over for seat 1", "over for seat 2", "draw for seat 0"};
  EXPECT_EQ(played->log, expected);
}

TEST_F(GamesCheckEngineTest, AFrameOnlyAnUnshownSeatWouldDrawStillFailsBecauseEveryLocalSeatIsDrawn) {
  copyGame("pass-hidden", "pass-hidden");
  // Seat 2's frame breaks only while no move has been made: the hidden flow shows seat 2 after the first move, never
  // then.
  edit("pass-hidden", "main.lua", "function game.draw(state, seat, ui)\n",
       "function game.draw(state, seat, ui)\n  if seat == 2 and state.moves == 0 then error(\"seat 2's first frame "
       "broke\") end\n");
  round("pass-hidden", "one-move", R"lua(
return { mode = "pass", unfinished = true, steps = { { seat = 1, x = 100, y = 200 }, { seat = 2, x = 100, y = 200 } } }
)lua");
  pack({"pass-hidden"});
  expectRed(games_check::playRounds(roots(), "pass-hidden"),
            {"round 'one-move', begin", "draw for seat 2 failed", "seat 2's first frame broke"});
  // Without the option the round plays as the device would show it: this is what the check exists to improve on.
  PlayOptions options;
  options.drawEveryLocalSeat = false;
  expectGreen(games_check::playRounds(roots(), "pass-hidden", nullptr, options));
}

TEST_F(GamesCheckEngineTest, AHiddenStepForTheWrongSeatOrAfterTheEndFails) {
  copyGame("pass-hidden", "pass-hidden");
  round("pass-hidden", "a-seat-2-first", R"lua(
return { mode = "pass", unfinished = true, steps = { { seat = 2, x = 100, y = 200 } } }
)lua");
  round("pass-hidden", "b-the-same-seat-twice", R"lua(
return { mode = "pass", unfinished = true, steps = { { seat = 1, x = 100, y = 200 }, { seat = 1, x = 100, y = 200 } } }
)lua");
  round("pass-hidden", "c-a-fifth-tap", R"lua(
return { mode = "pass", winners = {}, steps = {
  { seat = 1, x = 100, y = 200 }, { seat = 2, x = 100, y = 200 }, { seat = 1, x = 100, y = 200 }, { seat = 2, x = 100, y = 200 },
  { seat = 1, x = 100, y = 200 } } }
)lua");
  pack({"pass-hidden"});
  expectRed(games_check::playRounds(roots(), "pass-hidden"),
            {"round 'a-seat-2-first', step 1: the step is for seat 2, but the device shows seat 1",
             "round 'b-the-same-seat-twice', step 2: the step is for seat 1, but the device shows seat 2",
             "round 'c-a-fifth-tap', step 5: the round is already over (winners {})"});
}

TEST_F(GamesCheckEngineTest, AHiddenMoveThatKeepsTheTurnAndARejectedTapRedrawTheTurnSeatAlone) {
  // The flow pin (GamesCheckFlowTest) plays this same game and round through GameVM and compares the logs; here the log
  // is the one GameVM::stepHandOff's rules give: no Result or hand-off unless `!over && turn != mover`.
  writeGame("keeps-turn", games_check::test::HIDDEN_KEEPS_TURN_MANIFEST, games_check::test::HIDDEN_KEEPS_TURN_GAME);
  round("keeps-turn", "keeps-and-rejects", games_check::test::HIDDEN_KEEPS_TURN_ROUND);
  pack({"keeps-turn"});
  expectGreen(games_check::playRounds(roots(), "keeps-turn"));
  RoundDetails details;
  PlayOptions options;
  options.drawEveryLocalSeat = false;
  expectGreen(games_check::playRounds(roots(), "keeps-turn", &details, options));
  const games_check::RoundReport* played = detail(details, "keeps-and-rejects");
  ASSERT_NE(played, nullptr);
  EXPECT_EQ(played->log, games_check::test::HIDDEN_KEEPS_TURN_LOG);
}

TEST_F(GamesCheckEngineTest, AStepsWaitMovesTheClockByExactlyThatManyMilliseconds) {
  writeGame("clock", soloManifest("clock"), R"lua(
local game = {}
function game.setup(ctx) return { taps = 0 } end
function game.status(state) return { turn = 1 } end
function game.apply(state, seat, move)
  ch.log("apply at " .. ch.time.ms())
  state.taps = state.taps + 1
  return state
end
function game.input(state, seat, ui, ev) if ev.kind == "tap" then return { tap = true } end end
function game.draw(state, seat, ui) ch.gfx.clear("white") end
return game
)lua");
  round("clock", "waits", R"lua(
return { mode = "solo", unfinished = true, steps = {
  { seat = 1, x = 1, y = 1 }, { seat = 1, x = 1, y = 1, wait = 1234 }, { seat = 1, x = 1, y = 1, wait = 50 },
  { seat = 1, x = 1, y = 1 } } }
)lua");
  pack({"clock"});
  RoundDetails details;
  expectGreen(games_check::playRounds(roots(), "clock", &details));
  const games_check::RoundReport* played = detail(details, "waits");
  ASSERT_NE(played, nullptr);
  const std::vector<std::string> applies = logLines(*played, "apply at ");
  ASSERT_EQ(applies.size(), 4u);
  const auto at = [&](const size_t i) { return std::stoll(applies[i].substr(9)); };
  EXPECT_EQ(at(1) - at(0), 1234);
  EXPECT_EQ(at(2) - at(1), 50);
  EXPECT_EQ(at(3) - at(2), 0);
}

TEST_F(GamesCheckEngineTest, AStepAfterTheRoundIsOverFailsInSoloWhetherItMovesOrNot) {
  copyGame("pass-open", "pass-open");
  const std::string five = R"lua(
    { seat = 1, x = 97, y = 270 }, { seat = 1, x = 97, y = 410 }, { seat = 1, x = 237, y = 270 },
    { seat = 1, x = 237, y = 410 }, { seat = 1, x = 377, y = 270 })lua";
  round("pass-open", "a-moves-after-the-end",
        "return { mode = 'solo', winners = { 1 }, steps = {" + five + ", { seat = 1, x = 377, y = 410 } } }");
  round("pass-open", "b-no-move-after-the-end",
        "return { mode = 'solo', winners = { 1 }, steps = {" + five + ", { seat = 1, x = 5, y = 5, move = false } } }");
  pack({"pass-open"});
  const Report report = games_check::playRounds(roots(), "pass-open");
  expectRed(
      report,
      {"round 'a-moves-after-the-end', step 6: the round is already over (winners {1}); a step cannot follow its end",
       "round 'b-no-move-after-the-end', step 6: the round is already over (winners {1})"});
  EXPECT_EQ(report.failures.size(), 2u) << report.text();
}

TEST_F(GamesCheckEngineTest, ASoloRoundDrawsOnlyItsOneSeatNeverSeatZeroEvenOnceOver) {
  // Stands in for the device: solo's seatShown is the one local seat, and the device never asks a game to draw seat 0.
  copyGame("pass-open", "pass-open");
  round("pass-open", "x-wins-in-solo", X_WINS_IN_SOLO);
  pack({"pass-open"});
  RoundDetails details;
  expectGreen(games_check::playRounds(roots(), "pass-open", &details));
  const games_check::RoundReport* played = detail(details, "x-wins-in-solo");
  ASSERT_NE(played, nullptr);
  EXPECT_TRUE(played->over);
  ASSERT_FALSE(played->frames.empty());
  for (const auto& frame : played->frames) EXPECT_EQ(frame.seat, 1) << "a solo round drew seat " << int(frame.seat);
}

TEST_F(GamesCheckEngineTest, ATestNameIsValidAndUniqueForAnyFolderName) {
  // gtest aborts at registration on a name with a character that is not a letter, a digit, or `_`, and on a repeated
  // one.
  const std::vector<std::string> ids = {"a-b", "a_b", "a.b", "a b", "A-b", "", "9"};
  std::vector<std::string> names;
  for (size_t i = 0; i < ids.size(); ++i) {
    const std::string name = games_check::testNameOf(ids[i], i);
    for (const char c : name) EXPECT_TRUE(std::isalnum(static_cast<unsigned char>(c)) || c == '_') << name;
    names.push_back(name);
  }
  std::sort(names.begin(), names.end());
  EXPECT_EQ(std::adjacent_find(names.begin(), names.end()), names.end());
  EXPECT_EQ(games_check::testNameOf("pass-open", 3), "pass_open_3");
}

// ---- settings, seeds, and steps that are a function ----

TEST_F(GamesCheckEngineTest, SettingsReachTheGameAsTheRoundChoseOrAsTheManifestDefaults) {
  copyGame("pass-art", "pass-art");
  round("pass-art", "a-chosen", R"lua(
return { mode = "pass", settings = { level = "Easy", board = "Large" }, unfinished = true, steps = {
  { seat = 1, x = 100, y = 200, shows = "Level: Easy" }, { seat = 2, x = 100, y = 200, shows = "Board: Large" } } }
)lua");
  // No settings: Level's declared default (Hard), and Board's first value (Small), which declares none.
  round("pass-art", "b-defaults", R"lua(
return { mode = "pass", unfinished = true, steps = {
  { seat = 1, x = 100, y = 200, shows = "Level: Hard" }, { seat = 2, x = 100, y = 200, shows = "Board: Small" } } }
)lua");
  round("pass-art", "c-solo", R"lua(
return { mode = "solo", settings = { board = "Medium" }, unfinished = true, steps = {
  { seat = 1, x = 100, y = 200, shows = "Mode: solo" }, { seat = 1, x = 100, y = 200, shows = "Board: Medium" } } }
)lua");
  pack({"pass-art"});
  const Report report = games_check::playRounds(roots(), "pass-art");
  expectGreen(report);
  EXPECT_TRUE(noted(report, "round 'a-chosen' (pass) played to its end")) << report.text();

  // The same round claiming the other value fails: the settings are what the frame shows.
  round("pass-art", "a-chosen", R"lua(
return { mode = "pass", settings = { level = "Easy" }, unfinished = true, steps = { { seat = 1, x = 100, y = 200, shows = "Level: Hard" } } }
)lua");
  expectRed(games_check::playRounds(roots(), "pass-art"),
            {"round 'a-chosen', step 1: seat 1's next frame does not show 'Level: Hard'"});
}

TEST_F(GamesCheckEngineTest, TheSeedDecidesMathRandomTheSameSeedRepeatsAndAnotherDiffers) {
  writeGame("draws", soloManifest("draws"), DRAW_GAME);
  const auto roundWith = [](const std::string& seed) {
    return "return { mode = 'solo', " + seed + "unfinished = true, steps = { { seat = 1, x = 1, y = 1 } } }";
  };
  round("draws", "a-seven", roundWith("seed = 7, "));
  round("draws", "b-seven-again", roundWith("seed = 7, "));
  round("draws", "c-eight", roundWith("seed = 8, "));
  round("draws", "d-none", roundWith(""));
  round("draws", "e-one", roundWith("seed = 1, "));
  pack({"draws"});
  RoundDetails details;
  expectGreen(games_check::playRounds(roots(), "draws", &details));
  const auto draws = [&](const std::string& name) {
    const games_check::RoundReport* played = detail(details, name);
    EXPECT_NE(played, nullptr) << name;
    return played ? logLines(*played, "r ") : std::vector<std::string>{};
  };
  ASSERT_FALSE(draws("a-seven").empty());
  EXPECT_EQ(draws("a-seven"), draws("b-seven-again"));
  EXPECT_NE(draws("a-seven"), draws("c-eight"));
  EXPECT_EQ(draws("d-none"), draws("e-one")) << "no seed is seed 1";
  EXPECT_NE(draws("d-none"), draws("a-seven"));
}

TEST_F(GamesCheckEngineTest, AStepsFunctionOfTheDecodedInitialStatePlaysTheRound) {
  copyGame("pass-open", "pass-open");
  round("pass-open", "from-the-state", R"lua(
return { mode = "pass", winners = { 1 }, steps = function(state)
  assert(state.seats == 2 and #state.cells == 9 and state.moves == 0, "the initial state")
  local list = {}
  for i, cell in ipairs({ 1, 4, 2, 5, 3 }) do
    list[i] = { seat = (i - 1) % 2 + 1, x = 27 + ((cell - 1) % 3) * 140 + 70, y = 200 + ((cell - 1) // 3) * 140 + 70 }
  end
  return list
end }
)lua");
  pack({"pass-open"});
  expectGreen(games_check::playRounds(roots(), "pass-open"));
}

TEST_F(GamesCheckEngineTest, AStepsFunctionThatRaisesExceedsItsBudgetOrReturnsNoListFailsTheRound) {
  copyGame("pass-open", "pass-open");
  round("pass-open", "a-raises",
        "return { mode = 'pass', unfinished = true, steps = function(state) error('no steps for you') end }");
  round("pass-open", "b-spins",
        "return { mode = 'pass', unfinished = true, steps = function(state) while true do end end }");
  round("pass-open", "c-no-list",
        "return { mode = 'pass', unfinished = true, steps = function(state) return 'taps' end }");
  round("pass-open", "d-bad-step",
        "return { mode = 'pass', unfinished = true, steps = function(state) return { { seat = 1, x = 1 } } end }");
  round("pass-open", "e-fine", X_WINS_IN_PASS);
  pack({"pass-open"});
  const Report report = games_check::playRounds(roots(), "pass-open");
  expectRed(
      report,
      {"round 'a-raises', begin: rounds/a-raises.lua: steps(state): rounds/a-raises.lua:1: no steps for you",
       "round 'b-spins', begin: rounds/b-spins.lua: steps(state): rounds/b-spins.lua:1: instruction budget exceeded",
       "round 'c-no-list', begin: rounds/c-no-list.lua: steps(state)'s result must be a list of steps",
       "round 'd-bad-step', begin: rounds/d-bad-step.lua: steps(state)'s result[1].y is missing"});
  EXPECT_EQ(report.failures.size(), 4u) << report.text();
  EXPECT_TRUE(noted(report, "round 'e-fine' (pass) played to its end"));
}

}  // namespace
