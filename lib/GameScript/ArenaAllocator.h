#pragma once

#include <cstddef>
#include <cstdint>

namespace GameScript {

// A game VM's heap is one block of this size (AD-6: 256 KiB in PSRAM on the device).
inline constexpr size_t ARENA_BYTES = 256 * 1024;
// The most a game's Lua heap may hold, counted in the bytes Lua asks for (AD-6;
// `lua_heap_bytes` in api-level-1.txt). luaAlloc refuses growth past it.
inline constexpr size_t LUA_HEAP_BYTES = 256 * 1024;

// Small first-fit allocator with boundary tags and coalescing over one caller-owned
// block. The block is the port: src/games hands in PSRAM, host tests a malloc'd
// buffer, and freeing the block frees everything at once. Not thread-safe; only the
// VM task uses it.
class ArenaAllocator {
 public:
  ArenaAllocator() = default;
  ArenaAllocator(const ArenaAllocator&) = delete;
  ArenaAllocator& operator=(const ArenaAllocator&) = delete;

  // Starts over on [base, base + size), forgetting any earlier block. A block too
  // small for one allocation leaves the arena empty (every allocate fails).
  void reset(void* base, size_t size);

  // Null when no free block fits.
  void* allocate(size_t size);
  // Frees p (null is a no-op).
  void release(void* p);
  // Grows or shrinks in place when it can, otherwise moves. Null (p untouched)
  // when growing fails; shrinking never fails.
  void* reallocate(void* p, size_t size);

  // Bytes held by live allocations, block headers included, and the most ever held.
  size_t bytesInUse() const { return inUse; }
  size_t peakBytes() const { return peak; }
  size_t capacity() const { return static_cast<size_t>(end - begin); }

  // Bytes Lua holds through luaAlloc (its own sizes, no headers), and the cap on
  // them. reset() clears the count and keeps the cap.
  size_t luaBytes() const { return luaHeld; }
  size_t luaLimit() const { return luaCap; }
  void setLuaLimit(size_t bytes) { luaCap = bytes; }

  // lua_Alloc over an ArenaAllocator passed as `ud`: nsize 0 frees, a null ptr
  // allocates, anything else reallocates. Growth that would take luaBytes() past
  // luaLimit() fails (Lua then collects and retries, then raises a memory error);
  // shrinking never fails.
  static void* luaAlloc(void* ud, void* ptr, size_t osize, size_t nsize);

  // Block header, defined in the .cpp; public only so its helpers can name it.
  struct Block;

 private:
  Block* firstBlock() const;
  Block* nextBlock(const Block* block) const;
  Block* prevBlock(const Block* block) const;
  void setSize(Block* block, size_t size, bool used);
  void pushFree(Block* block);
  void unlinkFree(Block* block);
  // Splits a used block to `size`, returning the tail to the free list.
  void splitUsed(Block* block, size_t size);
  // Frees a block and merges it with free neighbours.
  void freeBlock(Block* block);

  uint8_t* begin = nullptr;
  uint8_t* end = nullptr;
  Block* freeHead = nullptr;
  size_t inUse = 0;
  size_t peak = 0;
  size_t luaHeld = 0;
  size_t luaCap = LUA_HEAP_BYTES;
};

}  // namespace GameScript
