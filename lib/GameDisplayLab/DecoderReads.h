#pragma once

#include <cstdint>

#include "ImageRows.h"

// The gray viewer's read callbacks for PNGdec and JPEGDEC (story dlab-g3), as pure functions over a small file
// interface, so the device reader (src/activities/games/display_lab/GrayImageReader.cpp, over HalFile) and the host
// decoder test (test/game_display_lab/test_image_decoders.cpp, over a fake file) run the same code. The answers they
// give are ImageRows.h's (readAllowance, the read caps, fillPngRead, answerJpegHeaderRead, answerJpegDecodeRead,
// jpegPositionAfterRead); this header is the bookkeeping around them: which answer a read gets, the caps' counts, the
// latch, a refused seek.
//
// `File` has:
//   bool isOpen() const;                   // a handle (a decoder's open callback may have failed: null)
//   uint32_t position();                   // the file's own position
//   int32_t read(uint8_t* buf, int32_t n); // up to n bytes at the position, which moves by them; negative: failed
//   bool seek(uint32_t position);          // false: refused (past the end, or failed)
//   bool stopped() const;                  // the stop asked of the read (the sink's)
//   void progress();                       // a read gave file bytes (the reader feeds its watchdog)
namespace GameDisplayLab {

// How many of open()'s reads from the walked header's end on are answered as the decode's: JPEGParseInfo makes one, its
// top-up after SOS (jpeg.inl 1780); a parse that went on past the header the walk found gets nothing after these.
constexpr uint32_t JPEG_OPEN_TOP_UPS = 4;

// One image's reads: what its walk found (WalkExtent) and what the reads have done. Set by the reader before each
// decoder's open() (start) and decode() (startDecode).
struct DecoderReads {
  // The walked size (WalkExtent::end): the decoder is told it as the file's size, and no read gives a byte past it.
  uint32_t walkedSize = 0;
  // A JPEG's walked header end and the buffer moves its parse takes (WalkExtent::headerEnd, windowMoves).
  uint32_t headerEnd = 0;
  uint32_t windowMoves = 0;
  // A seek the file refused: the reads give nothing until a seek succeeds, rather than bytes from where the file still
  // is (which the decoder would take for the bytes where it asked).
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

// The file's own read under the reader's allowance (readAllowance: nothing past the walked size, after a refused seek
// or, when `stopped`, once the stop is asked): what the file gives, a failed read as nothing. `allowed` is what the
// file should have given. A read that gave file bytes is progress.
template <typename File>
int32_t readUnderAllowance(File& file, const DecoderReads& reads, const uint32_t position, uint8_t* buf,
                           const int32_t len, const bool stopped, int32_t& allowed) {
  allowed = reads.seekFailed ? 0 : readAllowance(position, len, reads.walkedSize, stopped);
  if (allowed <= 0) return 0;
  const int32_t got = file.read(buf, allowed);
  if (got <= 0) return 0;
  file.progress();
  return got < allowed ? got : allowed;
}

// PNGdec's read: the length it asked for always, the rest 0xFF (fillPngRead).
template <typename File>
int32_t answerPngRead(File& file, const DecoderReads& reads, uint8_t* buf, const int32_t len) {
  int32_t allowed = 0;
  const int32_t got =
      file.isOpen() ? readUnderAllowance(file, reads, file.position(), buf, len, file.stopped(), allowed) : 0;
  return fillPngRead(buf, len, got);
}

// Either decoder's seek: the position, or -1 when the file refused it (the reads then give nothing until a seek
// succeeds).
template <typename File>
int32_t answerSeek(File& file, DecoderReads& reads, const int32_t position) {
  if (!file.isOpen()) return -1;
  reads.seekFailed = position < 0 || !file.seek(static_cast<uint32_t>(position));
  return reads.seekFailed ? -1 : position;
}

// JPEGDEC's read. While open() runs, a read starting before the walked header's end is the header parse's, kept whole
// or dropped (answerJpegHeaderRead): dropped at a stop, a short or failed read, a refused seek, past
// jpegHeaderReadCap header reads, or once one was (the latch). Every other read is the decode's, never latched
// (answerJpegDecodeRead): the whole span, the file's bytes for decoderReadCap reads, then zeros; open()'s top-up at the
// header's end is one (past JPEG_OPEN_TOP_UPS of them in open(), a read gets a zeroed span and 0, so a parse that ran
// past the walked header ends). Without a handle (the open callback failed) a read is taken as at the walked size. `size` is JPEGFILE.iSize (the walked size, or the file's when it shrank) and `iPos` JPEGFILE.iPos, which
// JPEGDEC tracks itself (JPEGGetMoreData compares it with the size): it is kept in step with the file's position, held
// under the size (jpegPositionAfterRead), as the JPEG converter keeps it.
template <typename File>
int32_t answerJpegRead(File& file, DecoderReads& reads, uint8_t* buf, const int32_t len, const int32_t size,
                       int32_t& iPos) {
  const uint32_t position = file.isOpen() ? file.position() : reads.walkedSize;
  int32_t answer = 0;
  if (reads.jpegOpening && position < reads.headerEnd) {
    const bool overCap = ++reads.headerReads > jpegHeaderReadCap(reads.windowMoves);
    if (reads.headerFailed || overCap || !file.isOpen() || reads.seekFailed || file.stopped()) {
      reads.headerFailed = true;
      answer = answerJpegHeaderRead(buf, len, 0, true);
    } else {
      // The header parse: the file must give all it should, or the read is dropped.
      int32_t expected = 0;
      const int32_t got = readUnderAllowance(file, reads, position, buf, len, false, expected);
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
      got = readUnderAllowance(file, reads, position, buf, len, file.stopped(), allowed);
    }
    answer = answerJpegDecodeRead(buf, len, got);
  }
  iPos = jpegPositionAfterRead(file.isOpen() ? file.position() : reads.walkedSize,
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

// The task watchdog's quiet time (GrayImageReader.cpp's DecodeWatchdog): the time the task has spent outside file
// calls since its last progress (a row, or a read that gave file bytes). The task leaves the watchdog around each
// blocking file call (a wait on the card's mutex can be long and legitimate), and leaving and rejoining feed the
// watchdog's timer (ESP-IDF's task_wdt.c resets it whenever every other watched task has reset), so a decoder that
// spins making reads but no progress would never trip it: once the quiet time reaches the watchdog's timeout the task
// stops leaving, and the next file call is watched like the rest.
class WatchdogQuiet {
 public:
  explicit WatchdogQuiet(const uint32_t limitMs, const uint32_t nowMs) : limitMs_(limitMs), sinceMs_(nowMs) {}
  // A row, or a read that gave file bytes.
  void progress(const uint32_t nowMs) {
    quietMs_ = 0;
    sinceMs_ = nowMs;
  }
  // Before a blocking file call: whether the task may leave the watchdog for it.
  bool mayLeave(const uint32_t nowMs) {
    quietMs_ += nowMs - sinceMs_;
    sinceMs_ = nowMs;
    return quietMs_ < limitMs_;
  }
  // After the file call: its time is not quiet time.
  void back(const uint32_t nowMs) { sinceMs_ = nowMs; }
  uint32_t quietMs() const { return quietMs_; }

 private:
  uint32_t limitMs_;
  uint32_t quietMs_ = 0;
  uint32_t sinceMs_;
};

}  // namespace GameDisplayLab
