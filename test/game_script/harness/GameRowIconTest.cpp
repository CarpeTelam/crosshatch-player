#include <GameIcons.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "GameIconBlit.h"
#include "GameRowIcon.h"
#include "HalStorage.h"
#include "HarnessSupport.h"

// GameRowIcon, the launcher's row icons, on the fake card: what its two readers put in the 64 x 64 Mask1 bitmap
// (top-down rows of 8 bytes, MSB first, bit 0 = ink) a list row draws, and the order that picks between them.
// GamesLauncherTest shows the screen draws one per row; here the pixels are pinned.

namespace {

using harness::Bytes;
using harness::logHas;

class RowIconTest : public harness::HarnessTest {
 protected:
  static Bytes iconFile(const int seed = 0) {
    return harness::bmpFile(64, 64, [seed](const int x, const int y) { return harness::speckle(x, y, seed); });
  }
  static void put(const std::string& id, const Bytes& file) { fakesd::addFile("/.games/" + id + "/icon.bmp", file); }
  static std::vector<uint8_t> read(const std::string& id, bool& ok) {
    std::vector<uint8_t> bits(GameRowIcon::BYTES, 0xA5);
    ok = GameRowIcon::readPackageIcon(id.c_str(), bits.data());
    return bits;
  }
};

// The library's own encoding, decoded here without GameIcons::PackedReader: a 2-byte length, then PackBits.
std::vector<uint8_t> unpack(const GameIcons::PackedBitmap bitmap) {
  const size_t length = bitmap.data[0] | (static_cast<size_t>(bitmap.data[1]) << 8);
  std::vector<uint8_t> out;
  for (size_t pos = 2; pos < 2 + length;) {
    const uint8_t control = bitmap.data[pos++];
    if (control < 128) {
      for (int i = 0; i <= control; ++i) out.push_back(bitmap.data[pos++]);
    } else {
      const uint8_t value = bitmap.data[pos++];
      for (int i = 0; i < 257 - control; ++i) out.push_back(value);
    }
  }
  return out;
}

TEST_F(RowIconTest, TheIconBmpsRowsAreTheBitmapAsTheyStand) {
  const Bytes file = iconFile(3);
  put("alpha", file);
  bool ok = false;
  const std::vector<uint8_t> bits = read("alpha", ok);
  ASSERT_TRUE(ok);
  EXPECT_EQ(bits, harness::rowsOf(file));
  ASSERT_EQ(bits.size(), 512u);
  // Mask1: a black pixel of the image (bit 0 in the file) is ink, and the image's white is not.
  const int x = 5;
  const int y = 9;
  const bool white = harness::speckle(x, y, 3);
  EXPECT_EQ(((bits[y * GameRowIcon::ROW_BYTES + x / 8] >> (7 - x % 8)) & 1) == 1, white);
  EXPECT_TRUE(GameRowIcon::hasPackageIcon("alpha"));
  EXPECT_FALSE(GameRowIcon::hasPackageIcon("beta"));
}

TEST_F(RowIconTest, AFileThatIsNotA64x64IconInTheConvertersLayoutIsRefused) {
  bool ok = true;
  read("missing", ok);
  EXPECT_FALSE(ok);
  EXPECT_FALSE(logHas("missing")) << "no icon.bmp is normal, not an error";

  Bytes truncated = iconFile();
  truncated.resize(40);
  put("truncated", truncated);
  read("truncated", ok);
  EXPECT_FALSE(ok);

  Bytes notBmp = iconFile();
  notBmp[0] = 'X';
  put("not-bmp", notBmp);
  read("not-bmp", ok);
  EXPECT_FALSE(ok);

  Bytes eightBit = iconFile();
  eightBit[28] = 8;  // bits per pixel
  put("eight-bit", eightBit);
  read("eight-bit", ok);
  EXPECT_FALSE(ok);

  Bytes longer = iconFile();
  longer.push_back(0);
  put("longer", longer);
  read("longer", ok);
  EXPECT_FALSE(ok);

  put("small", harness::bmpFile(32, 32, [](int x, int y) { return harness::speckle(x, y); }));
  read("small", ok);
  EXPECT_FALSE(ok);
  EXPECT_TRUE(logHas("32x32, not 64x64"));

  put("wide", harness::bmpFile(64, 32, [](int x, int y) { return harness::speckle(x, y); }));
  read("wide", ok);
  EXPECT_FALSE(ok);

  put("tall", harness::bmpFile(64, 96, [](int x, int y) { return harness::speckle(x, y); }));
  read("tall", ok);
  EXPECT_FALSE(ok);

  // A card error part way through the rows is a refusal, not a half-read bitmap taken as the icon.
  put("cut", iconFile());
  fakesd::sim().shortReadAt["/.games/cut/icon.bmp"] = 300;
  read("cut", ok);
  EXPECT_FALSE(ok);
  EXPECT_TRUE(logHas("Cannot read the rows"));

  put("unreadable", iconFile());
  fakesd::sim().failReadAt["/.games/unreadable/icon.bmp"] = 0;
  read("unreadable", ok);
  EXPECT_FALSE(ok);
}

TEST_F(RowIconTest, ALibraryIconIsDecodedRowForRowInBothWeights) {
  std::vector<uint8_t> bits(GameRowIcon::BYTES);
  for (size_t index = 0; index < GameIcons::ICON_COUNT; ++index) {
    const GameIcons::Icon& icon = GameIcons::ICONS[index];
    for (const bool fill : {false, true}) {
      SCOPED_TRACE(std::string(icon.name) + (fill ? " fill" : " regular"));
      ASSERT_TRUE(GameRowIcon::renderLibraryIcon(icon.name, fill, bits.data()));
      EXPECT_EQ(bits, unpack(icon.medium[fill ? 1 : 0]));
    }
  }
}

TEST_F(RowIconTest, TheWeightsDifferAndAnUnknownNameLeavesABlankIcon) {
  std::vector<uint8_t> regular(GameRowIcon::BYTES);
  std::vector<uint8_t> fill(GameRowIcon::BYTES);
  ASSERT_TRUE(GameRowIcon::renderLibraryIcon("dice-six", false, regular.data()));
  ASSERT_TRUE(GameRowIcon::renderLibraryIcon("dice-six", true, fill.data()));
  EXPECT_NE(regular, fill);
  EXPECT_NE(regular, std::vector<uint8_t>(GameRowIcon::BYTES, 0xFF)) << "it has ink";

  std::vector<uint8_t> none(GameRowIcon::BYTES, 0x12);
  EXPECT_FALSE(GameRowIcon::renderLibraryIcon("not-in-the-library", false, none.data()));
  EXPECT_EQ(none, std::vector<uint8_t>(GameRowIcon::BYTES, 0xFF));
  EXPECT_FALSE(GameRowIcon::renderLibraryIcon("", false, none.data()));
  EXPECT_TRUE(GameRowIcon::hasLibraryIcon("game-controller"));
  EXPECT_TRUE(GameRowIcon::hasLibraryIcon(GameRowIcon::FALLBACK_NAME));
  EXPECT_FALSE(GameRowIcon::hasLibraryIcon("game-controlle"));
}

TEST_F(RowIconTest, TheChoiceIsIconBmpThenTheManifestIconInItsWeightThenTheFallback) {
  using GameRowIcon::Source;
  const GameRowIcon::Choice bmp = GameRowIcon::choose(true, "dice-six", true);
  EXPECT_EQ(bmp.source, Source::PackageBmp);
  EXPECT_EQ(GameRowIcon::choose(true, "", false).source, Source::PackageBmp);

  const GameRowIcon::Choice regular = GameRowIcon::choose(false, "dice-six", false);
  EXPECT_EQ(regular.source, Source::Library);
  EXPECT_STREQ(regular.name, "dice-six");
  EXPECT_FALSE(regular.fill);
  const GameRowIcon::Choice fill = GameRowIcon::choose(false, "dice-six", true);
  EXPECT_EQ(fill.source, Source::Library);
  EXPECT_TRUE(fill.fill);

  // No icon, or one the library lacks: game-controller in its regular weight, whatever weight was asked for.
  for (const char* icon : {"", "not-in-the-library"}) {
    SCOPED_TRACE(icon);
    const GameRowIcon::Choice fallback = GameRowIcon::choose(false, icon, true);
    EXPECT_EQ(fallback.source, Source::Fallback);
    EXPECT_STREQ(fallback.name, GameRowIcon::FALLBACK_NAME);
    EXPECT_FALSE(fallback.fill);
  }
  EXPECT_EQ(GameRowIcon::choose(false, nullptr, false).source, Source::Fallback);
}

// The same bytes drawGameIcon draws from: GameIconBlit::inkAt, the decoder behind it, agrees with the row bitmap
// on every pixel (bit 0 = ink), for a few icons in both weights.
TEST_F(RowIconTest, TheRowBitmapHoldsTheInkGameIconBlitDraws) {
  std::vector<uint8_t> bits(GameRowIcon::BYTES);
  for (const char* name : {"dice-six", "boat", "game-controller"}) {
    const size_t index = static_cast<size_t>(GameIcons::find(name, std::strlen(name)));
    for (const bool fill : {false, true}) {
      SCOPED_TRACE(std::string(name) + (fill ? " fill" : " regular"));
      ASSERT_TRUE(GameRowIcon::renderLibraryIcon(name, fill, bits.data()));
      GameIconBlit::Source source;
      ASSERT_TRUE(GameIconBlit::sourceFor(index, GameRowIcon::SIDE,
                                          fill ? GameIcons::Weight::Fill : GameIcons::Weight::Regular, source));
      int mismatches = 0;
      for (int y = 0; y < GameRowIcon::SIDE; ++y) {
        for (int x = 0; x < GameRowIcon::SIDE; ++x) {
          const bool ink = ((bits[y * GameRowIcon::ROW_BYTES + x / 8] >> (7 - x % 8)) & 1) == 0;
          if (ink != GameIconBlit::inkAt(source.bitmap, GameRowIcon::SIDE, x, y)) ++mismatches;
        }
      }
      EXPECT_EQ(mismatches, 0);
    }
  }
}

}  // namespace
