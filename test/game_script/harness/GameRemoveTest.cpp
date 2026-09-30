// GamePackageInstaller::remove over the fake SD card, with the real installer beside it for the reinstall
// (entry 10 of epic-install-and-launcher). The launcher's confirmation is GameRemoveLauncherTest's.

#include <gtest/gtest.h>

#include <string>

#include "GamePackageInstaller.h"
#include "GameRegistry.h"
#include "HalDisplay.h"
#include "InstallerSupport.h"

HalDisplay display;

using namespace installer_test;
using GamePackageInstaller::Error;

namespace {

// What a game's saved data holds: the two files the runtime writes, and a third the runtime does not know about,
// so "keeps every file" is checked against the folder and not against a list of names.
const char* const DATA_FILES[] = {"store.bin", "resume.bin", "notes.dat"};

class RemoveTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fakesd::reset();
    fakelog::lines.clear();
  }

  // Installs `id` through the real installer, from the inbox.
  static void install(const std::string& id) {
    fakesd::addFile("/games/" + id + ".chgame", gamePackage(id));
    const GamePackageInstaller::Report report = GamePackageInstaller::installAll();
    ASSERT_EQ(report.installed, 1) << GamePackageInstaller::describe(report.firstError);
  }

  static void placeData(const std::string& id) {
    for (const char* name : DATA_FILES) {
      fakesd::addFile("/.games-data/" + id + "/" + name, std::string("saved ") + name + " of " + id);
    }
  }

  // Every file in the data folder, byte for byte, and no others.
  static void expectDataKept(const std::string& id) {
    const std::string dir = "/.games-data/" + id;
    EXPECT_EQ(childrenOf(dir).size(), std::size(DATA_FILES));
    for (const char* name : DATA_FILES) {
      ASSERT_TRUE(exists(dir + "/" + name)) << name;
      EXPECT_EQ(toText(fakesd::bytesOf(dir + "/" + name)), std::string("saved ") + name + " of " + id) << name;
    }
  }

  static size_t listed() {
    GameRegistry::Listing listing;
    EXPECT_TRUE(GameRegistry::load(listing));
    return listing.count;
  }

  // Whether any card call named a path under `prefix`.
  static bool touched(const std::string& prefix) {
    for (const std::string& op : fakesd::sim().ops)
      if (op.find(" " + prefix) != std::string::npos) return true;
    return false;
  }
};

}  // namespace

TEST_F(RemoveTest, DeletesTheGamesFolderAndKeepsEveryFileOfItsData) {
  install("g");
  install("other");
  placeData("g");
  ASSERT_EQ(listed(), 2u);

  EXPECT_EQ(GamePackageInstaller::remove("g"), Error::None);
  EXPECT_FALSE(exists("/.games/g"));
  EXPECT_FALSE(exists("/.games/g/.pkg"));
  EXPECT_TRUE(exists("/.games/other/.pkg")) << "another game is not touched";
  EXPECT_EQ(listed(), 1u);
  expectDataKept("g");
  EXPECT_TRUE(fakelog::any("Removed g"));
}

TEST_F(RemoveTest, AReinstallAfterARemoveKeepsTheDataToo) {
  install("g");
  placeData("g");
  ASSERT_EQ(GamePackageInstaller::remove("g"), Error::None);
  expectDataKept("g");

  install("g");
  EXPECT_EQ(listed(), 1u);
  EXPECT_TRUE(exists("/.games/g/.pkg"));
  expectDataKept("g");
}

TEST_F(RemoveTest, ARemoveOfAGameThatIsNotThereIsDoneAndMakesNothing) {
  install("other");
  placeData("g");
  fakesd::sim().ops.clear();
  EXPECT_EQ(GamePackageInstaller::remove("g"), Error::None);
  EXPECT_FALSE(exists("/.games/g"));
  EXPECT_TRUE(exists("/.games/other/.pkg"));
  expectDataKept("g");
  for (const std::string& op : fakesd::sim().ops) {
    EXPECT_TRUE(op.rfind("open ", 0) == 0 || op.rfind("close ", 0) == 0 || op.rfind("list ", 0) == 0) << op;
  }
}

TEST_F(RemoveTest, ACardThatCannotOpenTheGamesFolderIsNotAGameThatIsGone) {
  install("g");
  placeData("g");
  fakesd::sim().failOpen.insert("/.games");
  EXPECT_EQ(GamePackageInstaller::remove("g"), Error::SdCard);
  fakesd::sim().failOpen.clear();
  EXPECT_TRUE(exists("/.games/g/.pkg")) << "nothing was deleted";
  expectDataKept("g");

  // No /.games at all cannot be opened either: the caller is told, not "done".
  fakesd::reset();
  EXPECT_EQ(GamePackageInstaller::remove("g"), Error::SdCard);
}

TEST_F(RemoveTest, TheMarkerGoesBeforeTheOtherFiles) {
  install("g");
  fakesd::sim().ops.clear();
  ASSERT_EQ(GamePackageInstaller::remove("g"), Error::None);
  const int pkg = opIndex("remove /.games/g/.pkg");
  const int manifest = opIndex("remove /.games/g/manifest.json");
  ASSERT_GE(pkg, 0);
  ASSERT_GE(manifest, 0);
  EXPECT_LT(pkg, manifest) << "a stop partway must leave an unlisted folder, never a listed game with files missing";
}

TEST_F(RemoveTest, AMarkerThatWillNotGoLeavesTheGameListedAndWhole) {
  install("g");
  placeData("g");
  fakesd::sim().failRemove.insert("/.games/g/.pkg");
  fakesd::sim().ops.clear();

  EXPECT_EQ(GamePackageInstaller::remove("g"), Error::SdCard);
  EXPECT_TRUE(exists("/.games/g/.pkg"));
  EXPECT_TRUE(exists("/.games/g/manifest.json"));
  EXPECT_TRUE(exists("/.games/g/main.lua"));
  EXPECT_EQ(opIndex("remove /.games/g/manifest.json"), -1) << "no file is deleted once the marker will not go";
  EXPECT_EQ(listed(), 1u) << "the registry and the card agree: the game is still there";
  expectDataKept("g");

  // The card recovers: the same call finishes.
  fakesd::sim().failRemove.clear();
  EXPECT_EQ(GamePackageInstaller::remove("g"), Error::None);
  EXPECT_FALSE(exists("/.games/g"));
}

TEST_F(RemoveTest, ADeleteThatStopsPartwayLeavesAnUnlistedFolderAndTheDataWhole) {
  install("g");
  placeData("g");
  fakesd::sim().failRemove.insert("/.games/g/manifest.json");

  EXPECT_EQ(GamePackageInstaller::remove("g"), Error::SdCard);
  EXPECT_FALSE(exists("/.games/g/.pkg"));
  EXPECT_TRUE(exists("/.games/g")) << "leftovers stay; they are not a game";
  EXPECT_EQ(listed(), 0u) << "a folder without its marker is not listed";
  expectDataKept("g");
  EXPECT_TRUE(fakelog::any("Cannot remove /.games/g"));

  // A second remove (the game is not listed, so only a caller that still has its id) finishes once the card allows.
  fakesd::sim().failRemove.clear();
  EXPECT_EQ(GamePackageInstaller::remove("g"), Error::None);
  EXPECT_FALSE(exists("/.games/g"));
  expectDataKept("g");
}

TEST_F(RemoveTest, AReinstallOverAPartlyDeletedFolderFinishesTheJobAndKeepsTheData) {
  install("g");
  placeData("g");
  fakesd::sim().failRemove.insert("/.games/g/manifest.json");
  ASSERT_EQ(GamePackageInstaller::remove("g"), Error::SdCard);
  fakesd::sim().failRemove.clear();

  install("g");
  EXPECT_EQ(listed(), 1u);
  EXPECT_TRUE(exists("/.games/g/.pkg"));
  expectDataKept("g");
}

TEST_F(RemoveTest, AnIdNoManifestCouldCarryTouchesNothing) {
  install("g");
  fakesd::addFile("/.games-data/g/store.bin", std::string("x"));
  fakesd::sim().ops.clear();
  const std::string tooLong(GameCore::Manifest::MAX_ID_BYTES + 1, 'a');
  for (const char* id : {"", "..", "../.games-data", "a/b", "A", "-a", "a.b", "a b"}) {
    EXPECT_EQ(GamePackageInstaller::remove(id), Error::BadManifest) << id;
  }
  EXPECT_EQ(GamePackageInstaller::remove(nullptr), Error::BadManifest);
  EXPECT_EQ(GamePackageInstaller::remove(tooLong.c_str()), Error::BadManifest);
  EXPECT_TRUE(fakesd::sim().ops.empty()) << "no card call is made for a bad id";
  EXPECT_TRUE(exists("/.games/g/.pkg"));
  EXPECT_TRUE(exists("/.games-data/g/store.bin"));
  // The longest id there can be is accepted.
  const std::string longest(GameCore::Manifest::MAX_ID_BYTES, 'a');
  EXPECT_EQ(GamePackageInstaller::remove(longest.c_str()), Error::None);
}

TEST_F(RemoveTest, NeverTouchesTheDataFolderOrTheScratchFolder) {
  install("g");
  placeData("g");
  // What an interrupted install of the same id could leave beside the game.
  fakesd::addFile("/.games-tmp/g/main.lua", std::string("scratch"));
  fakesd::sim().ops.clear();

  ASSERT_EQ(GamePackageInstaller::remove("g"), Error::None);
  EXPECT_FALSE(touched("/.games-data")) << "the saved data is never opened, listed, renamed, or removed";
  EXPECT_FALSE(touched("/.games-tmp"));
  EXPECT_EQ(toText(fakesd::bytesOf("/.games-tmp/g/main.lua")), "scratch");
  expectDataKept("g");
}

// The shared fake cannot alias two folders, so a cluster chain shared by /.games-tmp/g and /.games/g is modelled as
// GamePackageInstallerTest does: the probe file the installer makes in /.games-tmp/g is already in /.games/g.
TEST_F(RemoveTest, AFolderWithoutAMarkerThatMayShareClustersWithScratchIsNotDeleted) {
  fakesd::addFile("/.games-tmp/g/main.lua", std::string("scratch"));
  fakesd::addFile("/.games/g/main.lua", std::string("final"));
  fakesd::addFile("/.games/g/.xlink", std::string());
  placeData("g");

  EXPECT_EQ(GamePackageInstaller::remove("g"), Error::SdCard);
  EXPECT_EQ(toText(fakesd::bytesOf("/.games-tmp/g/main.lua")), "scratch");
  EXPECT_EQ(toText(fakesd::bytesOf("/.games/g/main.lua")), "final");
  EXPECT_FALSE(exists("/.games-tmp/g/.xlink")) << "the probe file is removed either way";
  EXPECT_TRUE(fakelog::any("Keeping /.games/g"));
  expectDataKept("g");

  // A probe that cannot be made or removed is the safe side too.
  for (const bool cannotMake : {true, false}) {
    SCOPED_TRACE(cannotMake ? "cannot make" : "cannot remove");
    fakesd::removeEntry("/.games/g/.xlink");
    if (cannotMake) {
      fakesd::sim().failOpenWrite.insert("/.games-tmp/g/.xlink");
    } else {
      fakesd::sim().failOpenWrite.clear();
      fakesd::sim().failRemove.insert("/.games-tmp/g/.xlink");
    }
    EXPECT_EQ(GamePackageInstaller::remove("g"), Error::SdCard);
    EXPECT_EQ(toText(fakesd::bytesOf("/.games/g/main.lua")), "final");
  }
}

TEST_F(RemoveTest, AFolderWithoutAMarkerBesideIndependentScratchIsDeletedAndTheScratchStays) {
  fakesd::addFile("/.games-tmp/g/main.lua", std::string("scratch"));
  fakesd::addFile("/.games/g/main.lua", std::string("leftover"));  // the probe file does not show here
  EXPECT_EQ(GamePackageInstaller::remove("g"), Error::None);
  EXPECT_FALSE(exists("/.games/g"));
  EXPECT_EQ(toText(fakesd::bytesOf("/.games-tmp/g/main.lua")), "scratch");
  EXPECT_FALSE(exists("/.games-tmp/g/.xlink"));
}

TEST_F(RemoveTest, AListedGameWithScratchBesideItIsDeletedWithoutAProbe) {
  install("g");
  fakesd::addFile("/.games-tmp/g/main.lua", std::string("scratch"));
  fakesd::sim().ops.clear();
  EXPECT_EQ(GamePackageInstaller::remove("g"), Error::None);
  EXPECT_FALSE(exists("/.games/g"));
  EXPECT_EQ(opIndex("open /.games-tmp/g/.xlink"), -1) << "a marked folder was written after its rename: no probe";
}
