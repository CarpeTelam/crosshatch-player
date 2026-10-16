#pragma once

// The display lab compiles only in the X4 Pro development games build (`x4pro`, which alone sets
// FREEINK_CAP_DISPLAY_LAB=1 in platformio.ini): its scan primitive reaches the SDK facade through HalDisplay::eink()
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

#include <HalMemory.h>
#include <ScanBudget.h>

#include <cstddef>
#include <cstdint>

#include "activities/UiListActivity.h"

// The display lab (Games → Display lab; the spike plan `plan-spike-ssd1677-grayscale-calibration.md`, Part A): a bench
// for the SSD1677 batch of the X4 Pro that answers two numbers only the panel can give, the frame period and the drive
// polarity of the two LUT rows. Headed "Display lab", three rows: Frame period (FramePeriodActivity), Polarity
// (PolarityActivity), Exit. The launcher shows its row only on an X4 Pro whose probe said SSD1677 (available()), and
// pushes this screen only after the entry warning was confirmed, so Back on the warning sends nothing to the panel.
//
// It owns the scan primitive the two experiment screens call (scan()): both mask planes, 48,000 B each in PSRAM
// (allocated in onEnter, freed in onExit; the lab shows one notice row and runs nothing when PSRAM has no room), the
// caller's 112-byte LUT, and the facade's overlay-grayscale path: copyGrayscaleBuffers(bw, red) writes the raw masks
// into RAM 0x24 and 0x26, and displayGrayBuffer(turnOff=false, lut, factoryMode=true) uploads the LUT (bytes 0-104 to
// 0x32, the stock voltage tail to 0x03, 0x04 and 0x2C), activates with 0x22 = 0xCC and blocks on BUSY. The time of
// that call is the scan's busy time. Safety: stock voltages only (the LUT builder writes them), never the OTP command,
// a scan over SCAN_TIMEOUT_MS aborts its run and cleans the panel, every SESSION_CAP scans the panel gets a
// FULL_REFRESH, and every way out of the lab (Back, Home, sleep) runs one FULL_REFRESH when a scan reached the panel
// since the last clean, and logs the count. An inverted display is refused with a notice, and again at every scan
// (the control-center panel can invert it after onEnter): the facade drops grayscale calls while inverted, so a scan
// would measure nothing. A call the facade dropped for another reason returns in well under DROPPED_BELOW_US, so the
// budget reports it as Dropped rather than a near-zero busy time.
class DisplayLabActivity final : public UiListActivity {
 public:
  static constexpr const char* NAME = "DisplayLab";
  // Scans since the last FULL_REFRESH that force one (InkBoy's 2,400).
  static constexpr uint32_t SESSION_CAP = 2400;
  // A scan whose facade call takes longer than this aborts the run. A legitimate tp=100 scan at a slow frame period
  // can take over 5 s, so the limit sits well above it; the SDK's own BUSY wait gives up at 30 s, and a call in
  // progress cannot be interrupted, so a hung panel can stall the lab up to 30 s per scan.
  static constexpr uint32_t SCAN_TIMEOUT_MS = 15000;
  // A facade call back sooner than this never reached the panel (a one-frame activation alone takes milliseconds).
  static constexpr uint32_t DROPPED_BELOW_US = 500;

  // True on a device whose panel the lab can drive: an X4 Pro whose probe said SSD1677.
  static bool available();

  DisplayLabActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  enum class Scan : uint8_t { Done, TimedOut, Dropped, Refused };
  // One scan: fills the BW plane with `bwFill` and the RED plane with `redFill` (GameDisplayLab::PlaneFill), uploads
  // both, and runs `lut` (GameDisplayLab::LUT_BYTES bytes). `busyUs` is the facade call's time in microseconds.
  // Takes the render lock. Refused when the planes were not allocated or the display is inverted (nothing is sent);
  // Dropped when the call came back under DROPPED_BELOW_US (the facade dropped it; counted, no clean).
  Scan scan(const uint8_t* lut, uint8_t bwFill, uint8_t redFill, uint32_t& busyUs);

 private:
  enum class Row : uint8_t { FramePeriod, Polarity, Exit };
  static constexpr size_t ROW_COUNT = 3;

  int listCount() const override;
  const char* headerTitle() const override;
  void onEnter() override;
  // Frees the planes and runs the cleaning FULL_REFRESH when a scan reached the panel since the last clean; the
  // render mutex is already held by the caller.
  void onExit() override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;

  void buildRows();
  // A FULL_REFRESH of the framebuffer, which also resets the cap count. The caller holds the render lock.
  void cleanPanel();
  template <typename T>
  void openScreen();

  HalMemory::PsramBuffer bwPlane;
  HalMemory::PsramBuffer redPlane;
  size_t planeBytes = 0;
  // Why the lab cannot run (the one row's label), or null when it can.
  const char* notice = nullptr;
  GameDisplayLab::ScanBudget budget{SESSION_CAP, SCAN_TIMEOUT_MS * 1000UL, DROPPED_BELOW_US};
  freeink::ui::ListItem rowItems[ROW_COUNT] = {};
};

#endif  // CROSSHATCH_DISPLAY_LAB
