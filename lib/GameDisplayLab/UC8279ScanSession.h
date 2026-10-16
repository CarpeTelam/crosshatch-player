#pragma once

#include <cstddef>
#include <cstdint>

#include "UC8279ScanLut.h"

// The UC8279 scan backend's call sequence over the SDK seam's lab API (freeink-sdk docs/display-lab.md), header-only
// and templated on the lab so the host suite drives it with a recording fake and the backend with `display.eink()`
// (FreeInkDisplay). Pinned by test/game_display_lab/test_uc8279_scan_lut.cpp.
namespace GameDisplayLab {

// How one scan ended: Done; a request refused before any lab call (NoMasks, BadRate, TooManyFrames); or the first lab
// call that refused. Shared with UC8179ScanSession.h; TooManyFrames ("frames over 252", D7) is UC8279-only.
enum class ScanOutcome : uint8_t {
  Done,
  NoMasks,
  BadRate,
  TooManyFrames,
  Unsupported,
  UploadRefused,
  RateRefused,
  ScanRefused
};

// One self-contained lab session (D8): upload `tables` (UC8279_SCAN_TABLE_BYTES), set PLL `pllCode`, one labScan of
// the two planes, then labEnd. This is the seam's UltraChip session, shared with UC8179ScanSession.h's
// runUc8179Request (both controllers take the same calls and the same 215-byte table layout). Every scan uploads again
// because the seam requires labEnd() before any other display call and an ordinary refresh drops the waveform, while
// the screens repaint between scans.
//
// `busyUs` is 0 unless the scan was sent. When the lab is not supported nothing else is called (Unsupported). Once a
// lab call has been made, labEnd() ends the session on every path: the first call that returns false ends it there,
// with nothing after labEnd(), and names the outcome.
template <typename Lab>
ScanOutcome runUc8279Scan(Lab& lab, const uint8_t* tables, const uint8_t pllCode, const uint8_t* oldPlane,
                          const uint8_t* newPlane, uint32_t& busyUs) {
  busyUs = 0;
  if (!lab.labSupported()) return ScanOutcome::Unsupported;
  ScanOutcome outcome = ScanOutcome::Done;
  if (!lab.labUploadWaveform(tables, UC8279_SCAN_TABLE_BYTES)) {
    outcome = ScanOutcome::UploadRefused;
  } else if (!lab.labSetFrameRate(pllCode)) {
    outcome = ScanOutcome::RateRefused;
  } else if (!lab.labScan(oldPlane, newPlane, busyUs)) {
    outcome = ScanOutcome::ScanRefused;
    busyUs = 0;  // a refused scan is not a measurement
  }
  lab.labEnd();
  return outcome;
}

// One whole scan of `request` (ScanRequest's fields: blackMask, whiteMask, frames, rate, swapPolarity): a request
// with a null mask, a rate index not below UC8279_RATE_COUNT, or more than UC8279_MAX_FRAMES frames is refused with no
// lab call and nothing written; otherwise the tables go into `tables` (UC8279_SCAN_TABLE_BYTES), the planes into
// `oldPlane` and `newPlane` (`bytes` each, the masks' size), and runUc8279Scan sends them.
template <typename Lab, typename Request>
ScanOutcome runUc8279Request(Lab& lab, const Request& request, uint8_t* tables, uint8_t* oldPlane, uint8_t* newPlane,
                             const size_t bytes, uint32_t& busyUs) {
  busyUs = 0;
  if (request.blackMask == nullptr || request.whiteMask == nullptr) return ScanOutcome::NoMasks;
  uint8_t pllCode = 0;
  if (!uc8279PllCode(request.rate, pllCode)) return ScanOutcome::BadRate;
  if (!buildUc8279ScanTables(tables, request.frames, request.swapPolarity)) return ScanOutcome::TooManyFrames;
  encodeUc8279Planes(request.blackMask, request.whiteMask, oldPlane, newPlane, bytes);
  return runUc8279Scan(lab, tables, pllCode, oldPlane, newPlane, busyUs);
}

// TooManyFrames's name below spells the ceiling out, so it cannot drift from the constant.
static_assert(UC8279_MAX_FRAMES == 252, "scanOutcomeName(TooManyFrames) names 252");

// The outcome's name for a log line ("upload refused"); nullptr for Done.
constexpr const char* scanOutcomeName(const ScanOutcome outcome) {
  switch (outcome) {
    case ScanOutcome::Done:
      return nullptr;
    case ScanOutcome::NoMasks:
      return "request without masks";
    case ScanOutcome::BadRate:
      return "rate index out of range";
    case ScanOutcome::TooManyFrames:
      return "frames over 252";
    case ScanOutcome::Unsupported:
      return "lab unsupported";
    case ScanOutcome::UploadRefused:
      return "upload refused";
    case ScanOutcome::RateRefused:
      return "frame rate refused";
    case ScanOutcome::ScanRefused:
      return "scan refused";
  }
  return nullptr;
}

}  // namespace GameDisplayLab
