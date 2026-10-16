#include "Ssd1677ScanBackend.h"

#if CROSSHATCH_DISPLAY_LAB

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <Logging.h>
#include <Ssd1677Encoding.h>

// Two panel-sized planes for the whole session, never per scan. PSRAM only, as the game arena (no internal RAM
// fallback, by allocatePsram's contract).
Ssd1677ScanBackend::Ssd1677ScanBackend(GfxRenderer& renderer)
    : renderer(renderer),
      planeBytes(display.getBufferSize()),
      bwPlane(HalMemory::allocatePsram(planeBytes)),
      redPlane(HalMemory::allocatePsram(planeBytes)) {
  if (!bwPlane || !redPlane) {
    LOG_ERR("DLAB", "OOM: two %u byte PSRAM planes", static_cast<unsigned>(planeBytes));
    bwPlane.reset();
    redPlane.reset();
  }
}

bool Ssd1677ScanBackend::available() { return bwPlane && redPlane; }

const char* Ssd1677ScanBackend::name() { return "SSD1677"; }

uint8_t Ssd1677ScanBackend::rateCount() { return GameDisplayLab::SSD1677_RATE_COUNT; }

const char* Ssd1677ScanBackend::rateLabel(const uint8_t index) { return GameDisplayLab::ssd1677RateLabel(index); }

bool Ssd1677ScanBackend::supportsWeakRow() { return false; }

const char* Ssd1677ScanBackend::rowLabel(const uint8_t row, const bool swapped) const {
  return GameDisplayLab::ssd1677RowLabel(row, swapped);
}

ScanBackend::Scan Ssd1677ScanBackend::scan(const ScanRequest& request, uint32_t& busyUs) {
  busyUs = 0;
  if (!available()) {
    LOG_ERR("DLAB", "scan refused: no SSD1677 planes");
    return Scan::Refused;
  }
  if (!request.blackMask || !request.whiteMask) {
    LOG_ERR("DLAB", "scan refused: request without masks");
    return Scan::Refused;
  }
  // The one LUT buffer: 112 bytes on the stack.
  uint8_t lut[GameDisplayLab::LUT_BYTES];
  if (!GameDisplayLab::buildSsd1677ScanLut(lut, request.frames, request.rate, request.swapPolarity)) {
    LOG_ERR("DLAB", "scan refused: rate index %u of %u", static_cast<unsigned>(request.rate),
            static_cast<unsigned>(GameDisplayLab::SSD1677_RATE_COUNT));
    return Scan::Refused;
  }
  GameDisplayLab::encodeSsd1677Planes(request.blackMask, request.whiteMask, bwPlane.get(), redPlane.get(), planeBytes);
  EInkDisplay& eink = display.eink();
  // Both planes raw into RAM (overlay mode writes them as given), then the LUT, activation and the BUSY wait, timed as
  // one call: the frame period is the slope of this time against TP, which cancels the fixed upload and activation
  // overhead.
  eink.copyGrayscaleBuffers(bwPlane.get(), redPlane.get());
  const unsigned long began = micros();
  eink.displayGrayBuffer(false, lut, true);
  busyUs = static_cast<uint32_t>(micros() - began);
  return Scan::Done;
}

void Ssd1677ScanBackend::clean() { renderer.displayBuffer(HalDisplay::FULL_REFRESH); }

#endif  // CROSSHATCH_DISPLAY_LAB
