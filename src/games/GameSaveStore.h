#pragma once

#include <Codec.h>
#include <ISnapshotStore.h>
#include <Manifest.h>

#include <cstddef>
#include <cstdint>
#include <span>

#include "GamePaths.h"

namespace GameScript {
class StoreSlot;
}

// The only reader and writer of /.games-data/<id>/store.bin (AD-17): a blob header
// (magic "CHST", file version 1, the codec version) and then ch.store's codec bytes,
// laid out in docs/crosshatch/formats.md. Loop task only; the VM never touches
// Storage (AD-5). A write goes to store.bin.tmp, then replaces store.bin by a
// rename. A save that is not a valid store table is discarded with a log line and
// never decoded; the file stays until the game's next write replaces it.
class GameSaveStore final : public GameCore::ISnapshotStore {
 public:
  static constexpr char STORE_MAGIC[] = "CHST";
  static constexpr uint8_t STORE_FILE_VERSION = 1;
  // A dirty store is written at most this often by flushIfDue (AD-17).
  static constexpr uint32_t FLUSH_INTERVAL_MS = 5000;
  // The smallest buffer the constructor takes: one whole store.
  static constexpr size_t BUFFER_BYTES = GameScript::Codec::STORE_LIMIT;

  // `gameId` is a valid manifest id. `buffer` (at least BUFFER_BYTES; PSRAM the
  // match owns) carries the store between the slot and the SD card. `startMs`
  // (millis()) starts the flush interval.
  GameSaveStore(const char* gameId, std::span<uint8_t> buffer, uint32_t startMs);

  size_t loadStore(std::span<uint8_t> out) override;
  bool saveStore(std::span<const uint8_t> encoded) override;

  // Before the VM starts: seeds `slot` with the saved store, or leaves it empty.
  void restoreInto(GameScript::StoreSlot& slot);
  // Each loop pass: flush() once FLUSH_INTERVAL_MS has passed since the last write
  // (or the start). True unless a write was due and failed.
  bool flushIfDue(GameScript::StoreSlot& slot, uint32_t nowMs);
  // At round end and in onExit(): writes a dirty store now. True when there was
  // nothing to write or it was written; on failure the slot stays dirty, so a later
  // flush retries.
  bool flush(GameScript::StoreSlot& slot, uint32_t nowMs);

 private:
  // Reads `path` into `out`; null with `length` set when it holds a valid store,
  // otherwise why it does not.
  const char* readValid(const char* path, std::span<uint8_t> out, size_t& length) const;

  char id[GameCore::Manifest::MAX_ID_BYTES + 1] = {};
  char dirPath[GamePaths::DATA_PATH_BYTES] = {};
  char storePath[GamePaths::DATA_PATH_BYTES] = {};
  char tmpPath[GamePaths::DATA_PATH_BYTES] = {};
  std::span<uint8_t> buffer;
  uint32_t lastWriteMs;
};
