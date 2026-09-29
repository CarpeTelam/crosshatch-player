// GamePackageInstaller over the fake SD card, with the real ZipFile, PngToBmpConverter, and
// InflateStream: one package from the inbox to an installed game (entry 3 of
// epic-install-and-launcher). The zip reader's own hardening (limits, CRC, bombs) is entry 6.

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>

#include "GameImages.h"
#include "GamePackageInstaller.h"
#include "GameRegistry.h"
#include "HalDisplay.h"
#include "InstallerSupport.h"
#include "PackageLimits.h"

HalDisplay display;

using namespace installer_test;
using GamePackageInstaller::Error;

namespace {

const std::string INBOX = "/games/";

class InstallerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fakesd::reset();
    fakelog::lines.clear();
  }

  static void drop(const std::string& name, const Bytes& package) { fakesd::addFile(INBOX + name, package); }

  static GamePackageInstaller::Report install() { return GamePackageInstaller::installAll(); }

  // The file must be renamed .bad, with the original bytes, and no game installed.
  static void expectRejected(const std::string& name, const Bytes& original, const Error why) {
    const GamePackageInstaller::Report report = install();
    EXPECT_EQ(report.installed, 0);
    EXPECT_EQ(report.failed, 1);
    EXPECT_EQ(report.firstError, why) << GamePackageInstaller::describe(report.firstError);
    EXPECT_STREQ(report.firstFile, name.c_str());
    EXPECT_FALSE(exists(INBOX + name)) << name << " is still in the inbox";
    ASSERT_TRUE(exists(INBOX + name + ".bad"));
    EXPECT_EQ(fakesd::bytesOf(INBOX + name + ".bad"), original);
    EXPECT_TRUE(childrenOf("/.games").empty());
    EXPECT_FALSE(exists("/.games-tmp"));
  }

  static GameCore::ImageHeader headerOf(const std::string& path, const size_t budget = GameCore::IMAGES_BYTES) {
    const Bytes file = fakesd::bytesOf(path);
    GameCore::ImageHeader header;
    EXPECT_EQ(GameCore::checkImageHeader(file.data(), std::min<size_t>(file.size(), 62), file.size(), budget, header),
              GameCore::ImageCheck::Ok)
        << path;
    return header;
  }
};

}  // namespace

// ---- the golden package -------------------------------------------------------------------------

TEST_F(InstallerTest, InstallsTheVectorPackageWithItsFilesAndHash) {
  const Vector vector = loadVector();
  drop("package-vector.cpgame", vector.package);
  ASSERT_TRUE(GamePackageInstaller::hasInbox());

  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.installed, 1);
  EXPECT_EQ(report.failed, 0);

  // Every member is there byte for byte (main.lua is deflated in the vector), and .pkg.
  const std::string dir = "/.games/" + vector.id + "/";
  for (const Member& member : vector.members) EXPECT_EQ(fakesd::bytesOf(dir + member.name), member.data) << member.name;
  EXPECT_EQ(toText(fakesd::bytesOf(dir + ".pkg")), "v1\n" + vector.packageHash + "\n");
  EXPECT_EQ(childrenOf("/.games/" + vector.id).size(), 4u);
  // The inbox file and the scratch folder are gone.
  EXPECT_FALSE(exists(INBOX + "package-vector.cpgame"));
  EXPECT_FALSE(exists("/.games-tmp"));
  EXPECT_FALSE(GamePackageInstaller::hasInbox());

  GameRegistry::Listing listing;
  ASSERT_TRUE(GameRegistry::load(listing));
  ASSERT_EQ(listing.count, 1u);
  EXPECT_STREQ(listing.entries[0].manifest.id, vector.id.c_str());
  EXPECT_TRUE(listing.entries[0].check.ok());
  EXPECT_EQ(hex(Bytes(listing.entries[0].pkgHash, listing.entries[0].pkgHash + GamePkg::HASH_BYTES), 8),
            vector.packageHash);
}

TEST_F(InstallerTest, WritesPkgLastAndDeletesTheInboxFileAfterIt) {
  drop("package-vector.cpgame", loadVector().package);
  install();
  const int renamed = opIndex("rename /.games-tmp/package-vector /.games/package-vector");
  const int pkg = opIndex("open /.games/package-vector/.pkg");
  const int deleted = opIndex("remove /games/package-vector.cpgame");
  ASSERT_GE(renamed, 0);
  ASSERT_GE(pkg, 0);
  ASSERT_GE(deleted, 0);
  EXPECT_LT(renamed, pkg);
  EXPECT_LT(pkg, deleted);
  // Nothing in the game's folder is written after the .pkg is opened except the .pkg itself.
  const auto& ops = fakesd::sim().ops;
  for (size_t i = static_cast<size_t>(pkg) + 1; i < ops.size(); ++i) {
    if (ops[i].rfind("write ", 0) == 0) {
      EXPECT_EQ(ops[i], "write /.games/package-vector/.pkg");
    }
  }
}

TEST_F(InstallerTest, ReinstallReplacesTheFolderAndKeepsTheGamesData) {
  fakesd::addFile("/.games/package-vector/old.lua", std::string("old"));
  fakesd::addFile("/.games/package-vector/.pkg", std::string("v1\n0000000000000000\n"));
  fakesd::addFile("/.games-data/package-vector/store.bin", std::string("saved"));
  fakesd::addFile("/.games-data/package-vector/resume.bin", std::string("resume"));
  drop("package-vector.cpgame", loadVector().package);

  EXPECT_EQ(install().installed, 1);
  EXPECT_FALSE(exists("/.games/package-vector/old.lua"));
  EXPECT_EQ(toText(fakesd::bytesOf("/.games/package-vector/.pkg")), "v1\n" + loadVector().packageHash + "\n");
  EXPECT_EQ(toText(fakesd::bytesOf("/.games-data/package-vector/store.bin")), "saved");
  EXPECT_EQ(toText(fakesd::bytesOf("/.games-data/package-vector/resume.bin")), "resume");
}

TEST_F(InstallerTest, AFailedReinstallLeavesTheOldGameWhole) {
  drop("g.cpgame", gamePackage("keeper"));
  ASSERT_EQ(install().installed, 1);
  const Bytes before = fakesd::bytesOf("/.games/keeper/.pkg");

  // Same id, but an image that cannot convert: rejected before the old folder is touched.
  const Bytes bad = gamePackage("keeper", {{"badge.png", toBytes("not a png")}});
  drop("g.cpgame", bad);
  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.failed, 1);
  EXPECT_EQ(fakesd::bytesOf("/.games/keeper/.pkg"), before);
  EXPECT_TRUE(exists("/.games/keeper/main.lua"));
  EXPECT_FALSE(exists("/.games-tmp"));
}

TEST_F(InstallerTest, TheHashCoversTheOriginalMembersNotTheConvertedImages) {
  const Bytes badge = solidPng(20, 10, 255);
  const Bytes icon = solidPng(32, 32, 0);
  const std::string manifest = manifestJson("with-images");
  const std::vector<Member> members = {{"manifest.json", toBytes(manifest)},
                                       {"main.lua", toBytes("return {}\n")},
                                       {"badge.png", badge},
                                       {"icon.png", icon}};
  drop("with-images.cpgame", makeZip(members));
  ASSERT_EQ(install().installed, 1);
  EXPECT_EQ(toText(fakesd::bytesOf("/.games/with-images/.pkg")), "v1\n" + hex(packageDigest(members), 8) + "\n");
}

// ---- manifest ------------------------------------------------------------------------------------

TEST_F(InstallerTest, AnUnavailablePackageInstallsAndTheRegistryListsIt) {
  drop("future.cpgame", gamePackage("future", {}, manifestJson("future", "Future", 99)));
  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.installed, 1);
  EXPECT_EQ(report.failed, 0);
  EXPECT_TRUE(exists("/.games/future/.pkg"));
  GameRegistry::Listing listing;
  ASSERT_TRUE(GameRegistry::load(listing));
  ASSERT_EQ(listing.count, 1u);
  EXPECT_EQ(listing.entries[0].check.status, GameCore::CheckStatus::Unavailable);
}

TEST_F(InstallerTest, AManifestThatBreaksItsOwnRulesEndsBad) {
  struct Case {
    const char* what;
    std::string manifest;
  };
  const Case cases[] = {
      {"not JSON", "{ nope"},
      {"solo with two seats at least", manifestJson("g", "G", 1, R"({"min": 2, "max": 2})")},
      {"an id with capitals", manifestJson("Game")},
      {"no modes", manifestJson("g", "G", 1, R"({"min": 1, "max": 1})", "[]")},
      {"api zero", manifestJson("g", "G", 0)},
  };
  for (const Case& c : cases) {
    SetUp();
    const Bytes package = gamePackage("g", {}, c.manifest);
    drop("g.cpgame", package);
    SCOPED_TRACE(c.what);
    expectRejected("g.cpgame", package, Error::BadManifest);
    EXPECT_FALSE(HasFatalFailure());
  }
}

TEST_F(InstallerTest, APackageWithoutAManifestOrMainEndsBad) {
  const Bytes noManifest = makeZip({{"main.lua", toBytes("return {}\n")}});
  drop("a.cpgame", noManifest);
  expectRejected("a.cpgame", noManifest, Error::BadManifest);

  SetUp();
  const Bytes noMain = makeZip({{"manifest.json", toBytes(manifestJson("g"))}});
  drop("b.cpgame", noMain);
  expectRejected("b.cpgame", noMain, Error::NoMain);
}

// ---- members -------------------------------------------------------------------------------------

TEST_F(InstallerTest, AMemberOffTheWhitelistEndsBad) {
  const char* names[] = {"readme.txt",  "icon.bmp", "Main2.lua", "has space.lua", "sub/x.lua",
                         "../evil.lua", "sub/",     "up.PNG",    "x.lua.png",     ".lua"};
  for (const char* name : names) {
    SetUp();
    const Bytes package = gamePackage("g", {{name, toBytes("x")}});
    drop("g.cpgame", package);
    SCOPED_TRACE(name);
    expectRejected("g.cpgame", package, Error::BadMember);
    EXPECT_FALSE(HasFatalFailure());
  }
}

TEST_F(InstallerTest, ALongMemberNameEndsBad) {
  const Bytes package = gamePackage("g", {{std::string(33, 'a') + ".lua", toBytes("x")}});
  drop("g.cpgame", package);
  expectRejected("g.cpgame", package, Error::BadMember);
}

TEST_F(InstallerTest, AMemberNamedAtTheLimitIsAllowed) {
  drop("g.cpgame", gamePackage("g", {{std::string(32, 'a') + ".lua", toBytes("x")}}));
  EXPECT_EQ(install().installed, 1);
  EXPECT_TRUE(exists("/.games/g/" + std::string(32, 'a') + ".lua"));
}

TEST_F(InstallerTest, ADuplicateMemberEndsBad) {
  const Bytes package = gamePackage("g", {{"main.lua", toBytes("return 1\n")}});
  drop("g.cpgame", package);
  expectRejected("g.cpgame", package, Error::BadMember);
}

TEST_F(InstallerTest, MoreMembersThanTheLimitEndsBad) {
  std::vector<Member> extra;
  for (size_t i = 0; i + 2 < GameCore::PACKAGE_MEMBERS + 1; ++i)
    extra.push_back({"m" + std::to_string(i) + ".lua", {}});
  const Bytes package = gamePackage("g", extra);
  drop("g.cpgame", package);
  expectRejected("g.cpgame", package, Error::TooManyMembers);

  SetUp();  // exactly the limit installs
  extra.pop_back();
  drop("g.cpgame", gamePackage("g", extra));
  EXPECT_EQ(install().installed, 1);
}

TEST_F(InstallerTest, NotAZipEndsBad) {
  const Bytes junk = toBytes("this is not a zip file at all, just text long enough to hold an end record");
  drop("junk.cpgame", junk);
  expectRejected("junk.cpgame", junk, Error::NotAPackage);

  SetUp();
  const Bytes tiny = toBytes("PK");
  drop("tiny.cpgame", tiny);
  expectRejected("tiny.cpgame", tiny, Error::NotAPackage);
}

// ---- images --------------------------------------------------------------------------------------

TEST_F(InstallerTest, ConvertsIconPngToA64x64IconBmpAndDropsThePng) {
  drop("g.cpgame", gamePackage("g", {{"icon.png", solidPng(32, 32, 0)}}));
  ASSERT_EQ(install().installed, 1);
  EXPECT_FALSE(exists("/.games/g/icon.png"));
  const GameCore::ImageHeader header = headerOf("/.games/g/icon.bmp");
  EXPECT_EQ(header.width, 64u);
  EXPECT_EQ(header.height, 64u);
  EXPECT_EQ(fakesd::bytesOf("/.games/g/icon.bmp").size(), 62u + 8u * 64u);
}

TEST_F(InstallerTest, AnIconOfAnySquareSizeComesOutAt64x64) {
  for (const int side : {1, 2, 3, 5, 7, 10, 13, 31, 32, 33, 48, 63, 64, 65, 96, 100, 127, 128, 200}) {
    SetUp();
    drop("g.cpgame", gamePackage("g", {{"icon.png", solidPng(side, side, 128)}}));
    SCOPED_TRACE(side);
    const GamePackageInstaller::Report report = install();
    ASSERT_EQ(report.installed, 1) << GamePackageInstaller::describe(report.firstError);
    const GameCore::ImageHeader header = headerOf("/.games/g/icon.bmp");
    EXPECT_EQ(header.width, 64u);
    EXPECT_EQ(header.height, 64u);
  }
}

// Assumption for entry 14: a non-square icon.png is rejected, not scaled and cropped.
TEST_F(InstallerTest, ANonSquareIconEndsBadAndLeavesNothingBehind) {
  for (const auto& size : {std::pair<int, int>{100, 50}, {50, 100}, {64, 63}, {1, 2}}) {
    SetUp();
    const Bytes package = gamePackage("g", {{"icon.png", solidPng(size.first, size.second, 0)}});
    drop("g.cpgame", package);
    SCOPED_TRACE(std::to_string(size.first) + "x" + std::to_string(size.second));
    expectRejected("g.cpgame", package, Error::BadImage);
    EXPECT_FALSE(HasFatalFailure());
    EXPECT_TRUE(fakelog::any("the icon must be square"));
  }
}

TEST_F(InstallerTest, ConvertsAnImageToItsOwnSizeInTheLoadersLayout) {
  // Left half black, right half white, so the bit order shows.
  const Bytes png = makePng(20, 10, [](int x, int) { return x < 10 ? uint8_t{0} : uint8_t{255}; });
  drop("g.cpgame", gamePackage("g", {{"badge.png", png}}));
  ASSERT_EQ(install().installed, 1);
  EXPECT_FALSE(exists("/.games/g/badge.png"));
  EXPECT_FALSE(exists("/.games/g/icon.bmp"));  // no icon.png, no icon.bmp
  const GameCore::ImageHeader header = headerOf("/.games/g/badge.bmp");
  EXPECT_EQ(header.width, 20u);
  EXPECT_EQ(header.height, 10u);
  EXPECT_EQ(header.rowBytes, 4u);
  const Bytes file = fakesd::bytesOf("/.games/g/badge.bmp");
  ASSERT_EQ(file.size(), 62u + 4u * 10u);
  for (int row = 0; row < 10; ++row) {
    const uint8_t* bits = file.data() + 62 + row * 4;
    EXPECT_EQ(bits[0], 0x00) << "row " << row;         // x 0-7: black is 0
    EXPECT_EQ(bits[1] & 0xC0, 0x00) << "row " << row;  // x 8-9: black
    EXPECT_EQ(bits[1] & 0x3F, 0x3F) << "row " << row;  // x 10-15: white is 1, most significant bit first
    EXPECT_EQ(bits[2] & 0xF0, 0xF0) << "row " << row;  // x 16-19: white
  }
}

TEST_F(InstallerTest, APngTheConverterRefusesEndsBad) {
  const std::pair<const char*, Bytes> cases[] = {
      {"not a png", toBytes("GIF89a, and enough bytes after it to fill a whole PNG header")},
      {"too short for a header", Bytes{0x89, 'P', 'N', 'G'}},
      {"interlaced", makePng(
                         8, 8, [](int, int) { return uint8_t{0}; }, true)},
      {"wider than 2048", solidPng(2049, 1, 0)},
  };
  for (const auto& c : cases) {
    SetUp();
    const Bytes package = gamePackage("g", {{"pic.png", c.second}});
    drop("g.cpgame", package);
    SCOPED_TRACE(c.first);
    expectRejected("g.cpgame", package, Error::BadImage);
    EXPECT_FALSE(HasFatalFailure());
  }
}

TEST_F(InstallerTest, ADamagedPngWithASoundHeaderEndsBadAndIsShownOnce) {
  Bytes png = solidPng(20, 20, 0);
  png.resize(png.size() / 2);  // the header is whole, the pixel data is cut
  const Bytes package = gamePackage("g", {{"pic.png", png}});
  drop("g.cpgame", package);
  expectRejected("g.cpgame", package, Error::BadImage);
  EXPECT_TRUE(fakelog::any("Cannot convert"));
  // Renamed, so the next visit does not judge it again.
  EXPECT_FALSE(GamePackageInstaller::hasInbox());
}

TEST_F(InstallerTest, ADamagedPngOnAFailingCardIsTheCardsFault) {
  // The card drops the converter's writes and then the decode fails: the short write decides.
  Bytes png = solidPng(20, 20, 0);
  png.resize(png.size() / 2);
  drop("g.cpgame", gamePackage("g", {{"pic.png", png}}));
  fakesd::sim().failWrite.insert("/.games-tmp/g/pic.bmp");
  EXPECT_EQ(install().firstError, Error::SdCard);
  EXPECT_TRUE(exists("/games/g.cpgame"));
  EXPECT_FALSE(exists("/games/g.cpgame.bad"));
}

TEST_F(InstallerTest, AnImageWrittenShortIsTheCardsFaultNotThePackages) {
  // The converter ignores a failed write, so the output is just short.
  drop("g.cpgame", gamePackage("g", {{"badge.png", solidPng(20, 10, 255)}}));
  fakesd::sim().failWrite.insert("/.games-tmp/g/badge.bmp");
  EXPECT_EQ(install().firstError, Error::SdCard);
  EXPECT_TRUE(exists("/games/g.cpgame"));
  EXPECT_FALSE(exists("/games/g.cpgame.bad"));
  fakesd::sim().failWrite.clear();
  EXPECT_EQ(install().installed, 1);
}

// ---- the inbox -----------------------------------------------------------------------------------

TEST_F(InstallerTest, OnlyCpgameFilesAreInstalled) {
  drop("real.cpgame", gamePackage("real"));
  drop("UPPER.CPGAME", gamePackage("upper"));
  drop("old.cpgame.bad", toBytes("x"));
  drop("notes.txt", toBytes("x"));
  drop("cpgame", toBytes("x"));
  fakesd::addDir("/games/folder.cpgame");
  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.installed, 2);
  EXPECT_EQ(report.failed, 0);
  EXPECT_TRUE(exists("/.games/real/.pkg"));
  EXPECT_TRUE(exists("/.games/upper/.pkg"));
  EXPECT_TRUE(exists("/games/old.cpgame.bad"));
  EXPECT_TRUE(exists("/games/notes.txt"));
  EXPECT_TRUE(exists("/games/folder.cpgame"));
}

TEST_F(InstallerTest, AnEmptyOrMissingInboxDoesNothing) {
  EXPECT_FALSE(GamePackageInstaller::hasInbox());
  EXPECT_EQ(install().installed, 0);
  fakesd::addDir("/games");
  EXPECT_FALSE(GamePackageInstaller::hasInbox());
  EXPECT_EQ(install().installed, 0);
  EXPECT_FALSE(exists("/.games"));
}

TEST_F(InstallerTest, OneBadFileDoesNotStopTheGoodOnes) {
  drop("a-good.cpgame", gamePackage("good-a"));
  const Bytes bad = toBytes("not a zip, long enough to look for an end record in, honest");
  drop("b-bad.cpgame", bad);
  drop("c-good.cpgame", gamePackage("good-c"));
  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.installed, 2);
  EXPECT_EQ(report.failed, 1);
  EXPECT_STREQ(report.firstFile, "b-bad.cpgame");
  EXPECT_TRUE(exists("/.games/good-a/.pkg"));
  EXPECT_TRUE(exists("/.games/good-c/.pkg"));
  EXPECT_EQ(fakesd::bytesOf("/games/b-bad.cpgame.bad"), bad);
}

TEST_F(InstallerTest, ANewBadFileReplacesAnEarlierBadOfTheSameName) {
  drop("g.cpgame.bad", toBytes("earlier"));
  const Bytes bad = toBytes("not a zip, long enough to look for an end record in, honest");
  drop("g.cpgame", bad);
  EXPECT_EQ(install().failed, 1);
  EXPECT_EQ(fakesd::bytesOf("/games/g.cpgame.bad"), bad);
}

TEST_F(InstallerTest, ALeftoverScratchFolderIsDeletedEvenWithNothingToInstall) {
  fakesd::addFile("/.games-tmp/stale/main.lua", std::string("half"));
  fakesd::addFile("/.games-tmp/other/icon.png", std::string("half"));
  EXPECT_EQ(install().installed, 0);
  EXPECT_FALSE(exists("/.games-tmp"));
}

TEST_F(InstallerTest, ALeftoverScratchFolderOfTheSameIdDoesNotBlockTheInstall) {
  fakesd::addFile("/.games-tmp/package-vector/main.lua", std::string("half"));
  drop("package-vector.cpgame", loadVector().package);
  EXPECT_EQ(install().installed, 1);
  EXPECT_EQ(toText(fakesd::bytesOf("/.games/package-vector/util.lua")), "return {}\n");
}

TEST_F(InstallerTest, TakesAtMostMaxPerRunAndTheRestOnTheNextCall) {
  const Bytes package = gamePackage("same");
  for (size_t i = 0; i < GamePackageInstaller::MAX_PER_RUN + 1; ++i) {
    char name[24];
    std::snprintf(name, sizeof(name), "p%02zu.cpgame", i);
    drop(name, package);
  }
  EXPECT_EQ(install().installed, GamePackageInstaller::MAX_PER_RUN);
  EXPECT_TRUE(GamePackageInstaller::hasInbox());
  EXPECT_EQ(install().installed, 1);
  EXPECT_FALSE(GamePackageInstaller::hasInbox());
}

TEST_F(InstallerTest, ANameTooLongForTheInboxIsSkipped) {
  drop(std::string(GamePaths::INBOX_NAME_BYTES, 'a') + ".cpgame", gamePackage("long-name"));
  EXPECT_FALSE(GamePackageInstaller::hasInbox());
  EXPECT_EQ(install().installed, 0);
}

// ---- storage failures ----------------------------------------------------------------------------

TEST_F(InstallerTest, AStorageFailureKeepsTheFileForTheNextTry) {
  drop("g.cpgame", gamePackage("g"));
  fakesd::sim().failRename.insert("/.games-tmp/g");
  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.installed, 0);
  EXPECT_EQ(report.failed, 1);
  EXPECT_EQ(report.firstError, Error::SdCard);
  EXPECT_TRUE(exists("/games/g.cpgame"));
  EXPECT_FALSE(exists("/games/g.cpgame.bad"));
  EXPECT_FALSE(exists("/.games-tmp"));
  EXPECT_TRUE(childrenOf("/.games").empty());

  fakesd::sim().failRename.clear();  // the card recovers
  EXPECT_EQ(install().installed, 1);
  EXPECT_FALSE(exists("/games/g.cpgame"));
}

TEST_F(InstallerTest, AFailedMemberWriteIsAStorageFailure) {
  drop("g.cpgame", gamePackage("g"));
  fakesd::sim().failWrite.insert("/.games-tmp/g/main.lua");
  EXPECT_EQ(install().firstError, Error::SdCard);
  EXPECT_TRUE(exists("/games/g.cpgame"));
  EXPECT_FALSE(exists("/.games-tmp"));
}

TEST_F(InstallerTest, AFailedPkgWriteLeavesNoGameInTheRegistry) {
  drop("g.cpgame", gamePackage("g"));
  fakesd::sim().failWrite.insert("/.games/g/.pkg");
  EXPECT_EQ(install().firstError, Error::SdCard);
  GameRegistry::Listing listing;
  ASSERT_TRUE(GameRegistry::load(listing));
  EXPECT_EQ(listing.count, 0u);
  EXPECT_TRUE(exists("/games/g.cpgame"));  // retried next time, which replaces the folder
  fakesd::sim().failWrite.clear();
  EXPECT_EQ(install().installed, 1);
}

TEST_F(InstallerTest, ACardFailureInTheLastStepOfAReinstallIsRetried) {
  drop("g.cpgame", gamePackage("g"));
  ASSERT_EQ(install().installed, 1);

  // The new package extracts fine, then the rename into place fails: the old folder is already
  // gone (the game is unlisted), and the file stays so the next visit installs it again.
  const Bytes update = gamePackage("g", {{"extra.lua", toBytes("return 2\n")}});
  drop("g.cpgame", update);
  fakesd::sim().failRename.insert("/.games-tmp/g");
  EXPECT_EQ(install().firstError, Error::SdCard);
  EXPECT_TRUE(exists("/games/g.cpgame"));
  EXPECT_FALSE(exists("/games/g.cpgame.bad"));
  GameRegistry::Listing listing;
  ASSERT_TRUE(GameRegistry::load(listing));
  EXPECT_EQ(listing.count, 0u);

  fakesd::sim().failRename.clear();
  EXPECT_EQ(install().installed, 1);
  EXPECT_TRUE(exists("/.games/g/extra.lua"));
  ASSERT_TRUE(GameRegistry::load(listing));
  EXPECT_EQ(listing.count, 1u);
}

// The fixtures README says these pack with pack_game.py and install; a stored zip of the same
// members (the folder's manifest.json and .lua files) stands in for the packer here.
TEST_F(InstallerTest, TheFixtureGamesTheReadmeListsInstallAndCanStartSolo) {
  const char* fixtures[] = {"counter",      "gallery", "icons",  "limits", "loop",
                            "slow-restart", "timer",   "timing", "tracer"};
  for (const char* fixture : fixtures) {
    const std::string dir = std::string(GAME_FIXTURES_DIR) + "/" + fixture;
    std::vector<Member> members;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
      members.push_back({entry.path().filename().string(), readHostFile(entry.path().string())});
    }
    drop(std::string(fixture) + ".cpgame", makeZip(members));
    SCOPED_TRACE(fixture);
    const GamePackageInstaller::Report report = install();
    ASSERT_EQ(report.installed, 1) << GamePackageInstaller::describe(report.firstError);
    GameRegistry::Listing listing;
    ASSERT_TRUE(GameRegistry::load(listing));
    bool found = false;
    for (size_t i = 0; i < listing.count; ++i) {
      if (std::string(listing.entries[i].manifest.id) == fixture) {
        found = listing.entries[i].check.ok() && (listing.entries[i].check.modes & GameCore::Manifest::MODE_SOLO) != 0;
      }
    }
    EXPECT_TRUE(found);
    if (std::string(fixture) == "timing") {
      // The timing fixture's gray.png is the worst case for runs only if the converter dithers it: about half the
      // pixels of the converted 480 x 800 image are white, and neighbours differ.
      const Bytes bmp = fakesd::bytesOf("/.games/timing/gray.bmp");
      ASSERT_EQ(bmp.size(), 62u + 60u * 800u);
      size_t white = 0;
      size_t changes = 0;
      for (size_t y = 0; y < 800; ++y) {
        for (size_t x = 0; x < 480; ++x) {
          const bool bit = (bmp[62 + y * 60 + x / 8] >> (7 - x % 8)) & 1;
          white += bit;
          if (x > 0) changes += bit != (((bmp[62 + y * 60 + (x - 1) / 8] >> (7 - (x - 1) % 8)) & 1) != 0);
        }
      }
      EXPECT_GT(white, 480u * 800u * 4 / 10);
      EXPECT_LT(white, 480u * 800u * 6 / 10);
      EXPECT_GT(changes, 480u * 800u / 4) << "runs of about one or two pixels";
    }
  }
}

// ---- the last step, the scratch folder, and the inbox ---------------------------------------------

TEST_F(InstallerTest, AReinstallTakesTheMarkerAwayBeforeItRemovesTheOldFiles) {
  drop("g.cpgame", gamePackage("g"));
  ASSERT_EQ(install().installed, 1);
  drop("g.cpgame", gamePackage("g", {{"extra.lua", toBytes("return 2\n")}}));

  // Removing the old folder stops partway: the marker is gone, so nothing half-deleted is listed.
  fakesd::sim().failRemove.insert("/.games/g/manifest.json");
  EXPECT_EQ(install().firstError, Error::SdCard);
  EXPECT_FALSE(exists("/.games/g/.pkg"));
  GameRegistry::Listing listing;
  ASSERT_TRUE(GameRegistry::load(listing));
  EXPECT_EQ(listing.count, 0u);
  EXPECT_TRUE(exists("/games/g.cpgame"));
  EXPECT_FALSE(exists("/.games-tmp"));  // the new files were ours alone, so they are cleaned up

  fakesd::sim().failRemove.clear();
  EXPECT_EQ(install().installed, 1);
  EXPECT_TRUE(exists("/.games/g/extra.lua"));
}

TEST_F(InstallerTest, AMarkerThatWillNotGoLeavesTheOldGameWhole) {
  drop("g.cpgame", gamePackage("g"));
  ASSERT_EQ(install().installed, 1);
  drop("g.cpgame", gamePackage("g", {{"extra.lua", toBytes("return 2\n")}}));
  fakesd::sim().failRemove.insert("/.games/g/.pkg");
  EXPECT_EQ(install().firstError, Error::SdCard);
  EXPECT_TRUE(exists("/.games/g/main.lua"));
  EXPECT_FALSE(exists("/.games/g/extra.lua"));
  GameRegistry::Listing listing;
  ASSERT_TRUE(GameRegistry::load(listing));
  EXPECT_EQ(listing.count, 1u);
}

// The shared fake cannot alias two folders, so a cluster chain shared by /.games-tmp/g and /.games/g is
// modelled the way the installer sees it: the probe file it makes in /.games-tmp/g is already in
// /.games/g (installed there beforehand), which is what one directory's data on two paths shows.
void shareClusters(const std::string& id) { fakesd::addFile("/.games/" + id + "/.xlink", std::string()); }

TEST_F(InstallerTest, ScratchSharingClustersWithAGameWithoutAMarkerIsNeverRemoved) {
  // /.games-tmp/g and /.games/g (no .pkg) after an interrupted SdFat folder move: freeing either
  // would free the other's clusters.
  fakesd::addFile("/.games-tmp/g/main.lua", std::string("scratch"));
  fakesd::addFile("/.games/g/main.lua", std::string("final"));
  shareClusters("g");
  fakesd::addFile("/.games-tmp/other/main.lua", std::string("stale"));  // no /.games/other: safe to remove
  fakesd::addFile("/.games-tmp/done/main.lua", std::string("stale"));   // /.games/done has a .pkg: safe
  fakesd::addFile("/.games/done/.pkg", std::string("v1\n0530a15766e91bf1\n"));
  fakesd::addFile("/.games-tmp/loose.txt", std::string("x"));
  drop("g.cpgame", gamePackage("g"));

  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.installed, 0);
  EXPECT_EQ(report.firstError, Error::SdCard);  // mkdir of /.games-tmp/g fails: a clear, repeated report
  EXPECT_TRUE(fakelog::any("Keeping /.games-tmp/g"));
  EXPECT_EQ(toText(fakesd::bytesOf("/.games-tmp/g/main.lua")), "scratch");
  EXPECT_EQ(toText(fakesd::bytesOf("/.games/g/main.lua")), "final");
  EXPECT_FALSE(exists("/.games-tmp/g/.xlink"));  // the probe file is removed either way
  EXPECT_TRUE(exists("/games/g.cpgame"));        // kept, not renamed .bad
  EXPECT_FALSE(exists("/.games-tmp/other"));
  EXPECT_FALSE(exists("/.games-tmp/done"));
  EXPECT_FALSE(exists("/.games-tmp/loose.txt"));
  // The same again next visit: no silent loop, nothing deleted.
  EXPECT_EQ(install().firstError, Error::SdCard);
  EXPECT_EQ(toText(fakesd::bytesOf("/.games-tmp/g/main.lua")), "scratch");
}

TEST_F(InstallerTest, ScratchBesideAnIndependentHalfRemovedGameIsRemovedAndTheInstallGoesOn) {
  // A stop during the removal of the old folder: /.games/g has lost its .pkg and some files, and
  // /.games-tmp/g is whole and its own (the probe file does not show in /.games/g).
  fakesd::addFile("/.games-tmp/g/main.lua", std::string("scratch"));
  fakesd::addFile("/.games/g/manifest.json", std::string("{}"));
  drop("g.cpgame", gamePackage("g"));
  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.installed, 1);
  EXPECT_EQ(report.failed, 0);
  EXPECT_TRUE(exists("/.games/g/.pkg"));
  EXPECT_FALSE(exists("/.games-tmp"));
  EXPECT_FALSE(fakelog::any("Keeping"));
}

TEST_F(InstallerTest, ScratchIsKeptWhenTheProbeFileCannotBeMadeOrRemoved) {
  for (const bool cannotMake : {true, false}) {
    SetUp();
    fakesd::addFile("/.games-tmp/g/main.lua", std::string("scratch"));
    fakesd::addFile("/.games/g/main.lua", std::string("final"));
    drop("g.cpgame", gamePackage("g"));
    if (cannotMake) {
      fakesd::sim().failOpenWrite.insert("/.games-tmp/g/.xlink");
    } else {
      fakesd::sim().failRemove.insert("/.games-tmp/g/.xlink");
    }
    SCOPED_TRACE(cannotMake ? "cannot make" : "cannot remove");
    EXPECT_EQ(install().firstError, Error::SdCard);
    EXPECT_EQ(toText(fakesd::bytesOf("/.games-tmp/g/main.lua")), "scratch");
    EXPECT_TRUE(fakelog::any("Keeping /.games-tmp/g"));
  }
}

TEST_F(InstallerTest, AFileWhereTheScratchFolderGoesIsRemoved) {
  fakesd::addFile("/.games-tmp", std::string("not a folder"));
  drop("g.cpgame", gamePackage("g"));
  EXPECT_EQ(install().installed, 1);
  EXPECT_TRUE(exists("/.games/g/.pkg"));
  EXPECT_FALSE(exists("/.games-tmp"));
}

TEST_F(InstallerTest, HiddenAndSidecarNamesAreNotPackages) {
  drop("._g.cpgame", toBytes("AppleDouble, about 4 KB of resource fork on a real card"));
  drop(".hidden.cpgame", gamePackage("hidden"));
  drop(".cpgame", gamePackage("stemless"));
  EXPECT_FALSE(GamePackageInstaller::hasInbox());
  drop("g.cpgame", gamePackage("g"));
  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.installed, 1);
  EXPECT_EQ(report.failed, 0);
  EXPECT_TRUE(exists("/games/._g.cpgame"));
  EXPECT_FALSE(exists("/games/._g.cpgame.bad"));
  EXPECT_FALSE(exists("/.games/hidden"));
}

TEST_F(InstallerTest, AnInboxFileThatWillNotDeleteIsReportedAndKept) {
  drop("g.cpgame", gamePackage("g"));
  fakesd::sim().failRemove.insert("/games/g.cpgame");
  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.failed, 1);
  EXPECT_EQ(report.firstError, Error::SdCard);
  EXPECT_STREQ(report.firstFile, "g.cpgame");
  EXPECT_TRUE(exists("/.games/g/.pkg"));  // it did install
  EXPECT_TRUE(exists("/games/g.cpgame"));
}

TEST_F(InstallerTest, ABadPackageThatWillNotRenameIsReportedAsTheCardsFault) {
  drop("junk.cpgame", toBytes("not a zip, long enough to look for an end record in, honest"));
  fakesd::sim().failRename.insert("/games/junk.cpgame");
  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.firstError, Error::SdCard);
  EXPECT_TRUE(exists("/games/junk.cpgame"));
}

TEST_F(InstallerTest, AFailedPackageDoesNotBlockTheNextOneWithTheSameId) {
  drop("a.cpgame", gamePackage("g", {{"pic.png", toBytes("not a png at all, no header")}}));
  drop("b.cpgame", gamePackage("g"));
  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.failed, 1);
  EXPECT_EQ(report.installed, 1);
  EXPECT_STREQ(report.firstFile, "a.cpgame");
  EXPECT_TRUE(exists("/.games/g/.pkg"));
}

TEST_F(InstallerTest, ACloseThatFailsIsAStorageFailure) {
  for (const char* path : {"/.games-tmp/g/main.lua", "/.games-tmp/g/badge.bmp", "/.games-tmp/g/icon.bmp"}) {
    SetUp();
    drop("g.cpgame", gamePackage("g", {{"badge.png", solidPng(8, 8, 0)}, {"icon.png", solidPng(16, 16, 0)}}));
    fakesd::sim().failClose.insert(path);
    SCOPED_TRACE(path);
    EXPECT_EQ(install().firstError, Error::SdCard);
    EXPECT_TRUE(exists("/games/g.cpgame"));
    EXPECT_TRUE(childrenOf("/.games").empty());
  }
}

TEST_F(InstallerTest, ReadAndRemoveFailuresOnTheConvertedImageAreStorageFailures) {
  const Bytes package = gamePackage("g", {{"icon.png", solidPng(16, 16, 0)}});
  drop("g.cpgame", package);
  fakesd::sim().failReadAt["/.games-tmp/g/icon.bmp"] = 0;
  EXPECT_EQ(install().firstError, Error::SdCard);
  EXPECT_TRUE(exists("/games/g.cpgame"));

  SetUp();
  drop("g.cpgame", package);
  fakesd::sim().failRemove.insert("/.games-tmp/g/icon.png");
  EXPECT_EQ(install().firstError, Error::SdCard);
  EXPECT_TRUE(exists("/games/g.cpgame"));
}

TEST_F(InstallerTest, ANameTheSimulatorCutsIsSkippedToo) {
  fakesd::sim().getNameCuts = true;  // the simulator's getName cuts a long name to fit instead of returning 0
  drop(std::string(GamePaths::INBOX_NAME_BYTES, 'a') + ".cpgame", gamePackage("long-name"));
  EXPECT_FALSE(GamePackageInstaller::hasInbox());
  EXPECT_EQ(install().installed, 0);
}
