#include "ArenaAllocator.h"

#include <cstddef>
#include <cstring>

namespace GameScript {

// Every block starts with this header. Sizes count the header and are multiples of
// ALIGN, so bit 0 is free to mark a used block. A free block also holds FreeLinks
// at the start of its payload.
struct ArenaAllocator::Block {
  size_t sizeAndUsed;
  size_t prevSize;  // size of the block just below this one; 0 for the first block
};

namespace {

struct FreeLinks {
  ArenaAllocator::Block* next;
  ArenaAllocator::Block* prev;
};

constexpr size_t ALIGN = ArenaAllocator::ALIGN;
constexpr size_t HEADER = ArenaAllocator::HEADER;
constexpr size_t MIN_BLOCK = ArenaAllocator::MIN_BLOCK;
constexpr size_t roundUp(const size_t n) { return (n + ALIGN - 1) & ~(ALIGN - 1); }
static_assert(MIN_BLOCK >= HEADER + sizeof(FreeLinks), "a free block must hold its links");
constexpr size_t USED = 1;
static_assert(sizeof(ArenaAllocator::Block) <= HEADER, "the header must fit before the aligned payload");

size_t sizeOf(const ArenaAllocator::Block* block) { return block->sizeAndUsed & ~USED; }
bool isUsed(const ArenaAllocator::Block* block) { return (block->sizeAndUsed & USED) != 0; }
FreeLinks* linksOf(ArenaAllocator::Block* block) {
  return reinterpret_cast<FreeLinks*>(reinterpret_cast<uint8_t*>(block) + HEADER);
}
void* payloadOf(ArenaAllocator::Block* block) { return reinterpret_cast<uint8_t*>(block) + HEADER; }
ArenaAllocator::Block* blockOf(void* payload) {
  return reinterpret_cast<ArenaAllocator::Block*>(static_cast<uint8_t*>(payload) - HEADER);
}

}  // namespace

void ArenaAllocator::reset(void* base, const size_t size) {
  const auto raw = reinterpret_cast<uintptr_t>(base);
  const uintptr_t aligned = (raw + ALIGN - 1) & ~static_cast<uintptr_t>(ALIGN - 1);
  const size_t lost = aligned - raw;
  const size_t usable = size > lost ? (size - lost) & ~(ALIGN - 1) : 0;
  begin = reinterpret_cast<uint8_t*>(aligned);
  end = begin + (usable >= MIN_BLOCK ? usable : 0);
  freeHead = nullptr;
  inUse = 0;
  peak = 0;
  luaHeld = 0;
  if (end == begin) return;
  Block* whole = firstBlock();
  whole->prevSize = 0;
  whole->sizeAndUsed = usable;
  pushFree(whole);
}

ArenaAllocator::Block* ArenaAllocator::firstBlock() const {
  return begin < end ? reinterpret_cast<Block*>(begin) : nullptr;
}

ArenaAllocator::Block* ArenaAllocator::nextBlock(const Block* block) const {
  const uint8_t* next = reinterpret_cast<const uint8_t*>(block) + sizeOf(block);
  return next < end ? reinterpret_cast<Block*>(const_cast<uint8_t*>(next)) : nullptr;
}

ArenaAllocator::Block* ArenaAllocator::prevBlock(const Block* block) const {
  if (block->prevSize == 0) return nullptr;
  return reinterpret_cast<Block*>(const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(block) - block->prevSize));
}

void ArenaAllocator::setSize(Block* block, const size_t size, const bool used) {
  block->sizeAndUsed = size | (used ? USED : 0);
  if (Block* next = nextBlock(block)) next->prevSize = size;
}

void ArenaAllocator::pushFree(Block* block) {
  FreeLinks* links = linksOf(block);
  links->next = freeHead;
  links->prev = nullptr;
  if (freeHead) linksOf(freeHead)->prev = block;
  freeHead = block;
}

void ArenaAllocator::unlinkFree(Block* block) {
  FreeLinks* links = linksOf(block);
  if (links->prev) {
    linksOf(links->prev)->next = links->next;
  } else {
    freeHead = links->next;
  }
  if (links->next) linksOf(links->next)->prev = links->prev;
}

void ArenaAllocator::freeBlock(Block* block) {
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

void ArenaAllocator::splitUsed(Block* block, const size_t size) {
  const size_t total = sizeOf(block);
  if (total - size < MIN_BLOCK) return;
  setSize(block, size, true);
  auto* tail = reinterpret_cast<Block*>(reinterpret_cast<uint8_t*>(block) + size);
  tail->prevSize = size;
  setSize(tail, total - size, true);
  freeBlock(tail);
}

void* ArenaAllocator::allocate(const size_t size) {
  if (size > capacity()) return nullptr;
  const size_t need = size + HEADER <= MIN_BLOCK ? MIN_BLOCK : roundUp(size + HEADER);
  for (Block* block = freeHead; block; block = linksOf(block)->next) {
    if (sizeOf(block) < need) continue;
    unlinkFree(block);
    setSize(block, sizeOf(block), true);
    splitUsed(block, need);
    inUse += sizeOf(block);
    if (inUse > peak) peak = inUse;
    return payloadOf(block);
  }
  return nullptr;
}

void ArenaAllocator::release(void* p) {
  if (!p) return;
  Block* block = blockOf(p);
  inUse -= sizeOf(block);
  freeBlock(block);
}

void* ArenaAllocator::reallocate(void* p, const size_t size) {
  if (!p) return allocate(size);
  if (size > capacity()) return nullptr;
  Block* block = blockOf(p);
  const size_t current = sizeOf(block);
  const size_t need = size + HEADER <= MIN_BLOCK ? MIN_BLOCK : roundUp(size + HEADER);
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
  std::memcpy(moved, p, current - HEADER);
  release(p);
  return moved;
}

void* ArenaAllocator::luaAlloc(void* ud, void* ptr, const size_t osize, const size_t nsize) {
  auto* arena = static_cast<ArenaAllocator*>(ud);
  // With a null ptr, osize is a type tag, not a size.
  const size_t old = ptr ? osize : 0;
  if (nsize == 0) {
    arena->release(ptr);
    arena->luaHeld -= old;
    return nullptr;
  }
  if (nsize > old && (arena->luaHeld >= arena->luaCap || nsize - old > arena->luaCap - arena->luaHeld)) return nullptr;
  void* p = ptr ? arena->reallocate(ptr, nsize) : arena->allocate(nsize);
  if (p) arena->luaHeld = arena->luaHeld - old + nsize;
  return p;
}

}  // namespace GameScript
