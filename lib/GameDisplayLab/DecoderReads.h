#pragma once

#include <cstdint>

#include "ImageRows.h"

// The gray viewer's read callbacks for PNGdec and JPEGDEC and its task watchdog's decisions (story dlab-g3), as pure
// code over small interfaces, so the device reader (src/activities/games/display_lab/GrayImageReader.cpp, over HalFile
// and ESP-IDF's task watchdog) and the host tests (test_image_decoders.cpp over a fake file, test_image_rows.cpp over
// fakes of both) run the same code. The answers the reads give are ImageRows.h's (readAllowance, the read caps,
// fillPngRead, answerJpegHeaderRead, answerJpegDecodeRead, jpegDropRestartMarkers, jpegPositionAfterRead); this
// header is the bookkeeping around them: which answer a read gets, the caps' counts, the latch, a refused seek, the
// position the reads have reached.
//
// `File` has:
//   bool isOpen() const;                   // a handle (a decoder's open callback may have failed: null)
//   int32_t read(uint8_t* buf, int32_t n); // up to n bytes at the file's position, which moves by them; negative:
//                                          // failed (the position is then unknown)
//   bool seek(uint32_t position);          // false: refused (past the end, or failed)
//   bool stopped() const;                  // the stop asked of the read (the sink's)
//   void progress();                       // a read gave file bytes (the reader feeds its watchdog)
// The file's position is never asked: DecoderReads::position follows it (0 at the open, a seek's target, plus each
// read's bytes), so a read that can give no bytes makes no file call at all.
namespace GameDisplayLab {

// How many of open()'s reads from the walked header's end on are answered as the decode's: JPEGParseInfo makes one, its
// top-up after SOS (jpeg.inl 1780); a parse that went on past the header the walk found gets nothing after these.
constexpr uint32_t JPEG_OPEN_TOP_UPS = 4;
// How many bytes past the span asked for one decode answer reads at most to fill the room its restart markers left
// (jpegDropRestartMarkers): a valid scan has a byte of data at least for every restart marker (each restart interval
// starts on a byte of its own), so it fills any span long before this; a run of markers past it (crafted, or a damaged
// file) ends the decode in an error (DecoderReads::scanFlooded) rather than reading on through the file.
constexpr uint32_t JPEG_RESTART_READ_ON = 128 * 1024;

// One image's reads: what its walk found (WalkExtent) and what the reads have done. Set by the reader before each
// decoder's open() (start) and decode() (startDecode).
struct DecoderReads {
  // The walked size (WalkExtent::end): the decoder is told it as the file's size, and no read gives a byte past it.
  uint32_t walkedSize = 0;
  // A JPEG's walked header end and the buffer moves its parse takes (WalkExtent::headerEnd, windowMoves).
  uint32_t headerEnd = 0;
  uint32_t windowMoves = 0;
  // The file's position as the reads and seeks have moved it (the file opens at 0).
  uint32_t position = 0;
  // A seek the file refused, or a read that failed (the position is unknown): the reads give nothing until a seek
  // succeeds, rather than bytes from where the file still is (which the decoder would take for the bytes it asked for).
  bool seekFailed = false;
  // JPEGDEC's open() is under way: a read starting before the header's end is the header parse's.
  bool jpegOpening = false;
  // The header parse's reads (against jpegHeaderReadCap), open()'s reads from the header's end on (against
  // JPEG_OPEN_TOP_UPS), and the decode's (against decoderReadCap: open()'s top-up, then decode()'s, counted from 0
  // again at startDecode).
  uint32_t headerReads = 0;
  uint32_t openTopUps = 0;
  uint32_t decodeReads = 0;
  // A header read was dropped: every later header read is too (the parse buffer is zeroed and the file's position no
  // longer JPEGDEC's), and once open() returns the reader gives up (ImageRead::Open) whatever open() said.
  bool headerFailed = false;
  // A read of the file failed twice (it is tried once more), or a seek inside the walked size was refused twice: the
  // picture is the file's failure (ImageRead::Open), never Done; the reader stops drawing and logs it once.
  bool readFailed = false;
  // A decode answer read JPEG_RESTART_READ_ON bytes past its span and still found it all restart markers: the decode
  // ends in an error (ImageRead::Decode); the reader stops drawing.
  bool scanFlooded = false;

  // The read can no longer give the picture: the reader stops drawing.
  bool failed() const { return readFailed || scanFlooded; }

  void start(const WalkExtent& extent) {
    *this = DecoderReads{};
    walkedSize = extent.end;
    headerEnd = extent.headerEnd;
    windowMoves = extent.windowMoves;
  }
  void startJpegOpen() { jpegOpening = true; }
  void startDecode() {
    jpegOpening = false;
    decodeReads = 0;
  }
};

// The file's own read at `reads.position` under the reader's allowance (readAllowance: nothing past the walked size,
// after a refused seek or, when `stopped`, once the stop is asked; then no file call is made): what the file gives,
// the position moved by it. A read that fails or comes up short of the allowance (which already ends at the walked
// size) is tried once more from a seek back to the position (an SD card's transient error), as the walk's reads are;
// failing again it gives nothing, loses the position (as a refused seek) and marks the read failed
// (DecoderReads::readFailed); short again, it gives what it got (a file that shrank since its walk). `allowed` is what
// the file should have given. A read that gave file bytes is progress.
template <typename File>
int32_t readUnderAllowance(File& file, DecoderReads& reads, uint8_t* buf, const int32_t len, const bool stopped,
                           int32_t& allowed) {
  allowed = reads.seekFailed ? 0 : readAllowance(reads.position, len, reads.walkedSize, stopped);
  if (allowed <= 0) return 0;
  int32_t got = file.read(buf, allowed);
  if (got != allowed && file.seek(reads.position)) got = file.read(buf, allowed);
  if (got < 0) {
    reads.seekFailed = true;
    reads.readFailed = true;
    return 0;
  }
  if (got == 0) return 0;
  if (got > allowed) got = allowed;
  reads.position += static_cast<uint32_t>(got);
  file.progress();
  return got;
}

// The walk's read (its readAt): `n` bytes at `offset`, seek and read, tried once more when either fails or the read
// comes up short (an SD card's transient error); false after the second try. `File` as above (seek, read).
template <typename File>
bool readExactlyAt(File& file, const uint32_t offset, uint8_t* out, const size_t n) {
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (file.seek(offset) && file.read(out, static_cast<int32_t>(n)) == static_cast<int32_t>(n)) return true;
  }
  return false;
}

// A call tried once more when it fails (an SD card's transient error): an open, a seek. True when either try worked.
template <typename Call>
bool tryTwice(Call&& call) {
  return call() || call();
}

// PNGdec's read: the length it asked for always, the rest 0xFF (fillPngRead).
template <typename File>
int32_t answerPngRead(File& file, DecoderReads& reads, uint8_t* buf, const int32_t len) {
  int32_t allowed = 0;
  const int32_t got = file.isOpen() ? readUnderAllowance(file, reads, buf, len, file.stopped(), allowed) : 0;
  return fillPngRead(buf, len, got);
}

// Either decoder's seek: the position, or -1 when the file refused it, twice (it is tried once more; the reads then
// give nothing until a seek succeeds). A refused seek inside the walked size is the file's failure
// (DecoderReads::readFailed); past it, the decoder's own (the file may be shorter).
template <typename File>
int32_t answerSeek(File& file, DecoderReads& reads, const int32_t position) {
  if (!file.isOpen()) return -1;
  reads.seekFailed = position < 0 || !tryTwice([&] { return file.seek(static_cast<uint32_t>(position)); });
  if (reads.seekFailed && position >= 0 && static_cast<uint32_t>(position) < reads.walkedSize) reads.readFailed = true;
  if (reads.seekFailed) return -1;
  reads.position = static_cast<uint32_t>(position);
  return position;
}

// JPEGDEC's read. While open() runs, a read starting before the walked header's end is the header parse's, kept whole
// or dropped (answerJpegHeaderRead): dropped at a stop, a short or failed read, a refused seek, past
// jpegHeaderReadCap header reads, or once one was (the latch). Every other read is the decode's, never latched: the
// file's bytes for decoderReadCap reads, their restart markers dropped (jpegDropRestartMarkers: JPEGDEC counts
// restarts by its interval, and its filter would drop them anyway) and the span topped up from the file until it is
// full, the file gives no more (the walked size, a stop, a failure), or JPEG_RESTART_READ_ON bytes past the span were
// read (DecoderReads::scanFlooded: the decode ends in an error), then answerJpegDecodeRead (the rest zeroed, the whole
// span told, runs of 0xFF tamed): a decode answer is never short of its span, so no block starts on fewer bytes than
// the lab patch's refill keeps; open()'s top-up at the header's end is one (past
// JPEG_OPEN_TOP_UPS of them in open(), a read gets a zeroed span and 0, so a parse that ran past the walked header
// ends). Without a handle (the open callback failed) a read is taken as at the walked size. `size` is JPEGFILE.iSize
// (the walked size, or the file's when it shrank) and `iPos` JPEGFILE.iPos, which JPEGDEC tracks itself
// (JPEGGetMoreData compares it with the size): it is the reads' position, held under the size (jpegPositionAfterRead),
// as the JPEG converter keeps it in step with the file.
template <typename File>
int32_t answerJpegRead(File& file, DecoderReads& reads, uint8_t* buf, const int32_t len, const int32_t size,
                       int32_t& iPos) {
  const uint32_t position = file.isOpen() ? reads.position : reads.walkedSize;
  int32_t answer = 0;
  if (reads.jpegOpening && position < reads.headerEnd) {
    const bool overCap = ++reads.headerReads > jpegHeaderReadCap(reads.windowMoves);
    if (reads.headerFailed || overCap || !file.isOpen() || reads.seekFailed || file.stopped()) {
      reads.headerFailed = true;
      answer = answerJpegHeaderRead(buf, len, 0, true);
    } else {
      // The header parse: the file must give all it should, or the read is dropped.
      int32_t expected = 0;
      const int32_t got = readUnderAllowance(file, reads, buf, len, false, expected);
      const bool dropped = got < expected;
      if (dropped) reads.headerFailed = true;
      answer = answerJpegHeaderRead(buf, len, got, dropped);
    }
  } else if (reads.jpegOpening && ++reads.openTopUps > JPEG_OPEN_TOP_UPS) {
    answer = answerJpegHeaderRead(buf, len, 0, false);
  } else {
    int32_t got = 0;
    if (file.isOpen() && ++reads.decodeReads <= decoderReadCap(reads.walkedSize)) {
      int32_t allowed = 0;
      const int32_t first = readUnderAllowance(file, reads, buf, len, file.stopped(), allowed);
      uint32_t read = first > 0 ? static_cast<uint32_t>(first) : 0;
      got = jpegDropRestartMarkers(buf, first);
      while (got < len) {
        if (read >= static_cast<uint32_t>(len) + JPEG_RESTART_READ_ON) {
          reads.scanFlooded = true;
          break;
        }
        const int32_t extra = readUnderAllowance(file, reads, buf + got, len - got, file.stopped(), allowed);
        if (extra <= 0) break;
        read += static_cast<uint32_t>(extra);
        // Over the whole span again: a 0xFF that ended the last read pairs with this one's first byte.
        got = jpegDropRestartMarkers(buf, got + extra);
      }
    }
    answer = answerJpegDecodeRead(buf, len, got);
  }
  iPos = jpegPositionAfterRead(file.isOpen() ? reads.position : reads.walkedSize,
                               static_cast<uint32_t>(size < 0 ? 0 : size));
  return answer;
}

// JPEGDEC's seek: answerSeek, and JPEGFILE.iPos to the new position (held under the size) when it succeeded.
template <typename File>
int32_t answerJpegSeek(File& file, DecoderReads& reads, const int32_t position, const int32_t size, int32_t& iPos) {
  const int32_t at = answerSeek(file, reads, position);
  if (at >= 0) iPos = jpegPositionAfterRead(static_cast<uint32_t>(at), static_cast<uint32_t>(size < 0 ? 0 : size));
  return at;
}

// The task watchdog's quiet time: the time the task has spent outside file calls since its last progress (a row, or a
// read that gave file bytes), and the wall time since that progress. The task leaves the watchdog around each blocking
// file call (a wait on the card's mutex can be long and legitimate), and leaving and rejoining feed the watchdog's
// timer (ESP-IDF's task_wdt.c resets it whenever every other watched task has reset), so a decoder that spins making
// reads but no progress would never trip it: once the quiet time reaches the limit (the watchdog's timeout), or the
// wall time twice the limit, less the time in reads that returned bytes (slow failing reads with a loop between them
// count; a long wait on the card's mutex before a read that gives bytes is no reset; a seek's or an open's wait counts,
// as they return none), the task stops leaving, and the next file call is watched like the rest.
class WatchdogQuiet {
 public:
  explicit WatchdogQuiet(const uint32_t limitMs, const uint32_t nowMs)
      : limitMs_(limitMs), sinceMs_(nowMs), progressMs_(nowMs), callMs_(nowMs) {}
  // A row, or a read that gave file bytes.
  void progress(const uint32_t nowMs) {
    quietMs_ = 0;
    sinceMs_ = nowMs;
    progressMs_ = nowMs;
  }
  // Before a blocking file call: whether the task may leave the watchdog for it.
  bool mayLeave(const uint32_t nowMs) {
    quietMs_ += nowMs - sinceMs_;
    sinceMs_ = nowMs;
    callMs_ = nowMs;
    return quietMs_ < limitMs_ && nowMs - progressMs_ < 2 * limitMs_;
  }
  // After the file call: its time is not quiet time; when it returned bytes (a read: a wait on the card's mutex,
  // however long, before it) not wall time either, so a valid read is never reset for another task's use of the card;
  // any other call (a failed or short read, a seek, an open, a close) counts.
  void back(const uint32_t nowMs, const bool gaveBytes = false) {
    if (gaveBytes) progressMs_ += nowMs - callMs_;
    sinceMs_ = nowMs;
  }
  uint32_t quietMs() const { return quietMs_; }

 private:
  uint32_t limitMs_;
  uint32_t quietMs_ = 0;
  uint32_t sinceMs_;
  uint32_t progressMs_;
  uint32_t callMs_;
};

// What the watchdog said of the task when it was asked to join.
enum class WatchdogState : uint8_t { NotSubscribed, Subscribed, NotConfigured };
// What the reader logs, each once: the watchdog is not configured, the join failed (twice), a rejoin failed (twice).
enum class WatchdogEvent : uint8_t { NotConfigured, JoinFailed, RejoinFailed };

// The sweep task's watchdog for one read, over `Dog`:
//   WatchdogState state();                     // the task's subscription
//   int add();                                 // subscribe the task: 0, or the error
//   int remove();                              // unsubscribe it: 0, or the error
//   void reset();                              // feed it
//   uint32_t nowMs();                          // the millisecond clock
//   void report(WatchdogEvent event, int error);
// Joined when the task was not subscribed (an add failing is tried once more); a task already subscribed is joined
// but not owned (progress feeds it, nothing unsubscribes it, no gap leaves it); not configured, nothing. Owned, the
// task leaves for each blocking file call (gap()) while WatchdogQuiet allows, and rejoins after (tried twice; failing,
// the rest of the read is unwatched and reported); the read's end leaves.
template <typename Dog>
class TaskWatchdog {
 public:
  TaskWatchdog(Dog& dog, const uint32_t limitMs) : dog_(dog), quiet_(limitMs, dog.nowMs()) {
    switch (dog_.state()) {
      case WatchdogState::Subscribed:
        joined_ = true;
        break;
      case WatchdogState::NotConfigured:
        dog_.report(WatchdogEvent::NotConfigured, 0);
        break;
      case WatchdogState::NotSubscribed: {
        const int error = addTwice();
        joined_ = owned_ = error == 0;
        if (error != 0) dog_.report(WatchdogEvent::JoinFailed, error);
        break;
      }
    }
  }
  ~TaskWatchdog() {
    if (owned_) dog_.remove();
  }
  TaskWatchdog(const TaskWatchdog&) = delete;
  TaskWatchdog& operator=(const TaskWatchdog&) = delete;

  bool joined() const { return joined_; }
  bool owned() const { return owned_; }

  // A row, or a read that gave file bytes.
  void progress() {
    if (joined_) dog_.reset();
    quiet_.progress(dog_.nowMs());
  }

  // One blocking file call, from construction to destruction.
  class Gap {
   public:
    explicit Gap(TaskWatchdog* dog) : dog_(dog) {
      left_ = dog_ != nullptr && dog_->quiet_.mayLeave(dog_->dog_.nowMs()) && dog_->owned_ && dog_->dog_.remove() == 0;
    }
    ~Gap() {
      if (dog_ != nullptr) dog_->endGap(left_, gaveBytes_);
    }
    Gap(const Gap&) = delete;
    Gap& operator=(const Gap&) = delete;
    bool left() const { return left_; }
    // The call was a read that returned the bytes asked: its time is not wall time (any other call's is).
    void gaveBytes() { gaveBytes_ = true; }

   private:
    TaskWatchdog* dog_;
    bool left_ = false;
    bool gaveBytes_ = false;
  };

 private:
  int addTwice() {
    const int first = dog_.add();
    return first == 0 ? 0 : dog_.add();
  }
  void endGap(const bool left, const bool gaveBytes) {
    quiet_.back(dog_.nowMs(), gaveBytes);
    if (!left) return;
    const int error = addTwice();
    if (error == 0) return;
    joined_ = owned_ = false;
    dog_.report(WatchdogEvent::RejoinFailed, error);
  }

  Dog& dog_;
  WatchdogQuiet quiet_;
  bool joined_ = false;
  bool owned_ = false;
};

}  // namespace GameDisplayLab
