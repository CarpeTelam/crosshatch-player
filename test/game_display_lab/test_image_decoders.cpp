// The gray viewer's second layer against a decoder that never returns (story dlab-g3): the real PNGdec 1.1.6 and
// JPEGDEC, fed through the reader's read answers (GameDisplayLab::readAllowance, fillPngRead, clampJpegRead) with
// every cut and a few appends of the sample pictures, each decode bounded in reads and draws (a decode past the bound
// throws out of the callback and fails the test). The layout walk would refuse most of these files; here the decoders
// get them anyway, as when a file shrinks or changes after its walk, so the reads alone must end every decode.
//
// Built only when the decoders' sources are there (test/game_display_lab/CMakeLists.txt: CROSSHATCH_PNGDEC_DIR,
// CROSSHATCH_JPEGDEC_DIR, by default PlatformIO's library folder, which a host-only checkout does not have).

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

// The file behind the callbacks: its bytes, the size the walk would have given the decoder, the reader's position,
// the stop, and the counts against their bounds.
struct FakeFile {
  std::vector<uint8_t> data;
  uint32_t walked = 0;
  size_t pos = 0;
  bool stopped = false;
  int stopAtDraw = -1;
  int reads = 0;
  int draws = 0;
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

// The file's own read under the reader's allowance: what the file has at `position`, at most what is allowed.
int32_t readFake(FakeFile& f, const uint32_t position, uint8_t* buf, const int32_t len) {
  if (++f.reads > MAX_READS) throw Spun{};
  const int32_t allowed = readAllowance(position, len, f.walked, f.stopped);
  int32_t got = 0;
  while (got < allowed && position + static_cast<size_t>(got) < f.data.size()) {
    buf[got] = f.data[position + static_cast<size_t>(got)];
    ++got;
  }
  return got;
}

int32_t pngRead(PNGFILE* pFile, uint8_t* buf, const int32_t len) {
  FakeFile& f = *static_cast<FakeFile*>(pFile->fHandle);
  const int32_t got = readFake(f, static_cast<uint32_t>(f.pos), buf, len);
  f.pos += static_cast<size_t>(got);
  return fillPngRead(buf, len, got);
}

int32_t pngSeek(PNGFILE* pFile, const int32_t pos) {
  static_cast<FakeFile*>(pFile->fHandle)->pos = static_cast<size_t>(pos < 0 ? 0 : pos);
  return pos;
}

int pngDraw(PNGDRAW* pDraw) {
  FakeFile& f = *static_cast<FakeFile*>(pDraw->pUser);
  if (++f.draws > MAX_DRAWS) throw Spun{};
  if (f.draws == f.stopAtDraw) f.stopped = true;
  ++f.rows;
  return f.stopped ? 0 : 1;
}

int32_t jpegRead(JPEGFILE* pFile, uint8_t* buf, const int32_t len) {
  FakeFile& f = *static_cast<FakeFile*>(pFile->fHandle);
  const int32_t got = clampJpegRead(readFake(f, static_cast<uint32_t>(pFile->iPos), buf, len));
  pFile->iPos += got;
  return got;
}

// What the reader does not do: JPEGDEC given PNGdec's answer, the rest of the requested length as 0xFF.
int32_t jpegReadFilled(JPEGFILE* pFile, uint8_t* buf, const int32_t len) {
  FakeFile& f = *static_cast<FakeFile*>(pFile->fHandle);
  const int32_t got = readFake(f, static_cast<uint32_t>(pFile->iPos), buf, len);
  pFile->iPos += got;
  return fillPngRead(buf, len, got);
}

int32_t jpegSeek(JPEGFILE* pFile, const int32_t pos) {
  static_cast<FakeFile*>(pFile->fHandle)->pos = static_cast<size_t>(pos < 0 ? 0 : pos);
  pFile->iPos = pos;
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

bool decodeJpeg(FakeFile& f, const int option, int& result, JPEG_READ_CALLBACK* read = jpegRead) {
  auto jpeg = std::make_unique<JPEGDEC>();
  current = &f;
  try {
    result = jpeg->open("fake.jpg", openFake, closeFake, read, jpegSeek, jpegDraw);
    if (result == 1) {
      jpeg->setPixelType(EIGHT_BIT_GRAYSCALE);
      jpeg->setUserPointer(&f);
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

// The sample cut at every length, as walked (the decoder told the cut size) and as shrunk after a walk of the whole
// (told the whole size, given the cut), and with a few things appended.
std::vector<FakeFile> cutsAndAppends(const std::vector<uint8_t>& sample) {
  std::vector<FakeFile> files;
  for (size_t cut = 0; cut <= sample.size(); ++cut) {
    FakeFile walked;
    walked.data.assign(sample.begin(), sample.begin() + static_cast<long>(cut));
    walked.walked = static_cast<uint32_t>(cut);
    files.push_back(walked);
    FakeFile shrunk = walked;
    shrunk.walked = static_cast<uint32_t>(sample.size());
    files.push_back(shrunk);
  }
  const std::vector<std::vector<uint8_t>> tails = {std::vector<uint8_t>(7, 0),
                                                   std::vector<uint8_t>(10, 0),
                                                   std::vector<uint8_t>(300, 0xFF),
                                                   {0x00, 0x00, 0x00, 0x04, 'I', 'D', 'A', 'T', 1, 2, 3, 4, 0, 0, 0, 0},
                                                   sample};
  for (const auto& tail : tails) {
    FakeFile appended;
    appended.data = sample;
    appended.data.insert(appended.data.end(), tail.begin(), tail.end());
    appended.walked = static_cast<uint32_t>(appended.data.size());
    files.push_back(appended);
  }
  return files;
}

TEST(ImageDecoders, PngdecReturnsOnEveryCutAndAppend) {
  for (const auto& sample : {bytes(PNG_GRAY), bytes(PNG_PALETTE)}) {
    for (FakeFile& f : cutsAndAppends(sample)) {
      int result = 0;
      EXPECT_TRUE(decodePng(f, result)) << "size " << f.data.size() << " walked " << f.walked;
    }
  }
}

TEST(ImageDecoders, PngdecDecodesTheWholeSamples) {
  for (const auto& [sample, height] : {std::pair{bytes(PNG_GRAY), 16}, std::pair{bytes(PNG_PALETTE), 12}}) {
    FakeFile f;
    f.data = sample;
    f.walked = static_cast<uint32_t>(sample.size());
    int result = -1;
    ASSERT_TRUE(decodePng(f, result));
    EXPECT_EQ(result, PNG_SUCCESS);
    EXPECT_EQ(f.rows, height);
  }
}

TEST(ImageDecoders, JpegdecReturnsOnEveryCutAndAppend) {
  for (const auto& sample : {bytes(JPEG_BASELINE), bytes(JPEG_PROGRESSIVE)}) {
    for (const int option : {0, JPEG_SCALE_HALF, JPEG_SCALE_EIGHTH}) {
      for (FakeFile& f : cutsAndAppends(sample)) {
        int result = 0;
        EXPECT_TRUE(decodeJpeg(f, option, result))
            << "size " << f.data.size() << " walked " << f.walked << " option " << option;
      }
    }
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
                               Sample{bytes(JPEG_PROGRESSIVE), JPEG_SCALE_EIGHTH, 3}}) {
    FakeFile f;
    f.data = sample.file;
    f.walked = static_cast<uint32_t>(sample.file.size());
    int result = -1;
    ASSERT_TRUE(decodeJpeg(f, sample.option, result));
    EXPECT_EQ(result, 1);
    EXPECT_EQ(f.rows, sample.height);
  }
}

TEST(ImageDecoders, JpegdecIsGivenNoFillerBecauseItsHeaderParseWouldSpin) {
  // A 3,000-byte file with no SOS (SOI, an APP0 of 1500 bytes, zeros), given whole: once its bytes run out, 0xFF
  // filler keeps JPEGParseInfo's marker loop reading (jpeg.inl 1611 compares a buffer offset, kept under 1 KiB by each
  // refill, with the 3,000-byte size; 0xFFFF is stepped over, jpeg.inl 1642), while an empty read leaves the buffer
  // short and ends it (jpeg.inl 1623).
  FakeFile f;
  f.data = {0xFF, 0xD8, 0xFF, 0xE0, 0x05, 0xDC};
  f.data.resize(3000, 0x00);
  f.walked = static_cast<uint32_t>(f.data.size());
  int result = 0;
  FakeFile filled = f;
  EXPECT_FALSE(decodeJpeg(filled, 0, result, jpegReadFilled));
  EXPECT_GT(filled.reads, MAX_READS);
  EXPECT_TRUE(decodeJpeg(f, 0, result));
  EXPECT_NE(result, 1);
}

TEST(ImageDecoders, JpegdecOpensEveryCutTheWalkCallsComplete) {
  // The walk visits the offsets JPEGParseInfo visits: where it finds a whole header, JPEGDEC parses one (in a file of
  // 256 bytes or more: JPEGDEC refuses a smaller one outright, jpeg.inl 1598).
  for (const auto& sample : {bytes(JPEG_BASELINE), bytes(JPEG_PROGRESSIVE)}) {
    for (size_t cut = 0; cut <= sample.size(); ++cut) {
      const auto readAt = [&](const uint32_t offset, uint8_t* out, const size_t n) {
        if (offset + n > cut) return false;
        std::copy(sample.begin() + offset, sample.begin() + offset + static_cast<long>(n), out);
        return true;
      };
      const FileLayout layout = walkJpegSegments(static_cast<uint32_t>(cut), readAt, [] { return false; });
      if (layout != FileLayout::Complete || cut < 256) continue;
      FakeFile f;
      f.data.assign(sample.begin(), sample.begin() + static_cast<long>(cut));
      f.walked = static_cast<uint32_t>(cut);
      auto jpeg = std::make_unique<JPEGDEC>();
      current = &f;
      EXPECT_EQ(jpeg->open("fake.jpg", openFake, closeFake, jpegRead, jpegSeek, jpegDraw), 1) << cut;
      jpeg->close();
    }
  }
}

TEST(ImageDecoders, AStopEndsEitherDecodeAndItsReads) {
  FakeFile png;
  png.data = bytes(PNG_GRAY);
  png.walked = static_cast<uint32_t>(png.data.size());
  png.stopAtDraw = 3;
  int result = 0;
  ASSERT_TRUE(decodePng(png, result));
  EXPECT_EQ(result, PNG_QUIT_EARLY);
  EXPECT_EQ(png.draws, 3);
  FakeFile jpeg;
  jpeg.data = bytes(JPEG_BASELINE);
  jpeg.walked = static_cast<uint32_t>(jpeg.data.size());
  jpeg.stopAtDraw = 1;
  ASSERT_TRUE(decodeJpeg(jpeg, 0, result));
  // JPEGDEC reports success when its draw callback ends the decode (jpeg.inl 5342, 5373: only a decode error is a
  // failure), so the reader checks its own stop before the result.
  EXPECT_EQ(result, 1);
  EXPECT_EQ(jpeg.draws, 1);
  // Stopped before it starts: the file gives nothing, and both decoders still return.
  for (const auto& sample : {bytes(PNG_GRAY), bytes(JPEG_BASELINE)}) {
    FakeFile f;
    f.data = sample;
    f.walked = static_cast<uint32_t>(sample.size());
    f.stopped = true;
    EXPECT_TRUE(sample[0] == 0x89 ? decodePng(f, result) : decodeJpeg(f, 0, result));
    EXPECT_EQ(f.draws, 0);
  }
}

}  // namespace
