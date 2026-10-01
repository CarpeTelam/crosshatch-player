// GamePackageInstaller over the fake SD card, with the real ZipFile, PngToBmpConverter, and
// InflateStream: one package from the inbox to an installed game (entry 3 of
// epic-install-and-launcher). The zip reader's own hardening (limits, CRC, bombs) is entry 6.

#include <PngToBmpConverter.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <set>

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
  drop("package-vector.chgame", vector.package);
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
  EXPECT_FALSE(exists(INBOX + "package-vector.chgame"));
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
  drop("package-vector.chgame", loadVector().package);
  install();
  const int renamed = opIndex("rename /.games-tmp/package-vector /.games/package-vector");
  // The last open of the .pkg: the limit check looks for an installed one before the folder is moved (opens nothing).
  int pkg = -1;
  const auto& opsSoFar = fakesd::sim().ops;
  for (size_t i = 0; i < opsSoFar.size(); ++i) {
    if (opsSoFar[i] == "open /.games/package-vector/.pkg") pkg = static_cast<int>(i);
  }
  const int deleted = opIndex("remove /games/package-vector.chgame");
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
  drop("package-vector.chgame", loadVector().package);

  EXPECT_EQ(install().installed, 1);
  EXPECT_FALSE(exists("/.games/package-vector/old.lua"));
  EXPECT_EQ(toText(fakesd::bytesOf("/.games/package-vector/.pkg")), "v1\n" + loadVector().packageHash + "\n");
  EXPECT_EQ(toText(fakesd::bytesOf("/.games-data/package-vector/store.bin")), "saved");
  EXPECT_EQ(toText(fakesd::bytesOf("/.games-data/package-vector/resume.bin")), "resume");
}

TEST_F(InstallerTest, AFailedReinstallLeavesTheOldGameWhole) {
  drop("g.chgame", gamePackage("keeper"));
  ASSERT_EQ(install().installed, 1);
  const Bytes before = fakesd::bytesOf("/.games/keeper/.pkg");

  // Same id, but an image that cannot convert: rejected before the old folder is touched.
  const Bytes bad = gamePackage("keeper", {{"badge.png", toBytes("not a png")}});
  drop("g.chgame", bad);
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
  drop("with-images.chgame", makeZip(members));
  ASSERT_EQ(install().installed, 1);
  EXPECT_EQ(toText(fakesd::bytesOf("/.games/with-images/.pkg")), "v1\n" + hex(packageDigest(members), 8) + "\n");
}

// ---- manifest ------------------------------------------------------------------------------------

TEST_F(InstallerTest, AnUnavailablePackageInstallsAndTheRegistryListsIt) {
  drop("future.chgame", gamePackage("future", {}, manifestJson("future", "Future", 99)));
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
    drop("g.chgame", package);
    SCOPED_TRACE(c.what);
    expectRejected("g.chgame", package, Error::BadManifest);
    EXPECT_FALSE(HasFatalFailure());
  }
}

TEST_F(InstallerTest, APackageWithoutAManifestOrMainEndsBad) {
  const Bytes noManifest = makeZip({{"main.lua", toBytes("return {}\n")}});
  drop("a.chgame", noManifest);
  expectRejected("a.chgame", noManifest, Error::BadManifest);

  SetUp();
  const Bytes noMain = makeZip({{"manifest.json", toBytes(manifestJson("g"))}});
  drop("b.chgame", noMain);
  expectRejected("b.chgame", noMain, Error::NoMain);
}

// ---- members -------------------------------------------------------------------------------------

TEST_F(InstallerTest, AMemberOffTheWhitelistEndsBad) {
  // (".removing", the remove marker, and ".pkg", the commit marker, are not members either.)
  const char* names[] = {"readme.txt", "icon.bmp", "Main2.lua", "has space.lua", "sub/x.lua", "../evil.lua",
                         "sub/",       "up.PNG",   "x.lua.png", ".lua",          ".removing", ".pkg"};
  for (const char* name : names) {
    SetUp();
    const Bytes package = gamePackage("g", {{name, toBytes("x")}});
    drop("g.chgame", package);
    SCOPED_TRACE(name);
    expectRejected("g.chgame", package, Error::BadMember);
    EXPECT_FALSE(HasFatalFailure());
  }
}

TEST_F(InstallerTest, ALongMemberNameEndsBad) {
  const Bytes package = gamePackage("g", {{std::string(33, 'a') + ".lua", toBytes("x")}});
  drop("g.chgame", package);
  expectRejected("g.chgame", package, Error::BadMember);
}

TEST_F(InstallerTest, AMemberNamedAtTheLimitIsAllowed) {
  drop("g.chgame", gamePackage("g", {{std::string(32, 'a') + ".lua", toBytes("x")}}));
  EXPECT_EQ(install().installed, 1);
  EXPECT_TRUE(exists("/.games/g/" + std::string(32, 'a') + ".lua"));
}

TEST_F(InstallerTest, ADuplicateMemberEndsBad) {
  const Bytes package = gamePackage("g", {{"main.lua", toBytes("return 1\n")}});
  drop("g.chgame", package);
  expectRejected("g.chgame", package, Error::BadMember);
}

TEST_F(InstallerTest, MoreMembersThanTheLimitEndsBad) {
  std::vector<Member> extra;
  for (size_t i = 0; i + 2 < GameCore::PACKAGE_MEMBERS + 1; ++i)
    extra.push_back({"m" + std::to_string(i) + ".lua", {}});
  const Bytes package = gamePackage("g", extra);
  drop("g.chgame", package);
  expectRejected("g.chgame", package, Error::TooManyMembers);

  SetUp();  // exactly the limit installs
  extra.pop_back();
  drop("g.chgame", gamePackage("g", extra));
  EXPECT_EQ(install().installed, 1);
}

TEST_F(InstallerTest, NotAZipEndsBad) {
  const Bytes junk = toBytes("this is not a zip file at all, just text long enough to hold an end record");
  drop("junk.chgame", junk);
  expectRejected("junk.chgame", junk, Error::NotAPackage);

  SetUp();
  const Bytes tiny = toBytes("PK");
  drop("tiny.chgame", tiny);
  expectRejected("tiny.chgame", tiny, Error::NotAPackage);
}

// ---- images --------------------------------------------------------------------------------------

TEST_F(InstallerTest, ConvertsIconPngToA64x64IconBmpAndDropsThePng) {
  drop("g.chgame", gamePackage("g", {{"icon.png", solidPng(32, 32, 0)}}));
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
    drop("g.chgame", gamePackage("g", {{"icon.png", solidPng(side, side, 128)}}));
    SCOPED_TRACE(side);
    const GamePackageInstaller::Report report = install();
    ASSERT_EQ(report.installed, 1) << GamePackageInstaller::describe(report.firstError);
    const GameCore::ImageHeader header = headerOf("/.games/g/icon.bmp");
    EXPECT_EQ(header.width, 64u);
    EXPECT_EQ(header.height, 64u);
  }
}

// PngToBmpConverter sizes the icon as int(side * (64.0f / side)) in float32, which is 63 for 280 of the sides 1 to
// 2,048. The installer refuses a result that is not 64x64, and scripts/pack_game.py refuses the same sides up front
// (icon_scaled_side; pack_game_test.py checks these eight and the sides above): a package that packs installs.
TEST_F(InstallerTest, AnIconThatTheConverterScalesTo63IsBadImage) {
  for (const int side : {41, 47, 55, 61, 82, 83, 94, 97}) {
    SetUp();
    const Bytes package = gamePackage("g", {{"icon.png", solidPng(side, side, 128)}});
    drop("g.chgame", package);
    SCOPED_TRACE(side);
    expectRejected("g.chgame", package, Error::BadImage);
    EXPECT_FALSE(HasFatalFailure());
    EXPECT_TRUE(fakelog::any("icon.bmp came out 63x63, not 64x64"));
  }
}

// The packer's arithmetic against the converter's for every side (package_vectors.json's icon_scaling): the real
// converter is given a PNG of each square side 1 to 2,048 and asked for 64 x 64, as the installer asks. Its BMP header
// is written before any pixel is decoded, so a PNG whose image data is a stub (the converter then fails on the first
// row) is enough to read the size it chose. A side in the vector must come out at 63 and every other side at 64.
namespace {

struct HeaderSink final : Print {
  size_t write(const uint8_t byte) override {
    if (bytes.size() < 26) bytes.push_back(byte);
    return 1;
  }
  Bytes bytes;
};

Bytes headerOnlyPng(const int side) {
  Bytes png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  Bytes ihdr;
  putBE32(ihdr, static_cast<uint32_t>(side));
  putBE32(ihdr, static_cast<uint32_t>(side));
  for (const uint8_t byte : {8, 0, 0, 0, 0}) ihdr.push_back(byte);  // 8-bit grey, no interlace
  const auto chunk = [&png](const char* type, const Bytes& data) {
    putBE32(png, static_cast<uint32_t>(data.size()));
    png.insert(png.end(), type, type + 4);
    png.insert(png.end(), data.begin(), data.end());
    putBE32(png, 0);  // the converter does not read the CRC
  };
  chunk("IHDR", ihdr);
  chunk("IDAT", Bytes{0x78, 0x9C});  // a zlib header and nothing else
  return png;
}

std::set<int> iconSidesScaledTo63() {
  const std::string json = toText(readHostFile(std::string(PACKAGE_VECTOR_DIR) + "/package_vectors.json"));
  const size_t at = json.find("\"scaled_to_63\"");
  std::set<int> sides;
  if (at == std::string::npos) return sides;
  const size_t end = json.find(']', at);
  for (size_t i = json.find('[', at) + 1; i < end;) {
    if (json[i] >= '0' && json[i] <= '9') {
      size_t used = 0;
      sides.insert(std::stoi(json.substr(i), &used));
      i += used;
    } else {
      ++i;
    }
  }
  return sides;
}

}  // namespace

TEST_F(InstallerTest, TheConverterScalesEverySquareIconToExactly64ExceptTheSidesInTheVectors) {
  const std::set<int> to63 = iconSidesScaledTo63();
  ASSERT_EQ(to63.size(), 280u) << "the vectors' icon_scaling list";
  size_t at63 = 0;
  for (int side = 1; side <= 2048; ++side) {
    fakesd::addFile("/probe.png", headerOnlyPng(side));
    HalFile png;
    ASSERT_TRUE(Storage.openFileForRead("TEST", "/probe.png", png));
    HeaderSink sink;
    PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(png, sink, 64, 64);
    ASSERT_EQ(sink.bytes.size(), 26u) << "side " << side << ": the converter wrote no header";
    const auto le32 = [&sink](const size_t at) {
      return static_cast<int32_t>(sink.bytes[at] | sink.bytes[at + 1] << 8 | sink.bytes[at + 2] << 16 |
                                  static_cast<uint32_t>(sink.bytes[at + 3]) << 24);
    };
    const int expected = to63.count(side) != 0 ? 63 : 64;
    at63 += expected == 63;
    ASSERT_EQ(le32(18), expected) << "side " << side << " comes out this wide";
    ASSERT_EQ(std::abs(le32(22)), expected) << "side " << side << " comes out this high";
  }
  EXPECT_EQ(at63, 280u);
}

// Assumption for entry 14: a non-square icon.png is rejected, not scaled and cropped.
TEST_F(InstallerTest, ANonSquareIconEndsBadAndLeavesNothingBehind) {
  for (const auto& size : {std::pair<int, int>{100, 50}, {50, 100}, {64, 63}, {1, 2}}) {
    SetUp();
    const Bytes package = gamePackage("g", {{"icon.png", solidPng(size.first, size.second, 0)}});
    drop("g.chgame", package);
    SCOPED_TRACE(std::to_string(size.first) + "x" + std::to_string(size.second));
    expectRejected("g.chgame", package, Error::BadImage);
    EXPECT_FALSE(HasFatalFailure());
    EXPECT_TRUE(fakelog::any("the icon must be square"));
  }
}

TEST_F(InstallerTest, ConvertsAnImageToItsOwnSizeInTheLoadersLayout) {
  // Left half black, right half white, so the bit order shows.
  const Bytes png = makePng(20, 10, [](int x, int) { return x < 10 ? uint8_t{0} : uint8_t{255}; });
  drop("g.chgame", gamePackage("g", {{"badge.png", png}}));
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
    drop("g.chgame", package);
    SCOPED_TRACE(c.first);
    expectRejected("g.chgame", package, Error::BadImage);
    EXPECT_FALSE(HasFatalFailure());
  }
}

TEST_F(InstallerTest, ADamagedPngWithASoundHeaderEndsBadAndIsShownOnce) {
  Bytes png = solidPng(20, 20, 0);
  png.resize(png.size() / 2);  // the header is whole, the pixel data is cut
  const Bytes package = gamePackage("g", {{"pic.png", png}});
  drop("g.chgame", package);
  expectRejected("g.chgame", package, Error::BadImage);
  EXPECT_TRUE(fakelog::any("Cannot convert"));
  // Renamed, so the next visit does not judge it again.
  EXPECT_FALSE(GamePackageInstaller::hasInbox());
}

TEST_F(InstallerTest, ADamagedPngOnAFailingCardIsTheCardsFault) {
  // The card drops the converter's writes and then the decode fails: the short write decides.
  Bytes png = solidPng(20, 20, 0);
  png.resize(png.size() / 2);
  drop("g.chgame", gamePackage("g", {{"pic.png", png}}));
  fakesd::sim().failWrite.insert("/.games-tmp/g/pic.bmp");
  EXPECT_EQ(install().firstError, Error::SdCard);
  EXPECT_TRUE(exists("/games/g.chgame"));
  EXPECT_FALSE(exists("/games/g.chgame.bad"));
}

TEST_F(InstallerTest, AnImageWrittenShortIsTheCardsFaultNotThePackages) {
  // The converter ignores a failed write, so the output is just short.
  drop("g.chgame", gamePackage("g", {{"badge.png", solidPng(20, 10, 255)}}));
  fakesd::sim().failWrite.insert("/.games-tmp/g/badge.bmp");
  EXPECT_EQ(install().firstError, Error::SdCard);
  EXPECT_TRUE(exists("/games/g.chgame"));
  EXPECT_FALSE(exists("/games/g.chgame.bad"));
  fakesd::sim().failWrite.clear();
  EXPECT_EQ(install().installed, 1);
}

// ---- the inbox -----------------------------------------------------------------------------------

TEST_F(InstallerTest, OnlyChgameFilesAreInstalled) {
  drop("real.chgame", gamePackage("real"));
  drop("UPPER.CHGAME", gamePackage("upper"));
  drop("old.chgame.bad", toBytes("x"));
  drop("notes.txt", toBytes("x"));
  drop("chgame", toBytes("x"));
  fakesd::addDir("/games/folder.chgame");
  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.installed, 2);
  EXPECT_EQ(report.failed, 0);
  EXPECT_TRUE(exists("/.games/real/.pkg"));
  EXPECT_TRUE(exists("/.games/upper/.pkg"));
  EXPECT_TRUE(exists("/games/old.chgame.bad"));
  EXPECT_TRUE(exists("/games/notes.txt"));
  EXPECT_TRUE(exists("/games/folder.chgame"));
}

// The extension was .cpgame before it became .chgame; none shipped, so the installer takes no .cpgame: the file is not
// installed, not set aside as .bad, and not counted as an inbox file.
TEST_F(InstallerTest, ACpgameFileIsIgnored) {
  const Bytes package = gamePackage("old");
  drop("old.cpgame", package);
  drop("OLD2.CPGAME", gamePackage("old2"));
  EXPECT_FALSE(GamePackageInstaller::hasInbox());
  drop("new.chgame", gamePackage("new"));
  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.installed, 1);
  EXPECT_EQ(report.failed, 0);
  EXPECT_TRUE(exists("/.games/new/.pkg"));
  EXPECT_FALSE(exists("/.games/old"));
  EXPECT_FALSE(exists("/.games/old2"));
  ASSERT_TRUE(exists("/games/old.cpgame"));
  EXPECT_EQ(fakesd::bytesOf("/games/old.cpgame"), package);
  EXPECT_TRUE(exists("/games/OLD2.CPGAME"));
  EXPECT_FALSE(exists("/games/old.cpgame.bad"));
  EXPECT_FALSE(exists("/games/old.chgame.bad"));
  EXPECT_FALSE(exists("/games/old.cpgame.installed"));
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
  drop("a-good.chgame", gamePackage("good-a"));
  const Bytes bad = toBytes("not a zip, long enough to look for an end record in, honest");
  drop("b-bad.chgame", bad);
  drop("c-good.chgame", gamePackage("good-c"));
  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.installed, 2);
  EXPECT_EQ(report.failed, 1);
  EXPECT_STREQ(report.firstFile, "b-bad.chgame");
  EXPECT_TRUE(exists("/.games/good-a/.pkg"));
  EXPECT_TRUE(exists("/.games/good-c/.pkg"));
  EXPECT_EQ(fakesd::bytesOf("/games/b-bad.chgame.bad"), bad);
}

TEST_F(InstallerTest, ANewBadFileReplacesAnEarlierBadOfTheSameName) {
  drop("g.chgame.bad", toBytes("earlier"));
  const Bytes bad = toBytes("not a zip, long enough to look for an end record in, honest");
  drop("g.chgame", bad);
  EXPECT_EQ(install().failed, 1);
  EXPECT_EQ(fakesd::bytesOf("/games/g.chgame.bad"), bad);
}

TEST_F(InstallerTest, ALeftoverScratchFolderIsDeletedEvenWithNothingToInstall) {
  fakesd::addFile("/.games-tmp/stale/main.lua", std::string("half"));
  fakesd::addFile("/.games-tmp/other/icon.png", std::string("half"));
  EXPECT_EQ(install().installed, 0);
  EXPECT_FALSE(exists("/.games-tmp"));
}

TEST_F(InstallerTest, ALeftoverScratchFolderOfTheSameIdDoesNotBlockTheInstall) {
  fakesd::addFile("/.games-tmp/package-vector/main.lua", std::string("half"));
  drop("package-vector.chgame", loadVector().package);
  EXPECT_EQ(install().installed, 1);
  EXPECT_EQ(toText(fakesd::bytesOf("/.games/package-vector/util.lua")), "return {}\n");
}

TEST_F(InstallerTest, TakesAtMostMaxPerRunAndTheRestOnTheNextCall) {
  const Bytes package = gamePackage("same");
  for (size_t i = 0; i < GamePackageInstaller::MAX_PER_RUN + 1; ++i) {
    char name[24];
    std::snprintf(name, sizeof(name), "p%02zu.chgame", i);
    drop(name, package);
  }
  EXPECT_EQ(install().installed, GamePackageInstaller::MAX_PER_RUN);
  EXPECT_TRUE(GamePackageInstaller::hasInbox());
  EXPECT_EQ(install().installed, 1);
  EXPECT_FALSE(GamePackageInstaller::hasInbox());
}

TEST_F(InstallerTest, ANameTooLongForTheInboxIsSkipped) {
  drop(std::string(GamePaths::INBOX_NAME_BYTES, 'a') + ".chgame", gamePackage("long-name"));
  EXPECT_FALSE(GamePackageInstaller::hasInbox());
  EXPECT_EQ(install().installed, 0);
}

// ---- storage failures ----------------------------------------------------------------------------

TEST_F(InstallerTest, AStorageFailureKeepsTheFileForTheNextTry) {
  drop("g.chgame", gamePackage("g"));
  fakesd::sim().failRename.insert("/.games-tmp/g");
  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.installed, 0);
  EXPECT_EQ(report.failed, 1);
  EXPECT_EQ(report.firstError, Error::SdCard);
  EXPECT_TRUE(exists("/games/g.chgame"));
  EXPECT_FALSE(exists("/games/g.chgame.bad"));
  EXPECT_FALSE(exists("/.games-tmp"));
  EXPECT_TRUE(childrenOf("/.games").empty());

  fakesd::sim().failRename.clear();  // the card recovers
  EXPECT_EQ(install().installed, 1);
  EXPECT_FALSE(exists("/games/g.chgame"));
}

TEST_F(InstallerTest, AFailedMemberWriteIsAStorageFailure) {
  drop("g.chgame", gamePackage("g"));
  fakesd::sim().failWrite.insert("/.games-tmp/g/main.lua");
  EXPECT_EQ(install().firstError, Error::SdCard);
  EXPECT_TRUE(exists("/games/g.chgame"));
  EXPECT_FALSE(exists("/.games-tmp"));
}

TEST_F(InstallerTest, AFailedPkgWriteLeavesNoGameInTheRegistry) {
  drop("g.chgame", gamePackage("g"));
  fakesd::sim().failWrite.insert("/.games/g/.pkg");
  EXPECT_EQ(install().firstError, Error::SdCard);
  GameRegistry::Listing listing;
  ASSERT_TRUE(GameRegistry::load(listing));
  EXPECT_EQ(listing.count, 0u);
  EXPECT_TRUE(exists("/games/g.chgame"));  // retried next time, which replaces the folder
  fakesd::sim().failWrite.clear();
  EXPECT_EQ(install().installed, 1);
}

TEST_F(InstallerTest, ACardFailureInTheLastStepOfAReinstallIsRetried) {
  drop("g.chgame", gamePackage("g"));
  ASSERT_EQ(install().installed, 1);

  // The new package extracts fine, then the rename into place fails: the old folder is already
  // gone (the game is unlisted), and the file stays so the next visit installs it again.
  const Bytes update = gamePackage("g", {{"extra.lua", toBytes("return 2\n")}});
  drop("g.chgame", update);
  fakesd::sim().failRename.insert("/.games-tmp/g");
  EXPECT_EQ(install().firstError, Error::SdCard);
  EXPECT_TRUE(exists("/games/g.chgame"));
  EXPECT_FALSE(exists("/games/g.chgame.bad"));
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
  const char* fixtures[] = {"counter",   "gallery",      "icons", "limits", "loop",
                            "pass-open", "slow-restart", "timer", "timing", "tracer"};
  for (const char* fixture : fixtures) {
    const std::string dir = std::string(GAME_FIXTURES_DIR) + "/" + fixture;
    std::vector<Member> members;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
      members.push_back({entry.path().filename().string(), readHostFile(entry.path().string())});
    }
    drop(std::string(fixture) + ".chgame", makeZip(members));
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
  drop("g.chgame", gamePackage("g"));
  ASSERT_EQ(install().installed, 1);
  drop("g.chgame", gamePackage("g", {{"extra.lua", toBytes("return 2\n")}}));

  // Removing the old folder stops partway: the marker is gone, so nothing half-deleted is listed.
  fakesd::sim().failRemove.insert("/.games/g/manifest.json");
  EXPECT_EQ(install().firstError, Error::SdCard);
  EXPECT_FALSE(exists("/.games/g/.pkg"));
  GameRegistry::Listing listing;
  ASSERT_TRUE(GameRegistry::load(listing));
  EXPECT_EQ(listing.count, 0u);
  EXPECT_TRUE(exists("/games/g.chgame"));
  EXPECT_FALSE(exists("/.games-tmp"));  // the new files were ours alone, so they are cleaned up

  fakesd::sim().failRemove.clear();
  EXPECT_EQ(install().installed, 1);
  EXPECT_TRUE(exists("/.games/g/extra.lua"));
}

TEST_F(InstallerTest, AMarkerThatWillNotGoLeavesTheOldGameWhole) {
  drop("g.chgame", gamePackage("g"));
  ASSERT_EQ(install().installed, 1);
  drop("g.chgame", gamePackage("g", {{"extra.lua", toBytes("return 2\n")}}));
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
  drop("g.chgame", gamePackage("g"));

  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.installed, 0);
  EXPECT_EQ(report.firstError, Error::SdCard);  // mkdir of /.games-tmp/g fails: a clear, repeated report
  EXPECT_TRUE(fakelog::any("Keeping /.games-tmp/g"));
  EXPECT_EQ(toText(fakesd::bytesOf("/.games-tmp/g/main.lua")), "scratch");
  EXPECT_EQ(toText(fakesd::bytesOf("/.games/g/main.lua")), "final");
  EXPECT_FALSE(exists("/.games-tmp/g/.xlink"));  // the probe file is removed either way
  EXPECT_TRUE(exists("/games/g.chgame"));        // kept, not renamed .bad
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
  drop("g.chgame", gamePackage("g"));
  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.installed, 1);
  EXPECT_EQ(report.failed, 0);
  EXPECT_TRUE(exists("/.games/g/.pkg"));
  EXPECT_FALSE(exists("/.games-tmp"));
  EXPECT_FALSE(fakelog::any("Keeping"));
}

// The probe's order on the install path (mayRemoveTmp): its file is made and closed in /.games-tmp/g, then looked for
// in /.games/g, then removed. Looking after the remove would never see it, and a shared cluster chain would be taken
// for two folders. (Only one probe runs on this card: the install that follows finds no scratch left.)
TEST_F(InstallerTest, TheProbeLooksForItsFileBeforeItRemovesIt) {
  fakesd::addFile("/.games-tmp/g/main.lua", std::string("scratch"));
  fakesd::addFile("/.games/g/manifest.json", std::string("{}"));
  drop("g.chgame", gamePackage("g"));
  fakesd::sim().ops.clear();
  ASSERT_EQ(install().installed, 1);
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

TEST_F(InstallerTest, ScratchIsKeptWhenTheProbeFileCannotBeMadeOrRemoved) {
  for (const bool cannotMake : {true, false}) {
    SetUp();
    fakesd::addFile("/.games-tmp/g/main.lua", std::string("scratch"));
    fakesd::addFile("/.games/g/main.lua", std::string("final"));
    drop("g.chgame", gamePackage("g"));
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
  drop("g.chgame", gamePackage("g"));
  EXPECT_EQ(install().installed, 1);
  EXPECT_TRUE(exists("/.games/g/.pkg"));
  EXPECT_FALSE(exists("/.games-tmp"));
}

TEST_F(InstallerTest, HiddenAndSidecarNamesAreNotPackages) {
  drop("._g.chgame", toBytes("AppleDouble, about 4 KB of resource fork on a real card"));
  drop(".hidden.chgame", gamePackage("hidden"));
  drop(".chgame", gamePackage("stemless"));
  EXPECT_FALSE(GamePackageInstaller::hasInbox());
  drop("g.chgame", gamePackage("g"));
  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.installed, 1);
  EXPECT_EQ(report.failed, 0);
  EXPECT_TRUE(exists("/games/._g.chgame"));
  EXPECT_FALSE(exists("/games/._g.chgame.bad"));
  EXPECT_FALSE(exists("/.games/hidden"));
}

// The game is installed, so a file that stayed in the inbox would install again on every visit and undo a Remove: it
// moves out of the inbox instead.
TEST_F(InstallerTest, AnInstalledInboxFileThatWillNotDeleteIsRenamedOutOfTheInbox) {
  const Bytes package = gamePackage("g");
  drop("g.chgame", package);
  fakesd::sim().failRemove.insert("/games/g.chgame");
  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.installed, 1);
  EXPECT_EQ(report.failed, 0);
  EXPECT_EQ(report.firstError, Error::None);
  EXPECT_TRUE(exists("/.games/g/.pkg"));
  EXPECT_FALSE(exists("/games/g.chgame"));
  ASSERT_TRUE(exists("/games/g.chgame.installed"));
  EXPECT_EQ(fakesd::bytesOf("/games/g.chgame.installed"), package);
  EXPECT_FALSE(GamePackageInstaller::hasInbox()) << "the renamed file is not an inbox file";

  // The next visit installs nothing, so a Remove of the game between visits stays removed.
  EXPECT_EQ(GamePackageInstaller::remove("g"), Error::None);
  const GamePackageInstaller::Report again = install();
  EXPECT_EQ(again.installed, 0);
  EXPECT_EQ(again.failed, 0);
  EXPECT_FALSE(exists("/.games/g"));
}

TEST_F(InstallerTest, AnEarlierInstalledCopyIsReplacedByTheNextOne) {
  const Bytes second = gamePackage("g", {{"extra.lua", toBytes("return 2\n")}});
  drop("g.chgame", gamePackage("g"));
  fakesd::sim().failRemove.insert("/games/g.chgame");
  ASSERT_EQ(install().installed, 1);
  drop("g.chgame", second);
  EXPECT_EQ(install().installed, 1);
  EXPECT_EQ(fakesd::bytesOf("/games/g.chgame.installed"), second);
  EXPECT_FALSE(exists("/games/g.chgame"));
}

// A file the card marks read-only keeps the mark when it is renamed, and will not delete: an earlier copy under the
// aside name that will not go must not keep an update of the same file in the inbox (the fake card's failRemove stands
// in for the mark, and its rename, like SdFat's, will not replace a name).
TEST_F(InstallerTest, AnEarlierInstalledCopyThatWillNotGoLeavesTheNextNameFree) {
  const Bytes update = gamePackage("g", {{"extra.lua", toBytes("return 2\n")}});
  drop("g.chgame", update);
  fakesd::addFile("/games/g.chgame.installed", toBytes("an earlier copy"));
  fakesd::sim().failRemove.insert("/games/g.chgame");
  fakesd::sim().failRemove.insert("/games/g.chgame.installed");
  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.installed, 1);
  EXPECT_EQ(report.failed, 0);
  EXPECT_FALSE(exists("/games/g.chgame"));
  EXPECT_EQ(toText(fakesd::bytesOf("/games/g.chgame.installed")), "an earlier copy");
  EXPECT_EQ(fakesd::bytesOf("/games/g.chgame.installed.2"), update);
  EXPECT_TRUE(fakelog::any("Cannot remove /games/g.chgame.installed"));
  EXPECT_FALSE(GamePackageInstaller::hasInbox()) << "no name it takes ends in .chgame";
  EXPECT_EQ(install().installed, 0) << "and nothing installs again";
}

TEST_F(InstallerTest, AnEarlierBadCopyThatWillNotGoLeavesTheNextNameFree) {
  const Bytes package = gamePackage("g", {{"pic.png", toBytes("not a png at all, no header")}});
  drop("g.chgame", package);
  fakesd::addFile("/games/g.chgame.bad", toBytes("an earlier verdict"));
  fakesd::sim().failRemove.insert("/games/g.chgame.bad");
  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.firstError, Error::BadImage) << "the package's reason, not the card's";
  EXPECT_FALSE(exists("/games/g.chgame"));
  EXPECT_EQ(fakesd::bytesOf("/games/g.chgame.bad.2"), package);
  EXPECT_EQ(toText(fakesd::bytesOf("/games/g.chgame.bad")), "an earlier verdict");
}

TEST_F(InstallerTest, WhenEveryAsideNameIsTakenAndStuckTheFileIsReportedAndKept) {
  drop("g.chgame", gamePackage("g"));
  fakesd::sim().failRemove.insert("/games/g.chgame");
  for (const char* name : {"", ".2", ".3", ".4", ".5"}) {
    const std::string path = std::string("/games/g.chgame.installed") + name;
    fakesd::addFile(path, toBytes("stuck"));
    fakesd::sim().failRemove.insert(path);
  }
  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.failed, 1);
  EXPECT_EQ(report.firstError, Error::SdCard);
  EXPECT_TRUE(exists("/.games/g/.pkg")) << "it did install";
  EXPECT_TRUE(exists("/games/g.chgame"));
  EXPECT_FALSE(exists("/games/g.chgame.installed.6"));
  EXPECT_TRUE(fakelog::any("No free name for /games/g.chgame.installed"));
}

TEST_F(InstallerTest, AnInstalledInboxFileThatWillNeitherDeleteNorRenameIsReportedAndKept) {
  drop("g.chgame", gamePackage("g"));
  fakesd::sim().failRemove.insert("/games/g.chgame");
  fakesd::sim().failRename.insert("/games/g.chgame");
  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.failed, 1);
  EXPECT_EQ(report.firstError, Error::SdCard);
  EXPECT_STREQ(report.firstFile, "g.chgame");
  EXPECT_TRUE(exists("/.games/g/.pkg"));  // it did install
  EXPECT_TRUE(exists("/games/g.chgame"));
  EXPECT_FALSE(exists("/games/g.chgame.installed"));
  EXPECT_TRUE(fakelog::any("Cannot rename /games/g.chgame to /games/g.chgame.installed"));
}

// ---- the 65th game ------------------------------------------------------------------------------------

// GameRegistry lists at most MAX_GAMES games, in directory order, so a game installed past them would be on the card
// and not on the list, with no way to remove it from the launcher.
namespace {

class GameLimitTest : public InstallerTest {
 protected:
  static std::string idOf(const size_t n) {
    char id[16];
    std::snprintf(id, sizeof(id), "game-%02u", static_cast<unsigned>(n));
    return id;
  }
  // `count` installed games, game-00 upward: a .pkg and the manifest the registry needs to list them.
  static void installedGames(const size_t count) {
    for (size_t i = 0; i < count; ++i) {
      fakesd::addFile("/.games/" + idOf(i) + "/.pkg", std::string("v1\n0000000000000000\n"));
      fakesd::addFile("/.games/" + idOf(i) + "/manifest.json", manifestJson(idOf(i)));
    }
  }
};

}  // namespace

TEST_F(GameLimitTest, TheLimitIsTheRegistrysAndAllOfItsGamesAreListed) {
  ASSERT_EQ(GameRegistry::MAX_GAMES, 64u);
  installedGames(GameRegistry::MAX_GAMES - 1);
  drop("last.chgame", gamePackage("last"));
  EXPECT_EQ(install().installed, 1) << "the 64th game installs";
  GameRegistry::Listing listing;
  ASSERT_TRUE(GameRegistry::load(listing));
  EXPECT_EQ(listing.count, GameRegistry::MAX_GAMES) << "the registry lists every one of them";
  bool listed = false;
  for (size_t i = 0; i < listing.count; ++i) listed = listed || std::string(listing.entries[i].manifest.id) == "last";
  EXPECT_TRUE(listed);
}

TEST_F(GameLimitTest, A65thGameIsRefusedAndItsFileWaitsInTheInbox) {
  installedGames(GameRegistry::MAX_GAMES);
  const Bytes package = gamePackage("extra");
  drop("extra.chgame", package);
  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.installed, 0);
  EXPECT_EQ(report.failed, 1);
  EXPECT_EQ(report.firstError, Error::TooManyGames);
  EXPECT_STREQ(report.firstFile, "extra.chgame");
  // Not the package's fault: it is not renamed .bad, and nothing of it is on the card but the file.
  EXPECT_EQ(fakesd::bytesOf("/games/extra.chgame"), package);
  EXPECT_FALSE(exists("/games/extra.chgame.bad"));
  EXPECT_FALSE(exists("/.games/extra"));
  EXPECT_FALSE(exists("/.games-tmp"));
  EXPECT_EQ(fakesd::countOps("mkdir /.games-tmp"), 0u);
  EXPECT_TRUE(fakelog::any("Not installing extra: 64 games are installed already"));

  // It tries again on the next visit, and installs once a game has been removed.
  EXPECT_EQ(install().firstError, Error::TooManyGames);
  ASSERT_EQ(GamePackageInstaller::remove("game-07"), Error::None);
  EXPECT_EQ(install().installed, 1);
  EXPECT_TRUE(exists("/.games/extra/.pkg"));
  EXPECT_FALSE(exists("/games/extra.chgame"));
}

// The limit is judged right after the manifest, before anything is written, so a package that waits costs a read of its
// directory and manifest on every visit and no extraction. An invalid one at the limit therefore says "too many" until
// a game is removed, and then gets its own reason.
TEST_F(GameLimitTest, AnInvalidPackageAtTheLimitWaitsLikeAnyOtherAndIsBadOnceThereIsRoom) {
  installedGames(GameRegistry::MAX_GAMES);
  const Bytes package = gamePackage("extra", {{"pic.png", toBytes("not a png at all, no header")}});
  drop("extra.chgame", package);
  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.firstError, Error::TooManyGames);
  EXPECT_EQ(fakesd::bytesOf("/games/extra.chgame"), package);
  EXPECT_FALSE(exists("/games/extra.chgame.bad"));

  ASSERT_EQ(GamePackageInstaller::remove("game-07"), Error::None);
  const GamePackageInstaller::Report later = install();
  EXPECT_EQ(later.firstError, Error::BadImage) << GamePackageInstaller::describe(later.firstError);
  EXPECT_FALSE(exists("/games/extra.chgame"));
  EXPECT_EQ(fakesd::bytesOf("/games/extra.chgame.bad"), package);
  EXPECT_FALSE(exists("/.games/extra"));
}

// Nothing of a package that waits is written to the card: no scratch folder, no extracted member, no converted image.
TEST_F(GameLimitTest, APackageThatWaitsForRoomWritesNothing) {
  installedGames(GameRegistry::MAX_GAMES);
  drop("extra.chgame", gamePackage("extra", {{"icon.png", solidPng(16, 16, 0)}}));
  const std::vector<std::string> before = fakesd::sim().ops;
  EXPECT_EQ(install().firstError, Error::TooManyGames);
  for (size_t i = before.size(); i < fakesd::sim().ops.size(); ++i) {
    const std::string& op = fakesd::sim().ops[i];
    for (const char* verb : {"mkdir ", "write ", "rename ", "remove "}) {
      EXPECT_NE(op.rfind(verb, 0), 0u) << "the card was asked to: " << op;
    }
    EXPECT_EQ(op.find("/.games-tmp/"), std::string::npos) << op;
  }
  EXPECT_FALSE(exists("/.games-tmp"));
}

// A commit that fails after the folder moved can leave a valid .pkg (here its close fails, after the bytes are
// written): a game the count did not know. The next package counts again, so the 65th is refused.
TEST_F(GameLimitTest, AGameALateFailedCommitLeftBehindIsCountedForTheNextPackage) {
  installedGames(GameRegistry::MAX_GAMES - 1);
  drop("a.chgame", gamePackage("a"));
  drop("b.chgame", gamePackage("b"));
  fakesd::sim().failClose.insert("/.games/a/.pkg");
  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.installed, 0);
  EXPECT_EQ(report.failed, 2);
  EXPECT_STREQ(report.firstFile, "a.chgame");
  EXPECT_EQ(report.firstError, Error::SdCard);
  EXPECT_TRUE(exists("/.games/a/.pkg")) << "a is installed all the same";
  EXPECT_TRUE(exists("/games/b.chgame"));
  EXPECT_FALSE(exists("/.games/b"));
  EXPECT_TRUE(fakelog::any("Not installing b:"));
  GameRegistry::Listing listing;
  ASSERT_TRUE(GameRegistry::load(listing));
  EXPECT_EQ(listing.count, GameRegistry::MAX_GAMES) << "63 and a: every installed game is listed";
}

TEST_F(GameLimitTest, ThePackagesBehindManyThatWaitForRoomAreStillReached) {
  installedGames(GameRegistry::MAX_GAMES);
  constexpr int WAITING = 40;  // more than MAX_PER_RUN
  for (int i = 0; i < WAITING; ++i) {
    char name[16];
    std::snprintf(name, sizeof(name), "wait-%02d", i);
    drop(std::string(name) + ".chgame", gamePackage(name));
  }
  drop("update.chgame", gamePackage("game-33", {{"extra.lua", toBytes("return 2\n")}}));  // last in the inbox
  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.installed, 1) << "the update of an installed game is reached behind " << WAITING;
  EXPECT_EQ(report.failed, WAITING);
  EXPECT_EQ(report.firstError, Error::TooManyGames);
  EXPECT_TRUE(exists("/.games/game-33/extra.lua"));
  EXPECT_FALSE(exists("/games/update.chgame"));
  for (int i = 0; i < WAITING; ++i) {
    char name[32];
    std::snprintf(name, sizeof(name), "/games/wait-%02d.chgame", i);
    EXPECT_TRUE(exists(name)) << name << " waits";
  }
}

TEST_F(GameLimitTest, TheFoldersAreCountedOnceACallAndKeptAsInstallsLand) {
  installedGames(GameRegistry::MAX_GAMES - 3);
  for (const char* id : {"a", "b", "c", "d"}) drop(std::string(id) + ".chgame", gamePackage(id));
  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.installed, 3);
  EXPECT_EQ(report.failed, 1) << "the count went up with each install, so the fourth was over the limit";
  EXPECT_STREQ(report.firstFile, "d.chgame");
  // One read of each folder's .pkg for the count, not one more for every package.
  EXPECT_EQ(fakesd::countOps("open /.games/game-05/.pkg"), 1u);
}

TEST_F(GameLimitTest, ReplacingAnInstalledGameIsAllowedAtTheLimit) {
  installedGames(GameRegistry::MAX_GAMES);
  drop("game-33.chgame", gamePackage("game-33", {{"extra.lua", toBytes("return 2\n")}}));
  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.installed, 1) << GamePackageInstaller::describe(report.firstError);
  EXPECT_TRUE(exists("/.games/game-33/extra.lua"));
}

TEST_F(GameLimitTest, OnlyFoldersWithAMarkerCount) {
  installedGames(GameRegistry::MAX_GAMES - 1);
  // Not games: a folder with no valid .pkg (an unfinished install, a removal that stopped, or a marker that does not
  // parse), a dot folder, and a file.
  fakesd::addFile("/.games/unmarked/manifest.json", std::string("{}"));
  fakesd::addFile("/.games/.hidden/.pkg", std::string("x"));
  fakesd::addFile("/.games/stray.txt", std::string("x"));
  fakesd::addFile("/.games/broken/.pkg", std::string("not a package marker"));  // the registry skips it too
  drop("last.chgame", gamePackage("last"));
  EXPECT_EQ(install().installed, 1);
}

TEST_F(GameLimitTest, OnlyFoldersTheRegistryListsCount) {
  installedGames(GameRegistry::MAX_GAMES - 1);
  // A valid .pkg, but a manifest the registry skips: missing, invalid, or naming another id.
  const std::string pkg("v1\n0000000000000000\n");
  fakesd::addFile("/.games/no-manifest/.pkg", pkg);
  fakesd::addFile("/.games/bad-manifest/.pkg", pkg);
  fakesd::addFile("/.games/bad-manifest/manifest.json", std::string("{"));
  fakesd::addFile("/.games/other-id/.pkg", pkg);
  fakesd::addFile("/.games/other-id/manifest.json", manifestJson("game-00"));
  drop("last.chgame", gamePackage("last"));
  EXPECT_EQ(install().installed, 1) << "63 games are listed, so the 64th installs";
  GameRegistry::Listing listing;
  ASSERT_TRUE(GameRegistry::load(listing));
  EXPECT_EQ(listing.count, GameRegistry::MAX_GAMES);
}

TEST_F(GameLimitTest, APackageIntoAFolderTheRegistrySkipsAddsAGame) {
  installedGames(GameRegistry::MAX_GAMES - 1);
  fakesd::addFile("/.games/skipped/.pkg", std::string("v1\n0000000000000000\n"));  // no manifest: not listed
  drop("new.chgame", gamePackage("new"));
  drop("skipped.chgame", gamePackage("skipped"));
  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.installed, 1) << "whichever lands first is the 64th; the other is a 65th, not a replacement";
  EXPECT_EQ(report.firstError, Error::TooManyGames);
  GameRegistry::Listing listing;
  ASSERT_TRUE(GameRegistry::load(listing));
  EXPECT_EQ(listing.count, GameRegistry::MAX_GAMES);
}

TEST_F(GameLimitTest, TheOthersInTheInboxStillInstallWhenOneIsRefused) {
  installedGames(GameRegistry::MAX_GAMES - 1);
  drop("a.chgame", gamePackage("a"));
  drop("b.chgame", gamePackage("b"));
  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.installed, 1);
  EXPECT_EQ(report.failed, 1);
  EXPECT_EQ(report.firstError, Error::TooManyGames);
  EXPECT_STREQ(report.firstFile, "b.chgame");
  EXPECT_TRUE(exists("/.games/a/.pkg"));
  EXPECT_TRUE(exists("/games/b.chgame"));
}

TEST_F(InstallerTest, ABadPackageThatWillNotRenameIsReportedAsTheCardsFault) {
  drop("junk.chgame", toBytes("not a zip, long enough to look for an end record in, honest"));
  fakesd::sim().failRename.insert("/games/junk.chgame");
  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.firstError, Error::SdCard);
  EXPECT_TRUE(exists("/games/junk.chgame"));
}

TEST_F(InstallerTest, AFailedPackageDoesNotBlockTheNextOneWithTheSameId) {
  drop("a.chgame", gamePackage("g", {{"pic.png", toBytes("not a png at all, no header")}}));
  drop("b.chgame", gamePackage("g"));
  const GamePackageInstaller::Report report = install();
  EXPECT_EQ(report.failed, 1);
  EXPECT_EQ(report.installed, 1);
  EXPECT_STREQ(report.firstFile, "a.chgame");
  EXPECT_TRUE(exists("/.games/g/.pkg"));
}

TEST_F(InstallerTest, ACloseThatFailsIsAStorageFailure) {
  for (const char* path : {"/.games-tmp/g/main.lua", "/.games-tmp/g/badge.bmp", "/.games-tmp/g/icon.bmp"}) {
    SetUp();
    drop("g.chgame", gamePackage("g", {{"badge.png", solidPng(8, 8, 0)}, {"icon.png", solidPng(16, 16, 0)}}));
    fakesd::sim().failClose.insert(path);
    SCOPED_TRACE(path);
    EXPECT_EQ(install().firstError, Error::SdCard);
    EXPECT_TRUE(exists("/games/g.chgame"));
    EXPECT_TRUE(childrenOf("/.games").empty());
  }
}

TEST_F(InstallerTest, ReadAndRemoveFailuresOnTheConvertedImageAreStorageFailures) {
  const Bytes package = gamePackage("g", {{"icon.png", solidPng(16, 16, 0)}});
  drop("g.chgame", package);
  fakesd::sim().failReadAt["/.games-tmp/g/icon.bmp"] = 0;
  EXPECT_EQ(install().firstError, Error::SdCard);
  EXPECT_TRUE(exists("/games/g.chgame"));

  SetUp();
  drop("g.chgame", package);
  fakesd::sim().failRemove.insert("/.games-tmp/g/icon.png");
  EXPECT_EQ(install().firstError, Error::SdCard);
  EXPECT_TRUE(exists("/games/g.chgame"));
}

TEST_F(InstallerTest, ANameTheSimulatorCutsIsSkippedToo) {
  fakesd::sim().getNameCuts = true;  // the simulator's getName cuts a long name to fit instead of returning 0
  drop(std::string(GamePaths::INBOX_NAME_BYTES, 'a') + ".chgame", gamePackage("long-name"));
  EXPECT_FALSE(GamePackageInstaller::hasInbox());
  EXPECT_EQ(install().installed, 0);
}
