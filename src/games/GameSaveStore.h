#pragma once

#include <Codec.h>
#include <ISnapshotStore.h>
#include <Manifest.h>
#include <Roster.h>

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
// of a solo or pass match (setRoster; a nearby match has none), laid out in the same
// document, written the same way (resume.bin.tmp, then a rename) and read the same way
// (a whole tmp is read when resume.bin is missing). A save records its mode and seat
// count; the forms of peek and loadResume that take the game's manifest and the host's
// caps accept one that game can start on that host, and the older forms a solo one only. The firmware
// calls only the newer forms; the older ones stay for the host suites, so a new caller takes the newer.
//
// And of prefs.bin beside them: the game's remembered mode and settings (loadPrefs, savePrefs), which the title screen
// reads when it opens and writes on the loop task or in its own onExit() (AD-17 as amended 2026-10-04), never the
// match.
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
  // snapshot is 1 to Codec::SNAPSHOT_LIMIT bytes of canonical codec. Unstartable: a save of this
  // package whose mode or seat count this game cannot start on this host, or a later firmware's: a mode
  // byte, a codec version, or a snapshot size this firmware does not know, or a newer file version
  // (logged quietly, at LOG_INF, as kept): there is a save, which a new match would replace, but it
  // cannot be resumed here. None: no file, or one that was read and is none of
  // these (another package's, or malformed; one line logged, and the file stays). Unreadable: a file
  // is there and could not be checked, because it would not open or read or the buffer below could
  // not be allocated (logged): a card or heap fault that may pass, so the caller must not take it for
  // "no save" and offer a new match over what may be a good one. Reads the file into a buffer of its own for the
  // call (of a file over the size limit, only its fixed part); peekResume reads through the store's buffer
  // instead. Solo saves only: a pass save is Unstartable here (and kept); the form below takes it.
  enum class SaveState : uint8_t { None, Valid, Unreadable, Unstartable };
  static SaveState peek(const char* gameId, const uint8_t (&pkgHash)[PACKAGE_HASH_BYTES]);
  // As above for `game` (its id names the folder), accepting a solo or pass save whose mode and seat count
  // `game` can start on `host`: solo with one seat, or pass with max(2, seats.min) to
  // min(seats.max, host.maxSeats, Roster::MAX_SEATS) seats, each only where game.check(host) starts that mode.
  // A well-formed save it cannot start, or a later firmware's (above), is Unstartable, logged quietly and kept;
  // a malformed one (`bad seat count`) is None with an error line, and kept too. Valid exactly when the loadResume
  // below with the same `game` and `host` accepts the save.
  static SaveState peek(const GameCore::Manifest& game, const uint8_t (&pkgHash)[PACKAGE_HASH_BYTES],
                        const GameCore::HostCaps& host);
  // The peek above for this store's game id and package hash (None until setPackageHash, and with a nearby roster, as
  // loadResume reads nothing then). The match's Continue asks it after a loadResume that returned nothing, to learn
  // why. It reads through this store's buffer, which nothing references then, and allocates nothing; it spends the
  // buffer (as loadResume does) but never changes the roster, so the match still writes its own.
  SaveState peekResume(const GameCore::Manifest& game, const GameCore::HostCaps& host);
  // Before saveResume, flushResume, or loadResume: the hash of the installed package
  // (.pkg). Until it is set, resume.bin is neither read nor written.
  void setPackageHash(const uint8_t (&hash)[PACKAGE_HASH_BYTES]);
  // Before the first saveResume or flushResume: who plays this match, which a save records (its mode, and
  // its seat count for pass). Solo until called, or until the loadResume below loads a save (a resumed match
  // writes the roster it loaded; a setRoster after that load wins). A nearby roster turns resume.bin off:
  // nothing is read, written, or deleted (AD-17), as without the package hash.
  void setRoster(const GameCore::Roster& roster);
  // Before the VM starts: the saved snapshot, and its ver (the file's u16), in the
  // buffer this store owns; valid until this store's next call. Empty when there is no
  // usable save (the reason is logged, and the file stays); `unreadable` is then true if the
  // file was there and could not be opened or read (peek's Unreadable) and false for a file
  // that was read and refused or no file. It is required: a caller that ignored the difference
  // would start a new match over a save that only failed to read. Solo saves only: a pass save is
  // refused (logged, and kept); the form below takes it.
  std::span<const uint8_t> loadResume(uint16_t& ver, bool& unreadable);
  // As above, accepting what peek(game, pkgHash, host) calls Valid; `saved` is the roster the save
  // records (Roster::solo(), or Roster::pass(n)), and Roster::solo() when nothing is loaded. A save it
  // loads also becomes this store's roster, so the resumed match's writes keep its mode and seats.
  std::span<const uint8_t> loadResume(uint16_t& ver, bool& unreadable, const GameCore::Manifest& game,
                                      const GameCore::HostCaps& host, GameCore::Roster& saved);
  // Writes `snapshot` at `ver` (its low 16 bits) as resume.bin. False when it could not
  // be written; resume.bin is then as it was.
  bool saveResume(std::span<const uint8_t> snapshot, uint32_t ver);
  // Removes resume.bin and its tmp; false when one is still there. Does nothing (true) without the package hash
  // or with a nearby roster.
  bool deleteResume();
  // Each loop pass in Playing and Paused, and at Leave and the forced exit: takes the
  // latest snapshot `mailbox` holds and writes it, or, when its status is over, deletes
  // the save instead (a finished round never resumes). A failed write puts the snapshot
  // back and no call retries it until FLUSH_INTERVAL_MS after the failure, Leave and the
  // forced exit included (a card that just failed is not asked again while the device
  // sleeps). Does nothing without a package hash, with a nearby roster, or without a
  // pending snapshot. True unless a write was due and failed.
  bool flushResume(SnapshotMailbox& mailbox, uint32_t nowMs);
  // Forgets the failed-write backoff above: Play again starts a round whose first snapshot should replace the
  // finished round's file at once, not FLUSH_INTERVAL_MS after a delete or write that failed for the last round.
  void clearResumeBackoff();
  // How many times flushResume has made the previous resume.bin go so far: a snapshot written over it, a write that
  // failed after it had removed the old file (the new snapshot then waits in resume.bin.tmp, which loadResume reads),
  // or the delete of a finished round's save. A try that left the old file in place is not counted. A caller that
  // must know whether the old file is gone compares it before and after.
  uint32_t resumeReplacements() const { return resumeReplaceCount; }

  // ---- prefs.bin (AD-17, as amended 2026-10-02): a game's remembered choices ----

  static constexpr char PREFS_MAGIC[] = "CHPF";
  static constexpr uint8_t PREFS_FILE_VERSION = 1;
  // The longest prefs.bin: magic, version, mode, count, then per setting an id and a value, each after its length.
  static constexpr size_t PREFS_MAX_BYTES =
      4 + 3 +
      GameCore::SettingValues::MAX_SETTINGS *
          (2 + GameCore::ManifestSetting::MAX_ID_BYTES + GameCore::ManifestSetting::MAX_VALUE_BYTES);

  // What prefs.bin holds (docs/crosshatch/formats.md): the mode last started, as a GameCore::Manifest::Mode bit (0 for
  // none), and each setting's chosen value by id, as the manifest spelled them when they were written.
  struct Prefs {
    uint8_t mode = 0;
    GameCore::SettingValues settings;
  };
  // loadPrefs' answer. None: no file. Loaded: `out` holds it. Malformed: a file that was read and is not a prefs.bin
  // this firmware reads (logged at LOG_INF). Unreadable: a file is there and would not open or read (logged at
  // LOG_INF). Every answer but Loaded leaves `out` empty, and none is an error: the title screen falls back to the
  // manifest's defaults (resolvePrefs), and the file stays until the next savePrefs replaces it.
  enum class PrefsState : uint8_t { None, Loaded, Malformed, Unreadable };
  // Loop task, or the title screen's onExit() (rememberChoices' re-read); never in render(): reads
  // /.games-data/<gameId>/prefs.bin (or a whole prefs.bin.tmp, when it is missing) straight into `out`, with no buffer
  // of its own.
  static PrefsState loadPrefs(const char* gameId, Prefs& out);
  // Loop task, or the title screen's onExit() (AD-17 as amended 2026-10-04); never in render(): writes `prefs` as
  // prefs.bin by way of prefs.bin.tmp and a rename, as saveResume writes resume.bin. False (logged) when it could not,
  // or when `prefs` holds an entry no file can (an id or value empty or over its field, or more than MAX_SETTINGS);
  // prefs.bin is then as it was.
  static bool savePrefs(const char* gameId, const Prefs& prefs);

  // The title screen's choices: the current mode (a Manifest::Mode bit; 0 when `hostModes` is empty) and, for each
  // setting the manifest declares, the index of its chosen value.
  struct Choices {
    uint8_t mode = 0;
    uint8_t valueIndex[GameCore::ManifestSettings::MAX_SETTINGS] = {};
  };
  // The choices `saved` makes for `game` on a host that can start `hostModes` (CheckResult::modes), value by value: the
  // mode is game.startMode(saved.mode, hostModes); each declared setting takes the value saved under its id when the
  // manifest still declares that value, else its default. A remembered choice that falls back is logged at LOG_DBG
  // (a stale file is not an error). Pure but for the log.
  static Choices resolvePrefs(const Prefs& saved, const GameCore::Manifest& game,
                              const GameCore::ManifestSettings& settings, uint8_t hostModes);
  // What prefs.bin should hold for `choices`: its mode and each declared setting's chosen value (valuesAt).
  static Prefs prefsOf(const Choices& choices, const GameCore::ManifestSettings& settings);

 private:
  // The saved modes and seat counts a match may resume (defined in the .cpp).
  struct Startable;
  // What the solo-only peek and loadResume accept: a solo save.
  static const Startable SOLO_ONLY;
  // What `game` can start on `host`, by game.check(host) and the pass seat range.
  static Startable startableFor(const GameCore::Manifest& game, const GameCore::HostCaps& host);
  // Reads the resume file at `path` for a match of the package `pkgHash` that can start `startable`; null when it
  // can resume one (`saved` is then its roster), otherwise why it cannot.
  static const char* readResume(const char* path, const uint8_t (&pkgHash)[PACKAGE_HASH_BYTES],
                                const Startable& startable, uint16_t& ver, std::span<uint8_t> out, size_t& length,
                                GameCore::Roster& saved, bool* unreadable);
  // The peeks, for the saves `startable` allows, reading the snapshot into `scratch` (at least SNAPSHOT_LIMIT bytes),
  // or, when it is empty, into one snapshot's worth allocated for the call (the static peeks).
  static SaveState peekStartable(const char* gameId, const uint8_t (&pkgHash)[PACKAGE_HASH_BYTES],
                                 const Startable& startable, std::span<uint8_t> scratch);
  // Both loadResumes, for the saves `startable` allows.
  std::span<const uint8_t> loadStartable(uint16_t& ver, bool& unreadable, const Startable& startable,
                                         GameCore::Roster& saved);
  // resume.bin is read and written: the package hash is set and the roster is not nearby.
  bool resumeOn() const;

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
  GameCore::Roster roster = GameCore::Roster::solo();
  // The last flushResume write failed, at resumeFailedMs.
  bool resumeFailed = false;
  uint32_t resumeFailedMs = 0;
  uint32_t resumeReplaceCount = 0;
};
