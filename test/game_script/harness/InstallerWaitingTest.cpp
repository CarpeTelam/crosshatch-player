// GamePackageInstaller's Report::waiting over the fake SD card, with the real installer: of the failures it counts,
// the packages that wait for room (TooManyGames), the first included when it waits. The launcher's note tells those
// apart from the rest (GamesLauncherTest), so the count is pinned here on real packages at the game limit.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <string>

#include "GamePackageInstaller.h"
#include "GameRegistry.h"
#include "HalDisplay.h"
#include "InstallerSupport.h"

HalDisplay display;

using namespace installer_test;
using GamePackageInstaller::Error;
using GamePackageInstaller::Report;

namespace {

class WaitingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fakesd::reset();
    fakelog::lines.clear();
  }

  static void drop(const std::string& name, const Bytes& package) { fakesd::addFile("/games/" + name, package); }

  // `count` installed games, game-00 upward: a .pkg and the manifest the registry needs to list them.
  static void installedGames(const size_t count) {
    for (size_t i = 0; i < count; ++i) {
      char id[16];
      std::snprintf(id, sizeof(id), "game-%02u", static_cast<unsigned>(i));
      fakesd::addFile("/.games/" + std::string(id) + "/.pkg", std::string("v1\n0000000000000000\n"));
      fakesd::addFile("/.games/" + std::string(id) + "/manifest.json", manifestJson(id));
    }
  }
};

}  // namespace

TEST_F(WaitingTest, NothingWaitsBelowTheLimit) {
  installedGames(3);
  drop("a.chgame", gamePackage("a"));
  drop("b.chgame", toBytes("not a zip"));
  const Report report = GamePackageInstaller::installAll();
  EXPECT_EQ(report.installed, 1);
  EXPECT_EQ(report.failed, 1);
  EXPECT_EQ(report.waiting, 0);
}

// The retro's d-03: three valid packages over the limit all wait, the first among them.
TEST_F(WaitingTest, ThreeValidPackagesAtTheLimitAllWait) {
  installedGames(GameRegistry::MAX_GAMES);
  for (const char* id : {"a", "b", "c"}) drop(std::string(id) + ".chgame", gamePackage(id));
  const Report report = GamePackageInstaller::installAll();
  EXPECT_EQ(report.installed, 0);
  EXPECT_EQ(report.failed, 3);
  EXPECT_EQ(report.waiting, 3) << "the first counts too";
  EXPECT_EQ(report.firstError, Error::TooManyGames);
  EXPECT_STREQ(report.firstFile, "a.chgame");
}

// The retro's d-05: at the limit a package that is invalid past its manifest (here a picture that is not a PNG) is
// judged "too many" before it is judged invalid, so it waits like the valid one and stays in the inbox, not .bad.
TEST_F(WaitingTest, InvalidPackagesPastTheirManifestWaitAtTheLimitLikeAValidOne) {
  installedGames(GameRegistry::MAX_GAMES);
  drop("a.chgame", gamePackage("a"));
  for (const char* id : {"b", "c"}) {
    drop(std::string(id) + ".chgame", gamePackage(id, {{"pic.png", toBytes("not a png at all, no header")}}));
  }
  const Report report = GamePackageInstaller::installAll();
  EXPECT_EQ(report.installed, 0);
  EXPECT_EQ(report.failed, 3);
  EXPECT_EQ(report.waiting, 3) << "the note reads \"2 more waiting for room\"";
  EXPECT_EQ(report.firstError, Error::TooManyGames);
  for (const char* name : {"/games/a.chgame", "/games/b.chgame", "/games/c.chgame"}) EXPECT_TRUE(exists(name)) << name;
  EXPECT_FALSE(exists("/games/b.chgame.bad"));
}

// The retro's d-06: a file that is not a zip fails before the limit is judged, so it does not wait.
TEST_F(WaitingTest, ANonZipFailsForItsOwnReasonAndTheValidOneWaits) {
  installedGames(GameRegistry::MAX_GAMES);
  drop("a.chgame", toBytes("not a zip, and long enough to be looked at as one"));
  drop("b.chgame", gamePackage("b"));
  const Report report = GamePackageInstaller::installAll();
  EXPECT_EQ(report.failed, 2);
  EXPECT_EQ(report.waiting, 1);
  EXPECT_EQ(report.firstError, Error::NotAPackage) << GamePackageInstaller::describe(report.firstError);
  EXPECT_TRUE(exists("/games/a.chgame.bad"));
  EXPECT_TRUE(exists("/games/b.chgame"));
}

// One place left: a bad package fails, a valid one takes the place, and the next valid one waits.
TEST_F(WaitingTest, ABadPackageAValidOneAndOneThatWaitsAtSixtyThree) {
  installedGames(GameRegistry::MAX_GAMES - 1);
  drop("a.chgame", makeZip({{"manifest.json", toBytes(manifestJson("a"))}}));  // no main.lua
  drop("b.chgame", gamePackage("b"));
  drop("c.chgame", gamePackage("c"));
  const Report report = GamePackageInstaller::installAll();
  EXPECT_EQ(report.installed, 1);
  EXPECT_EQ(report.failed, 2);
  EXPECT_EQ(report.waiting, 1) << "c waits; a does not";
  EXPECT_EQ(report.firstError, Error::NoMain) << GamePackageInstaller::describe(report.firstError);
  EXPECT_TRUE(exists("/.games/b/.pkg"));
  EXPECT_TRUE(exists("/games/c.chgame"));
}

// Past a saturated `failed`, a package that waits is not counted in `waiting` either: every waiting package counted is
// one `failed` counted, so the others the note names ("and N more not installed") are never hidden by the ones past
// 255.
TEST_F(WaitingTest, AWaitingPackagePastASaturatedFailedCountIsNotCounted) {
  installedGames(GameRegistry::MAX_GAMES);
  drop("a-bad.chgame", toBytes("not a zip, and long enough to be looked at as one"));
  constexpr int WAITING = 256;
  for (int i = 0; i < WAITING; ++i) {
    char id[16];
    std::snprintf(id, sizeof(id), "wait-%03d", i);
    drop(std::string(id) + ".chgame", gamePackage(id));
  }
  const Report report = GamePackageInstaller::installAll();
  EXPECT_EQ(report.firstError, Error::NotAPackage) << GamePackageInstaller::describe(report.firstError);
  EXPECT_EQ(report.failed, UINT8_MAX);
  EXPECT_EQ(report.waiting, UINT8_MAX - 1) << "the 254 counted in failed after the first, not the 256 that wait";
}

// The count stops at 255 with `failed`, so it is never more than `failed` and never wraps. A waiting package does not
// use the per-run cap, so one call judges all of them.
TEST_F(WaitingTest, TheWaitingCountStopsAt255WithTheFailedCount) {
  installedGames(GameRegistry::MAX_GAMES);
  constexpr int WAITING = 260;
  for (int i = 0; i < WAITING; ++i) {
    char id[16];
    std::snprintf(id, sizeof(id), "wait-%03d", i);
    drop(std::string(id) + ".chgame", gamePackage(id));
  }
  const Report report = GamePackageInstaller::installAll();
  EXPECT_EQ(report.failed, UINT8_MAX);
  EXPECT_EQ(report.waiting, UINT8_MAX);
  EXPECT_EQ(report.firstError, Error::TooManyGames);
}
