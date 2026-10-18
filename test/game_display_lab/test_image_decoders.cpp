// The gray viewer's layers against the real decoders (story dlab-g3): PNGdec 1.1.6 and JPEGDEC (at the firmware's
// pinned commit with scripts/jpegdec_patches applied, its bit reader 32 bits wide as on the device:
// jpegdec_as_device.h), fed through the reader's walks and read answers (GameDisplayLab::walkPngChunks,
// walkJpegSegments, readAllowance, the read caps, fillPngRead, answerJpegHeaderRead, answerJpegDecodeRead,
// jpegPositionAfterRead) exactly as GrayImageReader.cpp's callbacks give them, with every cut and a few appends of the
// sample pictures, a stop at every read, every synthetic header the walk passes, and single-byte changes to a real
// header, each decode bounded in reads and draws (a decode past the bound throws out of the callback and fails the
// test). Built with AddressSanitizer and UndefinedBehaviorSanitizer; what they cannot see inside a decoder's object is
// watched by canaries: JPEGDEC's bit pointer stays inside its 2 KiB buffer at every strip and after the decode, its
// Huffman tables after that buffer are untouched by the decode, and the part of PNGdec's row buffer after its palette
// that no row uses is untouched.
//
// Built only when the decoders' sources are there (test/game_display_lab/CMakeLists.txt: CROSSHATCH_PNGDEC_DIR,
// CROSSHATCH_JPEGDEC_DIR, by default PlatformIO's library folder; the fork's CI fetches both).

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "ImageRows.h"
#include "image_fixtures.h"

// The decoders' objects' fields, open to the canaries (the headers' own types and declarations are unchanged).
#define private public
// As GrayImageReader.cpp: the two headers' byte-order macros differ, so JPEGDEC's are dropped before PNGdec.h.
#include "jpegdec_as_device.h"
#undef INTELSHORT
#undef INTELLONG
#undef MOTOSHORT
#undef MOTOLONG
#include <PNGdec.h>
#undef private

namespace {

using namespace GameDisplayLab;

// A decode that went on past its bound.
struct Spun {};

// The file behind the callbacks: the first `size` bytes of `source`, what its walk found (the walked size, a JPEG's
// header end and buffer moves), the reader's position (the file's own, as HalFile's), the stop (asked by hand, at a
// read index, or at a draw), the reader's JPEG state, the counts against their bounds, and the canaries.
struct FakeFile {
  std::shared_ptr<const std::vector<uint8_t>> source;
  size_t size = 0;
  uint32_t walked = 0;
  uint32_t headerEnd = 0;
  uint32_t windowMoves = 0;
  // The file shrank after a walk of the whole sample (its reads come up short: a failed read, as a pulled card's).
  bool shrunk = false;
  size_t pos = 0;
  bool stopped = false;
  bool seekFailed = false;
  int stopAtRead = -1;
  int stopAtDraw = -1;
  bool jpegOpening = false;
  uint32_t openReads = 0;
  uint32_t decodeReads = 0;
  bool headerFailed = false;
  int reads = 0;
  int decodeModeReads = 0;
  int draws = 0;
  // PNG: rows drawn; JPEG: the bottom of the lowest block drawn.
  int rows = 0;
  // The JPEGDEC object, for the bit-pointer canary at each strip.
  const JPEGDEC* jpeg = nullptr;
  bool pointerLeft = false;
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

// One read, counted against the bound; the stop asked at its index.
void countRead(FakeFile& f) {
  if (++f.reads > MAX_READS) throw Spun{};
  if (f.reads == f.stopAtRead) f.stopped = true;
}

// The file's own read under the reader's allowance (GrayImageReader.cpp's readFile): what the file has at its
// position, at most what is allowed (`allowed`, under the stop when `honourStop`); the position moves by what was read.
int32_t readFake(FakeFile& f, uint8_t* buf, const int32_t len, const bool honourStop, int32_t& allowed) {
  allowed = 0;
  if (f.seekFailed) return 0;
  allowed = readAllowance(static_cast<uint32_t>(f.pos), len, f.walked, honourStop && f.stopped);
  int32_t got = 0;
  while (got < allowed && f.pos < f.size) buf[got++] = (*f.source)[f.pos++];
  return got;
}

int32_t pngRead(PNGFILE* pFile, uint8_t* buf, const int32_t len) {
  FakeFile& f = *static_cast<FakeFile*>(pFile->fHandle);
  countRead(f);
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

// GrayImageReader.cpp's jpegRead: a read starting before the walked header's end is the header parse's (kept whole or
// dropped: a stop, a short read, a refused seek, past the open's cap, latched), from there on the decode's (the whole
// span, the file's bytes for decoderReadCap reads), the position held under the size.
int32_t jpegRead(JPEGFILE* pFile, uint8_t* buf, const int32_t len) {
  FakeFile& f = *static_cast<FakeFile*>(pFile->fHandle);
  countRead(f);
  const auto position = static_cast<uint32_t>(f.pos);
  int32_t answer = 0;
  const bool overOpenCap = f.jpegOpening && ++f.openReads > jpegHeaderReadCap(f.windowMoves);
  if (f.headerFailed || overOpenCap || (position < f.headerEnd && (f.seekFailed || f.stopped))) {
    f.headerFailed = true;
    answer = answerJpegHeaderRead(buf, len, 0, true);
  } else if (position < f.headerEnd) {
    int32_t expected = 0;
    const int32_t got = readFake(f, buf, len, false, expected);
    const bool dropped = got < expected;
    if (dropped) f.headerFailed = true;
    answer = answerJpegHeaderRead(buf, len, got, dropped);
  } else {
    ++f.decodeModeReads;
    int32_t got = 0;
    if (f.jpegOpening || ++f.decodeReads <= decoderReadCap(f.walked)) {
      int32_t allowed = 0;
      got = readFake(f, buf, len, true, allowed);
    }
    answer = answerJpegDecodeRead(buf, len, got);
  }
  pFile->iPos = jpegPositionAfterRead(static_cast<uint32_t>(f.pos), static_cast<uint32_t>(pFile->iSize));
  return answer;
}

// What the reader does not do: JPEGDEC given PNGdec's answer, the rest of the requested length as 0xFF.
int32_t jpegReadFilled(JPEGFILE* pFile, uint8_t* buf, const int32_t len) {
  FakeFile& f = *static_cast<FakeFile*>(pFile->fHandle);
  countRead(f);
  int32_t allowed = 0;
  const int32_t got = readFake(f, buf, len, true, allowed);
  pFile->iPos = static_cast<int32_t>(f.pos);
  return fillPngRead(buf, len, got);
}

// Nor this: what the file gave and the buffer otherwise untouched (the third batch's answer, and the converter's).
int32_t jpegReadStale(JPEGFILE* pFile, uint8_t* buf, const int32_t len) {
  FakeFile& f = *static_cast<FakeFile*>(pFile->fHandle);
  countRead(f);
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

// The bit-pointer canary: JPEGDEC's VLC offset and bit pointer inside its 2 KiB buffer.
bool pointerInside(const JPEGDEC& jpeg) {
  const JPEGIMAGE& image = jpeg._jpeg;
  const ptrdiff_t at = image.bb.pBuf - image.ucFileBuf;
  return image.iVLCOff >= 0 && image.iVLCOff <= JPEG_FILE_BUF_SIZE && at >= 0 && at <= JPEG_FILE_BUF_SIZE;
}

int jpegDraw(JPEGDRAW* pDraw) {
  FakeFile& f = *static_cast<FakeFile*>(pDraw->pUser);
  if (++f.draws > MAX_DRAWS) throw Spun{};
  if (f.jpeg && !pointerInside(*f.jpeg)) f.pointerLeft = true;
  if (f.draws == f.stopAtDraw) f.stopped = true;
  if (pDraw->y + pDraw->iHeight > f.rows) f.rows = pDraw->y + pDraw->iHeight;
  return f.stopped ? 0 : 1;
}

// The row-buffer canary: PNGdec's rows use the start of ucPixels (two lines and their 16-byte alignment, under 200
// bytes for the samples here, at most 64 px of 8 bits); past this offset, up to the fast palette's 512 bytes at its
// end, nothing of a decode writes, but a palette copied past ucPalette's 1,024 bytes runs straight into it.
constexpr size_t PNG_CANARY_FROM = 256;
constexpr uint8_t CANARY = 0xA5;

// Opens and decodes `f` as the reader does; false when it spun. `result` is the decode's (or the open's) code;
// `canaryHeld`, when given, whether the row-buffer canary held.
bool decodePng(FakeFile& f, int& result, bool* canaryHeld = nullptr) {
  auto png = std::make_unique<PNG>();
  current = &f;
  bool spun = false;
  bool held = true;
  try {
    result = png->open("fake.png", openFake, closeFake, pngRead, pngSeek, pngDraw);
    if (result == PNG_SUCCESS) {
      uint8_t* const pixels = png->_png.ucPixels;
      std::memset(pixels + PNG_CANARY_FROM, CANARY, sizeof(png->_png.ucPixels) - PNG_CANARY_FROM - 512);
      result = png->decode(&f, 0);
      for (size_t i = PNG_CANARY_FROM; i < sizeof(png->_png.ucPixels) - 512; ++i) held = held && pixels[i] == CANARY;
    }
  } catch (const Spun&) {
    spun = true;
  }
  png->close();
  if (canaryHeld) {
    *canaryHeld = held;
  } else {
    EXPECT_TRUE(held) << "PNGdec wrote past its palette into its row buffer";
  }
  return !spun;
}

// `opened` says whether open() succeeded and the size passed (and decode() ran). The canaries are checked here: the bit
// pointer at every strip and after the decode, and the Huffman tables after the 2 KiB buffer unchanged by the decode.
bool decodeJpeg(FakeFile& f, const int option, int& result, JPEG_READ_CALLBACK* read = jpegRead,
                bool* opened = nullptr) {
  auto jpeg = std::make_unique<JPEGDEC>();
  current = &f;
  f.jpeg = jpeg.get();
  if (opened) *opened = false;
  bool spun = false;
  try {
    f.jpegOpening = true;
    f.openReads = 0;
    f.headerFailed = false;
    result = jpeg->open("fake.jpg", openFake, closeFake, read, jpegSeek, jpegDraw);
    f.jpegOpening = false;
    // As the reader: a size outside the converters' limits is refused before decode() (a corrupt SOF can claim 65,535
    // rows).
    if (result == 1 && !imageSizeAccepted(jpeg->getWidth(), jpeg->getHeight())) result = -1;
    if (result == 1) {
      if (opened) *opened = true;
      jpeg->setPixelType(EIGHT_BIT_GRAYSCALE);
      jpeg->setUserPointer(&f);
      std::vector<uint8_t> tables(sizeof(jpeg->_jpeg.ucHuffDC) + sizeof(jpeg->_jpeg.usHuffAC));
      std::memcpy(tables.data(), jpeg->_jpeg.ucHuffDC, sizeof(jpeg->_jpeg.ucHuffDC));
      std::memcpy(tables.data() + sizeof(jpeg->_jpeg.ucHuffDC), jpeg->_jpeg.usHuffAC, sizeof(jpeg->_jpeg.usHuffAC));
      f.decodeReads = 0;
      result = jpeg->decode(0, 0, option);
      if (!pointerInside(*jpeg)) f.pointerLeft = true;
      EXPECT_EQ(std::memcmp(tables.data(), jpeg->_jpeg.ucHuffDC, sizeof(jpeg->_jpeg.ucHuffDC)), 0)
          << "JPEGDEC's decode wrote past its buffer into its DC tables";
      EXPECT_EQ(
          std::memcmp(tables.data() + sizeof(jpeg->_jpeg.ucHuffDC), jpeg->_jpeg.usHuffAC, sizeof(jpeg->_jpeg.usHuffAC)),
          0)
          << "JPEGDEC's decode wrote past its buffer into its AC tables";
    }
  } catch (const Spun&) {
    spun = true;
  }
  jpeg->close();
  f.jpeg = nullptr;
  EXPECT_FALSE(f.pointerLeft) << "JPEGDEC's bit pointer left its 2 KiB buffer";
  return !spun;
}

template <size_t N>
std::vector<uint8_t> bytes(const uint8_t (&a)[N]) {
  return std::vector<uint8_t>(a, a + N);
}

// The walk of the first `size` bytes of `bytes`, as the reader walks a file; its extent into `f`. True when Complete.
bool walkInto(FakeFile& f, const std::vector<uint8_t>& bytes, const size_t size, const bool png) {
  const auto readAt = [&bytes, size](const uint32_t offset, uint8_t* out, const size_t n) {
    if (offset + n > size) return false;
    std::copy(bytes.begin() + offset, bytes.begin() + offset + static_cast<long>(n), out);
    return true;
  };
  WalkExtent extent;
  const auto size32 = static_cast<uint32_t>(size);
  const FileLayout layout = png ? walkPngChunks(
                                      size32, readAt, [] { return false; }, &extent)
                                : walkJpegSegments(size32, readAt, [] { return false; }, &extent);
  if (layout != FileLayout::Complete) return false;
  f.walked = extent.end;
  f.headerEnd = extent.headerEnd;
  f.windowMoves = extent.windowMoves;
  return true;
}

// `sample` whole, as the reader gives it after its walk (or, for a file the walk refuses, the whole file as walked).
FakeFile whole(const std::vector<uint8_t>& sample) {
  FakeFile f;
  f.source = std::make_shared<const std::vector<uint8_t>>(sample);
  f.size = sample.size();
  f.walked = static_cast<uint32_t>(sample.size());
  const bool png = !sample.empty() && sample[0] == 0x89;
  walkInto(f, sample, sample.size(), png);
  return f;
}

// The sample cut at every length, as walked (the decoder told the cut's walk) and as shrunk after a walk of the
// whole (told the whole's walk, given the cut: a read that fails there), and with a few things appended. `walkedOnly`:
// only the files the walk passes, as the reader decodes only those; otherwise a file the walk refuses is given whole,
// to test the reads alone.
std::vector<FakeFile> cutsAndAppends(const std::vector<uint8_t>& sample, const bool walkedOnly) {
  const bool png = sample[0] == 0x89;
  std::vector<FakeFile> files;
  files.reserve(2 * sample.size() + 8);
  const auto source = std::make_shared<const std::vector<uint8_t>>(sample);
  FakeFile wholeSample;
  const bool sampleWalks = walkInto(wholeSample, sample, sample.size(), png);
  for (size_t cut = 0; cut <= sample.size(); ++cut) {
    FakeFile walked;
    walked.source = source;
    walked.size = cut;
    walked.walked = static_cast<uint32_t>(cut);
    if (walkInto(walked, sample, cut, png) || !walkedOnly) files.push_back(walked);
    if (sampleWalks) {
      FakeFile shrunk = wholeSample;
      shrunk.source = source;
      shrunk.size = cut;
      shrunk.shrunk = true;
      files.push_back(std::move(shrunk));
    }
  }
  const std::vector<std::vector<uint8_t>> tails = {std::vector<uint8_t>(7, 0),
                                                   std::vector<uint8_t>(10, 0),
                                                   std::vector<uint8_t>(300, 0xFF),
                                                   {0x00, 0x00, 0x00, 0x04, 'I', 'D', 'A', 'T', 1, 2, 3, 4, 0, 0, 0, 0},
                                                   sample};
  for (const auto& tail : tails) {
    std::vector<uint8_t> longer = sample;
    longer.insert(longer.end(), tail.begin(), tail.end());
    FakeFile appended = whole(longer);
    if (!walkedOnly || walkInto(appended, longer, longer.size(), png)) files.push_back(std::move(appended));
  }
  return files;
}

// Every decode of `sample` with the stop asked at read 1, 2, ... one past the reads a whole decode makes: each
// returns; a stop during JPEGDEC's header parse ends open() within a few reads.
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

// ---- PNG -----------------------------------------------------------------------------------------------------------

TEST(ImageDecoders, PngdecReturnsOnEveryCutAndAppend) {
  // PNG_NOISE's IDAT (3,131 bytes) crosses PNGdec's 2 KiB file buffer, so its cuts land in a refill too. Files the
  // walk refuses are given whole as well: the reads alone must end them.
  for (const auto& sample : {bytes(PNG_GRAY), bytes(PNG_PALETTE), bytes(PNG_NOISE)}) {
    for (FakeFile& f : cutsAndAppends(sample, false)) {
      int result = 0;
      EXPECT_TRUE(decodePng(f, result)) << "size " << f.size << " walked " << f.walked;
    }
  }
}

TEST(ImageDecoders, PngdecDecodesTheWholeSamples) {
  for (const auto& [sample, height] :
       {std::pair{bytes(PNG_GRAY), 16}, std::pair{bytes(PNG_PALETTE), 12}, std::pair{bytes(PNG_NOISE), 48}}) {
    FakeFile f = whole(sample);
    EXPECT_EQ(f.walked, sample.size());
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

TEST(ImageDecoders, APaletteAfterIendNeverReachesPngdec) {
  // The review's file: a short zlib stream, IEND, then a PLTE of 1,500 bytes. PNGdec has no IEND case: given the
  // whole file it parses the PLTE and copies it into its 1,024-byte palette and on into its row buffer (inside its
  // object, where AddressSanitizer cannot see it; the canary does. A PLTE longer than PNGdec's 2 KiB file buffer
  // would also be read from past its object, which AddressSanitizer stops the test binary at).
  const std::vector<uint8_t> review = fixtures::pngPaletteAfterIend(1500);
  FakeFile walked = whole(review);
  ASSERT_EQ(walked.walked, 33u + 24u + 12u);  // the walked size ends at IEND
  int result = 0;
  bool held = false;
  ASSERT_TRUE(decodePng(walked, result, &held));
  EXPECT_TRUE(held);
  EXPECT_NE(result, PNG_SUCCESS);
  // The fourth batch's reader, which gave the whole file.
  FakeFile unwalked = whole(review);
  unwalked.walked = static_cast<uint32_t>(review.size());
  ASSERT_TRUE(decodePng(unwalked, result, &held));
  EXPECT_FALSE(held);
}

// ---- JPEG ----------------------------------------------------------------------------------------------------------

constexpr int JPEG_OPTIONS[] = {0, JPEG_SCALE_HALF, JPEG_SCALE_QUARTER, JPEG_SCALE_EIGHTH};

TEST(ImageDecoders, JpegdecReturnsOnEveryCutAndAppend) {
  // The reader decodes a JPEG only once the walk has found it whole (JPEGDEC's header parse cannot be made safe for a
  // header the walk refuses): a cut or appended file as its walk sees it, a shrunk one (a failed read) as the whole.
  for (const auto& sample : {bytes(JPEG_BASELINE), bytes(JPEG_PROGRESSIVE)}) {
    int decoded = 0;
    for (FakeFile& f : cutsAndAppends(sample, true)) {
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
    EXPECT_GT(decoded, static_cast<int>(sample.size()));  // every shrunk cut, and the whole file
  }
}

TEST(ImageDecoders, NoiseJpegsReadPastTheirBufferAndReturnOnEveryCut) {
  // 128 x 128 noise: scans of about 7.5 KB, past JPEGDEC's 2 KiB buffer, so a decode reads.
  for (const auto& sample : {bytes(JPEG_NOISE_BASELINE), bytes(JPEG_NOISE_PROGRESSIVE)}) {
    FakeFile f = whole(sample);
    int result = -1;
    ASSERT_TRUE(decodeJpeg(f, 0, result));
    EXPECT_EQ(result, 1);
    EXPECT_GT(f.decodeModeReads, 0);
    for (FakeFile& cut : cutsAndAppends(sample, true)) {
      int r = 0;
      EXPECT_TRUE(decodeJpeg(cut, 0, r)) << "size " << cut.size << " walked " << cut.walked;
    }
  }
}

TEST(ImageDecoders, AnErasedSectorInTheScanIsTamed) {
  // 4 KB of 0xFF in the middle of a scan, as an erased flash sector reads: JPEGFilter drops 0xFF pairs as markers, so
  // unanswered the refills would add nothing and the bit pointer would run off the buffer.
  std::vector<uint8_t> erased = bytes(JPEG_NOISE_BASELINE);
  std::fill(erased.begin() + 2000, erased.begin() + 6000, 0xFF);
  FakeFile f = whole(erased);
  ASSERT_EQ(f.walked, erased.size());
  for (const int option : JPEG_OPTIONS) {
    FakeFile run = f;
    int result = 0;
    EXPECT_TRUE(decodeJpeg(run, option, result)) << option;
    EXPECT_GT(run.decodeModeReads, 0);
  }
  // A whole scan of 0xFF.
  std::vector<uint8_t> blank = bytes(JPEG_NOISE_BASELINE);
  std::fill(blank.begin() + 330, blank.end() - 2, 0xFF);
  FakeFile b = whole(blank);
  int result = 0;
  EXPECT_TRUE(decodeJpeg(b, 0, result));
}

TEST(ImageDecoders, ACameraShapedJpegReturnsOnEveryCut) {
  // Over 64 KB with an APP1 past JPEGDEC's 2 KiB buffer: its header parse seeks past it, and COM segments seek again.
  const std::vector<uint8_t> big = bigExifJpeg();
  for (FakeFile& f : cutsAndAppends(big, true)) {
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
  for (const Sample& sample :
       {Sample{bytes(JPEG_BASELINE), 0, 24}, Sample{bytes(JPEG_BASELINE), JPEG_SCALE_HALF, 12},
        Sample{bytes(JPEG_BASELINE), JPEG_SCALE_QUARTER, 6}, Sample{bytes(JPEG_PROGRESSIVE), JPEG_SCALE_EIGHTH, 3},
        Sample{bigExifJpeg(), 0, 24}, Sample{bytes(JPEG_NOISE_BASELINE), 0, 128},
        Sample{bytes(JPEG_NOISE_PROGRESSIVE), JPEG_SCALE_EIGHTH, 16}}) {
    FakeFile f = whole(sample.file);
    int result = -1;
    ASSERT_TRUE(decodeJpeg(f, sample.option, result));
    EXPECT_EQ(result, 1);
    EXPECT_EQ(f.rows, sample.height);
    EXPECT_GT(f.decodeModeReads, 0);  // open()'s last top-up at least is the decode's
  }
}

TEST(ImageDecoders, JpegdecReturnsAtAStopOnEveryRead) {
  for (const auto& sample : {bytes(JPEG_BASELINE), bytes(JPEG_PROGRESSIVE), bigExifJpeg(), bytes(JPEG_NOISE_BASELINE),
                             bytes(JPEG_NOISE_PROGRESSIVE)}) {
    stopAtEveryRead(sample, [](FakeFile& f) {
      int result = 0;
      return decodeJpeg(f, 0, result);
    });
  }
  // A stop during the header parse drops its read and ends open() within a few reads.
  const std::vector<uint8_t> big = bigExifJpeg();
  FakeFile full = whole(big);
  int result = 0;
  bool opened = false;
  ASSERT_TRUE(decodeJpeg(full, 0, result, jpegRead, &opened));
  for (int at = 1; at <= 4; ++at) {
    FakeFile f = whole(big);
    f.stopAtRead = at;
    ASSERT_TRUE(decodeJpeg(f, 0, result, jpegRead, &opened));
    EXPECT_FALSE(opened) << at;
    EXPECT_LE(f.reads, at + 2) << at;
  }
}

TEST(ImageDecoders, AFailedRefillInsideAHeaderSegmentErrsCleanly) {
  // Two 16-bit DQT tables whose marker and length are in JPEGParseInfo's buffer when it moves down to them, and whose
  // tables come in the refill after (they straddle the file's 2 KiB mark); and the same tables 1,100 bytes in, wholly
  // in the buffer after its move. The file fails at every byte around: the dropped read zeroes the whole buffer, the
  // bytes already in it too, and the parse errs. Zeros in the refill's span alone would leave the DQT's real length
  // over zero tables read as 8-bit ones (65 bytes, not 129): its 16-bit count wraps and it reads past its object.
  for (const auto& [name, from, to] : {std::tuple{"DQT two 16-bit tables across the 2 KiB mark", 1990, 2400},
                                       std::tuple{"DQT two 16-bit tables 1,100 bytes in", 1000, 1500}}) {
    std::vector<uint8_t> file;
    for (const fixtures::JpegCase& c : fixtures::jpegSegmentCases()) {
      if (c.name == name) file = c.file;
    }
    ASSERT_FALSE(file.empty()) << name;
    FakeFile wholeFile = whole(file);
    ASSERT_EQ(wholeFile.walked, file.size()) << name;
    for (int cut = from; cut < to; ++cut) {
      FakeFile f = wholeFile;
      f.size = static_cast<size_t>(cut);
      f.shrunk = true;
      int result = 0;
      bool opened = true;
      EXPECT_TRUE(decodeJpeg(f, 0, result, jpegRead, &opened)) << name << " cut " << cut;
    }
  }
}

TEST(ImageDecoders, EverySyntheticCaseTheWalkPassesDecodesCleanly) {
  // The walk's synthetic headers (image_fixtures.h): each it calls Complete is opened and decoded by the real JPEGDEC
  // under the sanitizers and the canaries, at every scale.
  int complete = 0;
  for (const fixtures::JpegCase& c : fixtures::jpegSegmentCases()) {
    FakeFile f = whole(c.file);
    const bool walks = walkInto(f, c.file, c.file.size(), false);
    ASSERT_EQ(walks, c.expected == FileLayout::Complete) << c.name;
    if (!walks) continue;
    ++complete;
    for (const int option : JPEG_OPTIONS) {
      FakeFile run = f;
      int result = 0;
      bool opened = false;
      EXPECT_TRUE(decodeJpeg(run, option, result, jpegRead, &opened)) << c.name << " option " << option;
      if (!opened) break;
    }
  }
  EXPECT_GT(complete, 20);
}

TEST(ImageDecoders, OverSubscribedHuffmanTablesAreRefusedBeforeOpen) {
  // The review's DHT (FF C4 00 1F 11 0C, 15 zero counts, 12 values) walked Complete and JPEGDEC::open wrote past its
  // object building its tables (JPEGMakeHuffTables, jpeg.inl 1066-1275); the walk now refuses it, so open() never runs.
  for (const fixtures::JpegCase& c : fixtures::jpegSegmentCases()) {
    if (c.name.rfind("DHT", 0) != 0 || c.expected == FileLayout::Complete) continue;
    FakeFile f;
    EXPECT_FALSE(walkInto(f, c.file, c.file.size(), false)) << c.name;
  }
}

TEST(ImageDecoders, SingleByteChangesToARealHeaderThatTheWalkPassesDecodeCleanly) {
  // Every byte of JPEG_BASELINE's header set to a few values; each file the walk passes is decoded.
  const std::vector<uint8_t> base = bytes(JPEG_BASELINE);
  int complete = 0;
  for (size_t at = 2; at < 330; ++at) {
    for (const int v : {0x00, 0x01, 0x0F, 0x10, 0x40, 0x80, 0xFF}) {
      if (base[at] == v) continue;
      std::vector<uint8_t> changed = base;
      changed[at] = static_cast<uint8_t>(v);
      FakeFile f = whole(changed);
      if (!walkInto(f, changed, changed.size(), false)) continue;
      ++complete;
      int result = 0;
      EXPECT_TRUE(decodeJpeg(f, 0, result)) << "byte " << at << " = " << v;
    }
  }
  EXPECT_GT(complete, 100);
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
  f.headerEnd = static_cast<uint32_t>(noSos.size());  // all of it is header to the reader's answers
  int result = 0;
  FakeFile filled = f;
  EXPECT_FALSE(decodeJpeg(filled, 0, result, jpegReadFilled));
  EXPECT_GT(filled.reads, MAX_READS);
  // The reader's answers end both: zeros are stepped over until the offset passes the bytes read (jpeg.inl 1623).
  EXPECT_TRUE(decodeJpeg(f, 0, result));
  EXPECT_NE(result, 1);
  FakeFile stopped = whole(bigExifJpeg());
  stopped.stopAtRead = 2;
  bool opened = true;
  EXPECT_TRUE(decodeJpeg(stopped, 0, result, jpegRead, &opened));
  EXPECT_FALSE(opened);
}

TEST(ImageDecoders, JpegdecOpensEveryFileTheWalkCallsComplete) {
  // The walk visits the offsets JPEGParseInfo visits: where it finds a whole file, JPEGDEC parses its header.
  for (const auto& sample : {bytes(JPEG_BASELINE), bytes(JPEG_PROGRESSIVE), bigExifJpeg(), bytes(JPEG_NOISE_BASELINE),
                             bytes(JPEG_NOISE_PROGRESSIVE)}) {
    FakeFile f = whole(sample);
    ASSERT_EQ(f.walked, sample.size());
    int result = 0;
    bool opened = false;
    ASSERT_TRUE(decodeJpeg(f, 0, result, jpegRead, &opened));
    EXPECT_TRUE(opened);
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
  // Stopped before it starts: neither gets the file, neither draws, both return.
  FakeFile stoppedPng = whole(bytes(PNG_GRAY));
  stoppedPng.stopped = true;
  EXPECT_TRUE(decodePng(stoppedPng, result));
  EXPECT_EQ(stoppedPng.draws, 0);
  for (const auto& sample : {bytes(JPEG_BASELINE), bigExifJpeg()}) {
    FakeFile f = whole(sample);
    f.stopped = true;
    bool opened = true;
    EXPECT_TRUE(decodeJpeg(f, 0, result, jpegRead, &opened));
    EXPECT_FALSE(opened);
    EXPECT_EQ(f.draws, 0);
  }
}

}  // namespace
