#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "ArenaAllocator.h"

using GameScript::ArenaAllocator;

namespace {

class ArenaAllocatorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    block.resize(64 * 1024);
    arena.reset(block.data(), block.size());
  }

  std::vector<uint8_t> block;
  ArenaAllocator arena;
};

bool aligned(const void* p) { return reinterpret_cast<uintptr_t>(p) % alignof(std::max_align_t) == 0; }

TEST_F(ArenaAllocatorTest, AllocatesAlignedDistinctBlocksInsideTheArena) {
  void* a = arena.allocate(1);
  void* b = arena.allocate(24);
  void* c = arena.allocate(1000);
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  ASSERT_NE(c, nullptr);
  EXPECT_TRUE(aligned(a) && aligned(b) && aligned(c));
  for (void* p : {a, b, c}) {
    EXPECT_GE(static_cast<uint8_t*>(p), block.data());
    EXPECT_LT(static_cast<uint8_t*>(p), block.data() + block.size());
  }
  std::memset(a, 0xAA, 1);
  std::memset(b, 0xBB, 24);
  std::memset(c, 0xCC, 1000);
  EXPECT_EQ(static_cast<uint8_t*>(a)[0], 0xAA);
  EXPECT_EQ(static_cast<uint8_t*>(b)[23], 0xBB);
  EXPECT_GT(arena.bytesInUse(), 1025u);
}

TEST_F(ArenaAllocatorTest, ExhaustsThenRecoversAfterFreeing) {
  std::vector<void*> blocks;
  while (void* p = arena.allocate(1000)) blocks.push_back(p);
  ASSERT_GT(blocks.size(), 50u);
  EXPECT_EQ(arena.allocate(1000), nullptr);
  EXPECT_EQ(arena.allocate(arena.capacity() + 1), nullptr);
  for (void* p : blocks) arena.release(p);
  EXPECT_EQ(arena.bytesInUse(), 0u);
  EXPECT_GE(arena.peakBytes(), blocks.size() * 1000);
  // Everything coalesced back: one block of nearly the whole arena fits again.
  EXPECT_NE(arena.allocate(arena.capacity() - 256), nullptr);
}

TEST_F(ArenaAllocatorTest, CoalescesNeighboursInAnyOrder) {
  void* a = arena.allocate(8000);
  void* b = arena.allocate(8000);
  void* c = arena.allocate(8000);
  void* rest = arena.allocate(arena.capacity() - 3 * 8100);
  ASSERT_TRUE(a && b && c && rest);
  EXPECT_EQ(arena.allocate(20000), nullptr);
  arena.release(a);
  arena.release(c);
  EXPECT_EQ(arena.allocate(20000), nullptr);  // two separate holes
  arena.release(b);                           // joins both
  EXPECT_NE(arena.allocate(20000), nullptr);
}

TEST_F(ArenaAllocatorTest, ReallocateGrowsInPlaceMovesAndShrinks) {
  auto* p = static_cast<uint8_t*>(arena.allocate(100));
  ASSERT_NE(p, nullptr);
  for (int i = 0; i < 100; ++i) p[i] = static_cast<uint8_t>(i);
  // Free space follows, so growth stays in place.
  auto* grown = static_cast<uint8_t*>(arena.reallocate(p, 400));
  EXPECT_EQ(grown, p);
  // A neighbour blocks in-place growth, so the data moves intact.
  void* fence = arena.allocate(16);
  ASSERT_NE(fence, nullptr);
  auto* moved = static_cast<uint8_t*>(arena.reallocate(grown, 4000));
  ASSERT_NE(moved, nullptr);
  EXPECT_NE(moved, grown);
  for (int i = 0; i < 100; ++i) ASSERT_EQ(moved[i], static_cast<uint8_t>(i));
  const size_t before = arena.bytesInUse();
  auto* shrunk = static_cast<uint8_t*>(arena.reallocate(moved, 10));
  EXPECT_EQ(shrunk, moved);
  EXPECT_LT(arena.bytesInUse(), before);
  EXPECT_EQ(shrunk[9], 9);
  // A failed growth leaves the old block valid.
  EXPECT_EQ(arena.reallocate(shrunk, arena.capacity()), nullptr);
  EXPECT_EQ(shrunk[5], 5);
}

TEST_F(ArenaAllocatorTest, LuaAllocFollowsTheLuaContract) {
  void* p = ArenaAllocator::luaAlloc(&arena, nullptr, 5 /* a type tag, not a size */, 64);
  ASSERT_NE(p, nullptr);
  void* q = ArenaAllocator::luaAlloc(&arena, p, 64, 128);
  ASSERT_NE(q, nullptr);
  EXPECT_EQ(ArenaAllocator::luaAlloc(&arena, q, 128, 0), nullptr);
  EXPECT_EQ(ArenaAllocator::luaAlloc(&arena, nullptr, 0, 0), nullptr);
  EXPECT_EQ(arena.bytesInUse(), 0u);
}

TEST_F(ArenaAllocatorTest, LuaAllocCapsLuaBytesNotArenaBytes) {
  EXPECT_EQ(arena.luaLimit(), GameScript::LUA_HEAP_BYTES);
  arena.setLuaLimit(1000);
  void* a = ArenaAllocator::luaAlloc(&arena, nullptr, 5, 600);
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(arena.luaBytes(), 600u);
  // The arena has room, but the cap does not.
  EXPECT_EQ(ArenaAllocator::luaAlloc(&arena, nullptr, 5, 401), nullptr);
  void* b = ArenaAllocator::luaAlloc(&arena, nullptr, 5, 400);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(arena.luaBytes(), 1000u);
  // Growth past the cap fails and leaves the block; shrinking always works.
  EXPECT_EQ(ArenaAllocator::luaAlloc(&arena, a, 600, 601), nullptr);
  void* a2 = ArenaAllocator::luaAlloc(&arena, a, 600, 100);
  ASSERT_NE(a2, nullptr);
  EXPECT_EQ(arena.luaBytes(), 500u);
  // Allocations made directly (codec scratch) are not Lua's.
  void* scratch = arena.allocate(4000);
  ASSERT_NE(scratch, nullptr);
  EXPECT_EQ(arena.luaBytes(), 500u);
  // A cap lowered below what Lua holds refuses all growth.
  arena.setLuaLimit(200);
  EXPECT_EQ(ArenaAllocator::luaAlloc(&arena, nullptr, 5, 1), nullptr);
  ArenaAllocator::luaAlloc(&arena, a2, 100, 0);
  ArenaAllocator::luaAlloc(&arena, b, 400, 0);
  EXPECT_EQ(arena.luaBytes(), 0u);
  arena.release(scratch);
  // reset() clears the count and keeps the cap.
  arena.reset(block.data(), block.size());
  EXPECT_EQ(arena.luaBytes(), 0u);
  EXPECT_EQ(arena.luaLimit(), 200u);
}

TEST(ArenaAllocatorEdgeTest, UnalignedAndTinyBlocks) {
  std::vector<uint8_t> block(4096);
  ArenaAllocator arena;
  arena.reset(block.data() + 3, block.size() - 3);
  void* p = arena.allocate(100);
  ASSERT_NE(p, nullptr);
  EXPECT_TRUE(aligned(p));
  arena.reset(block.data(), 8);
  EXPECT_EQ(arena.capacity(), 0u);
  EXPECT_EQ(arena.allocate(1), nullptr);
}

}  // namespace
