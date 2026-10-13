#include <gtest/gtest.h>

#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "DiagLogBuffer.h"
#include "DiagLogFlush.h"
#include "DiagLogWriter.h"

using DiagLog::DIRECTORY;
using DiagLog::Step;

namespace {

// A card as the writer sees it (DiagLog::FileStore), standing in for Storage / HalFile
// (src/diagnostics/DiagLogStorage.cpp's StorageFileStore). What it copies from the device, pinned by
// FakeStoreMatchesTheDevice below: create makes a missing file and writes at its end; append writes at the end of an
// existing file and answers Missing, creating nothing, for one that is not there (the device opens it without the
// create flag, so the open fails first, before any refusal); listFiles gives file names, not paths, and no directories;
// remove of a missing file is false; listFiles fails only for a missing folder (the device store asks exists() first,
// because the SDK lists a folder it cannot open as empty). Where it is more permissive than the device: no write is
// ever short or slow, the card never unmounts between the operations of one batch (the device can: Storage locks per
// call, so a sleep unmount can land between an open and its write, and that batch fails), and a name is never longer
// than the device's 127.
class FakeStore final : public DiagLog::FileStore {
 public:
  bool ready() override { return mounted; }
  bool makeDirectory(const char* path) override {
    if (failMakeDirectory) return false;
    directories.push_back(path);
    return true;
  }
  bool listFiles(const char* directory, NameSink& sink) override {
    if (failList) return false;
    const std::string prefix = std::string(directory) + "/";
    for (const auto& [path, content] : files) {
      if (path.rfind(prefix, 0) == 0 && path.find('/', prefix.size()) == std::string::npos) {
        sink.name(path.c_str() + prefix.size());
      }
    }
    return true;
  }
  bool remove(const char* path) override {
    removed.push_back(path);
    if (failRemove) return false;
    return files.erase(path) == 1;
  }
  bool create(const char* path, const char* data, size_t length) override { return write(path, data, length); }
  Appended append(const char* path, const char* data, size_t length) override {
    if (files.count(path) == 0 || appendsMissing) {
      appends++;
      return Appended::Missing;
    }
    return write(path, data, length) ? Appended::Written : Appended::Failed;
  }

  // create and append share the failure knobs, and each call of either counts in `appends`.
  bool write(const std::string& path, const char* data, size_t length) {
    appends++;
    if (failAppends > 0) {
      failAppends--;
      return false;
    }
    if (failAppendsAlways) return false;
    // A card that takes a file's header and refuses everything after it.
    if (failBatches && std::strncmp(data, DiagLog::MAGIC, std::strlen(DiagLog::MAGIC)) != 0) return false;
    files[path].append(data, length);
    return true;
  }

  std::string total() const {  // every file's content, oldest name first (b<boot>-s<segment> sorts in order)
    std::string all;
    for (const auto& [path, content] : files) all += content;
    return all;
  }
  size_t bytes() const {
    size_t sum = 0;
    for (const auto& [path, content] : files) sum += content.size();
    return sum;
  }

  bool mounted = true;
  bool failList = false;
  bool failMakeDirectory = false;
  bool failAppendsAlways = false;
  bool failBatches = false;
  bool failRemove = false;
  // A mounted card that refuses opens: the device's append open fails, and its Storage.exists check, itself an open in
  // SdFat, fails too, so a present file answers Missing. Headers (create) still go through.
  bool appendsMissing = false;
  int failAppends = 0;
  int appends = 0;
  std::map<std::string, std::string> files;
  std::vector<std::string> directories;
  std::vector<std::string> removed;
};

uint32_t clockMs = 1000;
uint32_t fakeNow() { return clockMs; }

template <size_t Capacity = 4096>
struct Rig {
  explicit Rig(DiagLog::Limits limits = {}, const char* device = "3F2A")
      : identity{device, "x4pro", "abc1234", "1.0.0-x4pro", "Oct 13 2026 12:00:00"},
        writer(store, buffer, identity, limits, chunk, sizeof(chunk), fakeNow) {
    clockMs = 1000;  // a test that failed an ASSERT before resetting the clock must not leak it to the next
  }

  void log(const std::string& text) { buffer.append(text.data(), text.size()); }
  // Many numbered lines of about `width` bytes.
  std::string lines(int from, int to, size_t width = 60) {
    std::string all;
    for (int i = from; i < to; i++) {
      std::string line = "[" + std::to_string(i) + "] [INF] [T] " + std::to_string(i);
      line.resize(width - 1, '.');
      line += "\n";
      log(line);
      all += line;
    }
    return all;
  }
  static std::string content(const std::string& file) {  // a file without its header
    const size_t end = file.find("# opened_ms ");
    const size_t newline = end == std::string::npos ? 0 : file.find('\n', end) + 1;
    return file.substr(newline);
  }
  std::string allLines() const {
    std::string all;
    for (const auto& [path, file] : store.files) all += content(file);
    return all;
  }

  FakeStore store;
  DiagLog::LineBuffer<Capacity> buffer;
  DiagLog::Identity identity;
  char chunk[512] = {};
  DiagLog::Writer<DiagLog::LineBuffer<Capacity>> writer;
};

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

}  // namespace

TEST(DiagLogWriter, NothingToWriteMakesNoFile) {
  Rig<> rig;
  EXPECT_EQ(rig.writer.pump(), Step::Idle);
  EXPECT_TRUE(rig.store.files.empty());
  EXPECT_TRUE(rig.store.directories.empty());
}

TEST(DiagLogWriter, TheFirstBatchMakesTheFolderAndAFileWhoseHeaderNamesTheDeviceTheFirmwareAndTheBoot) {
  Rig<> rig;
  rig.log("[5] [INF] [MAIN] first\n");
  rig.log("[6] [INF] [MAIN] second\n");
  EXPECT_EQ(rig.writer.pump(), Step::Wrote);
  ASSERT_EQ(rig.store.directories.size(), 1u);
  EXPECT_EQ(rig.store.directories[0], DIRECTORY);
  ASSERT_EQ(rig.store.files.size(), 1u);
  const auto& [path, file] = *rig.store.files.begin();
  EXPECT_EQ(path, std::string(DIRECTORY) + "/b0001-s00-3F2A.log");
  EXPECT_EQ(file,
            "# crosshatch-diag-log 1\n# device 3F2A\n# board x4pro\n# firmware abc1234\n# version 1.0.0-x4pro\n"
            "# built Oct 13 2026 12:00:00\n# boot 1\n# segment 0\n# opened_ms 1000\n"
            "[5] [INF] [MAIN] first\n[6] [INF] [MAIN] second\n");
  EXPECT_EQ(rig.writer.pump(), Step::Idle);
}

TEST(DiagLogWriter, ABootIsNumberedOnePastTheHighestInTheFolderAndForeignFilesAreLeftAlone) {
  Rig<> rig;
  const std::string dir = std::string(DIRECTORY) + "/";
  rig.store.files[dir + "b0007-s03-91C4.log"] = "old\n";
  rig.store.files[dir + "b0002-s00-91C4.log"] = "older\n";
  rig.store.files[dir + "notes.txt"] = "mine\n";
  rig.store.files[dir + "b-s.log"] = "not ours\n";
  rig.store.files["/elsewhere/b0099-s00-X.log"] = "other folder\n";
  rig.log("[1] [INF] [A] x\n");
  EXPECT_EQ(rig.writer.pump(), Step::Wrote);
  EXPECT_EQ(rig.writer.boot(), 8u);
  EXPECT_TRUE(rig.store.files.count(dir + "b0008-s00-3F2A.log"));
  EXPECT_EQ(rig.store.files[dir + "notes.txt"], "mine\n");
  EXPECT_EQ(rig.store.files[dir + "b-s.log"], "not ours\n");
}

TEST(DiagLogWriter, LinesBeforeTheCardMountsWaitAndThenReachTheFileInOrder) {
  Rig<> rig;
  rig.store.mounted = false;
  const std::string sent = rig.lines(0, 12);
  EXPECT_EQ(rig.writer.pump(), Step::NotReady);
  EXPECT_TRUE(rig.store.files.empty());
  rig.store.mounted = true;
  EXPECT_TRUE(rig.writer.flush());
  EXPECT_EQ(rig.allLines(), sent);
}

TEST(DiagLogWriter, ABatchIsAtMostTheChunkAndTheFileHoldsEveryLineInOrder) {
  Rig<8192> rig;
  const std::string sent = rig.lines(0, 100, 70);
  int batches = 0;
  while (rig.writer.pump() == Step::Wrote) batches++;
  EXPECT_GT(batches, 1);
  EXPECT_EQ(rig.allLines(), sent);
}

TEST(DiagLogWriter, ASegmentIsClosedAtTheSizeCapAndTheNextOneStartsWithItsOwnHeader) {
  DiagLog::Limits limits;
  limits.segmentBytes = 1500;
  limits.maxFiles = 100;
  Rig<8192> rig(limits);
  const std::string sent = rig.lines(0, 120, 60);
  EXPECT_TRUE(rig.writer.flush());
  EXPECT_GE(rig.store.files.size(), 4u);
  EXPECT_EQ(rig.writer.stats().segments, rig.store.files.size());
  int expectedSegment = 0;
  for (const auto& [path, file] : rig.store.files) {
    char name[64];
    std::snprintf(name, sizeof(name), "/b0001-s%02d-3F2A.log", expectedSegment);
    EXPECT_EQ(path, std::string(DIRECTORY) + name);
    EXPECT_TRUE(contains(file, "# segment " + std::to_string(expectedSegment) + "\n")) << path;
    EXPECT_TRUE(contains(file, "# device 3F2A\n")) << path;
    EXPECT_LE(file.size(), limits.segmentBytes + rig.writer.MIN_CHUNK) << path;  // a batch may pass the cap, not more
    expectedSegment++;
  }
  EXPECT_EQ(rig.allLines(), sent);
}

TEST(DiagLogWriter, TheOldestFilesAreDeletedSoTheFolderNeverHoldsMoreThanMaxFiles) {
  DiagLog::Limits limits;
  limits.segmentBytes = 1000;
  limits.maxFiles = 3;
  Rig<8192> rig(limits);
  const std::string dir = std::string(DIRECTORY) + "/";
  for (int boot = 1; boot <= 5; boot++) {
    char name[64];
    std::snprintf(name, sizeof(name), "b%04d-s00-3F2A.log", boot);
    rig.store.files[dir + name] = std::string(2000, 'o');
  }
  size_t most = 0;
  for (int round = 0; round < 30; round++) {
    rig.lines(round * 10, round * 10 + 10, 80);
    while (rig.writer.pump() == Step::Wrote) most = std::max(most, rig.store.files.size());
  }
  EXPECT_LE(most, 3u);
  EXPECT_EQ(rig.store.files.size(), 3u);
  // The survivors are the newest: no old boot is left, and the folder never grew past the cap in bytes.
  for (const auto& [path, file] : rig.store.files) EXPECT_TRUE(contains(path, "b0006-")) << path;
  EXPECT_LE(rig.store.bytes(), 3u * (limits.segmentBytes + rig.writer.MIN_CHUNK));
  EXPECT_GT(rig.writer.stats().filesRemoved, 5u);
}

TEST(DiagLogWriter, AWriteFailureIsCountedItsLinesAreReportedLostAndTheNextBatchGoesToANewSegment) {
  Rig<> rig;
  rig.log("[1] [INF] [A] before\n");
  EXPECT_EQ(rig.writer.pump(), Step::Wrote);
  const std::string lost = "[2] [INF] [A] in the failed batch\n";
  rig.log(lost);
  rig.store.failAppends = 1;
  EXPECT_EQ(rig.writer.pump(), Step::Failed);
  EXPECT_EQ(rig.writer.stats().writeFailures, 1u);
  EXPECT_EQ(rig.writer.stats().bytesLost, lost.size());
  rig.log("[3] [INF] [A] after\n");
  clockMs = 4242;
  EXPECT_EQ(rig.writer.pump(), Step::Wrote);
  ASSERT_EQ(rig.store.files.size(), 2u);
  const std::string second = rig.store.files.rbegin()->second;
  EXPECT_TRUE(contains(rig.store.files.rbegin()->first, "-s01-"));
  EXPECT_TRUE(contains(second, "# segment 1\n"));
  EXPECT_TRUE(
      contains(second, "[4242] [ERR] [DIAG] lost " + std::to_string(lost.size()) + " bytes (card write failed)\n"));
  EXPECT_TRUE(contains(second, "[3] [INF] [A] after\n"));
  EXPECT_FALSE(contains(rig.store.total(), "failed batch"));
  EXPECT_EQ(rig.writer.pump(), Step::Idle);
  clockMs = 1000;
}

// Entry 11's smoke run: the owner deleted the card logs from the Wi-Fi file manager mid-run, and the next batch
// remade the file without a header, which `merge` refuses. A batch never makes a file: the deletion costs that batch.
TEST(DiagLogWriter, FilesDeletedBetweenBatchesCostOneBatchAndTheNextFileStartsWithItsHeader) {
  Rig<> rig;
  rig.log("[1] [INF] [A] before\n");
  ASSERT_EQ(rig.writer.pump(), Step::Wrote);
  ASSERT_EQ(rig.store.files.size(), 1u);
  rig.store.files.clear();  // every file in the folder deleted
  const std::string lost = "[2] [INF] [A] in the batch after the delete\n";
  rig.log(lost);
  EXPECT_EQ(rig.writer.pump(), Step::Failed);
  EXPECT_TRUE(rig.store.files.empty());  // the failed batch made no file
  EXPECT_EQ(rig.writer.stats().writeFailures, 1u);
  EXPECT_EQ(rig.writer.stats().bytesLost, lost.size());
  rig.log("[3] [INF] [A] after\n");
  clockMs = 4242;
  EXPECT_EQ(rig.writer.pump(), Step::Wrote);
  ASSERT_EQ(rig.store.files.size(), 1u);
  EXPECT_TRUE(contains(rig.store.files.begin()->first, "b0001-s01-"));
  const std::string file = rig.store.files.begin()->second;
  EXPECT_EQ(file.rfind(std::string(DiagLog::MAGIC) + "\n", 0), 0u) << file;
  EXPECT_TRUE(contains(file, "# segment 1\n"));
  const std::string notice = "[4242] [ERR] [DIAG] lost " + std::to_string(lost.size()) + " bytes (card write failed)\n";
  EXPECT_EQ(Rig<>::content(file), notice + "[3] [INF] [A] after\n");
  EXPECT_EQ(rig.store.directories.size(), 2u);  // the folder is made again before the new file, in case it went too
  EXPECT_EQ(rig.writer.pump(), Step::Idle);
  clockMs = 1000;
}

// The streak rule keeps a card that refuses batches on the file it started; a file that is gone must not hold it there,
// or every later batch of the boot would fail on it.
TEST(DiagLogWriter, AFileDeletedDuringARefusalStreakStartsANewSegmentWhenTheCardRecovers) {
  Rig<8192> rig;
  rig.log("[1] [INF] [A] first\n");
  ASSERT_EQ(rig.writer.pump(), Step::Wrote);  // s00
  rig.store.failBatches = true;
  rig.lines(10, 11);
  EXPECT_EQ(rig.writer.pump(), Step::Failed);  // s00's batch refused: the next goes to s01
  rig.lines(11, 12);
  EXPECT_EQ(rig.writer.pump(), Step::Failed);  // s01's header written, its batch refused: the streak stays on s01
  const std::string dir = std::string(DIRECTORY) + "/";
  ASSERT_TRUE(rig.store.files.count(dir + "b0001-s01-3F2A.log"));
  const uint32_t segmentsBefore = rig.writer.stats().segments;
  rig.store.files.erase(dir + "b0001-s01-3F2A.log");
  rig.store.failBatches = false;  // the card recovers
  rig.lines(12, 13);
  EXPECT_EQ(rig.writer.pump(), Step::Failed);  // s01 is gone: this batch is lost, s01 is not remade
  EXPECT_FALSE(rig.store.files.count(dir + "b0001-s01-3F2A.log"));
  rig.lines(13, 14);
  clockMs = 777;
  EXPECT_TRUE(rig.writer.flush());
  EXPECT_EQ(rig.writer.stats().segments, segmentsBefore + 1);
  ASSERT_TRUE(rig.store.files.count(dir + "b0001-s02-3F2A.log"));
  for (const auto& [path, file] : rig.store.files) {
    EXPECT_EQ(file.rfind(std::string(DiagLog::MAGIC) + "\n", 0), 0u) << path;
  }
  const std::string notice =
      "[777] [ERR] [DIAG] lost " + std::to_string(rig.writer.stats().bytesLost) + " bytes (card write failed)\n";
  EXPECT_TRUE(contains(rig.store.files[dir + "b0001-s02-3F2A.log"], notice));
  EXPECT_EQ(rig.writer.stats().bytesLost, 3u * 60u);  // the three lost batches, one 60-byte line each
  clockMs = 1000;
}

// A card that refuses opens answers Missing for a file that is there; the streak's one extra segment for a gone file
// must not become a new file, and a deletion of the oldest, on every try.
TEST(DiagLogWriter, ACardThatRefusesOpensAndSaysMissingCostsAtMostTwoSegmentsNotAFilePerTry) {
  DiagLog::Limits limits;
  limits.maxFiles = 4;
  Rig<8192> rig(limits);
  const std::string dir = std::string(DIRECTORY) + "/";
  for (int boot = 1; boot <= 3; boot++) {
    char name[64];
    std::snprintf(name, sizeof(name), "b%04d-s00-3F2A.log", boot);
    rig.store.files[dir + name] = "old good log\n";
  }
  rig.log("[1] [INF] [A] first\n");
  ASSERT_EQ(rig.writer.pump(), Step::Wrote);  // b0004-s00
  const uint32_t segmentsBefore = rig.writer.stats().segments;
  const size_t removedBefore = rig.store.removed.size();
  rig.store.appendsMissing = true;
  for (int i = 0; i < 10; i++) {
    rig.lines(i, i + 1);
    EXPECT_EQ(rig.writer.pump(), Step::Failed) << i;
  }
  EXPECT_LE(rig.writer.stats().segments, segmentsBefore + 2);
  EXPECT_LE(rig.store.removed.size(), removedBefore + 2);
  for (const auto& [path, file] : rig.store.files) {
    if (contains(path, "b0004-")) {
      EXPECT_EQ(file.rfind(std::string(DiagLog::MAGIC) + "\n", 0), 0u) << path;
    }
  }
  rig.store.appendsMissing = false;
  EXPECT_TRUE(rig.writer.flush());  // and it recovers
}

TEST(DiagLogWriter, TheFakeAppendAnswersMissingAndMakesNothingWhileCreateMakesTheFile) {
  FakeStore store;
  EXPECT_EQ(store.append("/crosshatch-log/x.log", "a\n", 2), DiagLog::FileStore::Appended::Missing);
  EXPECT_TRUE(store.files.empty());
  EXPECT_TRUE(store.create("/crosshatch-log/x.log", "h\n", 2));
  EXPECT_EQ(store.append("/crosshatch-log/x.log", "a\n", 2), DiagLog::FileStore::Appended::Written);
  store.failAppends = 1;
  EXPECT_EQ(store.append("/crosshatch-log/x.log", "b\n", 2), DiagLog::FileStore::Appended::Failed);
  EXPECT_EQ(store.files["/crosshatch-log/x.log"], "h\na\n");
}

TEST(DiagLogWriter, ACardThatKeepsFailingNeverWedgesTheWriterOrTheBufferAndTheNoticeComesAfterItRecovers) {
  Rig<512> rig;                        // small: a dead card fills it, and the full buffer must drop, not wait
  rig.store.failAppendsAlways = true;  // even the header fails, so nothing leaves the buffer: its lines wait, then drop
  for (int i = 0; i < 20; i++) {
    rig.lines(i * 10, i * 10 + 10, 80);
    const Step step = rig.writer.pump();
    EXPECT_TRUE(step == Step::Failed || step == Step::Idle);
  }
  EXPECT_GT(rig.writer.stats().writeFailures, 5u);
  EXPECT_GT(rig.buffer.droppedTotal(), 0u);
  rig.store.failAppendsAlways = false;
  EXPECT_TRUE(rig.writer.flush());
  const std::string all = rig.store.total();
  EXPECT_TRUE(contains(all, "[ERR] [DIAG] dropped ")) << all;
  EXPECT_EQ(rig.writer.stats().linesDropped, rig.buffer.droppedTotal());
  EXPECT_EQ(rig.writer.stats().bytesLost, 0u);  // a failed header loses no batch: the lines stayed in the buffer
  EXPECT_EQ(rig.writer.segment(), 0u);          // and retrying it does not count up the segment number
}

TEST(DiagLogWriter, ADroppedNoticeSurvivesAFailedBatchIsSaidAgainAndIsNeverCountedAsLostBytes) {
  Rig<512> rig;
  const auto line = [](int i) { return "[" + std::to_string(i) + "] [INF] [A] " + std::string(100, 'z') + "\n"; };
  const size_t lineLength = line(10).size();
  rig.log("[1] [INF] [A] first\n");
  ASSERT_EQ(rig.writer.pump(), Step::Wrote);  // segment 0 exists
  for (int i = 10; i < 22; i++) {
    const std::string text = line(i);
    rig.buffer.append(text.data(), text.size());
  }
  const uint32_t dropped = rig.buffer.droppedTotal();
  ASSERT_GT(dropped, 0u);
  rig.store.failBatches = true;
  EXPECT_EQ(rig.writer.pump(), Step::Failed);
  EXPECT_EQ(rig.writer.stats().bytesLost % lineLength, 0u);  // the notice's bytes are not in it
  EXPECT_GT(rig.writer.stats().bytesLost, 0u);
  EXPECT_EQ(rig.writer.pump(), Step::Failed);  // a second failure in a row: the notice goes into the batch again
  EXPECT_EQ(rig.writer.stats().bytesLost % lineLength, 0u);
  rig.store.failBatches = false;
  clockMs = 555;
  while (rig.writer.pump() == Step::Wrote) {
  }
  const std::string all = rig.store.total();
  const std::string said = "[555] [ERR] [DIAG] dropped " + std::to_string(dropped) + " lines (buffer full)\n";
  ASSERT_TRUE(contains(all, said)) << all;
  EXPECT_EQ(all.find(said), all.rfind(said));  // once, not summed or repeated
  EXPECT_TRUE(contains(
      all, "[555] [ERR] [DIAG] lost " + std::to_string(rig.writer.stats().bytesLost) + " bytes (card write failed)\n"));
}

TEST(DiagLogWriter, AFailedDeleteStopsThePassAndTheNewSegmentStillStartsWithoutSpinning) {
  DiagLog::Limits limits;
  limits.maxFiles = 3;
  Rig<> rig(limits);
  const std::string dir = std::string(DIRECTORY) + "/";
  for (int boot = 1; boot <= 5; boot++) {
    char name[64];
    std::snprintf(name, sizeof(name), "b%04d-s00-3F2A.log", boot);
    rig.store.files[dir + name] = "old\n";
  }
  rig.store.failRemove = true;
  rig.log("[1] [INF] [A] x\n");
  EXPECT_EQ(rig.writer.pump(), Step::Wrote);
  EXPECT_EQ(rig.store.removed.size(), 1u);
  EXPECT_TRUE(rig.store.files.count(dir + "b0006-s00-3F2A.log"));
}

TEST(DiagLogWriter, ACardThatTakesHeadersAndRefusesBatchesCostsOneExtraSegmentNotAFilePerTry) {
  DiagLog::Limits limits;
  limits.maxFiles = 4;
  Rig<8192> rig(limits);
  const std::string dir = std::string(DIRECTORY) + "/";
  for (int boot = 1; boot <= 4; boot++) {
    char name[64];
    std::snprintf(name, sizeof(name), "b%04d-s00-3F2A.log", boot);
    rig.store.files[dir + name] = "old good log\n";
  }
  rig.store.failBatches = true;
  for (int i = 0; i < 10; i++) {
    rig.lines(i, i + 1);
    EXPECT_EQ(rig.writer.pump(), Step::Failed) << i;
  }
  EXPECT_LE(rig.writer.stats().segments, 3u);
  EXPECT_LE(rig.store.removed.size(), rig.writer.stats().segments);
  size_t survivors = 0;
  for (const auto& [path, file] : rig.store.files) survivors += file == "old good log\n" ? 1 : 0;
  EXPECT_GE(survivors, 4u - rig.writer.stats().segments);
  rig.store.failBatches = false;
  EXPECT_TRUE(rig.writer.flush());  // and it recovers
}

TEST(DiagLogWriter, AHeaderTooLongForTheChunkIsReplacedByAMinimalOneNotRetriedForever) {
  FakeStore store;
  DiagLog::LineBuffer<1024> buffer;
  const std::string longName(600, 'v');
  DiagLog::Identity identity{"3F2A", "x4pro", longName.c_str(), longName.c_str(), "built"};
  char chunk[512];
  DiagLog::Writer<DiagLog::LineBuffer<1024>> writer(store, buffer, identity, {}, chunk, sizeof(chunk), fakeNow);
  clockMs = 1000;
  buffer.append("[1] [INF] [A] x\n", 16);
  EXPECT_EQ(writer.pump(), Step::Wrote);
  ASSERT_EQ(store.files.size(), 1u);
  EXPECT_EQ(store.files.begin()->second,
            "# crosshatch-diag-log 1\n# device 3F2A\n# boot 1\n# segment 0\n# opened_ms 1000\n[1] [INF] [A] x\n");
}

TEST(DiagLogWriter, BootAndSegmentNumbersPastTheirPaddedWidthParse) {
  DiagLog::Limits limits;
  limits.maxFiles = 2;
  Rig<> rig(limits);
  const std::string dir = std::string(DIRECTORY) + "/";
  rig.store.files[dir + "b12345-s100-3F2A.log"] = "newer\n";
  rig.store.files[dir + "b12345-s99-3F2A.log"] = "older\n";
  rig.log("[1] [INF] [A] x\n");
  EXPECT_EQ(rig.writer.pump(), Step::Wrote);
  EXPECT_EQ(rig.writer.boot(), 12346u);
  EXPECT_TRUE(rig.store.files.count(dir + "b12346-s00-3F2A.log"));
  ASSERT_EQ(rig.store.removed.size(), 1u);
  EXPECT_EQ(rig.store.removed[0], dir + "b12345-s99-3F2A.log");  // s99 sorts before s100 by number, not by text
}

TEST(DiagLogWriter, TheFolderIsMadeAgainForEveryFileSoOneThatVanishedComesBack) {
  DiagLog::Limits limits;
  limits.segmentBytes = 1500;
  limits.maxFiles = 100;
  Rig<8192> rig(limits);
  rig.lines(0, 120, 60);
  EXPECT_TRUE(rig.writer.flush());
  EXPECT_GE(rig.writer.stats().segments, 4u);
  EXPECT_EQ(rig.store.directories.size(), rig.writer.stats().segments);
}

TEST(DiagLogWriter, LinesTheBufferDroppedAreSaidInTheFileWithTheirCount) {
  Rig<512> rig;
  for (int i = 0; i < 12; i++) rig.log("[" + std::to_string(i) + "] [INF] [A] " + std::string(100, 'z') + "\n");
  const uint32_t dropped = rig.buffer.droppedTotal();
  ASSERT_GT(dropped, 0u);
  clockMs = 777;
  EXPECT_TRUE(rig.writer.flush());
  EXPECT_TRUE(
      contains(rig.store.total(), "[777] [ERR] [DIAG] dropped " + std::to_string(dropped) + " lines (buffer full)\n"));
  clockMs = 1000;
}

TEST(DiagLogWriter, AFailedHeaderIsRetriedAsTheSameSegmentAndTheFirstBatchStillComesOut) {
  Rig<> rig;
  rig.log("[1] [INF] [A] x\n");
  rig.store.failAppends = 1;  // the header's append
  EXPECT_EQ(rig.writer.pump(), Step::Failed);
  EXPECT_EQ(rig.writer.pump(), Step::Wrote);
  ASSERT_EQ(rig.store.files.size(), 1u);
  EXPECT_TRUE(contains(rig.store.files.begin()->first, "b0001-s00-"));
  EXPECT_TRUE(contains(rig.store.files.begin()->second, "[1] [INF] [A] x\n"));
}

TEST(DiagLogWriter, AFolderThatCannotBeMadeOrReadIsAFailureTheNextPumpRetries) {
  Rig<> rig;
  rig.log("[1] [INF] [A] x\n");
  rig.store.failMakeDirectory = true;
  EXPECT_EQ(rig.writer.pump(), Step::Failed);
  rig.store.failMakeDirectory = false;
  rig.store.failList = true;
  EXPECT_EQ(rig.writer.pump(), Step::Failed);
  rig.store.failList = false;
  EXPECT_EQ(rig.writer.pump(), Step::Wrote);
  EXPECT_EQ(rig.writer.boot(), 1u);
}

TEST(DiagLogWriter, FlushWritesEverythingAppendedBeforeItAndSaysNoWhenTheCardIsGone) {
  Rig<8192> rig;
  const std::string sent = rig.lines(0, 60, 90);  // several batches
  EXPECT_TRUE(rig.writer.flush());
  EXPECT_EQ(rig.allLines(), sent);
  EXPECT_EQ(rig.buffer.used(), 0u);
  rig.lines(100, 110);
  rig.store.mounted = false;  // the unmount for sleep
  EXPECT_FALSE(rig.writer.flush());
  EXPECT_GT(rig.buffer.used(), 0u);  // nothing is lost by trying: the lines stay buffered
}

// The platform half of DiagLog::waitUntilDrained, standing in for FlushProbe in lib/DiagLog/DiagLogDevice.cpp (the
// writer task's state and FreeRTOS's clock and delay). The writer "task" runs inside pause(), one batch per call, so a
// test is deterministic; on the device it runs concurrently, so this is more permissive: no batch is ever in flight
// when the caller looks, a writer that was not ready or was refused pumps again at the next pause (the device retries
// every 25 ms while a flush waits, every 500 ms otherwise), and `stalled` is the only way to model a writer that does
// not get to run.
template <class Rig>
struct FlushProbeDouble {
  explicit FlushProbeDouble(Rig& rigIn) : rig(rigIn) {}
  uint32_t nowMs() const { return clockMs; }
  void pause() {
    pauses++;
    clockMs += 5;
    if (stalled) return;
    failing = rig.writer.pump() == Step::Failed;
  }
  void wake() { wakes++; }
  bool cardReady() const { return rig.store.ready(); }
  bool writerBusy() const { return false; }
  bool writerFailing() const { return failing; }

  Rig& rig;
  bool stalled = false;
  bool failing = false;
  int pauses = 0;
  int wakes = 0;
};

TEST(DiagLogFlush, ReturnsOnceEveryBufferedLineIsInTheStoreAndNotBefore) {
  Rig<8192> rig;
  const std::string sent = rig.lines(0, 60, 90);  // several batches
  FlushProbeDouble<Rig<8192>> probe(rig);
  EXPECT_TRUE(DiagLog::waitUntilDrained(rig.buffer, probe, 250));
  EXPECT_EQ(rig.buffer.used(), 0u);
  EXPECT_EQ(rig.allLines(), sent);  // in the store, in order, when it returned
  EXPECT_EQ(probe.wakes, 1);
  EXPECT_LT(clockMs - 1000u, 250u);  // and well inside its bound
}

TEST(DiagLogFlush, AWriterThatDoesNotRunCostsTheBoundAndNoMore) {
  Rig<> rig;
  rig.lines(0, 5);
  FlushProbeDouble<Rig<>> probe(rig);
  probe.stalled = true;
  EXPECT_FALSE(DiagLog::waitUntilDrained(rig.buffer, probe, 250));
  const uint32_t waited = clockMs - 1000u;
  EXPECT_GE(waited, 250u);
  EXPECT_LE(waited, 250u + 5u);      // one pause past the bound at most
  EXPECT_GT(rig.buffer.used(), 0u);  // nothing is lost by trying
}

TEST(DiagLogFlush, ACardThatRefusesEveryWriteSaysNoWithinTheBoundBecauseTheLinesAreGone) {
  Rig<> rig;
  rig.lines(0, 5);
  rig.store.failAppendsAlways = true;  // takes nothing, not even a header
  FlushProbeDouble<Rig<>> probe(rig);
  EXPECT_FALSE(DiagLog::waitUntilDrained(rig.buffer, probe, 250));
  EXPECT_LE(clockMs - 1000u, 250u + 5u);
  EXPECT_EQ(rig.store.bytes(), 0u);
}

TEST(DiagLogFlush, AnUnmountedCardWithLinesBufferedSaysNoAtOnceAndAnEmptyBufferSaysYes) {
  Rig<> rig;
  rig.lines(0, 5);
  rig.store.mounted = false;
  FlushProbeDouble<Rig<>> probe(rig);
  EXPECT_FALSE(DiagLog::waitUntilDrained(rig.buffer, probe, 250));
  EXPECT_EQ(probe.pauses, 0);  // it did not wait the bound for a card that is not there
  EXPECT_GT(rig.buffer.used(), 0u);

  Rig<> idle;
  idle.store.mounted = false;
  FlushProbeDouble<Rig<>> idleProbe(idle);
  EXPECT_TRUE(DiagLog::waitUntilDrained(idle.buffer, idleProbe, 250));  // nothing to lose
}

TEST(DiagLogFlush, TheBoundHoldsAcrossTheClockWrapping) {
  Rig<> rig;
  rig.lines(0, 5);
  clockMs = 0xFFFFFFF0u;
  FlushProbeDouble<Rig<>> probe(rig);
  probe.stalled = true;
  EXPECT_FALSE(DiagLog::waitUntilDrained(rig.buffer, probe, 250));
  EXPECT_GE(static_cast<uint32_t>(clockMs - 0xFFFFFFF0u), 250u);
  EXPECT_LE(static_cast<uint32_t>(clockMs - 0xFFFFFFF0u), 255u);
  clockMs = 1000;
}

// src/main.cpp is not built by the host suite, so the calls the sleep paths make are pinned by their text (the
// ledger's row 14): every Storage.prepareForDeepSleep() has the flush right before it, inside the define's guard, and
// the bound is the one the row names.
TEST(DiagLogFlush, EverySleepPathInMainFlushesJustBeforeItUnmountsTheCard) {
  const std::string main = readFile(std::string(REPO_ROOT_DIR) + "/src/main.cpp");
  ASSERT_FALSE(main.empty());
  const std::string unmount = "Storage.prepareForDeepSleep();";
  int sites = 0;
  for (size_t at = main.find(unmount); at != std::string::npos; at = main.find(unmount, at + 1)) {
    sites++;
    const size_t lineStart = main.rfind('\n', at) + 1;
    const std::string indent = main.substr(lineStart, at - lineStart);
    const std::string before = "#if CROSSHATCH_SD_LOG\n" + indent + "DiagLog::flushBeforeSleep();\n#endif\n";
    ASSERT_GE(lineStart, before.size()) << "site " << sites;
    EXPECT_EQ(main.substr(lineStart - before.size(), before.size()), before) << "site " << sites;
  }
  EXPECT_EQ(sites, 3);
  EXPECT_TRUE(contains(main, "#if CROSSHATCH_SD_LOG\n#include <DiagLog.h>\n#endif\n"));
  EXPECT_TRUE(contains(readFile(std::string(REPO_ROOT_DIR) + "/lib/DiagLog/DiagLog.h"),
                       "constexpr uint32_t SLEEP_FLUSH_TIMEOUT_MS = 250;"));
}

// The [MEM] heap lines' condition in loop() is pinned the same way (row 14, entry 21): with the define on, the `if`
// tests only the period, so a run with no USB host still puts the lines on the card; with it off, upstream's comment
// and condition compile unchanged; and both branches test the same period, taken from upstream's line.
TEST(DiagLogMain, TheHeapLinesNeedNoUsbHostOnlyWhenTheDefineIsOn) {
  const std::string main = readFile(std::string(REPO_ROOT_DIR) + "/src/main.cpp");
  ASSERT_FALSE(main.empty());
  const std::string hostTest = "(Serial || FREEINK_LOG_TRANSPORT == FREEINK_LOG_TRANSPORT_ROM_PRINTF) && ";
  const std::string period = "millis() - lastMemPrint >= 10000) {\n";
  const std::string upstream =
      "#else\n  // The ROM console does not depend on Arduino USB CDC's connection state.\n  if (" + hostTest + period +
      "#endif\n    const auto heap = HalMemory::getInternalHeap();\n";
  const size_t at = main.find(upstream);
  ASSERT_NE(at, std::string::npos) << "upstream's [MEM] comment or condition in loop() changed, or the "
                                      "#else / #endif around it moved: update row 14 and its #if CROSSHATCH_SD_LOG "
                                      "branch to match";
  EXPECT_EQ(main.find(upstream, at + 1), std::string::npos);

  // Between `#if CROSSHATCH_SD_LOG` and `#else`: one comment line and an `if` on the period alone.
  const std::string guard = "#if CROSSHATCH_SD_LOG\n";
  const size_t from = main.rfind(guard, at);
  ASSERT_NE(from, std::string::npos);
  const std::string onBranch = main.substr(from + guard.size(), at - from - guard.size());
  const size_t commentEnd = onBranch.find('\n');
  ASSERT_NE(commentEnd, std::string::npos);
  EXPECT_EQ(onBranch.rfind("  // ", 0), 0u) << "the define-on branch starts with one comment line:\n" << onBranch;
  EXPECT_EQ(onBranch.substr(commentEnd + 1), "  if (" + period)
      << "the define-on `if` tests upstream's period alone, with no host test:\n"
      << onBranch;
}

TEST(DiagLogWriter, ASmallScratchBufferOrAZeroLimitIsAFailureNotACrash) {
  FakeStore store;
  DiagLog::LineBuffer<1024> buffer;
  DiagLog::Identity identity{"3F2A", "x4pro", "f", "v", "b"};
  char small[64];
  DiagLog::Writer<DiagLog::LineBuffer<1024>> tiny(store, buffer, identity, {}, small, sizeof(small), fakeNow);
  EXPECT_EQ(tiny.pump(), Step::Failed);
  char chunk[512];
  DiagLog::Limits none;
  none.maxFiles = 0;
  DiagLog::Writer<DiagLog::LineBuffer<1024>> noFiles(store, buffer, identity, none, chunk, sizeof(chunk), fakeNow);
  EXPECT_EQ(noFiles.pump(), Step::Failed);
}

// The fake above stands in for Storage / HalFile; these pin the facts the writer relies on to the device's own sources
// (read from the working tree, as device_log_test.py reads firmware sources), so a change there fails here.
TEST(DiagLogWriter, FakeStoreMatchesTheDevice) {
  const std::string root = REPO_ROOT_DIR;
  const std::string device = readFile(root + "/src/diagnostics/DiagLogStorage.cpp");
  ASSERT_FALSE(device.empty());
  // create: made when missing, written at the end, closed. append: opened without the create flag, so a missing file
  // fails the open and is not made (SdFat's open reaches its create step only with O_CREAT), and a failed open is
  // Missing only when the file is not there. The flag is in create's open and nowhere else.
  EXPECT_TRUE(contains(device, "Storage.open(path, O_WRONLY | O_CREAT | O_APPEND)"));
  EXPECT_TRUE(contains(device, "Storage.open(path, O_WRONLY | O_APPEND)"));
  EXPECT_EQ(device.find("O_CREAT"), device.rfind("O_CREAT")) << "the create flag belongs only in create's open";
  EXPECT_TRUE(
      contains(device, "return Storage.ready() && !Storage.exists(path) ? Appended::Missing : Appended::Failed;"));
  EXPECT_TRUE(contains(device, "written == length && closed"));
  // the device lists through Storage and checks the card first (SDCardManager prints on an unmounted card).
  EXPECT_TRUE(contains(device, "Storage.listFiles(directory"));
  // listFiles answers an empty vector for a folder it cannot open, so the store asks exists() first and fails on a
  // missing folder: that is the only way the device's listFiles returns false, as FakeStore::failList does.
  EXPECT_TRUE(contains(device, "!Storage.exists(directory)"));
  // The SDK is a submodule that a fresh clone does not have until `git submodule update --init`: skip, not fail.
  const std::string manager = readFile(root + "/freeink-sdk/libs/hardware/SDCardManager/src/SDCardManager.cpp");
  if (manager.empty()) GTEST_SKIP() << "freeink-sdk is not checked out";
  // listFiles: names without the folder, files only (a directory is skipped).
  const size_t list = manager.find("std::vector<String> SDCardManager::listFiles");
  ASSERT_NE(list, std::string::npos);
  const std::string body = manager.substr(list, manager.find("\n}\n", list) - list);
  EXPECT_TRUE(contains(body, "if (f.isDirectory())"));
  EXPECT_TRUE(contains(body, "f.getName(name, sizeof(name));"));
  // a folder it cannot open lists as empty (no error to tell it from an empty folder).
  EXPECT_TRUE(contains(body, "auto root = vol().open(path);\n  if (!root)"));
  // a folder that exists is not an error (ensureDirectoryExists returns true for it).
  EXPECT_TRUE(contains(manager, "bool SDCardManager::ensureDirectoryExists"));
}

TEST(DiagLogWriter, TheNoticeAndHeaderFormatsAreTheOnesTheMergeScriptReads) {
  EXPECT_STREQ(DiagLog::MAGIC, "# crosshatch-diag-log 1");
  EXPECT_STREQ(DiagLog::DROPPED_NOTICE_FORMAT, "[%u] [ERR] [DIAG] dropped %u lines (buffer full)\n");
  EXPECT_STREQ(DiagLog::LOST_NOTICE_FORMAT, "[%u] [ERR] [DIAG] lost %u bytes (card write failed)\n");
  EXPECT_STREQ(DIRECTORY, "/crosshatch-log");
}
