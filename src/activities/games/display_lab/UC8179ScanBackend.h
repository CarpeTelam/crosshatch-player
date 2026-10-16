#pragma once

#include "ScanBackend.h"

#if CROSSHATCH_DISPLAY_LAB

#include <HalMemory.h>
#include <UC8179ScanLut.h>

#include <cstddef>
#include <cstdint>

class GfxRenderer;

// The UC8179 batch's scan backend (Uc8179Driver, behind the D1 seam; story dlab-a2), over the SDK facade's lab API
// (freeink-sdk docs/display-lab.md). Its pure parts are GameDisplayLab::UC8179ScanLut: the five 43-byte tables for the
// request's frames and polarity (one group of one phase, D3, D9), the PLL code list, the row labels, and the two DTM
// planes from the masks (D2). scan() is GameDisplayLab::runUc8179Request: the request's checks, the tables and the
// planes, then one self-contained lab session every call (D8, runUc8279Scan, the seam's one UltraChip sequence): upload
// the tables, set PLL, labScan, then labEnd, which restores PSR, CDI, PLL 0x06 and TSSET. Voltages are never written,
// and the lab never sends an OTP or MTP programming command.
//
// `busyUs` is the driver's DRF-to-BUSY-release time only: not the table upload, the plane streaming or PON, unlike the
// SSD1677 backend's LUT upload, activation and BUSY wait. Frame-period slopes compare across controllers; tp=1
// intercepts and the polarity screen's ms= do not.
//
// One exception to ScanBackend::scan's "Refused means nothing sent": a ScanRefused after a successful upload has
// written PSR, the LUT registers, CDI and PLL, which the session's labEnd restores; it is not reachable on an X4 Pro
// (labScan refuses only without an upload or with a null plane, and the UC8179 never refuses PLL).
//
// It owns the two encoded planes, `display.getBufferSize()` bytes each in PSRAM (48,000 on the X4 Pro), allocated by
// the constructor and freed with the backend, which the lab creates in onEnter and destroys in onExit; a backend whose
// planes did not fit is not available(). The masks of every request are `display.getBufferSize()` bytes too.
class UC8179ScanBackend final : public ScanBackend {
 public:
  explicit UC8179ScanBackend(GfxRenderer& renderer);

  bool available() override;
  const char* name() override;
  uint8_t rateCount() override;
  const char* rateLabel(uint8_t index) override;
  Scan scan(const ScanRequest& request, uint32_t& busyUs) override;
  // labEnd() first (the seam's rule before any other display call), then the FULL refresh.
  void clean() override;
  // The tables drive KW and WK only; WW and KK hold at GND.
  bool supportsWeakRow() override;
  // uc8179RowLabel(row, swapped).
  const char* rowLabel(uint8_t row, bool swapped) const override;

 private:
  // In this order: the constructor allocates the planes from planeBytes.
  GfxRenderer& renderer;
  size_t planeBytes;
  HalMemory::PsramBuffer oldPlane;
  HalMemory::PsramBuffer newPlane;
  // The one table buffer (215 bytes): a member, not a scan() local, to keep the frame under 256 bytes.
  uint8_t tables[GameDisplayLab::UC8179_SCAN_TABLE_BYTES] = {};
};

#endif  // CROSSHATCH_DISPLAY_LAB
