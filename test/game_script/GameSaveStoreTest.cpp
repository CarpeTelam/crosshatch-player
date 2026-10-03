#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <vector>

#include "BlobHeader.h"
#include "Codec.h"
#include "GameSaveStore.h"
#include "HalStorage.h"
#include "HostCaps.h"
#include "Logging.h"
#include "Manifest.h"
#include "Roster.h"
#include "Session.h"
#include "SnapshotMailbox.h"
#include "StoreSlot.h"

// GameSaveStore over the harness's fake SD card (harness/stubs/HalStorage.h): store.bin's layout, the
// restore rules (a valid header and a table payload within 4,096 B, or nothing),
// the tmp-then-rename write, and the flush interval; and resume.bin's layout, its
// peek and load rules, the same write, and the flush a match makes from the VM's
// mailbox; and a pass match's resume.bin (mode 1, its seat count), which only the forms of peek and loadResume
// that take the game's manifest and the host's caps accept.

// peek allocates one snapshot's worth with new (std::nothrow); a test can make that one call fail.
// Not-failing calls behave as the default (malloc-backed, as libstdc++'s is).
namespace {
bool failNextNothrowNew = false;
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
  if (failNextNothrowNew) {
    failNextNothrowNew = false;
    return nullptr;
  }
  return std::malloc(size ? size : 1);
}
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { std::free(pointer); }

namespace {

using Bytes = std::vector<uint8_t>;
using GameScript::StoreSlot;

constexpr const char* STORE = "/.games-data/counter/store.bin";
constexpr const char* TMP = "/.games-data/counter/store.bin.tmp";

// {taps = 3}
const Bytes TAPS3 = {0x06, 0x00, 0x01, 0x05, 0x04, 't', 'a', 'p', 's', 0x03, 0x06};
// {taps = 4}
const Bytes TAPS4 = {0x06, 0x00, 0x01, 0x05, 0x04, 't', 'a', 'p', 's', 0x03, 0x08};

Bytes header(const char* magic = "CHST", const uint8_t fileVersion = 1, const uint8_t codecVersion = 1) {
  Bytes out(magic, magic + 4);
  out.push_back(fileVersion);
  out.push_back(codecVersion);
  return out;
}

Bytes cat(Bytes a, const Bytes& b) {
  a.insert(a.end(), b.begin(), b.end());
  return a;
}

// {s = string.rep('a', n)}: 9 + n bytes for n in 128..16383.
Bytes bigTable(const size_t n) {
  Bytes out = {
      0x06, 0x00, 0x01, 0x05, 0x01, 's', 0x05, static_cast<uint8_t>(0x80 | (n & 0x7F)), static_cast<uint8_t>(n >> 7)};
  out.insert(out.end(), n, 'a');
  return out;
}

constexpr const char* RESUME = "/.games-data/counter/resume.bin";
constexpr const char* RESUME_TMP = "/.games-data/counter/resume.bin.tmp";
using PkgHash = uint8_t[GameSaveStore::PACKAGE_HASH_BYTES];
constexpr PkgHash PKG = {0x05, 0x30, 0xa1, 0x57, 0x66, 0xe9, 0x1b, 0xf1};
constexpr PkgHash OTHER_PKG = {0x05, 0x30, 0xa1, 0x57, 0x66, 0xe9, 0x1b, 0xf2};

// resume.bin's bytes: the blob header, the package hash, mode, seat count, ver (u16 LE), the snapshot.
Bytes resumeFile(const Bytes& snapshot, const uint16_t ver = 7, const PkgHash& hash = PKG, const uint8_t mode = 0,
                 const uint8_t seats = 1, const char* magic = "CHRS", const uint8_t fileVersion = 1,
                 const uint8_t codecVersion = 1) {
  Bytes out = header(magic, fileVersion, codecVersion);
  out.insert(out.end(), hash, hash + sizeof(PkgHash));
  out.push_back(mode);
  out.push_back(seats);
  out.push_back(static_cast<uint8_t>(ver & 0xFF));
  out.push_back(static_cast<uint8_t>(ver >> 8));
  return cat(out, snapshot);
}

Bytes firstBytes(const Bytes& file, const size_t count) { return Bytes(file.begin(), file.begin() + count); }

// The card's operations less its `exists` lines, so a write reads `open <tmp>`, a `write <tmp>` per call,
// `close <tmp>`, `remove <file>`, `rename <tmp> <file>`.
std::vector<std::string> cardOps() {
  std::vector<std::string> kept;
  for (const std::string& op : fakesd::sim().ops) {
    if (op.rfind("exists ", 0) == 0) continue;
    kept.push_back(op);
  }
  return kept;
}

// The write failures every write path is tried with: the open of the tmp, its second write (the one after
// `headBytes` of header, which stores none of its bytes), and its close.
enum class WriteFault : uint8_t { Open, SecondWrite, Close };
constexpr WriteFault WRITE_FAULTS[] = {WriteFault::Open, WriteFault::SecondWrite, WriteFault::Close};
// The header store.bin's and resume.bin's writes put first.
constexpr size_t STORE_HEAD_BYTES = GameScript::BLOB_HEADER_BYTES;
constexpr size_t RESUME_HEAD_BYTES = GameScript::BLOB_HEADER_BYTES + GameSaveStore::PACKAGE_HASH_BYTES + 4;

void failWriteOf(const char* tmp, const WriteFault fault, const size_t headBytes) {
  switch (fault) {
    case WriteFault::Open:
      fakesd::sim().failOpenWrite.insert(tmp);
      return;
    case WriteFault::SecondWrite:
      fakesd::sim().failWriteAt[tmp] = headBytes;
      return;
    case WriteFault::Close:
      fakesd::sim().failClose.insert(tmp);
      return;
  }
}

// The card's failures stay set until cleared (each is per path); this clears every one, and the files stay.
void clearFailures() {
  fakesd::Card& card = fakesd::sim();
  card.failOpen.clear();
  card.failOpenWrite.clear();
  card.failWrite.clear();
  card.failClose.clear();
  card.failRemove.clear();
  card.failRename.clear();
  card.failMkdir.clear();
  card.failRemoveDone.clear();
  card.failReadAt.clear();
  card.shortReadAt.clear();
  card.failListAfter.clear();
  card.failWriteAt.clear();
}

class GameSaveStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fakesd::reset();
    fakelog::lines.clear();
  }

  // A fresh match: a new slot and store, restored at startMs.
  void open(const uint32_t startMs = 1000) {
    slotStorage.assign(GameScript::Codec::STORE_LIMIT, 0);
    buffer.assign(GameSaveStore::BUFFER_BYTES, 0);
    slot = std::make_unique<StoreSlot>(slotStorage.data(), slotStorage.size());
    saves = std::make_unique<GameSaveStore>("counter", buffer, startMs);
    saves->restoreInto(*slot);
  }

  Bytes slotBytes() const {
    Bytes out(GameScript::Codec::STORE_LIMIT);
    out.resize(slot->read(out));
    return out;
  }

  // A fresh match of the package PKG, with a mailbox of its own.
  void openResume(const uint32_t startMs = 1000) {
    open(startMs);
    saves->setPackageHash(PKG);
    mailboxStorage.assign(GameCore::SNAPSHOT_BYTES, 0);
    mailbox = std::make_unique<SnapshotMailbox>(std::span<uint8_t>(mailboxStorage));
  }

  Bytes loaded(uint16_t& ver) {
    bool unreadable = false;
    const std::span<const uint8_t> snapshot = saves->loadResume(ver, unreadable);
    return Bytes(snapshot.begin(), snapshot.end());
  }

  Bytes slotStorage;
  Bytes buffer;
  Bytes mailboxStorage;
  std::unique_ptr<StoreSlot> slot;
  std::unique_ptr<GameSaveStore> saves;
  std::unique_ptr<SnapshotMailbox> mailbox;
};

TEST_F(GameSaveStoreTest, TheBlobHeaderIsTheSharedOne) {
  Bytes written(GameScript::BLOB_HEADER_BYTES);
  GameScript::writeBlobHeader(written.data(), GameSaveStore::STORE_MAGIC, GameSaveStore::STORE_FILE_VERSION);
  EXPECT_EQ(written, header());
}

TEST_F(GameSaveStoreTest, ARoundTripRestoresACleanSlot) {
  open();
  ASSERT_TRUE(slot->post(TAPS3));
  ASSERT_TRUE(saves->flush(*slot, 1001));
  EXPECT_EQ(fakesd::bytesOf(STORE), cat(header(), TAPS3));
  EXPECT_FALSE(fakesd::has(TMP));
  EXPECT_FALSE(slot->dirty());

  open();
  EXPECT_EQ(slotBytes(), TAPS3);
  EXPECT_FALSE(slot->dirty());
  EXPECT_TRUE(fakelog::any("INF GAME: counter: restored ch.store (11 bytes)"));
}

TEST_F(GameSaveStoreTest, NoSaveLeavesTheSlotEmptyWithoutALogLine) {
  open();
  EXPECT_TRUE(slotBytes().empty());
  EXPECT_TRUE(fakelog::lines.empty());
}

TEST_F(GameSaveStoreTest, ABadHeaderIsDiscardedWithALogLineAndKept) {
  const struct {
    Bytes file;
    const char* reason;
  } cases[] = {
      {Bytes{'C', 'H', 'S'}, "truncated"},
      {cat(header("XXXX"), TAPS3), "bad_magic"},
      {cat(header("CHST", 2), TAPS3), "unknown_file_version"},
      {cat(header("CHST", 1, 2), TAPS3), "unknown_codec_version"},
  };
  for (const auto& c : cases) {
    SetUp();
    fakesd::addFile(STORE, c.file);
    open();
    EXPECT_TRUE(slotBytes().empty()) << c.reason;
    EXPECT_TRUE(fakelog::any(std::string("ERR GAME: counter: discarded ") + STORE + ": " + c.reason)) << c.reason;
    EXPECT_EQ(fakesd::bytesOf(STORE), c.file) << c.reason << ": a discarded save is never deleted";
  }
}

TEST_F(GameSaveStoreTest, APayloadThatIsNotAStoreTableIsDiscarded) {
  const struct {
    Bytes payload;
    const char* reason;
  } cases[] = {
      {Bytes{}, "truncated"},
      {cat(TAPS3, Bytes{0x00}), "trailing"},
      {Bytes(TAPS3.begin(), TAPS3.end() - 1), "truncated"},
      {Bytes{0x06, 0x00, 0x01, 0x05, 0x01, 'a', 0x00}, "non_canonical"},
      {Bytes{0x03, 0x02}, "not a table"},
      {Bytes{0x05, 0x01, 'a'}, "not a table"},
  };
  for (const auto& c : cases) {
    SetUp();
    fakesd::addFile(STORE, cat(header(), c.payload));
    open();
    EXPECT_TRUE(slotBytes().empty()) << c.reason;
    EXPECT_TRUE(fakelog::any(std::string("discarded ") + STORE + ": " + c.reason)) << c.reason;
  }
}

TEST_F(GameSaveStoreTest, AStoreAtTheLimitIsRestoredAndOneOverIsDiscarded) {
  const Bytes atLimit = bigTable(GameScript::Codec::STORE_LIMIT - 9);
  ASSERT_EQ(atLimit.size(), GameScript::Codec::STORE_LIMIT);
  fakesd::addFile(STORE, cat(header(), atLimit));
  open();
  EXPECT_EQ(slotBytes(), atLimit);

  SetUp();
  fakesd::addFile(STORE, cat(header(), bigTable(GameScript::Codec::STORE_LIMIT - 8)));
  open();
  EXPECT_TRUE(slotBytes().empty());
  EXPECT_TRUE(fakelog::any(std::string("discarded ") + STORE + ": too large"));
}

TEST_F(GameSaveStoreTest, AWholeTmpIsReadWhenStoreBinIsMissing) {
  fakesd::addFile(TMP, cat(header(), TAPS3));
  open();
  EXPECT_EQ(slotBytes(), TAPS3);
  EXPECT_TRUE(fakelog::any(std::string("no store.bin; reading ") + TMP));
}

TEST_F(GameSaveStoreTest, ATornTmpIsDiscardedAndIgnoredBesideStoreBin) {
  const Bytes torn = cat(header(), Bytes(TAPS4.begin(), TAPS4.begin() + 5));
  fakesd::addFile(TMP, torn);
  open();
  EXPECT_TRUE(slotBytes().empty());
  EXPECT_TRUE(fakelog::any(std::string("discarded ") + TMP + ": truncated"));

  SetUp();
  fakesd::addFile(STORE, cat(header(), TAPS3));
  fakesd::addFile(TMP, torn);
  open();
  EXPECT_EQ(slotBytes(), TAPS3);
  EXPECT_FALSE(fakelog::any("discarded"));
}

TEST_F(GameSaveStoreTest, AWriteGoesThroughTheTmpThenARename) {
  fakesd::addFile(STORE, cat(header(), TAPS3));
  open();
  fakesd::sim().ops.clear();
  ASSERT_TRUE(slot->post(TAPS4));
  ASSERT_TRUE(saves->flush(*slot, 2000));
  const std::vector<std::string> expected = {
      std::string("open ") + TMP,  std::string("write ") + TMP,    std::string("write ") + TMP,
      std::string("close ") + TMP, std::string("remove ") + STORE, std::string("rename ") + TMP + " " + STORE,
  };
  EXPECT_EQ(cardOps(), expected);
  EXPECT_EQ(fakesd::bytesOf(STORE), cat(header(), TAPS4));
  EXPECT_FALSE(fakesd::has(TMP));
}

TEST_F(GameSaveStoreTest, TheFirstWriteCreatesTheFolder) {
  open();
  ASSERT_TRUE(slot->post(TAPS3));
  ASSERT_TRUE(saves->flush(*slot, 2000));
  EXPECT_TRUE(fakesd::isDir("/.games-data/counter"));
  EXPECT_EQ(fakesd::countOps("mkdir "), 1u);
}

TEST_F(GameSaveStoreTest, AFailedWriteRemovesThePartialTmpAndKeepsTheSlotDirty) {
  for (const WriteFault fault : WRITE_FAULTS) {
    SetUp();
    fakesd::addFile(STORE, cat(header(), TAPS3));
    open();
    ASSERT_TRUE(slot->post(TAPS4));
    failWriteOf(TMP, fault, STORE_HEAD_BYTES);
    EXPECT_FALSE(saves->flush(*slot, 2000));
    EXPECT_TRUE(slot->dirty());
    EXPECT_FALSE(fakesd::has(TMP));
    EXPECT_EQ(fakesd::bytesOf(STORE), cat(header(), TAPS3)) << "the previous save stays";
    EXPECT_TRUE(fakelog::any(std::string("ERR GAME: counter: cannot write ") + TMP));

    // The retry writes the latest contents.
    clearFailures();
    ASSERT_TRUE(saves->flush(*slot, 2001));
    EXPECT_EQ(fakesd::bytesOf(STORE), cat(header(), TAPS4));
    EXPECT_FALSE(slot->dirty());
  }
}

TEST_F(GameSaveStoreTest, AFailedRenameKeepsTheWholeTmpForTheNextLoad) {
  fakesd::addFile(STORE, cat(header(), TAPS3));
  open();
  ASSERT_TRUE(slot->post(TAPS4));
  fakesd::sim().failRename.insert(TMP);
  EXPECT_FALSE(saves->flush(*slot, 2000));
  EXPECT_TRUE(slot->dirty());
  EXPECT_FALSE(fakesd::has(STORE));
  EXPECT_EQ(fakesd::bytesOf(TMP), cat(header(), TAPS4));

  open();  // as after a restart
  EXPECT_EQ(slotBytes(), TAPS4);
}

// The retro's R5: a failed rename leaves the tmp as the only copy, and the next
// write must not truncate it before its own write has succeeded.
TEST_F(GameSaveStoreTest, TwoFailuresInARowKeepTheOnlyCopy) {
  for (const WriteFault fault : WRITE_FAULTS) {
    SetUp();
    fakesd::addFile(STORE, cat(header(), TAPS3));
    open();
    ASSERT_TRUE(slot->post(TAPS4));
    fakesd::sim().failRename.insert(TMP);
    EXPECT_FALSE(saves->flush(*slot, 2000));  // store.bin removed, the tmp holds TAPS4
    ASSERT_FALSE(fakesd::has(STORE));
    clearFailures();

    const Bytes taps5 = {0x06, 0x00, 0x01, 0x05, 0x04, 't', 'a', 'p', 's', 0x03, 0x0A};
    ASSERT_TRUE(slot->post(taps5));
    fakesd::sim().ops.clear();
    failWriteOf(TMP, fault, STORE_HEAD_BYTES);
    EXPECT_FALSE(saves->flush(*slot, 2001));
    EXPECT_TRUE(slot->dirty());
    EXPECT_EQ(cardOps().front(), std::string("rename ") + TMP + " " + STORE) << "promoted before the write";
    EXPECT_EQ(fakesd::bytesOf(STORE), cat(header(), TAPS4)) << "the first flush's save stays";

    open();  // as after a restart
    EXPECT_EQ(slotBytes(), TAPS4);
  }
}

TEST_F(GameSaveStoreTest, AFailedPromotionWritesNothingAndKeepsTheTmp) {
  fakesd::addFile(TMP, cat(header(), TAPS3));
  open();
  ASSERT_TRUE(slot->post(TAPS4));
  fakesd::sim().ops.clear();
  fakesd::sim().failRename.insert(TMP);
  EXPECT_FALSE(saves->flush(*slot, 2000));
  EXPECT_TRUE(slot->dirty());
  const std::vector<std::string> expected = {std::string("rename ") + TMP + " " + STORE};
  EXPECT_EQ(cardOps(), expected);
  EXPECT_EQ(fakesd::bytesOf(TMP), cat(header(), TAPS3));
  EXPECT_TRUE(fakelog::any(std::string("cannot rename ") + TMP + " to " + STORE));

  // The retry promotes it, then writes the latest contents.
  clearFailures();
  ASSERT_TRUE(saves->flush(*slot, 2001));
  EXPECT_EQ(fakesd::bytesOf(STORE), cat(header(), TAPS4));
  EXPECT_FALSE(fakesd::has(TMP));
}

TEST_F(GameSaveStoreTest, AFailedRemoveKeepsThePreviousSave) {
  fakesd::addFile(STORE, cat(header(), TAPS3));
  open();
  ASSERT_TRUE(slot->post(TAPS4));
  fakesd::sim().failRemove.insert(STORE);
  EXPECT_FALSE(saves->flush(*slot, 2000));
  EXPECT_TRUE(slot->dirty());
  EXPECT_EQ(fakesd::bytesOf(STORE), cat(header(), TAPS3));
  EXPECT_TRUE(fakelog::any(std::string("cannot replace ") + STORE));
}

// The replace-by-tmp sequence store.bin shares with resume.bin and prefs.bin: the folder, first write, rename and
// leftover-tmp paths.
TEST_F(GameSaveStoreTest, AFolderThatCannotBeCreatedStopsTheWriteBeforeAnyFileIsTouched) {
  open();
  ASSERT_TRUE(slot->post(TAPS3));
  fakesd::sim().failMkdir.insert("/.games-data/counter");
  fakesd::sim().ops.clear();
  EXPECT_FALSE(saves->flush(*slot, 2000));
  EXPECT_TRUE(slot->dirty());
  EXPECT_TRUE(fakelog::any("ERR GAME: counter: cannot create /.games-data/counter"));
  EXPECT_EQ(fakesd::countOps("open "), 0u);
  EXPECT_FALSE(fakesd::has(TMP));
  EXPECT_FALSE(fakesd::has(STORE));
}

TEST_F(GameSaveStoreTest, AFirstWriteHasNoRemoveAndRenamesTheTmpIntoPlace) {
  open();
  ASSERT_TRUE(slot->post(TAPS3));
  fakesd::sim().ops.clear();
  ASSERT_TRUE(saves->flush(*slot, 2000));
  const std::vector<std::string> ops = cardOps();
  ASSERT_FALSE(ops.empty());
  EXPECT_EQ(fakesd::countOps("remove "), 0u);
  EXPECT_EQ(ops.back(), std::string("rename ") + TMP + " " + STORE);
  EXPECT_EQ(fakesd::bytesOf(STORE), cat(header(), TAPS3));
}

TEST_F(GameSaveStoreTest, AFailedRenameAfterTheRemoveIsLoggedAndTheTmpHoldsTheOnlyCopy) {
  fakesd::addFile(STORE, cat(header(), TAPS3));
  open();
  ASSERT_TRUE(slot->post(TAPS4));
  fakesd::sim().failRename.insert(TMP);
  fakesd::sim().ops.clear();
  EXPECT_FALSE(saves->flush(*slot, 2000));
  const std::vector<std::string> expected = {
      std::string("open ") + TMP,  std::string("write ") + TMP,    std::string("write ") + TMP,
      std::string("close ") + TMP, std::string("remove ") + STORE, std::string("rename ") + TMP + " " + STORE,
  };
  EXPECT_EQ(cardOps(), expected);
  EXPECT_TRUE(fakelog::any(std::string("ERR GAME: counter: cannot rename ") + TMP + " to " + STORE));
  EXPECT_FALSE(fakesd::has(STORE));
  EXPECT_EQ(fakesd::bytesOf(TMP), cat(header(), TAPS4));
}

TEST_F(GameSaveStoreTest, ALeftoverTmpBesideStoreBinIsOverwrittenNotPromoted) {
  fakesd::addFile(STORE, cat(header(), TAPS3));
  fakesd::addFile(TMP, Bytes{1, 2, 3});
  open();
  ASSERT_TRUE(slot->post(TAPS4));
  fakesd::sim().ops.clear();
  ASSERT_TRUE(saves->flush(*slot, 2000));
  EXPECT_EQ(cardOps().front(), std::string("open ") + TMP) << "no promotion while store.bin exists";
  EXPECT_EQ(fakesd::bytesOf(STORE), cat(header(), TAPS4));
  EXPECT_FALSE(fakesd::has(TMP));
}

TEST_F(GameSaveStoreTest, FlushIfDueWritesADirtyStoreAtMostEveryFiveSeconds) {
  open(1000);
  ASSERT_TRUE(slot->post(TAPS3));
  EXPECT_TRUE(saves->flushIfDue(*slot, 5999));
  EXPECT_FALSE(fakesd::has(STORE));
  EXPECT_TRUE(saves->flushIfDue(*slot, 6000));
  EXPECT_EQ(fakesd::bytesOf(STORE), cat(header(), TAPS3));

  // The interval runs from the last write.
  ASSERT_TRUE(slot->post(TAPS4));
  fakesd::sim().ops.clear();
  EXPECT_TRUE(saves->flushIfDue(*slot, 10999));
  EXPECT_TRUE(cardOps().empty());
  EXPECT_TRUE(saves->flushIfDue(*slot, 11000));
  EXPECT_EQ(fakesd::bytesOf(STORE), cat(header(), TAPS4));

  // Clean: nothing to write however long it has been.
  fakesd::sim().ops.clear();
  EXPECT_TRUE(saves->flushIfDue(*slot, 60000));
  EXPECT_TRUE(cardOps().empty());
}

TEST_F(GameSaveStoreTest, AFailedDueWriteRetriesAfterTheInterval) {
  open(0);
  ASSERT_TRUE(slot->post(TAPS3));
  fakesd::sim().failOpenWrite.insert(TMP);
  EXPECT_FALSE(saves->flushIfDue(*slot, 5000));
  clearFailures();
  fakesd::sim().ops.clear();
  EXPECT_TRUE(saves->flushIfDue(*slot, 9999));
  EXPECT_TRUE(cardOps().empty());
  EXPECT_TRUE(saves->flushIfDue(*slot, 10000));
  EXPECT_EQ(fakesd::bytesOf(STORE), cat(header(), TAPS3));
}

TEST_F(GameSaveStoreTest, FlushWritesAtOnceAndOnlyWhenDirty) {
  open(1000);
  EXPECT_TRUE(saves->flush(*slot, 1000));
  EXPECT_TRUE(cardOps().empty());
  ASSERT_TRUE(slot->post(TAPS3));
  EXPECT_TRUE(saves->flush(*slot, 1001));
  EXPECT_EQ(fakesd::bytesOf(STORE), cat(header(), TAPS3));
  ASSERT_TRUE(slot->post(TAPS4));
  EXPECT_TRUE(saves->flush(*slot, 1002));
  EXPECT_EQ(fakesd::bytesOf(STORE), cat(header(), TAPS4));
}

TEST_F(GameSaveStoreTest, SaveStoreRefusesAnEmptyOrOversizedStore) {
  open();
  EXPECT_FALSE(saves->saveStore({}));
  const Bytes over(GameScript::Codec::STORE_LIMIT + 1, 0);
  EXPECT_FALSE(saves->saveStore(over));
  EXPECT_TRUE(cardOps().empty());
}

// ---- resume.bin ----

TEST_F(GameSaveStoreTest, TheResumeBlobHeaderIsTheSharedOne) {
  Bytes written(GameScript::BLOB_HEADER_BYTES);
  GameScript::writeBlobHeader(written.data(), GameSaveStore::RESUME_MAGIC, GameSaveStore::RESUME_FILE_VERSION);
  EXPECT_EQ(written, header("CHRS"));
  EXPECT_EQ(GameSaveStore::PACKAGE_HASH_BYTES, 8u);
}

TEST_F(GameSaveStoreTest, AResumeRoundTripWritesTheExactBytesAndPeekLoadAgree) {
  openResume();
  ASSERT_TRUE(saves->saveResume(TAPS3, 7));
  const Bytes expected = {'C',  'H',  'R',  'S',  1,    1,                 // blob header
                          0x05, 0x30, 0xa1, 0x57, 0x66, 0xe9, 0x1b, 0xf1,  // package hash
                          0,    1,    7,    0,                             // mode, seats, ver (u16 LE)
                          0x06, 0x00, 0x01, 0x05, 0x04, 't',  'a',  'p',  's', 0x03, 0x06};
  EXPECT_EQ(fakesd::bytesOf(RESUME), expected);
  EXPECT_EQ(fakesd::bytesOf(RESUME), resumeFile(TAPS3));
  EXPECT_FALSE(fakesd::has(RESUME_TMP));
  EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::Valid);
  uint16_t ver = 99;
  EXPECT_EQ(loaded(ver), TAPS3);
  EXPECT_EQ(ver, 7u);
  EXPECT_FALSE(fakelog::any("ERR"));
}

TEST_F(GameSaveStoreTest, AResumeWriteGoesThroughTheTmpThenARename) {
  fakesd::addFile(RESUME, resumeFile(TAPS3, 1));
  openResume();
  fakesd::sim().ops.clear();
  ASSERT_TRUE(saves->saveResume(TAPS4, 2));
  const std::vector<std::string> expected = {
      std::string("open ") + RESUME_TMP,  std::string("write ") + RESUME_TMP,
      std::string("write ") + RESUME_TMP, std::string("close ") + RESUME_TMP,
      std::string("remove ") + RESUME,    std::string("rename ") + RESUME_TMP + " " + RESUME,
  };
  EXPECT_EQ(cardOps(), expected);
  EXPECT_EQ(fakesd::bytesOf(RESUME), resumeFile(TAPS4, 2));
}

TEST_F(GameSaveStoreTest, VerIsKeptToItsLow16Bits) {
  openResume();
  ASSERT_TRUE(saves->saveResume(TAPS3, 0x12345));
  EXPECT_EQ(fakesd::bytesOf(RESUME), resumeFile(TAPS3, 0x2345));
  uint16_t ver = 0;
  EXPECT_EQ(loaded(ver), TAPS3);
  EXPECT_EQ(ver, 0x2345u);

  ASSERT_TRUE(saves->saveResume(TAPS3, 65536));
  EXPECT_EQ(fakesd::bytesOf(RESUME), resumeFile(TAPS3, 0));
  EXPECT_EQ(loaded(ver), TAPS3);
  EXPECT_EQ(ver, 0u);
}

TEST_F(GameSaveStoreTest, ASnapshotAtTheLimitIsKeptAndOneOverIsRefused) {
  openResume();
  const Bytes atLimit = bigTable(GameCore::SNAPSHOT_BYTES - 9);
  ASSERT_EQ(atLimit.size(), GameCore::SNAPSHOT_BYTES);
  ASSERT_TRUE(saves->saveResume(atLimit, 3));
  uint16_t ver = 0;
  EXPECT_EQ(loaded(ver), atLimit);
  EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::Valid);

  fakesd::sim().ops.clear();
  EXPECT_FALSE(saves->saveResume(bigTable(GameCore::SNAPSHOT_BYTES - 8), 4));
  EXPECT_FALSE(saves->saveResume({}, 4));
  EXPECT_TRUE(cardOps().empty());
  EXPECT_EQ(fakesd::bytesOf(RESUME), resumeFile(atLimit, 3));
}

TEST_F(GameSaveStoreTest, NothingIsSavedOrLoadedBeforeThePackageHashIsKnown) {
  open();
  fakesd::addFile(RESUME, resumeFile(TAPS3));
  fakesd::sim().ops.clear();
  EXPECT_FALSE(saves->saveResume(TAPS4, 8));
  uint16_t ver = 5;
  EXPECT_TRUE(loaded(ver).empty());
  EXPECT_EQ(ver, 0u);
  EXPECT_TRUE(cardOps().empty());
  EXPECT_EQ(fakesd::bytesOf(RESUME), resumeFile(TAPS3));
}

TEST_F(GameSaveStoreTest, AResumeThatDoesNotFitTheGameIsDiscardedWithALogLineAndKept) {
  const struct {
    Bytes file;
    const char* reason;
  } cases[] = {
      {Bytes{'C', 'H', 'R'}, "truncated"},
      {firstBytes(resumeFile(TAPS3), 12), "truncated"},
      {firstBytes(resumeFile(TAPS3), 17), "truncated"},
      {resumeFile(TAPS3, 7, PKG, 0, 1, "XXXX"), "bad_magic"},
      // An older or garbage version is malformed; a newer one, or a snapshot over the limit, or an unknown mode byte,
      // is a later firmware's save this host cannot start (ALaterFirmwaresSaveIsUnstartableAndKept).
      {resumeFile(TAPS3, 7, PKG, 0, 1, "CHRS", 0), "unknown_file_version"},
      {resumeFile(TAPS3, 7, PKG, 0, 1, "CHRS", 1, 0), "unknown_codec_version"},
      {resumeFile(TAPS3, 7, PKG, 0, 2), "bad seat count"},
      {resumeFile(TAPS3, 7, PKG, 0, 0), "bad seat count"},
      {resumeFile(Bytes{}), "empty snapshot"},
  };
  for (const auto& c : cases) {
    SetUp();
    fakesd::addFile(RESUME, c.file);
    openResume();
    EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::None) << c.reason;
    EXPECT_TRUE(fakelog::any(std::string("ERR GAME: counter: discarded ") + RESUME + ": " + c.reason)) << c.reason;
    EXPECT_EQ(std::count_if(fakelog::lines.begin(), fakelog::lines.end(),
                            [](const std::string& line) { return line.find("discarded") != std::string::npos; }),
              1)
        << c.reason << ": one log line";
    fakelog::lines.clear();
    uint16_t ver = 0;
    EXPECT_TRUE(loaded(ver).empty()) << c.reason;
    EXPECT_TRUE(fakelog::any(std::string("discarded ") + RESUME + ": " + c.reason)) << c.reason;
    EXPECT_EQ(fakesd::bytesOf(RESUME), c.file) << c.reason << ": a discarded save is never deleted";
  }
}

TEST_F(GameSaveStoreTest, ASnapshotThatIsNotCanonicalCodecBytesIsDiscardedByPeekAndLoadAlike) {
  fakesd::addFile(RESUME, resumeFile(cat(TAPS3, Bytes{0x00})));
  openResume();
  EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::None)
      << "Continue is offered only for a save that resumes";
  EXPECT_TRUE(fakelog::any(std::string("discarded ") + RESUME + ": trailing"));
  fakelog::lines.clear();
  uint16_t ver = 0;
  EXPECT_TRUE(loaded(ver).empty());
  EXPECT_TRUE(fakelog::any(std::string("discarded ") + RESUME + ": trailing"));
  EXPECT_EQ(fakesd::bytesOf(RESUME), resumeFile(cat(TAPS3, Bytes{0x00}))) << "and the file stays";
}

TEST_F(GameSaveStoreTest, PeekIsUnreadableWithALogLineWhenItCannotAllocateItsBufferAndTheFileStays) {
  fakesd::addFile(RESUME, resumeFile(TAPS3));
  openResume();
  ASSERT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::Valid)
      << "the same save is valid when memory is there";
  fakelog::lines.clear();
  failNextNothrowNew = true;
  // Not None: a file is there that could not be checked, and the title screen must not take it for no save.
  EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::Unreadable);
  failNextNothrowNew = false;
  EXPECT_TRUE(fakelog::any(std::string("ERR GAME: counter: OOM: 1400 bytes to check ") + RESUME));
  EXPECT_EQ(fakesd::bytesOf(RESUME), resumeFile(TAPS3));
  EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::Valid) << "and the next call is fine";
}

// Retro deferral 4.12 (ADV2): "no save" and "could not tell" are different answers. A save whose file would not
// open or read is Unreadable, in peek and in loadResume alike; a save that was read and refused is None.
TEST_F(GameSaveStoreTest, ASaveThatCannotBeOpenedOrReadIsUnreadableNotAbsentAndStays) {
  // The open fails (failOpen), or the first read does (failReadAt 0), as an SD error makes it.
  for (const bool failOpen : {true, false}) {
    SetUp();
    fakesd::addFile(RESUME, resumeFile(TAPS3));
    openResume();
    const auto fail = [failOpen] {
      if (failOpen) {
        fakesd::sim().failOpen.insert(RESUME);
      } else {
        fakesd::sim().failReadAt[RESUME] = 0;
      }
    };
    fail();
    EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::Unreadable);
    EXPECT_TRUE(fakelog::any(std::string("ERR GAME: counter: could not check ") + RESUME + ": cannot "));
    EXPECT_TRUE(fakelog::any("the file is kept"));
    clearFailures();
    EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::Valid) << "and the next try reads it";

    fail();
    uint16_t ver = 5;
    bool unreadable = false;
    EXPECT_TRUE(saves->loadResume(ver, unreadable).empty());
    EXPECT_TRUE(unreadable);
    EXPECT_EQ(ver, 0u);
    EXPECT_EQ(fakesd::bytesOf(RESUME), resumeFile(TAPS3)) << "the file is untouched";
    clearFailures();
    const std::span<const uint8_t> again = saves->loadResume(ver, unreadable);
    EXPECT_EQ(Bytes(again.begin(), again.end()), TAPS3) << "and the next try reads it";
    EXPECT_FALSE(unreadable);
  }
  // A file that was read and is not a save is None, and loadResume says it was no fault.
  SetUp();
  fakesd::addFile(RESUME, resumeFile(TAPS3, 7, OTHER_PKG));
  openResume();
  bool unreadable = true;
  uint16_t ver = 0;
  EXPECT_TRUE(saves->loadResume(ver, unreadable).empty());
  EXPECT_FALSE(unreadable);
  EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::None);
}

// The title screen asks again each time it opens, and a save of another package stays, so peek does not log it as an
// error; the match that would resume it still does (loadResume).
TEST_F(GameSaveStoreTest, ASaveOfAnotherPackageIsLoggedQuietlyByPeekAndAtErrorByLoad) {
  fakesd::addFile(RESUME, resumeFile(TAPS3, 7, OTHER_PKG));
  openResume();
  EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::None);
  EXPECT_TRUE(fakelog::any(std::string("INF GAME: counter: ") + RESUME + " is another package's save"));
  EXPECT_FALSE(fakelog::any("ERR"));
  fakelog::lines.clear();
  uint16_t ver = 0;
  EXPECT_TRUE(loaded(ver).empty());
  EXPECT_TRUE(fakelog::any(std::string("ERR GAME: counter: discarded ") + RESUME + ": other package"));
  EXPECT_EQ(fakesd::bytesOf(RESUME), resumeFile(TAPS3, 7, OTHER_PKG)) << "and the file stays";
}

TEST_F(GameSaveStoreTest, NoResumeIsNotAnError) {
  openResume();
  EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::None);
  uint16_t ver = 0;
  EXPECT_TRUE(loaded(ver).empty());
  EXPECT_TRUE(fakelog::lines.empty());
}

TEST_F(GameSaveStoreTest, AWholeResumeTmpIsReadWhenResumeBinIsMissing) {
  fakesd::addFile(RESUME_TMP, resumeFile(TAPS3, 9));
  openResume();
  EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::Valid);
  uint16_t ver = 0;
  EXPECT_EQ(loaded(ver), TAPS3);
  EXPECT_EQ(ver, 9u);
  EXPECT_TRUE(fakelog::any(std::string("no resume.bin; reading ") + RESUME_TMP));
}

TEST_F(GameSaveStoreTest, ATornResumeTmpIsDiscardedAndIgnoredBesideResumeBin) {
  const Bytes torn = firstBytes(resumeFile(TAPS4), 9);
  fakesd::addFile(RESUME_TMP, torn);
  openResume();
  EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::None);
  uint16_t ver = 0;
  EXPECT_TRUE(loaded(ver).empty());

  SetUp();
  fakesd::addFile(RESUME, resumeFile(TAPS3));
  fakesd::addFile(RESUME_TMP, torn);
  openResume();
  EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::Valid);
  EXPECT_EQ(loaded(ver), TAPS3);
  EXPECT_FALSE(fakelog::any("discarded"));
}

TEST_F(GameSaveStoreTest, AFailedResumeWriteKeepsThePreviousSaveAndLeavesNoTmp) {
  for (const WriteFault fault : WRITE_FAULTS) {
    SetUp();
    fakesd::addFile(RESUME, resumeFile(TAPS3, 1));
    openResume();
    failWriteOf(RESUME_TMP, fault, RESUME_HEAD_BYTES);
    EXPECT_FALSE(saves->saveResume(TAPS4, 2));
    EXPECT_FALSE(fakesd::has(RESUME_TMP));
    EXPECT_EQ(fakesd::bytesOf(RESUME), resumeFile(TAPS3, 1)) << "the previous save stays";
    EXPECT_TRUE(fakelog::any(std::string("ERR GAME: counter: cannot write ") + RESUME_TMP));
  }
}

// GameMatchActivity clears a pending Over delete when this moves: the finished round's file is gone (or replaced), so a
// delete from then on could only take the new round's save.
TEST_F(GameSaveStoreTest, TheReplacementCounterMovesWhenTheOldResumeFileIsGoneAndNotWhenItStays) {
  fakesd::addFile(RESUME, resumeFile(TAPS3, 1));
  openResume();
  EXPECT_EQ(saves->resumeReplacements(), 0u);
  fakesd::sim().failOpenWrite.insert(RESUME_TMP);
  EXPECT_FALSE(saves->saveResume(TAPS4, 2));
  EXPECT_EQ(saves->resumeReplacements(), 0u) << "the previous save stayed";
  clearFailures();
  fakesd::sim().failRename.insert(RESUME_TMP);
  EXPECT_FALSE(saves->saveResume(TAPS4, 2));
  EXPECT_FALSE(fakesd::has(RESUME));
  EXPECT_EQ(saves->resumeReplacements(), 1u) << "the write removed the old file before its rename failed";
  clearFailures();
  EXPECT_TRUE(saves->saveResume(TAPS4, 3));
  EXPECT_EQ(saves->resumeReplacements(), 2u);
}

TEST_F(GameSaveStoreTest, AFailedResumeRenameKeepsTheWholeTmpAsTheOnlyCopy) {
  fakesd::addFile(RESUME, resumeFile(TAPS3, 1));
  openResume();
  fakesd::sim().failRename.insert(RESUME_TMP);
  EXPECT_FALSE(saves->saveResume(TAPS4, 2));
  EXPECT_FALSE(fakesd::has(RESUME));
  EXPECT_EQ(fakesd::bytesOf(RESUME_TMP), resumeFile(TAPS4, 2));

  // A second failure does not truncate it: it is promoted before the write.
  for (const WriteFault fault : WRITE_FAULTS) {
    clearFailures();
    fakesd::sim().ops.clear();
    failWriteOf(RESUME_TMP, fault, RESUME_HEAD_BYTES);
    EXPECT_FALSE(saves->saveResume(TAPS3, 3));
    EXPECT_EQ(cardOps().front(), std::string("rename ") + RESUME_TMP + " " + RESUME);
    EXPECT_EQ(fakesd::bytesOf(RESUME), resumeFile(TAPS4, 2));
    // Put it back as a lone tmp for the next failure.
    fakesd::addFile(RESUME_TMP, fakesd::bytesOf(RESUME));
    fakesd::removeEntry(RESUME);
  }
  clearFailures();
  uint16_t ver = 0;
  EXPECT_EQ(loaded(ver), TAPS4);
  EXPECT_EQ(ver, 2u);
}

TEST_F(GameSaveStoreTest, DeleteResumeRemovesTheFileAndItsTmp) {
  fakesd::addFile(RESUME, resumeFile(TAPS3));
  fakesd::addFile(RESUME_TMP, resumeFile(TAPS4));
  fakesd::addFile("/.games-data/counter/store.bin", cat(header(), TAPS3));
  openResume();
  EXPECT_TRUE(saves->deleteResume());
  EXPECT_FALSE(fakesd::has(RESUME));
  EXPECT_FALSE(fakesd::has(RESUME_TMP));
  EXPECT_TRUE(fakesd::has("/.games-data/counter/store.bin")) << "store.bin is not resume.bin";
  EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::None);

  fakesd::sim().ops.clear();
  EXPECT_TRUE(saves->deleteResume()) << "nothing to delete is done";
  EXPECT_TRUE(cardOps().empty());
}

TEST_F(GameSaveStoreTest, DeleteResumeDoesNothingWithoutThePackageHash) {
  fakesd::addFile(RESUME, resumeFile(TAPS3));
  fakesd::addFile(RESUME_TMP, resumeFile(TAPS4));
  open();  // no setPackageHash: this match could not tell whose save it is
  fakesd::sim().ops.clear();
  EXPECT_TRUE(saves->deleteResume());
  EXPECT_TRUE(cardOps().empty());
  EXPECT_EQ(fakesd::bytesOf(RESUME), resumeFile(TAPS3));
  EXPECT_EQ(fakesd::bytesOf(RESUME_TMP), resumeFile(TAPS4));
}

TEST_F(GameSaveStoreTest, AFailedDeleteIsLoggedAndReported) {
  fakesd::addFile(RESUME, resumeFile(TAPS3));
  openResume();
  fakesd::sim().failRemove.insert(RESUME);
  EXPECT_FALSE(saves->deleteResume());
  EXPECT_TRUE(fakesd::has(RESUME));
  EXPECT_TRUE(fakelog::any(std::string("ERR GAME: counter: cannot delete ") + RESUME));
}

// ---- flushResume: what a match writes from the VM's mailbox ----

TEST_F(GameSaveStoreTest, FlushResumeWritesTheLatestPublishedSnapshotOnceAndAtOnce) {
  openResume(1000);
  EXPECT_TRUE(saves->flushResume(*mailbox, 1000));
  EXPECT_TRUE(cardOps().empty()) << "nothing published, nothing written";

  ASSERT_TRUE(mailbox->publish(TAPS3, 5, false));
  ASSERT_TRUE(mailbox->publish(TAPS4, 6, false));  // latest wins
  EXPECT_TRUE(saves->flushResume(*mailbox, 1001)) << "not held back by the store's 5 s interval";
  EXPECT_EQ(fakesd::bytesOf(RESUME), resumeFile(TAPS4, 6));
  EXPECT_FALSE(mailbox->pending());

  fakesd::sim().ops.clear();
  EXPECT_TRUE(saves->flushResume(*mailbox, 1002));
  EXPECT_TRUE(cardOps().empty()) << "written once";

  // Successive successes are never throttled.
  ASSERT_TRUE(mailbox->publish(TAPS3, 7, false));
  EXPECT_TRUE(saves->flushResume(*mailbox, 1003));
  EXPECT_EQ(fakesd::bytesOf(RESUME), resumeFile(TAPS3, 7));
}

TEST_F(GameSaveStoreTest, FlushResumeWritesNothingWithoutAPackageHash) {
  open();
  mailboxStorage.assign(GameCore::SNAPSHOT_BYTES, 0);
  SnapshotMailbox bare{std::span<uint8_t>(mailboxStorage)};
  ASSERT_TRUE(bare.publish(TAPS3, 5, false));
  EXPECT_TRUE(saves->flushResume(bare, 1000));
  EXPECT_TRUE(cardOps().empty());
  EXPECT_TRUE(fakesd::sim().entries.empty());
}

// Play again forgets the backoff a failed write or delete of the finished round armed, so the new round's first
// snapshot is not held back for the interval.
TEST_F(GameSaveStoreTest, ClearingTheBackoffLetsTheNextSnapshotThroughAtOnce) {
  openResume(0);
  ASSERT_TRUE(mailbox->publish(TAPS3, 5, false));
  fakesd::sim().failOpenWrite.insert(RESUME_TMP);
  EXPECT_FALSE(saves->flushResume(*mailbox, 2000));
  clearFailures();
  saves->clearResumeBackoff();
  fakesd::sim().ops.clear();
  EXPECT_TRUE(saves->flushResume(*mailbox, 2001));
  EXPECT_FALSE(mailbox->pending());
  EXPECT_EQ(fakesd::bytesOf(RESUME), resumeFile(TAPS3, 5));
}

TEST_F(GameSaveStoreTest, AFailedResumeWriteStaysPendingAndIsRetriedOnlyAfterTheInterval) {
  openResume(0);
  ASSERT_TRUE(mailbox->publish(TAPS3, 5, false));
  fakesd::sim().failOpenWrite.insert(RESUME_TMP);
  EXPECT_FALSE(saves->flushResume(*mailbox, 2000));
  EXPECT_TRUE(mailbox->pending());
  clearFailures();

  fakesd::sim().ops.clear();
  EXPECT_TRUE(saves->flushResume(*mailbox, 2001));
  EXPECT_TRUE(saves->flushResume(*mailbox, 6999));
  EXPECT_TRUE(cardOps().empty()) << "held back until the interval has passed";
  EXPECT_TRUE(mailbox->pending());

  // A newer snapshot meanwhile is what the retry writes.
  ASSERT_TRUE(mailbox->publish(TAPS4, 6, false));
  EXPECT_TRUE(saves->flushResume(*mailbox, 7000));
  EXPECT_EQ(fakesd::bytesOf(RESUME), resumeFile(TAPS4, 6));
  EXPECT_FALSE(mailbox->pending());

  // The throttle is over once a write has succeeded.
  ASSERT_TRUE(mailbox->publish(TAPS3, 7, false));
  EXPECT_TRUE(saves->flushResume(*mailbox, 7001));
  EXPECT_EQ(fakesd::bytesOf(RESUME), resumeFile(TAPS3, 7));
}

TEST_F(GameSaveStoreTest, ASnapshotWhoseStatusIsOverDeletesTheSaveInsteadOfBeingWritten) {
  fakesd::addFile(RESUME, resumeFile(TAPS3, 1));
  fakesd::addFile(RESUME_TMP, resumeFile(TAPS3, 1));
  openResume();
  ASSERT_TRUE(mailbox->publish(TAPS4, 2, true));
  fakesd::sim().ops.clear();
  EXPECT_TRUE(saves->flushResume(*mailbox, 1001));
  EXPECT_FALSE(fakesd::has(RESUME));
  EXPECT_FALSE(fakesd::has(RESUME_TMP));
  EXPECT_EQ(fakesd::countOps("write "), 0u) << "an over snapshot is never written";
  EXPECT_FALSE(mailbox->pending());
  EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::None);
}

TEST_F(GameSaveStoreTest, AFailedDeleteForAnOverSnapshotIsRetriedLikeAFailedWrite) {
  fakesd::addFile(RESUME, resumeFile(TAPS3, 1));
  openResume(0);
  ASSERT_TRUE(mailbox->publish(TAPS4, 2, true));
  fakesd::sim().failRemove.insert(RESUME);
  EXPECT_FALSE(saves->flushResume(*mailbox, 2000));
  EXPECT_TRUE(mailbox->pending());
  EXPECT_TRUE(fakesd::has(RESUME));
  clearFailures();
  EXPECT_TRUE(saves->flushResume(*mailbox, 3000)) << "throttled";
  EXPECT_TRUE(fakesd::has(RESUME));
  EXPECT_TRUE(saves->flushResume(*mailbox, 7000));
  EXPECT_FALSE(fakesd::has(RESUME));
}

TEST_F(GameSaveStoreTest, ANewRoundsSnapshotIsWrittenAgainAfterAnOverOne) {
  openResume();
  ASSERT_TRUE(mailbox->publish(TAPS3, 5, true));
  EXPECT_TRUE(saves->flushResume(*mailbox, 1001));
  EXPECT_FALSE(fakesd::has(RESUME));
  ASSERT_TRUE(mailbox->publish(TAPS4, 6, false));
  EXPECT_TRUE(saves->flushResume(*mailbox, 1002));
  EXPECT_EQ(fakesd::bytesOf(RESUME), resumeFile(TAPS4, 6));
}

// ---- pass saves: the forms that take the game's manifest and the host's caps ----

using GameCore::HostCaps;
using GameCore::Manifest;
using GameCore::Mode;
using GameCore::Roster;

constexpr uint8_t SOLO_BYTE = 0;
constexpr uint8_t PASS_BYTE = 1;

// "counter" with seats `seatsMin`..`seatsMax`, and `modes` (solo and pass, seats 1..3, unless said otherwise).
Manifest counterGame(const uint8_t modes = Manifest::MODE_SOLO | Manifest::MODE_PASS, const int32_t seatsMin = 1,
                     const int32_t seatsMax = 3) {
  Manifest game;
  std::strcpy(game.id, "counter");
  std::strcpy(game.name, "Counter");
  std::strcpy(game.version, "1.0");
  game.api = 1;
  game.seatsMin = seatsMin;
  game.seatsMax = seatsMax;
  game.modes = modes;
  return game;
}

HostCaps hostOf(const int32_t maxSeats, const bool pass) {
  HostCaps host;
  host.api = 1;
  host.minApi = 1;
  host.maxSeats = maxSeats;
  host.pass = pass;
  return host;
}

const HostCaps PASS_HOST = hostOf(4, true);

bool sameRoster(const Roster& a, const Roster& b) {
  return a.mode == b.mode && a.seats == b.seats && a.localSeats == b.localSeats && a.api == b.api;
}

TEST_F(GameSaveStoreTest, WithoutARosterASoloSaveIsTodaysBytesAndBothFormsTakeIt) {
  openResume();
  ASSERT_TRUE(saves->saveResume(TAPS3, 7));
  EXPECT_EQ(fakesd::bytesOf(RESUME), resumeFile(TAPS3, 7, PKG, SOLO_BYTE, 1));
  EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::Valid);
  EXPECT_EQ(GameSaveStore::peek(counterGame(), PKG, PASS_HOST), GameSaveStore::SaveState::Valid);
  uint16_t ver = 0;
  bool unreadable = true;
  Roster saved = Roster::pass(3);
  const std::span<const uint8_t> snapshot = saves->loadResume(ver, unreadable, counterGame(), PASS_HOST, saved);
  EXPECT_EQ(Bytes(snapshot.begin(), snapshot.end()), TAPS3);
  EXPECT_EQ(ver, 7u);
  EXPECT_FALSE(unreadable);
  EXPECT_TRUE(sameRoster(saved, Roster::solo()));
  EXPECT_FALSE(fakelog::any("ERR"));
}

TEST_F(GameSaveStoreTest, APassRosterWritesModeOneAndItsSeatsAndTheNewFormsTakeIt) {
  openResume();
  saves->setRoster(Roster::pass(2));
  ASSERT_TRUE(saves->saveResume(TAPS3, 7));
  const Bytes expected = {'C',  'H',  'R',  'S',  1,    1,                 // blob header
                          0x05, 0x30, 0xa1, 0x57, 0x66, 0xe9, 0x1b, 0xf1,  // package hash
                          1,    2,    7,    0,                             // mode (pass), seats, ver (u16 LE)
                          0x06, 0x00, 0x01, 0x05, 0x04, 't',  'a',  'p',  's', 0x03, 0x06};
  EXPECT_EQ(fakesd::bytesOf(RESUME), expected);
  EXPECT_EQ(GameSaveStore::peek(counterGame(), PKG, PASS_HOST), GameSaveStore::SaveState::Valid);
  uint16_t ver = 0;
  bool unreadable = true;
  Roster saved;
  const std::span<const uint8_t> snapshot = saves->loadResume(ver, unreadable, counterGame(), PASS_HOST, saved);
  EXPECT_EQ(Bytes(snapshot.begin(), snapshot.end()), TAPS3);
  EXPECT_EQ(ver, 7u);
  EXPECT_FALSE(unreadable);
  EXPECT_TRUE(sameRoster(saved, Roster::pass(2)));
  EXPECT_FALSE(fakelog::any("ERR"));
}

// The older, solo-only forms (no firmware code calls them; the title screen and the match's Continue use the new
// ones): a pass save is not theirs to offer or resume, so it is Unstartable to peek, loadResume refuses it, and the
// file stays.
TEST_F(GameSaveStoreTest, APassSaveIsUnstartableToTheSoloOnlyFormsAndIsKept) {
  const Bytes passSave = resumeFile(TAPS3, 7, PKG, PASS_BYTE, 2);
  fakesd::addFile(RESUME, passSave);
  openResume();
  EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::Unstartable);
  EXPECT_TRUE(fakelog::any(std::string("INF GAME: counter: ") + RESUME +
                           " is a save that cannot be resumed here: mode not startable; the file is kept"));
  EXPECT_FALSE(fakelog::any("ERR"));
  fakelog::lines.clear();
  uint16_t ver = 5;
  bool unreadable = true;
  EXPECT_TRUE(saves->loadResume(ver, unreadable).empty());
  EXPECT_FALSE(unreadable);
  EXPECT_EQ(ver, 0u);
  EXPECT_TRUE(fakelog::any(std::string("INF GAME: counter: ") + RESUME +
                           " is a save that cannot be resumed here: mode not startable; the file is kept"));
  EXPECT_FALSE(fakelog::any("ERR")) << "the save is kept, not discarded";
  EXPECT_EQ(fakesd::bytesOf(RESUME), passSave);
}

// A save of a mode or seat count this game cannot start on this host: Unstartable in peek and empty from loadResume,
// each with one quiet line saying the file is kept, and the file stays (cross-story review row 2: the title screen
// asks before New replaces it). The pass seats are max(2, seats.min) to min(seats.max, host.maxSeats,
// Roster::MAX_SEATS), and only where game.check(host) starts pass and some seat count fits. A seat count no save of its
// mode has is a malformed file: `bad seat count`, None, with an error line in peek too.
TEST_F(GameSaveStoreTest, ASaveTheGameOrHostCannotStartIsUnstartableAndKept) {
  constexpr uint8_t SOLO_ONLY = Manifest::MODE_SOLO;
  constexpr uint8_t PASS_ONLY = Manifest::MODE_PASS;
  constexpr uint8_t BOTH = Manifest::MODE_SOLO | Manifest::MODE_PASS;
  HostCaps newerHost = PASS_HOST;
  newerHost.minApi = 2;  // no longer runs the game's api 1: check() starts nothing
  const struct {
    uint8_t mode;
    uint8_t seats;
    Manifest game;
    HostCaps host;
    const char* reason;  // null: accepted
  } cases[] = {
      {PASS_BYTE, 2, counterGame(BOTH), PASS_HOST, nullptr},
      {PASS_BYTE, 3, counterGame(BOTH), PASS_HOST, nullptr},
      {PASS_BYTE, 2, counterGame(BOTH), hostOf(2, true), nullptr},
      {PASS_BYTE, 2, counterGame(PASS_ONLY), PASS_HOST, nullptr},
      {SOLO_BYTE, 1, counterGame(SOLO_ONLY), hostOf(1, false), nullptr},
      {PASS_BYTE, 16, counterGame(BOTH, 1, 20), hostOf(20, true), nullptr},
      {PASS_BYTE, 3, counterGame(PASS_ONLY, 3, 4), PASS_HOST, nullptr},
      {PASS_BYTE, 2, counterGame(PASS_ONLY, 3, 4), PASS_HOST, "seats not startable"},  // max(2, seats.min) is 3
      {PASS_BYTE, 3, counterGame(BOTH), hostOf(2, true), "seats not startable"},       // the host's maxSeats
      {PASS_BYTE, 4, counterGame(BOTH), PASS_HOST, "seats not startable"},             // the game's seats.max
      {PASS_BYTE, 1, counterGame(BOTH), PASS_HOST, "bad seat count"},                  // a pass match has 2 or more
      {PASS_BYTE, 0, counterGame(BOTH), PASS_HOST, "bad seat count"},
      {PASS_BYTE, 17, counterGame(BOTH, 1, 20), hostOf(20, true), "bad seat count"},  // Roster::MAX_SEATS
      {PASS_BYTE, 2, counterGame(BOTH), hostOf(1, true), "mode not startable"},   // no pass seat count fits the host
      {PASS_BYTE, 2, counterGame(BOTH), hostOf(4, false), "mode not startable"},  // host.pass off
      {PASS_BYTE, 2, counterGame(SOLO_ONLY), PASS_HOST, "mode not startable"},    // the manifest lacks pass
      {PASS_BYTE, 2, counterGame(BOTH), newerHost, "mode not startable"},
      {SOLO_BYTE, 1, counterGame(PASS_ONLY), PASS_HOST, "mode not startable"},  // the manifest lacks solo
      {SOLO_BYTE, 1, counterGame(BOTH), newerHost, "mode not startable"},
      {SOLO_BYTE, 2, counterGame(BOTH), PASS_HOST, "bad seat count"},
      {SOLO_BYTE, 0, counterGame(BOTH), PASS_HOST, "bad seat count"},
  };
  for (const auto& c : cases) {
    const std::string label = std::string("mode ") + std::to_string(c.mode) + ", n " + std::to_string(c.seats) +
                              ", host maxSeats " + std::to_string(c.host.maxSeats) + (c.host.pass ? " pass" : "");
    SetUp();
    const Bytes file = resumeFile(TAPS3, 7, PKG, c.mode, c.seats);
    fakesd::addFile(RESUME, file);
    openResume();
    const GameSaveStore::SaveState state = GameSaveStore::peek(c.game, PKG, c.host);
    uint16_t ver = 5;
    bool unreadable = true;
    Roster saved = Roster::pass(3);
    const std::span<const uint8_t> snapshot = saves->loadResume(ver, unreadable, c.game, c.host, saved);
    EXPECT_FALSE(unreadable) << label;
    EXPECT_EQ(fakesd::bytesOf(RESUME), file) << label << ": a refused save is never deleted";
    if (!c.reason) {
      EXPECT_EQ(state, GameSaveStore::SaveState::Valid) << label;
      EXPECT_EQ(Bytes(snapshot.begin(), snapshot.end()), TAPS3) << label;
      EXPECT_EQ(ver, 7u) << label;
      const Roster want = c.mode == PASS_BYTE ? Roster::pass(c.seats) : Roster::solo();
      EXPECT_TRUE(sameRoster(saved, want)) << label;
      EXPECT_FALSE(fakelog::any("ERR")) << label;
      continue;
    }
    const bool malformed = std::string(c.reason) == "bad seat count";
    EXPECT_EQ(state, malformed ? GameSaveStore::SaveState::None : GameSaveStore::SaveState::Unstartable) << label;
    EXPECT_TRUE(snapshot.empty()) << label;
    EXPECT_EQ(ver, 0u) << label;
    EXPECT_TRUE(sameRoster(saved, Roster::solo())) << label;
    const std::string kept = std::string("INF GAME: counter: ") + RESUME +
                             " is a save that cannot be resumed here: " + c.reason + "; the file is kept";
    EXPECT_EQ(std::count(fakelog::lines.begin(), fakelog::lines.end(), kept), malformed ? 0 : 2)
        << label << ": peek's and loadResume's lines";
    EXPECT_EQ(fakelog::any(std::string("ERR GAME: counter: discarded ") + RESUME + ": " + c.reason), malformed)
        << label;
    EXPECT_EQ(std::count_if(fakelog::lines.begin(), fakelog::lines.end(),
                            [](const std::string& line) { return line.rfind("ERR", 0) == 0; }),
              malformed ? 2 : 0)
        << label << (malformed ? ": both lines of a malformed file are errors" : ": a kept save logs no error");
  }
}

// A mode byte this firmware does not write (a later firmware's mode, perhaps): Unstartable to both peeks and refused by
// loadResume, each logged quietly as kept; the file stays.
TEST_F(GameSaveStoreTest, AnUnknownModeIsUnstartableAndKept) {
  for (const uint8_t mode : {2, 3, 255}) {
    SetUp();
    const Bytes file = resumeFile(TAPS3, 7, PKG, mode, 2);
    fakesd::addFile(RESUME, file);
    openResume();
    EXPECT_EQ(GameSaveStore::peek(counterGame(), PKG, PASS_HOST), GameSaveStore::SaveState::Unstartable) << int(mode);
    EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::Unstartable) << int(mode);
    EXPECT_TRUE(fakelog::any(std::string("INF GAME: counter: ") + RESUME +
                             " is a save that cannot be resumed here: unknown mode; the file is kept"))
        << int(mode);
    EXPECT_FALSE(fakelog::any("ERR")) << int(mode);
    fakelog::lines.clear();
    uint16_t ver = 5;
    bool unreadable = true;
    Roster saved = Roster::pass(3);
    EXPECT_TRUE(saves->loadResume(ver, unreadable, counterGame(), PASS_HOST, saved).empty()) << int(mode);
    EXPECT_FALSE(unreadable);
    EXPECT_EQ(ver, 0u);
    EXPECT_TRUE(sameRoster(saved, Roster::solo()));
    EXPECT_TRUE(fakelog::any(std::string("INF GAME: counter: ") + RESUME +
                             " is a save that cannot be resumed here: unknown mode; the file is kept"))
        << int(mode);
    EXPECT_FALSE(fakelog::any("ERR")) << int(mode);
    EXPECT_EQ(fakesd::bytesOf(RESUME), file) << int(mode);
  }
}

// A later firmware's save (cross-story fix review F1): a newer file version (its layout unknown, so no package check:
// it sits in this game's folder), a newer codec version or a snapshot over this firmware's limit with this package's
// hash. Each is Unstartable to both peeks, refused by loadResume, logged quietly as kept, and kept. The same versions
// or size on another package's save, and an older or garbage version, are not.
TEST_F(GameSaveStoreTest, ALaterFirmwaresSaveIsUnstartableAndKept) {
  const Bytes big = bigTable(GameCore::SNAPSHOT_BYTES - 8);  // 1,401 bytes
  const struct {
    Bytes file;
    const char* reason;
    bool kept;  // Unstartable; else None with an error line
    // The kept lines that name `reason`: one per call (the solo-only peek, peek, loadResume), but the solo-only peek
    // refuses a pass save as `mode not startable`, which is checked before the size and the codec.
    int keptLines = 3;
  } cases[] = {
      {resumeFile(TAPS3, 7, PKG, 0, 1, "CHRS", 2), "newer file version", true},
      {resumeFile(TAPS3, 7, OTHER_PKG, 0, 1, "CHRS", 255), "newer file version", true},
      {firstBytes(resumeFile(TAPS3, 7, PKG, 0, 1, "CHRS", 2), 6), "newer file version", true},
      {resumeFile(TAPS3, 7, PKG, 0, 1, "CHRS", 1, 2), "newer codec version", true},
      {resumeFile(TAPS3, 7, PKG, PASS_BYTE, 2, "CHRS", 1, 9), "newer codec version", true, 2},
      {resumeFile(big), "too large", true},
      {resumeFile(big, 7, PKG, PASS_BYTE, 2), "too large", true, 2},
      {resumeFile(TAPS3, 7, OTHER_PKG, 0, 1, "CHRS", 1, 2), "other package", false},
      {resumeFile(TAPS3, 7, OTHER_PKG, 0, 1, "CHRS", 1, 0), "other package", false},  // an older codec, too
      {resumeFile(TAPS3, 7, PKG, 0, 1, "XXXX", 2), "bad_magic", false},
      {resumeFile(TAPS3, 7, PKG, 0, 1, "CHRS", 0), "unknown_file_version", false},
      {resumeFile(TAPS3, 7, PKG, 0, 1, "CHRS", 1, 0), "unknown_codec_version", false},
      // Oversized but not this package's, or with a header this firmware refuses: the header or package reason, never
      // `too large`, which only a save of this package with a good header gets.
      {resumeFile(big, 7, OTHER_PKG), "other package", false},
      {resumeFile(big, 7, PKG, 0, 1, "XXXX"), "bad_magic", false},
      {resumeFile(big, 7, PKG, 0, 1, "CHRS", 0), "unknown_file_version", false},
      {resumeFile(big, 7, PKG, 0, 1, "CHRS", 1, 0), "unknown_codec_version", false},
      // This package's oversized or newer-codec save with a malformed seat count: a refusal that is not kept, size or
      // codec aside.
      {resumeFile(big, 7, PKG, 0, 2), "bad seat count", false},
      {resumeFile(TAPS3, 7, PKG, 0, 2, "CHRS", 1, 2), "bad seat count", false},
      // A newer codec names the mode or seat refusal before itself, and itself before the size.
      {resumeFile(TAPS3, 7, PKG, 5, 1, "CHRS", 1, 2), "unknown mode", true},
      {resumeFile(TAPS3, 7, PKG, PASS_BYTE, 4, "CHRS", 1, 2), "seats not startable", true, 2},
      {resumeFile(big, 7, PKG, 0, 1, "CHRS", 1, 2), "newer codec version", true},
  };
  for (const auto& c : cases) {
    SetUp();
    fakesd::addFile(RESUME, c.file);
    openResume();
    const std::string label = std::string(c.reason) + " (" + std::to_string(c.file.size()) + " bytes)";
    const GameSaveStore::SaveState want =
        c.kept ? GameSaveStore::SaveState::Unstartable : GameSaveStore::SaveState::None;
    EXPECT_EQ(GameSaveStore::peek("counter", PKG), want) << label;
    EXPECT_EQ(GameSaveStore::peek(counterGame(), PKG, PASS_HOST), want) << label;
    uint16_t ver = 5;
    bool unreadable = true;
    Roster saved = Roster::pass(3);
    EXPECT_TRUE(saves->loadResume(ver, unreadable, counterGame(), PASS_HOST, saved).empty()) << label;
    EXPECT_FALSE(unreadable) << label;
    const std::string keptLine = std::string("INF GAME: counter: ") + RESUME +
                                 " is a save that cannot be resumed here: " + c.reason + "; the file is kept";
    EXPECT_EQ(std::count(fakelog::lines.begin(), fakelog::lines.end(), keptLine), c.kept ? c.keptLines : 0) << label;
    EXPECT_EQ(fakelog::any(std::string("ERR GAME: counter: discarded ") + RESUME + ": " + c.reason), !c.kept) << label;
    if (std::string(c.reason) == "other package") {
      // Another package's save is a quiet line in peek, at any size.
      EXPECT_TRUE(
          fakelog::any(std::string("INF GAME: counter: ") + RESUME + " is another package's save: other package"))
          << label;
    }
    if (!c.kept) EXPECT_FALSE(fakelog::any(": too large")) << label << ": refused as too large";
    EXPECT_EQ(fakesd::bytesOf(RESUME), c.file) << label << ": kept";
  }
}

// peekResume, the match's refusal check (cross-story review row 10, fix review F6): the static peek's answer for the
// store's own id and package, read through the store's buffer (which nothing references after an empty loadResume) with
// no allocation, and the roster (what its next save writes) unchanged, where a loadResume that loads the save adopts
// the save's roster.
TEST_F(GameSaveStoreTest, PeekResumeAnswersAsPeekThroughTheStoresBufferAndLeavesItsRoster) {
  const Bytes passSave = resumeFile(TAPS3, 7, PKG, PASS_BYTE, 2);
  fakesd::addFile(RESUME, passSave);
  open();
  EXPECT_EQ(saves->peekResume(counterGame(), PASS_HOST), GameSaveStore::SaveState::None)
      << "without the package hash no file is known to be this package's";
  openResume();
  std::fill(buffer.begin(), buffer.end(), 0xAB);
  const Bytes before = buffer;
  // Through the store's own buffer, with no allocation: a nothrow array new would fail here, and is never made.
  failNextNothrowNew = true;
  EXPECT_EQ(saves->peekResume(counterGame(), PASS_HOST), GameSaveStore::SaveState::Valid);
  EXPECT_EQ(saves->peekResume(counterGame(), hostOf(4, false)), GameSaveStore::SaveState::Unstartable);
  EXPECT_EQ(saves->peekResume(counterGame(Manifest::MODE_SOLO), PASS_HOST), GameSaveStore::SaveState::Unstartable);
  EXPECT_TRUE(failNextNothrowNew) << "peekResume allocated";
  failNextNothrowNew = false;
  EXPECT_NE(buffer, before) << "the snapshot was read through the store's buffer";
  EXPECT_TRUE(std::equal(TAPS3.begin(), TAPS3.end(), buffer.begin())) << "the snapshot it checked";
  EXPECT_EQ(fakesd::bytesOf(RESUME), passSave);
  // The store's roster is still solo: its next save is mode 0, n 1, not the pass save's roster.
  ASSERT_TRUE(saves->saveResume(TAPS4, 8));
  EXPECT_EQ(fakesd::bytesOf(RESUME), resumeFile(TAPS4, 8, PKG, SOLO_BYTE, 1));
  // A nearby roster reads nothing, as loadResume does (AD-17).
  Roster nearby = Roster::pass(2);
  nearby.mode = Mode::Nearby;
  saves->setRoster(nearby);
  EXPECT_EQ(saves->peekResume(counterGame(), PASS_HOST), GameSaveStore::SaveState::None);
}

// A save the firmware before pass saves wrote: formats.md's example, byte for byte. File version 1 is unchanged.
TEST_F(GameSaveStoreTest, APreEpicSoloSaveIsValidInBothForms) {
  const Bytes example = {0x43, 0x48, 0x52, 0x53, 0x01, 0x01, 0x05, 0x30, 0xa1, 0x57, 0x66, 0xe9, 0x1b, 0xf1, 0x00,
                         0x01, 0x07, 0x00, 0x06, 0x00, 0x01, 0x05, 0x04, 0x74, 0x61, 0x70, 0x73, 0x03, 0x06};
  fakesd::addFile(RESUME, example);
  openResume();
  EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::Valid);
  EXPECT_EQ(GameSaveStore::peek(counterGame(), PKG, PASS_HOST), GameSaveStore::SaveState::Valid);
  uint16_t ver = 0;
  EXPECT_EQ(loaded(ver), TAPS3);
  EXPECT_EQ(ver, 7u);
  bool unreadable = true;
  Roster saved = Roster::pass(3);
  const std::span<const uint8_t> snapshot = saves->loadResume(ver, unreadable, counterGame(), PASS_HOST, saved);
  EXPECT_EQ(Bytes(snapshot.begin(), snapshot.end()), TAPS3);
  EXPECT_EQ(ver, 7u);
  EXPECT_TRUE(sameRoster(saved, Roster::solo()));
  EXPECT_EQ(fakesd::bytesOf(RESUME), example);
}

// Unreadable is a card or heap fault, whatever the save's mode: a pass save that would not open or read, or whose
// check buffer could not be allocated, is not None, and it stays.
TEST_F(GameSaveStoreTest, APassSaveThatCannotBeReadIsUnreadableAndKept) {
  const Bytes passSave = resumeFile(TAPS3, 7, PKG, PASS_BYTE, 2);
  for (const bool failOpen : {true, false}) {
    SetUp();
    fakesd::addFile(RESUME, passSave);
    openResume();
    if (failOpen) {
      fakesd::sim().failOpen.insert(RESUME);
    } else {
      fakesd::sim().failReadAt[RESUME] = 0;
    }
    EXPECT_EQ(GameSaveStore::peek(counterGame(), PKG, PASS_HOST), GameSaveStore::SaveState::Unreadable);
    EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::Unreadable) << "the old form alike";
    uint16_t ver = 5;
    bool unreadable = false;
    Roster saved = Roster::pass(3);
    EXPECT_TRUE(saves->loadResume(ver, unreadable, counterGame(), PASS_HOST, saved).empty());
    EXPECT_TRUE(unreadable);
    EXPECT_TRUE(sameRoster(saved, Roster::solo()));
    EXPECT_TRUE(fakelog::any("the file is kept"));
    EXPECT_EQ(fakesd::bytesOf(RESUME), passSave);
  }
  SetUp();
  fakesd::addFile(RESUME, passSave);
  openResume();
  failNextNothrowNew = true;
  EXPECT_EQ(GameSaveStore::peek(counterGame(), PKG, PASS_HOST), GameSaveStore::SaveState::Unreadable);
  failNextNothrowNew = false;
  EXPECT_TRUE(fakelog::any(std::string("ERR GAME: counter: OOM: 1400 bytes to check ") + RESUME));
  EXPECT_EQ(fakesd::bytesOf(RESUME), passSave);
}

// AD-17: a nearby match keeps no resume.bin. With its roster set, nothing is read, written, or deleted, as without
// the package hash.
TEST_F(GameSaveStoreTest, ANearbyRosterReadsWritesAndDeletesNothing) {
  const Bytes passSave = resumeFile(TAPS3, 7, PKG, PASS_BYTE, 2);
  fakesd::addFile(RESUME, passSave);
  fakesd::addFile(RESUME_TMP, passSave);
  openResume();
  Roster nearby = Roster::pass(2);
  nearby.mode = Mode::Nearby;
  saves->setRoster(nearby);
  fakesd::sim().ops.clear();
  EXPECT_FALSE(saves->saveResume(TAPS4, 8));
  uint16_t ver = 5;
  bool unreadable = true;
  EXPECT_TRUE(saves->loadResume(ver, unreadable).empty());
  EXPECT_FALSE(unreadable);
  EXPECT_EQ(ver, 0u);
  Roster saved = Roster::pass(3);
  EXPECT_TRUE(saves->loadResume(ver, unreadable, counterGame(), PASS_HOST, saved).empty());
  EXPECT_FALSE(unreadable);
  EXPECT_TRUE(sameRoster(saved, Roster::solo()));
  EXPECT_TRUE(saves->deleteResume());
  ASSERT_TRUE(mailbox->publish(TAPS4, 8, false));
  EXPECT_TRUE(saves->flushResume(*mailbox, 1001));
  ASSERT_TRUE(mailbox->publish(TAPS4, 9, true));
  EXPECT_TRUE(saves->flushResume(*mailbox, 1002));
  EXPECT_TRUE(fakesd::sim().ops.empty()) << "not even an exists";
  EXPECT_EQ(fakesd::bytesOf(RESUME), passSave);
  EXPECT_EQ(fakesd::bytesOf(RESUME_TMP), passSave);
  EXPECT_EQ(saves->resumeReplacements(), 0u);
}

// The package hash is checked before the mode: another package's save is that package's whatever its mode, so it is
// the quiet "another package's save" line, never a mode refusal, and the file stays.
TEST_F(GameSaveStoreTest, AnotherPackagesSaveIsThatWhateverItsMode) {
  for (const uint8_t mode : {PASS_BYTE, uint8_t{7}}) {
    SetUp();
    const Bytes file = resumeFile(TAPS3, 7, OTHER_PKG, mode, 2);
    fakesd::addFile(RESUME, file);
    openResume();
    EXPECT_EQ(GameSaveStore::peek(counterGame(), PKG, PASS_HOST), GameSaveStore::SaveState::None) << int(mode);
    EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::None) << int(mode);
    EXPECT_TRUE(fakelog::any(std::string("INF GAME: counter: ") + RESUME + " is another package's save")) << int(mode);
    EXPECT_FALSE(fakelog::any("ERR")) << int(mode);
    EXPECT_EQ(fakesd::bytesOf(RESUME), file) << int(mode);
  }
}

// A match that resumed a pass save goes on writing it as one, without a setRoster of its own.
TEST_F(GameSaveStoreTest, AResumedPassSaveIsWrittenBackAsAPassSave) {
  fakesd::addFile(RESUME, resumeFile(TAPS3, 7, PKG, PASS_BYTE, 3));
  openResume();
  uint16_t ver = 0;
  bool unreadable = true;
  Roster saved;
  ASSERT_FALSE(saves->loadResume(ver, unreadable, counterGame(), PASS_HOST, saved).empty());
  ASSERT_TRUE(sameRoster(saved, Roster::pass(3)));
  ASSERT_TRUE(saves->saveResume(TAPS4, 8));
  EXPECT_EQ(fakesd::bytesOf(RESUME), resumeFile(TAPS4, 8, PKG, PASS_BYTE, 3));
}

// The match's own path for a pass roster: the mailbox's snapshot is written as mode 1 with its seats, and an over
// snapshot deletes the save as it does a solo one.
TEST_F(GameSaveStoreTest, FlushResumeWritesAPassRostersSnapshotAndDeletesItWhenOver) {
  openResume();
  saves->setRoster(Roster::pass(2));
  ASSERT_TRUE(mailbox->publish(TAPS3, 5, false));
  EXPECT_TRUE(saves->flushResume(*mailbox, 1001));
  EXPECT_EQ(fakesd::bytesOf(RESUME), resumeFile(TAPS3, 5, PKG, PASS_BYTE, 2));
  ASSERT_TRUE(mailbox->publish(TAPS4, 6, true));
  EXPECT_TRUE(saves->flushResume(*mailbox, 1002));
  EXPECT_FALSE(fakesd::has(RESUME));
  EXPECT_FALSE(fakesd::has(RESUME_TMP));
}

// The fake card's two behaviours the save store's writes lean on, pinned against SdFat 2.3.1 so a suite is not
// fooled by the fake (R13):
//   - rename refuses an existing target and leaves both files: FatFile::rename makes the new entry with
//     O_CREAT | O_EXCL (FatFile.cpp ~973);
//   - a write that fails returns 0 and adds nothing, while the bytes earlier writes put there stay: FatFile::write's
//     fail path returns 0, and m_fileSize moves only once a whole write is in (FatFile.cpp ~1490-1503); the close
//     after it still succeeds, since FatFile::close is sync() (FatFile.cpp:128), which does not look at the write
//     error (~1231).
TEST_F(GameSaveStoreTest, TheFakeCardRenamesAndTearsWritesAsSdFatDoes) {
  fakesd::addFile("/d/a.bin", Bytes{1, 2});
  fakesd::addFile("/d/b.bin", Bytes{3});
  EXPECT_FALSE(Storage.rename("/d/a.bin", "/d/b.bin"));
  EXPECT_EQ(fakesd::bytesOf("/d/a.bin"), (Bytes{1, 2}));
  EXPECT_EQ(fakesd::bytesOf("/d/b.bin"), (Bytes{3}));

  fakesd::sim().failWriteAt["/d/t.bin"] = 4;
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", "/d/t.bin", file));
  const uint8_t head[3] = {10, 11, 12};
  const uint8_t body[2] = {13, 14};
  EXPECT_EQ(file.write(head, sizeof(head)), sizeof(head));
  EXPECT_EQ(file.write(body, sizeof(body)), 0u) << "its second byte would be at offset 4";
  EXPECT_EQ(fakesd::bytesOf("/d/t.bin"), (Bytes{10, 11, 12})) << "none of the failed write's bytes, the earlier stay";
  EXPECT_TRUE(file.close());
  EXPECT_EQ(fakesd::bytesOf("/d/t.bin"), (Bytes{10, 11, 12}));
}

// ---- prefs.bin (AD-17, as amended 2026-10-02): the remembered mode and settings ----

constexpr const char* PREFS = "/.games-data/counter/prefs.bin";
constexpr const char* PREFS_TMP = "/.games-data/counter/prefs.bin.tmp";
constexpr size_t PREFS_HEAD_BYTES = 7;

// prefs.bin's bytes: "CHPF", the version, the mode, the count, then each id and value after its length byte.
Bytes prefsFile(const uint8_t mode, const std::vector<std::pair<std::string, std::string>>& settings,
                const uint8_t version = 1, const char* magic = "CHPF") {
  Bytes out(magic, magic + 4);
  out.push_back(version);
  out.push_back(mode);
  out.push_back(static_cast<uint8_t>(settings.size()));
  for (const auto& [id, value] : settings) {
    out.push_back(static_cast<uint8_t>(id.size()));
    out.insert(out.end(), id.begin(), id.end());
    out.push_back(static_cast<uint8_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
  }
  return out;
}

GameSaveStore::Prefs prefsWith(const uint8_t mode, const std::vector<std::pair<std::string, std::string>>& settings) {
  GameSaveStore::Prefs prefs;
  prefs.mode = mode;
  for (const auto& [id, value] : settings) {
    GameCore::SettingValues::Entry& entry = prefs.settings.entries[prefs.settings.count++];
    std::snprintf(entry.id, sizeof(entry.id), "%s", id.c_str());
    std::snprintf(entry.value, sizeof(entry.value), "%s", value.c_str());
  }
  return prefs;
}

// A game that declares two settings: level (Easy, Normal, Hard; default Normal) and sound (On, Off).
struct SettingsGame {
  GameCore::Manifest manifest;
  GameCore::ManifestSettings settings;

  SettingsGame() {
    auto reader = std::make_unique<GameCore::ManifestReader>();
    const std::string json =
        R"({"id": "counter", "name": "Counter", "version": "1", "api": 1, "seats": {"min": 1, "max": 2},)"
        R"( "modes": ["solo", "pass"], "settings": [)"
        R"({"id": "level", "name": "Level", "values": ["Easy", "Normal", "Hard"], "default": "Normal"},)"
        R"({"id": "sound", "name": "Sound", "values": ["On", "Off"]}]})";
    reader->begin();
    reader->feed(json.data(), json.size());
    EXPECT_EQ(reader->finish(manifest), GameCore::ManifestError::None);
    settings = reader->settings();
  }
};

constexpr uint8_t SOLO = GameCore::Manifest::MODE_SOLO;
constexpr uint8_t PASS = GameCore::Manifest::MODE_PASS;

std::string prefsText(const GameSaveStore::Prefs& prefs) {
  std::string text = std::to_string(prefs.mode);
  for (uint8_t i = 0; i < prefs.settings.count; ++i) {
    text += std::string(" ") + prefs.settings.entries[i].id + "=" + prefs.settings.entries[i].value;
  }
  return text;
}

TEST_F(GameSaveStoreTest, PrefsRoundTripThroughTheExactBytes) {
  const GameSaveStore::Prefs written = prefsWith(PASS, {{"level", "Hard"}, {"sound", "Off"}});
  ASSERT_TRUE(GameSaveStore::savePrefs("counter", written));
  EXPECT_EQ(fakesd::bytesOf(PREFS), prefsFile(PASS, {{"level", "Hard"}, {"sound", "Off"}}));
  EXPECT_EQ(fakesd::bytesOf(PREFS), (Bytes{'C', 'H', 'P', 'F', 1, 2,   2,   5,   'l', 'e', 'v', 'e', 'l', 4,
                                           'H', 'a', 'r', 'd', 5, 's', 'o', 'u', 'n', 'd', 3,   'O', 'f', 'f'}));
  EXPECT_FALSE(fakesd::has(PREFS_TMP));
  GameSaveStore::Prefs read;
  EXPECT_EQ(GameSaveStore::loadPrefs("counter", read), GameSaveStore::PrefsState::Loaded);
  EXPECT_EQ(prefsText(read), "2 level=Hard sound=Off");

  // No mode and no settings: the 7-byte head alone.
  ASSERT_TRUE(GameSaveStore::savePrefs("counter", GameSaveStore::Prefs{}));
  EXPECT_EQ(fakesd::bytesOf(PREFS), prefsFile(0, {}));
  EXPECT_EQ(GameSaveStore::loadPrefs("counter", read), GameSaveStore::PrefsState::Loaded);
  EXPECT_EQ(prefsText(read), "0");
}

TEST_F(GameSaveStoreTest, ThePrefsAtTheLimitAreTheLongestFile) {
  const std::string id(GameCore::ManifestSetting::MAX_ID_BYTES, 'i');
  const std::string value(GameCore::ManifestSetting::MAX_VALUE_BYTES, 'v');
  GameSaveStore::Prefs full = prefsWith(SOLO, {{id, value}, {id, value}, {id, value}, {id, value}});
  ASSERT_TRUE(GameSaveStore::savePrefs("counter", full));
  EXPECT_EQ(fakesd::bytesOf(PREFS).size(), GameSaveStore::PREFS_MAX_BYTES);
  GameSaveStore::Prefs read;
  EXPECT_EQ(GameSaveStore::loadPrefs("counter", read), GameSaveStore::PrefsState::Loaded);
  EXPECT_EQ(read.settings.count, 4);
  EXPECT_STREQ(read.settings.entries[3].value, value.c_str());
}

TEST_F(GameSaveStoreTest, APrefsWriteGoesThroughTheTmpThenARename) {
  ASSERT_TRUE(GameSaveStore::savePrefs("counter", prefsWith(SOLO, {})));
  fakesd::sim().ops.clear();
  ASSERT_TRUE(GameSaveStore::savePrefs("counter", prefsWith(PASS, {{"level", "Easy"}})));
  const std::vector<std::string> ops = cardOps();
  const auto at = [&ops](const std::string& op) { return std::find(ops.begin(), ops.end(), op) - ops.begin(); };
  ASSERT_LT(at(std::string("open ") + PREFS_TMP), static_cast<long>(ops.size()));
  EXPECT_LT(at(std::string("close ") + PREFS_TMP), at(std::string("remove ") + PREFS));
  EXPECT_LT(at(std::string("remove ") + PREFS), at(std::string("rename ") + PREFS_TMP + " " + PREFS));
}

TEST_F(GameSaveStoreTest, NoPrefsIsNoneWithoutALogLine) {
  GameSaveStore::Prefs read = prefsWith(PASS, {{"level", "Easy"}});
  EXPECT_EQ(GameSaveStore::loadPrefs("counter", read), GameSaveStore::PrefsState::None);
  EXPECT_EQ(prefsText(read), "0");
  EXPECT_TRUE(fakelog::lines.empty());
}

TEST_F(GameSaveStoreTest, AMalformedPrefsFileIsIgnoredQuietlyAndKept) {
  Bytes lengthPastTheEnd = prefsFile(SOLO, {{"level", "Hard"}});
  lengthPastTheEnd.pop_back();
  Bytes longId = prefsFile(SOLO, {{std::string(17, 'i'), "Hard"}});
  const std::pair<const char*, Bytes> cases[] = {
      {"bad magic", prefsFile(SOLO, {}, 1, "CHPX")},
      {"unknown file version", prefsFile(SOLO, {}, 2)},
      {"truncated", Bytes{'C', 'H', 'P', 'F', 1, 1}},
      {"too many settings", prefsFile(SOLO, {{"a", "1"}, {"b", "1"}, {"c", "1"}, {"d", "1"}, {"e", "1"}})},
      {"malformed setting", prefsFile(SOLO, {{"", "Hard"}})},
      {"malformed setting", prefsFile(SOLO, {{"level", ""}})},
      {"malformed setting", lengthPastTheEnd},
      {"malformed setting", longId},
      {"trailing bytes", cat(prefsFile(SOLO, {{"level", "Hard"}}), Bytes{0})},
      {"too large", Bytes(GameSaveStore::PREFS_MAX_BYTES + 1, 'C')},
  };
  for (const auto& [why, file] : cases) {
    SCOPED_TRACE(why);
    fakesd::reset();
    fakelog::lines.clear();
    fakesd::addFile(PREFS, file);
    GameSaveStore::Prefs read = prefsWith(PASS, {{"x", "y"}});
    EXPECT_EQ(GameSaveStore::loadPrefs("counter", read), GameSaveStore::PrefsState::Malformed);
    EXPECT_EQ(prefsText(read), "0") << "nothing of a malformed file is used";
    EXPECT_TRUE(fakelog::any(std::string("INF GAME: counter: ignored ") + PREFS + ": " + why));
    EXPECT_FALSE(fakelog::any("ERR"));
    EXPECT_EQ(fakesd::bytesOf(PREFS), file) << "the file stays until the next write replaces it";
  }
}

TEST_F(GameSaveStoreTest, APrefsFileThatCannotBeOpenedOrReadIsUnreadableAndKept) {
  for (const bool failOpen : {true, false}) {
    SCOPED_TRACE(failOpen ? "open" : "read");
    fakesd::reset();
    fakelog::lines.clear();
    const Bytes file = prefsFile(PASS, {{"level", "Hard"}});
    fakesd::addFile(PREFS, file);
    if (failOpen) {
      fakesd::sim().failOpen.insert(PREFS);
    } else {
      fakesd::sim().failReadAt[PREFS] = 0;
    }
    GameSaveStore::Prefs read;
    EXPECT_EQ(GameSaveStore::loadPrefs("counter", read), GameSaveStore::PrefsState::Unreadable);
    EXPECT_EQ(prefsText(read), "0");
    EXPECT_TRUE(fakelog::any("INF GAME: counter: could not read"));
    clearFailures();
    EXPECT_EQ(fakesd::bytesOf(PREFS), file);
  }
}

TEST_F(GameSaveStoreTest, AWholePrefsTmpIsReadWhenPrefsBinIsMissing) {
  fakesd::addFile(PREFS_TMP, prefsFile(PASS, {{"sound", "Off"}}));
  GameSaveStore::Prefs read;
  EXPECT_EQ(GameSaveStore::loadPrefs("counter", read), GameSaveStore::PrefsState::Loaded);
  EXPECT_EQ(prefsText(read), "2 sound=Off");
  // The next write makes the tmp prefs.bin first, then replaces it.
  ASSERT_TRUE(GameSaveStore::savePrefs("counter", prefsWith(SOLO, {})));
  EXPECT_EQ(fakesd::bytesOf(PREFS), prefsFile(SOLO, {}));
  EXPECT_FALSE(fakesd::has(PREFS_TMP));
}

TEST_F(GameSaveStoreTest, AFailedPrefsWriteKeepsThePreviousFileAndLeavesNoTmp) {
  const Bytes before = prefsFile(SOLO, {{"level", "Easy"}});
  for (const WriteFault fault : WRITE_FAULTS) {
    SCOPED_TRACE(static_cast<int>(fault));
    fakesd::reset();
    fakelog::lines.clear();
    fakesd::addFile(PREFS, before);
    failWriteOf(PREFS_TMP, fault, PREFS_HEAD_BYTES);
    EXPECT_FALSE(GameSaveStore::savePrefs("counter", prefsWith(PASS, {{"level", "Hard"}})));
    EXPECT_TRUE(fakelog::any("ERR GAME: counter: cannot write " + std::string(PREFS_TMP)));
    EXPECT_EQ(fakesd::bytesOf(PREFS), before);
    EXPECT_FALSE(fakesd::has(PREFS_TMP));
  }
  // Out of memory for its buffer: logged, nothing written.
  fakesd::reset();
  failNextNothrowNew = true;
  EXPECT_FALSE(GameSaveStore::savePrefs("counter", prefsWith(PASS, {})));
  EXPECT_TRUE(fakelog::any("OOM"));
  EXPECT_FALSE(fakesd::has(PREFS));
  EXPECT_FALSE(fakesd::has(PREFS_TMP));
}

TEST_F(GameSaveStoreTest, PrefsNoFileCanHoldAreRefusedWithoutWriting) {
  GameSaveStore::Prefs emptyId = prefsWith(SOLO, {{"", "Hard"}});
  EXPECT_FALSE(GameSaveStore::savePrefs("counter", emptyId));
  GameSaveStore::Prefs tooMany = prefsWith(SOLO, {});
  tooMany.settings.count = GameCore::SettingValues::MAX_SETTINGS + 1;
  EXPECT_FALSE(GameSaveStore::savePrefs("counter", tooMany));
  GameSaveStore::Prefs unterminated = prefsWith(SOLO, {{"level", "Hard"}});
  std::memset(unterminated.settings.entries[0].value, 'v', sizeof(unterminated.settings.entries[0].value));
  EXPECT_FALSE(GameSaveStore::savePrefs("counter", unterminated));
  EXPECT_FALSE(fakesd::has(PREFS));
  EXPECT_FALSE(fakesd::has(PREFS_TMP));
}

// A stale file falls back value by value (Design Notes): the mode this host cannot start, a value the manifest no
// longer declares, and a setting it no longer has each take the manifest's default, and only those.
TEST_F(GameSaveStoreTest, StalePrefsFallBackToTheManifestsDefaultsValueByValue) {
  const SettingsGame game;
  ASSERT_EQ(game.settings.count, 2);
  const GameSaveStore::Prefs saved = prefsWith(PASS, {{"gone", "1"}, {"sound", "Loud"}, {"level", "Hard"}});

  GameSaveStore::Choices choices = GameSaveStore::resolvePrefs(saved, game.manifest, game.settings, SOLO | PASS);
  EXPECT_EQ(choices.mode, PASS) << "remembered and startable";
  EXPECT_EQ(choices.valueIndex[0], 2) << "level: Hard, remembered";
  EXPECT_EQ(choices.valueIndex[1], 0) << "sound: Loud is no value, so the default, On";
  EXPECT_TRUE(fakelog::any("DBG GAME: counter: the remembered sound \"Loud\" is not one of its values"));
  EXPECT_FALSE(fakelog::any("ERR"));

  fakelog::lines.clear();
  choices = GameSaveStore::resolvePrefs(saved, game.manifest, game.settings, SOLO);
  EXPECT_EQ(choices.mode, SOLO) << "pass cannot start here: the first startable";
  EXPECT_EQ(choices.valueIndex[0], 2) << "the settings resolve on their own";
  EXPECT_TRUE(fakelog::any("DBG GAME: counter: the remembered mode 0x2 cannot start here"));

  // Nothing remembered: every default (level's is Normal).
  choices = GameSaveStore::resolvePrefs(GameSaveStore::Prefs{}, game.manifest, game.settings, SOLO | PASS);
  EXPECT_EQ(choices.mode, SOLO);
  EXPECT_EQ(choices.valueIndex[0], 1);
  EXPECT_EQ(choices.valueIndex[1], 0);
}

// What the title screen writes is what it reads back: prefsOf, savePrefs, loadPrefs, resolvePrefs.
TEST_F(GameSaveStoreTest, ChoicesSurviveTheirOwnPrefsFile) {
  const SettingsGame game;
  GameSaveStore::Choices chosen;
  chosen.mode = PASS;
  chosen.valueIndex[0] = 0;
  chosen.valueIndex[1] = 1;
  const GameSaveStore::Prefs prefs = GameSaveStore::prefsOf(chosen, game.settings);
  EXPECT_EQ(prefsText(prefs), "2 level=Easy sound=Off");
  ASSERT_TRUE(GameSaveStore::savePrefs("counter", prefs));
  GameSaveStore::Prefs read;
  ASSERT_EQ(GameSaveStore::loadPrefs("counter", read), GameSaveStore::PrefsState::Loaded);
  const GameSaveStore::Choices back = GameSaveStore::resolvePrefs(read, game.manifest, game.settings, SOLO | PASS);
  EXPECT_EQ(back.mode, PASS);
  EXPECT_EQ(back.valueIndex[0], 0);
  EXPECT_EQ(back.valueIndex[1], 1);
}

}  // namespace
