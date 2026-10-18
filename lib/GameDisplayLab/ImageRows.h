#pragma once

#include <cstddef>
#include <cstdint>

// The gray viewer's decode arithmetic (story dlab-g3, the epic's D16 and its amendment), pure and pinned by
// test_image_rows.cpp. The device reader (src/activities/games/display_lab/GrayImageReader.cpp) drives PNGdec and
// JPEGDEC directly, because the firmware's converters cut gray to 2 bits before they write; everything it computes
// is here: which files it reads, the converters' size limits and PNG checks, a PNG row to 8-bit gray as the converters
// convert one (lib/Epub/Epub/converters/PngToFramebufferConverter.cpp, convertLineToGray with no alpha line: alpha
// composited on white), a PNG's chunk layout checked against its file size before PNGdec reads it, JPEGDEC's coarse
// scale and option as the JPEG converter picks them, the fit to the logical screen, which output pixels a decoded row
// or block makes, and their sampling: bilinear along an axis the fit enlarges (centre-aligned per axis; only the 16.16
// blend arithmetic is the JPEG converter's, which aligns corners and blends only when both axes enlarge), with the row
// above and the column left of a decoded block kept so blends cross block and strip seams, and nearest at pixel
// centres along an axis the fit shrinks or keeps.
namespace GameDisplayLab {

// The files the viewer lists: PNG (.png) and JPEG (.jpg, .jpeg), any case.
enum class ImageKind : uint8_t { None, Png, Jpeg };
// The kind of file name `name` (a NUL-terminated name, no path); None for a null name, a hidden one (starting with
// '.', such as macOS's "._name.png"), or another extension.
ImageKind imageKindOf(const char* name);
// ASCII case-insensitive order of two NUL-terminated names (the Image row's order); a strict weak order.
bool nameLess(const char* a, const char* b);

// One listed file's name, with its NUL.
constexpr size_t IMAGE_NAME_BYTES = 64;
struct ImageName {
  char text[IMAGE_NAME_BYTES];
};

// The Image row's entries: entry 0 is the test chart, entries 1..files() the listed files, sorted by nameLess. Over a
// caller's array of `capacity` names (the gray viewer's heap list); a default one lists nothing.
class ImageList {
 public:
  // What add() did with a directory entry.
  enum class Add : uint8_t {
    // Kept.
    Listed,
    // A directory, or not a PNG or JPEG file (imageKindOf None).
    Ignored,
    // A name of IMAGE_NAME_BYTES or more characters (it would not fit with its NUL).
    TooLong,
    // An image while the list is full: it replaced the greatest name when it sorts before it (the list keeps the
    // first `capacity` names by nameLess, whatever the directory's order); one name was dropped either way.
    Full,
  };

  ImageList() = default;
  ImageList(ImageName* names, size_t capacity) : names_(names), capacity_(names != nullptr ? capacity : 0) {}

  // One directory entry `name` (a directory when `isDirectory`).
  Add add(const char* name, bool isDirectory);
  // The listed names in nameLess order (after the last add()).
  void sort();
  size_t files() const { return count_; }
  size_t entries() const { return 1 + count_; }
  // File `i` (0..files() - 1); null past them.
  const char* file(size_t i) const { return i < count_ ? names_[i].text : nullptr; }
  // The entry after `entry` among `entries`: the next, and after the last the chart (0).
  static size_t nextEntry(size_t entry, size_t entries);

 private:
  ImageName* names_ = nullptr;
  size_t capacity_ = 0;
  size_t count_ = 0;
};

// The converters' source limits (ImageToFramebufferDecoder.h): each side 1..32767 and at most 8 MP.
constexpr int64_t MAX_SOURCE_DIMENSION = 32767;
constexpr int64_t MAX_SOURCE_PIXELS = 8388608;
bool imageSizeAccepted(int64_t width, int64_t height);

// PNGdec's pixel types (the PNG colour types), by value; GrayImageReader.cpp static_asserts each against PNGdec.h.
enum class PngPixel : uint8_t { Grayscale = 0, Truecolor = 2, Indexed = 3, GrayAlpha = 4, TruecolorAlpha = 6 };
// The converter's isSupportedBitDepth, and a PNG colour type that exists (0, 2, 3, 4, 6; the converter lets an 8-bit
// file of type 1, 5 or 7 through to paint flat gray): 8 bits for any of them, 1, 2 or 4 for gray and indexed only.
bool pngDepthSupported(int pixelType, int bitsPerSample);

// A PNG file's chunk layout against its size, before PNGdec reads it: PNGdec 1.1.6 spins without end on a chunk that
// runs past the end of the file (its IDAT loop asks for more data, gets none, and never calls back), so a truncated
// or malformed file is refused first.
enum class PngLayout : uint8_t {
  // The signature, then chunks of length + 12 bytes each, through IEND, all inside the file.
  Complete,
  // A chunk (or IEND) past the end of the file, or a read that failed.
  Truncated,
  // Not a PNG signature, or a chunk length over 2^31 - 1.
  Malformed,
};
// Walks the chunk headers of a `fileSize`-byte PNG through `readAt(uint32_t offset, uint8_t* out, size_t n) -> bool`
// (n bytes at offset; false when they could not be read): one 8-byte read per chunk, the data skipped.
template <typename ReadAt>
PngLayout walkPngChunks(const uint32_t fileSize, ReadAt&& readAt) {
  static constexpr uint8_t SIGNATURE[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  uint8_t head[8];
  if (fileSize < 8 || !readAt(0, head, 8)) return PngLayout::Truncated;
  for (size_t i = 0; i < 8; ++i) {
    if (head[i] != SIGNATURE[i]) return PngLayout::Malformed;
  }
  uint64_t offset = 8;
  while (true) {
    // Each chunk: 4-byte length, 4-byte type, the data, 4-byte CRC.
    if (offset + 12 > fileSize) return PngLayout::Truncated;
    if (!readAt(static_cast<uint32_t>(offset), head, 8)) return PngLayout::Truncated;
    const uint32_t length = (static_cast<uint32_t>(head[0]) << 24) | (static_cast<uint32_t>(head[1]) << 16) |
                            (static_cast<uint32_t>(head[2]) << 8) | head[3];
    if (length > 0x7FFFFFFFu) return PngLayout::Malformed;
    const uint64_t end = offset + 12 + length;
    if (end > fileSize) return PngLayout::Truncated;
    if (head[4] == 'I' && head[5] == 'E' && head[6] == 'N' && head[7] == 'D') return PngLayout::Complete;
    offset = end;
  }
}
// The converter's requiredPngInternalBufferBytes: PNGdec keeps two scanlines with their filter bytes, plus 32.
int pngInternalBufferBytes(int width, int pixelType, int bitsPerSample);
// One PNG row (`pixels` as PNGdec delivers it) to `width` 8-bit gray values in `gray`, as the converter's
// convertLineToGray with no alpha line: gray = (r * 77 + g * 150 + b * 29) >> 8 for colour, low-bit samples expanded
// (sample * 255 / max), and every pixel's alpha composited on white ((gray * a + 255 * (255 - a)) / 255): a tRNS
// colour key (`hasAlpha` and the sample or RGB equal to `transparentColor`) is alpha 0, an indexed palette's alpha is
// palette[768 + index] when `hasAlpha`; an indexed row without a palette reads its samples as gray; an unknown type is
// mid-gray (128). `palette` is PNGdec's (768 RGB bytes, then 256 alpha bytes).
void pngRowToGray(const uint8_t* pixels, uint8_t* gray, int width, int pixelType, int bitsPerSample,
                  const uint8_t* palette, bool hasAlpha, uint32_t transparentColor);

// JPEGDEC's coarse scale for a target scale of `targetPermille` / 1000 (the JPEG converter's chooseJpegScale): 8 at
// or under 125, 4 at or under 250, 2 at or under 500, else 1.
int jpegScaleDenominator(uint32_t targetPermille);

// JPEGDEC's decode for a `srcW` x `srcH` JPEG shown at `fitW` x `fitH`: the denominator (jpegScaleDenominator of the
// smaller of the two axes' scales; 8 for a progressive JPEG, which JPEGDEC decodes DC-only at an eighth whatever is
// asked), decode()'s option for it (0, or JPEG_SCALE_HALF, _QUARTER, _EIGHTH by value: 2, 4, 8; GrayImageReader.cpp
// static_asserts them), and the scaled source's size (each side rounded up, as JPEGDEC's MCUs cover it).
struct JpegScale {
  int denominator;
  int option;
  int scaledW;
  int scaledH;
};
JpegScale jpegScaleFor(int srcW, int srcH, int fitW, int fitH, bool progressive);

// Where a picture lands on the logical screen.
struct FitRect {
  int x;
  int y;
  int w;
  int h;
};
// `srcW` x `srcH` scaled to fit `screenW` x `screenH` (up or down), its aspect kept (the side that limits fills the
// screen, the other rounded to the nearest pixel, at least 1), centred; the rest is the white letterbox. All zero when
// any size is 0 or less.
FitRect fitToScreen(int srcW, int srcH, int screenW, int screenH);

// One axis of the fit: `dst` output pixels sampled from `src` source pixels, nearest at pixel centres (both > 0).
struct FitAxis {
  int src;
  int dst;
  // The source pixel output pixel `d` (0..dst-1) shows: floor((2d + 1) * src / (2 * dst)), non-decreasing in d.
  int srcOf(int d) const;
  // The first output pixel whose source is `s` or later (0..dst): output pixels [firstDst(s), firstDst(s + 1)) show
  // source pixel s (an empty range when downscaling skips it).
  int firstDst(int s) const;

  // The two source pixels output pixel `d` blends and the second's weight in 1/65536: enlarging (dst > src), the
  // pixels either side of its centre's source position ((2d + 1) * src / (2 * dst) - 1/2), clamped to the source's
  // edges (one pixel, weight 0, past them); otherwise srcOf(d) alone (weight 0). s1 is non-decreasing in d.
  struct Tap {
    int s0;
    int s1;
    uint32_t weight;
  };
  Tap tap(int d) const;
  bool enlarges() const { return dst > src; }
};

// Output pixels [first, end).
struct Span {
  int first;
  int end;
  bool empty() const { return first >= end; }
};
// The output pixels whose last needed source pixel (tap(d).s1) lies in source pixels [srcStart, srcEnd): the ones a
// decoded block covering them makes, once the source pixel before it is kept too (SourceBlock's above or left). Spans
// of adjacent blocks partition the output; when the fit shrinks, a block may make none.
Span outputsEndingIn(const FitAxis& axis, int srcStart, int srcEnd);
// The output rows a row-by-row decoder (PNGdec, top to bottom) can make once row `s` and the row before it have
// arrived: outputsEndingIn(axis, s, s + 1). Spans for s = 0, 1, ... partition the output.
Span rowsReadyAt(const FitAxis& axis, int s);

// A decoded block of source pixels and what borders it: its rows [y0, y1) and columns [x0, x1) (`pixels`, rows
// `stride` apart), the source row just above it (`above`, indexed by source column across the whole width: the
// previous JPEG strip's last row, or the previous PNG row; null at the top, or when the fit does not enlarge
// vertically), and the column just left of it (`left`, indexed by row - y0: the previous block's last column in the
// same strip; null at the left edge, or when not kept). at() clamps a source position into what is there, so a blend
// past an edge repeats the edge pixel.
struct SourceBlock {
  const uint8_t* pixels;
  int stride;
  int x0;
  int x1;
  int y0;
  int y1;
  const uint8_t* above = nullptr;
  const uint8_t* left = nullptr;
  uint8_t at(int sx, int sy) const;
};
// Output columns [dxFirst, dxEnd) of the output row whose tap is `rowTap`, from `block`: the four source pixels of the
// column's and the row's taps blended, horizontally then vertically, each step `(a * (65536 - w) + b * w) >> 16` (the
// JPEG converter's 16.16 arithmetic); along an axis that does not enlarge its tap is one pixel with weight 0, so that
// axis samples the nearest pixel at centres.
void sampleBlock(const SourceBlock& block, const FitAxis& ax, const FitAxis::Tap& rowTap, int dxFirst, int dxEnd,
                 uint8_t* out);

}  // namespace GameDisplayLab
