// The gray viewer's layers against the real decoders (story dlab-g3): PNGdec 1.1.6 and JPEGDEC (at the firmware's
// pinned commit), each with the fork's build-time patches (scripts/pngdec_patches, scripts/jpegdec_patches), JPEGDEC's
// bit reader 32 bits wide as on the device (jpegdec_as_device.h), fed through the reader's walks and its read
// callbacks (GameDisplayLab::walkPngChunks, walkJpegSegments, and DecoderReads.h's answerPngRead, answerSeek,
// answerJpegRead, answerJpegSeek: the very functions GrayImageReader.cpp's callbacks call, over a fake file here),
// with every cut and a few appends of the sample pictures, a stop at every read, every synthetic header the walk
// passes, and single-byte changes to a real header, each decode bounded in reads and draws (a decode past the bound
// throws out of the callback and fails the test). Built with AddressSanitizer and UndefinedBehaviorSanitizer; what they
// cannot see inside a decoder's object is watched: 64 poisoned bytes after JPEGDEC's file buffer (a test-only field of
// the build tree's copy, test/game_display_lab/CMakeLists.txt), so a header parse or decode that reads or writes just
// past it fails the test; JPEGDEC's bit offset stays within the data in its buffer at every strip and after the
// decode (iVLCOff at most iVLCSize); its Huffman tables after the buffer are untouched by the decode; and the part of
// PNGdec's row buffer after its palette that no row uses is untouched.
//
// Built only when the decoders' sources are there (test/game_display_lab/CMakeLists.txt: CROSSHATCH_PNGDEC_DIR,
// CROSSHATCH_JPEGDEC_DIR, by default PlatformIO's library folder; the fork's CI fetches both).

#include <gtest/gtest.h>
#include <sanitizer/asan_interface.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "DecoderReads.h"
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

// The guard is whole 8-byte granules, so AddressSanitizer poisons every byte of it.
static_assert(offsetof(JPEGIMAGE, ucGuard) % 8 == 0 && sizeof(JPEGIMAGE::ucGuard) % 8 == 0,
              "the guard after JPEGDEC's file buffer is whole ASan granules");

// A decode that went on past its bound.
struct Spun {};

// The file behind the callbacks: the first `size` bytes of `source`, the reads' state (DecoderReads: what the walk
// found, a refused seek, the caps' counts, the header latch), the file's own position (as HalFile's), the stop (asked
// by hand, at a read index, or at a draw), the counts against their bounds, and the canaries.
struct FakeFile {
  std::shared_ptr<const std::vector<uint8_t>> source;
  size_t size = 0;
  DecoderReads reads;
  // The file shrank after a walk of the whole sample (its reads come up short: a failed read, as a pulled card's).
  bool shrunk = false;
  size_t pos = 0;
  bool stopped = false;
  int stopAtRead = -1;
  int stopAtDraw = -1;
  int reads_ = 0;
  int progress = 0;
  int draws = 0;
  // PNG: rows drawn; JPEG: the bottom of the lowest block drawn.
  int rows = 0;
  // The JPEGDEC object, for the guard and the bit-pointer canary at each strip; whether the guard is poisoned and the
  // canary checked at strips (both, but for a crafted file whose one MCU reads past the buffer by design).
  JPEGDEC* jpeg = nullptr;
  bool poisonGuard = true;
  bool watchStrips = true;
  bool pointerLeft = false;
  // When set, the pixels drawn (PNG: each row as gray; JPEG: the 8-bit blocks), `width` a row.
  std::vector<uint8_t>* pixels = nullptr;
  int width = 0;
};

// DecoderReads.h's file, over a FakeFile: its bytes up to its size, a seek past it refused (as HalFile's), its stop,
// and the progress counted.
struct FakeAccess {
  FakeFile& f;

  bool isOpen() const { return true; }
  uint32_t position() { return static_cast<uint32_t>(f.pos); }
  int32_t read(uint8_t* buf, const int32_t n) {
    int32_t got = 0;
    while (got < n && f.pos < f.size) buf[got++] = (*f.source)[f.pos++];
    return got;
  }
  bool seek(const uint32_t position) {
    if (position > f.size) return false;
    f.pos = position;
    return true;
  }
  bool stopped() const { return f.stopped; }
  void progress() { ++f.progress; }
};

constexpr int MAX_READS = 4000;
constexpr int MAX_DRAWS = 4000;

FakeFile* current = nullptr;

void* openFake(const char*, int32_t* size) {
  current->pos = 0;
  *size = static_cast<int32_t>(current->reads.walkedSize);
  // JPEGDEC::open has just cleared its object (jpeg.inl's memset): the guard is poisoned from here to the close.
  if (current->jpeg && current->poisonGuard) {
    ASAN_POISON_MEMORY_REGION(current->jpeg->_jpeg.ucGuard, sizeof(current->jpeg->_jpeg.ucGuard));
  }
  return current;
}

void closeFake(void*) {}

// One read, counted against the bound; the stop asked at its index.
void countRead(FakeFile& f) {
  if (++f.reads_ > MAX_READS) throw Spun{};
  if (f.reads_ == f.stopAtRead) f.stopped = true;
}

int32_t pngRead(PNGFILE* pFile, uint8_t* buf, const int32_t len) {
  FakeFile& f = *static_cast<FakeFile*>(pFile->fHandle);
  countRead(f);
  FakeAccess file{f};
  return answerPngRead(file, f.reads, buf, len);
}

int32_t pngSeek(PNGFILE* pFile, const int32_t pos) {
  FakeFile& f = *static_cast<FakeFile*>(pFile->fHandle);
  FakeAccess file{f};
  return answerSeek(file, f.reads, pos);
}

int pngDraw(PNGDRAW* pDraw) {
  FakeFile& f = *static_cast<FakeFile*>(pDraw->pUser);
  if (++f.draws > MAX_DRAWS) throw Spun{};
  if (f.draws == f.stopAtDraw) f.stopped = true;
  if (f.pixels) {
    std::vector<uint8_t> gray(static_cast<size_t>(pDraw->iWidth));
    pngRowToGray(pDraw->pPixels, gray.data(), pDraw->iWidth, pDraw->iPixelType, pDraw->iBpp, pDraw->pPalette,
                 pDraw->iHasAlpha != 0, 0);
    f.pixels->insert(f.pixels->end(), gray.begin(), gray.end());
  }
  ++f.rows;
  return f.stopped ? 0 : 1;
}

// GrayImageReader.cpp's jpegRead and jpegSeek.
int32_t jpegRead(JPEGFILE* pFile, uint8_t* buf, const int32_t len) {
  FakeFile& f = *static_cast<FakeFile*>(pFile->fHandle);
  countRead(f);
  FakeAccess file{f};
  return answerJpegRead(file, f.reads, buf, len, pFile->iSize, pFile->iPos);
}

int32_t jpegSeek(JPEGFILE* pFile, const int32_t pos) {
  FakeFile& f = *static_cast<FakeFile*>(pFile->fHandle);
  FakeAccess file{f};
  return answerJpegSeek(file, f.reads, pos, pFile->iSize, pFile->iPos);
}

// What the reader does not do: JPEGDEC given PNGdec's answer, the rest of the requested length as 0xFF.
int32_t jpegReadFilled(JPEGFILE* pFile, uint8_t* buf, const int32_t len) {
  FakeFile& f = *static_cast<FakeFile*>(pFile->fHandle);
  countRead(f);
  FakeAccess file{f};
  int32_t allowed = 0;
  const int32_t got = readUnderAllowance(file, f.reads, file.position(), buf, len, f.stopped, allowed);
  pFile->iPos = static_cast<int32_t>(f.pos);
  return fillPngRead(buf, len, got);
}

// Nor this: what the file gave and the buffer otherwise untouched (the third batch's answer, and the converter's).
int32_t jpegReadStale(JPEGFILE* pFile, uint8_t* buf, const int32_t len) {
  FakeFile& f = *static_cast<FakeFile*>(pFile->fHandle);
  countRead(f);
  FakeAccess file{f};
  int32_t allowed = 0;
  const int32_t got = readUnderAllowance(file, f.reads, file.position(), buf, len, f.stopped, allowed);
  pFile->iPos = static_cast<int32_t>(f.pos);
  return got;
}

// Nor this: the reader's answers with the decode's refills only zero-filled, never tamed; `tameable` counts the
// refills the fifth batch's rule (jpegFilteredLength, every 0xFF to 0) would have tamed.
int tameable = 0;
int32_t jpegReadUntamed(JPEGFILE* pFile, uint8_t* buf, const int32_t len) {
  FakeFile& f = *static_cast<FakeFile*>(pFile->fHandle);
  FakeAccess file{f};
  if (f.reads.jpegOpening && file.position() < f.reads.headerEnd) return jpegRead(pFile, buf, len);
  countRead(f);
  int32_t allowed = 0;
  const int32_t got = readUnderAllowance(file, f.reads, file.position(), buf, len, f.stopped, allowed);
  for (int32_t i = got; i < len; ++i) buf[i] = 0;
  if (static_cast<int32_t>(JPEG_PARSE_BUFFER) - len + jpegFilteredLength(buf, len) <
      JPEG_FILE_HIGHWATER + JPEG_MCU_MARGIN) {
    ++tameable;
  }
  pFile->iPos = jpegPositionAfterRead(file.position(), static_cast<uint32_t>(pFile->iSize));
  return len;
}

// The bit-pointer canary: JPEGDEC's VLC offset and bit pointer within the data in its buffer (iVLCSize, at most its
// 2 KiB).
bool pointerInside(const JPEGDEC& jpeg) {
  const JPEGIMAGE& image = jpeg._jpeg;
  const ptrdiff_t at = image.bb.pBuf - image.ucFileBuf;
  return image.iVLCSize >= 0 && image.iVLCSize <= JPEG_FILE_BUF_SIZE && image.iVLCOff >= 0 &&
         image.iVLCOff <= image.iVLCSize && at >= 0 && at <= image.iVLCSize;
}

int jpegDraw(JPEGDRAW* pDraw) {
  FakeFile& f = *static_cast<FakeFile*>(pDraw->pUser);
  if (++f.draws > MAX_DRAWS) throw Spun{};
  if (f.jpeg && f.watchStrips && !pointerInside(*f.jpeg)) f.pointerLeft = true;
  if (f.draws == f.stopAtDraw) f.stopped = true;
  if (pDraw->y + pDraw->iHeight > f.rows) f.rows = pDraw->y + pDraw->iHeight;
  if (f.pixels) {
    const auto* const block = reinterpret_cast<const uint8_t*>(pDraw->pPixels);
    for (int y = 0; y < pDraw->iHeight; ++y) {
      for (int x = 0; x < pDraw->iWidthUsed; ++x) {
        const size_t at =
            static_cast<size_t>(pDraw->y + y) * static_cast<size_t>(f.width) + static_cast<size_t>(pDraw->x + x);
        if (at < f.pixels->size()) (*f.pixels)[at] = block[y * pDraw->iWidth + x];
      }
    }
  }
  return f.stopped ? 0 : 1;
}

// The row-buffer canary: PNGdec's rows use the start of ucPixels (two lines and their 16-byte alignment, under 200
// bytes for the samples here, at most 64 px of 8 bits); past this offset, up to the fast palette's 512 bytes at its
// end, nothing of a decode writes, but a palette copied past ucPalette's 1,024 bytes runs straight into it.
constexpr size_t PNG_CANARY_FROM = 256;
constexpr uint8_t CANARY = 0xA5;

// The reads' state for a new open() of `f` (its walk's extent kept).
void restartReads(FakeFile& f) {
  f.reads.start(WalkExtent{f.reads.walkedSize, f.reads.headerEnd, f.reads.windowMoves});
}

// Opens and decodes `f` as the reader does; false when it spun. `result` is the decode's (or the open's) code;
// `canaryHeld`, when given, whether the row-buffer canary held.
bool decodePng(FakeFile& f, int& result, bool* canaryHeld = nullptr) {
  auto png = std::make_unique<PNG>();
  current = &f;
  restartReads(f);
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

// `opened` says whether open() succeeded, no header read was dropped and the size passed (and decode() ran). The
// canaries are checked here: the bit pointer at every strip and after the decode, and the Huffman tables after the
// 2 KiB buffer unchanged by the decode; the guard is poisoned from open() to the close.
bool decodeJpeg(FakeFile& f, const int option, int& result, JPEG_READ_CALLBACK* read = jpegRead,
                bool* opened = nullptr) {
  auto jpeg = std::make_unique<JPEGDEC>();
  current = &f;
  f.jpeg = jpeg.get();
  restartReads(f);
  if (opened) *opened = false;
  bool spun = false;
  try {
    f.reads.startJpegOpen();
    result = jpeg->open("fake.jpg", openFake, closeFake, read, jpegSeek, jpegDraw);
    // As the reader: a dropped header read is a failed open whatever open() made of the zeros; a size outside the
    // converters' limits is refused before decode() (a corrupt SOF can claim 65,535 rows).
    if (f.reads.headerFailed) result = -2;
    if (result == 1 && !imageSizeAccepted(jpeg->getWidth(), jpeg->getHeight())) result = -1;
    if (result == 1) {
      if (opened) *opened = true;
      jpeg->setPixelType(EIGHT_BIT_GRAYSCALE);
      jpeg->setUserPointer(&f);
      std::vector<uint8_t> tables(sizeof(jpeg->_jpeg.ucHuffDC) + sizeof(jpeg->_jpeg.usHuffAC));
      std::memcpy(tables.data(), jpeg->_jpeg.ucHuffDC, sizeof(jpeg->_jpeg.ucHuffDC));
      std::memcpy(tables.data() + sizeof(jpeg->_jpeg.ucHuffDC), jpeg->_jpeg.usHuffAC, sizeof(jpeg->_jpeg.usHuffAC));
      f.reads.startDecode();
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
  ASAN_UNPOISON_MEMORY_REGION(jpeg->_jpeg.ucGuard, sizeof(jpeg->_jpeg.ucGuard));
  f.jpeg = nullptr;
  EXPECT_FALSE(f.pointerLeft) << "JPEGDEC's bit pointer left the data in its buffer";
  return !spun;
}

template <size_t N>
std::vector<uint8_t> bytes(const uint8_t (&a)[N]) {
  return std::vector<uint8_t>(a, a + N);
}

// The walk of the first `size` bytes of `bytes`, as the reader walks a file (the JPEG walk with the reader's EOI
// search buffer); its extent into `f`. True when Complete.
bool walkInto(FakeFile& f, const std::vector<uint8_t>& bytes, const size_t size, const bool png) {
  const auto readAt = [&bytes, size](const uint32_t offset, uint8_t* out, const size_t n) {
    if (offset + n > size) return false;
    std::copy(bytes.begin() + offset, bytes.begin() + offset + static_cast<long>(n), out);
    return true;
  };
  WalkExtent extent;
  std::vector<uint8_t> search(JPEG_EOI_SEARCH_BYTES);
  const auto size32 = static_cast<uint32_t>(size);
  const FileLayout layout =
      png ? walkPngChunks(
                size32, readAt, [] { return false; }, &extent)
          : walkJpegSegments(size32, readAt, [] { return false; }, &extent, search.data(), search.size());
  if (layout != FileLayout::Complete) return false;
  f.reads.start(extent);
  return true;
}

// `sample` whole, as the reader gives it after its walk (or, for a file the walk refuses, the whole file as walked).
FakeFile whole(const std::vector<uint8_t>& sample) {
  FakeFile f;
  f.source = std::make_shared<const std::vector<uint8_t>>(sample);
  f.size = sample.size();
  f.reads.walkedSize = static_cast<uint32_t>(sample.size());
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
    walked.reads.walkedSize = static_cast<uint32_t>(cut);
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
  const int reads = full.reads_;
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
      EXPECT_TRUE(decodePng(f, result)) << "size " << f.size << " walked " << f.reads.walkedSize;
    }
  }
}

TEST(ImageDecoders, PngdecDecodesTheWholeSamples) {
  for (const auto& [sample, height] :
       {std::pair{bytes(PNG_GRAY), 16}, std::pair{bytes(PNG_PALETTE), 12}, std::pair{bytes(PNG_NOISE), 48}}) {
    FakeFile f = whole(sample);
    EXPECT_EQ(f.reads.walkedSize, sample.size());
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
  ASSERT_EQ(walked.reads.walkedSize, 33u + 24u + 12u);  // the walked size ends at IEND
  int result = 0;
  bool held = false;
  ASSERT_TRUE(decodePng(walked, result, &held));
  EXPECT_TRUE(held);
  EXPECT_NE(result, PNG_SUCCESS);
  // The fourth batch's reader, which gave the whole file: PNGdec as released copied the PLTE on into its row buffer;
  // scripts/pngdec_patches/0001 now refuses a PLTE over 768 bytes before the copy as well.
  FakeFile unwalked = whole(review);
  unwalked.reads.walkedSize = static_cast<uint32_t>(review.size());
  ASSERT_TRUE(decodePng(unwalked, result, &held));
  EXPECT_TRUE(held);
  EXPECT_NE(result, PNG_SUCCESS);
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
            << "size " << f.size << " walked " << f.reads.walkedSize << " option " << option;
        if (!opened) break;  // the scale only matters to decode()
      }
    }
    EXPECT_GT(decoded, static_cast<int>(sample.size()));  // every shrunk cut, and the whole file
  }
}

TEST(ImageDecoders, NoiseJpegsReadPastTheirBufferAndReturnOnEveryCut) {
  // Noise: scans past JPEGDEC's 2 KiB buffer, so decode() itself reads (JPEG_NOISE_PROGRESSIVE's DC scan, all of it
  // that is decoded, fits in the buffer; JPEG_PROGRESSIVE_512's does not). The colour ones at quality 100 have MCUs of
  // three (4:4:4) and six (4:2:0) blocks of long codes.
  for (const auto& [sample, decodeReads] :
       {std::pair{bytes(JPEG_NOISE_BASELINE), true}, std::pair{bytes(JPEG_NOISE_PROGRESSIVE), false},
        std::pair{bytes(JPEG_PROGRESSIVE_512), true}, std::pair{bytes(JPEG_NOISE_420), true},
        std::pair{bytes(JPEG_NOISE_444), true}}) {
    FakeFile f = whole(sample);
    int result = -1;
    ASSERT_TRUE(decodeJpeg(f, 0, result));
    EXPECT_EQ(result, 1);
    if (decodeReads) {
      EXPECT_GT(f.reads.decodeReads, 0u) << sample.size();
    }
    for (FakeFile& cut : cutsAndAppends(sample, true)) {
      int r = 0;
      EXPECT_TRUE(decodeJpeg(cut, 0, r)) << "size " << cut.size << " walked " << cut.reads.walkedSize;
    }
  }
}

TEST(ImageDecoders, AnErasedSectorInTheScanIsTamed) {
  // 4 KB of 0xFF in the middle of a scan, as an erased flash sector reads: JPEGFilter drops 0xFF pairs as markers, so
  // unanswered the refills would add nothing and the bit pointer would run off the buffer.
  std::vector<uint8_t> erased = bytes(JPEG_NOISE_BASELINE);
  std::fill(erased.begin() + 2000, erased.begin() + 6000, 0xFF);
  FakeFile f = whole(erased);
  ASSERT_EQ(f.reads.walkedSize, erased.size());
  for (const int option : JPEG_OPTIONS) {
    FakeFile run = f;
    int result = 0;
    EXPECT_TRUE(decodeJpeg(run, option, result)) << option;
    EXPECT_GT(run.reads.decodeReads, 0u);
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
    EXPECT_TRUE(decodeJpeg(f, 0, result, jpegRead, &opened)) << "size " << f.size << " walked " << f.reads.walkedSize;
  }
}

TEST(ImageDecoders, JpegdecDecodesTheWholeSamples) {
  // A progressive JPEG is decoded at an eighth, as the reader decodes one (its DC-only decode): 40 x 24 is 5 x 3.
  // decode() itself reads only when the scan it decodes is past the 2 KiB buffer.
  struct Sample {
    std::vector<uint8_t> file;
    int option;
    int height;
    bool decodeReads;
  };
  for (const Sample& sample :
       {Sample{bytes(JPEG_BASELINE), 0, 24, false}, Sample{bytes(JPEG_BASELINE), JPEG_SCALE_HALF, 12, false},
        Sample{bytes(JPEG_BASELINE), JPEG_SCALE_QUARTER, 6, false},
        Sample{bytes(JPEG_PROGRESSIVE), JPEG_SCALE_EIGHTH, 3, false}, Sample{bigExifJpeg(), 0, 24, false},
        Sample{bytes(JPEG_NOISE_BASELINE), 0, 128, true},
        Sample{bytes(JPEG_NOISE_PROGRESSIVE), JPEG_SCALE_EIGHTH, 16, false},
        Sample{bytes(JPEG_PROGRESSIVE_512), JPEG_SCALE_EIGHTH, 64, true}, Sample{bytes(JPEG_NOISE_420), 0, 48, true},
        Sample{bytes(JPEG_NOISE_444), 0, 32, true}, Sample{bytes(JPEG_LINE_ART_RESTARTS), 0, 192, true}}) {
    FakeFile f = whole(sample.file);
    int result = -1;
    ASSERT_TRUE(decodeJpeg(f, sample.option, result));
    EXPECT_EQ(result, 1) << sample.file.size();
    EXPECT_EQ(f.rows, sample.height) << sample.file.size();
    if (sample.decodeReads) {
      EXPECT_GT(f.reads.decodeReads, 0u) << sample.file.size();
    } else {
      EXPECT_EQ(f.reads.decodeReads, 0u) << sample.file.size();
    }
  }
}

TEST(ImageDecoders, JpegdecReturnsAtAStopOnEveryRead) {
  for (const auto& sample : {bytes(JPEG_BASELINE), bytes(JPEG_PROGRESSIVE), bigExifJpeg(), bytes(JPEG_NOISE_BASELINE),
                             bytes(JPEG_NOISE_PROGRESSIVE), bytes(JPEG_PROGRESSIVE_512), bytes(JPEG_NOISE_420),
                             bytes(JPEG_NOISE_444), bytes(JPEG_LINE_ART_RESTARTS)}) {
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
    EXPECT_LE(f.reads_, at + 2) << at;
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
    ASSERT_EQ(wholeFile.reads.walkedSize, file.size()) << name;
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
    EXPECT_GT(stale.reads_, MAX_READS);
  }
  // A 3,000-byte file with no SOS (SOI, an APP0 of 1500 bytes, zeros), given whole: once its bytes run out, 0xFF
  // filler keeps the same loop reading (it compares a buffer offset, kept under 1 KiB by each refill, with the
  // 3,000-byte size; 0xFFFF is stepped over, jpeg.inl 1642).
  std::vector<uint8_t> noSos = {0xFF, 0xD8, 0xFF, 0xE0, 0x05, 0xDC};
  noSos.resize(3000, 0x00);
  FakeFile f = whole(noSos);
  f.reads.headerEnd = static_cast<uint32_t>(noSos.size());  // all of it is header to the reader's answers
  int result = 0;
  FakeFile filled = f;
  EXPECT_FALSE(decodeJpeg(filled, 0, result, jpegReadFilled));
  EXPECT_GT(filled.reads_, MAX_READS);
  // The reader's answers end both: zeros are stepped over until the offset passes the bytes read (jpeg.inl 1623). Told
  // the header ends at 1,000 (a parse that runs past the header the walk found), its reads from there on get the
  // decode's answer JPEG_OPEN_TOP_UPS times, then nothing, which ends it the same way, unlatched.
  EXPECT_TRUE(decodeJpeg(f, 0, result));
  EXPECT_NE(result, 1);
  FakeFile past = f;
  past.reads.headerEnd = 1000;
  EXPECT_TRUE(decodeJpeg(past, 0, result));
  EXPECT_NE(result, 1);
  EXPECT_FALSE(past.reads.headerFailed);
  EXPECT_GT(past.reads.openTopUps, JPEG_OPEN_TOP_UPS);
  FakeFile stopped = whole(bigExifJpeg());
  stopped.stopAtRead = 2;
  bool opened = true;
  EXPECT_TRUE(decodeJpeg(stopped, 0, result, jpegRead, &opened));
  EXPECT_FALSE(opened);
}

TEST(ImageDecoders, JpegdecOpensEveryFileTheWalkCallsComplete) {
  // The walk visits the offsets JPEGParseInfo visits: where it finds a whole file, JPEGDEC parses its header.
  for (const auto& sample : {bytes(JPEG_BASELINE), bytes(JPEG_PROGRESSIVE), bigExifJpeg(), bytes(JPEG_NOISE_BASELINE),
                             bytes(JPEG_NOISE_PROGRESSIVE), bytes(JPEG_PROGRESSIVE_512), bytes(JPEG_NOISE_420),
                             bytes(JPEG_NOISE_444), bytes(JPEG_LINE_ART_RESTARTS)}) {
    FakeFile f = whole(sample);
    ASSERT_EQ(f.reads.walkedSize, sample.size());
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

TEST(ImageDecoders, RestartMarkersAreNeverTamed) {
  // Flat line art with a restart marker after every block: a refill is a third markers, which the fifth batch's
  // estimate counted as lost and tamed, turning them into data. Decoded through the reader's answers and through
  // answers that never tame, the pixels are the same, at every scale.
  const std::vector<uint8_t> art = bytes(JPEG_LINE_ART_RESTARTS);
  for (const int option : JPEG_OPTIONS) {
    const int scale = option == 0 ? 1 : option;
    std::vector<uint8_t> tamed(static_cast<size_t>(256 / scale) * static_cast<size_t>(192 / scale), 0);
    std::vector<uint8_t> untamed = tamed;
    FakeFile a = whole(art);
    a.pixels = &tamed;
    a.width = 256 / scale;
    int result = 0;
    ASSERT_TRUE(decodeJpeg(a, option, result));
    EXPECT_EQ(result, 1);
    FakeFile b = whole(art);
    b.pixels = &untamed;
    b.width = 256 / scale;
    tameable = 0;
    ASSERT_TRUE(decodeJpeg(b, option, result, jpegReadUntamed));
    EXPECT_EQ(result, 1);
    EXPECT_GT(tameable, 0) << option;  // the fifth batch's rule would have rewritten its markers
    EXPECT_EQ(tamed, untamed) << option;
    EXPECT_EQ(a.rows, 192 / scale) << option;
  }
}

TEST(ImageDecoders, AColourMcuPastTheBufferEndsTheDecode) {
  // JPEGGetMoreData moves its data down and refills once a MCU ends past FILE_HIGHWATER, keeping 512 bytes of data past
  // the mark; as released it returned for good once the bit pointer had passed the data (iVLCOff > iVLCSize), and every
  // MCU after read on past its buffer, through its object and beyond. Colour noise at quality 100 (MCUs of 500 to 600
  // bytes) never gets there: it decodes whole at every scale under the canary and the poisoned guard.
  for (const auto& sample : {bytes(JPEG_NOISE_420), bytes(JPEG_NOISE_444)}) {
    for (const int option : JPEG_OPTIONS) {
      FakeFile f = whole(sample);
      int result = 0;
      EXPECT_TRUE(decodeJpeg(f, option, result)) << sample.size() << " option " << option;
      EXPECT_EQ(result, 1) << sample.size() << " option " << option;
    }
  }
  // A valid one of another seed: a MCU of about 600 bytes starts just under the mark and ends past the data in the
  // buffer (short of its 2 KiB end: the guard stays poisoned; the strip drawn after that MCU, before the refill check,
  // sees the pointer past the data, so the strip canary is off). The decode ends in an error with the pointer back
  // inside the data (deferred in the plan: such a file shows "did not decode").
  for (const int option : JPEG_OPTIONS) {
    FakeFile f = whole(bytes(JPEG_NOISE_420_OUTRUN));
    ASSERT_EQ(f.reads.walkedSize, sizeof(JPEG_NOISE_420_OUTRUN));
    f.watchStrips = false;
    int result = -1;
    bool opened = false;
    ASSERT_TRUE(decodeJpeg(f, option, result, jpegRead, &opened)) << option;
    ASSERT_TRUE(opened) << option;
    EXPECT_EQ(result, 0) << option;
  }
  // A crafted file of the longest codes the standard tables have: a 4:2:0 MCU of about 1.24 KB, started under the mark,
  // ends past the 2 KiB buffer. scripts/jpegdec_patches/0004 puts the pointer back at the data's end and ends the
  // decode with an error (it runs after every MCU, so no later MCU starts past the data), and the pointer ends inside
  // the data (decodeJpeg's canary). That one MCU's read past the buffer, at most about 1.24 KB - 512 bytes, stays
  // inside the JPEGDEC object, its Huffman tables after the buffer (the guard, between them here, is left unpoisoned
  // for this file): crafted only, deferred in the plan.
  for (const int option : JPEG_OPTIONS) {
    const std::vector<uint8_t> crafted = fixtures::longCodeColourJpeg(true, 48, 32);
    FakeFile f = whole(crafted);
    ASSERT_EQ(f.reads.walkedSize, crafted.size());
    f.poisonGuard = false;
    f.watchStrips = false;
    int result = -1;
    bool opened = false;
    ASSERT_TRUE(decodeJpeg(f, option, result, jpegRead, &opened)) << option;
    ASSERT_TRUE(opened) << option;
    EXPECT_EQ(result, 0) << option;  // JPEG_DECODE_ERROR
  }
}

// ---- PNG palettes ---------------------------------------------------------------------------------------------------

TEST(ImageDecoders, APaletteAfterALongTextChunkIsReadWhole) {
  // PNGdec copies PLTE out of its 2 KiB file buffer: with a tEXt of 1,000 to 1,900 bytes before it, the 768-byte
  // palette runs past the bytes in the buffer (PNGdec 1.1.6 copied from past its object; scripts/pngdec_patches/0001
  // reads the buffer again from the palette first). Every such file decodes to the pixels of the file without the
  // text, under AddressSanitizer.
  std::vector<uint8_t> plain;
  FakeFile p = whole(bytes(PNG_PALETTE_256));
  p.pixels = &plain;
  int result = -1;
  ASSERT_TRUE(decodePng(p, result));
  ASSERT_EQ(result, PNG_SUCCESS);
  ASSERT_EQ(plain.size(), 256u);
  for (uint32_t text = 1000; text <= 1900; text += 37) {
    const std::vector<uint8_t> file = fixtures::pngWithTextBeforePalette(text);
    std::vector<uint8_t> pixels;
    FakeFile f = whole(file);
    ASSERT_EQ(f.reads.walkedSize, file.size()) << text;
    f.pixels = &pixels;
    ASSERT_TRUE(decodePng(f, result)) << text;
    EXPECT_EQ(result, PNG_SUCCESS) << text;
    EXPECT_EQ(pixels, plain) << text;
  }
  // The file cut and stopped anywhere still returns.
  const std::vector<uint8_t> straddling = fixtures::pngWithTextBeforePalette(1500);
  for (FakeFile& f : cutsAndAppends(straddling, false)) {
    EXPECT_TRUE(decodePng(f, result)) << "size " << f.size;
  }
  stopAtEveryRead(straddling, [](FakeFile& f) {
    int r = 0;
    return decodePng(f, r);
  });
}

}  // namespace
