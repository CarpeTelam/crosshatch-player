#if FREEINK_CAP_GAMES

#include "MatchStore.h"

#include <Codec.h>
#include <Memory.h>

#include <span>

bool MatchStore::allocate(const char* gameId, const uint32_t startMs) {
  // The slot, then saves' buffer; GameAssets restores store.bin into the slot.
  constexpr size_t slotBytes = GameScript::Codec::STORE_LIMIT;
  storage = HalMemory::allocatePsram(slotBytes + GameSaveStore::BUFFER_BYTES);
  if (!storage) return false;
  slotPtr = makeUniqueNoThrow<GameScript::StoreSlot>(storage.get(), slotBytes);
  saver = makeUniqueNoThrow<GameSaveStore>(
      gameId, std::span<uint8_t>(storage.get() + slotBytes, GameSaveStore::BUFFER_BYTES), startMs);
  return ready();
}

void MatchStore::leak() {
  static_cast<void>(slotPtr.release());
  static_cast<void>(storage.release());
}

#endif  // FREEINK_CAP_GAMES
