#pragma once

#include "DisplayLabActivity.h"

#if CROSSHATCH_DISPLAY_LAB

#include <GrayModel.h>
#include <HalMemory.h>
#include <MaskPlanes.h>
#include <SlotScheduler.h>
#include <SweepControl.h>
#include <TransitionScene.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

// The transition test (Display lab → Transition test; the display-lab epic, R4; InkBoy's calibration screen): the
// gray engine on the whole panel. Headed "Transition test", six rows: Run (its line: "Not run", or how the last run
// ended), Mode (Static, Slow or Fast; a tap or Confirm steps it; Static on an UltraChip panel, Fast on the SSD1677 to
// start), Hold for photos (On or Off; a tap or Confirm toggles it; On on every visit, never saved; story dlab-c4, D13),
// Clean (one cleaning FULL refresh to white, no run), Speed (the last "N.N scans/s, N ms per scan"), Controls (help
// text).
//
// Run starts the test (GameDisplayLab::TransitionScene in the Mode row's mode) on its own FreeRTOS task, the sweep task
// (D11), which cleans the panel to white with a FULL refresh and then drives four shade bands K, D, L, W top to bottom
// with three rectangles of the other shades per band, from white through the session's gray preset
// (DisplayLabActivity::preset()). Fast: 12-px rectangles crossing the screen in 5 s, a 1 s pause, repeat. Slow: 48-px
// rectangles whose crossing keeps a pixel under one for the preset's longest dose run in slots
// (GrayPreset::longestRunSlots) at the longest real slot length of the bands' paint; they wait off-screen until that
// paint has settled, then cross. Static: rectangles a third of the width wide that stand still; once a slot finds no
// pixel to drive the pattern has settled (logged) and flips, every shade moved on by +1, +2, +3, +1, ... (three flips
// take the pixels through all twelve transitions), until stopped (story dlab-c3, D12). With Hold on the run holds
// itself at each mode's photo spots (TransitionScene::slotDone decides: Static's settles, Slow's painted bands, its
// rectangles after a full dwell and its crossing's end, Fast's pause): the scene shows a 32 px dash at the logical
// bottom-right, in the bottom band's margin below its last lane's rectangle, in the opposite shade of its band and
// stops the sweep clock, the task runs slots until the marker has settled, then clears Run itself
// (SweepControl::holdRun); Next resumes, the marker is driven back and settles, and only then does the run go on
// (Static's flip included). With Hold off nothing holds and Static flips at once. One slot per
// GameDisplayLab::SlotScheduler period: the scene moves the rectangles to the sweep clock, then
// GameDisplayLab::runGraySlot sends the drive scan and the follow-up scan through DisplayLabActivity::scan and commits
// what was delivered. The task owns the slot runner (the clock, the scheduler, the stats, the scene, the model); the
// loop task keeps polling input, so the controls answer while a scan runs, and talks to the task only through
// GameDisplayLab::SweepControl and a notification. While the test view is up the screen draws nothing (D4: the panel
// shows only what the scans drive); NavNext (side Down), Confirm, or a tap over the rows' area where touch routes while
// the view is up (the X4 Pro's does not, D12: its side buttons are the controls there) pauses or resumes, served at the
// end of the slot in flight; NavPrevious (side Up) or Back (the left-edge swipe included, checked before the swipe
// guard) ends the run after the scan or the clean in flight (the session cap's clean is one FULL refresh); any other
// swipe does nothing. A non-Done scan ends the run and the view with its outcome on the Run row. Another screen
// covering the view (the light panel) stops loop(): the task refuses its next scan once loop() has not run for
// COVERED_GAP_US, and the view ends once the cover closes; a forced refresh is swallowed. Leaving the view (only after
// the task has ended and been deleted) repaints the rows with a FULL refresh. Before a slot that would cross the
// session cap the task cleans first (white panel, model reset, the scene reapplied; the sweep clock stands still
// through it), so the lab's own cap clean never lands between a slot's two scans. Logs `DLAB gray scans_s=N.N
// ms_per_scan=N skipped=N` (skipped: slot periods lost in that window) about every 2 s while running and on every stop,
// and the task's stack high-water mark once per run.
//
// Stopping the task: loop() (Back, a cover, the task's own end) and handleHomeGesture() run without the render mutex,
// so they ask it to stop and join it (stopSweep). onExit() and prepareForSleep() run with the non-recursive render
// mutex held, which DisplayLabActivity::scan takes, so they must not wait for a task blocked on it: endSweepLocked()
// deletes the task when it is inside the panel and blocked (waiting for that mutex, holding nothing), and otherwise
// waits for it to end on its own. The task never deletes itself: it parks after publishing sweepDone, and only this
// screen deletes it, so a notification never reaches a freed task.
//
// Memory, all PSRAM, allocated in onEnter and freed in onExit: the model (3 B a pixel, 1,152,000 B at 800x480), the
// follow-up masks (2 x 48,000 B), the history (3 B a pixel, 1,152,000 B) only when the preset's history mode is on,
// and the weak bitmap (48,000 B) only when weak mode is on and the backend has a weak row. When any does not fit, the
// screen shows one notice row and runs nothing. The task's stack (SWEEP_STACK_BYTES, internal RAM) exists only while
// a run is up; when it cannot be created the Run row says so and nothing is sent.
class TransitionTestActivity final : public UiListActivity {
 public:
  static constexpr const char* NAME = "DlabTransition";
  // How often the running sweep logs and shows its speed.
  static constexpr uint32_t STATS_EVERY_US = 2000000;
  // Scans in one slot: the drive scan and the follow-up.
  static constexpr uint32_t SCANS_PER_SLOT = 2;
  // A gap between two loop() calls this long while the test view is up means another screen covered it. loop() only
  // polls input now (about 10 ms a pass), so a short gap bounds the scans that land under a covering screen.
  static constexpr uint32_t COVERED_GAP_US = 300000;
  // The sweep task's stack, internal RAM, only while a run is up (plan-dlab-c2-sweep-task.md, Implementation Notes):
  // the measured SSD1677 peak, a log line inside the SDK's BUSY wait under a scan, about 2,464 B (newlib's printf
  // core is 800 B of it); + 512 B for an interrupt's frame and the coprocessor area = 2,976 B; + 2,048 B for an
  // UltraChip backend's per-scan session (two plane streams, the table, PLL and register writes, labEnd), not
  // measured = 5,024 B; + 2,048 B margin = 7,072 B; + the 1,024 B under which the run logs an error
  // (SWEEP_STACK_MARGIN_BYTES) = 8,096 B, rounded up to 8 KiB. The run's log line gives the high-water mark.
  static constexpr uint32_t SWEEP_STACK_BYTES = 8192;
  // Priority and core as GameVM's task. The core is the loop task's (Arduino's loop runs on core 1):
  // endSweepLocked's check-then-delete relies on the sweep task not running while the loop task does.
  static constexpr int SWEEP_PRIORITY = 1;
  static constexpr int SWEEP_CORE = 1;
  // How often stopSweep() polls for the task's end.
  static constexpr uint32_t STOP_POLL_MS = 5;
  // How long a hold for a photo keeps the device awake from its start: long enough to set up and take the photos, short
  // enough that a forgotten hold does not keep the CPU up indefinitely (main.cpp resets the idle clock and turns power
  // saving off on every pass while preventAutoSleep is true).
  static constexpr uint32_t HOLD_AWAKE_MS = 10 * 60 * 1000;
  // A run whose stack high-water mark leaves fewer free bytes than this logs an error.
  static constexpr uint32_t SWEEP_STACK_MARGIN_BYTES = 1024;

  // `lab` is the menu under this screen on the stack, which outlives it.
  TransitionTestActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, DisplayLabActivity& lab);

  void loop() override;
  // The loop runs no slots, so it keeps its delay (a spinning loop would share the sweep task's core and priority);
  // preventAutoSleep keeps the clock up while the sweep advances (its task up and not asked to pause), and while the
  // run holds for a photo (Run is cleared then, but auto-sleep's FULL refresh would end the hold) for HOLD_AWAKE_MS
  // from the hold's start; after that, and in a pause the owner asked for, the normal idle timeout applies and
  // auto-sleep ends the run as any leave does.
  bool skipLoopDelay() override { return false; }
  bool preventAutoSleep() override;
  // Ends the sweep task (the render mutex is held: endSweepLocked).
  void prepareForSleep() override;
  // Stops and joins the sweep task; false, so Home goes on (onExit promotes the FULL refresh).
  bool handleHomeGesture() override;
  // A forced refresh (the power or home button's Refresh) would send the framebuffer over the drive: swallowed while
  // the test view is up (D4).
  bool handleForcedRefresh() override { return shownOnPanel.load(); }

 private:
  enum class Row : uint8_t { Run, Mode, Hold, Clean, Stats, Controls };
  static constexpr size_t ROW_COUNT = 6;
  // How the sweep task's run ended.
  enum class SweepEnd : uint8_t { Stopped, Covered, ScanFailed };

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

  // The model, the follow-up masks and the optional arrays; false, having logged why, when one has no room.
  bool allocate();
  void release();
  void buildRows();

  // ---- loop task --------------------------------------------------------------------------------------------------
  // The Run row: the test view up and the sweep task created (it cleans and sweeps from 0). When the task cannot be
  // created: logged, the Run row says so, the view not entered.
  void startRun();
  // NavNext, Confirm or a tap: pause or resume (a hold's resume too), served at the end of the slot in flight.
  void togglePause();
  // The Mode row: the next mode (Static, Slow, Fast), for the next run.
  void stepMode();
  // The Hold row: holds at the photo spots on or off, for the next run.
  void toggleHold();
  // The Clean row: white framebuffer and the lab's cleaning FULL refresh, under the render lock; the rows come back
  // after it. Only in the rows view, where no sweep task is up.
  void cleanFromRows();
  // Wakes the sweep task to read its requests.
  void notifySweep();
  // Unlocked stop (loop(), handleHomeGesture()): asks the task to stop, waits for its end (after the scan or the
  // clean in flight), and deletes it. The caller holds no render lock.
  void stopSweep();
  // Locked stop (onExit(), prepareForSleep()): the caller holds the render mutex. Asks the task to stop and waits for
  // its end, but deletes it at once when it is blocked inside the panel, where it can only be waiting for that mutex.
  void endSweepLocked();
  // The Run row's line for how the task's run ended.
  const char* endText() const;
  // Back to the rows with `outcome` on the Run row and the task's last speed on the Speed row, repainted with a FULL
  // refresh. Only once the task is gone.
  void leaveView(const char* outcome);

  // ---- sweep task -------------------------------------------------------------------------------------------------
  static void sweepEntry(void* param);
  // The task's run: serves the requests at each slot boundary and runs the slots until a stop, a cover or a failed
  // scan; then logs the stack's high-water mark and publishes sweepDone.
  void sweepRun();
  // Serves one take()'s requests: a pause, a replay or a clean, a resume. False when a clean was refused (sweepEnd
  // says why). The screen sends Replay only to start a run and Clean no longer (the Clean row runs without the task);
  // SweepControl's protocol keeps both.
  bool serve(const GameDisplayLab::SweepControl::Requests& requests);
  // White framebuffer, the lab's cleaning FULL refresh under the render lock, the model reset to white, the scene to
  // reapply; inside the panel gate. False (sweepEnd set) when the gate refused.
  bool cleanToWhite();
  // Records a refused gate in sweepEnd; true when `gate` let the task in.
  bool admitted(GameDisplayLab::SweepControl::Gate gate);
  // One slot at `nowUs`; false when the run ends (sweepEnd says why).
  bool runSlot(uint32_t nowUs);
  // After a slot at the sweep clock `elapsedMs` that drove `driven` pixels (`driveDone`: its drive scan was delivered)
  // and took `periodUs`: TransitionScene::slotDone decides (Static's settle and flip, Slow's measured crossing, a photo
  // spot, the marker settled out or back); this logs it, holds the run once the marker is out, and marks the scene for
  // reapplying when asked.
  void afterSlot(size_t driven, bool driveDone, uint32_t periodUs, uint32_t elapsedMs);
  // The sweep clock advanced to `nowUs` (it runs only while sweeping, and stands still through a hold's marker slots).
  void advanceClock(uint32_t nowUs);
  // Logs the window's speed (`DLAB gray scans_s=...`).
  void logStats(uint32_t nowUs) const;
  // The run's stack line (size, `freeBytes` at the high-water mark, the least room before a scan), and an error when
  // `freeBytes` is under SWEEP_STACK_MARGIN_BYTES; from the task's end, or after endSweepLocked deleted it.
  void logStackEnd(uint32_t freeBytes) const;
  // Logs the speed and, when the window delivered a scan, keeps its figures for the Speed row; starts a new window.
  void reportStats(uint32_t nowUs);

  DisplayLabActivity& lab;
  HalMemory::PsramBuffer pixelBuffer;
  HalMemory::PsramBuffer followBlack;
  HalMemory::PsramBuffer followWhite;
  HalMemory::PsramBuffer historyBuffer;
  HalMemory::PsramBuffer weakBuffer;
  // The weak row is in use: weak mode on and the backend has one (and its bitmap fitted).
  bool weakRow = false;
  // Why the screen cannot run (its one row), or null.
  const char* notice = nullptr;
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

  // Loop task to sweep task: the requests and the heartbeat. Sweep task to loop task: sweepDone (release), after which
  // the loop task may read the task's fields below; the loop task alone creates and deletes the task.
  GameDisplayLab::SweepControl control;
  TaskHandle_t sweepTask = nullptr;
  std::atomic<bool> sweepDone{false};
  // The run holds for a photo, and the millis() it began at: set by the sweep task when the marker has settled out
  // (the start first), cleared when a resume drives it back, when a run starts, and when the task ends; read by
  // preventAutoSleep.
  std::atomic<bool> heldForPhoto{false};
  std::atomic<uint32_t> heldSinceMs{0};

  // ---- the sweep task's own, while it is up (read by the loop task only after sweepDone or once it is deleted) -----
  GameDisplayLab::GrayModel model;
  GameDisplayLab::MaskPlanes follow;
  GameDisplayLab::TransitionScene scene;
  GameDisplayLab::SlotScheduler scheduler;
  // How the run ended, and the failing scan's outcome when it was ScanFailed.
  SweepEnd sweepEnd = SweepEnd::Stopped;
  DisplayLabActivity::Scan failedScan = DisplayLabActivity::Scan::Done;
  // The last reported speed, for the Speed row once the view ends.
  bool statsReported = false;
  uint32_t reportedTenths = 0;
  uint32_t reportedMsPerScan = 0;
  // The sweep advances (served Run; the task's own view of the request).
  bool sweeping = false;
  // The model must be retargeted to the whole scene before the next slot (after a clean or a Static flip).
  bool sceneStale = false;
  // The sweep clock, which stands still while paused, the micros() it was last advanced at, and the scene time the
  // model was last moved to.
  uint64_t elapsedUs = 0;
  uint32_t clockUs = 0;
  uint32_t sceneMs = 0;
  uint32_t statsSinceUs = 0;
  // The task's stack start (pxTaskGetStackStart), and the least room above it seen just before a scan.
  uintptr_t stackFloor = 0;
  uint32_t leastScanHeadroom = UINT32_MAX;
};

#endif  // CROSSHATCH_DISPLAY_LAB
