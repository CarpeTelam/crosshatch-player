#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace GameCore {

// Port: one game's saved data on this device (AD-17). The device adapter,
// src/games/GameSaveStore, is the only reader and writer of the files behind it
// and runs on the loop task only. Today it holds ch.store; resume snapshots join
// it with the launcher (epic-install-and-launcher).
class ISnapshotStore {
 public:
  virtual ~ISnapshotStore() = default;
  // Copies the saved ch.store's codec bytes into `out` and returns their length:
  // 0 when nothing is saved, or when the save is unusable (it is then discarded,
  // never decoded).
  virtual size_t loadStore(std::span<uint8_t> out) = 0;
  // Replaces the saved ch.store with `encoded` (codec bytes of a table). False when
  // it could not be written; a later loadStore then reads the previous save or,
  // when the failure came after `encoded` was written in full, `encoded`.
  virtual bool saveStore(std::span<const uint8_t> encoded) = 0;
};

}  // namespace GameCore
