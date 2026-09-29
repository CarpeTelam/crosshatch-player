// GamePackageInstaller's icon rules over the fake SD card (entry 7 of epic-install-and-launcher): the
// manifest's icon must be a name in the game icon library (R9), checked here because GameCore cannot see the
// library; a malformed icon or icon_weight is an invalid manifest; and a pass-only game installs but this
// host cannot start it.

#include <GameIcons.h>
#include <Manifest.h>
#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <string>
#include <utility>

#include "GamePackageInstaller.h"
#include "GameRegistry.h"
#include "HalDisplay.h"
#include "InstallerSupport.h"

HalDisplay display;

using namespace installer_test;
using GamePackageInstaller::Error;

namespace {

const std::string INBOX = "/games/";

// A manifest.json for a solo game with `extra` members (`"icon": "x"`) before the closing brace.
std::string withMembers(const std::string& extra, const std::string& modes = R"(["solo"])",
                        const std::string& seats = R"({"min": 1, "max": 1})") {
  std::string json = manifestJson("g", "Test Game", 1, seats, modes);
  json.pop_back();
  return json + ", " + extra + "}";
}

// Every file and folder under /.games and /.games-data, with its bytes.
using Snapshot = std::map<std::string, std::pair<bool, Bytes>>;

Snapshot snapshotOfGames() {
  Snapshot snapshot;
  for (const auto& entry : fakesd::sim().entries) {
    if (entry.dead) continue;
    if (fakesd::inSubtree(entry.path, "/.games") || fakesd::inSubtree(entry.path, "/.games-data")) {
      snapshot[entry.path] = {entry.isDir, entry.bytes};
    }
  }
  return snapshot;
}

class InstallerIconTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fakesd::reset();
    fakelog::lines.clear();
  }

  static GamePackageInstaller::Report install(const std::string& manifest) {
    fakesd::addFile(INBOX + "g.cpgame", gamePackage("g", {}, manifest));
    return GamePackageInstaller::installAll();
  }

  // The package is renamed .bad for `why`, and nothing is installed.
  static void expectRejected(const std::string& manifest, const Error why) {
    const GamePackageInstaller::Report report = install(manifest);
    EXPECT_EQ(report.installed, 0) << manifest;
    EXPECT_EQ(report.failed, 1) << manifest;
    EXPECT_EQ(report.firstError, why) << manifest << ": " << GamePackageInstaller::describe(report.firstError);
    EXPECT_FALSE(exists(INBOX + "g.cpgame"));
    EXPECT_TRUE(exists(INBOX + "g.cpgame.bad")) << manifest;
    EXPECT_TRUE(childrenOf("/.games").empty());
    EXPECT_FALSE(exists("/.games-tmp"));
  }
};

}  // namespace

TEST_F(InstallerIconTest, AnIconTheLibraryLacksEndsBad) {
  expectRejected(withMembers(R"("icon": "no-such-icon")"), Error::UnknownIcon);
  EXPECT_TRUE(fakelog::any("not in the game icon library"));
}

// Well formed by the grammar, but Phosphor's names only: a real name with a suffix is unknown (the ASSERT
// fails loudly if the library ever grows such a name).
TEST_F(InstallerIconTest, NearMissesOfLibraryNamesEndBad) {
  for (const size_t index : {size_t{0}, GameIcons::ICON_COUNT / 2, GameIcons::ICON_COUNT - 1}) {
    const std::string real = GameIcons::ICONS[index].name;
    for (const std::string& name : {real + "-x", real + "z"}) {
      SetUp();
      ASSERT_LT(GameIcons::find(name.data(), name.size()), 0) << name << " is a library icon";
      expectRejected(withMembers("\"icon\": \"" + name + "\""), Error::UnknownIcon);
      EXPECT_FALSE(HasFatalFailure());
    }
  }
}

TEST_F(InstallerIconTest, AnIconOutsideTheGrammarIsAnInvalidManifest) {
  for (const char* name : {"old_name", "Game-Controller", "a--b", "-x", "x-", "1x", "", "has space"}) {
    SetUp();
    expectRejected(withMembers(std::string(R"("icon": ")") + name + "\""), Error::BadManifest);
    EXPECT_FALSE(HasFatalFailure());
  }
  SetUp();
  expectRejected(withMembers("\"icon\": \"" + std::string(GameCore::Manifest::MAX_ICON_BYTES + 1, 'x') + "\""),
                 Error::BadManifest);
}

TEST_F(InstallerIconTest, EveryLibraryIconInstalls) {
  for (size_t i = 0; i < GameIcons::ICON_COUNT; ++i) {
    SetUp();
    const std::string name = GameIcons::ICONS[i].name;
    const GamePackageInstaller::Report report = install(withMembers("\"icon\": \"" + name + "\""));
    EXPECT_EQ(report.installed, 1) << name;
    EXPECT_EQ(report.failed, 0) << name;
  }
}

TEST_F(InstallerIconTest, ANameAtTheByteLimitIsCheckedAgainstTheLibraryToo) {
  // 32 bytes of the grammar: well formed, in no library.
  expectRejected(withMembers("\"icon\": \"" + std::string(GameCore::Manifest::MAX_ICON_BYTES, 'x') + "\""),
                 Error::UnknownIcon);
}

TEST_F(InstallerIconTest, NoIconIsFine) { EXPECT_EQ(install(withMembers(R"("icon_weight": "fill")")).installed, 1); }

TEST_F(InstallerIconTest, ABothWeightsIconInstallsAndItsWeightIsKept) {
  for (const char* weight : {"regular", "fill"}) {
    SetUp();
    ASSERT_EQ(
        install(withMembers(std::string(R"("icon": "game-controller", "icon_weight": ")") + weight + "\"")).installed,
        1)
        << weight;
    GameRegistry::Listing listing;
    ASSERT_TRUE(GameRegistry::load(listing));
    ASSERT_EQ(listing.count, 1u);
    EXPECT_STREQ(listing.entries[0].manifest.icon, "game-controller");
    EXPECT_EQ(listing.entries[0].manifest.iconWeight,
              std::strcmp(weight, "fill") == 0 ? GameCore::Manifest::ICON_FILL : GameCore::Manifest::ICON_REGULAR);
  }
}

TEST_F(InstallerIconTest, AMalformedIconWeightIsAnInvalidManifest) {
  for (const char* member : {R"("icon_weight": "bold")", R"("icon_weight": "Fill")", R"("icon_weight": "")",
                             R"("icon_weight": 1)", R"("icon_weight": null)", R"("icon_weight": ["fill"])"}) {
    SetUp();
    expectRejected(withMembers(std::string(R"("icon": "game-controller", )") + member), Error::BadManifest);
    EXPECT_FALSE(HasFatalFailure());
  }
}

// The pass capability: this host cannot run a pass game (HostCaps::pass is false), so a game with solo and
// pass lists as solo only, and a pass-only game installs (a later host may run it) but is unavailable here.
TEST_F(InstallerIconTest, ASoloAndPassGameOffersOnlySoloHere) {
  ASSERT_EQ(install(withMembers(R"("icon": "x")", R"(["solo", "pass"])", R"({"min": 1, "max": 2})")).installed, 1);
  GameRegistry::Listing listing;
  ASSERT_TRUE(GameRegistry::load(listing));
  ASSERT_EQ(listing.count, 1u);
  EXPECT_TRUE(listing.entries[0].check.ok());
  EXPECT_EQ(listing.entries[0].check.modes, GameCore::Manifest::MODE_SOLO);
}

TEST_F(InstallerIconTest, APassOnlyGameInstallsAndIsUnavailable) {
  ASSERT_EQ(install(withMembers(R"("icon": "x")", R"(["pass"])", R"({"min": 2, "max": 2})")).installed, 1);
  GameRegistry::Listing listing;
  ASSERT_TRUE(GameRegistry::load(listing));
  ASSERT_EQ(listing.count, 1u);
  EXPECT_EQ(listing.entries[0].check.status, GameCore::CheckStatus::Unavailable);
  EXPECT_EQ(listing.entries[0].check.reason, GameCore::CheckReason::NoHostMode);
}

// A package installed before the grammar tightened, with "_" in its icon, no longer parses: the registry skips
// its folder like any other invalid manifest (owner decision, epic-install-and-launcher inception).
TEST_F(InstallerIconTest, AFolderWithAnUnderscoreIconIsNotListed) {
  ASSERT_EQ(install(withMembers(R"("icon": "x")")).installed, 1);
  fakesd::addFile("/.games/g/manifest.json", withMembers(R"("icon": "old_name")"));
  GameRegistry::Listing listing;
  ASSERT_TRUE(GameRegistry::load(listing));
  EXPECT_EQ(listing.count, 0u);
  EXPECT_TRUE(fakelog::any("invalid icon"));
}

// An upgrade whose icon the library lacks is rejected before anything is removed: the installed game and its
// data are byte for byte what they were.
TEST_F(InstallerIconTest, AnUpgradeWithAnUnknownIconLeavesTheInstalledGameAndItsDataAlone) {
  ASSERT_EQ(install(withMembers(R"("icon": "x")")).installed, 1);
  fakesd::addFile("/.games-data/g/store.bin", std::string("saved"));
  fakesd::addFile("/.games-data/g/resume.bin", std::string("resume"));
  const Snapshot before = snapshotOfGames();
  ASSERT_FALSE(before.empty());

  const GamePackageInstaller::Report report = install(withMembers(R"("icon": "no-such-icon")"));
  EXPECT_EQ(report.firstError, Error::UnknownIcon);
  EXPECT_EQ(report.installed, 0);
  EXPECT_TRUE(exists(INBOX + "g.cpgame.bad"));
  EXPECT_TRUE(snapshotOfGames() == before) << "/.games or /.games-data changed";
  EXPECT_FALSE(exists("/.games-tmp"));
}
