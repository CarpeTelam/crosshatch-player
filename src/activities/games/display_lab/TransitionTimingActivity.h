#pragma once

#include "DisplayLabActivity.h"

#if CROSSHATCH_DISPLAY_LAB

#include <HalMemory.h>

#include <cstddef>
#include <cstdint>

// The transition-timing screen (Display lab → Transition timing, UltraChip stock-path bench, story dlab-u1): times the
// controller's stock non-flashing previous-to-current transition through the public facade. Headed "Transition
// timing", rows: Run (the status), Reference, Arm, Transition 1 .. FLIPS, Summary.
//
// Run, blocking and under one RenderLock: a 4 x 2 checker (GameDisplayLab::fillChecker) into the framebuffer and one
// stock displayBuffer(FAST) timed as the reference (`DLAB transition ref ms=`); then, when the panel has overlay
// grayscale, one all-B/W gray pass over the same checker (base, zero masks, activation, cleanup) timed as the arm
// (`DLAB arm ms=`), since only a gray pass makes the drivers take the transition path (docs/crosshatch/display-lab.md);
// then FLIPS times the checker, inverted on odd flips, through displayGrayscaleBase(FAST), each timed and judged by
// GameDisplayLab::transitionFellBack (`DLAB transition n=<i> ms=<ms> flashed=<yes/no>`). The Summary row gives the
// flips' min, median and max. A call over DisplayLabActivity::SCAN_TIMEOUT_MS ends the run (`DLAB timeout: ...`). The
// table is redrawn with a FULL refresh after every run; every way out after a run this visit runs one more FULL (Back
// promotes it onto the menu's repaint, Home and sleep run it in onExit). An inverted display is refused per run.
class TransitionTimingActivity final : public UiListActivity {
 public:
  static constexpr const char* NAME = "DlabTransition";
  static constexpr size_t FLIPS = 10;

  // `lab` (the menu under this screen) is unused: the stock bench alone has no scan session, and on the scan menu
  // (an UltraChip controller with a scan backend, the epic's D10) every scan has ended its own lab session.
  TransitionTimingActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, DisplayLabActivity& lab);

 private:
  static constexpr size_t ROW_RUN = 0;
  static constexpr size_t ROW_REFERENCE = 1;
  static constexpr size_t ROW_ARM = 2;
  static constexpr size_t ROW_FIRST_FLIP = 3;
  static constexpr size_t ROW_SUMMARY = ROW_FIRST_FLIP + FLIPS;
  static constexpr size_t ROW_COUNT = ROW_SUMMARY + 1;

  int listCount() const override { return static_cast<int>(ROW_COUNT); }
  const char* headerTitle() const override;
  void onEnter() override;
  // Runs the cleaning FULL refresh when a run drove the panel and Back did not promote it; frees the zero plane. The
  // render mutex is already held by the caller.
  void onExit() override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;

  void buildRows();
  void setStatus(const char* text);
  // Every result row back to "Not run".
  void resetResults();
  // The run: checks, measure(), the summary, and the FULL repaint.
  void run();
  // The timed calls into `frame` (width x height, the framebuffer); the Run row's outcome text. The caller holds the
  // render lock.
  const char* measure(uint8_t* frame, uint16_t width, uint16_t height);

  // One all-clear plane of the framebuffer's size: both masks of the arming gray pass. PSRAM, onEnter to onExit.
  HalMemory::PsramBuffer zeroPlane;
  const char* status = nullptr;
  // Transition rows' labels ("Transition 10") and every row's result ("12345 ms, fell back"; the summary's three).
  char flipLabels[FLIPS][24] = {};
  char results[ROW_COUNT][64] = {};
  // The flips' times this run, in microseconds, and how many ran.
  uint32_t flipUs[FLIPS] = {};
  size_t flipCount = 0;
  freeink::ui::ListItem rowItems[ROW_COUNT] = {};
  // A run sent something to the panel this visit and no FULL refresh for leaving has been arranged yet.
  bool drovePanel = false;
};

#endif  // CROSSHATCH_DISPLAY_LAB
