#include "UC8279ScanBackend.h"

#if CROSSHATCH_DISPLAY_LAB

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <Logging.h>
#include <UC8279ScanSession.h>

// Two panel-sized planes for the whole session, never per scan. PSRAM only, as the game arena (no internal RAM
// fallback, by allocatePsram's contract).
UC8279ScanBackend::UC8279ScanBackend(GfxRenderer& renderer)
    : renderer(renderer),
      planeBytes(display.getBufferSize()),
      oldPlane(HalMemory::allocatePsram(planeBytes)),
      newPlane(HalMemory::allocatePsram(planeBytes)) {
  if (!oldPlane || !newPlane) {
    LOG_ERR("DLAB", "OOM: two %u byte PSRAM planes", static_cast<unsigned>(planeBytes));
    oldPlane.reset();
    newPlane.reset();
  }
}

bool UC8279ScanBackend::available() { return oldPlane && newPlane; }

const char* UC8279ScanBackend::name() { return "UC8279"; }

uint8_t UC8279ScanBackend::rateCount() { return GameDisplayLab::UC8279_RATE_COUNT; }

const char* UC8279ScanBackend::rateLabel(const uint8_t index) { return GameDisplayLab::uc8279RateLabel(index); }

bool UC8279ScanBackend::supportsWeakRow() { return false; }

const char* UC8279ScanBackend::rowLabel(const uint8_t row, const bool swapped) const {
  return GameDisplayLab::uc8279RowLabel(row, swapped);
}

ScanBackend::Scan UC8279ScanBackend::scan(const ScanRequest& request, uint32_t& busyUs) {
  busyUs = 0;
  if (!available()) {
    LOG_ERR("DLAB", "scan refused: no UC8279 planes");
    return Scan::Refused;
  }
  // One self-contained session (D8): the request's checks, then upload, PLL, the timed scan, labEnd. busyUs is the
  // driver's DRF-to-BUSY time.
  const GameDisplayLab::ScanOutcome outcome = GameDisplayLab::runUc8279Request(
      display.eink(), request, tables, oldPlane.get(), newPlane.get(), planeBytes, busyUs);
  if (outcome != GameDisplayLab::ScanOutcome::Done) {
    LOG_ERR("DLAB", "scan refused: UC8279 %s (rate index %u, %u frames)", GameDisplayLab::scanOutcomeName(outcome),
            static_cast<unsigned>(request.rate), static_cast<unsigned>(request.frames));
    busyUs = 0;
    return Scan::Refused;
  }
  return Scan::Done;
}

// labEnd() is a no-op outside a session (every scan already ended its own); it stays first so no lab register can
// outlive the clean, as the seam requires before any other display call.
void UC8279ScanBackend::clean() {
  display.eink().labEnd();
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
}

#endif  // CROSSHATCH_DISPLAY_LAB
