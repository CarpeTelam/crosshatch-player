#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "BlobHeader.h"
#include "Codec.h"
#include "GameAssets.h"
#include "GameSaveStore.h"
#include "HarnessSupport.h"
#include "StoreSlot.h"

// GameAssets::load over the fake SD card and PSRAM stub: the block's layout, each
// span's offsets, the two passes' agreement, every result, and what is logged. The
// real GameAssets.cpp, GameSaveStore.cpp, and GameImages.cpp, unchanged.

namespace {

using GameCore::GameImages;
using GameCore::ImageSpan;
using GameScript::SourceSpan;
using harness::bmpFile;
using harness::Bytes;
using harness::logHas;
using harness::rowsOf;
using harness::speckle;
using Result = GameAssets::LoadResult;

constexpr const char* GAME = "/.games/demo";

std::string path(const std::string& name) { return std::string(GAME) + "/" + name; }

class GameAssetsLoadTest : public harness::HarnessTest {
 protected:
  void SetUp() override {
    HarnessTest::SetUp();
    slotStorage.assign(GameScript::Codec::STORE_LIMIT, 0);
    saveBuffer.assign(GameSaveStore::BUFFER_BYTES, 0);
    slot = std::make_unique<GameScript::StoreSlot>(slotStorage.data(), slotStorage.size());
    saves = std::make_unique<GameSaveStore>("demo", saveBuffer, 0);
  }

  void TearDown() override {
    assets.release();  // the block goes before the PSRAM check
    HarnessTest::TearDown();
  }

  Result load() { return assets.load("demo", *saves, *slot); }

  size_t slotBytes() const {
    Bytes out(GameScript::Codec::STORE_LIMIT);
    return slot->read(out);
  }

  // A converted image whose every pixel is set by `speckle(x, y, seed)`.
  static Bytes image(const int width, const int height, const int seed = 0) {
    return bmpFile(width, height, [seed](const int x, const int y) { return speckle(x, y, seed); });
  }

  // The bytes the block's first two tables and rows take.
  static size_t blockBytes(const size_t sources, const size_t images, const size_t pixelBytes, const size_t textBytes) {
    return sources * sizeof(SourceSpan) + images * sizeof(ImageSpan) + pixelBytes + textBytes;
  }

  GameAssets assets;
  Bytes slotStorage;
  Bytes saveBuffer;
  std::unique_ptr<GameScript::StoreSlot> slot;
  std::unique_ptr<GameSaveStore> saves;
};

// ---- ## 3.2: the block, its spans, and their offsets ----

TEST_F(GameAssetsLoadTest, ModulesAndImagesLandInOneBlockAtTheirOwnOffsets) {
  const std::string mainText = "return { setup = function() return {} end }";
  const std::string utilText = "return 7";
  const Bytes zed = image(40, 10, 0);   // 8-byte rows x 10 = 80 pixel bytes
  const Bytes alpha = image(33, 5, 1);  // 8-byte rows x 5 = 40 pixel bytes
  // Listing order is creation order: the spans follow it, not the names' order.
  fakesd::addFile(path("zed.bmp"), zed);
  fakesd::addFile(path("main.lua"), mainText);
  fakesd::addFile(path("alpha.bmp"), alpha);
  fakesd::addFile(path("util.lua"), utilText);

  ASSERT_EQ(load(), Result::Ok);

  const GameScript::GameSources& sources = assets.sources();
  ASSERT_EQ(sources.count, 2u);
  EXPECT_STREQ(sources.spans[0].name, "main");
  EXPECT_EQ(sources.spans[0].offset, 0u);
  EXPECT_EQ(sources.spans[0].length, mainText.size());
  EXPECT_STREQ(sources.spans[1].name, "util");
  EXPECT_EQ(sources.spans[1].offset, mainText.size());
  EXPECT_EQ(sources.spans[1].length, utilText.size());
  EXPECT_EQ(std::string(sources.text, mainText.size() + utilText.size()), mainText + utilText);

  const GameImages& images = assets.images();
  ASSERT_EQ(images.count, 2u);
  EXPECT_STREQ(images.spans[0].name, "zed");
  EXPECT_EQ(images.spans[0].width, 40u);
  EXPECT_EQ(images.spans[0].height, 10u);
  EXPECT_EQ(images.spans[0].rowBytes, 8u);
  EXPECT_EQ(images.spans[0].offset, 0u);
  EXPECT_STREQ(images.spans[1].name, "alpha");
  EXPECT_EQ(images.spans[1].width, 33u);
  EXPECT_EQ(images.spans[1].height, 5u);
  EXPECT_EQ(images.spans[1].rowBytes, 8u);
  EXPECT_EQ(images.spans[1].offset, 80u);  // after zed's rows
  const Bytes zedRows = rowsOf(zed);
  const Bytes alphaRows = rowsOf(alpha);
  EXPECT_EQ(Bytes(images.pixelsOf(images.spans[0]), images.pixelsOf(images.spans[0]) + zedRows.size()), zedRows);
  EXPECT_EQ(Bytes(images.pixelsOf(images.spans[1]), images.pixelsOf(images.spans[1]) + alphaRows.size()), alphaRows);

  // One block: [SourceSpan x 2][ImageSpan x 2][image rows][text], nothing between.
  const auto* const block = reinterpret_cast<const uint8_t*>(sources.spans);
  EXPECT_EQ(reinterpret_cast<const uint8_t*>(images.spans), block + 2 * sizeof(SourceSpan));
  EXPECT_EQ(images.pixels, block + 2 * sizeof(SourceSpan) + 2 * sizeof(ImageSpan));
  EXPECT_EQ(reinterpret_cast<const uint8_t*>(sources.text), images.pixels + 80 + 40);
  EXPECT_EQ(fakepsram::totalAllocations, 1u);
  EXPECT_EQ(fakepsram::lastRequestedBytes, blockBytes(2, 2, 120, mainText.size() + utilText.size()));
  EXPECT_TRUE(logHas("Loaded 2 Lua files (" + std::to_string(mainText.size() + utilText.size()) +
                     " bytes) and 2 images (" + std::to_string(zed.size() + alpha.size()) +
                     " bytes) from /.games/demo"));
}

TEST_F(GameAssetsLoadTest, AGameWithoutImagesLoadsItsModulesAlone) {
  fakesd::addFile(path("main.lua"), "return 1");
  fakesd::addFile(path("icon.bmp"), "not an image the game draws");
  fakesd::addDir(path("assets"));
  ASSERT_EQ(load(), Result::Ok);
  EXPECT_EQ(assets.sources().count, 1u);
  EXPECT_EQ(assets.images().count, 0u);
  EXPECT_EQ(fakepsram::lastRequestedBytes, blockBytes(1, 0, 0, 8));
  EXPECT_FALSE(logHas("icon.bmp"));  // the launcher's, skipped without a word
}

// The runtime's two pages (AD-15, as amended 2026-10-02) are converted beside the game's images but are no images of
// its own: GameAssets loads neither, says nothing of them, and loads the game's real images beside them.
TEST_F(GameAssetsLoadTest, TheReservedPagesAreSkippedWithoutAWordAndTheImagesBesideThemLoad) {
  fakesd::addFile(path("main.lua"), "return 1");
  fakesd::addFile(path("title.bmp"), image(480, 480));
  fakesd::addFile(path("handoff.bmp"), image(480, 800));
  fakesd::addFile(path("TITLE.BMP"), image(8, 8));
  fakesd::addFile(path("titles.bmp"), image(8, 8));
  ASSERT_EQ(load(), Result::Ok);
  ASSERT_EQ(assets.images().count, 1u);
  EXPECT_STREQ(assets.images().spans[0].name, "titles");
  EXPECT_EQ(assets.images().find("title", 5), -1);
  EXPECT_EQ(assets.images().find("handoff", 7), -1);
  EXPECT_FALSE(logHas("title.bmp"));
  EXPECT_FALSE(logHas("handoff.bmp"));
  EXPECT_FALSE(logHas("TITLE.BMP"));
  EXPECT_FALSE(logHas("is not loaded"));
}

TEST_F(GameAssetsLoadTest, TheLoadedViewsEmptyWhenTheGameIsReleased) {
  fakesd::addFile(path("main.lua"), "return 1");
  fakesd::addFile(path("dot.bmp"), image(8, 8));
  ASSERT_EQ(load(), Result::Ok);
  ASSERT_EQ(fakepsram::liveBlocks, 1u);
  assets.release();
  EXPECT_EQ(assets.sources().count, 0u);
  EXPECT_EQ(assets.images().count, 0u);
  EXPECT_EQ(fakepsram::liveBlocks, 0u);
  EXPECT_EQ(fakepsram::overruns, 0u);
}

TEST_F(GameAssetsLoadTest, ALaterLoadFreesTheBlockOfTheEarlierOne) {
  fakesd::addFile(path("main.lua"), "return 1");
  ASSERT_EQ(load(), Result::Ok);
  ASSERT_EQ(load(), Result::Ok);
  EXPECT_EQ(fakepsram::totalAllocations, 2u);
  EXPECT_EQ(fakepsram::liveBlocks, 1u);
  fakesd::removeEntry(path("main.lua"));
  EXPECT_EQ(load(), Result::NoSources);
  EXPECT_EQ(fakepsram::liveBlocks, 0u);
  EXPECT_EQ(assets.sources().count, 0u);
}

TEST_F(GameAssetsLoadTest, TheSavedStoreIsRestoredOnlyOnceTheLoadSucceeds) {
  // {taps = 3} in store.bin: header, then the codec bytes.
  Bytes store(GameScript::BLOB_HEADER_BYTES);
  GameScript::writeBlobHeader(store.data(), GameSaveStore::STORE_MAGIC, GameSaveStore::STORE_FILE_VERSION);
  const Bytes taps3 = {0x06, 0x00, 0x01, 0x05, 0x04, 't', 'a', 'p', 's', 0x03, 0x06};
  store.insert(store.end(), taps3.begin(), taps3.end());
  fakesd::addFile("/.games-data/demo/store.bin", store);

  fakesd::addDir(GAME);  // a folder with nothing to run
  EXPECT_EQ(load(), Result::NoSources);
  EXPECT_EQ(slotBytes(), 0u);

  fakesd::addFile(path("main.lua"), "return 1");
  ASSERT_EQ(load(), Result::Ok);
  EXPECT_EQ(slotBytes(), taps3.size());
}

// ---- Pass 1's results ----

TEST_F(GameAssetsLoadTest, AMissingFolderOrANonFolderIsFolderMissing) {
  EXPECT_EQ(load(), Result::FolderMissing);
  EXPECT_TRUE(logHas("No game folder /.games/demo"));
  fakelog::lines.clear();
  fakesd::addFile(GAME, "a file where the folder should be");
  EXPECT_EQ(load(), Result::FolderMissing);
  EXPECT_TRUE(logHas("/.games/demo is not a folder"));
  EXPECT_EQ(fakepsram::totalAllocations, 0u);
}

TEST_F(GameAssetsLoadTest, AFolderThatWillNotOpenIsCannotRead) {
  fakesd::addFile(path("main.lua"), "return 1");
  fakesd::sim().failOpen.insert(GAME);
  EXPECT_EQ(load(), Result::CannotRead);
  EXPECT_TRUE(logHas("Cannot open /.games/demo"));
}

TEST_F(GameAssetsLoadTest, NoLuaFilesIsNoSourcesAndOnlyMisnamedOnesIsBadSourceName) {
  fakesd::addDir(GAME);
  fakesd::addFile(path("logo.bmp"), image(8, 8));
  EXPECT_EQ(load(), Result::NoSources);
  EXPECT_TRUE(logHas("holds no loadable Lua sources"));

  fakesd::addFile(path("Main.lua"), "return 1");
  fakesd::addFile(path("bad-name.LUA"), "return 2");
  fakelog::lines.clear();
  EXPECT_EQ(load(), Result::BadSourceName);
  EXPECT_TRUE(logHas("/.games/demo/Main.lua is not loaded: a module name is [a-z0-9_]{1,32}.lua"));
  EXPECT_TRUE(logHas("/.games/demo/bad-name.LUA is not loaded"));
  EXPECT_EQ(fakepsram::totalAllocations, 0u);
}

TEST_F(GameAssetsLoadTest, AMisnamedFileBesideAModuleIsSkippedNotAFailure) {
  fakesd::addFile(path("main.lua"), "return 1");
  fakesd::addFile(path("Other.lua"), "return 2");
  fakesd::addFile(path("Big Logo.bmp"), image(8, 8));
  ASSERT_EQ(load(), Result::Ok);
  EXPECT_EQ(assets.sources().count, 1u);
  EXPECT_EQ(assets.images().count, 0u);
  EXPECT_TRUE(logHas("/.games/demo/Other.lua is not loaded"));
  EXPECT_TRUE(logHas("/.games/demo/Big Logo.bmp is not loaded: an image name is [a-z0-9_]{1,32}.bmp"));
}

TEST_F(GameAssetsLoadTest, TheModuleCapsAreInclusive) {
  // 32 modules of 8,192 bytes: exactly MAX_SOURCES and exactly MAX_SOURCE_BYTES.
  for (size_t i = 0; i < GameAssets::MAX_SOURCES; ++i) {
    fakesd::addFile(path("m" + std::to_string(i) + ".lua"),
                    Bytes(GameAssets::MAX_SOURCE_BYTES / GameAssets::MAX_SOURCES, 'x'));
  }
  ASSERT_EQ(load(), Result::Ok);
  EXPECT_EQ(assets.sources().count, GameAssets::MAX_SOURCES);

  fakesd::addFile(path("one_more.lua"), "return 1");  // 33 modules
  fakelog::lines.clear();
  EXPECT_EQ(load(), Result::TooLarge);
  EXPECT_TRUE(logHas("33 Lua files, 262152 bytes; limits 32 and 262144"));
  EXPECT_EQ(fakepsram::liveBlocks, 0u);
}

TEST_F(GameAssetsLoadTest, OneByteOverTheTextLimitIsTooLarge) {
  fakesd::addFile(path("main.lua"), Bytes(GameAssets::MAX_SOURCE_BYTES + 1, 'x'));
  EXPECT_EQ(load(), Result::TooLarge);
  fakesd::addFile(path("main.lua"), Bytes(GameAssets::MAX_SOURCE_BYTES, 'x'));
  EXPECT_EQ(load(), Result::Ok);
}

TEST_F(GameAssetsLoadTest, ADamagedImageIsBadImageWithItsReasonLogged) {
  fakesd::addFile(path("main.lua"), "return 1");
  Bytes notBmp = image(8, 8);
  notBmp[0] = 'X';
  fakesd::addFile(path("logo.bmp"), notBmp);
  EXPECT_EQ(load(), Result::BadImage);
  EXPECT_TRUE(logHas("/.games/demo/logo.bmp is not a usable image: not a BMP file"));
  EXPECT_EQ(fakepsram::totalAllocations, 0u);
  EXPECT_EQ(slotBytes(), 0u);
}

TEST_F(GameAssetsLoadTest, TheImageCapsAreBadImageWithTheirOwnLogLines) {
  fakesd::addFile(path("main.lua"), "return 1");
  for (size_t i = 0; i < GameCore::MAX_IMAGES + 1; ++i)
    fakesd::addFile(path("i" + std::to_string(i) + ".bmp"), image(8, 8));
  EXPECT_EQ(load(), Result::BadImage);
  EXPECT_TRUE(logHas("/.games/demo/i32.bmp is one image too many: a game has at most 32"));

  // 32,830 bytes each: the fourth passes the 128 KiB budget.
  for (size_t i = 0; i < GameCore::MAX_IMAGES + 1; ++i) fakesd::removeEntry(path("i" + std::to_string(i) + ".bmp"));
  fakelog::lines.clear();
  for (int i = 0; i < 4; ++i) fakesd::addFile(path("big" + std::to_string(i) + ".bmp"), image(256, 1024));
  EXPECT_EQ(load(), Result::BadImage);
  EXPECT_TRUE(logHas("/.games/demo/big3.bmp is not a usable image: over the images budget"));
  EXPECT_TRUE(logHas("images take at most 131072 bytes in all"));
}

TEST_F(GameAssetsLoadTest, TheImageCapsAreInclusive) {
  fakesd::addFile(path("main.lua"), "return 1");
  // Exactly MAX_IMAGES images load.
  for (size_t i = 0; i < GameCore::MAX_IMAGES; ++i)
    fakesd::addFile(path("i" + std::to_string(i) + ".bmp"), image(8, 8));
  ASSERT_EQ(load(), Result::Ok);
  EXPECT_EQ(assets.images().count, GameCore::MAX_IMAGES);
  for (size_t i = 0; i < GameCore::MAX_IMAGES; ++i) fakesd::removeEntry(path("i" + std::to_string(i) + ".bmp"));

  // Two files of exactly IMAGES_BYTES between them (62 + 4 x height each, 8 px wide) load;
  // one more image, however small, is over the budget.
  fakesd::addFile(path("tall.bmp"), image(8, 20000));
  fakesd::addFile(path("tall2.bmp"), image(8, 12737));
  ASSERT_EQ(fakesd::bytesOf(path("tall.bmp")).size() + fakesd::bytesOf(path("tall2.bmp")).size(),
            GameCore::IMAGES_BYTES);
  ASSERT_EQ(load(), Result::Ok);
  EXPECT_EQ(assets.images().count, 2u);
  fakesd::addFile(path("tiny.bmp"), image(8, 1));
  EXPECT_EQ(load(), Result::BadImage);
  EXPECT_TRUE(logHas("/.games/demo/tiny.bmp is not a usable image: over the images budget"));
}

TEST_F(GameAssetsLoadTest, PsramThatWillNotAllocateIsOutOfMemoryWithNothingHeld) {
  fakesd::addFile(path("main.lua"), "return 1");
  fakepsram::failNext = true;
  EXPECT_EQ(load(), Result::OutOfMemory);
  EXPECT_TRUE(logHas("OOM: " + std::to_string(blockBytes(1, 0, 0, 8)) + " bytes of PSRAM for Lua sources and images"));
  EXPECT_EQ(fakepsram::liveBlocks, 0u);
  EXPECT_EQ(assets.sources().count, 0u);
  EXPECT_EQ(slotBytes(), 0u);
}

// ---- f27dcefd: an image header that cannot be read is an SD error, not a damaged image ----

TEST_F(GameAssetsLoadTest, AFailedHeaderReadIsCannotReadNotBadImage) {
  fakesd::addFile(path("main.lua"), "return 1");
  fakesd::addFile(path("logo.bmp"), image(16, 16));
  fakesd::sim().failReadAt[path("logo.bmp")] = 0;
  EXPECT_EQ(load(), Result::CannotRead);
  EXPECT_TRUE(logHas("Cannot read /.games/demo/logo.bmp"));
  EXPECT_FALSE(logHas("not a usable image"));
  EXPECT_EQ(fakepsram::totalAllocations, 0u);
}

TEST_F(GameAssetsLoadTest, AShortHeaderReadOfALongEnoughFileIsCannotRead) {
  fakesd::addFile(path("main.lua"), "return 1");
  fakesd::addFile(path("logo.bmp"), image(16, 16));  // 62 + 64 bytes
  fakesd::sim().shortReadAt[path("logo.bmp")] = 30;
  EXPECT_EQ(load(), Result::CannotRead);
  EXPECT_TRUE(logHas("Cannot read /.games/demo/logo.bmp"));
  EXPECT_FALSE(logHas("truncated"));
}

TEST_F(GameAssetsLoadTest, TheHeaderSizeIsTheEdgeBetweenTruncatedAndCheckedForItsFields) {
  fakesd::addFile(path("main.lua"), "return 1");
  fakesd::addFile(path("edge.bmp"), Bytes(61, 0));
  EXPECT_EQ(load(), Result::BadImage);
  EXPECT_TRUE(logHas("edge.bmp is not a usable image: truncated"));
  fakelog::lines.clear();
  fakesd::addFile(path("edge.bmp"), Bytes(62, 0));  // a whole header, with no signature
  EXPECT_EQ(load(), Result::BadImage);
  EXPECT_TRUE(logHas("edge.bmp is not a usable image: not a BMP file"));
}

TEST_F(GameAssetsLoadTest, AFileUnderTheHeaderIsATruncatedImageNotAReadError) {
  fakesd::addFile(path("main.lua"), "return 1");
  fakesd::addFile(path("stub.bmp"), Bytes(40, 0));  // read back whole: 40 of the 40 it holds
  EXPECT_EQ(load(), Result::BadImage);
  EXPECT_TRUE(logHas("/.games/demo/stub.bmp is not a usable image: truncated"));
  EXPECT_FALSE(logHas("Cannot read"));

  fakelog::lines.clear();
  fakesd::addFile(path("stub.bmp"), Bytes{});  // an empty file reads back 0 of 0
  EXPECT_EQ(load(), Result::BadImage);
  EXPECT_TRUE(logHas("stub.bmp is not a usable image: truncated"));
}

// ---- Pass 2: the re-read, the failures, and the release ----

TEST_F(GameAssetsLoadTest, APixelReadThatFailsInPassTwoReleasesTheBlock) {
  fakesd::addFile(path("main.lua"), "return 1");
  fakesd::addFile(path("logo.bmp"), image(16, 16));
  fakesd::sim().failReadAt[path("logo.bmp")] = 62;  // the header reads; the rows do not
  EXPECT_EQ(load(), Result::CannotRead);
  EXPECT_TRUE(logHas("Cannot read /.games/demo/logo.bmp, or it changed since it was checked"));
  EXPECT_EQ(fakepsram::totalAllocations, 1u);  // pass 1 passed and sized a block...
  EXPECT_EQ(fakepsram::liveBlocks, 0u);        // ...which the failure freed
  EXPECT_EQ(fakepsram::overruns, 0u);
  EXPECT_EQ(assets.sources().count, 0u);
  EXPECT_EQ(assets.sources().spans, nullptr);
  EXPECT_EQ(assets.images().count, 0u);
  EXPECT_EQ(slotBytes(), 0u);
}

TEST_F(GameAssetsLoadTest, AModuleReadThatFailsOrComesUpShortReleasesTheBlock) {
  fakesd::addFile(path("main.lua"), "return 12345");
  fakesd::sim().failReadAt[path("main.lua")] = 0;
  EXPECT_EQ(load(), Result::CannotRead);
  EXPECT_TRUE(logHas("Cannot read /.games/demo/main.lua"));
  EXPECT_EQ(fakepsram::liveBlocks, 0u);

  fakesd::sim().failReadAt.clear();
  fakesd::sim().shortReadAt[path("main.lua")] = 3;
  EXPECT_EQ(load(), Result::CannotRead);
  EXPECT_EQ(fakepsram::liveBlocks, 0u);
  EXPECT_EQ(assets.sources().count, 0u);
}

TEST_F(GameAssetsLoadTest, AFileTheFolderLostBetweenThePassesIsCannotReadWithTheCounts) {
  fakesd::addFile(path("main.lua"), "return 1");
  fakesd::addFile(path("util.lua"), "return 2");
  fakesd::addFile(path("dot.bmp"), image(8, 8));
  fakesd::sim().onRewind = [&](const std::string&, const int rewinds) {
    if (rewinds == 2) fakesd::removeEntry(path("util.lua"));  // the second rewind is pass 2's
  };
  EXPECT_EQ(load(), Result::CannotRead);
  EXPECT_TRUE(logHas("Read 1 of 2 Lua files and 1 of 1 images from /.games/demo"));
  EXPECT_EQ(fakepsram::liveBlocks, 0u);
  EXPECT_EQ(assets.sources().count, 0u);
  EXPECT_EQ(assets.images().count, 0u);
}

TEST_F(GameAssetsLoadTest, AnImageThatGrewBetweenThePassesIsRefusedWithoutOverrunningTheBlock) {
  fakesd::addFile(path("main.lua"), "return 1");
  fakesd::addFile(path("logo.bmp"), image(8, 8));
  fakesd::sim().onRewind = [&](const std::string&, const int rewinds) {
    if (rewinds == 2) fakesd::addFile(path("logo.bmp"), image(64, 64));  // more rows than pass 1 sized
  };
  EXPECT_EQ(load(), Result::CannotRead);
  EXPECT_TRUE(logHas("or it changed since it was checked"));
  EXPECT_EQ(fakepsram::liveBlocks, 0u);
  EXPECT_EQ(fakepsram::overruns, 0u);
}

TEST_F(GameAssetsLoadTest, AnImageThatShrankBetweenThePassesStillLoadsWithItsNewSize) {
  fakesd::addFile(path("main.lua"), "return 1");
  fakesd::addFile(path("logo.bmp"), image(8, 8));
  const Bytes smaller = image(4, 4, 3);
  fakesd::sim().onRewind = [&](const std::string&, const int rewinds) {
    if (rewinds == 2) fakesd::addFile(path("logo.bmp"), smaller);
  };
  ASSERT_EQ(load(), Result::Ok);  // within what pass 1 sized
  ASSERT_EQ(assets.images().count, 1u);
  EXPECT_EQ(assets.images().spans[0].width, 4u);
  EXPECT_EQ(assets.images().spans[0].height, 4u);
  const Bytes rows = rowsOf(smaller);
  EXPECT_EQ(Bytes(assets.images().pixels, assets.images().pixels + rows.size()), rows);
}

TEST_F(GameAssetsLoadTest, AnImageThatGrewBeyondItsOwnRowsIsRefusedEvenWhenTheFilesStillFitTheBudget) {
  // Two 8 x 8 images: pass 1 counts 2 x (62 + 32) file bytes and 64 row bytes. Pass 2 lists
  // a.bmp first, grown to 8 x 23: 154 file bytes (within the 188) but 92 row bytes (over the 64).
  fakesd::addFile(path("main.lua"), "return 1");
  fakesd::addFile(path("a.bmp"), image(8, 8));
  fakesd::addFile(path("b.bmp"), image(8, 8, 1));
  fakesd::sim().onRewind = [&](const std::string&, const int rewinds) {
    if (rewinds == 2) fakesd::addFile(path("a.bmp"), image(8, 23));
  };
  EXPECT_EQ(load(), Result::CannotRead);
  EXPECT_TRUE(logHas("or it changed since it was checked"));
  EXPECT_EQ(fakepsram::liveBlocks, 0u);
  EXPECT_EQ(fakepsram::overruns, 0u);
}

TEST_F(GameAssetsLoadTest, AnImageThatAppearsInPassTwoIsNotReadPastThePassOneCount) {
  const Bytes logo = image(8, 8);
  fakesd::addFile(path("logo.bmp"), logo);
  fakesd::addFile(path("main.lua"), "return 1");
  fakesd::sim().onRewind = [&](const std::string&, const int rewinds) {
    if (rewinds != 2) return;
    // Pass 2 lists logo.bmp, a new image, then main.lua.
    fakesd::removeEntry(path("main.lua"));
    fakesd::addFile(path("late.bmp"), image(8, 8, 2));
    fakesd::addFile(path("main.lua"), "return 1");
  };
  ASSERT_EQ(load(), Result::Ok);
  ASSERT_EQ(assets.images().count, 1u);
  EXPECT_STREQ(assets.images().spans[0].name, "logo");
  ASSERT_EQ(assets.sources().count, 1u);
  EXPECT_STREQ(assets.sources().spans[0].name, "main");
  EXPECT_EQ(fakepsram::overruns, 0u);
}

TEST_F(GameAssetsLoadTest, AnImageSwappedForAnotherLayoutBetweenThePassesIsRefused) {
  fakesd::addFile(path("main.lua"), "return 1");
  fakesd::addFile(path("logo.bmp"), image(8, 8));
  fakesd::sim().onRewind = [&](const std::string&, const int rewinds) {
    if (rewinds == 2) fakesd::addFile(path("logo.bmp"), Bytes(100, 'x'));  // not a BMP now
  };
  EXPECT_EQ(load(), Result::CannotRead);
  EXPECT_TRUE(logHas("Cannot read /.games/demo/logo.bmp, or it changed since it was checked"));
  EXPECT_EQ(fakepsram::liveBlocks, 0u);
}

TEST_F(GameAssetsLoadTest, ModuleTextThatGrewBetweenThePassesIsRefusedWithoutOverrunningTheBlock) {
  fakesd::addFile(path("main.lua"), "return 1");
  fakesd::sim().onRewind = [&](const std::string&, const int rewinds) {
    if (rewinds == 2) fakesd::addFile(path("main.lua"), Bytes(500, 'x'));  // the block holds 8 bytes of text
  };
  EXPECT_EQ(load(), Result::CannotRead);
  EXPECT_TRUE(logHas("Cannot read /.games/demo/main.lua"));
  EXPECT_EQ(fakepsram::liveBlocks, 0u);
  EXPECT_EQ(fakepsram::overruns, 0u);
}

TEST_F(GameAssetsLoadTest, AModuleThatAppearsInPassTwoIsNotReadPastThePassOneCount) {
  const Bytes logo = image(8, 8);
  fakesd::addFile(path("main.lua"), "return 1");
  fakesd::addFile(path("logo.bmp"), logo);
  fakesd::sim().onRewind = [&](const std::string&, const int rewinds) {
    if (rewinds != 2) return;
    // Pass 2 lists main.lua, a new module, then the image.
    fakesd::removeEntry(path("logo.bmp"));
    fakesd::addFile(path("late.lua"), Bytes(400, 'x'));
    fakesd::addFile(path("logo.bmp"), logo);
  };
  ASSERT_EQ(load(), Result::Ok);
  ASSERT_EQ(assets.sources().count, 1u);
  ASSERT_EQ(assets.images().count, 1u);
  EXPECT_STREQ(assets.sources().spans[0].name, "main");
  EXPECT_STREQ(assets.images().spans[0].name, "logo");
  EXPECT_EQ(assets.images().spans[0].width, 8u);
  EXPECT_EQ(fakepsram::overruns, 0u);
}

// ---- e3r-2: names the buffer cannot hold ----

TEST_F(GameAssetsLoadTest, ANameSdFatCannotFitIsSaidAndNeverLoaded) {
  fakesd::addFile(path("main.lua"), "return 1");
  fakesd::addFile(path(std::string(60, 'a') + ".lua"), "return 2");
  ASSERT_EQ(load(), Result::Ok);  // getName returned 0 for it: skipped
  EXPECT_EQ(assets.sources().count, 1u);
  EXPECT_TRUE(logHas("/.games/demo holds a file whose name is 47 bytes or longer, or unreadable; it is not loaded"));

  // Alone, it is not a misnamed module either: the folder holds no Lua, not only bad names.
  fakesd::removeEntry(path("main.lua"));
  EXPECT_EQ(load(), Result::NoSources);
}

TEST_F(GameAssetsLoadTest, ALongNameTheSimulatorCutsIsSaidWithItsPrefixAndNeverLoaded) {
  fakesd::sim().getNameCuts = true;
  const std::string cut(47, 'a');
  fakesd::addFile(path(std::string(60, 'a') + ".lua"), "return 2");
  fakesd::addFile(path(std::string(60, 'b') + ".bmp"), image(8, 8));
  EXPECT_EQ(load(), Result::NoSources);  // neither counts as a module or as a misnamed one
  EXPECT_TRUE(logHas("/.games/demo/" + cut + "... has a name of 47 bytes or longer; it is not loaded"));
  EXPECT_TRUE(logHas("/.games/demo/" + std::string(47, 'b') + "... has a name of 47 bytes or longer"));
  EXPECT_FALSE(logHas("a module name is"));
}

TEST_F(GameAssetsLoadTest, ANameThatFitsButIsTooLongForAModuleIsMisnamedNotLong) {
  fakesd::addFile(path(std::string(40, 'a') + ".lua"), "return 2");  // 44 bytes: fits the buffer, not a module
  EXPECT_EQ(load(), Result::BadSourceName);
  EXPECT_TRUE(logHas("is not loaded: a module name is [a-z0-9_]{1,32}.lua"));
  EXPECT_FALSE(logHas("47 bytes or longer"));
}

TEST_F(GameAssetsLoadTest, TheNameLengthLimitsAreExactlyThirtyTwoAndFortySix) {
  const auto stem = [](const size_t n) { return std::string(n, 'a'); };
  // A 32-character stem is a module and an image; 33 is neither, and said so.
  fakesd::addFile(path(stem(32) + ".lua"), "return 1");
  fakesd::addFile(path(stem(32) + ".bmp"), image(8, 8));
  ASSERT_EQ(load(), Result::Ok);
  EXPECT_EQ(assets.sources().count, 1u);
  ASSERT_EQ(assets.images().count, 1u);
  EXPECT_STREQ(assets.sources().spans[0].name, stem(32).c_str());
  EXPECT_STREQ(assets.images().spans[0].name, stem(32).c_str());

  fakesd::removeEntry(path(stem(32) + ".lua"));
  fakesd::removeEntry(path(stem(32) + ".bmp"));
  fakesd::addFile(path(stem(33) + ".lua"), "return 1");
  fakesd::addFile(path(stem(33) + ".bmp"), image(8, 8));
  fakelog::lines.clear();
  EXPECT_EQ(load(), Result::BadSourceName);
  EXPECT_TRUE(logHas(stem(33) + ".lua is not loaded: a module name is"));
  EXPECT_TRUE(logHas(stem(33) + ".bmp is not loaded: an image name is"));

  // A 46-byte name still fits the buffer (so it is misnamed, not long); a 47-byte one does not.
  fakesd::removeEntry(path(stem(33) + ".lua"));
  fakesd::removeEntry(path(stem(33) + ".bmp"));
  fakesd::addFile(path(stem(42) + ".lua"), "return 1");
  fakelog::lines.clear();
  EXPECT_EQ(load(), Result::BadSourceName);
  EXPECT_TRUE(logHas(stem(42) + ".lua is not loaded: a module name is"));
  EXPECT_FALSE(logHas("47 bytes or longer"));
  fakesd::removeEntry(path(stem(42) + ".lua"));
  fakesd::addFile(path(stem(46)), "x");  // fits, and is neither Lua nor a bitmap: nothing to say
  fakesd::addFile(path("main.lua"), "return 1");
  fakelog::lines.clear();
  EXPECT_EQ(load(), Result::Ok);
  EXPECT_FALSE(logHas("bytes or longer"));
  fakesd::removeEntry(path(stem(46)));
  fakesd::removeEntry(path("main.lua"));
  fakesd::addFile(path(stem(47)), "x");
  fakelog::lines.clear();
  EXPECT_EQ(load(), Result::NoSources);
  EXPECT_TRUE(logHas("... has a name of 47 bytes or longer; it is not loaded"));
}

TEST_F(GameAssetsLoadTest, ANameThatFillsTheBufferIsNeverClassified) {
  // 47 bytes fill the buffer (47 and a NUL), so the name may have been cut: it is not read
  // as a module or an image, and a name ending in neither extension is said to be long.
  fakesd::addFile(path("main.lua"), "return 1");
  fakesd::addFile(path(std::string(47, 'a')), "x");
  ASSERT_EQ(load(), Result::Ok);
  EXPECT_TRUE(
      logHas("/.games/demo/" + std::string(47, 'a') + "... has a name of 47 bytes or longer; it is not loaded"));

  // One ending in .lua is the misnamed kind: said as that, and never loaded.
  fakelog::lines.clear();
  fakesd::removeEntry(path(std::string(47, 'a')));
  fakesd::addFile(path(std::string(43, 'a') + ".lua"), "return 2");
  ASSERT_EQ(load(), Result::Ok);
  EXPECT_EQ(assets.sources().count, 1u);
  EXPECT_TRUE(logHas("is not loaded: a module name is [a-z0-9_]{1,32}.lua"));
  EXPECT_FALSE(logHas("47 bytes or longer"));
}

TEST_F(GameAssetsLoadTest, FoldersInTheGameFolderAreNotFiles) {
  fakesd::addFile(path("main.lua"), "return 1");
  fakesd::addDir(path("sub.lua"));
  fakesd::addDir(path("pic.bmp"));
  fakesd::addDir(path(std::string(60, 'a')));
  ASSERT_EQ(load(), Result::Ok);
  EXPECT_EQ(assets.sources().count, 1u);
  EXPECT_EQ(assets.images().count, 0u);
  EXPECT_TRUE(fakelog::lines.size() == 1u && logHas("Loaded 1 Lua files"));  // no skip line for a folder
}

}  // namespace
