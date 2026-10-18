#pragma once

#include <cstddef>
#include <cstdint>

// The gray viewer's decode arithmetic (story dlab-g3, the epic's D16 and its amendment), pure and pinned by
// test_image_rows.cpp. The device reader (src/activities/games/display_lab/GrayImageReader.cpp) drives PNGdec and
// JPEGDEC directly, because the firmware's converters cut gray to 2 bits before they write; everything it computes
// is here: which files it reads, the converters' size limits and PNG checks, a PNG row to 8-bit gray as the converters
// convert one (lib/Epub/Epub/converters/PngToFramebufferConverter.cpp, convertLineToGray with no alpha line: alpha
// composited on white), JPEGDEC's coarse scale and option as the JPEG converter picks them, the fit to the logical
// screen, which output pixels a decoded row or block makes, and their sampling: bilinear along an axis the fit
// enlarges, as the JPEG converter enlarges (neighbours clamped inside the decoded block), nearest at pixel centres
// along one it shrinks or keeps.
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
// The converter's isSupportedBitDepth: 8 bits for any type, 1, 2 or 4 for gray and indexed only.
bool pngDepthSupported(int pixelType, int bitsPerSample);
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
// The output pixels whose nearest source pixel lies in source pixels [srcStart, srcEnd): the ones a decoded block
// covering them makes (a JPEG MCU block; an empty span when shrinking skips it). Spans of adjacent blocks partition
// the output.
Span outputSpan(const FitAxis& axis, int srcStart, int srcEnd);
// The output rows whose last needed source row (tap(d).s1) is `s`: the ones a row-by-row decoder (PNGdec, top to
// bottom) can make once row `s` and the row before it have arrived. Spans for s = 0, 1, ... partition the output.
Span rowsReadyAt(const FitAxis& axis, int s);
// Output columns [dxFirst, dxEnd) of one output row, blended from source rows `row0` and `row1` (indexed from source
// column `winStart`; the same row twice when the row is not blended) with `row1`'s weight `rowWeight` (1/65536):
// along x by `ax`'s taps, each source column clamped into [winStart, winEnd) (a decoded block's columns, or the whole
// row). Bilinear as the JPEG converter's: horizontal first, each step `(a * (65536 - w) + b * w) >> 16`.
void sampleRow(const uint8_t* row0, const uint8_t* row1, uint32_t rowWeight, const FitAxis& ax, int winStart,
               int winEnd, int dxFirst, int dxEnd, uint8_t* out);

}  // namespace GameDisplayLab
