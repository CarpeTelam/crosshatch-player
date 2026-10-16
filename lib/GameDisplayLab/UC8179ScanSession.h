#pragma once

#include <cstddef>
#include <cstdint>

#include "UC8179ScanLut.h"
#include "UC8279ScanSession.h"

// The UC8179 scan backend's request-to-session step over the SDK seam's lab API (freeink-sdk docs/display-lab.md),
// header-only and templated on the lab so the host suite drives it with a recording fake and the backend with
// `display.eink()` (FreeInkDisplay). Pinned by test/game_display_lab/test_uc8179_scan_lut.cpp.
namespace GameDisplayLab {

// The seam's UltraChip session is one call sequence and one table layout for both controllers (labSupported, upload of
// the five 43-byte tables, PLL, labScan, labEnd), so the UC8179 reuses runUc8279Scan and ScanOutcome rather than a
// second copy. runUc8279Scan uploads UC8279_SCAN_TABLE_BYTES, which must be the UC8179's length too.
static_assert(UC8179_SCAN_TABLE_BYTES == UC8279_SCAN_TABLE_BYTES, "runUc8279Scan uploads the UC8179's tables");

// One whole scan of `request` (ScanRequest's fields: blackMask, whiteMask, frames, rate, swapPolarity): a request with
// a null mask (NoMasks) or a rate index not below UC8179_RATE_COUNT (BadRate) is refused with no lab call and nothing
// written; otherwise the tables go into `tables` (UC8179_SCAN_TABLE_BYTES), the planes into `oldPlane` and `newPlane`
// (`bytes` each, the masks' size), and runUc8279Scan sends them as one self-contained session (D8: upload, PLL,
// labScan, labEnd every call). Every frame count fits the UC8179's 8-bit TA, so TooManyFrames never occurs.
template <typename Lab, typename Request>
ScanOutcome runUc8179Request(Lab& lab, const Request& request, uint8_t* tables, uint8_t* oldPlane, uint8_t* newPlane,
                             const size_t bytes, uint32_t& busyUs) {
  busyUs = 0;
  if (request.blackMask == nullptr || request.whiteMask == nullptr) return ScanOutcome::NoMasks;
  uint8_t pllCode = 0;
  if (!uc8179PllCode(request.rate, pllCode)) return ScanOutcome::BadRate;
  buildUc8179ScanTables(tables, request.frames, request.swapPolarity);
  encodeUc8179Planes(request.blackMask, request.whiteMask, oldPlane, newPlane, bytes);
  return runUc8279Scan(lab, tables, pllCode, oldPlane, newPlane, busyUs);
}

}  // namespace GameDisplayLab
