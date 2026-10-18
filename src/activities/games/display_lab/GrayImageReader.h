#pragma once

#include "ScanBackend.h"  // CROSSHATCH_DISPLAY_LAB

#if CROSSHATCH_DISPLAY_LAB

#include <ImageRows.h>

#include <cstdint>

// The gray viewer's image reader (story dlab-g3, the epic's D16 amendment): a PNG or JPEG file from the SD card as
// 8-bit gray rows fitted to the logical screen. The firmware's converters (lib/Epub/Epub/converters) cut gray to two
// bits before they write the 1 bpp framebuffer, so this reader drives the same libraries, PNGdec and JPEGDEC at the
// pinned versions, directly: its files are opened through Storage as the converters open them, a PNG row is converted
// to gray as the PNG converter converts one (GameDisplayLab::pngRowToGray: every pixel type and depth it accepts, alpha
// composited on white), a JPEG is decoded as 8-bit gray at JPEGDEC's coarse scale as the JPEG converter picks it (a
// progressive one at an eighth, its DC-only decode; GameDisplayLab::jpegScaleFor), with the converters' size limits and
// free-heap floors (taken from the decoders' real sizes). Against a decoder that never returns or writes past its
// object there are four layers: the file's layout is walked against its size first (walkPngChunks through a PNG's
// IEND, walkJpegSegments from a JPEG's SOI to SOS as JPEGDEC parses it, then its EOI), and the walked size ends there;
// every read is answered so it cannot stall the decoder (DecoderReads.h over readAllowance, fillPngRead,
// answerJpegHeaderRead, answerJpegDecodeRead, the read caps: nothing past the walked size, which is the size the
// decoder is given, nothing once stopped, PNGdec's span filled with 0xFF, JPEGDEC's with zeros, a dropped JPEG header
// read zeroing its whole parse buffer); the sweep task's owner waits for a stop only so long (GrayRunner::endLocked
// detaches); and the task watchdog, which the sweep task joins for the whole read and feeds on progress (a row, a read
// that gave file bytes), leaving it around each blocking file call, resets the device when a decoder spins where no
// read or row reaches it or reads on without progress. The decoders carry the fork's build-time patches
// (scripts/pngdec_patches, scripts/jpegdec_patches).
// Rows are scaled to GameDisplayLab::fitToScreen's rectangle: bilinear along an axis the fit enlarges (only the 16.16
// blend arithmetic is the JPEG converter's), nearest at pixel centres along one it shrinks (FitAxis::tap, sampleBlock);
// a PNG row completes the output rows rowsReadyAt names, a JPEG block the ones outputsEndingIn names, with the row
// above (the previous PNG row, the previous JPEG strip's last row) and the previous block's last column kept so blends
// cross seams (SeamKeeper). All of that arithmetic is pure and pinned by test_image_rows.cpp; only the library calls
// and the callbacks' glue here are device-only. A JPEG's EXIF orientation is ignored (as the converters ignore it): a
// picture is shown as its pixels are stored. The renderer's framebuffer is not touched. Only GrayImageReader.cpp
// includes PNGdec.h and JPEGDEC.h (scripts/check_layers.py's FILE_EDGES, D16).
//
// It runs on the caller's task (the gray viewer's sweep task): it yields one tick at most every YIELD_MS of progress so
// lower-priority tasks on its core run, as the converters do, and checks the sink's stopped() before every output row,
// so a stop waits for at most one row's decode and SD read.

// Where the reader's rows go.
class GrayRowSink {
 public:
  // One row segment: logical row `y`, columns [x0, x0 + count), `count` grays (0 black .. 255 white), valid only
  // during the call.
  virtual void row(int y, int x0, const uint8_t* gray, int count) = 0;
  // End the decode early (a stop was asked for).
  virtual bool stopped() const = 0;

 protected:
  ~GrayRowSink() = default;
};

// How a read ended.
enum class ImageRead : uint8_t {
  Done,
  // The file would not open, a read of it failed (in the walk, or a JPEG header read the reader dropped), or the
  // library rejected its header as corrupt.
  Open,
  // A size, depth, colour type or width outside what the converters accept, a file over 2 GiB, a feature the library
  // does not decode (an interlaced or 16-bit PNG, an extended or lossless JPEG), or a truncated or malformed file
  // (GameDisplayLab::FileLayout Truncated and Malformed: a chunk or segment past its end, a bad signature or length, an
  // oversized PLTE or tRNS, a JPEG table, SOF, SOS or Exif IFD JPEGDEC would read past or build past its tables, too
  // many chunks, segments or buffer moves).
  Format,
  // Under a signature's worth of bytes (8 for a PNG, 4 for a JPEG).
  Short,
  // Whole chunks or segments without the end the decoder needs: no IEND (a PNG); no SOS, or no EOI after it (a JPEG).
  NoEnd,
  // The decoder or a row buffer did not fit (the converters' free-heap floor included).
  Memory,
  // The decode itself failed.
  Decode,
  // The sink asked to stop.
  Stopped,
};

// The read's name in the log.
const char* imageReadName(ImageRead read);

// What the read found: the source size, the rectangle it painted, JPEGDEC's scale denominator (1 for a PNG), whether
// it was a progressive JPEG, and the milliseconds of the decode and the paint into the sink together.
struct ImageReadInfo {
  int width = 0;
  int height = 0;
  GameDisplayLab::FitRect fit{0, 0, 0, 0};
  int jpegScale = 1;
  bool progressive = false;
  uint32_t ms = 0;
};

// Reads `path` (a `kind` file, Png or Jpeg) fitted to a logical screen `screenW` x `screenH` into `sink`: every pixel
// of the fitted rectangle once, a PNG in whole rows top to bottom, a JPEG in one segment per decoded block and row (the
// letterbox is not sent); `info` says what it found.
ImageRead readGrayImage(const char* path, GameDisplayLab::ImageKind kind, int screenW, int screenH, GrayRowSink& sink,
                        ImageReadInfo& info);

#endif  // CROSSHATCH_DISPLAY_LAB
