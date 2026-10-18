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

#include "DecoderReads.h"

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

// As the converters: yield a tick at most this often during a read, so lower-priority tasks on the core run.
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

// One reader at a time (the gray viewer's sweep task), so the open's outcome and the reads' state are file statics,
// set before each decoder's open() and read after it.
// openFile could not open the file: PNG::open returns 0, which is PNG_SUCCESS, when its open callback fails, so the
// reader records the failure here.
bool openFailed = false;
// The reads' state (GameDisplayLab::DecoderReads: the walked size, a JPEG's header end and buffer moves, a refused
// seek, the caps' counts, the header latch).
GameDisplayLab::DecoderReads reads;
// The sink's stop, for the read callbacks: once it is asked, every read gives no file bytes (layer 2).
GrayRowSink* readSink = nullptr;

bool readsStopped() { return readSink != nullptr && readSink->stopped(); }

// Layer 4, the task watchdog: for the whole read of a picture (its walk and its decode) the sweep task is subscribed
// to ESP-IDF's task watchdog, which the x4pro build configures to panic after 5 s without a feed (its prebuilt dio_opi
// sdkconfig: CONFIG_ESP_TASK_WDT_INIT, CONFIG_ESP_TASK_WDT_PANIC, CONFIG_ESP_TASK_WDT_TIMEOUT_S 5), and feeds it on
// progress: every row a decoder hands over, and every read that gives file bytes (the walk's too). Every blocking file
// call (open, read, seek, close and a handle's end: each takes the card's mutex, which another task may hold for longer
// than 5 s, legitimately) runs in a gap, out of the watchdog, while the read is making progress
// (GameDisplayLab::TaskWatchdog decides, over EspTaskWatchdog: the gaps end once the task has gone the timeout outside
// file calls, or twice it in all less the time in reads that returned bytes, without progress): a decoder that spins
// where no read or row reaches it, or that keeps reading without progress, then resets the device within seconds
// instead of holding the task, its runner and maybe the card's mutex for good. The timeout is the core's, shared by
// every watched task; it is not changed here. A watchdog that is not configured, a join that fails and a rejoin that
// fails are each logged once, with the error.
#ifdef CONFIG_ESP_TASK_WDT_TIMEOUT_S
constexpr uint32_t WATCHDOG_TIMEOUT_MS = CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000;
#else
constexpr uint32_t WATCHDOG_TIMEOUT_MS = 5000;
#endif

// ESP-IDF's task watchdog for the calling task, as GameDisplayLab::TaskWatchdog asks it.
struct EspTaskWatchdog {
  GameDisplayLab::WatchdogState state() {
    const esp_err_t status = esp_task_wdt_status(nullptr);
    if (status == ESP_OK) return GameDisplayLab::WatchdogState::Subscribed;
    if (status == ESP_ERR_NOT_FOUND) return GameDisplayLab::WatchdogState::NotSubscribed;
    notConfiguredError = status;
    return GameDisplayLab::WatchdogState::NotConfigured;
  }
  // Static: the calling task's watchdog is ESP-IDF's, not this object's (TaskWatchdog calls them through it).
  static int add() { return esp_task_wdt_add(nullptr); }
  static int remove() { return esp_task_wdt_delete(nullptr); }
  static void reset() { esp_task_wdt_reset(); }
  static uint32_t nowMs() { return millis(); }
  void report(const GameDisplayLab::WatchdogEvent event, int error) {
    static bool logged[3] = {false, false, false};
    const auto index = static_cast<size_t>(event);
    if (logged[index]) return;
    logged[index] = true;
    const char* what = "the task watchdog is not configured";
    if (event == GameDisplayLab::WatchdogEvent::JoinFailed) what = "cannot join the task watchdog";
    if (event == GameDisplayLab::WatchdogEvent::RejoinFailed) what = "cannot rejoin the task watchdog";
    if (event == GameDisplayLab::WatchdogEvent::NotConfigured) error = notConfiguredError;
    LOG_ERR("DLAB", "gray viewer: %s (%s); the rest of the read runs unwatched", what, esp_err_to_name(error));
  }
  esp_err_t notConfiguredError = ESP_OK;
};
using DecodeWatchdog = GameDisplayLab::TaskWatchdog<EspTaskWatchdog>;

// The read's watchdog, for the callbacks (null outside a read), and the yield on progress.
DecodeWatchdog* activeWatchdog = nullptr;
uint32_t lastYieldMs = 0;

// A row, or a read that gave file bytes: the watchdog is fed, and a tick is yielded at most every YIELD_MS (as the
// converters yield), so lower-priority tasks on the core run.
void progressDuringRead() {
  if (activeWatchdog) activeWatchdog->progress();
  const uint32_t now = millis();
  if (now - lastYieldMs < YIELD_MS) return;
  vTaskDelay(1);
  lastYieldMs = millis();
}

// One blocking file call, from construction to destruction.
struct FileCall {
  DecodeWatchdog::Gap gap{activeWatchdog};
  // A read that returned the bytes asked: its wait is not the watchdog's wall time (every other call's is).
  void gaveBytes() { gap.gaveBytes(); }
};

// ---- file callbacks, as the converters' (the HalFile behind fHandle) ----------------------------------------------

void* openFile(const char* filename, int32_t* size) {
  // Released by closeFile.
  HalFile* f = new (std::nothrow) HalFile();
  if (!f) {
    LOG_ERR("DLAB", "OOM: image file handle");
    openFailed = true;
    return nullptr;
  }
  {
    // The handle's end takes the card's mutex too (HalFile::Impl's destructor), so a failed open ends in the gap. An
    // open that fails is tried once more (an SD card's transient error).
    const FileCall call;
    if (!GameDisplayLab::tryTwice([&] { return Storage.openFileForRead("DLAB", filename, *f); })) {
      delete f;
      f = nullptr;
    }
  }
  if (!f) {
    openFailed = true;
    return nullptr;
  }
  const size_t real = f->size();
  // The walk's size, or the file's if the file shrank since (its reads then end early and are filled or cut).
  *size = static_cast<int32_t>(real < reads.walkedSize ? real : reads.walkedSize);
  return f;
}

// Tolerates a null handle (an open that failed), so a decoder's close() is always safe. The close and the handle's end
// are one call out of the watchdog.
void closeFile(void* handle) {
  HalFile* f = reinterpret_cast<HalFile*>(handle);
  if (f) {
    const FileCall call;
    f->close();
    delete f;
  }
}

// The HalFile behind a decoder's handle, as GameDisplayLab::DecoderReads' callbacks take a file (DecoderReads.h): each
// blocking call out of the watchdog, the sink's stop, and a read that gave file bytes fed to the watchdog. Its
// position is never asked (DecoderReads::position follows it).
struct ReaderFile {
  HalFile* f;

  bool isOpen() const { return f != nullptr; }
  int32_t read(uint8_t* buf, const int32_t n) {
    FileCall call;
    const int32_t got = f->read(buf, static_cast<size_t>(n));
    if (got == n) call.gaveBytes();
    return got;
  }
  bool seek(const uint32_t position) {
    const FileCall call;
    return f->seek(position);
  }
  // Static: the reads' stop and the watchdog are the read's, not the handle's (DecoderReads.h calls them on the file).
  static bool stopped() { return readsStopped(); }
  static void progress() { progressDuringRead(); }
};

// Layer 2, a read that cannot stall a decoder (GameDisplayLab::DecoderReads.h, which the host decoder test runs too):
// the file gives at most what readAllowance allows (nothing once stopped, past a refused seek or at the walked size),
// a negative read is taken as nothing, and then:
// - PNGdec gets the requested length always, the rest 0xFF (GameDisplayLab::fillPngRead, which names the png.inl lines
//   this rests on: a short read inside IDAT would spin its loop at png.inl 885-904);
// - JPEGDEC gets the rest zero-filled, never 0xFF nor the stale buffer (GameDisplayLab::answerJpegHeaderRead and
//   answerJpegDecodeRead name the jpeg.inl lines): while open() runs, a read starting before the walked header's end
//   is the header parse's, kept whole or dropped (a stop, a short or failed read, a refused seek, past
//   jpegHeaderReadCap header reads), a drop zeroing the whole parse buffer and latching, so open() errs (and a latched
//   failure is Open whatever open() returns); every other read is the decode's, never latched, told the whole span,
//   its runs of 0xFF 0xFF tamed, the file's bytes for decoderReadCap reads.
int32_t pngRead(PNGFILE* pFile, uint8_t* pBuf, const int32_t len) {
  ReaderFile file{reinterpret_cast<HalFile*>(pFile->fHandle)};
  return GameDisplayLab::answerPngRead(file, reads, pBuf, len);
}

int32_t pngSeek(PNGFILE* pFile, const int32_t pos) {
  ReaderFile file{reinterpret_cast<HalFile*>(pFile->fHandle)};
  return GameDisplayLab::answerSeek(file, reads, pos);
}

// JPEGDEC tracks the file position in pFile->iPos itself (JPEGGetMoreData compares it with the size), so the callbacks
// keep it in step with the file, as the JPEG converter's do (otherwise a progressive JPEG with a large header fails to
// parse), held under the size (GameDisplayLab::jpegPositionAfterRead); the reads go by the file's own position.
int32_t jpegRead(JPEGFILE* pFile, uint8_t* pBuf, const int32_t len) {
  ReaderFile file{reinterpret_cast<HalFile*>(pFile->fHandle)};
  return GameDisplayLab::answerJpegRead(file, reads, pBuf, len, pFile->iSize, pFile->iPos);
}

int32_t jpegSeek(JPEGFILE* pFile, const int32_t pos) {
  ReaderFile file{reinterpret_cast<HalFile*>(pFile->fHandle)};
  return GameDisplayLab::answerJpegSeek(file, reads, pos, pFile->iSize, pFile->iPos);
}

// A read of the file failed twice (DecoderReads::readFailed) during `when`: logged once, the picture is the file's
// failure ("could not open"), never Done.
ImageRead readFailedDuring(const char* kind, const char* when, const char* path) {
  LOG_ERR("DLAB", "%s read failed during %s (at byte %u; tried twice): %s", kind, when,
          static_cast<unsigned>(reads.position), path);
  return ImageRead::Open;
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
  // A read that failed (or a JPEG scan of markers past the read-on bound): the rest is not the file's.
  if (!ctx || reads.failed()) return 0;
  Fitted& f = ctx->fitted;
  // A row: the decode is moving.
  progressDuringRead();
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
// before its decoder reads anything, inside the watchdog (its file calls out of it, the handle's end included, each
// read that gives the bytes asked fed to it), its duration logged. PNGdec 1.1.6's IDAT loop spins without end when the
// file ends inside a chunk, and a PNG or JPEG cut short is refused here instead; the walked size is what the decoder
// is then given (`reads`).
template <typename Walk>
ImageRead checkLayout(const char* kind, const char* path, GrayRowSink& sink, Walk&& walk) {
  HalFile file;
  bool opened = false;
  {
    // HalFile::Impl's destructor takes the card's mutex: a handle that did not open ends in the call. An open that
    // fails is tried once more (an SD card's transient error).
    const FileCall call;
    opened = GameDisplayLab::tryTwice([&] { return Storage.openFileForRead("DLAB", path, file); });
    if (!opened) file = HalFile();
  }
  if (!opened) {
    LOG_ERR("DLAB", "%s cannot be opened: %s", kind, path);
    return ImageRead::Open;
  }
  const size_t size = file.size();
  const uint32_t size32 = size > INT32_MAX ? static_cast<uint32_t>(INT32_MAX) : static_cast<uint32_t>(size);
  // Each seek and read one file call; a read that fails or comes up short is tried once more from its seek
  // (GameDisplayLab::readExactlyAt), then the walk is ReadFailed.
  struct WalkFile {
    HalFile& f;
    bool seek(const uint32_t offset) {
      const FileCall call;
      return f.seek(offset);
    }
    int32_t read(uint8_t* out, const int32_t n) {
      FileCall call;
      const int32_t got = f.read(out, static_cast<size_t>(n));
      if (got == n) call.gaveBytes();
      return got;
    }
  };
  WalkFile walkFile{file};
  const auto readAt = [&walkFile](const uint32_t offset, uint8_t* out, const size_t n) {
    const bool read = GameDisplayLab::readExactlyAt(walkFile, offset, out, n);
    if (read) progressDuringRead();
    return read;
  };
  const auto stopped = [&sink] { return sink.stopped(); };
  GameDisplayLab::WalkExtent extent;
  const uint32_t walkStartMs = millis();
  const GameDisplayLab::FileLayout layout = walk(size32, readAt, stopped, extent);
  const uint32_t walkMs = millis() - walkStartMs;
  {
    // The close and the handle's end (the local's destructor then has nothing to do) in one call.
    const FileCall call;
    file.close();
    file = HalFile();
  }
  reads.start(extent);
  const bool png = kind[0] == 'P';
  switch (layout) {
    case GameDisplayLab::FileLayout::Complete:
      if (size > size32) {
        LOG_ERR("DLAB", "%s over 2 GiB: %s", kind, path);
        return ImageRead::Format;
      }
      LOG_INF("DLAB", "%s walked in %u ms: %s", kind, static_cast<unsigned>(walkMs), path);
      if (extent.end < size) {
        LOG_INF("DLAB", "%s: the %u bytes after its end (%s at %u) are not read: %s", kind,
                static_cast<unsigned>(size - extent.end), png ? "IEND" : "its EOI, or a progressive file's first scan",
                static_cast<unsigned>(extent.end), path);
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
                "JPEG malformed (SOI, a segment length, SOF, DQT, DHT (class, index, codes, over-subscribed), "
                "SOS (components, tables, spectral range, approximation), an Exif IFD, or a table or SOS past the 2 "
                "KiB parse buffer, or "
                "too many buffer moves): %s",
                path);
      }
      return ImageRead::Format;
    case GameDisplayLab::FileLayout::Unsupported:
      LOG_ERR("DLAB", "%s not supported (a baseline file of several scans, one per component): %s", kind, path);
      return ImageRead::Format;
    case GameDisplayLab::FileLayout::ReadFailed:
      LOG_ERR("DLAB", "%s read failed during the layout walk: %s", kind, path);
      return ImageRead::Open;
    case GameDisplayLab::FileLayout::Stopped:
      return ImageRead::Stopped;
  }
  return ImageRead::Format;
}

// Not inlined into readGrayImage, so the two decoders' contexts never share one frame on the sweep task's stack.
[[gnu::noinline]] ImageRead readPng(const char* path, const int screenW, const int screenH, GrayRowSink& sink,
                                    ImageReadInfo& info) {
  const ImageRead layout =
      checkLayout("PNG", path, sink, [](const uint32_t size, auto& readAt, auto& stopped, auto& extent) {
        return GameDisplayLab::walkPngChunks(size, readAt, stopped, &extent);
      });
  if (layout != ImageRead::Done) return layout;
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
  // A stop during open() (its reads then give nothing of the file) is the stop's, not the file's.
  if (sink.stopped()) return ImageRead::Stopped;
  if (reads.readFailed) return readFailedDuring("PNG", "its header", path);
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
  // A read that failed twice: the file's, never Done (the decode was given 0xFF from there and stopped drawing).
  if (reads.readFailed) return readFailedDuring("PNG", "the decode", path);
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
  // A read that failed, or a scan of restart markers past the read-on bound: the rest is not the file's.
  if (!ctx || reads.failed()) return 0;
  Fitted* f = &ctx->fitted;
  progressDuringRead();
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
[[gnu::noinline]] ImageRead readJpeg(const char* path, const int screenW, const int screenH, GrayRowSink& sink,
                                     ImageReadInfo& info) {
  ImageRead layout = ImageRead::Memory;
  {
    // The walk's buffer for the EOI search, on the heap (the sweep task's stack keeps the walk's frame small).
    const auto search = makeUniqueNoThrow<uint8_t[]>(GameDisplayLab::JPEG_EOI_SEARCH_BYTES);
    if (!search) {
      LOG_ERR("DLAB", "OOM: %u byte JPEG walk buffer", static_cast<unsigned>(GameDisplayLab::JPEG_EOI_SEARCH_BYTES));
      return ImageRead::Memory;
    }
    uint8_t* const buffer = search.get();
    layout = checkLayout("JPEG", path, sink, [buffer](const uint32_t size, auto& readAt, auto& stopped, auto& extent) {
      return GameDisplayLab::walkJpegSegments(size, readAt, stopped, &extent, buffer,
                                              GameDisplayLab::JPEG_EOI_SEARCH_BYTES);
    });
  }
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
  reads.startJpegOpen();
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
  // A stop during open() (its reads then give nothing of the file) is the stop's, not the file's.
  if (sink.stopped()) return ImageRead::Stopped;
  // A dropped header read (a short or failed read, a refused seek, past the header's read cap) is the file's, whatever
  // open() made of the zeros it was given.
  if (reads.readFailed) return readFailedDuring("JPEG", "its header", path);
  if (reads.headerFailed) {
    LOG_ERR("DLAB", "JPEG header read failed (open %d): %s", rc, path);
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
  reads.startDecode();
  const int decoded = jpeg->decode(0, 0, scale.option);
  info.ms = millis() - start;
  // A stop seen only by the reads (which then gave nothing of the file) ends a decode in an error, not a draw.
  if (ctx.fitted.stopped || sink.stopped()) return ImageRead::Stopped;
  // A read that failed twice: the file's, never Done (the decode was given zeros from there and stopped drawing).
  if (reads.readFailed) return readFailedDuring("JPEG", "the decode", path);
  if (reads.scanFlooded) {
    LOG_ERR("DLAB", "JPEG scan: over %u KiB of restart markers in a row; not decoded: %s",
            static_cast<unsigned>(GameDisplayLab::JPEG_RESTART_READ_ON / 1024), path);
    return ImageRead::Decode;
  }
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
  // The whole read, its walk included, is watched (layer 4); declared after the sink, so it is left first, and taken
  // from the callbacks before it is.
  EspTaskWatchdog esp;
  DecodeWatchdog watchdog(esp, WATCHDOG_TIMEOUT_MS);
  activeWatchdog = &watchdog;
  lastYieldMs = millis();
  struct ClearWatchdog {
    ~ClearWatchdog() { activeWatchdog = nullptr; }
  } clearWatchdog;
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
