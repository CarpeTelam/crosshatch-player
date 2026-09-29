// GamePackageInstaller's hardening over the fake SD card (entry 6 of epic-install-and-launcher): one crafted
// package per rejection, the at-limit and one-over vectors of package_vectors.json, and what a card fault does.
// The packages come from gen_hardening_packages.py at build time (cases.txt names each and what should happen
// to it), so a new crafted package needs no C++.

#include <gtest/gtest.h>

#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "GamePackageInstaller.h"
#include "GameRegistry.h"
#include "HalDisplay.h"
#include "InstallerSupport.h"
#include "PackageLimits.h"
#include "PngToBmpConverter.h"

HalDisplay display;

using namespace installer_test;
using GamePackageInstaller::Error;

namespace {

struct Case {
  std::string file;      // "<name>.cpgame", in HARDENING_PACKAGES_DIR
  std::string expected;  // "Ok", or an Error's name
  int64_t held = -1;     // the most bytes any file under /.games-tmp may ever hold, or -1 for no check
};

std::vector<Case> loadCases() {
  std::vector<Case> cases;
  std::ifstream list(std::string(HARDENING_PACKAGES_DIR) + "/cases.txt");
  std::string line;
  while (std::getline(list, line)) {
    std::istringstream fields(line);
    Case next;
    if (!(fields >> next.file >> next.expected)) continue;
    if (!(fields >> next.held)) next.held = -1;
    cases.push_back(next);
  }
  return cases;
}

const std::map<std::string, Error>& errorsByName() {
  static const std::map<std::string, Error> names = {
      {"NotAPackage", Error::NotAPackage},
      {"BadManifest", Error::BadManifest},
      {"BadMember", Error::BadMember},
      {"NoMain", Error::NoMain},
      {"TooManyMembers", Error::TooManyMembers},
      {"BadImage", Error::BadImage},
      {"PackageTooBig", Error::PackageTooBig},
      {"MemberTooBig", Error::MemberTooBig},
      {"ImagesTooBig", Error::ImagesTooBig},
      {"BadSize", Error::BadSize},
      {"BadCrc", Error::BadCrc},
      {"BinaryLua", Error::BinaryLua},
      {"SourcesTooBig", Error::SourcesTooBig},
      {"Unsupported", Error::Unsupported},
      {"BadDirectory", Error::BadDirectory},
      {"UnknownIcon", Error::UnknownIcon},
  };
  return names;
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

// The most bytes any file under the scratch folder ever held: a removed file keeps its bytes in the fake card's
// entry list, so this sees files the installer has since deleted.
int64_t mostBytesHeldInScratch() {
  int64_t most = 0;
  for (const auto& entry : fakesd::sim().entries) {
    if (!entry.isDir && fakesd::inSubtree(entry.path, "/.games-tmp")) {
      most = std::max<int64_t>(most, static_cast<int64_t>(entry.bytes.size()));
    }
  }
  return most;
}

// An installed game and its saved data, which no rejection may touch. The game with the crafted packages' own id
// is there too, so a rejected upgrade must leave it whole.
void seedInstalledGame() {
  fakesd::addFile("/.games/hardening/manifest.json", toBytes(manifestJson("hardening")));
  fakesd::addFile("/.games/hardening/main.lua", toBytes("return 'old'\n"));
  fakesd::addFile("/.games/hardening/.pkg", toBytes("v1\nfedcba9876543210\n"));
  fakesd::addFile("/.games/keeper/manifest.json", toBytes(manifestJson("keeper")));
  fakesd::addFile("/.games/keeper/main.lua", toBytes("return {}\n"));
  fakesd::addFile("/.games/keeper/.pkg", toBytes("v1\n0123456789abcdef\n"));
  fakesd::addFile("/.games-data/keeper/store.bin", toBytes("saved"));
  fakesd::addFile("/.games-data/keeper/resume.bin", toBytes("resume"));
  fakesd::addFile("/.games-data/hardening/store.bin", toBytes("saved too"));
}

class HardeningTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fakesd::reset();
    fakelog::lines.clear();
  }
};

class CraftedPackageTest : public ::testing::TestWithParam<Case> {
 protected:
  void SetUp() override {
    fakesd::reset();
    fakelog::lines.clear();
  }
};

std::string nameOf(const ::testing::TestParamInfo<Case>& info) {
  std::string name = info.param.file.substr(0, info.param.file.find('.'));
  for (char& c : name) {
    if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
  }
  return name;
}

}  // namespace

// One test per crafted package: it installs, or it ends .bad with its own reason and the installed games and
// their data are exactly as they were.
TEST_P(CraftedPackageTest, EndsAsTheGeneratorSaysItShould) {
  const Case& crafted = GetParam();
  const Bytes package = readHostFile(std::string(HARDENING_PACKAGES_DIR) + "/" + crafted.file);
  ASSERT_FALSE(package.empty()) << crafted.file;
  seedInstalledGame();
  fakesd::addFile("/games/" + crafted.file, package);
  const Snapshot before = snapshotOfGames();

  const GamePackageInstaller::Report report = GamePackageInstaller::installAll();

  EXPECT_FALSE(exists("/.games-tmp"));
  if (crafted.held >= 0) {
    EXPECT_LE(mostBytesHeldInScratch(), crafted.held) << "more reached the card than the member declares";
  }
  if (crafted.expected == "Ok") {
    EXPECT_EQ(report.installed, 1) << GamePackageInstaller::describe(report.firstError);
    EXPECT_EQ(report.failed, 0);
    EXPECT_FALSE(exists("/games/" + crafted.file));
    EXPECT_TRUE(exists("/.games/hardening/.pkg"));
    EXPECT_EQ(fakesd::bytesOf("/.games/keeper/.pkg"), toBytes("v1\n0123456789abcdef\n"));
    return;
  }
  const auto expected = errorsByName().find(crafted.expected);
  ASSERT_NE(expected, errorsByName().end()) << "cases.txt names an unknown error: " << crafted.expected;
  EXPECT_EQ(report.installed, 0);
  EXPECT_EQ(report.failed, 1);
  EXPECT_EQ(report.firstError, expected->second) << GamePackageInstaller::describe(report.firstError);
  EXPECT_STREQ(report.firstFile, crafted.file.c_str());
  EXPECT_FALSE(exists("/games/" + crafted.file));
  ASSERT_TRUE(exists("/games/" + crafted.file + ".bad"));
  EXPECT_EQ(fakesd::bytesOf("/games/" + crafted.file + ".bad"), package);
  EXPECT_TRUE(snapshotOfGames() == before) << "/.games or /.games-data changed";
}

INSTANTIATE_TEST_SUITE_P(Packages, CraftedPackageTest, ::testing::ValuesIn(loadCases()), nameOf);

// The generator ran and covers every rejection the installer has.
TEST_F(HardeningTest, TheGeneratorMadeOnePackageForEachRejection) {
  const std::vector<Case> cases = loadCases();
  ASSERT_FALSE(cases.empty()) << "no cases in " << HARDENING_PACKAGES_DIR;
  std::set<std::string> expected;
  for (const Case& crafted : cases) expected.insert(crafted.expected);
  for (const char* name :
       {"Ok", "PackageTooBig", "MemberTooBig", "ImagesTooBig", "SourcesTooBig", "TooManyMembers", "BadMember",
        "BadImage", "BadSize", "BadCrc", "BinaryLua", "Unsupported", "BadDirectory", "NotAPackage", "UnknownIcon"}) {
    EXPECT_EQ(expected.count(name), 1u) << "no crafted package for " << name;
  }
  // Each name in cases.txt is one the installer knows, and none of the card-fault errors (those keep the file).
  for (const std::string& name : expected) {
    EXPECT_TRUE(name == "Ok" || errorsByName().count(name) == 1) << name;
  }
}

TEST_F(HardeningTest, EveryErrorHasItsOwnDescription) {
  std::set<std::string> texts;
  for (const Error error :
       {Error::SdCard, Error::OutOfMemory, Error::NotAPackage, Error::BadManifest, Error::BadMember, Error::NoMain,
        Error::TooManyMembers, Error::BadImage, Error::PackageTooBig, Error::MemberTooBig, Error::ImagesTooBig,
        Error::SourcesTooBig, Error::BadSize, Error::BadCrc, Error::BinaryLua, Error::Unsupported, Error::BadDirectory,
        Error::UnknownIcon}) {
    const std::string text = GamePackageInstaller::describe(error);
    EXPECT_NE(text, "unknown error");
    EXPECT_TRUE(texts.insert(text).second) << "two errors describe themselves as: " << text;
  }
}

// ---- the card's faults are not the package's ----------------------------------------------------------

TEST_F(HardeningTest, ADirectoryThatCannotBeReadKeepsTheFileForTheNextTry) {
  const Bytes package = gamePackage("g");
  seedInstalledGame();
  fakesd::addFile("/games/g.cpgame", package);
  const Snapshot before = snapshotOfGames();
  fakesd::sim().failReadAt["/games/g.cpgame"] = 0;

  const GamePackageInstaller::Report report = GamePackageInstaller::installAll();
  EXPECT_EQ(report.failed, 1);
  EXPECT_EQ(report.firstError, Error::SdCard);
  EXPECT_TRUE(exists("/games/g.cpgame"));
  EXPECT_FALSE(exists("/games/g.cpgame.bad"));
  EXPECT_TRUE(snapshotOfGames() == before);

  fakesd::sim().failReadAt.clear();  // the card recovers
  EXPECT_EQ(GamePackageInstaller::installAll().installed, 1);
}

TEST_F(HardeningTest, AFileThatWillNotOpenKeepsItToo) {
  fakesd::addFile("/games/g.cpgame", gamePackage("g"));
  fakesd::sim().failOpen.insert("/games/g.cpgame");
  const GamePackageInstaller::Report report = GamePackageInstaller::installAll();
  EXPECT_EQ(report.firstError, Error::SdCard);
  EXPECT_TRUE(exists("/games/g.cpgame"));
  EXPECT_FALSE(exists("/games/g.cpgame.bad"));
}

// ---- a rejection is one file's, not the run's ---------------------------------------------------------

TEST_F(HardeningTest, ARejectedPackageDoesNotBlockTheGoodOnesAfterIt) {
  seedInstalledGame();
  fakesd::addFile("/games/a-bad.cpgame", readHostFile(std::string(HARDENING_PACKAGES_DIR) + "/crc-deflated.cpgame"));
  fakesd::addFile("/games/b-good.cpgame", gamePackage("second"));
  const GamePackageInstaller::Report report = GamePackageInstaller::installAll();
  EXPECT_EQ(report.installed, 1);
  EXPECT_EQ(report.failed, 1);
  EXPECT_EQ(report.firstError, Error::BadCrc);
  EXPECT_TRUE(exists("/games/a-bad.cpgame.bad"));
  EXPECT_TRUE(exists("/.games/second/.pkg"));
}

TEST_F(HardeningTest, TheVectorPackageStillInstallsWithItsCrcsChecked) {
  const Vector vector = loadVector();
  fakesd::addFile("/games/package-vector.cpgame", vector.package);
  const GamePackageInstaller::Report report = GamePackageInstaller::installAll();
  EXPECT_EQ(report.installed, 1);
  EXPECT_EQ(toText(fakesd::bytesOf("/.games/" + vector.id + "/.pkg")), "v1\n" + vector.packageHash + "\n");
}

// ---- the picture limits are the converter's own -----------------------------------------------------------

namespace {

// Takes what the converter writes and keeps none of it.
class DiscardSink final : public Print {
 public:
  size_t write(const uint8_t) override { return 1; }
  size_t write(const uint8_t*, const size_t size) override { return size; }
};

bool converts(const int width, const int height) {
  fakesd::addFile("/probe.png", solidPng(width, height, 200));
  HalFile png;
  EXPECT_TRUE(Storage.openFileForRead("TEST", "/probe.png", png));
  DiscardSink sink;
  return PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(png, sink, 0, 0);
}

}  // namespace

// IMAGE_MAX_WIDTH and IMAGE_MAX_HEIGHT repeat constants inside PngToBmpConverter.cpp; this ties them.
TEST_F(HardeningTest, TheConverterAcceptsThePictureLimitsAndRefusesOneOver) {
  const int width = static_cast<int>(GameCore::IMAGE_MAX_WIDTH);
  const int height = static_cast<int>(GameCore::IMAGE_MAX_HEIGHT);
  EXPECT_TRUE(converts(width, 1));
  EXPECT_FALSE(converts(width + 1, 1));
  EXPECT_TRUE(converts(1, height));
  EXPECT_FALSE(converts(1, height + 1));
}
