#pragma once

#include "DisplayLabActivity.h"

#if CROSSHATCH_DISPLAY_LAB

#include <GrayModel.h>
#include <GrayPreset.h>
#include <GraySlot.h>
#include <TransitionScene.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "GrayRunner.h"

// The transition test (Display lab → Transition test; the display-lab epic, R4; InkBoy's calibration screen): the
// gray engine on the whole panel. Headed "Transition test", six rows: Run (its line: "Not run", or how the last run
// ended), Mode (Static, Slow, Fast or Ladder; a tap or Confirm steps it; Static on an UltraChip panel, Fast on the
// SSD1677 to start), Hold for photos (On or Off; a tap or Confirm toggles it; On on every visit, never saved; story
// dlab-c4, D13; "Always in Ladder" while Mode is Ladder, which holds whatever the row says), Clean (three cleaning FULL
// refreshes, white, black, white, no run), Speed (the last "N.N scans/s, N ms per scan"), Controls (help text).
//
// Run starts the test (GameDisplayLab::TransitionScene in the Mode row's mode) on the shared sweep runner (GrayRunner,
// extracted by story dlab-g3: the sweep task of D11, its slots, its stops, the cover and the session cap), which
// cleans the panel white, black, then white with three FULL refreshes (story dlab-g2) and then drives the mode's
// pattern from white through the session's gray preset (DisplayLabActivity::preset()): in Static, Slow and Fast seven
// level bands 0 (black) .. 6 (white) top to bottom (D14) with one rectangle per band, of level (band + 3) mod 7, in
// Ladder its columns. Fast: 12-px rectangles crossing the screen in 5 s, a 1 s pause, repeat, from the slot after the
// bands' paint has settled (they wait off-screen until then). Slow: 48-px rectangles whose crossing keeps a pixel under
// one for the preset's longest dose run in slots (GrayPreset::longestRunSlots) at the longest real slot length of the
// bands' paint; they wait off-screen until that paint has settled, then cross. Static: rectangles a third of the
// width wide that stand still; once a slot finds no pixel to drive the pattern has settled (logged) and flips, every
// level moved on by +1, +2, .., +6, +1, ... (six flips take the pixels through all 42 transitions), until stopped
// (story dlab-c3, D12). Ladder (story dlab-g1, D14): no bands but the panel's response to single pulses: the bottom
// half painted black (the base), then 24 columns per half, column n getting n pulses toward the opposite shade, one a
// slot and one scan each (TransitionScene::slotPreset, the preset with every hold bit set), end-aligned so every
// column's last pulse is in the stage's last slot, then the same pulses back; each stage ends at a photo spot, which
// holds whatever the Hold row says, and the run ends after the return ("Finished"). With Hold on (and always in Ladder)
// the run holds itself at each mode's photo spots (TransitionScene::slotDone decides: Static's settles, Slow's painted
// bands, its rectangles after a full dwell and its crossing's end, Fast's painted bands and its pause, the Ladder's two
// stage ends): the scene shows the hold marker, a pause symbol (two bars, TransitionScene::markerBars) in the logical
// bottom-right corner, in the opposite shade of the band under it, and stops the sweep clock, the task runs slots until
// the marker has settled, then holds (GrayRunner::holdForPhoto); Next resumes, the marker is driven back and settles,
// and only then does the run go on (Static's flip included). With Hold off nothing holds but the Ladder, and Static
// flips at once. In each slot the scene moves the rectangles to the sweep clock (prepareSlot), the runner sends the
// slot's scans, and the scene decides what it meant (afterSlot). While the test view is up the screen draws nothing
// (D4: the panel shows only what the scans drive); NavNext (side Down), Confirm, or a tap over the rows' area where
// touch routes while the view is up (the X4 Pro's does not, D12: its side buttons are the controls there) pauses or
// resumes, served at the end of the slot in flight; NavPrevious (side Up) or Back (the left-edge swipe included,
// checked before the swipe guard) ends the run after the scan or the clean in flight (the session cap's clean is one
// FULL refresh); any other swipe does nothing. A non-Done scan ends the run and the view with its outcome on the Run
// row. Another screen covering the view (the light panel) stops loop(), and the view ends once the cover closes; a
// forced refresh is swallowed. Leaving the view (only after the task has ended and been deleted) repaints the rows
// with a FULL refresh. The session cap's clean drops a hold in progress and restarts Static's flips and the Ladder
// (from its base), or ends a Ladder whose second photo was taken.
//
// Memory, all PSRAM, allocated in onEnter and freed in onExit: the runner's working set (GrayRunner::allocate). When
// any does not fit, the screen shows one notice row and runs nothing. The task's stack (SWEEP_STACK_BYTES, internal
// RAM) exists only while a run is up; when it cannot be created the Run row says so and nothing is sent.
class TransitionTestActivity final : public UiListActivity, private GrayRunner::Client {
 public:
  static constexpr const char* NAME = "DlabTransition";
  // The sweep task's stack, internal RAM, only while a run is up (plan-dlab-c2-sweep-task.md, Implementation Notes):
  // the measured SSD1677 peak, a log line inside the SDK's BUSY wait under a scan, about 2,464 B (newlib's printf
  // core is 800 B of it); + 512 B for an interrupt's frame and the coprocessor area = 2,976 B; + 2,048 B for an
  // UltraChip backend's per-scan session (two plane streams, the table, PLL and register writes, labEnd), not
  // measured = 5,024 B; + 2,048 B margin = 7,072 B; + the 1,024 B under which the run logs an error
  // (GrayRunner::SWEEP_STACK_MARGIN_BYTES) = 8,096 B, rounded up to 8 KiB. The run's log line gives the high-water
  // mark.
  static constexpr uint32_t SWEEP_STACK_BYTES = 8192;

  // `lab` is the menu under this screen on the stack, which outlives it.
  TransitionTestActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, DisplayLabActivity& lab);

  void loop() override;
  // The loop runs no slots, so it keeps its delay (a spinning loop would share the sweep task's core and priority);
  // preventAutoSleep keeps the clock up while the sweep advances (its task up and not asked to pause), and while the
  // run holds for a photo (Run is cleared then, but auto-sleep's FULL refresh would end the hold) for
  // GrayRunner::HOLD_AWAKE_MS from the hold's start; after that, and in a pause the owner asked for, the normal idle
  // timeout applies and auto-sleep ends the run as any leave does.
  bool skipLoopDelay() override { return false; }
  bool preventAutoSleep() override { return runner && runner->preventAutoSleep(); }
  // Ends the sweep task (the render mutex is held: GrayRunner::endLocked).
  void prepareForSleep() override;
  // Stops and joins the sweep task; false, so Home goes on (onExit promotes the FULL refresh).
  bool handleHomeGesture() override;
  // A forced refresh (the power or home button's Refresh) would send the framebuffer over the drive: swallowed while
  // the test view is up (D4).
  bool handleForcedRefresh() override { return shownOnPanel.load(); }

 private:
  enum class Row : uint8_t { Run, Mode, Hold, Clean, Stats, Controls };
  static constexpr size_t ROW_COUNT = 6;

  int listCount() const override { return notice ? 1 : static_cast<int>(ROW_COUNT); }
  const char* headerTitle() const override;
  void onEnter() override;
  // Ends the sweep task and frees every buffer; the render mutex is already held by the caller (no RenderLock here).
  void onExit() override;
  // Draws nothing while the test view is up; the base otherwise.
  void render(RenderLock&& lock) override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  // The test view's buttons: NavNext and Confirm pause, NavPrevious and Back stop; nothing in the rows view.
  bool handleCustomInput() override;
  // No selection moves while the test view is up.
  void navigateButtons() override;
  void onBackButton() override;

  void buildRows();

  // ---- loop task --------------------------------------------------------------------------------------------------
  // The Run row: the test view up and the sweep task created (it cleans and sweeps from 0). When the task cannot be
  // created: logged, the Run row says so, the view not entered.
  void startRun();
  // The Mode row: the next mode (Static, Slow, Fast, Ladder), for the next run.
  void stepMode();
  // The Hold row: holds at the photo spots on or off, for the next run.
  void toggleHold();
  // The Clean row: white framebuffer and the lab's cleaning FULL refresh, under the render lock; the rows come back
  // after it. Only in the rows view, where no sweep task is up.
  void cleanFromRows();
  // The Run row's line for how the task's run ended.
  const char* endText() const;
  // After a stop: endText() when the task was joined, "Still stopping" when it was left running past the bound.
  const char* stopText(bool joined) const;
  // The runner's working set and the scene over it; false (logged, the runner's buffers released) when one has no room.
  bool allocateWorkingSet();
  // Why no run starts while a sweep task is still ending: a restart when one was detached, else "Still stopping".
  const char* blockedText() const;
  // The task's last speed into the Speed line (kept as it was when there was none); only once the task is joined.
  void takeStats();
  // Back to the rows, repainted with a FULL refresh: when the task was `joined`, how the run ended on the Run row and
  // its last speed on the Speed row; else "Still stopping", and both wait for reap() to join it.
  void leaveView(bool joined);

  // ---- sweep task: the runner's client --------------------------------------------------------------------------
  // Every hook reaches the runner through `sweep`, the one it is handed, never through `runner` (null once detached).
  // The run's start: the scene from flip 0 and the Ladder's base, applied at clock 0, and the run's line.
  bool onReplay(GrayRunner& sweep, GameDisplayLab::GrayModel& model, const GameDisplayLab::GrayPreset& preset) override;
  // A pause between two pulse slots of a Ladder stage is counted for its settle line.
  void onPaused(GrayRunner& sweep) override;
  // A run held at a photo spot: the marker goes back to its band's shade before anything else moves.
  void onResume(GrayRunner& sweep, GameDisplayLab::GrayModel& model, const GameDisplayLab::GrayPreset& preset) override;
  // The session cap's clean: a hold in progress dropped, Static from flip 0, the Ladder from its base (false: the
  // Ladder's second photo was taken, the run is over).
  bool onCapClean(GrayRunner& sweep) override;
  // The rectangles moved to the clock, or the whole scene reapplied.
  bool prepareSlot(GrayRunner& sweep, GameDisplayLab::GrayModel& model, uint32_t prevMs, uint32_t nowMs, bool stale,
                   const GameDisplayLab::GrayPreset& preset) override;
  const GameDisplayLab::GrayPreset& slotPreset(GrayRunner& sweep, const GameDisplayLab::GrayPreset& preset) override {
    (void)sweep;
    return scene.slotPreset(preset);
  }
  // The Ladder's pulse-slot figures, then TransitionScene::slotDone decides (Static's settle and flip, Slow's measured
  // crossing, a photo spot, the marker settled out or back, a Ladder stage); this logs it and holds the run once the
  // marker is out.
  SlotVerdict afterSlot(GrayRunner& sweep, const GameDisplayLab::SlotResult& slot, uint32_t slotStartUs,
                        uint32_t periodUs, uint32_t elapsedMs) override;
  bool clockStill(const GrayRunner& sweep) const override {
    (void)sweep;
    return scene.clockStill();
  }
  // The Ladder's per-stage figures cleared: at each stage's end, a run's start and the session cap's restart.
  void resetLadderStage();
  // The Ladder's columns and rows at a run's start (`gray ladder:`), and the narrow-screen line when they are 1 px.
  void logLadderLayout() const;
  // A Ladder stage that settled: the base, or stage 1 or 2 with what the photo needs: the rate, the rhythm (and the
  // pauses that broke it, and where), the stage's longest pulse interval and slot, the base's 6>0 dose and the columns.
  void logLadderSettled(const GameDisplayLab::LadderSettled& settled) const;

  DisplayLabActivity& lab;
  // Why the screen cannot run (its one row), or null.
  const char* notice = nullptr;
  // The working set was not allocated at onEnter, a detached task still holding its own: Run allocates it.
  bool deferred = false;
  // The Run row's line and the speed line.
  const char* status = nullptr;
  char statsText[48] = {};
  freeink::ui::ListItem rowItems[ROW_COUNT] = {};
  // The test view is up: the panel shows the drive and render() draws nothing. Written by the loop task, read by the
  // render task.
  std::atomic<bool> shownOnPanel{false};
  // The Mode row's mode: the loop task's, set into the scene before the sweep task is created. onEnter starts it at
  // Static on an UltraChip panel and Fast on the SSD1677 (D12).
  GameDisplayLab::SweepMode sweepMode = GameDisplayLab::SweepMode::Fast;
  // The Hold row: the loop task's, set into the scene before the sweep task is created; onEnter turns it on.
  bool holdOn = true;

  // The sweep task and its working set; this screen is its client.
  // Null when it had no room (the notice row), or once endLocked detached it.
  std::unique_ptr<GrayRunner> runner;

  // ---- the sweep task's own, while it is up (read by the loop task only once it is deleted) ------------------------
  GameDisplayLab::TransitionScene scene;
  // Ladder: the current stage's manual pauses between two of its pulse slots (TransitionScene::ladderMidStage; each
  // breaks the one-pulse-a-slot rhythm) and its longest pulse slot, for its settle line; cleared by resetLadderStage.
  uint32_t ladderPauses = 0;
  uint32_t ladderSlotUs = 0;
  // The stage slots (TransitionScene::ladderStageSlots) the first LADDER_PAUSES_NAMED pauses fell after, for the line.
  static constexpr uint32_t LADDER_PAUSES_NAMED = 4;
  uint8_t ladderPauseSlots[LADDER_PAUSES_NAMED] = {};
  // The longest start-to-start interval between two of the stage's pulse slots (a skipped slot period or a pause shows
  // here), and the last pulse slot's start (ladderPulsed: there was one in this stage).
  uint32_t ladderIntervalUs = 0;
  uint32_t ladderLastStartUs = 0;
  bool ladderPulsed = false;
};

#endif  // CROSSHATCH_DISPLAY_LAB
