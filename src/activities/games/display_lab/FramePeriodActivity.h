#pragma once

#include "DisplayLabActivity.h"

#if CROSSHATCH_DISPLAY_LAB

#include <LabExperiments.h>

#include <cstddef>
#include <cstdint>

// The frame-period screen (Display lab → Frame period): the first unknown. Headed "Frame period", a Run row over one
// result row per scan of the experiment: TP in GameDisplayLab::FRAME_PERIOD_TPS {1, 10, 100} against every rate of the
// lab's backend (up to MAX_RATES; on the SSD1677 the frame-rate bytes {0x22, 0x11, 0x44}, nine scans), tp outer, with
// both masks clear, so no pixel is driven and the screen stays. Run performs the scans in order through the lab's
// scan(), each logged as `DLAB tp=<tp> fr=<rate label> busy_us=<us>`, and the rows then show the busy time of each. A
// scan over the lab's timeout ends the run there (the lab logged it and cleaned the panel); the rows not reached stay
// "Not run". The frame period is the slope of busy time against TP for one rate: (busy(100) - busy(1)) / 99 frames.
class FramePeriodActivity final : public UiListActivity {
 public:
  static constexpr const char* NAME = "DlabFramePeriod";
  static constexpr size_t TP_COUNT = sizeof(GameDisplayLab::FRAME_PERIOD_TPS);
  // The rates of a backend the screen runs; a backend with more runs only its first MAX_RATES (one LOG_ERR).
  static constexpr uint8_t MAX_RATES = 16;
  static constexpr size_t MAX_SCANS = TP_COUNT * MAX_RATES;
  static constexpr size_t MAX_ROWS = 1 + MAX_SCANS;  // Run, then one row per scan

  // `lab` is the menu under this screen on the stack, which outlives it.
  FramePeriodActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, DisplayLabActivity& lab);

 private:
  int listCount() const override { return static_cast<int>(1 + scanCount); }
  const char* headerTitle() const override;
  void onEnter() override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;

  // The TP of scan `i`, and its rate index into the backend's list (GameDisplayLab::framePeriodTp/Rate).
  uint8_t tpOf(size_t i) const { return GameDisplayLab::framePeriodTp(i, rateCount); }
  uint8_t rateOf(size_t i) const { return GameDisplayLab::framePeriodRate(i, rateCount); }
  // The backend's label of scan `i`'s rate ("0x22"), or "?" when it has none.
  const char* rateLabelOf(size_t i) const;
  // The scans; writes the rows and the Run row's status as it goes.
  void run();
  void buildRows();

  DisplayLabActivity& lab;
  // The rates the screen runs (the backend's rateCount(), capped at MAX_RATES) and the scans: TP_COUNT * rateCount.
  uint8_t rateCount = 0;
  size_t scanCount = 0;
  // The Run row's status, and each scan's label ("tp=100 fr=0x44") and result ("12345 us", or a status).
  const char* status = nullptr;
  char labels[MAX_SCANS][24] = {};
  char results[MAX_SCANS][24] = {};  // "4294967295 us", or a status such as "Not enough memory"
  freeink::ui::ListItem rowItems[MAX_ROWS] = {};
  bool running = false;
};

#endif  // CROSSHATCH_DISPLAY_LAB
