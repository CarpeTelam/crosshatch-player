#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>

namespace GameScript {

// ch.store's latest-wins slot (AD-17): the codec bytes of the game's one store
// table, over caller storage (PSRAM the match owns, so it outlives the VM). The VM
// task posts each ch.store.set and copies it back for ch.store.get; the loop task
// seeds it before the VM starts and takes it when dirty to write store.bin (entry
// 12). Thread-safe; every call holds the mutex only to compare or copy.
class StoreSlot {
 public:
  // `capacity` bytes at `storage`; at least Codec::STORE_LIMIT for real use.
  StoreSlot(uint8_t* storage, size_t capacity) : bytes(storage), capacity(capacity) {}
  StoreSlot(const StoreSlot&) = delete;
  StoreSlot& operator=(const StoreSlot&) = delete;

  // Replaces the contents with a saved store (loaded from store.bin), not dirty.
  // False, leaving the slot empty, when it does not fit.
  bool restore(std::span<const uint8_t> saved);
  // Replaces the contents; dirty only when they differ from what is held (an empty
  // slot differs from every value). False, changing nothing, when `encoded` is
  // empty or does not fit.
  bool post(std::span<const uint8_t> encoded);
  // Copies the contents into `out` and returns their length: 0 for an empty slot,
  // or when `out` is too small.
  size_t read(std::span<uint8_t> out) const;
  // When dirty: copies the contents (never empty once posted) into `out`, clears
  // dirty, and returns their length. Otherwise, or when `out` is too small, returns
  // 0 and stays as it is.
  size_t takeIfDirty(std::span<uint8_t> out);
  // Marks the contents dirty again after a taken copy failed to reach the SD card,
  // so the next flush retries; the latest contents win. No-op when empty.
  void markDirty();
  bool dirty() const;

 private:
  mutable std::mutex mutex;
  uint8_t* bytes;
  size_t capacity;
  size_t length = 0;
  bool changed = false;
};

}  // namespace GameScript
