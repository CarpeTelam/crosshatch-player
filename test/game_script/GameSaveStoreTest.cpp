#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <vector>

#include "BlobHeader.h"
#include "Codec.h"
#include "GameSaveStore.h"
#include "HalStorage.h"
#include "Logging.h"
#include "Session.h"
#include "SnapshotMailbox.h"
#include "StoreSlot.h"

// GameSaveStore over a fake SD card (save_store_stubs/): store.bin's layout, the
// restore rules (a valid header and a table payload within 4,096 B, or nothing),
// the tmp-then-rename write, and the flush interval; and resume.bin's layout, its
// peek and load rules, the same write, and the flush a match makes from the VM's
// mailbox.

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

size_t opsStartingWith(const std::string& prefix) {
  size_t n = 0;
  for (const std::string& op : fakesd::ops) n += op.compare(0, prefix.size(), prefix) == 0;
  return n;
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
  EXPECT_EQ(fakesd::files[STORE], cat(header(), TAPS3));
  EXPECT_EQ(fakesd::files.count(TMP), 0u);
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
    fakesd::files[STORE] = c.file;
    open();
    EXPECT_TRUE(slotBytes().empty()) << c.reason;
    EXPECT_TRUE(fakelog::any(std::string("ERR GAME: counter: discarded ") + STORE + ": " + c.reason)) << c.reason;
    EXPECT_EQ(fakesd::files[STORE], c.file) << c.reason << ": a discarded save is never deleted";
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
    fakesd::files[STORE] = cat(header(), c.payload);
    open();
    EXPECT_TRUE(slotBytes().empty()) << c.reason;
    EXPECT_TRUE(fakelog::any(std::string("discarded ") + STORE + ": " + c.reason)) << c.reason;
  }
}

TEST_F(GameSaveStoreTest, AStoreAtTheLimitIsRestoredAndOneOverIsDiscarded) {
  const Bytes atLimit = bigTable(GameScript::Codec::STORE_LIMIT - 9);
  ASSERT_EQ(atLimit.size(), GameScript::Codec::STORE_LIMIT);
  fakesd::files[STORE] = cat(header(), atLimit);
  open();
  EXPECT_EQ(slotBytes(), atLimit);

  SetUp();
  fakesd::files[STORE] = cat(header(), bigTable(GameScript::Codec::STORE_LIMIT - 8));
  open();
  EXPECT_TRUE(slotBytes().empty());
  EXPECT_TRUE(fakelog::any(std::string("discarded ") + STORE + ": too large"));
}

TEST_F(GameSaveStoreTest, AWholeTmpIsReadWhenStoreBinIsMissing) {
  fakesd::files[TMP] = cat(header(), TAPS3);
  open();
  EXPECT_EQ(slotBytes(), TAPS3);
  EXPECT_TRUE(fakelog::any(std::string("no store.bin; reading ") + TMP));
}

TEST_F(GameSaveStoreTest, ATornTmpIsDiscardedAndIgnoredBesideStoreBin) {
  const Bytes torn = cat(header(), Bytes(TAPS4.begin(), TAPS4.begin() + 5));
  fakesd::files[TMP] = torn;
  open();
  EXPECT_TRUE(slotBytes().empty());
  EXPECT_TRUE(fakelog::any(std::string("discarded ") + TMP + ": truncated"));

  SetUp();
  fakesd::files[STORE] = cat(header(), TAPS3);
  fakesd::files[TMP] = torn;
  open();
  EXPECT_EQ(slotBytes(), TAPS3);
  EXPECT_FALSE(fakelog::any("discarded"));
}

TEST_F(GameSaveStoreTest, AWriteGoesThroughTheTmpThenARename) {
  fakesd::files[STORE] = cat(header(), TAPS3);
  open();
  fakesd::ops.clear();
  ASSERT_TRUE(slot->post(TAPS4));
  ASSERT_TRUE(saves->flush(*slot, 2000));
  const std::vector<std::string> expected = {
      std::string("open-write ") + TMP,
      std::string("close ") + TMP,
      std::string("remove ") + STORE,
      std::string("rename ") + TMP + " " + STORE,
  };
  EXPECT_EQ(fakesd::ops, expected);
  EXPECT_EQ(fakesd::files[STORE], cat(header(), TAPS4));
  EXPECT_EQ(fakesd::files.count(TMP), 0u);
}

TEST_F(GameSaveStoreTest, TheFirstWriteCreatesTheFolder) {
  open();
  ASSERT_TRUE(slot->post(TAPS3));
  ASSERT_TRUE(saves->flush(*slot, 2000));
  ASSERT_EQ(fakesd::directories.size(), 1u);
  EXPECT_EQ(fakesd::directories[0], "/.games-data/counter");
}

TEST_F(GameSaveStoreTest, AFailedWriteRemovesThePartialTmpAndKeepsTheSlotDirty) {
  for (bool* failure : {&fakesd::failOpenWrite, &fakesd::failWrite, &fakesd::failClose}) {
    SetUp();
    fakesd::files[STORE] = cat(header(), TAPS3);
    open();
    ASSERT_TRUE(slot->post(TAPS4));
    *failure = true;
    EXPECT_FALSE(saves->flush(*slot, 2000));
    EXPECT_TRUE(slot->dirty());
    EXPECT_EQ(fakesd::files.count(TMP), 0u);
    EXPECT_EQ(fakesd::files[STORE], cat(header(), TAPS3)) << "the previous save stays";
    EXPECT_TRUE(fakelog::any(std::string("ERR GAME: counter: cannot write ") + TMP));

    // The retry writes the latest contents.
    ASSERT_TRUE(saves->flush(*slot, 2001));
    EXPECT_EQ(fakesd::files[STORE], cat(header(), TAPS4));
    EXPECT_FALSE(slot->dirty());
  }
}

TEST_F(GameSaveStoreTest, AFailedRenameKeepsTheWholeTmpForTheNextLoad) {
  fakesd::files[STORE] = cat(header(), TAPS3);
  open();
  ASSERT_TRUE(slot->post(TAPS4));
  fakesd::failRename = true;
  EXPECT_FALSE(saves->flush(*slot, 2000));
  EXPECT_TRUE(slot->dirty());
  EXPECT_EQ(fakesd::files.count(STORE), 0u);
  EXPECT_EQ(fakesd::files[TMP], cat(header(), TAPS4));

  open();  // as after a restart
  EXPECT_EQ(slotBytes(), TAPS4);
}

// The retro's R5: a failed rename leaves the tmp as the only copy, and the next
// write must not truncate it before its own write has succeeded.
TEST_F(GameSaveStoreTest, TwoFailuresInARowKeepTheOnlyCopy) {
  for (bool* failure : {&fakesd::failOpenWrite, &fakesd::failWrite, &fakesd::failClose}) {
    SetUp();
    fakesd::files[STORE] = cat(header(), TAPS3);
    open();
    ASSERT_TRUE(slot->post(TAPS4));
    fakesd::failRename = true;
    EXPECT_FALSE(saves->flush(*slot, 2000));  // store.bin removed, the tmp holds TAPS4
    ASSERT_EQ(fakesd::files.count(STORE), 0u);

    const Bytes taps5 = {0x06, 0x00, 0x01, 0x05, 0x04, 't', 'a', 'p', 's', 0x03, 0x0A};
    ASSERT_TRUE(slot->post(taps5));
    fakesd::ops.clear();
    *failure = true;
    EXPECT_FALSE(saves->flush(*slot, 2001));
    EXPECT_TRUE(slot->dirty());
    EXPECT_EQ(fakesd::ops.front(), std::string("rename ") + TMP + " " + STORE) << "promoted before the write";
    EXPECT_EQ(fakesd::files[STORE], cat(header(), TAPS4)) << "the first flush's save stays";

    open();  // as after a restart
    EXPECT_EQ(slotBytes(), TAPS4);
  }
}

TEST_F(GameSaveStoreTest, AFailedPromotionWritesNothingAndKeepsTheTmp) {
  fakesd::files[TMP] = cat(header(), TAPS3);
  open();
  ASSERT_TRUE(slot->post(TAPS4));
  fakesd::ops.clear();
  fakesd::failRename = true;
  EXPECT_FALSE(saves->flush(*slot, 2000));
  EXPECT_TRUE(slot->dirty());
  const std::vector<std::string> expected = {std::string("rename ") + TMP + " " + STORE};
  EXPECT_EQ(fakesd::ops, expected);
  EXPECT_EQ(fakesd::files[TMP], cat(header(), TAPS3));
  EXPECT_TRUE(fakelog::any(std::string("cannot rename ") + TMP + " to " + STORE));

  // The retry promotes it, then writes the latest contents.
  ASSERT_TRUE(saves->flush(*slot, 2001));
  EXPECT_EQ(fakesd::files[STORE], cat(header(), TAPS4));
  EXPECT_EQ(fakesd::files.count(TMP), 0u);
}

TEST_F(GameSaveStoreTest, AFailedRemoveKeepsThePreviousSave) {
  fakesd::files[STORE] = cat(header(), TAPS3);
  open();
  ASSERT_TRUE(slot->post(TAPS4));
  fakesd::failRemove = true;
  EXPECT_FALSE(saves->flush(*slot, 2000));
  EXPECT_TRUE(slot->dirty());
  EXPECT_EQ(fakesd::files[STORE], cat(header(), TAPS3));
  EXPECT_TRUE(fakelog::any(std::string("cannot replace ") + STORE));
}

TEST_F(GameSaveStoreTest, FlushIfDueWritesADirtyStoreAtMostEveryFiveSeconds) {
  open(1000);
  ASSERT_TRUE(slot->post(TAPS3));
  EXPECT_TRUE(saves->flushIfDue(*slot, 5999));
  EXPECT_EQ(fakesd::files.count(STORE), 0u);
  EXPECT_TRUE(saves->flushIfDue(*slot, 6000));
  EXPECT_EQ(fakesd::files[STORE], cat(header(), TAPS3));

  // The interval runs from the last write.
  ASSERT_TRUE(slot->post(TAPS4));
  fakesd::ops.clear();
  EXPECT_TRUE(saves->flushIfDue(*slot, 10999));
  EXPECT_TRUE(fakesd::ops.empty());
  EXPECT_TRUE(saves->flushIfDue(*slot, 11000));
  EXPECT_EQ(fakesd::files[STORE], cat(header(), TAPS4));

  // Clean: nothing to write however long it has been.
  fakesd::ops.clear();
  EXPECT_TRUE(saves->flushIfDue(*slot, 60000));
  EXPECT_TRUE(fakesd::ops.empty());
}

TEST_F(GameSaveStoreTest, AFailedDueWriteRetriesAfterTheInterval) {
  open(0);
  ASSERT_TRUE(slot->post(TAPS3));
  fakesd::failOpenWrite = true;
  EXPECT_FALSE(saves->flushIfDue(*slot, 5000));
  fakesd::ops.clear();
  EXPECT_TRUE(saves->flushIfDue(*slot, 9999));
  EXPECT_TRUE(fakesd::ops.empty());
  EXPECT_TRUE(saves->flushIfDue(*slot, 10000));
  EXPECT_EQ(fakesd::files[STORE], cat(header(), TAPS3));
}

TEST_F(GameSaveStoreTest, FlushWritesAtOnceAndOnlyWhenDirty) {
  open(1000);
  EXPECT_TRUE(saves->flush(*slot, 1000));
  EXPECT_TRUE(fakesd::ops.empty());
  ASSERT_TRUE(slot->post(TAPS3));
  EXPECT_TRUE(saves->flush(*slot, 1001));
  EXPECT_EQ(fakesd::files[STORE], cat(header(), TAPS3));
  ASSERT_TRUE(slot->post(TAPS4));
  EXPECT_TRUE(saves->flush(*slot, 1002));
  EXPECT_EQ(fakesd::files[STORE], cat(header(), TAPS4));
}

TEST_F(GameSaveStoreTest, SaveStoreRefusesAnEmptyOrOversizedStore) {
  open();
  EXPECT_FALSE(saves->saveStore({}));
  const Bytes over(GameScript::Codec::STORE_LIMIT + 1, 0);
  EXPECT_FALSE(saves->saveStore(over));
  EXPECT_TRUE(fakesd::ops.empty());
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
  EXPECT_EQ(fakesd::files[RESUME], expected);
  EXPECT_EQ(fakesd::files[RESUME], resumeFile(TAPS3));
  EXPECT_EQ(fakesd::files.count(RESUME_TMP), 0u);
  EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::Valid);
  uint16_t ver = 99;
  EXPECT_EQ(loaded(ver), TAPS3);
  EXPECT_EQ(ver, 7u);
  EXPECT_FALSE(fakelog::any("ERR"));
}

TEST_F(GameSaveStoreTest, AResumeWriteGoesThroughTheTmpThenARename) {
  fakesd::files[RESUME] = resumeFile(TAPS3, 1);
  openResume();
  fakesd::ops.clear();
  ASSERT_TRUE(saves->saveResume(TAPS4, 2));
  const std::vector<std::string> expected = {
      std::string("open-write ") + RESUME_TMP,
      std::string("close ") + RESUME_TMP,
      std::string("remove ") + RESUME,
      std::string("rename ") + RESUME_TMP + " " + RESUME,
  };
  EXPECT_EQ(fakesd::ops, expected);
  EXPECT_EQ(fakesd::files[RESUME], resumeFile(TAPS4, 2));
}

TEST_F(GameSaveStoreTest, VerIsKeptToItsLow16Bits) {
  openResume();
  ASSERT_TRUE(saves->saveResume(TAPS3, 0x12345));
  EXPECT_EQ(fakesd::files[RESUME], resumeFile(TAPS3, 0x2345));
  uint16_t ver = 0;
  EXPECT_EQ(loaded(ver), TAPS3);
  EXPECT_EQ(ver, 0x2345u);

  ASSERT_TRUE(saves->saveResume(TAPS3, 65536));
  EXPECT_EQ(fakesd::files[RESUME], resumeFile(TAPS3, 0));
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

  fakesd::ops.clear();
  EXPECT_FALSE(saves->saveResume(bigTable(GameCore::SNAPSHOT_BYTES - 8), 4));
  EXPECT_FALSE(saves->saveResume({}, 4));
  EXPECT_TRUE(fakesd::ops.empty());
  EXPECT_EQ(fakesd::files[RESUME], resumeFile(atLimit, 3));
}

TEST_F(GameSaveStoreTest, NothingIsSavedOrLoadedBeforeThePackageHashIsKnown) {
  open();
  fakesd::files[RESUME] = resumeFile(TAPS3);
  fakesd::ops.clear();
  EXPECT_FALSE(saves->saveResume(TAPS4, 8));
  uint16_t ver = 5;
  EXPECT_TRUE(loaded(ver).empty());
  EXPECT_EQ(ver, 0u);
  EXPECT_TRUE(fakesd::ops.empty());
  EXPECT_EQ(fakesd::files[RESUME], resumeFile(TAPS3));
}

TEST_F(GameSaveStoreTest, AResumeThatDoesNotFitTheGameIsDiscardedWithALogLineAndKept) {
  const Bytes big = bigTable(GameCore::SNAPSHOT_BYTES - 8);  // 1,401 bytes
  const struct {
    Bytes file;
    const char* reason;
  } cases[] = {
      {Bytes{'C', 'H', 'R'}, "truncated"},
      {firstBytes(resumeFile(TAPS3), 12), "truncated"},
      {firstBytes(resumeFile(TAPS3), 17), "truncated"},
      {resumeFile(TAPS3, 7, PKG, 0, 1, "XXXX"), "bad_magic"},
      {resumeFile(TAPS3, 7, PKG, 0, 1, "CHRS", 2), "unknown_file_version"},
      {resumeFile(TAPS3, 7, PKG, 0, 1, "CHRS", 1, 2), "unknown_codec_version"},
      {resumeFile(TAPS3, 7, PKG, 1), "not a solo save"},
      {resumeFile(TAPS3, 7, PKG, 0, 2), "not a solo save"},
      {resumeFile(TAPS3, 7, PKG, 0, 0), "not a solo save"},
      {resumeFile(Bytes{}), "empty snapshot"},
      {resumeFile(big), "too large"},
  };
  for (const auto& c : cases) {
    SetUp();
    fakesd::files[RESUME] = c.file;
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
    EXPECT_EQ(fakesd::files[RESUME], c.file) << c.reason << ": a discarded save is never deleted";
  }
}

TEST_F(GameSaveStoreTest, ASnapshotThatIsNotCanonicalCodecBytesIsDiscardedByPeekAndLoadAlike) {
  fakesd::files[RESUME] = resumeFile(cat(TAPS3, Bytes{0x00}));
  openResume();
  EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::None)
      << "Continue is offered only for a save that resumes";
  EXPECT_TRUE(fakelog::any(std::string("discarded ") + RESUME + ": trailing"));
  fakelog::lines.clear();
  uint16_t ver = 0;
  EXPECT_TRUE(loaded(ver).empty());
  EXPECT_TRUE(fakelog::any(std::string("discarded ") + RESUME + ": trailing"));
  EXPECT_EQ(fakesd::files[RESUME], resumeFile(cat(TAPS3, Bytes{0x00}))) << "and the file stays";
}

TEST_F(GameSaveStoreTest, PeekIsUnreadableWithALogLineWhenItCannotAllocateItsBufferAndTheFileStays) {
  fakesd::files[RESUME] = resumeFile(TAPS3);
  openResume();
  ASSERT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::Valid)
      << "the same save is valid when memory is there";
  fakelog::lines.clear();
  failNextNothrowNew = true;
  // Not None: a file is there that could not be checked, and the launcher must not take it for no save.
  EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::Unreadable);
  failNextNothrowNew = false;
  EXPECT_TRUE(fakelog::any(std::string("ERR GAME: counter: OOM: 1400 bytes to check ") + RESUME));
  EXPECT_EQ(fakesd::files[RESUME], resumeFile(TAPS3));
  EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::Valid) << "and the next call is fine";
}

// Retro deferral 4.12 (ADV2): "no save" and "could not tell" are different answers. A save whose file would not
// open or read is Unreadable, in peek and in loadResume alike; a save that was read and refused is None.
TEST_F(GameSaveStoreTest, ASaveThatCannotBeOpenedOrReadIsUnreadableNotAbsentAndStays) {
  for (bool* failure : {&fakesd::failOpenRead, &fakesd::failRead}) {
    SetUp();
    fakesd::files[RESUME] = resumeFile(TAPS3);
    openResume();
    *failure = true;
    EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::Unreadable);
    EXPECT_TRUE(fakelog::any(std::string("ERR GAME: counter: discarded ") + RESUME + ": cannot "));
    EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::Valid) << "and the next try reads it";

    *failure = true;
    uint16_t ver = 5;
    bool unreadable = false;
    EXPECT_TRUE(saves->loadResume(ver, unreadable).empty());
    EXPECT_TRUE(unreadable);
    EXPECT_EQ(ver, 0u);
    EXPECT_EQ(fakesd::files[RESUME], resumeFile(TAPS3)) << "the file is untouched";
    const std::span<const uint8_t> again = saves->loadResume(ver, unreadable);
    EXPECT_EQ(Bytes(again.begin(), again.end()), TAPS3) << "and the next try reads it";
    EXPECT_FALSE(unreadable);
  }
  // A file that was read and is not a save is None, and loadResume says it was no fault.
  SetUp();
  fakesd::files[RESUME] = resumeFile(TAPS3, 7, OTHER_PKG);
  openResume();
  bool unreadable = true;
  uint16_t ver = 0;
  EXPECT_TRUE(saves->loadResume(ver, unreadable).empty());
  EXPECT_FALSE(unreadable);
  EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::None);
}

// The launcher asks again on every build, and a save of another package stays, so peek does not log it as an error;
// the match that would resume it still does (loadResume).
TEST_F(GameSaveStoreTest, ASaveOfAnotherPackageIsLoggedQuietlyByPeekAndAtErrorByLoad) {
  fakesd::files[RESUME] = resumeFile(TAPS3, 7, OTHER_PKG);
  openResume();
  EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::None);
  EXPECT_TRUE(fakelog::any(std::string("INF GAME: counter: ") + RESUME + " is another package's save"));
  EXPECT_FALSE(fakelog::any("ERR"));
  fakelog::lines.clear();
  uint16_t ver = 0;
  EXPECT_TRUE(loaded(ver).empty());
  EXPECT_TRUE(fakelog::any(std::string("ERR GAME: counter: discarded ") + RESUME + ": other package"));
  EXPECT_EQ(fakesd::files[RESUME], resumeFile(TAPS3, 7, OTHER_PKG)) << "and the file stays";
}

TEST_F(GameSaveStoreTest, NoResumeIsNotAnError) {
  openResume();
  EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::None);
  uint16_t ver = 0;
  EXPECT_TRUE(loaded(ver).empty());
  EXPECT_TRUE(fakelog::lines.empty());
}

TEST_F(GameSaveStoreTest, AWholeResumeTmpIsReadWhenResumeBinIsMissing) {
  fakesd::files[RESUME_TMP] = resumeFile(TAPS3, 9);
  openResume();
  EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::Valid);
  uint16_t ver = 0;
  EXPECT_EQ(loaded(ver), TAPS3);
  EXPECT_EQ(ver, 9u);
  EXPECT_TRUE(fakelog::any(std::string("no resume.bin; reading ") + RESUME_TMP));
}

TEST_F(GameSaveStoreTest, ATornResumeTmpIsDiscardedAndIgnoredBesideResumeBin) {
  const Bytes torn = firstBytes(resumeFile(TAPS4), 9);
  fakesd::files[RESUME_TMP] = torn;
  openResume();
  EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::None);
  uint16_t ver = 0;
  EXPECT_TRUE(loaded(ver).empty());

  SetUp();
  fakesd::files[RESUME] = resumeFile(TAPS3);
  fakesd::files[RESUME_TMP] = torn;
  openResume();
  EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::Valid);
  EXPECT_EQ(loaded(ver), TAPS3);
  EXPECT_FALSE(fakelog::any("discarded"));
}

TEST_F(GameSaveStoreTest, AFailedResumeWriteKeepsThePreviousSaveAndLeavesNoTmp) {
  for (bool* failure : {&fakesd::failOpenWrite, &fakesd::failWrite, &fakesd::failClose}) {
    SetUp();
    fakesd::files[RESUME] = resumeFile(TAPS3, 1);
    openResume();
    *failure = true;
    EXPECT_FALSE(saves->saveResume(TAPS4, 2));
    EXPECT_EQ(fakesd::files.count(RESUME_TMP), 0u);
    EXPECT_EQ(fakesd::files[RESUME], resumeFile(TAPS3, 1)) << "the previous save stays";
    EXPECT_TRUE(fakelog::any(std::string("ERR GAME: counter: cannot write ") + RESUME_TMP));
  }
}

TEST_F(GameSaveStoreTest, AFailedResumeRenameKeepsTheWholeTmpAsTheOnlyCopy) {
  fakesd::files[RESUME] = resumeFile(TAPS3, 1);
  openResume();
  fakesd::failRename = true;
  EXPECT_FALSE(saves->saveResume(TAPS4, 2));
  EXPECT_EQ(fakesd::files.count(RESUME), 0u);
  EXPECT_EQ(fakesd::files[RESUME_TMP], resumeFile(TAPS4, 2));

  // A second failure does not truncate it: it is promoted before the write.
  for (bool* failure : {&fakesd::failOpenWrite, &fakesd::failWrite, &fakesd::failClose}) {
    fakesd::ops.clear();
    *failure = true;
    EXPECT_FALSE(saves->saveResume(TAPS3, 3));
    EXPECT_EQ(fakesd::ops.front(), std::string("rename ") + RESUME_TMP + " " + RESUME);
    EXPECT_EQ(fakesd::files[RESUME], resumeFile(TAPS4, 2));
    // Put it back as a lone tmp for the next failure.
    fakesd::files[RESUME_TMP] = fakesd::files[RESUME];
    fakesd::files.erase(RESUME);
  }
  uint16_t ver = 0;
  EXPECT_EQ(loaded(ver), TAPS4);
  EXPECT_EQ(ver, 2u);
}

TEST_F(GameSaveStoreTest, DeleteResumeRemovesTheFileAndItsTmp) {
  fakesd::files[RESUME] = resumeFile(TAPS3);
  fakesd::files[RESUME_TMP] = resumeFile(TAPS4);
  fakesd::files["/.games-data/counter/store.bin"] = cat(header(), TAPS3);
  openResume();
  EXPECT_TRUE(saves->deleteResume());
  EXPECT_EQ(fakesd::files.count(RESUME), 0u);
  EXPECT_EQ(fakesd::files.count(RESUME_TMP), 0u);
  EXPECT_EQ(fakesd::files.count("/.games-data/counter/store.bin"), 1u) << "store.bin is not resume.bin";
  EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::None);

  fakesd::ops.clear();
  EXPECT_TRUE(saves->deleteResume()) << "nothing to delete is done";
  EXPECT_TRUE(fakesd::ops.empty());
}

TEST_F(GameSaveStoreTest, AFailedDeleteIsLoggedAndReported) {
  fakesd::files[RESUME] = resumeFile(TAPS3);
  openResume();
  fakesd::failRemove = true;
  EXPECT_FALSE(saves->deleteResume());
  EXPECT_EQ(fakesd::files.count(RESUME), 1u);
  EXPECT_TRUE(fakelog::any(std::string("ERR GAME: counter: cannot delete ") + RESUME));
}

// ---- flushResume: what a match writes from the VM's mailbox ----

TEST_F(GameSaveStoreTest, FlushResumeWritesTheLatestPublishedSnapshotOnceAndAtOnce) {
  openResume(1000);
  EXPECT_TRUE(saves->flushResume(*mailbox, 1000));
  EXPECT_TRUE(fakesd::ops.empty()) << "nothing published, nothing written";

  ASSERT_TRUE(mailbox->publish(TAPS3, 5, false));
  ASSERT_TRUE(mailbox->publish(TAPS4, 6, false));  // latest wins
  EXPECT_TRUE(saves->flushResume(*mailbox, 1001)) << "not held back by the store's 5 s interval";
  EXPECT_EQ(fakesd::files[RESUME], resumeFile(TAPS4, 6));
  EXPECT_FALSE(mailbox->pending());

  fakesd::ops.clear();
  EXPECT_TRUE(saves->flushResume(*mailbox, 1002));
  EXPECT_TRUE(fakesd::ops.empty()) << "written once";

  // Successive successes are never throttled.
  ASSERT_TRUE(mailbox->publish(TAPS3, 7, false));
  EXPECT_TRUE(saves->flushResume(*mailbox, 1003));
  EXPECT_EQ(fakesd::files[RESUME], resumeFile(TAPS3, 7));
}

TEST_F(GameSaveStoreTest, FlushResumeWritesNothingWithoutAPackageHash) {
  open();
  mailboxStorage.assign(GameCore::SNAPSHOT_BYTES, 0);
  SnapshotMailbox bare{std::span<uint8_t>(mailboxStorage)};
  ASSERT_TRUE(bare.publish(TAPS3, 5, false));
  EXPECT_TRUE(saves->flushResume(bare, 1000));
  EXPECT_TRUE(fakesd::ops.empty());
  EXPECT_TRUE(fakesd::files.empty());
}

TEST_F(GameSaveStoreTest, AFailedResumeWriteStaysPendingAndIsRetriedOnlyAfterTheInterval) {
  openResume(0);
  ASSERT_TRUE(mailbox->publish(TAPS3, 5, false));
  fakesd::failOpenWrite = true;
  EXPECT_FALSE(saves->flushResume(*mailbox, 2000));
  EXPECT_TRUE(mailbox->pending());

  fakesd::ops.clear();
  EXPECT_TRUE(saves->flushResume(*mailbox, 2001));
  EXPECT_TRUE(saves->flushResume(*mailbox, 6999));
  EXPECT_TRUE(fakesd::ops.empty()) << "held back until the interval has passed";
  EXPECT_TRUE(mailbox->pending());

  // A newer snapshot meanwhile is what the retry writes.
  ASSERT_TRUE(mailbox->publish(TAPS4, 6, false));
  EXPECT_TRUE(saves->flushResume(*mailbox, 7000));
  EXPECT_EQ(fakesd::files[RESUME], resumeFile(TAPS4, 6));
  EXPECT_FALSE(mailbox->pending());

  // The throttle is over once a write has succeeded.
  ASSERT_TRUE(mailbox->publish(TAPS3, 7, false));
  EXPECT_TRUE(saves->flushResume(*mailbox, 7001));
  EXPECT_EQ(fakesd::files[RESUME], resumeFile(TAPS3, 7));
}

TEST_F(GameSaveStoreTest, ASnapshotWhoseStatusIsOverDeletesTheSaveInsteadOfBeingWritten) {
  fakesd::files[RESUME] = resumeFile(TAPS3, 1);
  fakesd::files[RESUME_TMP] = resumeFile(TAPS3, 1);
  openResume();
  ASSERT_TRUE(mailbox->publish(TAPS4, 2, true));
  fakesd::ops.clear();
  EXPECT_TRUE(saves->flushResume(*mailbox, 1001));
  EXPECT_EQ(fakesd::files.count(RESUME), 0u);
  EXPECT_EQ(fakesd::files.count(RESUME_TMP), 0u);
  EXPECT_EQ(opsStartingWith("open-write"), 0u) << "an over snapshot is never written";
  EXPECT_FALSE(mailbox->pending());
  EXPECT_EQ(GameSaveStore::peek("counter", PKG), GameSaveStore::SaveState::None);
}

TEST_F(GameSaveStoreTest, AFailedDeleteForAnOverSnapshotIsRetriedLikeAFailedWrite) {
  fakesd::files[RESUME] = resumeFile(TAPS3, 1);
  openResume(0);
  ASSERT_TRUE(mailbox->publish(TAPS4, 2, true));
  fakesd::failRemove = true;
  EXPECT_FALSE(saves->flushResume(*mailbox, 2000));
  EXPECT_TRUE(mailbox->pending());
  EXPECT_EQ(fakesd::files.count(RESUME), 1u);
  EXPECT_TRUE(saves->flushResume(*mailbox, 3000)) << "throttled";
  EXPECT_EQ(fakesd::files.count(RESUME), 1u);
  EXPECT_TRUE(saves->flushResume(*mailbox, 7000));
  EXPECT_EQ(fakesd::files.count(RESUME), 0u);
}

TEST_F(GameSaveStoreTest, ANewRoundsSnapshotIsWrittenAgainAfterAnOverOne) {
  openResume();
  ASSERT_TRUE(mailbox->publish(TAPS3, 5, true));
  EXPECT_TRUE(saves->flushResume(*mailbox, 1001));
  EXPECT_EQ(fakesd::files.count(RESUME), 0u);
  ASSERT_TRUE(mailbox->publish(TAPS4, 6, false));
  EXPECT_TRUE(saves->flushResume(*mailbox, 1002));
  EXPECT_EQ(fakesd::files[RESUME], resumeFile(TAPS4, 6));
}

}  // namespace
