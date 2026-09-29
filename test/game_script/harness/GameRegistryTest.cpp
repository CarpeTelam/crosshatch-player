// GameRegistry over the fake SD card: which /.games/<id>/ folders are games, in what order,
// and the package hash it reads back from .pkg.

#include <gtest/gtest.h>

#include "GameRegistry.h"
#include "InstallerSupport.h"

using namespace installer_test;

namespace {

const std::string PKG = "v1\n0530a15766e91bf1\n";

class RegistryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fakesd::reset();
    fakelog::lines.clear();
  }

  static void addGame(const std::string& id, const std::string& name = "Test Game", const std::string& pkg = PKG,
                      const std::string& folder = "") {
    const std::string dir = "/.games/" + (folder.empty() ? id : folder);
    fakesd::addFile(dir + "/manifest.json", manifestJson(id, name));
    fakesd::addFile(dir + "/main.lua", std::string("return {}\n"));
    if (!pkg.empty()) fakesd::addFile(dir + "/.pkg", pkg);
  }

  static GameRegistry::Listing list() {
    GameRegistry::Listing listing;
    EXPECT_TRUE(GameRegistry::load(listing));
    return listing;
  }

  static std::vector<std::string> ids(const GameRegistry::Listing& listing) {
    std::vector<std::string> out;
    for (size_t i = 0; i < listing.count; ++i) out.push_back(listing.entries[i].manifest.id);
    return out;
  }
};

}  // namespace

TEST_F(RegistryTest, ACardWithoutGamesIsEmpty) {
  EXPECT_EQ(list().count, 0u);
  fakesd::addDir("/.games");
  EXPECT_EQ(list().count, 0u);
}

TEST_F(RegistryTest, ListsAFolderWithAValidPkgAndItsPackageHash) {
  addGame("alpha", "Alpha");
  const GameRegistry::Listing listing = list();
  ASSERT_EQ(listing.count, 1u);
  EXPECT_STREQ(listing.entries[0].manifest.id, "alpha");
  EXPECT_STREQ(listing.entries[0].manifest.name, "Alpha");
  EXPECT_EQ(hex(Bytes(listing.entries[0].pkgHash, listing.entries[0].pkgHash + GamePkg::HASH_BYTES), 8),
            "0530a15766e91bf1");
  EXPECT_TRUE(listing.entries[0].check.ok());
}

TEST_F(RegistryTest, ALoneFolderWithoutPkgIsNotAGame) {
  addGame("half-installed", "Half", "");
  addGame("done", "Done");
  EXPECT_EQ(ids(list()), std::vector<std::string>{"done"});
}

TEST_F(RegistryTest, AnInvalidPkgIsNotAGame) {
  addGame("upper", "U", "v1\n0530A15766e91bf1\n");
  addGame("no-newline", "N", "v1\n0530a15766e91bf1");
  addGame("longer", "L", "v1\n0530a15766e91bf1\nextra\n");
  addGame("version-two", "V", "v2\n0530a15766e91bf1\n");
  addGame("empty-pkg", "E", "");
  fakesd::addFile("/.games/empty-pkg/.pkg", std::string());
  EXPECT_EQ(list().count, 0u);
}

TEST_F(RegistryTest, TheFolderNameMustEqualTheManifestId) {
  addGame("real-id", "Renamed", PKG, "other-name");
  addGame("fine");
  EXPECT_EQ(ids(list()), std::vector<std::string>{"fine"});
  EXPECT_TRUE(fakelog::any("manifest id is real-id"));
}

TEST_F(RegistryTest, AManifestThatDoesNotParseIsNotAGame) {
  addGame("broken");
  fakesd::addFile("/.games/broken/manifest.json", std::string("{not json"));
  addGame("missing");
  fakesd::removeEntry("/.games/missing/manifest.json");
  EXPECT_EQ(list().count, 0u);
}

TEST_F(RegistryTest, LooseFilesAndDotFoldersAreSkipped) {
  fakesd::addFile("/.games/readme.txt", std::string("x"));
  addGame("kept");
  fakesd::addFile("/.games/.hidden/.pkg", PKG);
  EXPECT_EQ(ids(list()), std::vector<std::string>{"kept"});
}

TEST_F(RegistryTest, SortsByNameIgnoringCaseThenById) {
  addGame("zeta", "apple");
  addGame("beta", "Banana");
  addGame("alpha", "Apple");
  EXPECT_EQ(ids(list()), (std::vector<std::string>{"alpha", "zeta", "beta"}));
}

TEST_F(RegistryTest, AnUnavailableGameIsListedWithItsVerdict) {
  fakesd::addFile("/.games/future/manifest.json", manifestJson("future", "Future", 99));
  fakesd::addFile("/.games/future/.pkg", PKG);
  const GameRegistry::Listing listing = list();
  ASSERT_EQ(listing.count, 1u);
  EXPECT_EQ(listing.entries[0].check.status, GameCore::CheckStatus::Unavailable);
  EXPECT_EQ(listing.entries[0].check.reason, GameCore::CheckReason::ApiTooNew);
}

TEST_F(RegistryTest, ListsAtMostMaxGames) {
  for (size_t i = 0; i < GameRegistry::MAX_GAMES + 3; ++i) {
    char id[16];
    std::snprintf(id, sizeof(id), "game-%03zu", i);
    addGame(id, id);
  }
  EXPECT_EQ(list().count, GameRegistry::MAX_GAMES);
}

TEST_F(RegistryTest, ReadPackageHashReadsOneGamesPkg) {
  addGame("alpha");
  uint8_t hash[GamePkg::HASH_BYTES] = {};
  ASSERT_TRUE(GameRegistry::readPackageHash("alpha", hash));
  EXPECT_EQ(hex(Bytes(hash, hash + sizeof(hash)), 8), "0530a15766e91bf1");
  EXPECT_FALSE(GameRegistry::readPackageHash("absent", hash));
  fakesd::removeEntry("/.games/alpha/.pkg");
  EXPECT_FALSE(GameRegistry::readPackageHash("alpha", hash));
}
