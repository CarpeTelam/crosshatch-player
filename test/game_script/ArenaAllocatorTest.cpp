#include <Session.h>
#include <gtest/gtest.h>

#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <lua.hpp>
#include <vector>

#include "ArenaAllocator.h"
#include "LuaGame.h"

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

TEST_F(ArenaAllocatorTest, BlockBytesBoundsWhatAnAllocationTakes) {
  // LuaGame's static_assert sizes the scratch reserve with blockBytes.
  for (const size_t size : {size_t{0}, size_t{1}, size_t{15}, size_t{16}, size_t{17}, size_t{1000}, size_t{13720}}) {
    arena.reset(block.data(), block.size());
    const size_t before = arena.bytesInUse();
    void* p = arena.allocate(size);
    ASSERT_NE(p, nullptr) << size;
    EXPECT_LE(arena.bytesInUse() - before, ArenaAllocator::blockBytes(size)) << size;
    EXPECT_GE(ArenaAllocator::blockBytes(size), size + ArenaAllocator::HEADER) << size;
  }
  // An exact fit whose remainder is too small to split keeps the whole block.
  const size_t size = 1000;
  const size_t whole = ArenaAllocator::blockBytes(size) - ArenaAllocator::ALIGN;
  arena.reset(block.data(), whole);
  ASSERT_NE(arena.allocate(size), nullptr);
  EXPECT_LE(arena.bytesInUse(), ArenaAllocator::blockBytes(size));
}

TEST_F(ArenaAllocatorTest, CreateConstructsInTheArenaAndDestroyFrees) {
  struct Counted {
    explicit Counted(int& live) : live(live) { ++live; }
    ~Counted() { --live; }
    int& live;
  };
  int live = 0;
  Counted* object = arena.create<Counted>(live);
  ASSERT_NE(object, nullptr);
  EXPECT_EQ(live, 1);
  EXPECT_GT(arena.bytesInUse(), 0u);
  arena.destroy(object);
  EXPECT_EQ(live, 0);
  EXPECT_EQ(arena.bytesInUse(), 0u);
  arena.destroy<Counted>(nullptr);  // a no-op
  arena.reset(block.data(), 8);     // too small for any block
  EXPECT_EQ(arena.create<Counted>(live), nullptr);
  EXPECT_EQ(live, 0);
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

TEST(ArenaAllocatorRegionTest, SplitKeepsLuaAndTheReserveApart) {
  std::vector<uint8_t> block(24 * 1024);
  ArenaAllocator arena;
  arena.split(block.data(), 16 * 1024, 8 * 1024);
  const uint8_t* reserveStart = block.data() + 16 * 1024;
  EXPECT_EQ(arena.luaRegionCapacity(), 16u * 1024);
  EXPECT_EQ(arena.reserveCapacity(), 8u * 1024);
  EXPECT_EQ(arena.capacity(), 24u * 1024);
  auto* runtime = static_cast<uint8_t*>(arena.allocate(1000));
  ASSERT_NE(runtime, nullptr);
  EXPECT_GE(runtime, reserveStart);
  // Lua takes its whole region, and only its region.
  std::vector<void*> lua;
  while (void* p = ArenaAllocator::luaAlloc(&arena, nullptr, 5, 500)) {
    EXPECT_LT(static_cast<uint8_t*>(p), reserveStart);
    lua.push_back(p);
  }
  EXPECT_GT(lua.size(), 20u);
  EXPECT_EQ(arena.luaRegionRefusals(), 1u);
  EXPECT_EQ(arena.luaCapRefusals(), 0u);
  EXPECT_NE(arena.allocate(4000), nullptr);  // the reserve still has room
  EXPECT_EQ(arena.bytesInUse(), arena.luaRegionInUse() + arena.reserveInUse());
  for (void* p : lua) ArenaAllocator::luaAlloc(&arena, p, 500, 0);
  EXPECT_EQ(arena.luaRegionInUse(), 0u);
  EXPECT_EQ(arena.luaBytes(), 0u);
  // reset() goes back to one region that both share.
  arena.reset(block.data(), block.size());
  EXPECT_EQ(arena.luaRegionCapacity(), arena.reserveCapacity());
  EXPECT_EQ(arena.luaRegionRefusals(), 0u);
  void* p = ArenaAllocator::luaAlloc(&arena, nullptr, 5, 64);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(arena.reserveInUse(), arena.luaRegionInUse());
  ArenaAllocator::luaAlloc(&arena, p, 64, 0);
}

// Runs `chunk` in a Lua state over `arena` with the libraries a heap bomb needs;
// returns its status (LUA_ERRMEM when the heap ran out) and the Lua bytes held then.
struct LuaRun {
  int status;
  size_t luaBytes;
};
LuaRun runLua(ArenaAllocator& arena, const char* chunk) {
  lua_State* L = lua_newstate(&ArenaAllocator::luaAlloc, &arena, 1);
  EXPECT_NE(L, nullptr);
  if (!L) return {LUA_ERRMEM, 0};
  luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
  luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
  luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
  lua_settop(L, 0);
  int status = luaL_loadstring(L, chunk);
  if (status == LUA_OK) status = lua_pcall(L, 0, 0, 0);
  const LuaRun run{status, arena.luaBytes()};
  lua_close(L);
  return run;
}

// The heaps that cost the region the most per Lua byte: the heap fault fixture's
// strings, closures with an upvalue each, a mixed heap, and a hash of string keys.
constexpr const char* HEAP_BOMBS[] = {
    "local t = {} for i = 1, 1e7 do t[i] = string.rep('x', 64) .. i end",
    "local t = {} for i = 1, 1e7 do t[i] = function() return i end end",
    "local t = {} for i = 1, 1e7 do t[i] = {i, 'n' .. i, function() return i end} end",
    "local t = {} for i = 1, 1e7 do t['k' .. i] = true end",
};

// Lua's region is sized so the count cap binds first whatever the header size:
// with 16 B headers (this host) and 8 B (the device's, modelled by 8 B alignment),
// every bomb ends on the cap, never on the region, at the same Lua byte count.
TEST(ArenaAllocatorRegionTest, AHeapBombEndsOnTheCapWithEitherHeaderSize) {
  static_assert(GameScript::arenaHeaderBytes(16) == 16 && GameScript::arenaHeaderBytes(8) == 8);
  std::vector<uint8_t> block(GameScript::ARENA_BYTES);
  for (const char* bomb : HEAP_BOMBS) {
    size_t usable[2] = {};
    int index = 0;
    for (const size_t align : {size_t{16}, size_t{8}}) {
      ArenaAllocator arena(align);
      arena.split(block.data(), GameScript::LUA_REGION_BYTES, GameScript::SCRATCH_RESERVE_BYTES);
      const LuaRun run = runLua(arena, bomb);
      EXPECT_EQ(run.status, LUA_ERRMEM) << bomb;
      EXPECT_GT(arena.luaCapRefusals(), 0u) << align << " B alignment: " << bomb;
      EXPECT_EQ(arena.luaRegionRefusals(), 0u) << align << " B alignment: " << bomb;
      EXPECT_LT(arena.peakBytes(), arena.luaRegionCapacity()) << align << " B alignment: " << bomb;
      EXPECT_EQ(arena.luaRegionInUse(), 0u);
      usable[index++] = run.luaBytes;
    }
    EXPECT_EQ(usable[0], usable[1]) << bomb;
    EXPECT_GT(usable[0], GameScript::LUA_HEAP_BYTES * 3 / 4) << bomb;
  }
}

// The Session and codec scratch sit in the reserve, so a Lua heap that runs out of
// its own region (here one smaller than the cap) never touches them.
TEST(ArenaAllocatorRegionTest, TheReserveStaysIntactWhenLuaExhaustsItsRegion) {
  constexpr size_t LUA_REGION = 64 * 1024;
  constexpr size_t SESSION = sizeof(GameCore::Session);
  constexpr size_t SCRATCH = GameScript::LuaGame::SCRATCH_BYTES;
  std::vector<uint8_t> block(LUA_REGION + GameScript::SCRATCH_RESERVE_BYTES);
  ArenaAllocator arena;
  arena.split(block.data(), LUA_REGION, GameScript::SCRATCH_RESERVE_BYTES);
  auto* session = static_cast<uint8_t*>(arena.allocate(SESSION));
  auto* scratch = static_cast<uint8_t*>(arena.allocate(SCRATCH));
  ASSERT_NE(session, nullptr);
  ASSERT_NE(scratch, nullptr);
  std::memset(session, 0x5A, SESSION);
  std::memset(scratch, 0xC3, SCRATCH);
  const size_t reserved = arena.reserveInUse();
  EXPECT_EQ(runLua(arena, HEAP_BOMBS[0]).status, LUA_ERRMEM);
  EXPECT_GT(arena.luaRegionRefusals(), 0u);
  EXPECT_EQ(arena.luaCapRefusals(), 0u);
  EXPECT_EQ(arena.luaRegionInUse(), 0u);  // lua_close returned every block
  EXPECT_EQ(arena.reserveInUse(), reserved);
  EXPECT_EQ(std::vector<uint8_t>(session, session + SESSION), std::vector<uint8_t>(SESSION, 0x5A));
  EXPECT_EQ(std::vector<uint8_t>(scratch, scratch + SCRATCH), std::vector<uint8_t>(SCRATCH, 0xC3));
  arena.release(scratch);
  arena.release(session);
  EXPECT_EQ(arena.reserveInUse(), 0u);
}

}  // namespace
