#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "GfxRenderer.h"
#include "HalMemory.h"
#include "HalStorage.h"
#include "HarnessSupport.h"

// The harness's own doubles, pinned so a suite that leans on one is not fooled by
// it: the fake SD card (listing, getName, failures), the PSRAM stub's guards, and
// the recording renderer's clip.

namespace {

using harness::Bytes;
using harness::HarnessTest;

std::vector<std::string> namesIn(const char* folder) {
  std::vector<std::string> names;
  auto dir = Storage.open(folder);
  dir.rewindDirectory();
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    char name[64];
    entry.getName(name, sizeof(name));
    names.emplace_back(entry.isDirectory() ? std::string(name) + "/" : std::string(name));
  }
  return names;
}

TEST_F(HarnessTest, AFolderListsItsDirectChildrenInCreationOrder) {
  fakesd::addFile("/g/zed.lua", "z");
  fakesd::addDir("/g/sub");
  fakesd::addFile("/g/alpha.bmp", "a");
  fakesd::addFile("/g/sub/deep.lua", "d");
  fakesd::addFile("/other/x.lua", "x");
  EXPECT_EQ(namesIn("/g"), (std::vector<std::string>{"zed.lua", "sub/", "alpha.bmp"}));
  EXPECT_EQ(namesIn("/g/sub"), (std::vector<std::string>{"deep.lua"}));
  EXPECT_EQ(namesIn("/"), (std::vector<std::string>{"g/", "other/"}));
}

TEST_F(HarnessTest, OpeningWhatIsMissingGivesAnEmptyFile) {
  fakesd::addFile("/g/a.lua", "a");
  EXPECT_FALSE(Storage.open("/g/none.lua"));
  EXPECT_TRUE(Storage.open("/g/a.lua"));
  EXPECT_TRUE(Storage.open("/g").isDirectory());
  EXPECT_FALSE(Storage.open("/g/a.lua").isDirectory());
  fakesd::sim().failOpen.insert("/g/a.lua");
  EXPECT_FALSE(Storage.open("/g/a.lua"));
}

TEST_F(HarnessTest, RewindingCallsTheHookWithTheFoldersRewindCount) {
  fakesd::addFile("/g/a.lua", "a");
  std::vector<std::string> seen;
  fakesd::sim().onRewind = [&](const std::string& dir, const int count) {
    seen.push_back(dir + "#" + std::to_string(count));
  };
  auto dir = Storage.open("/g");
  dir.rewindDirectory();
  dir.rewindDirectory();
  EXPECT_EQ(seen, (std::vector<std::string>{"/g#1", "/g#2"}));
}

TEST_F(HarnessTest, GetNameFollowsSdFatOrTheSimulator) {
  const std::string longName(60, 'a');
  fakesd::addFile("/g/" + longName, "x");
  fakesd::addFile("/g/ok.lua", "x");
  char name[48];
  auto dir = Storage.open("/g");
  dir.rewindDirectory();
  auto longFile = dir.openNextFile();
  auto okFile = dir.openNextFile();
  EXPECT_EQ(okFile.getName(name, sizeof(name)), 6u);
  EXPECT_STREQ(name, "ok.lua");
  // SdFat: 0 for a name that does not fit.
  EXPECT_EQ(longFile.getName(name, sizeof(name)), 0u);
  // The simulator: cut to fit.
  fakesd::sim().getNameCuts = true;
  EXPECT_EQ(longFile.getName(name, sizeof(name)), 47u);
  EXPECT_EQ(std::string(name), longName.substr(0, 47));
}

TEST_F(HarnessTest, ReadsStopAtTheFileEndAndFailPerPath) {
  fakesd::addFile("/g/a.bin", Bytes{1, 2, 3, 4, 5, 6, 7, 8});
  fakesd::addFile("/g/b.bin", Bytes{1, 2, 3, 4, 5, 6, 7, 8});
  fakesd::addFile("/g/c.bin", Bytes{1, 2, 3, 4, 5, 6, 7, 8});
  uint8_t out[16];
  auto a = Storage.open("/g/a.bin");
  EXPECT_EQ(a.read(out, 6), 6);
  EXPECT_EQ(a.read(out, 6), 2);  // to the end
  EXPECT_EQ(a.read(out, 6), 0);

  fakesd::sim().failReadAt["/g/b.bin"] = 4;
  auto b = Storage.open("/g/b.bin");
  EXPECT_EQ(b.read(out, 4), 4);   // bytes 0-3 are before the failure
  EXPECT_EQ(b.read(out, 1), -1);  // byte 4 is at it

  fakesd::sim().shortReadAt["/g/c.bin"] = 3;
  auto c = Storage.open("/g/c.bin");
  EXPECT_EQ(c.read(out, 8), 3);  // a short read, not an error
  EXPECT_EQ(c.read(out, 8), 0);
}

TEST_F(HarnessTest, WritesRenamesRemovesAndMkdirsFailPerPath) {
  fakesd::addDir("/d");
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("T", "/d/f.tmp", file));
  const uint8_t data[3] = {9, 8, 7};
  EXPECT_EQ(file.write(data, 3), 3u);
  EXPECT_TRUE(file.close());
  EXPECT_EQ(fakesd::bytesOf("/d/f.tmp"), (Bytes{9, 8, 7}));

  // Opening for write truncates; a write failure stores nothing.
  fakesd::sim().failWrite.insert("/d/f.tmp");
  ASSERT_TRUE(Storage.openFileForWrite("T", "/d/f.tmp", file));
  EXPECT_EQ(file.write(data, 3), 0u);
  file.close();
  EXPECT_TRUE(fakesd::bytesOf("/d/f.tmp").empty());
  fakesd::sim().failWrite.clear();

  fakesd::addFile("/d/f.bin", "x");
  fakesd::addFile("/d/f.tmp", "y");
  EXPECT_FALSE(Storage.rename("/d/f.tmp", "/d/f.bin"));  // as SdFat: not over an existing file
  fakesd::sim().failRename.insert("/d/f.tmp");
  EXPECT_FALSE(Storage.rename("/d/f.tmp", "/d/g.bin"));
  fakesd::sim().failRename.clear();
  EXPECT_TRUE(Storage.rename("/d/f.tmp", "/d/g.bin"));
  EXPECT_TRUE(fakesd::has("/d/g.bin"));
  EXPECT_FALSE(fakesd::has("/d/f.tmp"));

  fakesd::sim().failRemove.insert("/d/g.bin");
  EXPECT_FALSE(Storage.remove("/d/g.bin"));
  fakesd::sim().failRemove.clear();
  EXPECT_TRUE(Storage.remove("/d/g.bin"));
  EXPECT_FALSE(Storage.remove("/d/g.bin"));

  fakesd::sim().failMkdir.insert("/d/new");
  EXPECT_FALSE(Storage.mkdir("/d/new"));
  fakesd::sim().failMkdir.clear();
  EXPECT_TRUE(Storage.mkdir("/d/new/deeper"));  // with parents
  EXPECT_TRUE(fakesd::has("/d/new"));
  EXPECT_EQ(fakesd::countOps("rename "), 3u);
}

TEST_F(HarnessTest, ThePsramStubCatchesAWritePastTheEnd) {
  expectCleanPsram = false;  // this test damages blocks on purpose
  {
    auto block = HalMemory::allocatePsram(16);
    ASSERT_TRUE(block);
    EXPECT_EQ(fakepsram::liveBlocks, 1u);
    EXPECT_EQ(block[0], fakepsram::POISON);
    EXPECT_EQ(block[15], fakepsram::POISON);
    std::memset(block.get(), 0, 16);
  }
  EXPECT_EQ(fakepsram::liveBlocks, 0u);
  EXPECT_EQ(fakepsram::overruns, 0u);
  {
    auto block = HalMemory::allocatePsram(16);
    block[16] = 0;  // one past the end
  }
  EXPECT_EQ(fakepsram::overruns, 1u);
  {
    auto block = HalMemory::allocatePsram(16);
    block[-1] = 0;  // one before the start
  }
  EXPECT_EQ(fakepsram::overruns, 2u);
}

TEST_F(HarnessTest, TheRemainingCardCallsBehaveAndFailPerPath) {
  fakesd::addFile("/d/f.bin", "abc");
  fakesd::addDir("/d/empty");
  HalFile file;
  // openFileForRead: a missing file is false, an existing one opens and reads.
  EXPECT_FALSE(Storage.openFileForRead("T", "/d/none.bin", file));
  ASSERT_TRUE(Storage.openFileForRead("T", std::string("/d/f.bin"), file));
  EXPECT_EQ(file.fileSize(), 3u);
  EXPECT_EQ(file.read(), 'a');
  EXPECT_EQ(file.position(), 1u);
  // A folder is not read as a file.
  uint8_t out[4];
  EXPECT_EQ(Storage.open("/d/empty").read(out, 4), -1);
  // Opening for write can fail per path, and close can.
  fakesd::sim().failOpenWrite.insert("/d/w.bin");
  EXPECT_FALSE(Storage.openFileForWrite("T", "/d/w.bin", file));
  EXPECT_FALSE(fakesd::has("/d/w.bin"));
  fakesd::sim().failClose.insert("/d/x.bin");
  ASSERT_TRUE(Storage.openFileForWrite("T", "/d/x.bin", file));
  EXPECT_FALSE(file.close());
  EXPECT_FALSE(file.close());  // already closed
  // ensureDirectoryExists makes parents; rmdir refuses a folder with something in it.
  EXPECT_TRUE(Storage.ensureDirectoryExists("/a/b/c"));
  EXPECT_TRUE(fakesd::has("/a/b"));
  EXPECT_FALSE(Storage.rmdir("/a/b"));
  EXPECT_TRUE(Storage.rmdir("/a/b/c"));
  EXPECT_TRUE(Storage.rmdir("/a/b"));
  // A file written and renamed through its own handle keeps the new path.
  ASSERT_TRUE(Storage.openFileForWrite("T", "/d/y.tmp", file));
  EXPECT_TRUE(file.rename("/d/y.bin"));
  EXPECT_TRUE(fakesd::has("/d/y.bin"));
  EXPECT_FALSE(fakesd::has("/d/y.tmp"));
}

TEST_F(HarnessTest, ThePsramStubFailsOnRequest) {
  fakepsram::failNext = true;
  EXPECT_FALSE(HalMemory::allocatePsram(8));
  EXPECT_TRUE(HalMemory::allocatePsram(8));  // once
  fakepsram::failAbove = 100;
  EXPECT_TRUE(HalMemory::allocatePsram(100));
  EXPECT_FALSE(HalMemory::allocatePsram(101));
  EXPECT_EQ(fakepsram::liveBlocks, 0u);
}

TEST_F(HarnessTest, TheRendererDoubleRecordsWhatWasAskedAndHonoursTheClip) {
  GfxRenderer renderer;
  renderer.clearScreen();
  EXPECT_EQ(renderer.pixel(0, 0), GfxRenderer::PixelWhite);
  renderer.setClipRect(10, 20, 30, 40);
  renderer.fillRect(0, 0, 100, 100, true);  // asked for far more than the clip
  EXPECT_EQ(renderer.pixel(9, 20), GfxRenderer::PixelWhite);
  EXPECT_EQ(renderer.pixel(10, 20), GfxRenderer::PixelBlack);
  EXPECT_EQ(renderer.pixel(39, 59), GfxRenderer::PixelBlack);
  EXPECT_EQ(renderer.pixel(40, 59), GfxRenderer::PixelWhite);
  EXPECT_EQ(renderer.pixel(39, 60), GfxRenderer::PixelWhite);
  EXPECT_EQ(renderer.pixelCount(GfxRenderer::PixelBlack), 30u * 40u);
  // The request itself is recorded whole, with the clip in force.
  const GfxRenderer::Call& call = renderer.calls.back();
  EXPECT_EQ(call.kind, GfxRenderer::Kind::FillRect);
  EXPECT_EQ(call.w, 100);
  EXPECT_EQ(call.clip, (std::array<int, 4>{10, 20, 30, 40}));
  EXPECT_EQ(renderer.fillsOutside(10, 20, 30, 40), 1u);
  EXPECT_EQ(renderer.fillsOutside(0, 0, 480, 800), 0u);
  EXPECT_EQ(renderer.getClipRect(), (std::array<int, 4>{10, 20, 30, 40}));
}

TEST_F(HarnessTest, TheRendererDoubleFillsDitheredColors) {
  GfxRenderer renderer;
  renderer.fillRectDither(0, 0, 2, 1, Color::LightGray);
  renderer.fillRectDither(2, 0, 2, 1, Color::DarkGray);
  renderer.fillRectDither(4, 0, 1, 1, Color::Black);
  renderer.fillRectDither(5, 0, 1, 1, Color::White);
  renderer.fillRectDither(6, 0, 1, 1, Color::Clear);  // transparent: nothing drawn
  EXPECT_EQ(renderer.pixel(1, 0), GfxRenderer::PixelLight);
  EXPECT_EQ(renderer.pixel(3, 0), GfxRenderer::PixelDark);
  EXPECT_EQ(renderer.pixel(4, 0), GfxRenderer::PixelBlack);
  EXPECT_EQ(renderer.pixel(5, 0), GfxRenderer::PixelWhite);
  EXPECT_EQ(renderer.pixel(6, 0), GfxRenderer::Untouched);
  EXPECT_EQ(renderer.count(GfxRenderer::Kind::FillRectDither), 5u);
}

TEST_F(HarnessTest, LogLinesAreCapturedWithTheirLevel) {
  LOG_ERR("GAME", "bad %d", 5);
  LOG_INF("GAME", "fine");
  ASSERT_EQ(fakelog::lines.size(), 2u);
  EXPECT_EQ(fakelog::lines[0], "ERR GAME: bad 5");
  EXPECT_EQ(fakelog::lines[1], "INF GAME: fine");
}

}  // namespace
