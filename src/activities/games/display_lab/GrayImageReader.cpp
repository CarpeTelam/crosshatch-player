#include "GrayImageReader.h"

#if CROSSHATCH_DISPLAY_LAB

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_err.h>
#include <esp_task_wdt.h>
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

// One reader at a time (the gray viewer's sweep task), so the open's outcome and the walked size are file statics, set
// before each decoder's open() and read after it.
// openFile could not open the file: PNG::open returns 0, which is PNG_SUCCESS, when its open callback fails, so the
// reader records the failure here.
bool openFailed = false;
// The walked size (GameDisplayLab::WalkExtent::end: a PNG through IEND, a JPEG through its EOI): openFile hands it to
// the decoder as the file's size (PNGFILE.iSize, JPEGFILE.iSize), and the reads give nothing of the file past it.
uint32_t walkedSize = 0;
// A JPEG's walked header end and the buffer moves its parse takes (WalkExtent::headerEnd, windowMoves).
uint32_t jpegHeaderEnd = 0;
uint32_t jpegWindowMoves = 0;
// The sink's stop, for the read callbacks: once it is asked, every read gives no file bytes (layer 2).
GrayRowSink* readSink = nullptr;

bool readsStopped() { return readSink != nullptr && readSink->stopped(); }
// A seek the file refused: the reads give nothing until a seek succeeds, rather than bytes from where the file still
// is (which the decoder would take for the bytes where it asked).
bool seekFailed = false;
// JPEGDEC's open() is under way (its reads all count against jpegHeaderReadCap), the reads it has made in open() and
// in decode(), and a header read it dropped (from then on open() gets nothing: the parse buffer is zeroed and the
// file's position no longer JPEGDEC's); all set before each open() and each decode().
bool jpegOpening = false;
uint32_t jpegOpenReads = 0;
uint32_t jpegDecodeReads = 0;
bool jpegHeaderFailed = false;

// Layer 4, the task watchdog: while a decoder runs, the sweep task is subscribed to ESP-IDF's task watchdog, which the
// core configures to panic (CONFIG_ESP_TASK_WDT_PANIC, CONFIG_ESP_TASK_WDT_TIMEOUT_S 5, sdkconfig), and feeds it at
// every row a decoder hands over (yieldDuringDecode); the SD card's own waits feed it too (SDCardManager feeds a
// subscribed task). A decoder that spins where no read or row reaches it then resets the device within 5 s instead
// of holding the task, its runner and maybe the SD card's mutex for good. The timeout is the core's, shared by every
// watched task; it is not changed here.
class DecodeWatchdog {
 public:
  DecodeWatchdog() : added(esp_task_wdt_status(nullptr) == ESP_ERR_NOT_FOUND && esp_task_wdt_add(nullptr) == ESP_OK) {}
  ~DecodeWatchdog() {
    if (added) esp_task_wdt_delete(nullptr);
  }
  DecodeWatchdog(const DecodeWatchdog&) = delete;
  DecodeWatchdog& operator=(const DecodeWatchdog&) = delete;

 private:
  bool added;
};

void yieldDuringDecode(uint32_t& lastYieldMs) {
  // A row: the decode is moving.
  if (esp_task_wdt_status(nullptr) == ESP_OK) esp_task_wdt_reset();
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
  const size_t real = f->size();
  // The walk's size, or the file's if the file shrank since (its reads then end early and are filled or cut).
  *size = static_cast<int32_t>(real < walkedSize ? real : walkedSize);
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

// Layer 2, a read that cannot stall a decoder: the file gives at most what readAllowance allows (nothing once stopped,
// past a refused seek or at the walked size), a negative read is taken as nothing, and then:
// - PNGdec gets the requested length always, the rest 0xFF (GameDisplayLab::fillPngRead, which names the png.inl lines
//   this rests on: a short read inside IDAT would spin its loop at png.inl 885-904);
// - JPEGDEC gets the rest zero-filled, never 0xFF nor the stale buffer (GameDisplayLab::answerJpegHeaderRead and
//   answerJpegDecodeRead name the jpeg.inl lines): a read starting before the walked header's end is the header
//   parse's, kept whole or dropped (a stop, a short or failed read, a refused seek, past jpegHeaderReadCap for all of
//   open()), a drop zeroing the whole parse buffer and latching, so open() errs; from the header's end on it is the
//   decode's, told the whole span, its 0xFF runs tamed, the file's bytes for decoderReadCap reads.
// `allowed` is set to what the file should have given.
int32_t readFile(HalFile* f, const uint32_t position, uint8_t* pBuf, const int32_t len, const bool stopped,
                 int32_t& allowed) {
  allowed = seekFailed ? 0 : GameDisplayLab::readAllowance(position, len, walkedSize, stopped);
  if (allowed <= 0) return 0;
  const int got = f->read(pBuf, static_cast<size_t>(allowed));
  return got < 0 ? 0 : got;
}

int32_t pngRead(PNGFILE* pFile, uint8_t* pBuf, const int32_t len) {
  HalFile* f = reinterpret_cast<HalFile*>(pFile->fHandle);
  int32_t allowed = 0;
  const int32_t got = f ? readFile(f, static_cast<uint32_t>(f->position()), pBuf, len, readsStopped(), allowed) : 0;
  return GameDisplayLab::fillPngRead(pBuf, len, got);
}

int32_t pngSeek(PNGFILE* pFile, const int32_t pos) {
  HalFile* f = reinterpret_cast<HalFile*>(pFile->fHandle);
  if (!f) return -1;
  seekFailed = pos < 0 || !f->seek(static_cast<size_t>(pos));
  return seekFailed ? -1 : pos;
}

// JPEGDEC tracks the file position in pFile->iPos itself (JPEGGetMoreData compares it with the size), so the callbacks
// keep it in step with the file, as the JPEG converter's do (otherwise a progressive JPEG with a large header fails to
// parse), held under the size (GameDisplayLab::jpegPositionAfterRead); the reads go by the file's own position.
int32_t jpegRead(JPEGFILE* pFile, uint8_t* pBuf, const int32_t len) {
  HalFile* f = reinterpret_cast<HalFile*>(pFile->fHandle);
  const uint32_t position = f ? static_cast<uint32_t>(f->position()) : walkedSize;
  int32_t answer = 0;
  const bool overOpenCap = jpegOpening && ++jpegOpenReads > GameDisplayLab::jpegHeaderReadCap(jpegWindowMoves);
  if (jpegHeaderFailed || overOpenCap || (position < jpegHeaderEnd && (!f || seekFailed || readsStopped()))) {
    jpegHeaderFailed = true;
    answer = GameDisplayLab::answerJpegHeaderRead(pBuf, len, 0, true);
  } else if (position < jpegHeaderEnd) {
    // The header parse: the file must give all it should, or the read is dropped.
    int32_t expected = 0;
    const int32_t got = readFile(f, position, pBuf, len, false, expected);
    const bool dropped = got < expected;
    if (dropped) jpegHeaderFailed = true;
    answer = GameDisplayLab::answerJpegHeaderRead(pBuf, len, got, dropped);
  } else {
    int32_t got = 0;
    if (f && (jpegOpening || ++jpegDecodeReads <= GameDisplayLab::decoderReadCap(walkedSize))) {
      int32_t allowed = 0;
      got = readFile(f, position, pBuf, len, readsStopped(), allowed);
    }
    answer = GameDisplayLab::answerJpegDecodeRead(pBuf, len, got);
  }
  pFile->iPos = GameDisplayLab::jpegPositionAfterRead(f ? static_cast<uint32_t>(f->position()) : walkedSize,
                                                      static_cast<uint32_t>(pFile->iSize));
  return answer;
}

int32_t jpegSeek(JPEGFILE* pFile, const int32_t pos) {
  HalFile* f = reinterpret_cast<HalFile*>(pFile->fHandle);
  if (!f) return -1;
  seekFailed = pos < 0 || !f->seek(static_cast<size_t>(pos));
  if (seekFailed) return -1;
  pFile->iPos = GameDisplayLab::jpegPositionAfterRead(static_cast<uint32_t>(pos), static_cast<uint32_t>(pFile->iSize));
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
  // The row just decoded, as gray.
  std::unique_ptr<uint8_t[]> gray;
  // The row before it, for a fit that enlarges vertically (a blend reaches it): the keeper's two strip rows.
  std::unique_ptr<uint8_t[]> stripRows[2];
  GameDisplayLab::SeamKeeper seams;
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
  GameDisplayLab::pngRowToGray(pDraw->pPixels, ctx->gray.get(), ctx->srcW, pDraw->iPixelType, pDraw->iBpp,
                               pDraw->pPalette, pDraw->iHasAlpha != 0, ctx->png->getTransparentColor());
  // A PNG row is a one-row strip of one block; the keeper hands out the row before it.
  const GameDisplayLab::SourceBlock block = ctx->seams.begin(ctx->gray.get(), 0, pDraw->y, ctx->srcW, 1, ctx->srcW);
  for (int dy = rows.first; dy < rows.end; ++dy) {
    if (!f.keepGoing()) return 0;
    GameDisplayLab::sampleBlock(block, f.ax, f.ay.tap(dy), 0, f.fit.w, f.out.get());
    f.sink->row(f.fit.y + dy, f.fit.x, f.out.get(), f.fit.w);
  }
  ctx->seams.end(block);
  return 1;
}

// Layer 1, a complete walk: the file's layout against its size (GameDisplayLab::walkPngChunks, walkJpegSegments),
// before its decoder reads anything. PNGdec 1.1.6's IDAT loop spins without end when the file ends inside a chunk,
// and a PNG or JPEG cut short is refused here instead; the walked size is what the decoder is then given.
template <typename Walk>
ImageRead checkLayout(const char* kind, const char* path, GrayRowSink& sink, Walk&& walk) {
  HalFile file;
  if (!Storage.openFileForRead("DLAB", path, file)) {
    LOG_ERR("DLAB", "%s cannot be opened: %s", kind, path);
    return ImageRead::Open;
  }
  const size_t size = file.size();
  const uint32_t size32 = size > INT32_MAX ? static_cast<uint32_t>(INT32_MAX) : static_cast<uint32_t>(size);
  const auto readAt = [&file](const uint32_t offset, uint8_t* out, const size_t n) {
    return file.seek(offset) && file.read(out, n) == static_cast<int>(n);
  };
  const auto stopped = [&sink] { return sink.stopped(); };
  GameDisplayLab::WalkExtent extent;
  const GameDisplayLab::FileLayout layout = walk(size32, readAt, stopped, &extent);
  file.close();
  walkedSize = extent.end;
  jpegHeaderEnd = extent.headerEnd;
  jpegWindowMoves = extent.windowMoves;
  const bool png = kind[0] == 'P';
  switch (layout) {
    case GameDisplayLab::FileLayout::Complete:
      if (size > size32) {
        LOG_ERR("DLAB", "%s over 2 GiB: %s", kind, path);
        return ImageRead::Format;
      }
      if (extent.end < size) {
        LOG_INF("DLAB", "%s: the %u bytes after its end (%s at %u) are not read: %s", kind,
                static_cast<unsigned>(size - extent.end), png ? "IEND" : "EOI", static_cast<unsigned>(extent.end),
                path);
      }
      return ImageRead::Done;
    case GameDisplayLab::FileLayout::TooShort:
      LOG_ERR("DLAB", "%s too short (%u bytes, no signature): %s", kind, static_cast<unsigned>(size), path);
      return ImageRead::Short;
    case GameDisplayLab::FileLayout::Truncated:
      LOG_ERR("DLAB", "%s truncated (a chunk or segment runs past the file's %u bytes): %s", kind,
              static_cast<unsigned>(size), path);
      return ImageRead::Format;
    case GameDisplayLab::FileLayout::NoEnd:
      LOG_ERR("DLAB", "%s has no end (%s): %s", kind,
              png ? "no IEND chunk before the file's end" : "no SOS marker, or no EOI after it", path);
      return ImageRead::NoEnd;
    case GameDisplayLab::FileLayout::Malformed:
      if (png) {
        LOG_ERR("DLAB", "PNG malformed (signature, a chunk length, PLTE, tRNS, or the chunk count): %s", path);
      } else {
        LOG_ERR("DLAB",
                "JPEG malformed (SOI, a segment length, SOF, DQT, DHT (class, index, codes, over-subscribed), an Exif "
                "IFD, or a table or SOS past the 2 KiB parse buffer, or too many buffer moves): %s",
                path);
      }
      return ImageRead::Format;
    case GameDisplayLab::FileLayout::ReadFailed:
      LOG_ERR("DLAB", "%s read failed during the layout walk: %s", kind, path);
      return ImageRead::Open;
    case GameDisplayLab::FileLayout::Stopped:
      return ImageRead::Stopped;
  }
  return ImageRead::Format;
}

ImageRead readPng(const char* path, const int screenW, const int screenH, GrayRowSink& sink, ImageReadInfo& info) {
  const ImageRead layout =
      checkLayout("PNG", path, sink, [](const uint32_t size, auto& readAt, auto& stopped, auto* extent) {
        return GameDisplayLab::walkPngChunks(size, readAt, stopped, extent);
      });
  if (layout != ImageRead::Done) return layout;
  if (ESP.getFreeHeap() < MIN_FREE_HEAP_FOR_PNG) {
    LOG_ERR("DLAB", "Not enough heap for the PNG decoder (%u free, need %u)", static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(MIN_FREE_HEAP_FOR_PNG));
    return ImageRead::Memory;
  }
  seekFailed = false;
  const DecodeWatchdog watchdog;
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
  // A stop during open() (its reads then give nothing of the file) is the stop's, not the file's.
  if (sink.stopped()) return ImageRead::Stopped;
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
  if (!ctx.fitted.begin(sink, info.fit, info.width, info.height)) return ImageRead::Memory;
  const size_t width = static_cast<size_t>(info.width);
  ctx.gray = makeUniqueNoThrow<uint8_t[]>(width);
  if (ctx.fitted.ay.enlarges()) {
    ctx.stripRows[0] = makeUniqueNoThrow<uint8_t[]>(width);
    ctx.stripRows[1] = makeUniqueNoThrow<uint8_t[]>(width);
  }
  if (!ctx.gray || (ctx.fitted.ay.enlarges() && (!ctx.stripRows[0] || !ctx.stripRows[1]))) {
    LOG_ERR("DLAB", "OOM: %d byte PNG gray lines", info.width);
    return ImageRead::Memory;
  }
  ctx.seams.reset(info.width, ctx.stripRows[0].get(), ctx.stripRows[1].get(), ctx.fitted.ax.enlarges());
  const uint32_t start = millis();
  const int decoded = png->decode(&ctx, 0);
  info.ms = millis() - start;
  // A stop seen only by the reads (which then gave nothing of the file) ends a decode in an error, not a draw.
  if (ctx.fitted.stopped || sink.stopped()) return ImageRead::Stopped;
  if (decoded != PNG_SUCCESS) {
    LOG_ERR("DLAB", "PNG decode failed (%d): %s", decoded, path);
    return ImageRead::Decode;
  }
  return ImageRead::Done;
}

// ---- JPEG ----------------------------------------------------------------------------------------------------------

// JPEGDEC delivers a strip of MCU rows at a time, left to right in blocks (several MCUs each, the right-edge one
// narrower than its pitch), strips top to bottom (the last one trimmed); the keeper hands each block the previous
// strip's last row and the previous block's last column, so blends cross the seams.
struct JpegContext {
  Fitted fitted;
  // The keeper's two strip rows of the scaled width, only when the fit enlarges vertically (a blend reaches the row
  // above).
  std::unique_ptr<uint8_t[]> stripRows[2];
  GameDisplayLab::SeamKeeper seams;
};

int jpegDraw(JPEGDRAW* pDraw) {
  JpegContext* ctx = reinterpret_cast<JpegContext*>(pDraw->pUser);
  if (!ctx) return 0;
  Fitted* f = &ctx->fitted;
  yieldDuringDecode(f->lastYieldMs);
  if (!f->keepGoing()) return 0;
  // EIGHT_BIT_GRAYSCALE: one byte a pixel, rows `iWidth` apart, `iWidthUsed` of them valid, in JPEGDEC's scaled
  // source coordinates.
  if (pDraw->iWidth <= 0 || pDraw->iWidthUsed <= 0 || pDraw->iHeight <= 0) return 1;
  const GameDisplayLab::SourceBlock block =
      ctx->seams.begin(reinterpret_cast<const uint8_t*>(pDraw->pPixels), pDraw->x, pDraw->y, pDraw->iWidthUsed,
                       pDraw->iHeight, pDraw->iWidth);
  // The output pixels whose last needed source pixel is in this block (none when shrinking skips it); the pixels
  // before it come from the row above and the column left.
  const GameDisplayLab::Span cols = GameDisplayLab::outputsEndingIn(f->ax, block.x0, block.x1);
  const GameDisplayLab::Span rows = GameDisplayLab::outputsEndingIn(f->ay, block.y0, block.y1);
  if (!cols.empty()) {
    for (int dy = rows.first; dy < rows.end; ++dy) {
      if (!f->keepGoing()) return 0;
      GameDisplayLab::sampleBlock(block, f->ax, f->ay.tap(dy), cols.first, cols.end, f->out.get());
      f->sink->row(f->fit.y + dy, f->fit.x + cols.first, f->out.get(), cols.end - cols.first);
    }
  }
  ctx->seams.end(block);
  return 1;
}

// A JPEG's header is walked from SOI to SOS, as JPEGDEC parses it, and an EOI must follow (checkLayout); past that
// JPEGDEC's decode walks every MCU of the frame in bounded loops whatever data arrives (its reads are topped up with
// zeros past the walked size, a stop or a failed read, and tamed where 0xFF runs would starve it:
// GameDisplayLab::answerJpegDecodeRead), calling back each strip, so scan data that runs short decodes to garbage or a
// decode error and the stop is still seen per row.
ImageRead readJpeg(const char* path, const int screenW, const int screenH, GrayRowSink& sink, ImageReadInfo& info) {
  const ImageRead layout =
      checkLayout("JPEG", path, sink, [](const uint32_t size, auto& readAt, auto& stopped, auto* extent) {
        return GameDisplayLab::walkJpegSegments(size, readAt, stopped, extent);
      });
  if (layout != ImageRead::Done) return layout;
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
  seekFailed = false;
  jpegOpening = true;
  jpegOpenReads = 0;
  jpegDecodeReads = 0;
  jpegHeaderFailed = false;
  const DecodeWatchdog watchdog;
  const int rc = jpeg->open(path, openFile, closeFile, jpegRead, jpegSeek, jpegDraw);
  jpegOpening = false;
  // Closed on every way out from here, a rejected header included, as the converter closes it.
  struct Closer {
    JPEGDEC* j;
    ~Closer() { j->close(); }
  } closer{jpeg.get()};
  if (openFailed) {
    LOG_ERR("DLAB", "JPEG cannot be opened: %s", path);
    return ImageRead::Open;
  }
  // A stop during open() (its reads then give nothing of the file) is the stop's, not the file's.
  if (sink.stopped()) return ImageRead::Stopped;
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
  JpegContext ctx;
  if (!ctx.fitted.begin(sink, info.fit, scale.scaledW, scale.scaledH)) return ImageRead::Memory;
  if (ctx.fitted.ay.enlarges()) {
    ctx.stripRows[0] = makeUniqueNoThrow<uint8_t[]>(static_cast<size_t>(scale.scaledW));
    ctx.stripRows[1] = makeUniqueNoThrow<uint8_t[]>(static_cast<size_t>(scale.scaledW));
    if (!ctx.stripRows[0] || !ctx.stripRows[1]) {
      LOG_ERR("DLAB", "OOM: two %d byte JPEG strip rows", scale.scaledW);
      return ImageRead::Memory;
    }
  }
  ctx.seams.reset(scale.scaledW, ctx.stripRows[0].get(), ctx.stripRows[1].get(), ctx.fitted.ax.enlarges());
  // After open(), as the converter sets them.
  jpeg->setPixelType(EIGHT_BIT_GRAYSCALE);
  jpeg->setUserPointer(&ctx);
  const uint32_t start = millis();
  jpegDecodeReads = 0;
  const int decoded = jpeg->decode(0, 0, scale.option);
  info.ms = millis() - start;
  // A stop seen only by the reads (which then gave nothing of the file) ends a decode in an error, not a draw.
  if (ctx.fitted.stopped || sink.stopped()) return ImageRead::Stopped;
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
    case ImageRead::Short:
      return "file too short";
    case ImageRead::NoEnd:
      return "file has no end";
  }
  return "";
}

ImageRead readGrayImage(const char* path, const ImageKind kind, const int screenW, const int screenH, GrayRowSink& sink,
                        ImageReadInfo& info) {
  info = ImageReadInfo{};
  readSink = &sink;
  struct ClearSink {
    ~ClearSink() { readSink = nullptr; }
  } clearSink;
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
