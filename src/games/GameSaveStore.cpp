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
// hash, the mode (0 solo, 1 pass), the seat count n (1 for solo), and the ver as a u16,
// little-endian. The mode bytes are the file's own, never GameCore::Mode's values.
constexpr size_t RESUME_HASH_AT = GameScript::BLOB_HEADER_BYTES;
constexpr size_t RESUME_MODE_AT = RESUME_HASH_AT + GameSaveStore::PACKAGE_HASH_BYTES;
constexpr size_t RESUME_SEATS_AT = RESUME_MODE_AT + 1;
constexpr size_t RESUME_VER_AT = RESUME_SEATS_AT + 1;
constexpr size_t RESUME_PREFIX_BYTES = RESUME_VER_AT + 2;
constexpr uint8_t RESUME_MODE_SOLO = 0;
constexpr uint8_t RESUME_MODE_PASS = 1;
constexpr uint8_t RESUME_SEATS_SOLO = 1;
static_assert(RESUME_PREFIX_BYTES <= 32, "the resume prefix lives on the stack");
static_assert(GameScript::Codec::SNAPSHOT_LIMIT == GameCore::SNAPSHOT_BYTES,
              "the codec's snapshot limit is the Session's snapshot size (AD-10)");
static_assert(GameSaveStore::BUFFER_BYTES >= GameScript::Codec::SNAPSHOT_LIMIT,
              "the store's buffer holds a whole snapshot, which loadResume and flushResume copy through it");

// Why readResume refuses a save of another package. The file stays: it is that package's, and a reinstall of this
// game's package keeps its saved data (AD-16), so peek logs it quietly and the match logs it as it does any refusal.
constexpr char OTHER_PACKAGE[] = "other package";

// Why readResume refuses a save this game or host cannot start: its mode, or its seat count for that mode. The
// file stays, and peek logs it quietly: the title screen asks again each time it opens, and the same game on another
// host, or after an update, may start it.
constexpr char MODE_NOT_STARTABLE[] = "mode not startable";
constexpr char SEATS_NOT_STARTABLE[] = "seats not startable";
// A seat count no save of its mode has (solo n other than 1; pass n below 2 or over Roster::MAX_SEATS): a malformed
// file, refused at LOG_ERR as other malformed files are.
constexpr char BAD_SEAT_COUNT[] = "bad seat count";
// A mode byte this firmware does not write (2..255): a later firmware's mode, perhaps, so the save is kept as one this
// host cannot start, not taken for a malformed file.
constexpr char UNKNOWN_MODE[] = "unknown mode";

// Whether a refusal makes the save Unstartable, which peek and loadResume log at LOG_INF as kept: a save of this
// package that is fine, or may be, only not this host's to resume, which a new match must not replace without asking.
bool unstartableHere(const char* problem) {
  return std::strcmp(problem, MODE_NOT_STARTABLE) == 0 || std::strcmp(problem, SEATS_NOT_STARTABLE) == 0 ||
         std::strcmp(problem, UNKNOWN_MODE) == 0;
}

// The mode resume.bin's mode byte names; false for a byte no mode is written as (a nearby match saves nothing).
bool modeOfByte(const uint8_t byte, GameCore::Mode& mode) {
  switch (byte) {
    case RESUME_MODE_SOLO:
      mode = GameCore::Mode::Solo;
      return true;
    case RESUME_MODE_PASS:
      mode = GameCore::Mode::Pass;
      return true;
    default:  // 2..255: a mode this firmware does not save
      return false;
  }
}

// Replaces `path` with `head` then `body` by way of `tmp`, as saveStore does for
// store.bin (see there for why each step is ordered as it is); false when it could not. `removedOld` (when given) is
// set once the old `path` has been removed, whether or not the rename after it then succeeds: from there the old file
// is gone and `tmp` holds the new one.
bool replaceFile(const char* id, const char* dir, const char* path, const char* tmp,
                 const std::span<const uint8_t> head, const std::span<const uint8_t> body, bool* removedOld = nullptr) {
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
  if (Storage.exists(path)) {
    if (!Storage.remove(path)) {
      LOG_ERR("GAME", "%s: cannot replace %s", id, path);
      return false;
    }
    if (removedOld) *removedOld = true;
  }
  if (!Storage.rename(tmp, path)) {
    LOG_ERR("GAME", "%s: cannot rename %s to %s", id, tmp, path);
    return false;
  }
  return true;
}

}  // namespace

struct GameSaveStore::Startable {
  bool solo;
  bool pass;
  // The seats a pass save may have, when `pass`.
  uint8_t passMin;
  uint8_t passMax;
};

const GameSaveStore::Startable GameSaveStore::SOLO_ONLY = {true, false, 0, 0};

GameSaveStore::Startable GameSaveStore::startableFor(const GameCore::Manifest& game, const GameCore::HostCaps& host) {
  using GameCore::Manifest;
  const GameCore::CheckResult check = game.check(host);
  if (!check.ok()) return Startable{false, false, 0, 0};
  const bool solo = (check.modes & Manifest::MODE_SOLO) != 0;
  // passSeats is the fewest a pass match has, max(2, seats.min), or 0 when no pass match fits this host; the most
  // is the same three bounds' minimum. lib/GameCore keeps the range itself out of its API, so it is taken here.
  const uint8_t fewest = GameCore::passSeats(game.seatsMin, game.seatsMax, host.maxSeats);
  const bool pass = (check.modes & Manifest::MODE_PASS) != 0 && fewest != 0;
  if (!pass) return Startable{solo, false, 0, 0};
  const int32_t most = std::min<int32_t>({game.seatsMax, host.maxSeats, GameCore::Roster::MAX_SEATS});
  return Startable{solo, true, fewest, static_cast<uint8_t>(most)};
}

// Reads the resume file at `path` for a match of the package `pkgHash`; null when it
// can resume one, otherwise why it cannot. `unreadable` (when given) says whether the
// file could not be opened or read at all, a card or device fault that a later try may
// not repeat, as against a file that was read and is not a usable save. The snapshot (1 to Codec::SNAPSHOT_LIMIT
// bytes after the fixed part) is read into `out` and checked as a codec value, and
// `length` is its length. `ver` is the file's once the fixed part was read and its mode and seats accepted.
// `saved` is the save's roster when it is accepted, Roster::solo() otherwise.
const char* GameSaveStore::readResume(const char* path, const uint8_t (&pkgHash)[PACKAGE_HASH_BYTES],
                                      const Startable& startable, uint16_t& ver, const std::span<uint8_t> out,
                                      size_t& length, GameCore::Roster& saved, bool* unreadable) {
  using namespace GameScript;
  using GameCore::Mode;
  using GameCore::Roster;
  length = 0;
  ver = 0;
  saved = Roster::solo();
  if (unreadable) *unreadable = false;
  HalFile file;
  if (!Storage.openFileForRead("GAME", path, file)) {
    if (unreadable) *unreadable = true;
    return "cannot open";
  }
  const size_t size = file.fileSize();
  const bool tooLarge = size > RESUME_PREFIX_BYTES + std::min(out.size(), Codec::SNAPSHOT_LIMIT);
  uint8_t prefix[RESUME_PREFIX_BYTES] = {};
  const size_t prefixBytes = std::min(size, RESUME_PREFIX_BYTES);
  const size_t snapshotBytes = size - prefixBytes;
  const bool read = !tooLarge && readExactly(file, prefix, prefixBytes) && readExactly(file, out.data(), snapshotBytes);
  file.close();
  if (tooLarge) return "too large";
  if (!read) {
    if (unreadable) *unreadable = true;
    return "cannot read";
  }

  const BlobHeaderStatus status = checkBlobHeader(prefix, std::min(prefixBytes, BLOB_HEADER_BYTES),
                                                  GameSaveStore::RESUME_MAGIC, GameSaveStore::RESUME_FILE_VERSION);
  if (status != BlobHeaderStatus::Ok) return blobHeaderStatusName(status);
  if (prefixBytes < RESUME_PREFIX_BYTES) return "truncated";
  // Before the mode: another package's save is that package's whatever its mode, and stays a quiet line in peek.
  if (std::memcmp(prefix + RESUME_HASH_AT, pkgHash, GameSaveStore::PACKAGE_HASH_BYTES) != 0) return OTHER_PACKAGE;
  Mode mode = Mode::Solo;
  if (!modeOfByte(prefix[RESUME_MODE_AT], mode)) return UNKNOWN_MODE;
  const uint8_t seats = prefix[RESUME_SEATS_AT];
  Roster found;
  switch (mode) {
    case Mode::Solo:
      if (seats != RESUME_SEATS_SOLO) return BAD_SEAT_COUNT;
      if (!startable.solo) return MODE_NOT_STARTABLE;
      found = Roster::solo();
      break;
    case Mode::Pass:
      if (seats < 2 || seats > Roster::MAX_SEATS) return BAD_SEAT_COUNT;
      if (!startable.pass) return MODE_NOT_STARTABLE;
      if (seats < startable.passMin || seats > startable.passMax) return SEATS_NOT_STARTABLE;
      found = Roster::pass(seats);
      break;
    case Mode::Nearby:  // modeOfByte never gives it: a nearby match saves nothing (AD-17)
      return MODE_NOT_STARTABLE;
  }
  ver = static_cast<uint16_t>(prefix[RESUME_VER_AT] | (prefix[RESUME_VER_AT + 1] << 8));
  if (snapshotBytes == 0) return "empty snapshot";
  bool isTable = false;
  const Codec::Error error = Codec::check(out.data(), snapshotBytes, Codec::SNAPSHOT_LIMIT, isTable);
  if (error != Codec::Error::None) return Codec::errorName(error);
  length = snapshotBytes;
  saved = found;
  return nullptr;
}

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

GameSaveStore::SaveState GameSaveStore::peek(const char* gameId, const uint8_t (&pkgHash)[PACKAGE_HASH_BYTES]) {
  return peekStartable(gameId, pkgHash, SOLO_ONLY);
}

GameSaveStore::SaveState GameSaveStore::peek(const GameCore::Manifest& game,
                                             const uint8_t (&pkgHash)[PACKAGE_HASH_BYTES],
                                             const GameCore::HostCaps& host) {
  return peekStartable(game.id, pkgHash, startableFor(game, host));
}

GameSaveStore::SaveState GameSaveStore::peekStartable(const char* gameId, const uint8_t (&pkgHash)[PACKAGE_HASH_BYTES],
                                                      const Startable& startable) {
  char path[GamePaths::DATA_PATH_BYTES];
  snprintf(path, sizeof(path), "%s/%s/resume.bin", GamePaths::GAMES_DATA_DIR, gameId);
  if (!Storage.exists(path)) {
    snprintf(path, sizeof(path), "%s/%s/resume.bin.tmp", GamePaths::GAMES_DATA_DIR, gameId);
    if (!Storage.exists(path)) return SaveState::None;
  }
  // A static call has no store buffer: one snapshot's worth, allocated for this call only
  // (the title screen asks once, when it opens, never while it draws or a match runs).
  std::unique_ptr<uint8_t[]> snapshot(new (std::nothrow) uint8_t[GameScript::Codec::SNAPSHOT_LIMIT]);
  if (!snapshot) {
    // A file is there and could not be checked: not the same as no save, or the title screen would hide a good one.
    LOG_ERR("GAME", "%s: OOM: %u bytes to check %s", gameId, static_cast<unsigned>(GameScript::Codec::SNAPSHOT_LIMIT),
            path);
    return SaveState::Unreadable;
  }
  uint16_t ver = 0;
  size_t length = 0;
  bool unreadable = false;
  GameCore::Roster saved;
  if (const char* problem =
          readResume(path, pkgHash, startable, ver, {snapshot.get(), GameScript::Codec::SNAPSHOT_LIMIT}, length, saved,
                     &unreadable)) {
    // The title screen asks again each time it opens, so a save of another package (which stays) is not an error, and
    // nor is a well-formed save this form, game, or host cannot resume (it stays too).
    if (std::strcmp(problem, OTHER_PACKAGE) == 0) {
      LOG_INF("GAME", "%s: %s is another package's save: %s", gameId, path, problem);
    } else if (unstartableHere(problem)) {
      LOG_INF("GAME", "%s: %s is a save that cannot be resumed here: %s; the file is kept", gameId, path, problem);
      return SaveState::Unstartable;
    } else if (unreadable) {
      LOG_ERR("GAME", "%s: could not check %s: %s; the file is kept", gameId, path, problem);
    } else {
      LOG_ERR("GAME", "%s: discarded %s: %s", gameId, path, problem);
    }
    return unreadable ? SaveState::Unreadable : SaveState::None;
  }
  return SaveState::Valid;
}

GameSaveStore::SaveState GameSaveStore::peekResume(const GameCore::Manifest& game,
                                                   const GameCore::HostCaps& host) const {
  // As loadResume: without the package hash no file here is known to be this package's, and a nearby match has none.
  if (!resumeOn()) return SaveState::None;
  return peekStartable(id, packageHash, startableFor(game, host));
}

void GameSaveStore::setPackageHash(const uint8_t (&hash)[PACKAGE_HASH_BYTES]) {
  std::memcpy(packageHash, hash, PACKAGE_HASH_BYTES);
  hasPackageHash = true;
}

void GameSaveStore::setRoster(const GameCore::Roster& matchRoster) { roster = matchRoster; }

bool GameSaveStore::resumeOn() const { return hasPackageHash && roster.mode != GameCore::Mode::Nearby; }

std::span<const uint8_t> GameSaveStore::loadResume(uint16_t& ver, bool& unreadable) {
  GameCore::Roster saved;
  return loadStartable(ver, unreadable, SOLO_ONLY, saved);
}

std::span<const uint8_t> GameSaveStore::loadResume(uint16_t& ver, bool& unreadable, const GameCore::Manifest& game,
                                                   const GameCore::HostCaps& host, GameCore::Roster& saved) {
  const std::span<const uint8_t> snapshot = loadStartable(ver, unreadable, startableFor(game, host), saved);
  // A resumed match goes on writing the roster it loaded (a pass save stays a pass save); setRoster after this wins.
  if (!snapshot.empty()) roster = saved;
  return snapshot;
}

std::span<const uint8_t> GameSaveStore::loadStartable(uint16_t& ver, bool& unreadable, const Startable& startable,
                                                      GameCore::Roster& saved) {
  ver = 0;
  unreadable = false;
  saved = GameCore::Roster::solo();
  if (!resumeOn()) return {};
  const char* path = resumePath;
  if (!Storage.exists(resumePath)) {
    if (!Storage.exists(resumeTmpPath)) return {};
    // A write stopped between removing resume.bin and the rename (see loadStore).
    LOG_INF("GAME", "%s: no resume.bin; reading %s", id, resumeTmpPath);
    path = resumeTmpPath;
  }
  size_t length = 0;
  bool fault = false;
  if (const char* problem = readResume(path, packageHash, startable, ver, buffer, length, saved, &fault)) {
    if (fault) {
      LOG_ERR("GAME", "%s: could not read %s: %s; the file is kept", id, path, problem);
    } else if (unstartableHere(problem)) {
      LOG_INF("GAME", "%s: %s is a save that cannot be resumed here: %s; the file is kept", id, path, problem);
    } else {
      LOG_ERR("GAME", "%s: discarded %s: %s", id, path, problem);
    }
    ver = 0;
    unreadable = fault;
    return {};
  }
  return {buffer.data(), length};
}

bool GameSaveStore::saveResume(const std::span<const uint8_t> snapshot, const uint32_t ver) {
  using namespace GameScript;
  if (!resumeOn()) return false;
  if (snapshot.empty() || snapshot.size() > Codec::SNAPSHOT_LIMIT) {
    LOG_ERR("GAME", "%s: refused a %u-byte snapshot", id, static_cast<unsigned>(snapshot.size()));
    return false;
  }
  uint8_t prefix[RESUME_PREFIX_BYTES];
  writeBlobHeader(prefix, RESUME_MAGIC, RESUME_FILE_VERSION);
  std::memcpy(prefix + RESUME_HASH_AT, packageHash, PACKAGE_HASH_BYTES);
  switch (roster.mode) {
    case GameCore::Mode::Solo:
      prefix[RESUME_MODE_AT] = RESUME_MODE_SOLO;
      prefix[RESUME_SEATS_AT] = RESUME_SEATS_SOLO;
      break;
    case GameCore::Mode::Pass:
      prefix[RESUME_MODE_AT] = RESUME_MODE_PASS;
      prefix[RESUME_SEATS_AT] = roster.seats;
      break;
    case GameCore::Mode::Nearby:  // resumeOn() refused it above: a nearby match saves nothing (AD-17)
      return false;
  }
  // The spine's u16: the low 16 bits of ver.
  prefix[RESUME_VER_AT] = static_cast<uint8_t>(ver & 0xFF);
  prefix[RESUME_VER_AT + 1] = static_cast<uint8_t>((ver >> 8) & 0xFF);
  bool removedOld = false;
  const bool written = replaceFile(id, dirPath, resumePath, resumeTmpPath, prefix, snapshot, &removedOld);
  if (written || removedOld) ++resumeReplaceCount;
  return written;
}

bool GameSaveStore::deleteResume() {
  // Without the package hash this match neither read nor wrote a save (flushResume does nothing either), and a file
  // there is not known to be this package's: a match that could not tell (its .pkg would not read) must not remove a
  // save at Over that a Continue may still offer. A nearby match keeps no save (AD-17), so it never deletes one either.
  if (!resumeOn()) return true;
  bool gone = true;
  for (const char* path : {resumeTmpPath, resumePath}) {
    if (!Storage.exists(path) || Storage.remove(path)) continue;
    LOG_ERR("GAME", "%s: cannot delete %s", id, path);
    gone = false;
  }
  return gone;
}

void GameSaveStore::clearResumeBackoff() { resumeFailed = false; }

bool GameSaveStore::flushResume(SnapshotMailbox& mailbox, const uint32_t nowMs) {
  if (!resumeOn() || !mailbox.pending()) return true;
  if (resumeFailed && nowMs - resumeFailedMs < FLUSH_INTERVAL_MS) return true;
  SnapshotMailbox::Taken taken;
  if (!mailbox.take(buffer, taken)) return true;
  // A snapshot whose status is over is never saved: the round is finished, and a save
  // from before it would resume a round its player already lost or won.
  const bool done = taken.over ? deleteResume() : saveResume({buffer.data(), taken.length}, taken.ver);
  if (done) {
    resumeFailed = false;
    if (taken.over) ++resumeReplaceCount;  // the delete took the file away
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
