#if FREEINK_CAP_GAMES

#include "GameLinkStore.h"

#include <Memory.h>

#include <algorithm>
#include <utility>

using GameCore::NO_STATE_HANDLE;
using GameCore::StateHandle;

std::unique_ptr<GameLinkStore> GameLinkStore::create() {
  HalMemory::PsramBuffer block = HalMemory::allocatePsram(BLOCK_BYTES);
  if (!block) return nullptr;
  return makeUniqueNoThrow<GameLinkStore>(std::move(block));
}

GameLinkStore::GameLinkStore(HalMemory::PsramBuffer memory) : block(std::move(memory)) {}

StateHandle GameLinkStore::acquire() {
  if (!block) return NO_STATE_HANDLE;
  for (size_t i = 0; i < used.size(); ++i) {
    if (used[i]) continue;
    used[i] = true;
    return static_cast<StateHandle>(i);
  }
  return NO_STATE_HANDLE;
}

void GameLinkStore::release(const StateHandle handle) {
  if (handle == NO_STATE_HANDLE) return;
  if (handle >= used.size() || !used[handle]) {
    ++badReleaseCount;  // a double release would put one buffer on the free list twice
    return;
  }
  used[handle] = false;
}

std::span<uint8_t> GameLinkStore::bytes(const StateHandle handle) {
  if (!block || handle >= used.size() || !used[handle]) return {};
  return {block.get() + static_cast<size_t>(handle) * BUFFER_BYTES, BUFFER_BYTES};
}

std::span<uint8_t> GameLinkStore::outbound() {
  if (!block) return {};
  return {block.get() + POOL_BUFFERS * BUFFER_BYTES, BUFFER_BYTES};
}

size_t GameLinkStore::inUse() const { return static_cast<size_t>(std::count(used.begin(), used.end(), true)); }

#endif  // FREEINK_CAP_GAMES
