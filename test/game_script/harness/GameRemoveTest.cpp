// GamePackageInstaller::remove over the fake SD card, with the real installer beside it for the reinstall
// (entry 10 of epic-install-and-launcher). The launcher's confirmation is GameRemoveLauncherTest's.

#include <gtest/gtest.h>

#include <cstdio>
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

  // The next visit to Games: installAll with nothing in the inbox, which finishes the removes that stopped partway.
  static GamePackageInstaller::Report visit() { return GamePackageInstaller::installAll(); }

  // What a stop leaves in /.games/<id>/: the folder's files and the .removing marker, and no .pkg.
  static void placeMarkedFolder(const std::string& id, const bool withPkg = false) {
    fakesd::addFile("/.games/" + id + "/manifest.json", std::string("{}"));
    fakesd::addFile("/.games/" + id + "/main.lua", std::string("return {}"));
    if (withPkg) fakesd::addFile("/.games/" + id + "/.pkg", std::string("v1\n0000000000000000\n"));
    fakesd::addFile("/.games/" + id + "/.removing", std::string());
  }

  // As placeMarkedFolder, but the marker comes first in directory order, as a FAT card can list it (a freed slot is
  // reused by the next entry made): the order removeDir would delete it in.
  static void placeMarkerFirstFolder(const std::string& id) {
    fakesd::addFile("/.games/" + id + "/.removing", std::string());
    fakesd::addFile("/.games/" + id + "/manifest.json", std::string("{}"));
    fakesd::addFile("/.games/" + id + "/main.lua", std::string("return {}"));
    fakesd::addFile("/.games/" + id + "/.pkg", std::string("v1\n0000000000000000\n"));
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
    EXPECT_TRUE(op.rfind("open ", 0) == 0 || op.rfind("close ", 0) == 0 || op.rfind("list ", 0) == 0 ||
                op.rfind("exists ", 0) == 0)
        << op;
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

// e4-r1: the launcher says "Could not remove it", so the game the person still sees must not vanish at the next visit.
TEST_F(RemoveTest, APkgThatWillNotGoLeavesTheGameListedWholeAndUnmarked) {
  install("g");
  placeData("g");
  fakesd::sim().failRemove.insert("/.games/g/.pkg");
  fakesd::sim().ops.clear();

  EXPECT_EQ(GamePackageInstaller::remove("g"), Error::SdCard);
  EXPECT_TRUE(exists("/.games/g/.pkg"));
  EXPECT_TRUE(exists("/.games/g/manifest.json"));
  EXPECT_TRUE(exists("/.games/g/main.lua"));
  EXPECT_FALSE(exists("/.games/g/.removing")) << "the marker this call wrote goes again";
  EXPECT_EQ(opIndex("remove /.games/g/manifest.json"), -1) << "no file is deleted once the .pkg will not go";
  EXPECT_EQ(listed(), 1u) << "the registry and the card agree: the game is still there";
  expectDataKept("g");
  EXPECT_TRUE(fakelog::any("Cannot remove /.games/g/.pkg"));

  // A visit with the card back keeps the game the person was told is still there.
  fakesd::sim().failRemove.clear();
  visit();
  EXPECT_TRUE(exists("/.games/g/.pkg"));
  EXPECT_TRUE(exists("/.games/g/main.lua"));
  EXPECT_EQ(listed(), 1u);
  expectDataKept("g");

  // The person asks again, and the same call finishes.
  EXPECT_EQ(GamePackageInstaller::remove("g"), Error::None);
  EXPECT_FALSE(exists("/.games/g"));
  expectDataKept("g");
}

// SdFat's remove can report failure after the entry went (a failed sync): the folder is then unlisted, and only the
// marker lets the next visit finish it, so it stays.
TEST_F(RemoveTest, APkgDeleteThatFailsAfterTheEntryWentKeepsTheMarkerForTheNextVisit) {
  install("g");
  placeData("g");
  fakesd::sim().failRemoveDone.insert("/.games/g/.pkg");

  EXPECT_EQ(GamePackageInstaller::remove("g"), Error::SdCard);
  EXPECT_FALSE(exists("/.games/g/.pkg"));
  EXPECT_TRUE(exists("/.games/g/.removing"));
  EXPECT_TRUE(fakesd::bytesOf("/.games/g/.removing").empty()) << "the marker remove writes is an empty file";
  EXPECT_TRUE(exists("/.games/g/main.lua"));
  EXPECT_EQ(listed(), 0u);

  visit();
  EXPECT_FALSE(exists("/.games/g"));
  expectDataKept("g");
}

// When the marker will not go either, it stays on the listed game, and the next visit finishes the remove (as a
// marker whose close failed and would not go, in AMarkerThatWillNotBeWrittenChangesNothing).
TEST_F(RemoveTest, APkgAndAMarkerThatWillNotGoLeaveTheMarkerForTheNextVisit) {
  install("g");
  placeData("g");
  fakesd::sim().failRemove.insert("/.games/g/.pkg");
  fakesd::sim().failRemove.insert("/.games/g/.removing");

  EXPECT_EQ(GamePackageInstaller::remove("g"), Error::SdCard);
  EXPECT_TRUE(exists("/.games/g/.pkg"));
  EXPECT_TRUE(exists("/.games/g/.removing"));
  EXPECT_TRUE(fakelog::any("Cannot remove /.games/g/.removing"));

  fakesd::sim().failRemove.clear();
  visit();
  EXPECT_FALSE(exists("/.games/g"));
  EXPECT_EQ(listed(), 0u);
  expectDataKept("g");
}

// The probe's order on the remove path: its file is made and closed in /.games-tmp/g, then looked for in /.games/g,
// then removed. Looking after the remove would never see it, and a shared cluster chain would be taken for two folders.
TEST_F(RemoveTest, TheProbeLooksForItsFileBeforeItRemovesIt) {
  fakesd::addFile("/.games-tmp/g/main.lua", std::string("scratch"));
  fakesd::addFile("/.games/g/main.lua", std::string("leftover"));
  fakesd::sim().ops.clear();
  ASSERT_EQ(GamePackageInstaller::remove("g"), Error::None);
  const int opened = opIndex("open /.games-tmp/g/.xlink");
  const int closed = opIndex("close /.games-tmp/g/.xlink");
  const int looked = opIndex("exists /.games/g/.xlink");
  const int removed = opIndex("remove /.games-tmp/g/.xlink");
  ASSERT_GE(opened, 0);
  ASSERT_GE(closed, 0);
  ASSERT_GE(looked, 0);
  ASSERT_GE(removed, 0);
  EXPECT_LT(opened, closed);
  EXPECT_LT(closed, looked);
  EXPECT_LT(looked, removed);
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
  EXPECT_FALSE(exists("/.games/g/.removing")) << "the folder the install replaced took its marker with it";
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
  EXPECT_FALSE(exists("/.games/g/.removing")) << "nothing is written into a folder that may share clusters";
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

// A18: a remove that stops partway is finished at the next visit, only in a folder that holds the .removing marker.

TEST_F(RemoveTest, TheRemovingMarkerIsWrittenBeforeThePkgGoesAndThePkgBeforeTheFolder) {
  install("g");
  fakesd::sim().ops.clear();
  ASSERT_EQ(GamePackageInstaller::remove("g"), Error::None);
  const int opened = opIndex("open /.games/g/.removing");
  const int closed = opIndex("close /.games/g/.removing");
  const int pkg = opIndex("remove /.games/g/.pkg");
  const int manifest = opIndex("remove /.games/g/manifest.json");
  const int marker = opIndex("remove /.games/g/.removing");
  ASSERT_GE(opened, 0);
  ASSERT_GE(closed, 0);
  ASSERT_GE(pkg, 0);
  ASSERT_GE(manifest, 0);
  ASSERT_GE(marker, 0) << "removeDir takes the marker with the folder";
  EXPECT_LT(opened, closed);
  EXPECT_LT(closed, pkg) << "the marker is whole before the .pkg goes";
  EXPECT_LT(pkg, manifest);
  EXPECT_LT(pkg, marker);
  EXPECT_LT(manifest, marker) << "the marker goes after every file it stands for";
  EXPECT_LT(opIndex("remove /.games/g/main.lua"), marker);
  EXPECT_FALSE(exists("/.games/g"));
  std::string lastRemove;
  for (const std::string& op : fakesd::sim().ops)
    if (op.rfind("remove ", 0) == 0) lastRemove = op;
  EXPECT_EQ(lastRemove, "remove /.games/g") << "the folder goes last";
}

// A stop right after the marker was written leaves it on a listed, whole game. A remove or a visit that then cannot
// delete the .pkg keeps that marker (it stands for the earlier request), and the next visit with the card back
// finishes.
TEST_F(RemoveTest, AMarkerFromAnEarlierRemoveStaysWhenThePkgWillNotGoAndTheNextVisitFinishes) {
  install("g");
  install("other");
  placeData("g");
  fakesd::addFile("/.games/g/.removing", std::string());
  fakesd::sim().failRemove.insert("/.games/g/.pkg");

  EXPECT_EQ(GamePackageInstaller::remove("g"), Error::SdCard);
  ASSERT_TRUE(exists("/.games/g/.removing")) << "a marker that was there before the call stays";
  EXPECT_TRUE(fakesd::bytesOf("/.games/g/.removing").empty()) << "the marker is an empty file";
  EXPECT_TRUE(exists("/.games/g/.pkg"));
  EXPECT_EQ(listed(), 2u);

  // A card that still refuses: the visit tries and changes nothing, and keeps the marker too.
  fakesd::sim().ops.clear();
  EXPECT_EQ(visit().failed, 0) << "a resume that fails is logged, not reported as an inbox failure";
  EXPECT_TRUE(exists("/.games/g/manifest.json"));
  EXPECT_TRUE(exists("/.games/g/.removing"));
  EXPECT_EQ(opIndex("remove /.games/g/manifest.json"), -1);
  EXPECT_EQ(opIndex("remove /.games/g/.removing"), -1);
  EXPECT_TRUE(fakelog::any("Could not finish removing g"));

  // The card recovers, and nobody calls remove again.
  fakesd::sim().failRemove.clear();
  fakesd::sim().ops.clear();
  visit();
  EXPECT_FALSE(exists("/.games/g"));
  EXPECT_TRUE(exists("/.games/other/.pkg"));
  EXPECT_EQ(listed(), 1u);
  EXPECT_FALSE(touched("/.games-data"));
  expectDataKept("g");
}

TEST_F(RemoveTest, AStopAfterThePkgIsFinishedOnTheNextVisit) {
  install("g");
  install("other");
  placeData("g");
  fakesd::sim().failRemove.insert("/.games/g/main.lua");

  EXPECT_EQ(GamePackageInstaller::remove("g"), Error::SdCard);
  EXPECT_FALSE(exists("/.games/g/.pkg"));
  EXPECT_TRUE(exists("/.games/g/.removing"));
  EXPECT_EQ(listed(), 1u) << "unlisted already";

  visit();
  EXPECT_TRUE(exists("/.games/g/main.lua")) << "a card that still refuses keeps the folder";

  fakesd::sim().failRemove.clear();
  fakesd::sim().ops.clear();
  visit();
  EXPECT_FALSE(exists("/.games/g"));
  EXPECT_TRUE(exists("/.games/other/.pkg"));
  EXPECT_FALSE(touched("/.games-data"));
  expectDataKept("g");
}

TEST_F(RemoveTest, AFolderThatAPowerLossLeftWithTheMarkerIsRemovedWithNoCallToRemove) {
  install("other");
  placeMarkedFolder("stopped");                  // power lost after the .pkg went
  placeMarkedFolder("early", /*withPkg=*/true);  // power lost right after the marker was written
  placeData("stopped");
  visit();
  EXPECT_FALSE(exists("/.games/stopped"));
  EXPECT_FALSE(exists("/.games/early"));
  EXPECT_TRUE(exists("/.games/other/.pkg"));
  expectDataKept("stopped");
}

TEST_F(RemoveTest, AFolderWithNoMarkerIsNeverTouchedByAVisit) {
  install("g");
  // What a person copies by hand: no .pkg, no marker.
  fakesd::addFile("/.games/hand/main.lua", std::string("mine"));
  fakesd::addFile("/.games/hand/manifest.json", std::string("{}"));
  fakesd::addFile("/.games/hand/sub/notes.txt", std::string("notes"));
  fakesd::addFile("/.games/loose.txt", std::string("a file, not a game"));
  // A marker outside a vetted id's folder, or not directly in a folder of /.games, is not ours either.
  fakesd::addFile("/.games/Bad_Name/.removing", std::string());
  fakesd::addFile("/.games/-dash/.removing", std::string());
  fakesd::addFile("/.games/hand/sub/.removing", std::string());
  fakesd::addFile("/.games/.removing", std::string());
  fakesd::sim().ops.clear();

  const GamePackageInstaller::Report report = visit();
  EXPECT_EQ(report.failed, 0);
  for (const std::string& op : fakesd::sim().ops) {
    EXPECT_TRUE(op.rfind("remove ", 0) != 0 && op.rfind("write ", 0) != 0 && op.rfind("rename ", 0) != 0) << op;
  }
  EXPECT_EQ(toText(fakesd::bytesOf("/.games/hand/main.lua")), "mine");
  EXPECT_TRUE(exists("/.games/hand/manifest.json"));
  EXPECT_TRUE(exists("/.games/hand/sub/notes.txt"));
  EXPECT_TRUE(exists("/.games/loose.txt"));
  EXPECT_TRUE(exists("/.games/Bad_Name/.removing"));
  EXPECT_TRUE(exists("/.games/-dash/.removing"));
  EXPECT_TRUE(exists("/.games/hand/sub/.removing"));
  EXPECT_TRUE(exists("/.games/.removing"));
  EXPECT_TRUE(exists("/.games/g/.pkg"));
  EXPECT_EQ(listed(), 1u);
}

TEST_F(RemoveTest, AVisitNeverTouchesTheDataFolder) {
  install("g");
  placeData("g");
  placeData("orphan");  // data with no game: not ours to sweep either
  placeMarkedFolder("stopped");
  placeData("stopped");
  fakesd::sim().ops.clear();
  visit();
  EXPECT_FALSE(exists("/.games/stopped"));
  EXPECT_FALSE(touched("/.games-data")) << "the saved data is never opened, listed, renamed, or removed";
  expectDataKept("g");
  expectDataKept("orphan");
  expectDataKept("stopped");
}

TEST_F(RemoveTest, AMarkerThatWillNotBeWrittenChangesNothing) {
  install("g");
  placeData("g");
  fakesd::sim().failOpenWrite.insert("/.games/g/.removing");
  fakesd::sim().ops.clear();

  EXPECT_EQ(GamePackageInstaller::remove("g"), Error::SdCard);
  EXPECT_TRUE(exists("/.games/g/.pkg"));
  EXPECT_TRUE(exists("/.games/g/manifest.json"));
  EXPECT_TRUE(exists("/.games/g/main.lua"));
  EXPECT_FALSE(exists("/.games/g/.removing"));
  EXPECT_EQ(opIndex("remove "), -1) << "nothing is deleted once the marker will not be written";
  EXPECT_EQ(listed(), 1u);
  expectDataKept("g");

  // A marker whose close fails may not be whole: it is taken away again, and the game stays listed and whole.
  fakesd::sim().failOpenWrite.clear();
  fakesd::sim().failClose.insert("/.games/g/.removing");
  EXPECT_EQ(GamePackageInstaller::remove("g"), Error::SdCard);
  EXPECT_FALSE(exists("/.games/g/.removing"));
  EXPECT_TRUE(exists("/.games/g/.pkg"));
  visit();
  EXPECT_EQ(listed(), 1u) << "a game that is still whole is not swept by the next visit";
  expectDataKept("g");

  // A marker that will not go either stays on the listed game, and the next visit finishes the remove.
  fakesd::sim().failRemove.insert("/.games/g/.removing");
  EXPECT_EQ(GamePackageInstaller::remove("g"), Error::SdCard);
  EXPECT_TRUE(exists("/.games/g/.removing"));
  EXPECT_TRUE(fakelog::any("Cannot remove /.games/g/.removing"));
  fakesd::sim().failRemove.clear();
  fakesd::sim().failClose.clear();
  visit();
  EXPECT_FALSE(exists("/.games/g"));
  expectDataKept("g");
}

// (The shared fake models a shared cluster chain as RemoveTest's earlier probe cases do: the probe file made in
// /.games-tmp/g is already in /.games/g.)
TEST_F(RemoveTest, AMarkedFolderThatMayShareClustersWithScratchIsKeptByAVisit) {
  fakesd::addFile("/.games-tmp/g/main.lua", std::string("scratch"));
  fakesd::addFile("/.games/g/main.lua", std::string("final"));
  fakesd::addFile("/.games/g/.xlink", std::string());
  fakesd::addFile("/.games/g/.removing", std::string());
  placeData("g");

  visit();
  EXPECT_EQ(toText(fakesd::bytesOf("/.games-tmp/g/main.lua")), "scratch");
  EXPECT_EQ(toText(fakesd::bytesOf("/.games/g/main.lua")), "final");
  EXPECT_TRUE(exists("/.games/g/.removing"));
  EXPECT_FALSE(exists("/.games-tmp/g/.xlink")) << "the probe file is removed either way";
  EXPECT_TRUE(fakelog::any("Keeping /.games/g"));
  EXPECT_TRUE(fakelog::any("Could not finish removing g"));
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
    visit();
    EXPECT_EQ(toText(fakesd::bytesOf("/.games/g/main.lua")), "final");
    EXPECT_EQ(toText(fakesd::bytesOf("/.games-tmp/g/main.lua")), "scratch");
  }
}

TEST_F(RemoveTest, AMarkedFolderBesideIndependentScratchIsFinishedByAVisit) {
  fakesd::addFile("/.games-tmp/g/main.lua", std::string("scratch"));
  placeMarkedFolder("g");
  visit();
  EXPECT_FALSE(exists("/.games/g"));
  EXPECT_FALSE(exists("/.games-tmp/g")) << "the visit empties the scratch folder as it always does";
}

TEST_F(RemoveTest, AVisitFinishesAtMostAFullBatchOfMarkedFolders) {
  const size_t total = GamePackageInstaller::MAX_PER_RUN + 8;
  for (size_t i = 0; i < total; ++i) placeMarkedFolder(std::string("m") + std::to_string(10 + i));
  auto left = [total] {
    size_t n = 0;
    for (size_t i = 0; i < total; ++i) n += exists("/.games/m" + std::to_string(10 + i)) ? 1 : 0;
    return n;
  };
  visit();
  EXPECT_EQ(left(), 8u);
  visit();
  EXPECT_EQ(left(), 0u);
}

// (The fake lists in creation order, so the stuck folder is reached first; a card lists in slot order.)
TEST_F(RemoveTest, AFailingMarkedFolderUsesUpItsPlaceInTheBatchAndDoesNotBlockTheRest) {
  const size_t total = GamePackageInstaller::MAX_PER_RUN;
  placeMarkedFolder("a-stuck");
  fakesd::sim().failRemove.insert("/.games/a-stuck/main.lua");
  for (size_t i = 0; i < total; ++i) placeMarkedFolder(std::string("m") + std::to_string(10 + i));
  visit();
  EXPECT_TRUE(exists("/.games/a-stuck"));
  size_t left = 0;
  for (size_t i = 0; i < total; ++i) left += exists("/.games/m" + std::to_string(10 + i)) ? 1 : 0;
  EXPECT_EQ(left, 1u) << "31 of the 32 marked folders behind the stuck one went, and the stuck one counted";
  visit();
  for (size_t i = 0; i < total; ++i) EXPECT_FALSE(exists("/.games/m" + std::to_string(10 + i)));
  EXPECT_TRUE(exists("/.games/a-stuck"));
}

TEST_F(RemoveTest, AVisitFinishesAMarkedFolderBeforeItInstallsTheSameIdAgain) {
  install("g");
  placeData("g");
  fakesd::sim().failRemove.insert("/.games/g/main.lua");
  ASSERT_EQ(GamePackageInstaller::remove("g"), Error::SdCard);
  fakesd::sim().failRemove.clear();

  fakesd::addFile("/games/g.chgame", gamePackage("g"));
  const GamePackageInstaller::Report report = visit();
  EXPECT_EQ(report.installed, 1);
  EXPECT_EQ(report.failed, 0);
  EXPECT_EQ(listed(), 1u);
  EXPECT_TRUE(exists("/.games/g/.pkg"));
  EXPECT_FALSE(exists("/.games/g/.removing"));
  expectDataKept("g");
}

// The marker outlives every file: a card can list it first, and removeDir would delete it first.
TEST_F(RemoveTest, AMarkerListedBeforeTheFilesIsTheLastEntryDeletedSoAStopIsStillFinished) {
  install("other");
  placeMarkerFirstFolder("g");
  placeData("g");
  fakesd::sim().failRemove.insert("/.games/g/main.lua");
  fakesd::sim().ops.clear();

  EXPECT_EQ(GamePackageInstaller::remove("g"), Error::SdCard);
  EXPECT_TRUE(exists("/.games/g/.removing")) << "the marker is still there when a file will not go";
  EXPECT_FALSE(exists("/.games/g/.pkg"));
  EXPECT_FALSE(exists("/.games/g/manifest.json"));
  EXPECT_TRUE(exists("/.games/g/main.lua"));
  EXPECT_EQ(opIndex("remove /.games/g/.removing"), -1);

  // The visit finds the marker, and finishes once the card allows.
  visit();
  EXPECT_TRUE(exists("/.games/g/.removing"));
  fakesd::sim().failRemove.clear();
  fakesd::sim().ops.clear();
  visit();
  EXPECT_FALSE(exists("/.games/g"));
  EXPECT_TRUE(exists("/.games/other/.pkg"));
  EXPECT_FALSE(touched("/.games-data"));
  expectDataKept("g");
  const int marker = opIndex("remove /.games/g/.removing");
  ASSERT_GE(marker, 0);
  EXPECT_LT(opIndex("remove /.games/g/main.lua"), marker);
}

TEST_F(RemoveTest, ASubfolderInAMarkedFolderGoesBeforeTheMarker) {
  placeMarkedFolder("g");
  fakesd::addFile("/.games/g/extra/notes.txt", std::string("notes"));
  fakesd::sim().ops.clear();
  visit();
  EXPECT_FALSE(exists("/.games/g"));
  EXPECT_LT(opIndex("remove /.games/g/extra/notes.txt"), opIndex("remove /.games/g/.removing"));
}

TEST_F(RemoveTest, ARetryOverAnExistingMarkerDoesNotRewriteIt) {
  placeMarkedFolder("g");
  fakesd::sim().failClose.insert("/.games/g/.removing");  // a rewrite would fail its close and take the marker away
  fakesd::sim().ops.clear();
  EXPECT_EQ(GamePackageInstaller::remove("g"), Error::None);
  EXPECT_FALSE(exists("/.games/g"));
  EXPECT_EQ(opIndex("open /.games/g/.removing"), -1) << "the marker that is there is not made again";

  placeMarkedFolder("h");
  fakesd::sim().failClose.insert("/.games/h/.removing");
  fakesd::sim().ops.clear();
  visit();
  EXPECT_FALSE(exists("/.games/h"));
  EXPECT_EQ(opIndex("open /.games/h/.removing"), -1);
}

// finishRemovals runs before the installs: at the limit, a folder it clears makes room for a package in the same visit.
TEST_F(RemoveTest, AFinishedRemoveFreesRoomForAnInstallInTheSameVisit) {
  char id[16];
  for (size_t i = 0; i < GameRegistry::MAX_GAMES; ++i) {
    std::snprintf(id, sizeof(id), "game-%02u", static_cast<unsigned>(i));
    fakesd::addFile(std::string("/.games/") + id + "/.pkg", std::string("v1\n0000000000000000\n"));
    fakesd::addFile(std::string("/.games/") + id + "/manifest.json", manifestJson(id));
  }
  fakesd::addFile("/.games/game-07/.removing", std::string());  // a stop right after the marker: still whole
  fakesd::addFile("/games/extra.chgame", gamePackage("extra"));

  const GamePackageInstaller::Report report = visit();
  EXPECT_EQ(report.installed, 1);
  EXPECT_EQ(report.failed, 0);
  EXPECT_FALSE(exists("/.games/game-07"));
  EXPECT_TRUE(exists("/.games/extra/.pkg"));
}

// A name too long for removeFolderMarkerLast's buffer (only a person puts one in a game's folder) sends the folder to
// removeDir, so it is still removed: without the fallback the marker would go and rmdir would refuse a full folder.
TEST_F(RemoveTest, AFolderHoldingANameTooLongForTheLoopIsStillRemoved) {
  install("g");
  placeData("g");
  fakesd::addFile("/.games/g/" + std::string(45, 'y') + ".txt", std::string("hand"));
  EXPECT_EQ(GamePackageInstaller::remove("g"), Error::None);
  EXPECT_FALSE(exists("/.games/g"));
  expectDataKept("g");

  placeMarkedFolder("h");
  fakesd::addFile("/.games/h/" + std::string(45, 'y') + ".txt", std::string("hand"));
  visit();
  EXPECT_FALSE(exists("/.games/h"));
}
