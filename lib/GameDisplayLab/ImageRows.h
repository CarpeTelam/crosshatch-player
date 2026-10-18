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

// A file's layout against its size, walked before a decoder reads it (the first of the reader's three layers against
// a decoder that never returns: complete walks, reads that cannot stall it, bounded exits). PNGdec 1.1.6 spins
// without end on a chunk that runs past the end of the file (png.inl 885-960: its IDAT loop asks for more data, gets
// none and never calls back), and copies PLTE and tRNS into a 1,024-byte palette unbounded (png.inl 850, 868);
// JPEGDEC parses its header up to SOS (jpeg.inl 1611).
enum class FileLayout : uint8_t {
  // Whole: a PNG's chunks through IEND and every byte after it in whole chunks too (fewer than 8 trailing bytes
  // tolerated); a JPEG's marker segments from SOI through SOS (the scan data after it is the decoder's).
  Complete,
  // Under a PNG signature (8 bytes), or a JPEG's SOI and one marker (4).
  TooShort,
  // A chunk or segment running past the end of the file.
  Truncated,
  // Whole chunks or segments that end without IEND (PNG) or SOS (JPEG).
  NoEnd,
  // Not the format's signature, a chunk length over 2^31 - 1, a PLTE over 768 bytes or not a multiple of 3, a tRNS
  // over 256, a segment length under 2, a marker where none can be, or past MAX_LAYOUT_CHUNKS chunks or segments.
  Malformed,
  // A read of the file failed.
  ReadFailed,
  // The stop predicate said stop.
  Stopped,
};
constexpr uint32_t MAX_LAYOUT_CHUNKS = 65536;

// Walks a `fileSize`-byte PNG's chunk headers to the end of the file through `readAt(uint32_t offset, uint8_t* out,
// size_t n) -> bool` (n bytes at offset; false when they could not be read): one 8-byte read per chunk, the data
// skipped; `stopped() -> bool` is asked before each chunk.
template <typename ReadAt, typename Stopped>
FileLayout walkPngChunks(const uint32_t fileSize, ReadAt&& readAt, Stopped&& stopped) {
  static constexpr uint8_t SIGNATURE[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  uint8_t head[8];
  if (fileSize < 8) return FileLayout::TooShort;
  if (!readAt(0, head, 8)) return FileLayout::ReadFailed;
  for (size_t i = 0; i < 8; ++i) {
    if (head[i] != SIGNATURE[i]) return FileLayout::Malformed;
  }
  uint64_t offset = 8;
  bool ended = false;
  for (uint32_t chunks = 0;; ++chunks) {
    if (stopped()) return FileLayout::Stopped;
    const uint64_t left = fileSize - offset;
    // Fewer than a chunk header's 8 bytes left: the end, after IEND; otherwise no IEND (nothing left) or a cut.
    if (left < 8) {
      if (ended) return FileLayout::Complete;
      return left == 0 ? FileLayout::NoEnd : FileLayout::Truncated;
    }
    if (chunks >= MAX_LAYOUT_CHUNKS) return FileLayout::Malformed;
    // Each chunk: 4-byte length, 4-byte type, the data, 4-byte CRC.
    if (!readAt(static_cast<uint32_t>(offset), head, 8)) return FileLayout::ReadFailed;
    const uint32_t length = (static_cast<uint32_t>(head[0]) << 24) | (static_cast<uint32_t>(head[1]) << 16) |
                            (static_cast<uint32_t>(head[2]) << 8) | head[3];
    if (length > 0x7FFFFFFFu) return FileLayout::Malformed;
    const uint64_t end = offset + 12 + length;
    if (end > fileSize) return FileLayout::Truncated;
    const auto is = [&head](const char* type) {
      return head[4] == type[0] && head[5] == type[1] && head[6] == type[2] && head[7] == type[3];
    };
    if (is("PLTE") && (length > 768 || length % 3 != 0)) return FileLayout::Malformed;
    if (is("tRNS") && length > 256) return FileLayout::Malformed;
    if (is("IEND")) ended = true;
    offset = end;
  }
}

// Walks a `fileSize`-byte JPEG's header from SOI to SOS through `readAt` (as walkPngChunks'), asking `stopped()`
// before each marker, at the offsets JPEGDEC's header parse (JPEGParseInfo, jpeg.inl 1611-1762) visits, so every byte
// it parses is inside the file: a marker and its two-byte length (jpeg.inl 1638-1640), then the segment, must lie
// inside the file. As JPEGParseInfo: a value under 0xFFC0, or 0xFFFF (a fill byte), is no marker and is stepped over
// with the byte after it, three bytes in all (jpeg.inl 1642-1646), so fill bytes are walked as JPEGDEC walks them; SOI,
// RSTn and any other marker from 0xFFC0 up are read with a length (its default, jpeg.inl 1762). An EOI before SOS is
// NoEnd. The segments JPEGDEC copies into fixed tables are checked too (Malformed otherwise): a SOF0 or SOF2 of 1 to 4
// components and length 8 + 3 per component (it fills a 4-entry component table, jpeg.inl 1679-1712), and a DQT of
// whole 65- or 129-byte tables (its 16-bit count wraps otherwise, jpeg.inl 1728-1759).
template <typename ReadAt, typename Stopped>
FileLayout walkJpegSegments(const uint32_t fileSize, ReadAt&& readAt, Stopped&& stopped) {
  uint8_t two[2];
  if (fileSize < 4) return FileLayout::TooShort;
  if (!readAt(0, two, 2)) return FileLayout::ReadFailed;
  if (two[0] != 0xFF || two[1] != 0xD8) return FileLayout::Malformed;
  const auto u16 = [&two] { return static_cast<uint32_t>((two[0] << 8) | two[1]); };
  uint64_t offset = 2;
  for (uint32_t segments = 0;; ++segments) {
    if (stopped()) return FileLayout::Stopped;
    if (segments >= MAX_LAYOUT_CHUNKS) return FileLayout::Malformed;
    if (offset >= fileSize) return FileLayout::NoEnd;
    if (offset + 2 > fileSize) return FileLayout::Truncated;
    if (!readAt(static_cast<uint32_t>(offset), two, 2)) return FileLayout::ReadFailed;
    const uint32_t marker = u16();
    if (marker < 0xFFC0 || marker == 0xFFFF) {
      offset += 3;
      continue;
    }
    if (marker == 0xFFD9) return FileLayout::NoEnd;
    if (offset + 4 > fileSize) return FileLayout::Truncated;
    if (!readAt(static_cast<uint32_t>(offset + 2), two, 2)) return FileLayout::ReadFailed;
    const uint32_t length = u16();
    if (length < 2) return FileLayout::Malformed;
    const uint64_t end = offset + 2 + length;
    if (end > fileSize) return FileLayout::Truncated;
    if (marker == 0xFFDA) return FileLayout::Complete;  // SOS: the header is whole
    if (marker == 0xFFC0 || marker == 0xFFC2) {
      // Precision, height, width, then the component count.
      uint8_t count = 0;
      if (length < 8 || !readAt(static_cast<uint32_t>(offset + 9), &count, 1)) {
        return length < 8 ? FileLayout::Malformed : FileLayout::ReadFailed;
      }
      if (count < 1 || count > 4 || length != 8 + 3u * count) return FileLayout::Malformed;
    } else if (marker == 0xFFDB) {
      uint64_t table = offset + 4;
      while (table < end) {
        uint8_t precision = 0;
        if (!readAt(static_cast<uint32_t>(table), &precision, 1)) return FileLayout::ReadFailed;
        table += (precision & 0xF0) != 0 ? 129 : 65;
      }
      if (table != end) return FileLayout::Malformed;
    }
    offset = end;
  }
}

// The reader's answers to a decoder's read (the second layer: a read can never stall a decoder). How many bytes of a
// `requested`-byte read at `position` may come from the file: none once the read starts at or past `walkedSize` (the
// size the walk checked, which is also the size the decoder is given) or `stopped`, else up to the walked end.
int32_t readAllowance(uint32_t position, int32_t requested, uint32_t walkedSize, bool stopped);
// PNGdec's read answer: the file gave `got` bytes of `requested` (negative: an error, taken as 0); the rest are filled
// with 0xFF and `requested` is returned, so PNGdec never sees a short read. Its IDAT loop then always moves on
// (png.inl 885-904 subtracts what was read from the chunk length), an all-0xFF chunk length is invalid
// (png.inl 828: negative), and inflate meets bad data (png.inl 953: Z_DATA_ERROR ends the loop) or makes rows, whose
// draw callback sees the stop.
int32_t fillPngRead(uint8_t* buf, int32_t requested, int32_t got);
// JPEGDEC's read answer: `got` with a negative taken as 0. Not filler: JPEGParseInfo's marker loop (jpeg.inl 1611)
// compares a buffer offset, kept under half its 2 KiB buffer by each refill, with the file size, so in a file over
// 1 KiB endless 0xFF filler keeps it reading (0xFFFF is stepped over, jpeg.inl 1642); an empty read instead leaves the
// buffer short, which ends it (jpeg.inl 1623: offset past the bytes read, a decode error) or runs its offset past the
// size. DecodeJPEG's MCU loops are bounded whatever the data (JPEGGetMoreData, called at jpeg.inl 5368, reads only
// below the file size: jpeg.inl 1558).
int32_t clampJpegRead(int32_t got);

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

// The seams between a decoder's blocks, kept for both readers: a decoder delivers blocks left to right along a strip
// of rows, strips top to bottom (PNGdec: one row a strip, one block a row; JPEGDEC: a strip of MCU rows, `height` up to
// 16 and the last one trimmed, in blocks `stride` wide of which `widthUsed` are valid at the right edge). A blend needs
// the source row above a strip and the column left of a block, so the keeper holds the previous strip's last row (the
// whole width) and the previous block's last column in this strip, and hands them out with each block.
class SeamKeeper {
 public:
  // A block taller than this keeps no left column (an MCU row is at most 16 pixels: 8 x 2 vertical subsampling).
  static constexpr int EDGE_ROWS = 16;

  // A source `width` wide. `rowA` and `rowB` are two caller buffers of `width` bytes, the strip rows, or both null
  // when no blend reaches the row above (the fit does not enlarge vertically); `keepLeft` when a blend reaches the
  // column to the left (it enlarges horizontally).
  void reset(int width, uint8_t* rowA, uint8_t* rowB, bool keepLeft);
  // One decoder block: its top-left source pixel (x, y), `widthUsed` valid columns of `height` rows, rows `stride`
  // bytes apart. A block at a new y starts a strip: the previous strip's last row becomes the row above. The result
  // has `above` once a strip has ended (and strip rows are kept) and `left` when the previous block in this strip
  // ended at x (and a left column is kept).
  SourceBlock begin(const uint8_t* pixels, int x, int y, int widthUsed, int height, int stride);
  // After the block's outputs are sampled: keeps its last row (into the strip being built) and its last column.
  void end(const SourceBlock& block);

 private:
  int width_ = 0;
  uint8_t* above_ = nullptr;
  uint8_t* current_ = nullptr;
  bool haveAbove_ = false;
  bool keepLeft_ = false;
  int stripY_ = -1;
  uint8_t edge_[EDGE_ROWS] = {};
  // Where the previous block in this strip ended (-1: none, or its column was not kept).
  int edgeEndX_ = -1;
};
}  // namespace GameDisplayLab
