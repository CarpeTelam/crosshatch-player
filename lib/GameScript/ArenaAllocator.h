#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

namespace GameScript {

// The most a game's Lua heap may hold, counted in the bytes Lua asks for (AD-6;
// `lua_heap_bytes` in api-level-1.txt). luaAlloc refuses growth past it.
inline constexpr size_t LUA_HEAP_BYTES = 256 * 1024;
// Lua's own region of the arena: the cap plus room for block headers, rounding,
// and fragmentation, so the cap, not the region, ends a heap bomb on the 64-bit
// host (16 B headers and alignment) and the 32-bit device (8 B, 8 B) alike.
// Headers and rounding: a request of s bytes takes roundUp(s + HEADER, ALIGN)
// bytes, at least MIN_BLOCK, so small objects cost the most. The smallest Lua
// holds by the thousand are, on the host, a one-upvalue closure and its upvalue,
// 40 B each in 64 B blocks (1.6 times); a table 48 B in 64; a 7-character string
// 32 B in 48. On the device: 20 B in 32 (1.6), 32 B in 40, 28 B in 40, 24 B in 32.
// So 256 KiB * 1.6 = 409.6 KiB covers either platform's worst small-object heap.
// Fragmentation: the largest block Lua allocates under the cap, a 192 KiB hash
// part (8,192 nodes of 24 B), needs one free hole that big in a first-fit heap.
// Measured on the host over 17 heap bombs at both header sizes, the smallest
// region where none met the region before the cap was 440 KiB (a hash of string
// keys and a mixed heap failed at 432); 448 KiB = 1.75 times the cap.
inline constexpr size_t LUA_REGION_BYTES = 448 * 1024;
// The runtime's own region, which luaAlloc never touches: the Session and one
// codec scratch (AD-5). LuaGame.cpp checks they fit.
inline constexpr size_t SCRATCH_RESERVE_BYTES = 16 * 1024;
// A game VM's heap is one block of this size in PSRAM: Lua's region, then the
// reserve. Freeing the block frees both at once.
inline constexpr size_t ARENA_BYTES = LUA_REGION_BYTES + SCRATCH_RESERVE_BYTES;

// An ArenaAllocator block's header, two 32-bit sizes padded to the alignment, and
// its smallest block (a free block holds two links), for a given alignment.
constexpr size_t arenaHeaderBytes(const size_t align) { return (2 * sizeof(uint32_t) + align - 1) & ~(align - 1); }
constexpr size_t arenaMinBlockBytes(const size_t align) {
  return (arenaHeaderBytes(align) + 2 * sizeof(void*) + align - 1) & ~(align - 1);
}

// Small first-fit allocator with boundary tags and coalescing over one caller-owned
// block. The block is the port: src/games hands in PSRAM, host tests a malloc'd
// buffer, and freeing the block frees everything at once. After reset() the block
// is one region that allocate() and luaAlloc share; after split() it is two, Lua's
// (luaAlloc) and the reserve (allocate, create), so neither can take the other's
// bytes. Not thread-safe; only the VM task uses it.
class ArenaAllocator {
 public:
  // Every block is aligned to ALIGN, starts with a HEADER-byte header, and takes at
  // least MIN_BLOCK bytes: this platform's values.
  static constexpr size_t ALIGN = alignof(std::max_align_t);
  static constexpr size_t HEADER = arenaHeaderBytes(ALIGN);
  static constexpr size_t MIN_BLOCK = arenaMinBlockBytes(ALIGN);
  // The most arena bytes one allocation of `size` takes: its rounded block, plus a
  // tail too small to split off.
  static constexpr size_t blockBytes(const size_t size) {
    return ((size + HEADER + ALIGN - 1) & ~(ALIGN - 1)) + MIN_BLOCK;
  }

  // `align` is ALIGN except in tests, which pass 8 to model the device's 8 B
  // headers on a 64-bit host; anything under 8 or not a power of two means ALIGN.
  explicit ArenaAllocator(size_t align = ALIGN);
  ArenaAllocator(const ArenaAllocator&) = delete;
  ArenaAllocator& operator=(const ArenaAllocator&) = delete;

  // Starts over on [base, base + size) as one shared region, forgetting any earlier
  // block. A block too small for one allocation leaves the arena empty (every
  // allocate fails).
  void reset(void* base, size_t size);
  // Starts over with Lua's region on [base, base + luaRegion) and the reserve on the
  // reserve bytes right after it.
  void split(void* base, size_t luaRegion, size_t reserve);

  // From the reserve (the shared region after reset()). Null when no free block fits.
  void* allocate(size_t size);
  // Frees p (null is a no-op).
  void release(void* p);
  // Grows or shrinks in place when it can, otherwise moves. Null (p untouched)
  // when growing fails; shrinking never fails.
  void* reallocate(void* p, size_t size);

  // Constructs a T in the reserve; null when it does not fit. destroy() runs its
  // destructor and frees it (null is a no-op).
  template <typename T, typename... Args>
  T* create(Args&&... args) {
    void* p = allocate(sizeof(T));
    return p ? new (p) T(std::forward<Args>(args)...) : nullptr;
  }
  template <typename T>
  void destroy(T* object) {
    if (!object) return;
    object->~T();
    release(object);
  }

  // Bytes held by live allocations in both regions, block headers included, the
  // sum of each region's most ever held, and both regions' size.
  size_t bytesInUse() const { return reserve.inUse + (isSplit ? lua.inUse : 0); }
  size_t peakBytes() const { return reserve.peak + (isSplit ? lua.peak : 0); }
  size_t capacity() const { return reserve.capacity() + (isSplit ? lua.capacity() : 0); }
  // The same for Lua's region and for the reserve alone; one region after reset().
  size_t luaRegionInUse() const { return luaSide().inUse; }
  size_t luaRegionCapacity() const { return luaSide().capacity(); }
  size_t reserveInUse() const { return reserve.inUse; }
  size_t reserveCapacity() const { return reserve.capacity(); }

  // Bytes Lua holds through luaAlloc (its own sizes, no headers), and the cap on
  // them. reset() and split() clear the count and keep the cap.
  size_t luaBytes() const { return luaHeld; }
  size_t luaLimit() const { return luaCap; }
  void setLuaLimit(size_t bytes) { luaCap = bytes; }
  // Lua growth refused by the cap, and refused because Lua's region was full,
  // since the last reset() or split().
  size_t luaCapRefusals() const { return capRefusals; }
  size_t luaRegionRefusals() const { return regionRefusals; }

  // lua_Alloc over an ArenaAllocator passed as `ud`, in Lua's region: nsize 0 frees,
  // a null ptr allocates, anything else reallocates. Growth that would take
  // luaBytes() past luaLimit() fails (Lua then collects and retries, then raises a
  // memory error); shrinking never fails.
  static void* luaAlloc(void* ud, void* ptr, size_t osize, size_t nsize);

  // Block header, defined in the .cpp; public only so its helpers can name it.
  struct Block;

 private:
  // One first-fit heap over [begin, end).
  class Region {
   public:
    void reset(void* base, size_t size, size_t align);
    void* allocate(size_t size);
    void release(void* p);
    void* reallocate(void* p, size_t size);
    size_t capacity() const { return static_cast<size_t>(end - begin); }

    size_t inUse = 0;
    size_t peak = 0;

   private:
    size_t blockFor(size_t size) const;
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
    size_t align = ALIGN;
    size_t header = HEADER;
    size_t minBlock = MIN_BLOCK;
  };

  const Region& luaSide() const { return isSplit ? lua : reserve; }
  Region& luaSide() { return isSplit ? lua : reserve; }
  void clearCounts();

  size_t align;
  Region reserve;  // the whole block after reset()
  Region lua;      // empty unless split
  bool isSplit = false;
  size_t luaHeld = 0;
  size_t luaCap = LUA_HEAP_BYTES;
  size_t capRefusals = 0;
  size_t regionRefusals = 0;
};

}  // namespace GameScript
