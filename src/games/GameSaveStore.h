#pragma once

#include <Codec.h>
#include <ISnapshotStore.h>
#include <Manifest.h>

#include <cstddef>
#include <cstdint>
#include <span>

#include "GamePaths.h"
#include "SnapshotMailbox.h"

namespace GameScript {
class StoreSlot;
}

// The only reader and writer of /.games-data/<id>/store.bin (AD-17): a blob header
// (magic "CHST", file version 1, the codec version) and then ch.store's codec bytes,
// laid out in docs/crosshatch/formats.md. Loop task only; the VM never touches
// Storage (AD-5). A write goes to store.bin.tmp, then replaces store.bin by a
// rename. A save that is not a valid store table is discarded with a log line and
// never decoded; the file stays until the game's next write replaces it.
//
// It is also the only reader and writer of resume.bin beside it: the latest snapshot
// of a solo match, laid out in the same document, written the same way (resume.bin.tmp,
// then a rename) and read the same way (a whole tmp is read when resume.bin is missing).
class GameSaveStore final : public GameCore::ISnapshotStore {
 public:
  static constexpr char STORE_MAGIC[] = "CHST";
  static constexpr uint8_t STORE_FILE_VERSION = 1;
  // A dirty store is written at most this often by flushIfDue (AD-17).
  static constexpr uint32_t FLUSH_INTERVAL_MS = 5000;
  // The smallest buffer the constructor takes: one whole store.
  static constexpr size_t BUFFER_BYTES = GameScript::Codec::STORE_LIMIT;
  static constexpr char RESUME_MAGIC[] = "CHRS";
  static constexpr uint8_t RESUME_FILE_VERSION = 1;
  // The package hash resume.bin records (GamePkg::HASH_BYTES; GameMatchActivity checks
  // they are equal, since this file builds without GameHash.h).
  static constexpr size_t PACKAGE_HASH_BYTES = 8;

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

  // What /.games-data/<gameId>/resume.bin (or a whole resume.bin.tmp, when it is missing) holds for a
  // match of the package with `pkgHash`. Valid: exactly what loadResume accepts. Its header names this
  // file, file version, codec version, and package, it is a solo save (mode 0, one seat), and its
  // snapshot is 1 to Codec::SNAPSHOT_LIMIT bytes of canonical codec. None: no file, or one that was
  // read and is not that (one line logged, and the file stays). Unreadable: a file is there and
  // could not be checked, because it would not open or read or the buffer below could not be
  // allocated (logged): a card or heap fault that may pass, so the caller must not take it for
  // "no save" and offer a new match over what may be a good one. Reads the whole file into a buffer of its own for
  // the call.
  enum class SaveState : uint8_t { None, Valid, Unreadable };
  static SaveState peek(const char* gameId, const uint8_t (&pkgHash)[PACKAGE_HASH_BYTES]);
  // Before saveResume, flushResume, or loadResume: the hash of the installed package
  // (.pkg). Until it is set, resume.bin is neither read nor written.
  void setPackageHash(const uint8_t (&hash)[PACKAGE_HASH_BYTES]);
  // Before the VM starts: the saved snapshot, and its ver (the file's u16), in the
  // buffer this store owns; valid until this store's next call. Empty when there is no
  // usable save (the reason is logged, and the file stays); `unreadable` is then true if the
  // file was there and could not be opened or read (peek's Unreadable) and false for a file
  // that was read and refused or no file. It is required: a caller that ignored the difference
  // would start a new match over a save that only failed to read.
  std::span<const uint8_t> loadResume(uint16_t& ver, bool& unreadable);
  // Writes `snapshot` at `ver` (its low 16 bits) as resume.bin. False when it could not
  // be written; resume.bin is then as it was.
  bool saveResume(std::span<const uint8_t> snapshot, uint32_t ver);
  // Removes resume.bin and its tmp; false when one is still there. Does nothing (true) without the package hash.
  bool deleteResume();
  // Each loop pass in Playing and Paused, and at Leave and the forced exit: takes the
  // latest snapshot `mailbox` holds and writes it, or, when its status is over, deletes
  // the save instead (a finished round never resumes). A failed write puts the snapshot
  // back and no call retries it until FLUSH_INTERVAL_MS after the failure, Leave and the
  // forced exit included (a card that just failed is not asked again while the device
  // sleeps). Does nothing without a package hash or a pending snapshot. True unless a
  // write was due and failed.
  bool flushResume(SnapshotMailbox& mailbox, uint32_t nowMs);
  // Forgets the failed-write backoff above: Play again starts a round whose first snapshot should replace the
  // finished round's file at once, not FLUSH_INTERVAL_MS after a delete or write that failed for the last round.
  void clearResumeBackoff();

 private:
  // Reads `path` into `out`; null with `length` set when it holds a valid store,
  // otherwise why it does not.
  const char* readValid(const char* path, std::span<uint8_t> out, size_t& length) const;

  char id[GameCore::Manifest::MAX_ID_BYTES + 1] = {};
  char dirPath[GamePaths::DATA_PATH_BYTES] = {};
  char storePath[GamePaths::DATA_PATH_BYTES] = {};
  char tmpPath[GamePaths::DATA_PATH_BYTES] = {};
  char resumePath[GamePaths::DATA_PATH_BYTES] = {};
  char resumeTmpPath[GamePaths::DATA_PATH_BYTES] = {};
  std::span<uint8_t> buffer;
  uint32_t lastWriteMs;
  uint8_t packageHash[PACKAGE_HASH_BYTES] = {};
  bool hasPackageHash = false;
  // The last flushResume write failed, at resumeFailedMs.
  bool resumeFailed = false;
  uint32_t resumeFailedMs = 0;
};
