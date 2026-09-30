// The packer's live output through the C++ installer. The host build runs scripts/pack_game.py on fixture folders
// (packed_fixtures.cmake) and this suite installs each package with the real GamePackageInstaller over the fake card
// and lists it through GameRegistry. The installer's other suites build their packages themselves (a stored zip of the
// members, or the crafted ones of gen_hardening_packages.py), and the one committed vector has no image or icon, so
// without this a change to the packer's framing, image handling, or hash would ship with every suite green.

#include <gtest/gtest.h>

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include "GameImages.h"
#include "GamePackageInstaller.h"
#include "GameRegistry.h"
#include "HalDisplay.h"
#include "InstallerSupport.h"
#include "PackageLimits.h"

HalDisplay display;

using namespace installer_test;

namespace {

std::vector<std::string> fixtureIds() {
  std::vector<std::string> ids;
  std::stringstream stream(PACKED_FIXTURE_IDS);
  for (std::string id; std::getline(stream, id, ',');) ids.push_back(id);
  return ids;
}

class PackedFixtureBase : public ::testing::Test {
 protected:
  void SetUp() override {
    fakesd::reset();
    fakelog::lines.clear();
  }

  // The bytes of a file the build wrote into the packed-fixtures folder.
  static Bytes packed(const std::string& name) { return readHostFile(std::string(PACKED_FIXTURES_DIR) + "/" + name); }

  // The header of a converted image on the fake card, which the game loader would accept.
  static GameCore::ImageHeader headerOf(const std::string& path) {
    const Bytes file = fakesd::bytesOf(path);
    GameCore::ImageHeader header;
    EXPECT_EQ(
        GameCore::checkImageHeader(file.data(), std::min<size_t>(file.size(), 62), file.size(), file.size(), header),
        GameCore::ImageCheck::Ok)
        << path;
    return header;
  }
};

class PackedFixtureTest : public PackedFixtureBase, public ::testing::WithParamInterface<std::string> {};

}  // namespace

TEST_P(PackedFixtureTest, ThePackersPackageInstallsAndIsListedWithItsOwnHash) {
  const std::string id = GetParam();
  const Bytes package = packed(id + ".chgame");
  ASSERT_FALSE(package.empty());
  std::string packerHash = toText(packed(id + ".hash"));
  while (!packerHash.empty() && (packerHash.back() == '\n' || packerHash.back() == '\r')) packerHash.pop_back();
  ASSERT_EQ(packerHash.size(), 16u) << "the hash line pack_game.py printed";
  fakesd::addFile("/games/" + id + ".chgame", package);

  const GamePackageInstaller::Report report = GamePackageInstaller::installAll();
  ASSERT_EQ(report.installed, 1) << GamePackageInstaller::describe(report.firstError);
  EXPECT_EQ(report.failed, 0);
  EXPECT_FALSE(exists("/games/" + id + ".chgame"));
  EXPECT_FALSE(exists("/games/" + id + ".chgame.bad"));
  EXPECT_FALSE(exists("/.games-tmp"));

  // Listed through the registry, whole, and with the hash the packer printed: the two implementations of R4 agree.
  GameRegistry::Listing listing;
  ASSERT_TRUE(GameRegistry::load(listing));
  ASSERT_EQ(listing.count, 1u);
  EXPECT_STREQ(listing.entries[0].manifest.id, id.c_str());
  EXPECT_TRUE(listing.entries[0].check.ok());
  EXPECT_EQ(hex(Bytes(listing.entries[0].pkgHash, listing.entries[0].pkgHash + GamePkg::HASH_BYTES), 8), packerHash);
  EXPECT_EQ(toText(fakesd::bytesOf("/.games/" + id + "/.pkg")), "v1\n" + packerHash + "\n");
  EXPECT_TRUE(exists("/.games/" + id + "/manifest.json"));
  EXPECT_TRUE(exists("/.games/" + id + "/main.lua"));
}

INSTANTIATE_TEST_SUITE_P(Fixtures, PackedFixtureTest, ::testing::ValuesIn(fixtureIds()),
                         [](const ::testing::TestParamInfo<std::string>& info) {
                           std::string name = info.param;
                           std::replace(name.begin(), name.end(), '-', '_');
                           return name;
                         });

namespace {
class PackedImagesTest : public PackedFixtureBase {};
}  // namespace

// The fixture with an icon.png and other .png images: the installer turned each into a .bmp the loader reads, and the
// launcher's icon into the 64 x 64 one, at the packer's word (the packer already refused a side that would not scale).
TEST_F(PackedImagesTest, TheIconAndTheImagesAreConvertedAndTheirPngsAreGone) {
  fakesd::addFile("/games/pack-images.chgame", packed("pack-images.chgame"));
  ASSERT_EQ(GamePackageInstaller::installAll().installed, 1);

  const std::string dir = "/.games/pack-images/";
  for (const char* member : {"icon.png", "badge.png", "dot.png"}) EXPECT_FALSE(exists(dir + member)) << member;
  const GameCore::ImageHeader icon = headerOf(dir + "icon.bmp");
  EXPECT_EQ(icon.width, 64u);
  EXPECT_EQ(icon.height, 64u);
  const GameCore::ImageHeader badge = headerOf(dir + "badge.bmp");
  EXPECT_EQ(badge.width, 100u);
  EXPECT_EQ(badge.height, 60u);
  const GameCore::ImageHeader dot = headerOf(dir + "dot.bmp");
  EXPECT_EQ(dot.width, 37u);
  EXPECT_EQ(dot.height, 37u);
  // The icon has ink and white in it: an all-white or all-black conversion would mean the pixels were lost.
  const Bytes bmp = fakesd::bytesOf(dir + "icon.bmp");
  ASSERT_EQ(bmp.size(), 62u + 8u * 64u);
  const size_t white = std::count_if(bmp.begin() + 62, bmp.end(), [](const uint8_t b) { return b == 0xFF; });
  const size_t black = std::count_if(bmp.begin() + 62, bmp.end(), [](const uint8_t b) { return b == 0x00; });
  EXPECT_GT(white, 0u);
  EXPECT_GT(black, 0u);
}

// The fixture with one other image, the worst-case size: the packer accepts what the installer's budget accepts.
TEST_F(PackedImagesTest, ALargeImageAssetIsConvertedToItsOwnSize) {
  fakesd::addFile("/games/timing.chgame", packed("timing.chgame"));
  ASSERT_EQ(GamePackageInstaller::installAll().installed, 1);
  const GameCore::ImageHeader gray = headerOf("/.games/timing/gray.bmp");
  EXPECT_EQ(gray.width, 480u);
  EXPECT_EQ(gray.height, 800u);
  EXPECT_FALSE(exists("/.games/timing/gray.png"));
}
