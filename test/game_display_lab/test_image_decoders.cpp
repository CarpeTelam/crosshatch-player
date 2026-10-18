// The gray viewer's second layer against a decoder that never returns (story dlab-g3): the real PNGdec 1.1.6 and
// JPEGDEC (at the firmware's pinned commit with scripts/jpegdec_patches applied), fed through the reader's read answers
// (GameDisplayLab::readAllowance, decoderReadCap, fillPngRead, fillJpegRead, jpegPositionAfterRead) exactly as
// GrayImageReader.cpp's callbacks give them, with every cut and a few appends of the sample pictures and a stop at
// every read, each decode bounded in reads and draws (a decode past the bound throws out of the callback and fails the
// test). The layout walk would refuse most of these files; here the decoders get them anyway, as when a file shrinks
// or changes after its walk, or a read fails, so the reads alone must end every decode. Built with AddressSanitizer
// and UndefinedBehaviorSanitizer, so a read outside the decoders' buffers fails the test too.
//
// Built only when the decoders' sources are there (test/game_display_lab/CMakeLists.txt: CROSSHATCH_PNGDEC_DIR,
// CROSSHATCH_JPEGDEC_DIR, by default PlatformIO's library folder; the fork's CI fetches both).

#include <gtest/gtest.h>

// As GrayImageReader.cpp: the two headers' byte-order macros differ, so JPEGDEC's are dropped before PNGdec.h.
#include <JPEGDEC.h>
#undef INTELSHORT
#undef INTELLONG
#undef MOTOSHORT
#undef MOTOLONG
#include <PNGdec.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "ImageRows.h"
#include "image_fixtures.h"

namespace {

using namespace GameDisplayLab;

// A decode that went on past its bound.
struct Spun {};

// The file behind the callbacks: its bytes, the size the walk would have given the decoder, the reader's position
// (the file's own, as HalFile's), the stop (asked by hand, at a read index, or at a draw), JPEGDEC's phase and reads
// as the reader keeps them, and the counts against their bounds.
struct FakeFile {
  // The file is the first `size` bytes of `source` (shared, so the cuts of a 66 KB sample copy nothing).
  std::shared_ptr<const std::vector<uint8_t>> source;
  size_t size = 0;
  uint32_t walked = 0;
  size_t pos = 0;
  bool stopped = false;
  bool seekFailed = false;
  int stopAtRead = -1;
  int stopAtDraw = -1;
  bool jpegDecoding = false;
  uint32_t jpegReads = 0;
  int reads = 0;
  int draws = 0;
  // The file shrank after a walk of the whole sample (its reads come up short: a failed read, as a pulled card's).
  bool shrunk = false;
  // PNG: rows drawn; JPEG: the bottom of the lowest block drawn.
  int rows = 0;
};

constexpr int MAX_READS = 4000;
constexpr int MAX_DRAWS = 4000;

FakeFile* current = nullptr;

void* openFake(const char*, int32_t* size) {
  current->pos = 0;
  *size = static_cast<int32_t>(current->walked);
  return current;
}

void closeFake(void*) {}

// The file's own read under the reader's allowance (GrayImageReader.cpp's readFile): what the file has at its
// position, at most what is allowed (`allowed`, under the stop when `honourStop`); the position moves by what was read.
int32_t readFake(FakeFile& f, uint8_t* buf, const int32_t len, const bool honourStop, int32_t& allowed) {
  if (++f.reads > MAX_READS) throw Spun{};
  if (f.reads == f.stopAtRead) f.stopped = true;
  allowed = 0;
  if (f.seekFailed) return 0;
  allowed = readAllowance(static_cast<uint32_t>(f.pos), len, f.walked, honourStop && f.stopped);
  int32_t got = 0;
  while (got < allowed && f.pos < f.size) {
    buf[got++] = (*f.source)[f.pos++];
  }
  return got;
}

int32_t pngRead(PNGFILE* pFile, uint8_t* buf, const int32_t len) {
  FakeFile& f = *static_cast<FakeFile*>(pFile->fHandle);
  int32_t allowed = 0;
  return fillPngRead(buf, len, readFake(f, buf, len, true, allowed));
}

int32_t pngSeek(PNGFILE* pFile, const int32_t pos) {
  FakeFile& f = *static_cast<FakeFile*>(pFile->fHandle);
  // HalFile refuses a seek past the end, as the reader then gives nothing until a seek succeeds.
  f.seekFailed = pos < 0 || static_cast<size_t>(pos) > f.size;
  if (f.seekFailed) return -1;
  f.pos = static_cast<size_t>(pos);
  return pos;
}

int pngDraw(PNGDRAW* pDraw) {
  FakeFile& f = *static_cast<FakeFile*>(pDraw->pUser);
  if (++f.draws > MAX_DRAWS) throw Spun{};
  if (f.draws == f.stopAtDraw) f.stopped = true;
  ++f.rows;
  return f.stopped ? 0 : 1;
}

// GrayImageReader.cpp's jpegRead: the file's bytes for decoderReadCap reads per phase (the header parse unstopped,
// keeping a read whole or not at all), the rest zero-filled, the header parse told what it kept and the decode the
// whole span, the position held under the size.
int32_t jpegRead(JPEGFILE* pFile, uint8_t* buf, const int32_t len) {
  FakeFile& f = *static_cast<FakeFile*>(pFile->fHandle);
  int32_t kept = 0;
  if (++f.jpegReads <= decoderReadCap(f.walked)) {
    int32_t allowed = 0;
    const int32_t got = readFake(f, buf, len, f.jpegDecoding, allowed);
    kept = jpegKeptBytes(allowed, got, f.jpegDecoding);
  } else if (++f.reads > MAX_READS) {
    throw Spun{};
  }
  const int32_t answer = fillJpegRead(buf, len, kept, f.jpegDecoding);
  pFile->iPos = jpegPositionAfterRead(static_cast<uint32_t>(f.pos), static_cast<uint32_t>(pFile->iSize));
  return answer;
}

// What the reader does not do: JPEGDEC given PNGdec's answer, the rest of the requested length as 0xFF.
int32_t jpegReadFilled(JPEGFILE* pFile, uint8_t* buf, const int32_t len) {
  FakeFile& f = *static_cast<FakeFile*>(pFile->fHandle);
  int32_t allowed = 0;
  const int32_t got = readFake(f, buf, len, true, allowed);
  pFile->iPos = static_cast<int32_t>(f.pos);
  return fillPngRead(buf, len, got);
}

// Nor this: what the file gave and the buffer otherwise untouched (the third batch's answer, and the converter's).
int32_t jpegReadStale(JPEGFILE* pFile, uint8_t* buf, const int32_t len) {
  FakeFile& f = *static_cast<FakeFile*>(pFile->fHandle);
  int32_t allowed = 0;
  const int32_t got = readFake(f, buf, len, true, allowed);
  pFile->iPos = static_cast<int32_t>(f.pos);
  return got;
}

int32_t jpegSeek(JPEGFILE* pFile, const int32_t pos) {
  FakeFile& f = *static_cast<FakeFile*>(pFile->fHandle);
  f.seekFailed = pos < 0 || static_cast<size_t>(pos) > f.size;
  if (f.seekFailed) return -1;
  f.pos = static_cast<size_t>(pos);
  pFile->iPos = jpegPositionAfterRead(static_cast<uint32_t>(pos), static_cast<uint32_t>(pFile->iSize));
  return pos;
}

int jpegDraw(JPEGDRAW* pDraw) {
  FakeFile& f = *static_cast<FakeFile*>(pDraw->pUser);
  if (++f.draws > MAX_DRAWS) throw Spun{};
  if (f.draws == f.stopAtDraw) f.stopped = true;
  if (pDraw->y + pDraw->iHeight > f.rows) f.rows = pDraw->y + pDraw->iHeight;
  return f.stopped ? 0 : 1;
}

// Opens and decodes `f` as the reader does; false when it spun. `result` is the decode's (or the open's) code.
bool decodePng(FakeFile& f, int& result) {
  auto png = std::make_unique<PNG>();
  current = &f;
  try {
    result = png->open("fake.png", openFake, closeFake, pngRead, pngSeek, pngDraw);
    if (result == PNG_SUCCESS) result = png->decode(&f, 0);
  } catch (const Spun&) {
    png->close();
    return false;
  }
  png->close();
  return true;
}

// `opened` says whether open() succeeded (and decode() ran).
bool decodeJpeg(FakeFile& f, const int option, int& result, JPEG_READ_CALLBACK* read = jpegRead,
                bool* opened = nullptr) {
  auto jpeg = std::make_unique<JPEGDEC>();
  current = &f;
  if (opened) *opened = false;
  try {
    f.jpegDecoding = false;
    f.jpegReads = 0;
    result = jpeg->open("fake.jpg", openFake, closeFake, read, jpegSeek, jpegDraw);
    if (result == 1) {
      if (opened) *opened = true;
      jpeg->setPixelType(EIGHT_BIT_GRAYSCALE);
      jpeg->setUserPointer(&f);
      f.jpegDecoding = true;
      f.jpegReads = 0;
      result = jpeg->decode(0, 0, option);
    }
  } catch (const Spun&) {
    jpeg->close();
    return false;
  }
  jpeg->close();
  return true;
}

template <size_t N>
std::vector<uint8_t> bytes(const uint8_t (&a)[N]) {
  return std::vector<uint8_t>(a, a + N);
}

FakeFile whole(const std::vector<uint8_t>& sample) {
  FakeFile f;
  f.source = std::make_shared<const std::vector<uint8_t>>(sample);
  f.size = sample.size();
  f.walked = static_cast<uint32_t>(sample.size());
  return f;
}

// The sample cut at every length, as walked (the decoder told the cut size) and as shrunk after a walk of the whole
// (told the whole size, given the cut), and with a few things appended.
std::vector<FakeFile> cutsAndAppends(const std::vector<uint8_t>& sample) {
  std::vector<FakeFile> files;
  files.reserve(2 * sample.size() + 8);
  const auto source = std::make_shared<const std::vector<uint8_t>>(sample);
  for (size_t cut = 0; cut <= sample.size(); ++cut) {
    FakeFile walked;
    walked.source = source;
    walked.size = cut;
    walked.walked = static_cast<uint32_t>(cut);
    files.push_back(walked);
    FakeFile shrunk = walked;
    shrunk.walked = static_cast<uint32_t>(sample.size());
    shrunk.shrunk = true;
    files.push_back(std::move(shrunk));
  }
  const std::vector<std::vector<uint8_t>> tails = {std::vector<uint8_t>(7, 0),
                                                   std::vector<uint8_t>(10, 0),
                                                   std::vector<uint8_t>(300, 0xFF),
                                                   {0x00, 0x00, 0x00, 0x04, 'I', 'D', 'A', 'T', 1, 2, 3, 4, 0, 0, 0, 0},
                                                   sample};
  for (const auto& tail : tails) {
    std::vector<uint8_t> longer = sample;
    longer.insert(longer.end(), tail.begin(), tail.end());
    files.push_back(whole(longer));
  }
  return files;
}

// Every decode of `sample` with the stop asked at read 1, 2, ... one past the reads a whole decode makes: each
// returns, and reads nothing of the file after the stop.
template <typename Decode>
void stopAtEveryRead(const std::vector<uint8_t>& sample, Decode&& decode) {
  FakeFile full = whole(sample);
  ASSERT_TRUE(decode(full));
  const int reads = full.reads;
  ASSERT_GT(reads, 1);
  for (int at = 1; at <= reads + 1; ++at) {
    FakeFile f = whole(sample);
    f.stopAtRead = at;
    EXPECT_TRUE(decode(f)) << "stop at read " << at << " of " << reads;
  }
}

TEST(ImageDecoders, PngdecReturnsOnEveryCutAndAppend) {
  // PNG_NOISE's IDAT (3,131 bytes) crosses PNGdec's 2 KiB file buffer, so its cuts land in a refill too.
  for (const auto& sample : {bytes(PNG_GRAY), bytes(PNG_PALETTE), bytes(PNG_NOISE)}) {
    for (FakeFile& f : cutsAndAppends(sample)) {
      int result = 0;
      EXPECT_TRUE(decodePng(f, result)) << "size " << f.size << " walked " << f.walked;
    }
  }
}

TEST(ImageDecoders, PngdecDecodesTheWholeSamples) {
  for (const auto& [sample, height] :
       {std::pair{bytes(PNG_GRAY), 16}, std::pair{bytes(PNG_PALETTE), 12}, std::pair{bytes(PNG_NOISE), 48}}) {
    FakeFile f = whole(sample);
    int result = -1;
    ASSERT_TRUE(decodePng(f, result));
    EXPECT_EQ(result, PNG_SUCCESS);
    EXPECT_EQ(f.rows, height);
  }
}

TEST(ImageDecoders, PngdecReturnsAtAStopOnEveryRead) {
  for (const auto& sample : {bytes(PNG_GRAY), bytes(PNG_PALETTE), bytes(PNG_NOISE)}) {
    stopAtEveryRead(sample, [](FakeFile& f) {
      int result = 0;
      return decodePng(f, result);
    });
  }
}

constexpr int JPEG_OPTIONS[] = {0, JPEG_SCALE_HALF, JPEG_SCALE_QUARTER, JPEG_SCALE_EIGHTH};

// The reader decodes a JPEG only once the walk has found it whole: a cut or appended file as its walk sees it, a
// shrunk one as the whole sample its walk saw. JPEGDEC's header parse cannot be made safe for a header the walk refuses
// (a DQT whose count wraps reads on past its object, jpeg.inl 1732-1757: the walk is what refuses it).
bool walkedWhole(const FakeFile& f, const std::vector<uint8_t>& sample) {
  const uint8_t* const seen = f.shrunk ? sample.data() : f.source->data();
  const size_t seenSize = f.shrunk ? sample.size() : f.size;
  const auto readAt = [seen, seenSize](const uint32_t offset, uint8_t* out, const size_t n) {
    if (offset + n > seenSize) return false;
    std::copy(seen + offset, seen + offset + n, out);
    return true;
  };
  return walkJpegSegments(f.shrunk ? static_cast<uint32_t>(sample.size()) : f.walked, readAt, [] { return false; }) ==
         FileLayout::Complete;
}

TEST(ImageDecoders, JpegdecReturnsOnEveryCutAndAppend) {
  for (const auto& sample : {bytes(JPEG_BASELINE), bytes(JPEG_PROGRESSIVE)}) {
    int decoded = 0;
    for (FakeFile& f : cutsAndAppends(sample)) {
      if (!walkedWhole(f, sample)) continue;
      ++decoded;
      for (const int option : JPEG_OPTIONS) {
        FakeFile run = f;
        int result = 0;
        bool opened = false;
        EXPECT_TRUE(decodeJpeg(run, option, result, jpegRead, &opened))
            << "size " << f.size << " walked " << f.walked << " option " << option;
        if (!opened) break;  // the scale only matters to decode()
      }
    }
    // Every shrunk cut (a read failure anywhere), and the whole file.
    EXPECT_GT(decoded, static_cast<int>(sample.size()));
  }
}

TEST(ImageDecoders, ACameraShapedJpegReturnsOnEveryCut) {
  // Over 64 KB with an APP1 past JPEGDEC's 2 KiB buffer: its header parse seeks past it, and COM segments seek again.
  const std::vector<uint8_t> big = bigExifJpeg();
  for (FakeFile& f : cutsAndAppends(big)) {
    if (!walkedWhole(f, big)) continue;
    int result = 0;
    bool opened = false;
    EXPECT_TRUE(decodeJpeg(f, 0, result, jpegRead, &opened)) << "size " << f.size << " walked " << f.walked;
  }
}

TEST(ImageDecoders, JpegdecDecodesTheWholeSamples) {
  // A progressive JPEG is decoded at an eighth, as the reader decodes one (its DC-only decode): 40 x 24 is 5 x 3.
  struct Sample {
    std::vector<uint8_t> file;
    int option;
    int height;
  };
  for (const Sample& sample : {Sample{bytes(JPEG_BASELINE), 0, 24}, Sample{bytes(JPEG_BASELINE), JPEG_SCALE_HALF, 12},
                               Sample{bytes(JPEG_BASELINE), JPEG_SCALE_QUARTER, 6},
                               Sample{bytes(JPEG_PROGRESSIVE), JPEG_SCALE_EIGHTH, 3}, Sample{bigExifJpeg(), 0, 24}}) {
    FakeFile f = whole(sample.file);
    int result = -1;
    ASSERT_TRUE(decodeJpeg(f, sample.option, result));
    EXPECT_EQ(result, 1);
    EXPECT_EQ(f.rows, sample.height);
  }
}

TEST(ImageDecoders, JpegdecReturnsAtAStopOnEveryRead) {
  for (const auto& sample : {bytes(JPEG_BASELINE), bytes(JPEG_PROGRESSIVE), bigExifJpeg()}) {
    stopAtEveryRead(sample, [](FakeFile& f) {
      int result = 0;
      return decodeJpeg(f, 0, result);
    });
  }
}

TEST(ImageDecoders, JpegdecIsGivenZerosBecauseFillerOrAStaleBufferWouldSpinItsHeaderParse) {
  // A stop at the read after the APP1 (the seek past the 2 KiB buffer): given nothing and its buffer left as it was,
  // JPEGParseInfo reads SOI again and the APP1 marker as a length, seeks, and goes round for ever (jpeg.inl 1611-1636).
  {
    FakeFile stale = whole(bigExifJpeg());
    stale.stopAtRead = 2;
    int result = 0;
    EXPECT_FALSE(decodeJpeg(stale, 0, result, jpegReadStale));
    EXPECT_GT(stale.reads, MAX_READS);
  }
  // A 3,000-byte file with no SOS (SOI, an APP0 of 1500 bytes, zeros), given whole: once its bytes run out, 0xFF
  // filler keeps the same loop reading (it compares a buffer offset, kept under 1 KiB by each refill, with the
  // 3,000-byte size; 0xFFFF is stepped over, jpeg.inl 1642).
  std::vector<uint8_t> noSos = {0xFF, 0xD8, 0xFF, 0xE0, 0x05, 0xDC};
  noSos.resize(3000, 0x00);
  FakeFile f = whole(noSos);
  int result = 0;
  FakeFile filled = f;
  EXPECT_FALSE(decodeJpeg(filled, 0, result, jpegReadFilled));
  EXPECT_GT(filled.reads, MAX_READS);
  // The reader's answer ends both: zeros are stepped over until the offset passes the bytes read (jpeg.inl 1623); and
  // the stop ends the decode, not the header parse, which reads the walked header whole.
  EXPECT_TRUE(decodeJpeg(f, 0, result));
  EXPECT_NE(result, 1);
  FakeFile stopped = whole(bigExifJpeg());
  stopped.stopAtRead = 2;
  bool opened = false;
  EXPECT_TRUE(decodeJpeg(stopped, 0, result, jpegRead, &opened));
  EXPECT_TRUE(opened);
  EXPECT_LE(stopped.draws, 1);
  // A read that fails at the seek past the APP1 (the file shrank there): its partial bytes dropped, the parse meets
  // zeros at the buffer's start and errs.
  FakeFile failed = whole(bigExifJpeg());
  failed.size = 5000;
  EXPECT_TRUE(decodeJpeg(failed, 0, result, jpegRead, &opened));
  EXPECT_FALSE(opened);
}

TEST(ImageDecoders, JpegdecOpensEveryFileTheWalkCallsComplete) {
  // The walk visits the offsets JPEGParseInfo visits: where it finds a whole file, JPEGDEC parses its header (in a
  // file of 256 bytes or more: JPEGDEC refuses a smaller one outright, jpeg.inl 1598).
  for (const auto& sample : {bytes(JPEG_BASELINE), bytes(JPEG_PROGRESSIVE), bigExifJpeg()}) {
    const auto readAt = [&sample](const uint32_t offset, uint8_t* out, const size_t n) {
      if (offset + n > sample.size()) return false;
      std::copy(sample.begin() + offset, sample.begin() + offset + static_cast<long>(n), out);
      return true;
    };
    ASSERT_EQ(walkJpegSegments(static_cast<uint32_t>(sample.size()), readAt, [] { return false; }),
              FileLayout::Complete);
    FakeFile f = whole(sample);
    auto jpeg = std::make_unique<JPEGDEC>();
    current = &f;
    EXPECT_EQ(jpeg->open("fake.jpg", openFake, closeFake, jpegRead, jpegSeek, jpegDraw), 1);
    jpeg->close();
  }
}

TEST(ImageDecoders, AStopEndsEitherDecodeAndItsReads) {
  FakeFile png = whole(bytes(PNG_GRAY));
  png.stopAtDraw = 3;
  int result = 0;
  ASSERT_TRUE(decodePng(png, result));
  EXPECT_EQ(result, PNG_QUIT_EARLY);
  EXPECT_EQ(png.draws, 3);
  FakeFile jpeg = whole(bytes(JPEG_BASELINE));
  jpeg.stopAtDraw = 1;
  ASSERT_TRUE(decodeJpeg(jpeg, 0, result));
  // JPEGDEC reports success when its draw callback ends the decode (jpeg.inl 5342, 5373: only a decode error is a
  // failure), so the reader checks its own stop before the result.
  EXPECT_EQ(result, 1);
  EXPECT_EQ(jpeg.draws, 1);
  // Stopped before it starts: PNGdec gets nothing of the file and draws nothing; JPEGDEC parses the walked header (its
  // parse is not stopped) and its first draw ends the decode. Both return.
  FakeFile stoppedPng = whole(bytes(PNG_GRAY));
  stoppedPng.stopped = true;
  EXPECT_TRUE(decodePng(stoppedPng, result));
  EXPECT_EQ(stoppedPng.draws, 0);
  for (const auto& sample : {bytes(JPEG_BASELINE), bigExifJpeg()}) {
    FakeFile f = whole(sample);
    f.stopped = true;
    EXPECT_TRUE(decodeJpeg(f, 0, result));
    EXPECT_LE(f.draws, 1);
  }
}

}  // namespace
