#pragma once

// Steers and inspects the PSRAM stub (HalMemoryStub.cpp), which stands in for
// HalMemory::allocatePsram on the host. Every block is malloc'd between two guard
// bands and poisoned, so a write past either end, or a read of memory a load never
// wrote, shows up: the guards are checked when the block is freed.

#include <cstddef>

namespace fakepsram {

inline size_t liveBlocks = 0;        // allocated and not yet freed
inline size_t totalAllocations = 0;  // successful allocatePsram calls
inline size_t lastRequestedBytes = 0;
inline size_t overruns = 0;    // blocks freed with a damaged guard band
inline bool failNext = false;  // the next allocatePsram returns null once
inline size_t failAbove = 0;   // when nonzero, a request over this many bytes returns null

// The byte value every fresh block holds until something writes it.
inline constexpr unsigned char POISON = 0xCD;

inline void reset() {
  liveBlocks = totalAllocations = lastRequestedBytes = overruns = 0;
  failNext = false;
  failAbove = 0;
}

}  // namespace fakepsram
