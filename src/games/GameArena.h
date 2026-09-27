#pragma once

#include <ArenaAllocator.h>
#include <HalMemory.h>

// The arena backend (AD-6): one ARENA_BYTES block of PSRAM under the in-tree
// allocator. Freeing the block frees the whole VM heap in one call.
class GameArena {
 public:
  // False (logged) when PSRAM is missing or full.
  bool allocate();
  // Frees the block; the allocator is empty afterwards.
  void release();
  GameScript::ArenaAllocator& allocator() { return arena; }

 private:
  HalMemory::PsramBuffer block;
  GameScript::ArenaAllocator arena;
};
