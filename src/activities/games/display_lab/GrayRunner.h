#pragma once

#include "DisplayLabActivity.h"

#if CROSSHATCH_DISPLAY_LAB

#include <GrayModel.h>
#include <GrayPreset.h>
#include <GraySlot.h>
#include <HalMemory.h>
#include <MaskPlanes.h>
#include <SlotScheduler.h>
#include <SweepControl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

class GfxRenderer;

// The gray engine's sweep runner, shared by the transition test (TransitionTestActivity) and the gray viewer
// (GrayViewerActivity): extracted from the transition test by story dlab-g3, its behaviour unchanged. A screen owns
// one and implements its Client: what the screen paints and decides per slot (the scene); the runner owns everything
// that touches the sweep task or the panel.
//
// The working set (allocate): the model (3 B a pixel), the follow-up masks, the history (3 B a pixel) only when the
// preset's history mode is on, and the weak bitmap only when weak mode is on and the backend has a weak row; all PSRAM,
// allocated in the screen's onEnter and freed in its onExit (release).
//
// A run (prepare, then launch, from the loop task with the screen's view up) is the sweep task (the epic's D11): it
// cleans the panel to white with a FULL refresh, asks the client to paint (onReplay), then runs one slot per
// GameDisplayLab::SlotScheduler period: the client moves its picture (prepareSlot), GameDisplayLab::runGraySlot sends
// the drive scan and the follow-up scan through DisplayLabActivity::scan and commits what was delivered, and the
// client decides what the slot means (afterSlot: a settle, a photo spot, the run's end). The loop task keeps polling
// input and talks to the task only through GameDisplayLab::SweepControl and a notification: press (a pause or a
// resume, served at the end of the slot in flight, or, in the gray viewer while the task holds, the next picture: one
// step on the word), stop. A hold for a photo (holdForPhoto, which the client calls from afterSlot once its
// marker has settled) stops the clock and the slots and clears Run, so one press resumes; preventAutoSleep keeps the
// device awake through it for HOLD_AWAKE_MS. A non-Done scan ends the run (End::ScanFailed with its outcome). Another
// screen covering the view stops the loop: the task refuses its next scan once the heartbeat is older than
// COVERED_GAP_US, and viewEnds() reports the cover. Before a slot that would cross the session cap the task cleans
// first (the model reset, the client told: onCapClean), so the lab's own cap clean never lands between a slot's two
// scans; the sweep clock stands still through it. Logs `DLAB gray scans_s=N.N ms_per_scan=N skipped=N` (skipped: slot
// periods lost in that window) about every 2 s while running and on every stop, and the task's stack high-water mark
// once per run.
//
// Stopping the task: loop() (Back, a cover, the task's own end) and handleHomeGesture() run without the render mutex,
// so they ask it to stop and join it (stop). onExit() and prepareForSleep() run with the non-recursive render mutex
// held, which DisplayLabActivity::scan takes, so they must not wait for a task blocked on it: endLocked() deletes the
// task when it is inside the panel and blocked (waiting for that mutex, holding nothing), and otherwise waits for it to
// end on its own. Both waits are bounded (STOP_WAIT_MS): stop() then leaves the task running and the screen joins it
// later (reap); endLocked() detaches the runner, which the task frees itself when it ends. A task that was not
// detached never deletes itself: it parks after publishing its end, and only the screen (through the runner) deletes
// it, so a notification never reaches a freed task; a detached one is notified by no one.
class GrayRunner {
 public:
  // How often the running sweep logs and keeps its speed.
  static constexpr uint32_t STATS_EVERY_US = 2000000;
  // Scans in one slot: the drive scan and the follow-up.
  static constexpr uint32_t SCANS_PER_SLOT = 2;
  // A gap between two loop() calls this long while the view is up means another screen covered it. loop() only polls
  // input (about 10 ms a pass), so a short gap bounds the scans that land under a covering screen.
  static constexpr uint32_t COVERED_GAP_US = 300000;
  // Priority and core as GameVM's task. The core is the loop task's (Arduino's loop runs on core 1): endLocked's
  // check-then-delete relies on the sweep task not running while the loop task does.
  static constexpr int SWEEP_PRIORITY = 1;
  static constexpr int SWEEP_CORE = 1;
  // How often stop() polls for the task's end.
  static constexpr uint32_t STOP_POLL_MS = 5;
  // The longest stop() and endLocked() wait for the task: past SCAN_TIMEOUT_MS and a decode's row (the SDK's own BUSY
  // wait can run to 30 s, so a scan stuck there outlasts it: stop() then shows "Still stopping" until it returns).
  // Past it, stop() leaves the task running undeleted and endLocked() detaches it, so Back, Home and sleep wait at
  // most this long for a task that does not return.
  static constexpr uint32_t STOP_WAIT_MS = 20000;
  // How long a hold for a photo keeps the device awake from its start: long enough to set up and take the photos,
  // short enough that a forgotten hold does not keep the CPU up indefinitely (main.cpp resets the idle clock and turns
  // power saving off on every pass while preventAutoSleep is true).
  static constexpr uint32_t HOLD_AWAKE_MS = 10 * 60 * 1000;
  // A run whose stack high-water mark leaves fewer free bytes than this logs an error.
  static constexpr uint32_t SWEEP_STACK_MARGIN_BYTES = 1024;

  // How the sweep task's run ended (Finished: the client's own end, the Ladder's; PaintFailed: the client could not
  // paint, its reason its own).
  enum class End : uint8_t { Stopped, Covered, ScanFailed, Finished, PaintFailed };

  // What a screen paints and decides. Every hook runs on the sweep task, which owns the model while it is up.
  class Client {
   public:
    // What a slot left for the runner.
    struct SlotVerdict {
      // Retarget every pixel to the picture before the next slot (prepareSlot's `stale`).
      bool reapply = false;
      // The run is over (End::Finished).
      bool finished = false;
    };

    // A run's start or a replay, after the clean to white (the model reset to white): paint the whole picture from
    // the start (clock 0). False when it cannot (End::PaintFailed, or Stopped when a stop was asked meanwhile).
    virtual bool onReplay(GameDisplayLab::GrayModel& model, const GameDisplayLab::GrayPreset& preset) = 0;
    // The owner paused the run (not a hold), between two slots.
    virtual void onPaused() {}
    // A resume arrived: true keeps the run holding instead (a picture that is finished and held, which a press does
    // not resume; the client logs it). Asked before the resume line and the slot clock.
    virtual bool keepHolding() { return false; }
    // The run resumes (after a pause or a hold), the hold already released: a client holding at a photo spot drives
    // its marker back.
    virtual void onResume(GameDisplayLab::GrayModel& model, const GameDisplayLab::GrayPreset& preset) {
      (void)model;
      (void)preset;
    }
    // The session cap's clean repainted the panel white and reset the model: false when the run is over (Finished).
    virtual bool onCapClean() = 0;
    // Before a slot at sweep clock `nowMs` (the last slot's at `prevMs`): move the picture; with `stale`, retarget all
    // of it (after a clean or a reapply). False when it cannot (as onReplay).
    virtual bool prepareSlot(GameDisplayLab::GrayModel& model, uint32_t prevMs, uint32_t nowMs, bool stale,
                             const GameDisplayLab::GrayPreset& preset) = 0;
    // The preset the next slot runs on; `preset` itself by default.
    virtual const GameDisplayLab::GrayPreset& slotPreset(const GameDisplayLab::GrayPreset& preset) { return preset; }
    // After a delivered slot that began at `slotStartUs`, took `periodUs` (its real length) at sweep clock
    // `elapsedMs`; the model has committed it. The client may call holdForPhoto() here.
    virtual SlotVerdict afterSlot(const GameDisplayLab::SlotResult& slot, uint32_t slotStartUs, uint32_t periodUs,
                                  uint32_t elapsedMs) = 0;
    // The sweep clock stands still (a hold's marker slots).
    virtual bool clockStill() const { return false; }

   protected:
    ~Client() = default;
  };

  // `who` names the screen in the allocation's log lines; `stackBytes` is the task's stack (internal RAM, only while a
  // run is up). `lab` is the menu under the screen, which outlives it.
  GrayRunner(GfxRenderer& renderer, DisplayLabActivity& lab, Client& client, const char* who, uint32_t stackBytes);

  // ---- loop task --------------------------------------------------------------------------------------------------
  // The working set for a logical screen logicalW x logicalH, which must cover the lab's panel; the model reset to
  // white. False, having logged why, when the lab has no session or one buffer has no room (call release()).
  bool allocate(int logicalW, int logicalH);
  void release();
  // A new run's state: no task alive (running() false). The client is configured after this and before launch().
  // With `pressesWaitForFirstPaint`, presses are ignored until the run's first paint is served (the gray viewer, whose
  // presses write the entry the task reads at a replay).
  void prepare(bool pressesWaitForFirstPaint = false);
  // Creates the sweep task, which cleans and paints from the start; false (logged) when it cannot be created.
  bool launch();
  // The task is up (it may have ended and be parked until a stop joins it).
  bool running() const { return sweepTask != nullptr; }
  using Press = GameDisplayLab::SweepControl::Press;
  // NavNext, Confirm or a tap, in one step on the request word (SweepControl::press): Ignored while a replay with
  // presses waiting is pending; with `nextFromHold` (the gray viewer) and the task holding, the next picture (Next);
  // otherwise a pause or a resume (Toggled), served at the end of the slot in flight. Ignored without a task.
  Press press(bool nextFromHold);
  // A replay with presses waiting is pending (the gray viewer reads it before it writes the next entry).
  bool replayPending() const { return control.replayPending(); }
  // Unlocked stop (loop(), handleHomeGesture()): asks the task to stop, waits for its end (after the scan, the clean
  // or the decode row in flight) at most STOP_WAIT_MS, and deletes it: true. Past that, logged and left running, false
  // (stillStopping() until reap() joins it; the screen shows "Still stopping" and reads no end until then). The caller
  // holds no render lock.
  bool stop();
  // A stop left the task running past STOP_WAIT_MS and it has not been joined yet.
  bool stillStopping() const { return sweepTask != nullptr && control.stopRequested(); }
  // From the rows view's loop: joins a task stop() left running once it has ended; true when it joined one.
  bool reap();
  // Locked stop (onExit(), prepareForSleep()): the caller holds the render mutex. Asks `owned`'s task to stop and waits
  // for its end, but deletes it at once when it is blocked inside the panel, where it can only be waiting for that
  // mutex. Waits at most STOP_WAIT_MS: past that the runner is detached (logged once, naming the screen), `owned`
  // released to the task, which frees it (the model, the masks, every buffer it can touch) when it ends; `owned` is
  // then null and the caller frees nothing of it.
  static void endLocked(std::unique_ptr<GrayRunner>& owned);
  // Runners detached by endLocked whose task has not ended yet; no screen starts a run while there are any.
  static int detachedTasks();
  // The view must end now (a pass of loop() while the view is up at `nowUs`): another screen covered it (a heartbeat
  // older than COVERED_GAP_US, logged), or the task ended on its own or was ended; the task has then been joined. The
  // caller leaves the view and heartbeats.
  bool viewEnds(uint32_t nowUs);
  // Once per loop() pass: the task refuses its next scan once this is older than COVERED_GAP_US.
  void heartbeat(uint32_t nowUs) { control.heartbeat(nowUs); }
  bool preventAutoSleep() const;
  // How the last run ended, and the failing scan's outcome when it was ScanFailed; read once the task is gone (never
  // while stillStopping()).
  End end() const { return sweepEnd; }
  DisplayLabActivity::Scan failedScan() const { return failedScan_; }
  // The last reported speed (statsReported: there was one), for the Speed row once the view ends.
  bool statsReported() const { return statsReported_; }
  uint32_t reportedTenths() const { return reportedTenths_; }
  uint32_t reportedMsPerScan() const { return reportedMsPerScan_; }
  uint32_t stackBytes() const { return stackBytes_; }

  // ---- either side --------------------------------------------------------------------------------------------------
  // A stop was asked for (the client's long work, a decode, checks it to end early).
  bool stopRequested() const { return control.stopRequested(); }
  // A result code the client keeps in the runner rather than in itself (the gray viewer's read error), so a task
  // returning after the screen is gone writes nothing of the screen; read once the task is gone.
  void setClientCode(const uint8_t code) { clientCode_.store(code); }
  uint8_t clientCode() const { return clientCode_.load(); }
  bool hasHistory() const { return historyBuffer.get() != nullptr; }
  bool weakRow() const { return weakRow_; }

  // ---- sweep task (the client's hooks) ------------------------------------------------------------------------------
  // Held for a photo: nothing is driven until Next (or Confirm) resumes. The task clears Run itself so one press
  // resumes: SweepControl::holdRun sets Held and clears Run in one step, after the hold's start is stored, so
  // preventAutoSleep (one load of both) always sees the run or the hold.
  void holdForPhoto();
  GameDisplayLab::GrayModel& model() { return model_; }

 private:
  static void sweepEntry(void* param);
  // endLocked's body, on the runner `owned` holds.
  void endLockedOrDetach(std::unique_ptr<GrayRunner>& owned);
  // The current client (an inert one once detached).
  Client& client() { return *client_.load(); }
  // The task published its end (and was not detached).
  bool taskEnded() const { return taskState.load(std::memory_order_acquire) == TASK_DONE; }
  // The task's run: serves the requests at each slot boundary and runs the slots until a stop, a cover, a failed scan
  // or the client's end; then logs the stack's high-water mark (sweepEntry publishes the end).
  void sweepRun();
  // Serves one take()'s requests: a pause, a replay or a clean, a resume. False when a clean was refused or the paint
  // failed (sweepEnd says why). The screens send Replay to start a run (and the viewer for its next picture) and Clean
  // no longer (the transition test's Clean row runs without the task); SweepControl's protocol keeps both.
  bool serve(const GameDisplayLab::SweepControl::Requests& requests);
  // White framebuffer, the lab's cleaning FULL refresh under the render lock, the model reset to white, the picture to
  // reapply; inside the panel gate. False (sweepEnd set) when the gate refused.
  bool cleanToWhite();
  // The client could not paint: Stopped when a stop was asked meanwhile (its decode ended early), else PaintFailed.
  bool paintFailed();
  // Records a refused gate in sweepEnd; true when `gate` let the task in.
  bool admitted(GameDisplayLab::SweepControl::Gate gate);
  // One slot at `nowUs`; false when the run ends (sweepEnd says why).
  bool runSlot(uint32_t nowUs);
  // The sweep clock advanced to `nowUs` (it runs only while sweeping, and stands still while the client says so).
  void advanceClock(uint32_t nowUs);
  // Logs the window's speed (`DLAB gray scans_s=...`).
  void logStats(uint32_t nowUs) const;
  // The run's stack line (size, `freeBytes` at the high-water mark, the least room before a scan), and an error when
  // `freeBytes` is under SWEEP_STACK_MARGIN_BYTES; from the task's end, or after endLocked deleted it.
  void logStackEnd(uint32_t freeBytes) const;
  // Logs the speed and, when the window delivered a scan, keeps its figures for the Speed row; starts a new window.
  void reportStats(uint32_t nowUs);
  void notifySweep();

  GfxRenderer& renderer;
  DisplayLabActivity& lab;
  // The screen, or, once detached, an inert client the task can ask anything.
  std::atomic<Client*> client_;
  const char* who;
  uint32_t stackBytes_;

  HalMemory::PsramBuffer pixelBuffer;
  HalMemory::PsramBuffer followBlack;
  HalMemory::PsramBuffer followWhite;
  HalMemory::PsramBuffer historyBuffer;
  HalMemory::PsramBuffer weakBuffer;
  // The weak row is in use: weak mode on and the backend has one (and its bitmap fitted).
  bool weakRow_ = false;

  // Loop task to sweep task: the requests and the heartbeat. Sweep task to loop task: taskState Done (release), after
  // which the loop task may read the task's fields below; the loop task alone creates and deletes the task, except a
  // detached one, which deletes itself (sweepEntry). Running becomes Done (the task's end) or Detached (endLocked's
  // give-up) in one exchange, so the two can never both happen.
  static constexpr uint8_t TASK_RUNNING = 0;
  static constexpr uint8_t TASK_DONE = 1;
  static constexpr uint8_t TASK_DETACHED = 2;
  GameDisplayLab::SweepControl control;
  TaskHandle_t sweepTask = nullptr;
  std::atomic<uint8_t> taskState{TASK_RUNNING};
  std::atomic<uint8_t> clientCode_{0};
  // The millis() the run's hold for a photo began at (the hold itself is the request word's Held), written by the
  // sweep task before it holds; read by preventAutoSleep.
  std::atomic<uint32_t> heldSinceMs{0};

  // ---- the sweep task's own, while it is up (read by the loop task only once it has ended or been deleted) ---------
  GameDisplayLab::GrayModel model_;
  GameDisplayLab::MaskPlanes follow;
  GameDisplayLab::SlotScheduler scheduler;
  End sweepEnd = End::Stopped;
  DisplayLabActivity::Scan failedScan_ = DisplayLabActivity::Scan::Done;
  bool statsReported_ = false;
  uint32_t reportedTenths_ = 0;
  uint32_t reportedMsPerScan_ = 0;
  // The sweep advances (served Run; the task's own view of the request).
  bool sweeping = false;
  // The model must be retargeted to the whole picture before the next slot (after a clean or a reapply).
  bool sceneStale = false;
  // The sweep clock, which stands still while paused, the micros() it was last advanced at, and the clock the picture
  // was last moved to.
  uint64_t elapsedUs = 0;
  uint32_t clockUs = 0;
  uint32_t sceneMs = 0;
  uint32_t statsSinceUs = 0;
  // The task's stack start (pxTaskGetStackStart), and the least room above it seen just before a scan.
  uintptr_t stackFloor = 0;
  uint32_t leastScanHeadroom = UINT32_MAX;
};

#endif  // CROSSHATCH_DISPLAY_LAB
