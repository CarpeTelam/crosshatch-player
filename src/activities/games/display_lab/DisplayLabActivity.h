#pragma once

#include "ScanBackend.h"  // CROSSHATCH_DISPLAY_LAB, the gate every display_lab file sits behind

#if CROSSHATCH_DISPLAY_LAB

#include <GrayPreset.h>
#include <HalMemory.h>
#include <MaskPlanes.h>
#include <ScanBudget.h>
#include <StockBench.h>

#include <cstddef>
#include <cstdint>
#include <memory>

#include "activities/UiListActivity.h"

// The display lab (Games → Display lab; the spike plan `plan-spike-ssd1677-grayscale-calibration.md`, Part A, and the
// display-lab epic's backend seam and its gray engine, dlab-c1): a bench for the X4 Pro's panel that answers the
// numbers only the panel can give: the frame period, the drive polarity of the two drive rows, and the gray engine's
// dose table. Headed "Display lab", its menu per the controller (the epic's D10, StockBenchGate.h's labMenuFor): on an
// SSD1677 seven rows: Frame period (FramePeriodActivity), Polarity (PolarityActivity), Transition test
// (TransitionTestActivity), Gray viewer (GrayViewerActivity, story dlab-g3), Tuning (TuningActivity), Save preset
// (writes the session's gray preset to GrayPresetFile::PATH; its line then says whether that worked), Exit; on an
// UltraChip controller with a scan backend (UC8179, UC8279) nine: those with the stock-path bench's Transition timing
// (TransitionTimingActivity) and Gray ramp (GrayRampActivity) before Exit; on an UltraChip controller without one, the
// stock bench's three: Transition timing, Gray ramp, Exit. The launcher shows its row only on an X4 Pro that gets one
// of these menus (available()), and pushes this screen only after the entry warning was confirmed, so Back on the
// warning sends nothing to the panel.
//
// It owns the session the experiment screens share: the controller's ScanBackend (ScanBackends.h), the two drive
// masks (masks(), GameDisplayLab::MaskPlanes over two `display.getBufferSize()` buffers, 48,000 B each in PSRAM), all
// created in onEnter and freed in onExit (the lab shows one notice row and runs nothing when any of them has no room),
// the gray preset (preset(), read from GrayPresetFile::PATH in onEnter over the controller's defaults: the UltraChip
// preset on a UC8179 or UC8279, InkBoy's otherwise; the tuning screen edits it, the transition test and the gray
// viewer read it), and scan(), the controller-neutral safety around backend()->scan(). Safety: the backend keeps the
// stock voltages and never sends an OTP command, a scan over SCAN_TIMEOUT_MS aborts its run and cleans the panel, every
// SESSION_CAP scans the panel gets a FULL_REFRESH, and every way out of the lab (Back, Home, sleep) runs one
// FULL_REFRESH when a scan reached the panel since the last clean, and logs the count. An inverted display is refused
// with a notice, and again at every scan (the control-center panel can invert it after onEnter): the facade drops
// grayscale calls while inverted, so a scan would measure nothing. A call the facade dropped for another reason returns
// in well under DROPPED_BELOW_US, so the budget reports it as Dropped rather than a near-zero busy time.
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

  // True on a device whose panel the lab can drive: an X4 Pro whose controller gets a lab menu (StockBenchGate.h's
  // labMenuFor): one with a scan backend, or an UltraChip one without, which gets the stock-path bench's menu alone.
  static bool available();

  DisplayLabActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  using Scan = ScanBackend::Scan;
  // One scan through the backend: the screens fill masks() and pass masks().black() and masks().white() in `request`.
  // `busyUs` is the backend's activation time in microseconds. Takes the render lock, and cleans first when the
  // session cap is due. Refused when the session has no backend and masks, the display is inverted, the rate index is
  // not below backend()->rateCount(), or the backend refused (nothing is sent); TimedOut when the call took over
  // SCAN_TIMEOUT_MS (logged, the panel cleaned); Dropped when it came back under DROPPED_BELOW_US (the facade dropped
  // it; counted, no clean).
  Scan scan(const ScanRequest& request, uint32_t& busyUs);
  // The session's two drive masks; valid() false when the lab shows a notice.
  GameDisplayLab::MaskPlanes& masks() { return maskPlanes; }
  // The session's backend; null when the lab shows a notice.
  ScanBackend* backend() { return scanBackend.get(); }
  // The session's gray preset: the file's, or the controller's defaults; edited in place by the tuning screen.
  GameDisplayLab::GrayPreset& preset() { return grayPreset; }
  // The controller's defaults into preset() (their rate index, 0, is in every backend's list), and their base as the
  // one Save writes; returns that base. The caller holds the render lock while the tuning screen shows the values.
  GameDisplayLab::PresetBase resetPreset();
  // Writes preset() to GrayPresetFile::PATH and sets the Save preset row's line ("Saved", or "Save failed, values
  // kept"); false when the write failed (logged), the session's values unchanged either way.
  bool savePreset();
  // Scans left before scan() runs the session cap's cleaning refresh first (0 when it is due).
  uint32_t scansBeforeClean() const {
    return budget.sinceClean() < SESSION_CAP ? SESSION_CAP - budget.sinceClean() : 0;
  }
  // A cleaning FULL refresh of the framebuffer now, which restarts the cap count; nothing without a backend. The
  // caller holds the render lock.
  void cleanNow();
  // The status text of a scan that ended with `result` ("Done", "Timed out", ...).
  static const char* outcomeText(Scan result);

 private:
  // A row's actionValue is its GameDisplayLab::LabRow; each menu's rows come from GameDisplayLab::labMenuRows.
  using Row = GameDisplayLab::LabRow;

  int listCount() const override;
  const char* headerTitle() const override;
  void onEnter() override;
  // Runs the cleaning FULL_REFRESH when a scan reached the panel since the last clean, then frees the backend and the
  // masks; the render mutex is already held by the caller.
  void onExit() override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;

  // The masks and the backend for this session, and `loaded` as its gray preset (its rate index checked against the
  // backend); false, having logged why, when one of them has no room.
  bool openSession(const GameDisplayLab::GrayPreset& loaded);
  void closeSession();
  // The menu's rows (or the notice row) into rowItems and rowCount.
  void buildRows();
  // A FULL_REFRESH through the backend, which also resets the cap count. The caller holds the render lock, and the
  // session has a backend (a scan reached the panel).
  void cleanPanel();
  template <typename T>
  void openScreen();

  HalMemory::PsramBuffer blackMask;
  HalMemory::PsramBuffer whiteMask;
  GameDisplayLab::MaskPlanes maskPlanes;
  std::unique_ptr<ScanBackend> scanBackend;
  // Why the lab cannot run (the one row's label), or null when it can.
  const char* notice = nullptr;
  // This session's menu (labMenuFor); LabMenu::StockBench has no backend and no masks.
  GameDisplayLab::LabMenu menu = GameDisplayLab::LabMenu::None;
  GameDisplayLab::ScanBudget budget{SESSION_CAP, SCAN_TIMEOUT_MS * 1000UL, DROPPED_BELOW_US};
  GameDisplayLab::GrayPreset grayPreset = GameDisplayLab::GrayPreset::inkBoyDefaults();
  // The controller's defaults (panelBase), and the defaults grayPreset's values carry (presetBase: a loaded file's own
  // base when it had one, else the panel's), written as the preset file's `base` on Save.
  GameDisplayLab::PresetBase panelBase = GameDisplayLab::PresetBase::InkBoy;
  GameDisplayLab::PresetBase presetBase = GameDisplayLab::PresetBase::InkBoy;
  // The Save preset row's line after a save, or null before one.
  const char* saveStatus = nullptr;
  freeink::ui::ListItem rowItems[GameDisplayLab::LAB_ROW_COUNT] = {};
  // The rows buildRows put in rowItems.
  size_t rowCount = 0;
};

#endif  // CROSSHATCH_DISPLAY_LAB
