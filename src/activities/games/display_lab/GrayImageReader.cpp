#include "GrayImageReader.h"

#if CROSSHATCH_DISPLAY_LAB

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// JPEGDEC.h and PNGdec.h each define these byte-order macros, differently, for their own sources (the converters
// include them in separate files); this file uses none of them, so JPEGDEC's are dropped before PNGdec.h defines its.
#include <JPEGDEC.h>
#undef INTELSHORT
#undef INTELLONG
#undef MOTOSHORT
#undef MOTOLONG
#include <PNGdec.h>

#include <memory>
#include <new>
#include <utility>

using GameDisplayLab::FitAxis;
using GameDisplayLab::FitRect;
using GameDisplayLab::ImageKind;
using GameDisplayLab::PngPixel;

// The pure conversion's pixel types are PNGdec's, by value.
static_assert(static_cast<int>(PngPixel::Grayscale) == PNG_PIXEL_GRAYSCALE, "PngPixel follows PNGdec");
static_assert(static_cast<int>(PngPixel::Truecolor) == PNG_PIXEL_TRUECOLOR, "PngPixel follows PNGdec");
static_assert(static_cast<int>(PngPixel::Indexed) == PNG_PIXEL_INDEXED, "PngPixel follows PNGdec");
static_assert(static_cast<int>(PngPixel::GrayAlpha) == PNG_PIXEL_GRAY_ALPHA, "PngPixel follows PNGdec");
static_assert(static_cast<int>(PngPixel::TruecolorAlpha) == PNG_PIXEL_TRUECOLOR_ALPHA, "PngPixel follows PNGdec");

namespace {

// As the converters: yield a tick at most this often during a decode, so the idle task (and its watchdog) is fed.
constexpr uint32_t YIELD_MS = 250;
// The converters' free-heap floors are the decoder object plus 16 KB of headroom (PngToFramebufferConverter.cpp,
// JpegToFramebufferConverter.cpp), but their PNG figure (44 KiB) predates the firmware's PNG_MAX_BUFFERED_PIXELS of
// 16416, which makes PNGdec's object about 59 KB (its zlib window and inflate state, the palette, the two-scanline
// buffer, the file buffer): the floors here take the objects' real sizes.
constexpr size_t FLOOR_HEADROOM = 16 * 1024;
constexpr size_t MIN_FREE_HEAP_FOR_PNG = sizeof(PNG) + FLOOR_HEADROOM;
constexpr size_t MIN_FREE_HEAP_FOR_JPEG = sizeof(JPEGDEC) + FLOOR_HEADROOM;
// The PNG converter's widest expanded gray row.
constexpr int MAX_GRAY_LINE_BYTES = PNG_MAX_BUFFERED_PIXELS / 2;
// decode()'s scale options are the denominators by value (GameDisplayLab::jpegScaleFor returns them so).
static_assert(JPEG_SCALE_HALF == 2 && JPEG_SCALE_QUARTER == 4 && JPEG_SCALE_EIGHTH == 8, "JPEGDEC's scale options");

// openFile could not open the file: PNG::open returns 0, which is PNG_SUCCESS, when its open callback fails, so the
// reader records the failure here. One reader at a time (the gray viewer's sweep task), which sets it before each
// open() and reads it after.
bool openFailed = false;

void yieldDuringDecode(uint32_t& lastYieldMs) {
  const uint32_t now = millis();
  if (now - lastYieldMs < YIELD_MS) return;
  vTaskDelay(1);
  lastYieldMs = millis();
}

// ---- file callbacks, as the converters' (the HalFile behind fHandle) ----------------------------------------------

void* openFile(const char* filename, int32_t* size) {
  // Released by closeFile.
  HalFile* f = new (std::nothrow) HalFile();
  if (!f) {
    LOG_ERR("DLAB", "OOM: image file handle");
    openFailed = true;
    return nullptr;
  }
  if (!Storage.openFileForRead("DLAB", filename, *f)) {
    delete f;
    openFailed = true;
    return nullptr;
  }
  *size = static_cast<int32_t>(f->size());
  return f;
}

// Tolerates a null handle (an open that failed), so a decoder's close() is always safe.
void closeFile(void* handle) {
  HalFile* f = reinterpret_cast<HalFile*>(handle);
  if (f) {
    f->close();
    delete f;
  }
}

int32_t pngRead(PNGFILE* pFile, uint8_t* pBuf, const int32_t len) {
  HalFile* f = reinterpret_cast<HalFile*>(pFile->fHandle);
  if (!f) return 0;
  return f->read(pBuf, static_cast<size_t>(len));
}

int32_t pngSeek(PNGFILE* pFile, const int32_t pos) {
  HalFile* f = reinterpret_cast<HalFile*>(pFile->fHandle);
  if (!f) return -1;
  return f->seek(static_cast<size_t>(pos));
}

// JPEGDEC tracks the file position in pFile->iPos itself (JPEGGetMoreData reads it), so the callbacks keep it in step
// with the file, as the JPEG converter's do: otherwise a progressive JPEG with a large header fails to parse.
int32_t jpegRead(JPEGFILE* pFile, uint8_t* pBuf, const int32_t len) {
  HalFile* f = reinterpret_cast<HalFile*>(pFile->fHandle);
  if (!f) return 0;
  const int32_t bytesRead = f->read(pBuf, static_cast<size_t>(len));
  if (bytesRead < 0) return 0;
  pFile->iPos += bytesRead;
  return bytesRead;
}

int32_t jpegSeek(JPEGFILE* pFile, const int32_t pos) {
  HalFile* f = reinterpret_cast<HalFile*>(pFile->fHandle);
  if (!f) return -1;
  if (!f->seek(static_cast<size_t>(pos))) return -1;
  pFile->iPos = pos;
  return pos;
}

// ---- the fit, shared by both decoders -----------------------------------------------------------------------------

// The fitted rectangle over a source (or JPEGDEC's scaled source) of srcW x srcH: its two axes, one output row
// segment's buffer, and the stop.
struct Fitted {
  GrayRowSink* sink = nullptr;
  FitRect fit{0, 0, 0, 0};
  FitAxis ax{1, 1};
  FitAxis ay{1, 1};
  std::unique_ptr<uint8_t[]> out;
  uint32_t lastYieldMs = 0;
  bool stopped = false;

  // False when the buffer did not fit (logged).
  bool begin(GrayRowSink& rowSink, const FitRect& rect, const int srcW, const int srcH) {
    sink = &rowSink;
    fit = rect;
    ax = FitAxis{srcW, rect.w};
    ay = FitAxis{srcH, rect.h};
    out = makeUniqueNoThrow<uint8_t[]>(static_cast<size_t>(rect.w));
    if (!out) {
      LOG_ERR("DLAB", "OOM: %d px gray viewer row buffer", rect.w);
      return false;
    }
    lastYieldMs = millis();
    return true;
  }

  // Before each output row: false (stopped) once the sink asks to stop, so a stop waits for one row's decode at most.
  bool keepGoing() {
    if (!stopped && sink->stopped()) stopped = true;
    return !stopped;
  }
};

// ---- PNG -----------------------------------------------------------------------------------------------------------

struct PngContext {
  PNG* png = nullptr;
  Fitted fitted;
  int srcW = 0;
  // The row just decoded and the one before it (a fit that enlarges blends the two).
  std::unique_ptr<uint8_t[]> current;
  std::unique_ptr<uint8_t[]> previous;
};

int pngDraw(PNGDRAW* pDraw) {
  PngContext* ctx = reinterpret_cast<PngContext*>(pDraw->pUser);
  if (!ctx) return 0;
  Fitted& f = ctx->fitted;
  yieldDuringDecode(f.lastYieldMs);
  if (!f.keepGoing()) return 0;
  // The output rows this source row completes (PNGdec delivers rows top to bottom): none when shrinking skips it, and
  // a row that only feeds the next one's blend when enlarging, which is still converted and kept.
  const GameDisplayLab::Span rows = GameDisplayLab::rowsReadyAt(f.ay, pDraw->y);
  if (rows.empty() && !f.ay.enlarges()) return 1;
  // PNGdec parses tRNS while decoding, after open() returns, so the transparent colour is read here, as the converter
  // reads it.
  GameDisplayLab::pngRowToGray(pDraw->pPixels, ctx->current.get(), ctx->srcW, pDraw->iPixelType, pDraw->iBpp,
                               pDraw->pPalette, pDraw->iHasAlpha != 0, ctx->png->getTransparentColor());
  for (int dy = rows.first; dy < rows.end; ++dy) {
    if (!f.keepGoing()) return 0;
    const FitAxis::Tap tap = f.ay.tap(dy);
    // tap.s1 is this row; tap.s0 this row or the one before.
    const uint8_t* row0 = tap.s0 == pDraw->y ? ctx->current.get() : ctx->previous.get();
    GameDisplayLab::sampleRow(row0, ctx->current.get(), tap.weight, f.ax, 0, ctx->srcW, 0, f.fit.w, f.out.get());
    f.sink->row(f.fit.y + dy, f.fit.x, f.out.get(), f.fit.w);
  }
  std::swap(ctx->current, ctx->previous);
  return 1;
}

ImageRead readPng(const char* path, const int screenW, const int screenH, GrayRowSink& sink, ImageReadInfo& info) {
  if (ESP.getFreeHeap() < MIN_FREE_HEAP_FOR_PNG) {
    LOG_ERR("DLAB", "Not enough heap for the PNG decoder (%u free, need %u)", static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(MIN_FREE_HEAP_FOR_PNG));
    return ImageRead::Memory;
  }
  std::unique_ptr<PNG> png(new (std::nothrow) PNG());
  if (!png) {
    LOG_ERR("DLAB", "OOM: PNG decoder");
    return ImageRead::Memory;
  }
  PngContext ctx;
  ctx.png = png.get();
  openFailed = false;
  const int rc = png->open(path, openFile, closeFile, pngRead, pngSeek, pngDraw);
  // Closed on every way out from here, a rejected header included (the file is open then), as the converter closes it;
  // closeFile tolerates the null handle of a failed open.
  struct Closer {
    PNG* p;
    ~Closer() { p->close(); }
  } closer{png.get()};
  if (openFailed) {
    LOG_ERR("DLAB", "PNG cannot be opened: %s", path);
    return ImageRead::Open;
  }
  if (rc != PNG_SUCCESS) {
    // The library's own rejections of what it cannot decode (an interlaced or 16-bit PNG, a size past its limits)
    // are the format's; anything else is the file's.
    const bool unsupported = rc == PNG_UNSUPPORTED_FEATURE || rc == PNG_TOO_BIG;
    LOG_ERR("DLAB", "PNG %s (%d): %s", unsupported ? "not supported" : "rejected", rc, path);
    return unsupported ? ImageRead::Format : ImageRead::Open;
  }
  info.width = png->getWidth();
  info.height = png->getHeight();
  const int pixelType = png->getPixelType();
  const int bits = png->getBpp();
  if (!GameDisplayLab::imageSizeAccepted(info.width, info.height)) {
    LOG_ERR("DLAB", "PNG %dx%d is outside the decoders' limits: %s", info.width, info.height, path);
    return ImageRead::Format;
  }
  if (!GameDisplayLab::pngDepthSupported(pixelType, bits)) {
    LOG_ERR("DLAB", "PNG %d-bit samples of pixel type %d not supported: %s", bits, pixelType, path);
    return ImageRead::Format;
  }
  // PNGdec keeps two scanlines in a fixed buffer, and the expanded gray row is capped as the converter caps it.
  const int need = GameDisplayLab::pngInternalBufferBytes(info.width, pixelType, bits);
  if (need > PNG_MAX_BUFFERED_PIXELS) {
    LOG_ERR("DLAB", "PNG %d px wide needs %d decoder buffer bytes, over the %d configured: %s", info.width, need,
            PNG_MAX_BUFFERED_PIXELS, path);
    return ImageRead::Format;
  }
  if (info.width > MAX_GRAY_LINE_BYTES) {
    LOG_ERR("DLAB", "PNG %d px wide is over the %d px gray row the converter allows: %s", info.width,
            MAX_GRAY_LINE_BYTES, path);
    return ImageRead::Format;
  }
  info.fit = GameDisplayLab::fitToScreen(info.width, info.height, screenW, screenH);
  ctx.srcW = info.width;
  ctx.current = makeUniqueNoThrow<uint8_t[]>(static_cast<size_t>(info.width));
  ctx.previous = makeUniqueNoThrow<uint8_t[]>(static_cast<size_t>(info.width));
  if (!ctx.current || !ctx.previous) {
    LOG_ERR("DLAB", "OOM: two %d byte PNG gray lines", info.width);
    return ImageRead::Memory;
  }
  if (!ctx.fitted.begin(sink, info.fit, info.width, info.height)) return ImageRead::Memory;
  const uint32_t start = millis();
  const int decoded = png->decode(&ctx, 0);
  info.ms = millis() - start;
  if (ctx.fitted.stopped) return ImageRead::Stopped;
  if (decoded != PNG_SUCCESS) {
    LOG_ERR("DLAB", "PNG decode failed (%d): %s", decoded, path);
    return ImageRead::Decode;
  }
  return ImageRead::Done;
}

// ---- JPEG ----------------------------------------------------------------------------------------------------------

int jpegDraw(JPEGDRAW* pDraw) {
  Fitted* f = reinterpret_cast<Fitted*>(pDraw->pUser);
  if (!f) return 0;
  yieldDuringDecode(f->lastYieldMs);
  if (!f->keepGoing()) return 0;
  // EIGHT_BIT_GRAYSCALE: one byte a pixel, rows `iWidth` apart, `iWidthUsed` of them valid, in JPEGDEC's scaled
  // source coordinates.
  const uint8_t* pixels = reinterpret_cast<const uint8_t*>(pDraw->pPixels);
  const int stride = pDraw->iWidth;
  const int validW = pDraw->iWidthUsed;
  const int blockH = pDraw->iHeight;
  if (stride <= 0 || validW <= 0 || blockH <= 0) return 1;
  const int bx = pDraw->x;
  const int by = pDraw->y;
  // The output pixels whose nearest source pixel is in this block (none when shrinking skips it); a blend's
  // neighbours are clamped inside the block, as the JPEG converter clamps them.
  const GameDisplayLab::Span cols = GameDisplayLab::outputSpan(f->ax, bx, bx + validW);
  const GameDisplayLab::Span rows = GameDisplayLab::outputSpan(f->ay, by, by + blockH);
  if (cols.empty() || rows.empty()) return 1;
  // A source row's pixels, clamped into the block (the name `local` is zlib's macro, which PNGdec.h brings in).
  const auto rowAt = [pixels, stride, by, blockH](const int s) {
    const int inBlock = s < by ? 0 : (s - by < blockH ? s - by : blockH - 1);
    return pixels + static_cast<size_t>(inBlock) * static_cast<size_t>(stride);
  };
  for (int dy = rows.first; dy < rows.end; ++dy) {
    if (!f->keepGoing()) return 0;
    const FitAxis::Tap tap = f->ay.tap(dy);
    GameDisplayLab::sampleRow(rowAt(tap.s0), rowAt(tap.s1), tap.weight, f->ax, bx, bx + validW, cols.first, cols.end,
                              f->out.get());
    f->sink->row(f->fit.y + dy, f->fit.x + cols.first, f->out.get(), cols.end - cols.first);
  }
  return 1;
}

ImageRead readJpeg(const char* path, const int screenW, const int screenH, GrayRowSink& sink, ImageReadInfo& info) {
  if (ESP.getFreeHeap() < MIN_FREE_HEAP_FOR_JPEG) {
    LOG_ERR("DLAB", "Not enough heap for the JPEG decoder (%u free, need %u)", static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(MIN_FREE_HEAP_FOR_JPEG));
    return ImageRead::Memory;
  }
  std::unique_ptr<JPEGDEC> jpeg(new (std::nothrow) JPEGDEC());
  if (!jpeg) {
    LOG_ERR("DLAB", "OOM: JPEG decoder");
    return ImageRead::Memory;
  }
  openFailed = false;
  const int rc = jpeg->open(path, openFile, closeFile, jpegRead, jpegSeek, jpegDraw);
  // Closed on every way out from here, a rejected header included, as the converter closes it.
  struct Closer {
    JPEGDEC* j;
    ~Closer() { j->close(); }
  } closer{jpeg.get()};
  if (openFailed) {
    LOG_ERR("DLAB", "JPEG cannot be opened: %s", path);
    return ImageRead::Open;
  }
  if (rc != 1) {
    // JPEGDEC's own rejection of what it cannot decode (an extended or lossless JPEG) is the format's.
    const int error = jpeg->getLastError();
    const bool unsupported = error == JPEG_UNSUPPORTED_FEATURE;
    LOG_ERR("DLAB", "JPEG %s (error %d): %s", unsupported ? "not supported" : "rejected", error, path);
    return unsupported ? ImageRead::Format : ImageRead::Open;
  }
  info.width = jpeg->getWidth();
  info.height = jpeg->getHeight();
  if (!GameDisplayLab::imageSizeAccepted(info.width, info.height)) {
    LOG_ERR("DLAB", "JPEG %dx%d is outside the decoders' limits: %s", info.width, info.height, path);
    return ImageRead::Format;
  }
  info.fit = GameDisplayLab::fitToScreen(info.width, info.height, screenW, screenH);
  info.progressive = jpeg->getJPEGType() == JPEG_MODE_PROGRESSIVE;
  const GameDisplayLab::JpegScale scale =
      GameDisplayLab::jpegScaleFor(info.width, info.height, info.fit.w, info.fit.h, info.progressive);
  info.jpegScale = scale.denominator;
  Fitted fitted;
  if (!fitted.begin(sink, info.fit, scale.scaledW, scale.scaledH)) return ImageRead::Memory;
  // After open(), as the converter sets them.
  jpeg->setPixelType(EIGHT_BIT_GRAYSCALE);
  jpeg->setUserPointer(&fitted);
  const uint32_t start = millis();
  const int decoded = jpeg->decode(0, 0, scale.option);
  info.ms = millis() - start;
  if (fitted.stopped) return ImageRead::Stopped;
  if (decoded != 1) {
    LOG_ERR("DLAB", "JPEG decode failed (rc=%d, error %d): %s", decoded, jpeg->getLastError(), path);
    return ImageRead::Decode;
  }
  return ImageRead::Done;
}

}  // namespace

const char* imageReadName(const ImageRead read) {
  switch (read) {
    case ImageRead::Done:
      return "done";
    case ImageRead::Open:
      return "cannot open";
    case ImageRead::Format:
      return "unsupported format";
    case ImageRead::Memory:
      return "out of memory";
    case ImageRead::Decode:
      return "decode failed";
    case ImageRead::Stopped:
      return "stopped";
  }
  return "";
}

ImageRead readGrayImage(const char* path, const ImageKind kind, const int screenW, const int screenH, GrayRowSink& sink,
                        ImageReadInfo& info) {
  info = ImageReadInfo{};
  switch (kind) {
    case ImageKind::Png:
      return readPng(path, screenW, screenH, sink, info);
    case ImageKind::Jpeg:
      return readJpeg(path, screenW, screenH, sink, info);
    case ImageKind::None:
      break;
  }
  return ImageRead::Format;
}

#endif  // CROSSHATCH_DISPLAY_LAB
