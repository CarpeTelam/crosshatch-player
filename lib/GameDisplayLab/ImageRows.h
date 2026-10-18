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

// A file's layout against its size, walked before a decoder reads it (the first of the reader's four layers against
// a decoder that never returns or reads past its object: complete walks, reads that cannot stall it, bounded exits,
// the task watchdog). PNGdec 1.1.6 spins
// without end on a chunk that runs past the end of the file (png.inl 885-960: its IDAT loop asks for more data, gets
// none and never calls back), and copies PLTE and tRNS into a 1,024-byte palette unbounded (png.inl 850, 868);
// JPEGDEC parses its header up to SOS (jpeg.inl 1611) from a 2 KiB buffer, copying tables out of it unchecked.
enum class FileLayout : uint8_t {
  // Whole: a PNG's chunks through IEND; a JPEG's marker segments from SOI through SOS, then an EOI. The walked size
  // (WalkExtent::end) stops there: the decoder is given nothing after it.
  Complete,
  // Under a PNG signature (8 bytes), or a JPEG's SOI and one marker (4).
  TooShort,
  // A chunk or segment running past the end of the file.
  Truncated,
  // Whole chunks or segments that end without IEND (PNG); no SOS, or no EOI after it (JPEG).
  NoEnd,
  // Not the format's signature, a chunk length over 2^31 - 1, a PLTE over 768 bytes or not a multiple of 3, a tRNS
  // over 256, a segment length under 2, a table, Exif IFD or SOS JPEGDEC would read past, an over-subscribed Huffman
  // table (walkJpegSegments), past MAX_LAYOUT_CHUNKS chunks or segments, or past MAX_JPEG_WINDOW_MOVES buffer moves.
  Malformed,
  // A read of the file failed.
  ReadFailed,
  // The stop predicate said stop.
  Stopped,
};
constexpr uint32_t MAX_LAYOUT_CHUNKS = 65536;

// What a Complete walk found.
struct WalkExtent {
  // The walked size, the bytes the decoder is given: a PNG through IEND's CRC (PNGdec has no IEND case: chunks after
  // it, a PLTE among them, would be parsed); a JPEG through the first EOI after SOS.
  uint32_t end = 0;
  // A JPEG's header end, the SOS segment's: a read from there on is the decode's (GrayImageReader.cpp).
  uint32_t headerEnd = 0;
  // How often JPEGParseInfo moves its buffer on over the header: a read each, the bound of its header reads.
  uint32_t windowMoves = 0;
};

// Walks a `fileSize`-byte PNG's chunk headers through IEND through `readAt(uint32_t offset, uint8_t* out, size_t n)
// -> bool` (n bytes at offset; false when they could not be read): one 8-byte read per chunk, the data skipped;
// `stopped() -> bool` is asked before each chunk. Complete at a whole IEND, with `extent->end` its end: what follows
// IEND is never given to PNGdec (its reads there are filler, which reads as an invalid chunk length).
template <typename ReadAt, typename Stopped>
FileLayout walkPngChunks(const uint32_t fileSize, ReadAt&& readAt, Stopped&& stopped, WalkExtent* extent = nullptr) {
  static constexpr uint8_t SIGNATURE[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  uint8_t head[8];
  if (fileSize < 8) return FileLayout::TooShort;
  if (!readAt(0, head, 8)) return FileLayout::ReadFailed;
  for (size_t i = 0; i < 8; ++i) {
    if (head[i] != SIGNATURE[i]) return FileLayout::Malformed;
  }
  uint64_t offset = 8;
  for (uint32_t chunks = 0;; ++chunks) {
    if (stopped()) return FileLayout::Stopped;
    const uint64_t left = fileSize - offset;
    // Fewer than a chunk header's 8 bytes left before IEND: none at all (no IEND), or a cut.
    if (left < 8) return left == 0 ? FileLayout::NoEnd : FileLayout::Truncated;
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
    if (is("IEND")) {
      if (extent) extent->end = static_cast<uint32_t>(end);
      return FileLayout::Complete;
    }
    offset = end;
  }
}

// JPEGParseInfo's file buffer (JPEG_FILE_BUF_SIZE, 2 KiB): it parses every header segment from it, and moves it on
// (a move down, or a seek and a read, jpeg.inl 1613-1636) whenever the parse offset is half of it or more.
constexpr uint32_t JPEG_PARSE_BUFFER = 2048;
// The most buffer moves a JPEG header may take (each is one read of JPEGParseInfo's): a camera's APP1, APP2 ICC
// profile and maker notes take a few dozen; more is refused.
constexpr uint32_t MAX_JPEG_WINDOW_MOVES = 256;
// The buffer the reader gives walkJpegSegments for its EOI search (on the heap: the walk's own frame stays small).
constexpr size_t JPEG_EOI_SEARCH_BYTES = 4096;

// Walks a `fileSize`-byte JPEG's header from SOI to SOS through `readAt` (as walkPngChunks'), asking `stopped()`
// before each marker, at the offsets JPEGDEC's header parse (JPEGParseInfo, jpeg.inl 1611-1762) visits, and with its
// buffer where it has it, so every byte it parses is inside the file and inside that buffer:
// - a marker and its two-byte length (jpeg.inl 1638-1640), then the segment, must lie inside the file. As
//   JPEGParseInfo: a value under 0xFFC0, or 0xFFFF (a fill byte), is no marker and is stepped over with the byte after
//   it, three bytes in all (jpeg.inl 1642-1646), so fill bytes are walked as JPEGDEC walks them; SOI, RSTn and any
//   other marker from 0xFFC0 up are read with a length (its default, jpeg.inl 1762). An EOI before SOS is NoEnd.
// - the segments JPEGDEC reads out of its buffer lie wholly in it (its buffer start is tracked as it moves it, at most
//   MAX_JPEG_WINDOW_MOVES times): SOF0 or SOF2 of 1 to 4 components and length 8 + 3 per component (a 4-entry
//   component table, jpeg.inl 1679-1712); DQT of whole 65- or 129-byte tables (its 16-bit count wraps otherwise,
//   jpeg.inl 1732-1757); DHT of whole tables, each a class 0 or 1 and index 0 or 1 byte (any other class or an index
//   past 7 never shortens its loop's count, jpeg.inl 844-870; JPEGDEC keeps two DC and two AC tables, and builds a DC
//   table of index 2 or 3 past its two into the AC tables, jpeg.inl 1093-1100), sixteen counts summing to at most
//   256, code lengths within the Kraft bound (an over-subscribed table makes JPEGMakeHuffTables write past its
//   tables, jpeg.inl 1066-1275), and values within the legal maxima (a DC category at most 11, an AC size at most
//   10: the lab's JPEGDEC patch's block margin rests on them); SOS
//   (jpeg.inl 1764-1768). An APP1 JPEGDEC takes for Exif ('E', 'x' and a byte order 'M' or 'I' where it looks,
//   jpeg.inl 1654-1677) must hold, inside the segment and the buffer, the IFD offset and tag count it reads, each
//   IFD's count and its 12-byte entries (GetTIFFInfo, jpeg.inl 1346-1376), and the next IFD's offset when it follows
//   it, its sum with the buffer offset taken as JPEGDEC's int takes it.
// - SOS, after a SOF0 or SOF2, lies in the buffer and is what JPEGGetSOS takes whole (1 to 4 components, length
//   6 + 2 per component, the length checked before the count is read): each component one of the frame's, once, its
//   DC and AC table index 0 or 1 (a higher index makes JPEGDEC decode from tables that are not there, an AC index of 3
//   about 20 KB past its object); a baseline scan the whole frame (every component, both tables defined by an earlier
//   DHT, Ss 0, Se 63, Ah and Al 0); a progressive file's first scan, the one JPEGDEC decodes, DC only and first (Ss and
//   Se 0, Ah 0, its DC tables defined).
// Malformed otherwise. After SOS the picture must end: a baseline file's last two bytes when they are FF D9 and no MPF
// APP2 says more pictures follow, else its first FF D9 after SOS (a motion photo's video, a second picture such as an
// MPF preview, or padding after it are not the decoder's); a progressive file's first marker after its first scan
// other than RSTn (JPEGDEC decodes that scan alone), or its first 2 KiB if that is further (JPEGParseInfo refuses a
// first read under 256 bytes, jpeg.inl 1612). Read forward once in reads of the caller's `search` buffer
// (`searchBytes`, JPEG_EOI_SEARCH_BYTES from the reader; a 17-byte one of the walk's own when null or smaller), a 0xFF
// ending one read carried into the next, fill bytes (0xFF 0xFF) and stuffed bytes (0xFF 00) no marker; NoEnd when
// there is none (a scan cut short would leave JPEGDEC decoding past the data it has). `extent` gets the walked size
// (that end), the header's end (SOS's end) and the buffer moves. The walk's own frame holds 17 bytes of buffer.
template <typename ReadAt, typename Stopped>
FileLayout walkJpegSegments(const uint32_t fileSize, ReadAt&& readAt, Stopped&& stopped, WalkExtent* extent,
                            uint8_t* search, const size_t searchBytes) {
  uint8_t b[17];
  if (fileSize < 4) return FileLayout::TooShort;
  if (!readAt(0, b, 2)) return FileLayout::ReadFailed;
  if (b[0] != 0xFF || b[1] != 0xD8) return FileLayout::Malformed;
  const auto u16 = [](const uint8_t* q, const bool motorola = true) {
    return motorola ? static_cast<uint32_t>((q[0] << 8) | q[1]) : static_cast<uint32_t>(q[0] | (q[1] << 8));
  };
  const auto u32 = [](const uint8_t* q, const bool motorola) {
    return motorola ? (static_cast<uint32_t>(q[0]) << 24) | (static_cast<uint32_t>(q[1]) << 16) |
                          (static_cast<uint32_t>(q[2]) << 8) | q[3]
                    : (static_cast<uint32_t>(q[3]) << 24) | (static_cast<uint32_t>(q[2]) << 16) |
                          (static_cast<uint32_t>(q[1]) << 8) | q[0];
  };
  uint64_t bufStart = 0;
  uint32_t moves = 0;
  uint64_t offset = 2;
  // What the scan needs from before it: the frame (SOF0 or SOF2, its component ids), the Huffman tables defined (a bit
  // per index, DC and AC), and an MPF APP2 (the file holds more than one picture).
  bool frame = false;
  bool progressive = false;
  uint8_t componentIds[4] = {};
  uint8_t components = 0;
  uint8_t dcDefined = 0;
  uint8_t acDefined = 0;
  bool mpf = false;
  for (uint32_t segments = 0;; ++segments) {
    if (stopped()) return FileLayout::Stopped;
    if (segments >= MAX_LAYOUT_CHUNKS) return FileLayout::Malformed;
    if (offset >= fileSize) return FileLayout::NoEnd;
    if (offset - bufStart >= JPEG_PARSE_BUFFER / 2) {
      bufStart = offset;
      if (++moves > MAX_JPEG_WINDOW_MOVES) return FileLayout::Malformed;
    }
    if (offset + 2 > fileSize) return FileLayout::Truncated;
    if (!readAt(static_cast<uint32_t>(offset), b, 2)) return FileLayout::ReadFailed;
    const uint32_t marker = u16(b);
    if (marker < 0xFFC0 || marker == 0xFFFF) {
      offset += 3;
      continue;
    }
    if (marker == 0xFFD9) return FileLayout::NoEnd;
    if (offset + 4 > fileSize) return FileLayout::Truncated;
    if (!readAt(static_cast<uint32_t>(offset + 2), b, 2)) return FileLayout::ReadFailed;
    const uint32_t length = u16(b);
    if (length < 2) return FileLayout::Malformed;
    const uint64_t end = offset + 2 + length;
    if (end > fileSize) return FileLayout::Truncated;
    const uint64_t bufEnd = bufStart + JPEG_PARSE_BUFFER;
    // [from, to) is inside the segment and inside JPEGDEC's buffer.
    const auto held = [&](const uint64_t from, const uint64_t to) { return from <= to && to <= end && to <= bufEnd; };
    if (marker == 0xFFDA) {
      // The segment's length first (a component count needs at least one component's bytes after it), so a cut or
      // short SOS is Malformed, never a read past it.
      if (end > bufEnd || !frame || length < 8) return FileLayout::Malformed;
      // The scan's components, each in the frame once with a DC and an AC table of index 0 or 1. A baseline scan is
      // the whole frame: every frame component, both tables of each defined by an earlier DHT, Ss 0, Se 63, Ah and
      // Al 0. A progressive file's first scan, the one JPEGDEC decodes: DC only (Ss and Se 0), first (Ah 0), its DC
      // tables defined (its AC tables unused). As JPEGGetSOS takes it.
      if (!readAt(static_cast<uint32_t>(offset + 4), b, 1)) return FileLayout::ReadFailed;
      const uint32_t count = b[0];
      if (count < 1 || count > 4 || length != 6 + 2 * count) return FileLayout::Malformed;
      if (!readAt(static_cast<uint32_t>(offset + 5), b, 2 * count + 3)) return FileLayout::ReadFailed;
      const uint32_t ss = b[2 * count];
      const uint32_t se = b[2 * count + 1];
      const uint32_t ah = b[2 * count + 2] >> 4;
      const uint32_t al = b[2 * count + 2] & 0x0F;
      if (progressive) {
        if (ss != 0 || se != 0 || ah != 0) return FileLayout::Malformed;
      } else if (ss != 0 || se != 63 || ah != 0 || al != 0 || count != components) {
        return FileLayout::Malformed;
      }
      uint32_t seen = 0;
      for (uint32_t c = 0; c < count; ++c) {
        uint32_t index = components;
        for (uint32_t f = 0; f < components; ++f) {
          if (componentIds[f] == b[2 * c]) index = f;
        }
        const uint32_t dc = b[2 * c + 1] >> 4;
        const uint32_t ac = b[2 * c + 1] & 0x0F;
        if (index == components || (seen & (1u << index)) != 0 || dc > 1 || ac > 1) return FileLayout::Malformed;
        seen |= 1u << index;
        if ((dcDefined & (1u << dc)) == 0) return FileLayout::Malformed;
        if (!progressive && (acDefined & (1u << ac)) == 0) return FileLayout::Malformed;
      }
      uint64_t eoiEnd = 0;
      // A baseline file ending in FF D9 with no MPF APP2 (no second picture) ends there.
      if (!progressive && !mpf && fileSize >= end + 2) {
        if (!readAt(fileSize - 2, b, 2)) return FileLayout::ReadFailed;
        if (b[0] == 0xFF && b[1] == 0xD9) eoiEnd = fileSize;
      }
      // Else the first EOI after SOS (baseline), or the first marker after the first scan other than RSTn
      // (progressive: JPEGDEC decodes that scan alone), read forward; fill bytes (0xFF 0xFF) and stuffed bytes
      // (0xFF 00) are no marker.
      uint8_t* const chunk = search != nullptr && searchBytes >= sizeof(b) ? search : b;
      const size_t chunkBytes = chunk == b ? sizeof(b) : searchBytes;
      bool lastFF = false;
      for (uint64_t at = end; at < fileSize && eoiEnd == 0;) {
        if (stopped()) return FileLayout::Stopped;
        const size_t n = fileSize - at < chunkBytes ? static_cast<size_t>(fileSize - at) : chunkBytes;
        if (!readAt(static_cast<uint32_t>(at), chunk, n)) return FileLayout::ReadFailed;
        for (size_t i = 0; i < n; ++i) {
          const uint8_t v = chunk[i];
          const bool scanMarker = lastFF && v != 0x00 && v != 0xFF && (v < 0xD0 || v > 0xD7);
          if (scanMarker && (progressive || v == 0xD9)) {
            eoiEnd = at + i + 1;
            break;
          }
          lastFF = v == 0xFF;
        }
        at += n;
      }
      if (eoiEnd == 0) return FileLayout::NoEnd;
      // JPEGParseInfo refuses a first read under 256 bytes: a progressive file is given at least its first 2 KiB.
      if (progressive && eoiEnd < JPEG_PARSE_BUFFER)
        eoiEnd = fileSize < JPEG_PARSE_BUFFER ? fileSize : JPEG_PARSE_BUFFER;
      if (extent) {
        extent->end = static_cast<uint32_t>(eoiEnd);
        extent->headerEnd = static_cast<uint32_t>(end);
        extent->windowMoves = moves;
      }
      return FileLayout::Complete;
    }
    if (marker == 0xFFC0 || marker == 0xFFC2) {
      // Precision, height, width, then the component count.
      if (length < 8 || end > bufEnd) return FileLayout::Malformed;
      if (!readAt(static_cast<uint32_t>(offset + 9), b, 1)) return FileLayout::ReadFailed;
      if (b[0] < 1 || b[0] > 4 || length != 8 + 3u * b[0]) return FileLayout::Malformed;
      components = b[0];
      if (!readAt(static_cast<uint32_t>(offset + 10), b, 3u * components)) return FileLayout::ReadFailed;
      for (uint32_t c = 0; c < components; ++c) componentIds[c] = b[3 * c];
      frame = true;
      progressive = marker == 0xFFC2;
    } else if (marker == 0xFFDB) {
      if (end > bufEnd) return FileLayout::Malformed;
      uint64_t table = offset + 4;
      while (table < end) {
        if (!readAt(static_cast<uint32_t>(table), b, 1)) return FileLayout::ReadFailed;
        table += (b[0] & 0xF0) != 0 ? 129 : 65;
      }
      if (table != end) return FileLayout::Malformed;
    } else if (marker == 0xFFC4) {
      if (end > bufEnd) return FileLayout::Malformed;
      uint64_t table = offset + 4;
      while (table < end) {
        if (table + 17 > end) return FileLayout::Malformed;
        if (!readAt(static_cast<uint32_t>(table), b, 17)) return FileLayout::ReadFailed;
        if ((b[0] >> 4) > 1 || (b[0] & 0x0F) > 1) return FileLayout::Malformed;
        if ((b[0] >> 4) == 0) {
          dcDefined |= static_cast<uint8_t>(1u << (b[0] & 0x0F));
        } else {
          acDefined |= static_cast<uint8_t>(1u << (b[0] & 0x0F));
        }
        // The codes of each length, canonical: after length i they may fill at most the 2^i codes of i bits.
        uint32_t codes = 0;
        uint32_t code = 0;
        for (int i = 1; i <= 16; ++i) {
          codes += b[i];
          code += b[i];
          if (code > (1u << i)) return FileLayout::Malformed;
          code <<= 1;
        }
        if (codes > 256) return FileLayout::Malformed;
        // The values: a DC category at most 11, an AC coefficient size (the low nibble) at most 10, the legal maxima
        // (JPEG_BLOCK_MARGIN in the lab's JPEGDEC patch rests on them: a block of 209 bytes at most). In reads of the
        // caller's buffer, or of the walk's own 17 bytes.
        const bool dcTable = (b[0] >> 4) == 0;
        uint8_t* const values = search != nullptr && searchBytes >= sizeof(b) ? search : b;
        const uint32_t valuesBytes = values == b ? sizeof(b) : static_cast<uint32_t>(searchBytes);
        for (uint32_t at = 0; at < codes;) {
          const uint32_t n = codes - at < valuesBytes ? codes - at : valuesBytes;
          if (!readAt(static_cast<uint32_t>(table + 17 + at), values, n)) return FileLayout::ReadFailed;
          for (uint32_t i = 0; i < n; ++i) {
            if (dcTable ? values[i] > 11 : (values[i] & 0x0F) > 10) return FileLayout::Malformed;
          }
          at += n;
        }
        table += 17 + codes;
      }
      if (table != end) return FileLayout::Malformed;
    } else if (marker == 0xFFE2) {
      // An MPF APP2 ("MPF\0"): the file holds more pictures after this one, so its last FF D9 is not this one's.
      if (length >= 6) {
        if (!readAt(static_cast<uint32_t>(offset + 4), b, 4)) return FileLayout::ReadFailed;
        mpf = mpf || (b[0] == 'M' && b[1] == 'P' && b[2] == 'F' && b[3] == 0);
      }
    } else if (marker == 0xFFE1) {
      // JPEGDEC's own test for Exif, from the segment's length field `at`: 'E' and 'x' at +2 and +3, the byte order at
      // +8 (bytes past the segment are the next one's, as in its buffer; past the file, zeros).
      const uint64_t at = offset + 2;
      if (at + 9 <= fileSize) {
        if (!readAt(static_cast<uint32_t>(at), b, 9)) return FileLayout::ReadFailed;
        const bool exif = b[2] == 'E' && b[3] == 'x' && (b[8] == 'M' || b[8] == 'I');
        if (exif) {
          const bool motorola = b[8] == 'M';
          const uint64_t tiff = at + 8;
          // The TIFF header's buffer offset, as JPEGDEC adds it to an IFD offset in its int (iOffset + 8).
          const uint32_t tiffInBuffer = static_cast<uint32_t>(tiff - bufStart);
          // The first IFD's offset (TIFF +4) and the tag count it takes from TIFF +8.
          if (!held(at, at + 18)) return FileLayout::Malformed;
          if (!readAt(static_cast<uint32_t>(at + 12), b, 6)) return FileLayout::ReadFailed;
          const uint32_t ifd = u32(b, motorola);
          const uint32_t firstCount = u16(b + 4, motorola);
          // One IFD at `start`: its count, and its entries when GetTIFFInfo reads them (a count of 1 to 256).
          const auto ifdHeld = [&](const uint64_t start, FileLayout& why) {
            if (!held(start, start + 2)) return why = FileLayout::Malformed, false;
            if (!readAt(static_cast<uint32_t>(start), b, 2)) return why = FileLayout::ReadFailed, false;
            const uint32_t count = u16(b, motorola);
            if (count >= 1 && count <= 256 && !held(start, start + 2 + 12ull * count)) {
              return why = FileLayout::Malformed, false;
            }
            return true;
          };
          FileLayout why = FileLayout::Complete;
          // GetTIFFInfo is always called on IFD0 (its offset in JPEGDEC's int with the buffer offset).
          if (ifd > 0x7FFFFFFFu - tiffInBuffer || !ifdHeld(tiff + ifd, why)) {
            return why == FileLayout::Complete ? FileLayout::Malformed : why;
          }
          if (firstCount >= 1 && firstCount < 32) {
            const uint64_t next = tiff + ifd + 2 + 12ull * firstCount;
            if (!held(next, next + 4)) return FileLayout::Malformed;
            if (!readAt(static_cast<uint32_t>(next), b, 4)) return FileLayout::ReadFailed;
            const uint32_t second = u32(b, motorola);
            // JPEGDEC follows it when it is not 0 and its sum with the buffer offset, in its int, is under the
            // buffer's end: a sum past INT32_MAX wraps negative and is followed too.
            const auto secondAt = static_cast<int32_t>(second + tiffInBuffer);
            if (second != 0 && secondAt < static_cast<int32_t>(JPEG_PARSE_BUFFER)) {
              if (second > 0x7FFFFFFFu - tiffInBuffer || !ifdHeld(tiff + second, why)) {
                return why == FileLayout::Complete ? FileLayout::Malformed : why;
              }
            }
          }
        }
      }
    }
    offset = end;
  }
}

// The reader's answers to a decoder's read (the second layer: a read can never stall a decoder). How many bytes of a
// `requested`-byte read at `position` may come from the file: none once the read starts at or past `walkedSize` (the
// size the walk checked, which is also the size the decoder is given) or `stopped`, else up to the walked end.
int32_t readAllowance(uint32_t position, int32_t requested, uint32_t walkedSize, bool stopped);
// How many reads one decode() of a `walkedSize`-byte file is answered from the file: 2 per 2 KiB of it and 64 more (a
// whole decode reads about one per 1.5 KiB); past that, every read gives nothing of the file.
uint32_t decoderReadCap(uint32_t walkedSize);
// How many reads JPEGDEC's header parse (open() up to the header's end) is answered from the file: its first read, one
// per buffer move the walk counted (WalkExtent::windowMoves), and a few more; past that, nothing.
uint32_t jpegHeaderReadCap(uint32_t windowMoves);
// PNGdec's read answer: the file gave `got` bytes of `requested` (negative: an error, taken as 0); the rest are filled
// with 0xFF and `requested` is returned, so PNGdec never sees a short read. Its IDAT loop then always moves on
// (png.inl 885-904 subtracts what was read from the chunk length), an all-0xFF chunk length is invalid
// (png.inl 828: negative), and inflate meets bad data (png.inl 953: Z_DATA_ERROR ends the loop) or makes rows, whose
// draw callback sees the stop.
int32_t fillPngRead(uint8_t* buf, int32_t requested, int32_t got);
// JPEGDEC's read answers, both zero-filling what the file did not give, never 0xFF (JPEGParseInfo's marker loop,
// jpeg.inl 1611, compares a buffer offset kept under half its 2 KiB buffer with the file size, so endless 0xFF keeps it
// reading; 0xFFFF is stepped over, jpeg.inl 1642) and never the buffer as it was (after a segment sends its offset past
// the buffer it seeks and reads at the start of it, jpeg.inl 1615-1636, and an empty read there leaves the header's
// first bytes, SOI then the APP1 marker read as a length, which send it round again without end). A read starting
// before the walked header's end is the header parse's, from there on the decode's (WalkExtent::headerEnd), so the
// header parse's last top-up (JPEGGetMoreData at jpeg.inl 1780) is already answered as the decode.
//
// The header parse's answer: `got` bytes into `buf` of a `requested`-byte read into JPEGParseInfo's buffer (always the
// buffer's tail: s + 2 KiB - requested, jpeg.inl 1597, 1634). Kept, the rest of the span is zeroed and `got` returned.
// `dropped` (a stop, a short or failed read, a seek the file refused, past the read cap; the reader then gives the
// header nothing more), the whole 2 KiB buffer is zeroed, the bytes already in it too, and 0 returned: the parse
// then steps over zeros three bytes at a time until its offset passes the bytes read (jpeg.inl 1623, a decode error),
// so no segment it began meets zeros (a DQT whose length or tables did would wrap its 16-bit count and read past its
// object, jpeg.inl 1732-1757).
int32_t answerJpegHeaderRead(uint8_t* buf, int32_t requested, int32_t got, bool dropped);
// The length JPEGFilter (jpeg.inl 1431-1538) leaves of `n` bytes with no 0xFF carried in: a 0xFF and the byte after it
// go (a marker) unless that byte is 0, which goes alone (a stuffed 0xFF stays); a final 0xFF waits for the next read.
int32_t jpegFilteredLength(const uint8_t* buf, int32_t n);
// JPEGGetMoreData's high-water mark (FILE_HIGHWATER, JPEGDEC.h) and the room past it a MCU of real data needs (a
// gray block of maximum-length codes is about 213 bytes; real ones are far shorter).
constexpr int32_t JPEG_FILE_HIGHWATER = 1536;
constexpr int32_t JPEG_MCU_MARGIN = 256;
// The restart markers (0xFF D0 to D7) dropped from `n` bytes of scan data in place, pairs taken as JPEGFilter takes
// them (a 0xFF and the byte after it; a final 0xFF kept, for the next read): the bytes left. JPEGDEC counts restarts by
// its interval, not by marker, and its filter drops them anyway; dropped before, they leave room in the refill for
// more scan data (DecoderReads.h's answerJpegRead tops it up), so a picture with a short restart interval over flat
// areas, where the markers are most of the data, never starves the decoder.
int32_t jpegDropRestartMarkers(uint8_t* buf, int32_t n);
// The decode's answer: `got` bytes of a `requested`-byte read into JPEGDEC's VLC buffer (its tail again: ucFileBuf +
// iVLCSize, iVLCSize = 2 KiB - requested, jpeg.inl 1562), the rest zeroed, and `requested` returned. JPEGGetMoreData
// (jpeg.inl 1544-1566) then tops its buffer up and moves its data down once a MCU ends past FILE_HIGHWATER
// (jpeg.inl 5367), so its bit pointer stays inside ucFileBuf (a MCU of zeros is a few dozen bytes); a short read
// would let the pointer pass the data it has, where JPEGDEC as released returns without moving it (jpeg.inl
// 1549-1550) and runs on past the buffer for every MCU left (the lab's JPEGDEC patch,
// scripts/lab_decoder_patches/jpegdec, refills between a MCU's blocks and ends the decode with an error there). The
// same holds against what JPEGFilter drops: a run of 0xFF (an erased sector) filters to nothing, so when this refill
// would leave the buffer under FILE_HIGHWATER + JPEG_MCU_MARGIN as filtered (jpegFilteredLength; the restart markers
// are already out, jpegDropRestartMarkers), each 0xFF followed by another 0xFF becomes 0 and the run filters to all
// but its last byte; a stuffed 0xFF 00 and every other byte are never touched, so real scan data is never changed
// (test_image_decoders.cpp decodes restart-interval pictures pixel-identical with and without the tame). Still needed
// with the lab patch's block refills: a scan erased whole (all 0xFF) leaves the filter nothing, every refill finds no
// data and the decode ends in an error ("did not decode"); tamed, it decodes to its end
// (ImageDecoders.AnErasedSectorInTheScanIsTamed). Data that would still starve the decoder (pairs of 0xFF 00, other
// markers) is refilled block by block by the lab patch, or ends in its decode error.
int32_t answerJpegDecodeRead(uint8_t* buf, int32_t requested, int32_t got);
// The position JPEGDEC is told after a read (JPEGFILE.iPos, which only JPEGGetMoreData reads, comparing it with the
// size, jpeg.inl 1558): the file's `position`, held under `size`, so it keeps asking (above) from the first refill
// its header parse makes (jpeg.inl 1780) to the decode's last MCU. The reads themselves go by the file's position.
int32_t jpegPositionAfterRead(uint32_t position, uint32_t size);

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
