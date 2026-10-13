#pragma once

// The writer behind the diagnostics log (lib/DiagLog/DiagLog.h): drains the line buffer to per-boot files on the SD
// card through a FileStore, which the device backs with Storage / HalFile (src/diagnostics/DiagLogStorage.cpp) and the
// host tests with a fake. All of its decisions are here, so the host tests run the code the device does: the file
// names, the header, the size cap and rotation, what a write failure does, and what the file says about the lines it
// lost.
//
// Files. /crosshatch-log/b<boot>-s<segment>-<device>.log. The card has no clock, so a boot's number is one more than
// the highest in the folder; a segment is one file of the boot, started when the last reached the size cap. Before a
// file is started the oldest are deleted until at most `maxFiles` remain with it, so the folder cannot fill the card
// (maxFiles x segmentBytes). Each file starts with a header (`# key value` lines; the first is the magic) that names
// the device, the firmware, the boot, and the segment, so one file is read alone.
//
// Writes. Every batch is open, append, close (FileStore::append): no file stays open between batches, because the SD
// card is unmounted for sleep and for the USB drive under any open handle (HalStorage::prepareForDeepSleep), and a
// closed file is complete on the card. A batch is at most chunkBytes. A failed append is counted, its bytes are
// reported in the next file as `lost N bytes`, and the next batch starts a new segment; the writer never retries a
// batch, so a dead card costs the lines the buffer cannot hold and nothing more.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace DiagLog {

// The card file system as the writer uses it. Each call is one whole operation (open, act, close).
class FileStore {
 public:
  // Receives the name of each file in a folder (not its path), one call per file.
  class NameSink {
   public:
    virtual void name(const char* fileName) = 0;

   protected:
    ~NameSink() = default;
  };

  virtual ~FileStore() = default;
  // True while the card is mounted (it is not before boot finishes, after the USB drive took it, or after sleep).
  virtual bool ready() = 0;
  virtual bool makeDirectory(const char* path) = 0;
  virtual bool listFiles(const char* directory, NameSink& sink) = 0;
  virtual bool remove(const char* path) = 0;
  // Creates the file when missing and writes all `length` bytes at its end; true only when every byte was written.
  virtual bool append(const char* path, const char* data, size_t length) = 0;
};

// The strings of a file's header; each must outlive the writer.
struct Identity {
  const char* device;    // the last two bytes of the station MAC, as the nearby label spells them (3F2A)
  const char* board;     // BoardConfig's name of the board
  const char* firmware;  // what names the build: the commit when the build passes it, else the version string
  const char* version;   // CROSSPOINT_VERSION
  const char* built;     // the build's date and time
};

struct Limits {
  size_t segmentBytes = 1024 * 1024;  // a file is closed for the next once it holds this much (a batch may pass it)
  size_t maxFiles = 16;               // the folder holds at most this many, so at most maxFiles x segmentBytes
};

// What one pump did.
enum class Step : uint8_t {
  Idle,      // nothing to write
  Wrote,     // one batch reached the card
  NotReady,  // the card is not mounted
  Failed,    // the card refused a write or a folder step; counted, and the next pump tries again
};

struct Stats {
  uint32_t bytesWritten = 0;
  uint32_t writeFailures = 0;
  uint32_t bytesLost = 0;     // drained from the buffer and not written
  uint32_t linesDropped = 0;  // the buffer was full
  uint32_t segments = 0;      // files started
  uint32_t filesRemoved = 0;
};

inline constexpr char DIRECTORY[] = "/crosshatch-log";
inline constexpr char MAGIC[] = "# crosshatch-diag-log 1";

// The writer's formats, here so the device_log.py test can pin them against this file: the header's `# key value`
// lines are read by `merge`, and the two notices are log lines (the writer's own, origin DIAG) that `summary` counts.
inline constexpr char DROPPED_NOTICE_FORMAT[] = "[%u] [ERR] [DIAG] dropped %u lines (buffer full)\n";
inline constexpr char LOST_NOTICE_FORMAT[] = "[%u] [ERR] [DIAG] lost %u bytes (card write failed)\n";

// Buffer needs drain(char*, size_t), takeDropped(), and used(), as LineBuffer has them.
template <class Buffer>
class Writer {
 public:
  // The scratch every batch, header, and notice is built in; at least this, so the longest line and a notice fit.
  static constexpr size_t MIN_CHUNK = 512;

  Writer(FileStore& store, Buffer& buffer, const Identity& identity, const Limits& limits, char* chunk,
         size_t chunkBytes, uint32_t (*now)())
      : store_(store),
        buffer_(buffer),
        identity_(identity),
        limits_(limits),
        chunk_(chunk),
        chunkBytes_(chunkBytes),
        now_(now) {}

  // Does one step: starts the boot's file when there is something to write and no file, and writes the next batch.
  Step pump() {
    if (chunkBytes_ < MIN_CHUNK || limits_.maxFiles == 0 || limits_.segmentBytes == 0) return Step::Failed;
    if (!store_.ready()) return Step::NotReady;
    const uint32_t dropped = buffer_.takeDropped();
    pendingDropped_ += dropped;
    stats_.linesDropped += dropped;
    if (buffer_.used() == 0 && pendingDropped_ == 0 && pendingLost_ == 0) return Step::Idle;
    if (needFile_ && !startFile()) return Step::Failed;
    // The notices of earlier losses come first, then the lines; `noticeBytes` is how much of the batch is notices. A
    // notice that did not fit stays pending (only one that went into the batch is subtracted once it is written).
    const uint32_t noticeDropped = pendingDropped_;
    const uint32_t noticeLost = pendingLost_;
    size_t used = 0;
    const size_t droppedBytes =
        noticeDropped > 0 ? notice(DROPPED_NOTICE_FORMAT, noticeDropped, chunk_ + used, chunkBytes_ - used) : 0;
    used += droppedBytes;
    const size_t lostBytes =
        noticeLost > 0 ? notice(LOST_NOTICE_FORMAT, noticeLost, chunk_ + used, chunkBytes_ - used) : 0;
    used += lostBytes;
    const bool saidDropped = droppedBytes > 0;
    const bool saidLost = lostBytes > 0;
    const size_t noticeBytes = used;
    used += buffer_.drain(chunk_ + used, chunkBytes_ - used);
    if (used == 0) return Step::Idle;
    if (!store_.append(path_, chunk_, used)) {
      // The batch is gone: its lines count as lost and the next file says so; a notice that was in it is said again
      // there. The next batch starts a new segment, since this file may now end in half a batch, unless the batch
      // before also failed: a card that takes headers and refuses batches would otherwise start a file, and delete the
      // oldest, on every retry. A streak of failures costs one extra segment.
      stats_.writeFailures++;
      stats_.bytesLost += static_cast<uint32_t>(used - noticeBytes);
      pendingLost_ += static_cast<uint32_t>(used - noticeBytes);
      if (!lastBatchFailed_) {
        segment_++;
        needFile_ = true;
      }
      lastBatchFailed_ = true;
      return Step::Failed;
    }
    lastBatchFailed_ = false;
    if (saidDropped) pendingDropped_ -= noticeDropped;
    if (saidLost) pendingLost_ -= noticeLost;
    segmentBytes_ += used;
    stats_.bytesWritten += static_cast<uint32_t>(used);
    if (segmentBytes_ >= limits_.segmentBytes) {  // full: the next batch goes to the next file
      segment_++;
      needFile_ = true;
    }
    return Step::Wrote;
  }

  // Pumps until the buffer is empty or a pump fails, at most a bounded number of batches (appends may go on meanwhile);
  // true when it ended on Idle, that is, everything appended before the call is on the card.
  bool flush() {
    for (int i = 0; i < MAX_FLUSH_PUMPS; i++) {
      const Step step = pump();
      if (step == Step::Idle) return true;
      if (step != Step::Wrote) return false;
    }
    return false;
  }

  const Stats& stats() const { return stats_; }
  uint32_t boot() const { return boot_; }
  uint32_t segment() const { return segment_; }
  const char* path() const { return path_; }

 private:
  static constexpr int MAX_FLUSH_PUMPS = 256;
  static constexpr size_t PATH_BYTES = 96;
  static constexpr size_t NAME_BYTES = 48;

  // What the folder holds: the highest boot, how many log files, and the oldest by (boot, segment).
  class Scan final : public FileStore::NameSink {
   public:
    void name(const char* fileName) override {
      uint32_t bootNumber = 0;
      uint32_t segmentNumber = 0;
      if (!parse(fileName, bootNumber, segmentNumber)) return;
      count++;
      if (bootNumber > maxBoot) maxBoot = bootNumber;
      if (!haveOldest || bootNumber < oldestBoot || (bootNumber == oldestBoot && segmentNumber < oldestSegment)) {
        haveOldest = true;
        oldestBoot = bootNumber;
        oldestSegment = segmentNumber;
        std::snprintf(oldestName, sizeof(oldestName), "%s", fileName);
      }
    }
    uint32_t maxBoot = 0;
    size_t count = 0;
    bool haveOldest = false;
    uint32_t oldestBoot = 0;
    uint32_t oldestSegment = 0;
    char oldestName[NAME_BYTES] = {};
  };

  // "b0007-s02-3F2A.log" is boot 7, segment 2; anything else in the folder is not ours and is left alone.
  static bool parse(const char* name, uint32_t& boot, uint32_t& segment) {
    const size_t length = std::strlen(name);
    if (length < 4 || std::strcmp(name + length - 4, ".log") != 0 || name[0] != 'b') return false;
    const char* p = name + 1;
    if (!number(p, boot) || *p != '-' || p[1] != 's') return false;
    p += 2;
    return number(p, segment) && *p == '-';
  }
  static bool number(const char*& p, uint32_t& value) {
    if (*p < '0' || *p > '9') return false;
    uint32_t result = 0;
    while (*p >= '0' && *p <= '9') {
      if (result > 100000000u) return false;
      result = result * 10 + static_cast<uint32_t>(*p - '0');
      p++;
    }
    value = result;
    return true;
  }

  // One notice line, stamped with the clock as a log line is; 0 when it does not fit (it stays pending).
  size_t notice(const char* format, uint32_t count, char* out, size_t room) const {
    const int written = std::snprintf(out, room, format, static_cast<unsigned>(now_()), static_cast<unsigned>(count));
    return written > 0 && static_cast<size_t>(written) < room ? static_cast<size_t>(written) : 0;
  }

  bool startFile() {
    // Every time: the folder can vanish between files (a card swap, the USB drive).
    if (!store_.makeDirectory(DIRECTORY)) return fail();
    if (boot_ == 0) {
      Scan scan;
      if (!store_.listFiles(DIRECTORY, scan)) return fail();
      boot_ = scan.maxBoot + 1;
      segment_ = 0;
    }
    std::snprintf(path_, sizeof(path_), "%s/b%04u-s%02u-%s.log", DIRECTORY, static_cast<unsigned>(boot_),
                  static_cast<unsigned>(segment_), identity_.device);
    makeRoom();
    int length =
        std::snprintf(chunk_, chunkBytes_,
                      "%s\n"
                      "# device %s\n"
                      "# board %s\n"
                      "# firmware %s\n"
                      "# version %s\n"
                      "# built %s\n"
                      "# boot %u\n"
                      "# segment %u\n"
                      "# opened_ms %u\n",
                      MAGIC, identity_.device, identity_.board, identity_.firmware, identity_.version, identity_.built,
                      static_cast<unsigned>(boot_), static_cast<unsigned>(segment_), static_cast<unsigned>(now_()));
    if (length <= 0 || static_cast<size_t>(length) >= chunkBytes_) {
      // The strings are too long for the scratch: keep the keys `merge` needs rather than never start a file.
      length = std::snprintf(chunk_, chunkBytes_, "%s\n# device %s\n# boot %u\n# segment %u\n# opened_ms %u\n", MAGIC,
                             identity_.device, static_cast<unsigned>(boot_), static_cast<unsigned>(segment_),
                             static_cast<unsigned>(now_()));
    }
    if (length <= 0 || static_cast<size_t>(length) >= chunkBytes_) return fail();
    if (!store_.append(path_, chunk_, static_cast<size_t>(length))) {
      store_.remove(path_);  // best effort: a torn header must not be glued to the retry's
      return fail();
    }
    needFile_ = false;
    segmentBytes_ = static_cast<size_t>(length);
    stats_.segments++;
    stats_.bytesWritten += static_cast<uint32_t>(length);
    return true;
  }

  bool fail() {
    stats_.writeFailures++;
    return false;
  }

  // Deletes the oldest files until the folder has room for the file about to start (its name is path_).
  void makeRoom() {
    for (size_t pass = 0; pass < limits_.maxFiles + 8; pass++) {
      Scan scan;
      if (!store_.listFiles(DIRECTORY, scan)) return;
      if (scan.count + 1 <= limits_.maxFiles || !scan.haveOldest) return;
      char victim[PATH_BYTES];
      std::snprintf(victim, sizeof(victim), "%s/%s", DIRECTORY, scan.oldestName);
      if (!store_.remove(victim)) return;
      stats_.filesRemoved++;
    }
  }

  FileStore& store_;
  Buffer& buffer_;
  Identity identity_;
  Limits limits_;
  char* chunk_;
  size_t chunkBytes_;
  uint32_t (*now_)();
  char path_[PATH_BYTES] = {};
  uint32_t boot_ = 0;  // 0 until the folder has been read
  uint32_t segment_ = 0;
  bool needFile_ = true;
  bool lastBatchFailed_ = false;  // the previous batch was refused: a streak starts one new segment, not one per try
  size_t segmentBytes_ = 0;
  uint32_t pendingDropped_ = 0;  // dropped lines not yet said in a file
  uint32_t pendingLost_ = 0;     // bytes lost to a failed write, not yet said in a file
  Stats stats_;
};

}  // namespace DiagLog
