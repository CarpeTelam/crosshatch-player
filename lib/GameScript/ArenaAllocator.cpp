#include "ArenaAllocator.h"

#include <cstddef>
#include <cstring>

namespace GameScript {

// Every block starts with this header. Sizes count the header and are multiples of
// the alignment, so bit 0 is free to mark a used block. A free block also holds
// FreeLinks at the start of its payload. 32-bit sizes keep the header 8 B on every
// platform; the region's alignment pads it.
struct ArenaAllocator::Block {
  uint32_t sizeAndUsed;
  uint32_t prevSize;  // size of the block just below this one; 0 for the first block
};

namespace {

using Block = ArenaAllocator::Block;

struct FreeLinks {
  Block* next;
  Block* prev;
};

constexpr size_t MIN_ALIGN = 8;
constexpr uint32_t USED = 1;
constexpr size_t MAX_REGION = UINT32_MAX;
static_assert(sizeof(Block) == 2 * sizeof(uint32_t), "arenaHeaderBytes() assumes an 8 B header");
static_assert(arenaMinBlockBytes(MIN_ALIGN) >= arenaHeaderBytes(MIN_ALIGN) + sizeof(FreeLinks),
              "a free block must hold its links");
static_assert(alignof(FreeLinks) <= MIN_ALIGN && alignof(Block) <= MIN_ALIGN, "blocks must align their fields");

constexpr size_t roundUp(const size_t n, const size_t align) { return (n + align - 1) & ~(align - 1); }
size_t sizeOf(const Block* block) { return block->sizeAndUsed & ~USED; }
bool isUsed(const Block* block) { return (block->sizeAndUsed & USED) != 0; }
FreeLinks* linksOf(Block* block, const size_t header) {
  return reinterpret_cast<FreeLinks*>(reinterpret_cast<uint8_t*>(block) + header);
}

}  // namespace

ArenaAllocator::ArenaAllocator(const size_t align)
    : align(align >= MIN_ALIGN && (align & (align - 1)) == 0 ? align : ALIGN) {}

void ArenaAllocator::clearCounts() {
  luaHeld = 0;
  capRefusals = 0;
  regionRefusals = 0;
}

void ArenaAllocator::reset(void* base, const size_t size) {
  reserve.reset(base, size, align);
  lua.reset(nullptr, 0, align);
  isSplit = false;
  clearCounts();
}

void ArenaAllocator::split(void* base, const size_t luaRegion, const size_t reserveBytes) {
  lua.reset(base, luaRegion, align);
  reserve.reset(base ? static_cast<uint8_t*>(base) + luaRegion : nullptr, reserveBytes, align);
  isSplit = true;
  clearCounts();
}

void* ArenaAllocator::allocate(const size_t size) { return reserve.allocate(size); }

void ArenaAllocator::release(void* p) { reserve.release(p); }

void* ArenaAllocator::reallocate(void* p, const size_t size) { return reserve.reallocate(p, size); }

void* ArenaAllocator::luaAlloc(void* ud, void* ptr, const size_t osize, const size_t nsize) {
  auto* arena = static_cast<ArenaAllocator*>(ud);
  Region& region = arena->luaSide();
  // With a null ptr, osize is a type tag, not a size.
  const size_t old = ptr ? osize : 0;
  if (nsize == 0) {
    region.release(ptr);
    arena->luaHeld -= old;
    return nullptr;
  }
  if (nsize > old && (arena->luaHeld >= arena->luaCap || nsize - old > arena->luaCap - arena->luaHeld)) {
    ++arena->capRefusals;
    return nullptr;
  }
  void* p = ptr ? region.reallocate(ptr, nsize) : region.allocate(nsize);
  if (p) {
    arena->luaHeld = arena->luaHeld - old + nsize;
  } else {
    ++arena->regionRefusals;
  }
  return p;
}

void ArenaAllocator::Region::reset(void* base, const size_t size, const size_t alignment) {
  align = alignment;
  header = arenaHeaderBytes(align);
  minBlock = arenaMinBlockBytes(align);
  const auto raw = reinterpret_cast<uintptr_t>(base);
  const uintptr_t aligned = (raw + align - 1) & ~static_cast<uintptr_t>(align - 1);
  const size_t lost = aligned - raw;
  size_t usable = base && size > lost ? (size - lost) & ~(align - 1) : 0;
  if (usable > MAX_REGION) usable = MAX_REGION & ~(align - 1);
  begin = reinterpret_cast<uint8_t*>(aligned);
  end = begin + (usable >= minBlock ? usable : 0);
  freeHead = nullptr;
  inUse = 0;
  peak = 0;
  if (end == begin) return;
  Block* whole = firstBlock();
  whole->prevSize = 0;
  whole->sizeAndUsed = static_cast<uint32_t>(usable);
  pushFree(whole);
}

size_t ArenaAllocator::Region::blockFor(const size_t size) const {
  return size + header <= minBlock ? minBlock : roundUp(size + header, align);
}

Block* ArenaAllocator::Region::firstBlock() const { return begin < end ? reinterpret_cast<Block*>(begin) : nullptr; }

Block* ArenaAllocator::Region::nextBlock(const Block* block) const {
  const uint8_t* next = reinterpret_cast<const uint8_t*>(block) + sizeOf(block);
  return next < end ? reinterpret_cast<Block*>(const_cast<uint8_t*>(next)) : nullptr;
}

Block* ArenaAllocator::Region::prevBlock(const Block* block) const {
  if (block->prevSize == 0) return nullptr;
  return reinterpret_cast<Block*>(const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(block) - block->prevSize));
}

void ArenaAllocator::Region::setSize(Block* block, const size_t size, const bool used) {
  block->sizeAndUsed = static_cast<uint32_t>(size) | (used ? USED : 0);
  if (Block* next = nextBlock(block)) next->prevSize = static_cast<uint32_t>(size);
}

void ArenaAllocator::Region::pushFree(Block* block) {
  FreeLinks* links = linksOf(block, header);
  links->next = freeHead;
  links->prev = nullptr;
  if (freeHead) linksOf(freeHead, header)->prev = block;
  freeHead = block;
}

void ArenaAllocator::Region::unlinkFree(Block* block) {
  FreeLinks* links = linksOf(block, header);
  if (links->prev) {
    linksOf(links->prev, header)->next = links->next;
  } else {
    freeHead = links->next;
  }
  if (links->next) linksOf(links->next, header)->prev = links->prev;
}

void ArenaAllocator::Region::freeBlock(Block* block) {
  setSize(block, sizeOf(block), false);
  Block* next = nextBlock(block);
  if (next && !isUsed(next)) {
    unlinkFree(next);
    setSize(block, sizeOf(block) + sizeOf(next), false);
  }
  Block* prev = prevBlock(block);
  if (prev && !isUsed(prev)) {
    unlinkFree(prev);
    setSize(prev, sizeOf(prev) + sizeOf(block), false);
    block = prev;
  }
  pushFree(block);
}

void ArenaAllocator::Region::splitUsed(Block* block, const size_t size) {
  const size_t total = sizeOf(block);
  if (total - size < minBlock) return;
  setSize(block, size, true);
  auto* tail = reinterpret_cast<Block*>(reinterpret_cast<uint8_t*>(block) + size);
  tail->prevSize = static_cast<uint32_t>(size);
  setSize(tail, total - size, true);
  freeBlock(tail);
}

void* ArenaAllocator::Region::allocate(const size_t size) {
  if (size > capacity()) return nullptr;
  const size_t need = blockFor(size);
  for (Block* block = freeHead; block; block = linksOf(block, header)->next) {
    if (sizeOf(block) < need) continue;
    unlinkFree(block);
    setSize(block, sizeOf(block), true);
    splitUsed(block, need);
    inUse += sizeOf(block);
    if (inUse > peak) peak = inUse;
    return reinterpret_cast<uint8_t*>(block) + header;
  }
  return nullptr;
}

void ArenaAllocator::Region::release(void* p) {
  if (!p) return;
  auto* block = reinterpret_cast<Block*>(static_cast<uint8_t*>(p) - header);
  inUse -= sizeOf(block);
  freeBlock(block);
}

void* ArenaAllocator::Region::reallocate(void* p, const size_t size) {
  if (!p) return allocate(size);
  if (size > capacity()) return nullptr;
  auto* block = reinterpret_cast<Block*>(static_cast<uint8_t*>(p) - header);
  const size_t current = sizeOf(block);
  const size_t need = blockFor(size);
  if (need <= current) {
    inUse -= current;
    splitUsed(block, need);
    inUse += sizeOf(block);
    return p;
  }
  Block* next = nextBlock(block);
  if (next && !isUsed(next) && current + sizeOf(next) >= need) {
    unlinkFree(next);
    inUse -= current;
    setSize(block, current + sizeOf(next), true);
    splitUsed(block, need);
    inUse += sizeOf(block);
    if (inUse > peak) peak = inUse;
    return p;
  }
  void* moved = allocate(size);
  if (!moved) return nullptr;
  std::memcpy(moved, p, current - header);
  release(p);
  return moved;
}

}  // namespace GameScript
