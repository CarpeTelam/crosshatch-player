#pragma once

#include "DisplayLabActivity.h"

#if CROSSHATCH_DISPLAY_LAB

#include <Ssd1677ScanLut.h>

#include <cstddef>
#include <cstdint>

// The frame-period screen (Display lab → Frame period): the first unknown. Headed "Frame period", a Run row over nine
// result rows, one per scan of GameDisplayLab's experiment: TP[0A] in {1, 10, 100} against the frame-rate bytes
// {0x22, 0x11, 0x44}, all-hold masks (both planes zero, so no pixel is driven and the screen stays). Run performs the
// nine scans in order through the lab's scan(), each logged as `DLAB tp=<tp> fr=0x<rate> busy_us=<us>`, and the rows
// then show the busy time of each. A scan over the lab's timeout ends the run there (the lab logged it and cleaned the
// panel); the rows not reached stay "Not run". The frame period is the slope of busy time against TP for one
// frame-rate byte: (busy(100) - busy(1)) / 99 frames.
class FramePeriodActivity final : public UiListActivity {
 public:
  static constexpr const char* NAME = "DlabFramePeriod";
  static constexpr size_t SCANS = GameDisplayLab::FRAME_PERIOD_SCANS;
  static constexpr size_t ROW_COUNT = 1 + SCANS;  // Run, then one row per scan

  // `lab` is the menu under this screen on the stack, which outlives it.
  FramePeriodActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, DisplayLabActivity& lab);

 private:
  int listCount() const override { return static_cast<int>(ROW_COUNT); }
  const char* headerTitle() const override;
  void onEnter() override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;

  // The nine scans; writes the rows and the Run row's status as it goes.
  void run();
  void buildRows();

  DisplayLabActivity& lab;
  // The Run row's status, and each scan's label ("tp=100 fr=0x44") and result ("12345 us", or a status).
  const char* status = nullptr;
  char labels[SCANS][16] = {};
  char results[SCANS][24] = {};  // "4294967295 us", or a status such as "Not enough memory"
  freeink::ui::ListItem rowItems[ROW_COUNT] = {};
  bool running = false;
};

#endif  // CROSSHATCH_DISPLAY_LAB
