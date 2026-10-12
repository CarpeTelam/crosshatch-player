#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

#include "GameLinkStore.h"
#include "HalMemoryStub.h"
#include "Logging.h"
#include "StateQueue.h"

// The nearby match's PSRAM pool (entry 8 of epic-play-nearby): one block of five 1,400 B queue buffers and the outbound
// buffer, made once, with the free mask that guards a double release. The HalMemory double puts a 4 KB guard band round
// every block, so a write past a buffer shows when the block is freed.

namespace {

using GameCore::NO_STATE_HANDLE;
using GameCore::StateHandle;

class GameLinkStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fakepsram::reset();
    fakelog::lines.clear();
  }
  void TearDown() override {
    EXPECT_EQ(fakepsram::liveBlocks, 0u) << "a PSRAM block was never freed";
    EXPECT_EQ(fakepsram::overruns, 0u) << "a PSRAM block was written past its end";
  }
};

TEST_F(GameLinkStoreTest, OneBlockOfSixBuffersInPsramAndNoneElsewhere) {
  auto store = GameLinkStore::create();
  ASSERT_NE(store, nullptr);
  EXPECT_TRUE(store->ok());
  EXPECT_EQ(fakepsram::liveBlocks, 1u);
  EXPECT_EQ(fakepsram::totalAllocations, 1u);
  EXPECT_EQ(fakepsram::lastRequestedBytes, 8400u);
  EXPECT_EQ(GameLinkStore::BLOCK_BYTES, 8400u);
  EXPECT_EQ(store->capacity(), 5u);
  EXPECT_EQ(store->capacity(), GameCore::StateQueue::DEPTH + 1) << "four queued and one being adopted";
  store.reset();
  EXPECT_EQ(fakepsram::liveBlocks, 0u) << "the destructor frees the block";
}

TEST_F(GameLinkStoreTest, AcquireHandsOutEachBufferOnceThenNone) {
  auto store = GameLinkStore::create();
  ASSERT_NE(store, nullptr);
  StateHandle handles[5];
  for (StateHandle& handle : handles) {
    handle = store->acquire();
    ASSERT_NE(handle, NO_STATE_HANDLE);
    EXPECT_EQ(store->bytes(handle).size(), 1400u);
  }
  for (size_t i = 0; i < 5; ++i) {
    for (size_t j = i + 1; j < 5; ++j) EXPECT_NE(handles[i], handles[j]);
  }
  EXPECT_EQ(store->inUse(), 5u);
  EXPECT_EQ(store->acquire(), NO_STATE_HANDLE);
}

TEST_F(GameLinkStoreTest, TheBuffersAndTheOutboundOneDoNotOverlap) {
  auto store = GameLinkStore::create();
  ASSERT_NE(store, nullptr);
  StateHandle handles[5];
  uint8_t fill = 1;
  for (StateHandle& handle : handles) {
    handle = store->acquire();
    const auto buffer = store->bytes(handle);
    std::fill(buffer.begin(), buffer.end(), fill++);
  }
  const auto outbound = store->outbound();
  ASSERT_EQ(outbound.size(), 1400u);
  std::fill(outbound.begin(), outbound.end(), 0x77);
  fill = 1;
  for (const StateHandle handle : handles) {
    const auto buffer = store->bytes(handle);
    EXPECT_TRUE(std::all_of(buffer.begin(), buffer.end(), [&](const uint8_t b) { return b == fill; }));
    ++fill;
  }
  EXPECT_TRUE(std::all_of(outbound.begin(), outbound.end(), [](const uint8_t b) { return b == 0x77; }));
  // The outbound buffer is never one of the pool's.
  for (const StateHandle handle : handles) EXPECT_NE(store->bytes(handle).data(), outbound.data());
}

TEST_F(GameLinkStoreTest, ReleaseReturnsABufferAndADoubleReleaseIsCounted) {
  auto store = GameLinkStore::create();
  ASSERT_NE(store, nullptr);
  const StateHandle first = store->acquire();
  ASSERT_NE(first, NO_STATE_HANDLE);
  store->release(first);
  EXPECT_EQ(store->badReleases(), 0u);
  EXPECT_EQ(store->inUse(), 0u);
  store->release(first);  // free already
  EXPECT_EQ(store->badReleases(), 1u);
  store->release(NO_STATE_HANDLE);  // a no-op, not a mistake
  store->release(200);              // out of range
  EXPECT_EQ(store->badReleases(), 2u);
  // The double release did not put the buffer on the free list twice: five acquires succeed, the sixth does not.
  for (int i = 0; i < 5; ++i) EXPECT_NE(store->acquire(), NO_STATE_HANDLE);
  EXPECT_EQ(store->acquire(), NO_STATE_HANDLE);
}

TEST_F(GameLinkStoreTest, AFreeOrInvalidHandleHasNoBytes) {
  auto store = GameLinkStore::create();
  ASSERT_NE(store, nullptr);
  const StateHandle handle = store->acquire();
  EXPECT_FALSE(store->bytes(handle).empty());
  store->release(handle);
  EXPECT_TRUE(store->bytes(handle).empty());
  EXPECT_TRUE(store->bytes(NO_STATE_HANDLE).empty());
  EXPECT_TRUE(store->bytes(5).empty()) << "the sixth buffer is not a pool handle";
}

TEST_F(GameLinkStoreTest, NullWhenPsramIsMissing) {
  fakepsram::failNext = true;
  EXPECT_EQ(GameLinkStore::create(), nullptr);
  EXPECT_EQ(fakepsram::liveBlocks, 0u);
  fakepsram::failAbove = 8399;
  EXPECT_EQ(GameLinkStore::create(), nullptr) << "the block is 8,400 B";
  fakepsram::failAbove = 8400;
  EXPECT_NE(GameLinkStore::create(), nullptr);
}

TEST_F(GameLinkStoreTest, TheQueueRunsOverItWithoutLeaking) {
  // The store is what the queue was written against: fill it, replace, take, release; nothing stays acquired.
  auto store = GameLinkStore::create();
  ASSERT_NE(store, nullptr);
  {
    GameCore::StateQueue queue(*store);
    ASSERT_TRUE(queue.ok());
    const uint8_t snapshot[3] = {1, 2, 3};
    GameCore::Status over;
    over.over = true;
    over.winners = 1;
    for (uint16_t ver = 1; ver <= 4; ++ver)
      EXPECT_EQ(queue.push(2, ver, over, snapshot), GameCore::StateQueue::Push::Queued);
    EXPECT_EQ(queue.push(2, 5, over, snapshot), GameCore::StateQueue::Push::Full);
    EXPECT_EQ(queue.overflows(), 1u);
    GameCore::QueuedState taken;
    ASSERT_TRUE(queue.take(taken));
    EXPECT_EQ(taken.ver, 1u);
    EXPECT_EQ(queue.bytes(taken).size(), 3u);
    queue.release(taken);
    EXPECT_EQ(store->inUse(), 3u);
  }
  EXPECT_EQ(store->inUse(), 0u) << "the queue's destructor returned what it held";
  EXPECT_EQ(store->badReleases(), 0u);
}

}  // namespace
