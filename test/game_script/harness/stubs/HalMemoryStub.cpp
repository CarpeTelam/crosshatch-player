#include "HalMemoryStub.h"

#include <HalMemory.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {

constexpr size_t GUARD_BYTES = 4096;  // room for a realistic overrun to land in the guard, not the heap
constexpr unsigned char GUARD = 0xA5;
// The block is [size_t bytes, padded to GUARD_BYTES][guard][user bytes][guard]; the
// caller holds a pointer to its bytes. (Nothing here needs alignment past 16.)
constexpr size_t HEADER_BYTES = 16;
static_assert(sizeof(size_t) <= HEADER_BYTES, "the header holds the size");

uint8_t* rawOf(uint8_t* user) { return user - GUARD_BYTES - HEADER_BYTES; }

}  // namespace

void HalMemory::PsramDeleter::operator()(uint8_t* buffer) const {
  if (!buffer) return;
  uint8_t* const raw = rawOf(buffer);
  size_t bytes = 0;
  std::memcpy(&bytes, raw, sizeof(bytes));
  bool damaged = false;
  for (size_t i = 0; i < GUARD_BYTES; ++i) {
    damaged = damaged || raw[HEADER_BYTES + i] != GUARD || buffer[bytes + i] != GUARD;
  }
  if (damaged) ++fakepsram::overruns;
  --fakepsram::liveBlocks;
  std::free(raw);
}

HalMemory::PsramBuffer HalMemory::allocatePsram(const size_t bytes) {
  if (fakepsram::failNext) {
    fakepsram::failNext = false;
    return PsramBuffer();
  }
  if (fakepsram::failAbove != 0 && bytes > fakepsram::failAbove) return PsramBuffer();
  if (bytes == 0) return PsramBuffer();  // heap_caps_malloc(0) is NULL on the device
  auto* const raw = static_cast<uint8_t*>(std::malloc(HEADER_BYTES + GUARD_BYTES + bytes + GUARD_BYTES));
  if (!raw) return PsramBuffer();
  std::memset(raw, 0, HEADER_BYTES);
  std::memcpy(raw, &bytes, sizeof(bytes));
  std::memset(raw + HEADER_BYTES, GUARD, GUARD_BYTES);
  std::memset(raw + HEADER_BYTES + GUARD_BYTES, fakepsram::POISON, bytes);
  std::memset(raw + HEADER_BYTES + GUARD_BYTES + bytes, GUARD, GUARD_BYTES);
  ++fakepsram::liveBlocks;
  ++fakepsram::totalAllocations;
  fakepsram::lastRequestedBytes = bytes;
  return PsramBuffer(raw + HEADER_BYTES + GUARD_BYTES);
}
