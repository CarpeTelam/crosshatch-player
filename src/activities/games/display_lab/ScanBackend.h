#pragma once

// The display lab compiles only in the X4 Pro development games build (`x4pro`, which alone sets
// FREEINK_CAP_DISPLAY_LAB=1 in platformio.ini): its scan backends reach the SDK facade through HalDisplay::eink()
// (upstream touch ledger row 13), which the simulator's HAL stand-in does not have, the panel it measures is the X4
// Pro's, and a bench has no place in a release image. Every display_lab file and the launcher's row sit behind this
// one macro, so the C3 `default`, `sticky`, the `x4pro-gh_release*` and simulator envs compile none of it.
// Tested by value, not defined(): BoardConfig.h defaults every FREEINK_DEVICE_* macro to 0.
#ifndef FREEINK_CAP_DISPLAY_LAB
#define FREEINK_CAP_DISPLAY_LAB 0
#endif
#if FREEINK_CAP_GAMES && FREEINK_DEVICE_X4PRO && FREEINK_CAP_DISPLAY_LAB && !defined(SIMULATOR)
#define CROSSHATCH_DISPLAY_LAB 1
#else
#define CROSSHATCH_DISPLAY_LAB 0
#endif

#if CROSSHATCH_DISPLAY_LAB

#include <cstdint>

// One scan (the display-lab epic, D1): two full-screen 1-bpp masks in the framebuffer's byte order
// (GameDisplayLab::MaskPlanes; a set bit drives that pixel that way, a pixel in neither holds), a frame count, and an
// index into the backend's frame-rate list (rateLabel names each), with the backend's two drive rows swapped when
// `swapPolarity`. D1 does not define a pixel set in both masks: callers set at most one mask per pixel, and the SSD1677
// backend holds such a pixel (LUT3, all VSS).
struct ScanRequest {
  const uint8_t* blackMask;
  const uint8_t* whiteMask;
  uint8_t frames;
  uint8_t rate;
  bool swapPolarity;
};

// The scan primitive of one panel controller (the display-lab epic, D1), chosen by the detected controller
// (ScanBackends.h). A backend owns only the controller's encoding and facade calls; the lab (DisplayLabActivity::scan)
// keeps the controller-neutral safety around it: the render lock, the session cap, the timeout and the dropped-call
// floor. Every backend keeps the panel's stock voltages and never sends an OTP or MTP programming command.
class ScanBackend {
 public:
  enum class Scan : uint8_t { Done, TimedOut, Dropped, Refused };

  virtual ~ScanBackend() = default;

  // Whether the backend got what it needs to scan (its buffers); a backend that is not available refuses every scan.
  virtual bool available() = 0;
  // The controller's name for a log line ("SSD1677").
  virtual const char* name() = 0;
  // The frame-rate list: ScanRequest::rate is an index below rateCount(), and rateLabel(index) prints it (nullptr out
  // of range).
  virtual uint8_t rateCount() = 0;
  virtual const char* rateLabel(uint8_t index) = 0;
  // Sends one scan and blocks until the panel is done; `busyUs` is the time of the controller's activation and BUSY
  // wait. Done when the scan was sent; Refused, with nothing sent and `busyUs` 0, when the backend is not available,
  // a mask is missing, or the rate index is out of range. The caller holds the render lock; the lab turns the time
  // into TimedOut or Dropped.
  virtual Scan scan(const ScanRequest& request, uint32_t& busyUs) = 0;
  // A FULL refresh of the framebuffer through the renderer. The caller holds the render lock.
  virtual void clean() = 0;
  // Whether the backend can drive a third, weak row (the gray engine's weak mode).
  virtual bool supportsWeakRow() = 0;
  // Short text naming what drive row `row` (1: the white mask's, 2: the black mask's) drives on this controller,
  // with `swapped` matching ScanRequest::swapPolarity (SSD1677: "LUT1 VSL" unswapped, "LUT1 VSH1" swapped);
  // nullptr for any other row. The display-lab epic's Amendment D1a: the screens label and log rows with it, so no
  // controller fact lives outside its backend.
  virtual const char* rowLabel(uint8_t row, bool swapped) const = 0;
};

#endif  // CROSSHATCH_DISPLAY_LAB
