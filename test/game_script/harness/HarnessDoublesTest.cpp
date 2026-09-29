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

TEST_F(HarnessTest, RenamingAFolderMovesItsWholeSubtree) {
  fakesd::addFile("/.games-tmp/demo/main.lua", "m");
  fakesd::addFile("/.games-tmp/demo/assets/logo.bmp", "l");
  fakesd::addDir("/.games");
  ASSERT_TRUE(Storage.rename("/.games-tmp/demo", "/.games/demo"));
  EXPECT_TRUE(fakesd::has("/.games/demo/main.lua"));
  EXPECT_TRUE(fakesd::has("/.games/demo/assets/logo.bmp"));
  EXPECT_FALSE(fakesd::has("/.games-tmp/demo"));
  EXPECT_FALSE(fakesd::has("/.games-tmp/demo/main.lua"));
  EXPECT_EQ(namesIn("/.games/demo"), (std::vector<std::string>{"main.lua", "assets/"}));
  EXPECT_EQ(namesIn("/.games-tmp"), std::vector<std::string>{});
  // A name that only starts like the folder's is not in its subtree.
  fakesd::addFile("/.games-tmp/demo2/x.lua", "x");
  fakesd::addDir("/.games-tmp/demo3");
  ASSERT_TRUE(Storage.rename("/.games-tmp/demo3", "/.games-tmp/demo4"));
  EXPECT_TRUE(fakesd::has("/.games-tmp/demo2/x.lua"));
}

TEST_F(HarnessTest, RenameRefusesAnExistingTargetItsOwnSubtreeAndAFileForAFolder) {
  fakesd::addFile("/a/f.txt", "x");
  fakesd::addDir("/b");
  fakesd::addFile("/file", "x");
  EXPECT_FALSE(Storage.rename("/a", "/b"));            // exists
  EXPECT_FALSE(Storage.rename("/a", "/a/inside"));     // into itself
  EXPECT_FALSE(Storage.rename("/a", "/file/inside"));  // the target's parent is a file
  EXPECT_FALSE(Storage.rename("/a", "/nope/a"));       // no such folder
  EXPECT_FALSE(Storage.rename("/missing", "/c"));
  EXPECT_TRUE(fakesd::has("/a/f.txt"));
}

TEST_F(HarnessTest, RemoveDirRemovesTheSubtreeAndStopsAtTheFirstFailure) {
  fakesd::addFile("/g/a.lua", "a");
  fakesd::addFile("/g/sub/b.bmp", "b");
  fakesd::addFile("/g/sub/deep/c.txt", "c");
  fakesd::addFile("/g/d.lua", "d");
  fakesd::addFile("/keep/x", "x");
  EXPECT_FALSE(Storage.removeDir("/g/a.lua"));  // a file
  EXPECT_FALSE(Storage.removeDir("/none"));
  fakesd::sim().failRemove.insert("/g/sub/b.bmp");
  EXPECT_FALSE(Storage.removeDir("/g"));
  EXPECT_TRUE(fakesd::has("/g"));
  EXPECT_TRUE(fakesd::has("/g/sub/b.bmp"));
  fakesd::sim().failRemove.clear();
  EXPECT_TRUE(Storage.removeDir("/g"));
  EXPECT_FALSE(fakesd::has("/g"));
  EXPECT_FALSE(fakesd::has("/g/sub/deep/c.txt"));
  EXPECT_TRUE(fakesd::has("/keep/x"));
}

TEST_F(HarnessTest, RemovingWhileAFolderIsListedSkipsNothing) {
  for (const char* name : {"a", "b", "c", "d", "e"}) fakesd::addFile(std::string("/f/") + name, "x");
  auto dir = Storage.open("/f");
  dir.rewindDirectory();
  std::vector<std::string> seen;
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    char name[16];
    entry.getName(name, sizeof(name));
    seen.emplace_back(name);
    entry.close();
    ASSERT_TRUE(Storage.remove((std::string("/f/") + name).c_str()));
  }
  EXPECT_EQ(seen, (std::vector<std::string>{"a", "b", "c", "d", "e"}));
  EXPECT_TRUE(Storage.rmdir("/f"));
}

TEST_F(HarnessTest, MkdirFailsOnAPathThatExistsAndEnsureDirectoryExistsDoesNot) {
  fakesd::addDir("/d");
  fakesd::addFile("/file", "x");
  EXPECT_FALSE(Storage.mkdir("/d"));  // SdFat creates with O_EXCL
  EXPECT_FALSE(Storage.mkdir("/file"));
  EXPECT_FALSE(Storage.mkdir("/file/sub"));    // a file for a parent
  EXPECT_FALSE(Storage.mkdir("/x/y", false));  // no parents asked for
  EXPECT_TRUE(Storage.ensureDirectoryExists("/d"));
  EXPECT_FALSE(Storage.ensureDirectoryExists("/file"));
  EXPECT_TRUE(Storage.ensureDirectoryExists("/x/y"));
}

TEST_F(HarnessTest, AFileTakesWritesThroughPrintAndTheModeLimitsWhatItAllows) {
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("T", "/w.bin", file));
  Print& out = file;  // ZipFile::readFileToStream and the converters write to a Print&
  EXPECT_EQ(out.write(static_cast<uint8_t>('a')), 1u);
  const uint8_t more[2] = {'b', 'c'};
  EXPECT_EQ(out.write(more, 2), 2u);
  file.close();
  EXPECT_EQ(fakesd::bytesOf("/w.bin"), (Bytes{'a', 'b', 'c'}));

  uint8_t buffer[4];
  HalFile readOnly = Storage.open("/w.bin", O_RDONLY);
  EXPECT_EQ(readOnly.write(more, 2), 0u);  // a read-only handle stores nothing
  EXPECT_EQ(readOnly.read(buffer, 3), 3);
  HalFile writeOnly = Storage.open("/w.bin", O_WRONLY);
  EXPECT_EQ(writeOnly.read(buffer, 3), -1);
  EXPECT_EQ(writeOnly.write(more, 1), 1u);
  EXPECT_FALSE(Storage.open("/", O_RDWR));
  fakesd::addDir("/dir");
  EXPECT_FALSE(Storage.open("/dir", O_RDWR | O_CREAT));  // a folder does not open for writing
  EXPECT_TRUE(Storage.open("/dir"));
  fakesd::addFile("/plain", "x");
  EXPECT_FALSE(Storage.open("/plain/child", O_RDWR | O_CREAT));  // a file is not a folder
}

TEST_F(HarnessTest, AFileClosesWhenItsHandleGoesOutOfScopeOrIsAssignedOver) {
  fakesd::addFile("/a", "x");
  fakesd::addFile("/b", "y");
  fakesd::sim().failClose.insert("/a");  // close() reports it, a destructor cannot
  {
    HalFile file = Storage.open("/a");
    EXPECT_EQ(fakesd::countOps("close "), 0u);
  }
  EXPECT_EQ(fakesd::countOps("close /a"), 1u);
  HalFile file;
  ASSERT_TRUE(Storage.openFileForRead("T", "/a", file));
  ASSERT_TRUE(Storage.openFileForRead("T", "/b", file));  // assigned over: /a closes
  EXPECT_EQ(fakesd::countOps("close /a"), 2u);
  file.close();
  EXPECT_EQ(fakesd::countOps("close /b"), 1u);
}

TEST_F(HarnessTest, AListingCanFailPartWay) {
  for (const char* name : {"a", "b", "c"}) fakesd::addFile(std::string("/f/") + name, "x");
  fakesd::sim().failListAfter["/f"] = 2;
  EXPECT_EQ(namesIn("/f"), (std::vector<std::string>{"a", "b"}));
  fakesd::sim().failListAfter["/f"] = 0;
  EXPECT_TRUE(namesIn("/f").empty());
  fakesd::sim().failListAfter.clear();
  EXPECT_EQ(namesIn("/f").size(), 3u);
}

TEST_F(HarnessTest, SeekCurMovesRelativeToThePositionAndRefusesAMoveBeforeTheStart) {
  fakesd::addFile("/s.bin", Bytes{0, 1, 2, 3, 4, 5, 6, 7});
  auto file = Storage.open("/s.bin");
  ASSERT_TRUE(file.seekCur(3));
  EXPECT_EQ(file.position(), 3u);
  EXPECT_EQ(file.read(), 3);  // the byte at 3; the position is now 4
  ASSERT_TRUE(file.seekCur(2));
  EXPECT_EQ(file.read(), 6);
  ASSERT_TRUE(file.seekCur(-4));
  EXPECT_EQ(file.read(), 3);
  EXPECT_FALSE(file.seekCur(-100));
  EXPECT_EQ(file.position(), 4u);  // a refused move leaves the position
}

TEST_F(HarnessTest, AReadThatOnlyReachesPastTheEndDoesNotFail) {
  fakesd::addFile("/h.bmp", Bytes(40, 1));
  fakesd::sim().failReadAt["/h.bmp"] = 50;  // past the file's 40 bytes
  uint8_t out[62];
  auto file = Storage.open("/h.bmp");
  EXPECT_EQ(file.read(out, sizeof(out)), 40);
  fakesd::sim().failReadAt["/h.bmp"] = 30;
  auto again = Storage.open("/h.bmp");
  EXPECT_EQ(again.read(out, sizeof(out)), -1);
}

TEST_F(HarnessTest, APathTheFakeDoesNotModelAborts) {
  EXPECT_DEATH(Storage.exists("/a/"), "not normalised");
  EXPECT_DEATH(Storage.exists("a"), "not normalised");
  EXPECT_DEATH(Storage.open("/a//b"), "not normalised");
  EXPECT_TRUE(Storage.exists("/"));
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

TEST_F(HarnessTest, ThePsramStubGivesNullForZeroBytesLikeTheDevice) {
  EXPECT_FALSE(HalMemory::allocatePsram(0));
  EXPECT_EQ(fakepsram::liveBlocks, 0u);
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
