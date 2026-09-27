#include "StoreSlot.h"

#include <cstring>

namespace GameScript {

bool StoreSlot::restore(const std::span<const uint8_t> saved) {
  std::lock_guard<std::mutex> lock(mutex);
  changed = false;
  if (saved.size() > capacity) {
    length = 0;
    return false;
  }
  if (!saved.empty()) std::memcpy(bytes, saved.data(), saved.size());
  length = saved.size();
  return true;
}

bool StoreSlot::post(const std::span<const uint8_t> encoded) {
  std::lock_guard<std::mutex> lock(mutex);
  if (encoded.empty() || encoded.size() > capacity) return false;
  if (encoded.size() == length && std::memcmp(bytes, encoded.data(), length) == 0) return true;
  std::memcpy(bytes, encoded.data(), encoded.size());
  length = encoded.size();
  changed = true;
  return true;
}

size_t StoreSlot::read(const std::span<uint8_t> out) const {
  std::lock_guard<std::mutex> lock(mutex);
  if (length == 0 || out.size() < length) return 0;
  std::memcpy(out.data(), bytes, length);
  return length;
}

size_t StoreSlot::takeIfDirty(const std::span<uint8_t> out) {
  std::lock_guard<std::mutex> lock(mutex);
  if (!changed || out.size() < length) return 0;
  std::memcpy(out.data(), bytes, length);
  changed = false;
  return length;
}

void StoreSlot::markDirty() {
  std::lock_guard<std::mutex> lock(mutex);
  if (length > 0) changed = true;
}

bool StoreSlot::dirty() const {
  std::lock_guard<std::mutex> lock(mutex);
  return changed;
}

}  // namespace GameScript
