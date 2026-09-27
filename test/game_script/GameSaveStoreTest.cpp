#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "BlobHeader.h"
#include "Codec.h"
#include "GameSaveStore.h"
#include "HalStorage.h"
#include "Logging.h"
#include "StoreSlot.h"

// GameSaveStore over a fake SD card (save_store_stubs/): store.bin's layout, the
// restore rules (a valid header and a table payload within 4,096 B, or nothing),
// the tmp-then-rename write, and the flush interval.

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

  Bytes slotStorage;
  Bytes buffer;
  std::unique_ptr<StoreSlot> slot;
  std::unique_ptr<GameSaveStore> saves;
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

}  // namespace
