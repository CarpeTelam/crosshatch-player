#if FREEINK_CAP_GAMES

#include "GameSaveStore.h"

#include <BlobHeader.h>
#include <HalStorage.h>
#include <Logging.h>
#include <StoreSlot.h>

#include <algorithm>
#include <cstdio>
#include <string>

namespace {

bool readExactly(HalFile& file, uint8_t* out, const size_t count) {
  return count == 0 || file.read(out, count) == static_cast<int>(count);
}

bool writeExactly(HalFile& file, const uint8_t* data, const size_t count) { return file.write(data, count) == count; }

// The longest path, store.bin.tmp's with the longest id, fits the path buffers:
// the folder, '/', the id, then "/store.bin.tmp" with its terminator.
static_assert(std::char_traits<char>::length(GamePaths::GAMES_DATA_DIR) + 1 + GameCore::Manifest::MAX_ID_BYTES +
                      sizeof("/store.bin.tmp") <=
                  GamePaths::DATA_PATH_BYTES,
              "GamePaths::DATA_PATH_BYTES holds GAMES_DATA_DIR/<id>/store.bin.tmp");

}  // namespace

GameSaveStore::GameSaveStore(const char* gameId, const std::span<uint8_t> buffer, const uint32_t startMs)
    : buffer(buffer), lastWriteMs(startMs) {
  snprintf(id, sizeof(id), "%s", gameId);
  snprintf(dirPath, sizeof(dirPath), "%s/%s", GamePaths::GAMES_DATA_DIR, id);
  snprintf(storePath, sizeof(storePath), "%s/%s/store.bin", GamePaths::GAMES_DATA_DIR, id);
  snprintf(tmpPath, sizeof(tmpPath), "%s/%s/store.bin.tmp", GamePaths::GAMES_DATA_DIR, id);
}

size_t GameSaveStore::loadStore(const std::span<uint8_t> out) {
  const char* path = storePath;
  if (!Storage.exists(storePath)) {
    if (!Storage.exists(tmpPath)) return 0;
    // A write stopped between removing store.bin and the rename. A tmp torn by an
    // earlier stop fails the checks below.
    LOG_INF("GAME", "%s: no store.bin; reading %s", id, tmpPath);
    path = tmpPath;
  }
  size_t length = 0;
  if (const char* problem = readValid(path, out, length)) {
    LOG_ERR("GAME", "%s: discarded %s: %s", id, path, problem);
    return 0;
  }
  return length;
}

const char* GameSaveStore::readValid(const char* path, const std::span<uint8_t> out, size_t& length) const {
  using namespace GameScript;
  length = 0;
  HalFile file;
  if (!Storage.openFileForRead("GAME", path, file)) return "cannot open";
  const size_t size = file.fileSize();
  const bool tooLarge = size > BLOB_HEADER_BYTES + std::min(out.size(), Codec::STORE_LIMIT);
  uint8_t header[BLOB_HEADER_BYTES] = {};
  const size_t headerBytes = std::min(size, BLOB_HEADER_BYTES);
  const size_t payloadBytes = size - headerBytes;
  const bool read = !tooLarge && readExactly(file, header, headerBytes) && readExactly(file, out.data(), payloadBytes);
  file.close();
  if (tooLarge) return "too large";
  if (!read) return "cannot read";

  const BlobHeaderStatus status = checkBlobHeader(header, headerBytes, STORE_MAGIC, STORE_FILE_VERSION);
  if (status != BlobHeaderStatus::Ok) return blobHeaderStatusName(status);
  bool isTable = false;
  const Codec::Error error = Codec::check(out.data(), payloadBytes, Codec::STORE_LIMIT, isTable);
  if (error != Codec::Error::None) return Codec::errorName(error);
  if (!isTable) return "not a table";
  length = payloadBytes;
  return nullptr;
}

bool GameSaveStore::saveStore(const std::span<const uint8_t> encoded) {
  using namespace GameScript;
  if (encoded.empty() || encoded.size() > Codec::STORE_LIMIT) {
    LOG_ERR("GAME", "%s: refused a %u-byte store", id, static_cast<unsigned>(encoded.size()));
    return false;
  }
  if (!Storage.ensureDirectoryExists(dirPath)) {
    LOG_ERR("GAME", "%s: cannot create %s", id, dirPath);
    return false;
  }
  // A tmp without store.bin is the only copy (an earlier write stopped or failed
  // before its rename), and opening the tmp for writing would truncate it, so it
  // becomes store.bin first. loadStore reads the same bytes either way.
  if (!Storage.exists(storePath) && Storage.exists(tmpPath) && !Storage.rename(tmpPath, storePath)) {
    LOG_ERR("GAME", "%s: cannot rename %s to %s", id, tmpPath, storePath);
    return false;
  }
  uint8_t header[BLOB_HEADER_BYTES];
  writeBlobHeader(header, STORE_MAGIC, STORE_FILE_VERSION);
  HalFile file;
  bool written = Storage.openFileForWrite("GAME", tmpPath, file);
  if (written) {
    written = writeExactly(file, header, sizeof(header)) && writeExactly(file, encoded.data(), encoded.size());
    written = file.close() && written;
  }
  if (!written) {
    LOG_ERR("GAME", "%s: cannot write %s", id, tmpPath);
    Storage.remove(tmpPath);
    return false;
  }
  // The tmp is whole from here, and loadStore reads it while store.bin is missing,
  // so a failure below loses nothing. SdFat's rename refuses an existing target.
  if (Storage.exists(storePath) && !Storage.remove(storePath)) {
    LOG_ERR("GAME", "%s: cannot replace %s", id, storePath);
    return false;
  }
  if (!Storage.rename(tmpPath, storePath)) {
    LOG_ERR("GAME", "%s: cannot rename %s to %s", id, tmpPath, storePath);
    return false;
  }
  return true;
}

void GameSaveStore::restoreInto(GameScript::StoreSlot& slot) {
  const size_t length = loadStore(buffer);
  if (!slot.restore({buffer.data(), length})) {
    LOG_ERR("GAME", "%s: the ch.store slot cannot hold %u bytes", id, static_cast<unsigned>(length));
    return;
  }
  if (length > 0) LOG_INF("GAME", "%s: restored ch.store (%u bytes)", id, static_cast<unsigned>(length));
}

bool GameSaveStore::flushIfDue(GameScript::StoreSlot& slot, const uint32_t nowMs) {
  if (nowMs - lastWriteMs < FLUSH_INTERVAL_MS || !slot.dirty()) return true;
  return flush(slot, nowMs);
}

bool GameSaveStore::flush(GameScript::StoreSlot& slot, const uint32_t nowMs) {
  const size_t length = slot.takeIfDirty(buffer);
  if (length == 0) return true;
  lastWriteMs = nowMs;
  if (saveStore({buffer.data(), length})) {
    LOG_INF("GAME", "%s: saved ch.store (%u bytes)", id, static_cast<unsigned>(length));
    return true;
  }
  slot.markDirty();
  return false;
}

#endif  // FREEINK_CAP_GAMES
