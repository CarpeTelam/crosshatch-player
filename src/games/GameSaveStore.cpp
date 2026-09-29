#if FREEINK_CAP_GAMES

#include "GameSaveStore.h"

#include <BlobHeader.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Session.h>
#include <StoreSlot.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>
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

static_assert(std::char_traits<char>::length(GamePaths::GAMES_DATA_DIR) + 1 + GameCore::Manifest::MAX_ID_BYTES +
                      sizeof("/resume.bin.tmp") <=
                  GamePaths::DATA_PATH_BYTES,
              "GamePaths::DATA_PATH_BYTES holds GAMES_DATA_DIR/<id>/resume.bin.tmp");

// resume.bin's fixed part (docs/crosshatch/formats.md): the blob header, the package
// hash, the mode (0, solo), the seat count (1), and the ver as a u16, little-endian.
constexpr size_t RESUME_HASH_AT = GameScript::BLOB_HEADER_BYTES;
constexpr size_t RESUME_MODE_AT = RESUME_HASH_AT + GameSaveStore::PACKAGE_HASH_BYTES;
constexpr size_t RESUME_SEATS_AT = RESUME_MODE_AT + 1;
constexpr size_t RESUME_VER_AT = RESUME_SEATS_AT + 1;
constexpr size_t RESUME_PREFIX_BYTES = RESUME_VER_AT + 2;
constexpr uint8_t RESUME_MODE_SOLO = 0;
constexpr uint8_t RESUME_SEATS_SOLO = 1;
static_assert(RESUME_PREFIX_BYTES <= 32, "the resume prefix lives on the stack");
static_assert(GameScript::Codec::SNAPSHOT_LIMIT == GameCore::SNAPSHOT_BYTES,
              "the codec's snapshot limit is the Session's snapshot size (AD-10)");
static_assert(GameSaveStore::BUFFER_BYTES >= GameScript::Codec::SNAPSHOT_LIMIT,
              "the store's buffer holds a whole snapshot, which loadResume and flushResume copy through it");

// Reads the resume file at `path` for a match of the package `pkgHash`; null when it
// can resume one, otherwise why it cannot. The snapshot (1 to Codec::SNAPSHOT_LIMIT
// bytes after the fixed part) is read into `out` and checked as a codec value, and
// `length` is its length. `ver` is the file's once the fixed part was read.
const char* readResume(const char* path, const uint8_t (&pkgHash)[GameSaveStore::PACKAGE_HASH_BYTES], uint16_t& ver,
                       const std::span<uint8_t> out, size_t& length) {
  using namespace GameScript;
  length = 0;
  ver = 0;
  HalFile file;
  if (!Storage.openFileForRead("GAME", path, file)) return "cannot open";
  const size_t size = file.fileSize();
  const bool tooLarge = size > RESUME_PREFIX_BYTES + std::min(out.size(), Codec::SNAPSHOT_LIMIT);
  uint8_t prefix[RESUME_PREFIX_BYTES] = {};
  const size_t prefixBytes = std::min(size, RESUME_PREFIX_BYTES);
  const size_t snapshotBytes = size - prefixBytes;
  const bool read = !tooLarge && readExactly(file, prefix, prefixBytes) && readExactly(file, out.data(), snapshotBytes);
  file.close();
  if (tooLarge) return "too large";
  if (!read) return "cannot read";

  const BlobHeaderStatus status = checkBlobHeader(prefix, std::min(prefixBytes, BLOB_HEADER_BYTES),
                                                  GameSaveStore::RESUME_MAGIC, GameSaveStore::RESUME_FILE_VERSION);
  if (status != BlobHeaderStatus::Ok) return blobHeaderStatusName(status);
  if (prefixBytes < RESUME_PREFIX_BYTES) return "truncated";
  if (std::memcmp(prefix + RESUME_HASH_AT, pkgHash, GameSaveStore::PACKAGE_HASH_BYTES) != 0) return "other package";
  if (prefix[RESUME_MODE_AT] != RESUME_MODE_SOLO || prefix[RESUME_SEATS_AT] != RESUME_SEATS_SOLO) {
    return "not a solo save";
  }
  ver = static_cast<uint16_t>(prefix[RESUME_VER_AT] | (prefix[RESUME_VER_AT + 1] << 8));
  if (snapshotBytes == 0) return "empty snapshot";
  bool isTable = false;
  const Codec::Error error = Codec::check(out.data(), snapshotBytes, Codec::SNAPSHOT_LIMIT, isTable);
  if (error != Codec::Error::None) return Codec::errorName(error);
  length = snapshotBytes;
  return nullptr;
}

// Replaces `path` with `head` then `body` by way of `tmp`, as saveStore does for
// store.bin (see there for why each step is ordered as it is); false when it could not.
bool replaceFile(const char* id, const char* dir, const char* path, const char* tmp,
                 const std::span<const uint8_t> head, const std::span<const uint8_t> body) {
  if (!Storage.ensureDirectoryExists(dir)) {
    LOG_ERR("GAME", "%s: cannot create %s", id, dir);
    return false;
  }
  if (!Storage.exists(path) && Storage.exists(tmp) && !Storage.rename(tmp, path)) {
    LOG_ERR("GAME", "%s: cannot rename %s to %s", id, tmp, path);
    return false;
  }
  HalFile file;
  bool written = Storage.openFileForWrite("GAME", tmp, file);
  if (written) {
    written = writeExactly(file, head.data(), head.size()) && writeExactly(file, body.data(), body.size());
    written = file.close() && written;
  }
  if (!written) {
    LOG_ERR("GAME", "%s: cannot write %s", id, tmp);
    Storage.remove(tmp);
    return false;
  }
  if (Storage.exists(path) && !Storage.remove(path)) {
    LOG_ERR("GAME", "%s: cannot replace %s", id, path);
    return false;
  }
  if (!Storage.rename(tmp, path)) {
    LOG_ERR("GAME", "%s: cannot rename %s to %s", id, tmp, path);
    return false;
  }
  return true;
}

}  // namespace

GameSaveStore::GameSaveStore(const char* gameId, const std::span<uint8_t> buffer, const uint32_t startMs)
    : buffer(buffer), lastWriteMs(startMs) {
  snprintf(id, sizeof(id), "%s", gameId);
  snprintf(dirPath, sizeof(dirPath), "%s/%s", GamePaths::GAMES_DATA_DIR, id);
  snprintf(storePath, sizeof(storePath), "%s/%s/store.bin", GamePaths::GAMES_DATA_DIR, id);
  snprintf(tmpPath, sizeof(tmpPath), "%s/%s/store.bin.tmp", GamePaths::GAMES_DATA_DIR, id);
  snprintf(resumePath, sizeof(resumePath), "%s/%s/resume.bin", GamePaths::GAMES_DATA_DIR, id);
  snprintf(resumeTmpPath, sizeof(resumeTmpPath), "%s/%s/resume.bin.tmp", GamePaths::GAMES_DATA_DIR, id);
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

bool GameSaveStore::peek(const char* gameId, const uint8_t (&pkgHash)[PACKAGE_HASH_BYTES]) {
  char path[GamePaths::DATA_PATH_BYTES];
  snprintf(path, sizeof(path), "%s/%s/resume.bin", GamePaths::GAMES_DATA_DIR, gameId);
  if (!Storage.exists(path)) {
    snprintf(path, sizeof(path), "%s/%s/resume.bin.tmp", GamePaths::GAMES_DATA_DIR, gameId);
    if (!Storage.exists(path)) return false;
  }
  // A static call has no store buffer: one snapshot's worth, allocated for this call only
  // (the launcher asks once per row it draws, never while a match runs).
  std::unique_ptr<uint8_t[]> snapshot(new (std::nothrow) uint8_t[GameScript::Codec::SNAPSHOT_LIMIT]);
  if (!snapshot) {
    LOG_ERR("GAME", "%s: OOM: %u bytes to check %s", gameId, static_cast<unsigned>(GameScript::Codec::SNAPSHOT_LIMIT),
            path);
    return false;
  }
  uint16_t ver = 0;
  size_t length = 0;
  if (const char* problem =
          readResume(path, pkgHash, ver, {snapshot.get(), GameScript::Codec::SNAPSHOT_LIMIT}, length)) {
    LOG_ERR("GAME", "%s: discarded %s: %s", gameId, path, problem);
    return false;
  }
  return true;
}

void GameSaveStore::setPackageHash(const uint8_t (&hash)[PACKAGE_HASH_BYTES]) {
  std::memcpy(packageHash, hash, PACKAGE_HASH_BYTES);
  hasPackageHash = true;
}

std::span<const uint8_t> GameSaveStore::loadResume(uint16_t& ver) {
  ver = 0;
  if (!hasPackageHash) return {};
  const char* path = resumePath;
  if (!Storage.exists(resumePath)) {
    if (!Storage.exists(resumeTmpPath)) return {};
    // A write stopped between removing resume.bin and the rename (see loadStore).
    LOG_INF("GAME", "%s: no resume.bin; reading %s", id, resumeTmpPath);
    path = resumeTmpPath;
  }
  size_t length = 0;
  if (const char* problem = readResume(path, packageHash, ver, buffer, length)) {
    LOG_ERR("GAME", "%s: discarded %s: %s", id, path, problem);
    ver = 0;
    return {};
  }
  return {buffer.data(), length};
}

bool GameSaveStore::saveResume(const std::span<const uint8_t> snapshot, const uint32_t ver) {
  using namespace GameScript;
  if (!hasPackageHash) return false;
  if (snapshot.empty() || snapshot.size() > Codec::SNAPSHOT_LIMIT) {
    LOG_ERR("GAME", "%s: refused a %u-byte snapshot", id, static_cast<unsigned>(snapshot.size()));
    return false;
  }
  uint8_t prefix[RESUME_PREFIX_BYTES];
  writeBlobHeader(prefix, RESUME_MAGIC, RESUME_FILE_VERSION);
  std::memcpy(prefix + RESUME_HASH_AT, packageHash, PACKAGE_HASH_BYTES);
  prefix[RESUME_MODE_AT] = RESUME_MODE_SOLO;
  prefix[RESUME_SEATS_AT] = RESUME_SEATS_SOLO;
  // The spine's u16: the low 16 bits of ver.
  prefix[RESUME_VER_AT] = static_cast<uint8_t>(ver & 0xFF);
  prefix[RESUME_VER_AT + 1] = static_cast<uint8_t>((ver >> 8) & 0xFF);
  return replaceFile(id, dirPath, resumePath, resumeTmpPath, prefix, snapshot);
}

bool GameSaveStore::deleteResume() {
  bool gone = true;
  for (const char* path : {resumeTmpPath, resumePath}) {
    if (!Storage.exists(path) || Storage.remove(path)) continue;
    LOG_ERR("GAME", "%s: cannot delete %s", id, path);
    gone = false;
  }
  return gone;
}

bool GameSaveStore::flushResume(SnapshotMailbox& mailbox, const uint32_t nowMs, const bool force) {
  if (!hasPackageHash || !mailbox.pending()) return true;
  if (!force && resumeFailed && nowMs - resumeFailedMs < FLUSH_INTERVAL_MS) return true;
  SnapshotMailbox::Taken taken;
  if (!mailbox.take(buffer, taken)) return true;
  // A snapshot whose status is over is never saved: the round is finished, and a save
  // from before it would resume a round its player already lost or won.
  const bool done = taken.over ? deleteResume() : saveResume({buffer.data(), taken.length}, taken.ver);
  if (done) {
    resumeFailed = false;
    if (!taken.over) {
      LOG_DBG("GAME", "%s: saved resume.bin (%u bytes, ver %u)", id, static_cast<unsigned>(taken.length),
              static_cast<unsigned>(taken.ver));
    }
    return true;
  }
  // The latest snapshot stays pending, so the retry writes whichever is newest by then.
  mailbox.markPending();
  resumeFailed = true;
  resumeFailedMs = nowMs;
  return false;
}

#endif  // FREEINK_CAP_GAMES
