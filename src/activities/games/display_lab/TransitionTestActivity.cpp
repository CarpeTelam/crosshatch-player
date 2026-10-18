#include "TransitionTestActivity.h"

#if CROSSHATCH_DISPLAY_LAB

#include <Arduino.h>
#include <GfxRenderer.h>
#include <GraySlot.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <optional>
#include <utility>

#include "MappedInputManager.h"
#include "StockBenchGate.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

using GameDisplayLab::GrayModel;
using GameDisplayLab::GrayPreset;
using GameDisplayLab::MaskPlanes;
using GameDisplayLab::PanelTurn;
using GameDisplayLab::Shade;
using GameDisplayLab::SweepMode;

namespace {

// GfxRenderer's four orientations and the scene's panel turns name the same rotations in the same order, so the
// renderer's orientation is the scene's turn by value; the asserts pin each pair by name.
static_assert(static_cast<int>(PanelTurn::Portrait) == GfxRenderer::Portrait, "PanelTurn follows GfxRenderer");
static_assert(static_cast<int>(PanelTurn::LandscapeClockwise) == GfxRenderer::LandscapeClockwise,
              "PanelTurn follows GfxRenderer");
static_assert(static_cast<int>(PanelTurn::PortraitInverted) == GfxRenderer::PortraitInverted,
              "PanelTurn follows GfxRenderer");
static_assert(static_cast<int>(PanelTurn::LandscapeCounterClockwise) == GfxRenderer::LandscapeCounterClockwise,
              "PanelTurn follows GfxRenderer");

PanelTurn turnOf(const GfxRenderer::Orientation orientation) { return static_cast<PanelTurn>(orientation); }

// The Mode row's line.
const char* modeText(const SweepMode mode) {
  switch (mode) {
    case SweepMode::Static:
      return tr(STR_GAMES_DLAB_MODE_STATIC);
    case SweepMode::Slow:
      return tr(STR_GAMES_DLAB_MODE_SLOW);
    case SweepMode::Fast:
      return tr(STR_GAMES_DLAB_MODE_FAST);
    case SweepMode::Ladder:
      return tr(STR_GAMES_DLAB_MODE_LADDER);
  }
  return "";
}

// The Hold row's line.
const char* holdText(const bool on) { return on ? tr(STR_STATE_ON) : tr(STR_STATE_OFF); }

// The mode's name in the log.
const char* modeName(const SweepMode mode) {
  switch (mode) {
    case SweepMode::Static:
      return "static";
    case SweepMode::Slow:
      return "slow";
    case SweepMode::Fast:
      return "fast";
    case SweepMode::Ladder:
      return "ladder";
  }
  return "";
}

}  // namespace

TransitionTestActivity::TransitionTestActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               DisplayLabActivity& lab)
    : UiListActivity(NAME, renderer, mappedInput), lab(lab) {}

const char* TransitionTestActivity::headerTitle() const { return tr(STR_GAMES_DLAB_TRANSITION_TEST); }

void TransitionTestActivity::onEnter() {
  UiListActivity::onEnter();
  // The render task may draw this screen while onEnter runs, and it reads the rows.
  RenderLock lock(*this);
  shownOnPanel = false;
  notice = nullptr;
  status = tr(STR_GAMES_DLAB_NOT_RUN);
  snprintf(statsText, sizeof(statsText), "%s", tr(STR_GAMES_DLAB_NOT_RUN));
  // D12: Static on an UltraChip panel, whose slot is too long for the sweep; Fast stays the SSD1677's sweep.
  sweepMode = isUltraChip(display.getController()) ? SweepMode::Static : SweepMode::Fast;
  holdOn = true;  // D13: on every visit, a lab preference never saved with the preset
  if (!allocate()) {
    release();
    notice = tr(STR_GAMES_OUT_OF_MEMORY);
  }
  buildRows();
}

bool TransitionTestActivity::allocate() {
  const MaskPlanes& drive = lab.masks();
  ScanBackend* backend = lab.backend();
  // The lab opens this screen only with a session; without one there is nothing to drive.
  if (!drive.valid() || !backend) {
    LOG_ERR("DLAB", "transition test: no lab session");
    return false;
  }
  const uint16_t width = drive.width();
  const uint16_t height = drive.height();
  const int logicalW = renderer.getScreenWidth();
  const int logicalH = renderer.getScreenHeight();
  if (static_cast<size_t>(logicalW) * static_cast<size_t>(logicalH) != GrayModel::pixelCount(width, height)) {
    LOG_ERR("DLAB", "screen %dx%d does not cover the %ux%u panel", logicalW, logicalH, static_cast<unsigned>(width),
            static_cast<unsigned>(height));
    return false;
  }
  // The working set of one visit, never per slot: the model (3 B a pixel) and the follow-up mask pair, PSRAM only
  // (allocatePsram never falls back to internal RAM).
  const size_t modelBytes = GrayModel::bytesFor(width, height);
  const size_t maskBytes = MaskPlanes::bytesFor(width, height);
  pixelBuffer = HalMemory::allocatePsram(modelBytes);
  followBlack = HalMemory::allocatePsram(maskBytes);
  followWhite = HalMemory::allocatePsram(maskBytes);
  if (!pixelBuffer || !followBlack || !followWhite) {
    LOG_ERR("DLAB", "OOM: %u byte PSRAM model and two %u byte follow-up masks", static_cast<unsigned>(modelBytes),
            static_cast<unsigned>(maskBytes));
    return false;
  }
  const GrayPreset& preset = lab.preset();
  // History (3 B a pixel) only when the preset turns it on.
  if (preset.historyMode != 0) {
    const size_t historyBytes = GrayModel::historyBytesFor(width, height);
    historyBuffer = HalMemory::allocatePsram(historyBytes);
    if (!historyBuffer) {
      LOG_ERR("DLAB", "OOM: %u byte PSRAM gray history", static_cast<unsigned>(historyBytes));
      return false;
    }
  }
  // The weak bitmap (1 bit a pixel) only when weak mode is on and the backend can drive a weak row.
  weakRow = preset.weakWhiteToLight && backend->supportsWeakRow();
  if (preset.weakWhiteToLight && !weakRow) LOG_INF("DLAB", "weak mode on, but %s has no weak row", backend->name());
  if (weakRow) {
    const size_t weakBytes = GrayModel::weakBytesFor(width, height);
    weakBuffer = HalMemory::allocatePsram(weakBytes);
    if (!weakBuffer) {
      LOG_ERR("DLAB", "OOM: %u byte PSRAM weak bitmap", static_cast<unsigned>(weakBytes));
      return false;
    }
  }
  model = GrayModel(pixelBuffer.get(), width, height, historyBuffer.get(), weakBuffer.get());
  follow = MaskPlanes(followBlack.get(), followWhite.get(), width, height);
  scene = GameDisplayLab::TransitionScene(turnOf(renderer.getOrientation()), logicalW, logicalH, width, height);
  model.reset(Shade::W);
  LOG_INF("DLAB", "transition test: %u B model, %u B follow-up masks, history %s, weak row %s",
          static_cast<unsigned>(modelBytes), static_cast<unsigned>(2 * maskBytes), historyBuffer ? "on" : "off",
          weakRow ? "on" : "off");
  return true;
}

void TransitionTestActivity::release() {
  model = GrayModel();
  follow = MaskPlanes();
  weakRow = false;
  pixelBuffer.reset();
  followBlack.reset();
  followWhite.reset();
  historyBuffer.reset();
  weakBuffer.reset();
}

void TransitionTestActivity::onExit() {
  // ActivityManager holds the render mutex here (a pop, or a Replace by Home or sleep, which also drains a covered
  // screen from the stack): take no RenderLock (12cc816), and end the task without waiting on that mutex.
  endSweepLocked();
  if (shownOnPanel.load()) {
    // The next screen's first frame cleans the panel; the lab's own exit cleans it too when it is left.
    renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
    shownOnPanel = false;
  }
  release();
  Activity::onExit();
}

void TransitionTestActivity::prepareForSleep() {
  // ActivityManager::prepareForSleep holds the render mutex, for this screen as the current one or as a covered one.
  endSweepLocked();
}

bool TransitionTestActivity::preventAutoSleep() {
  if (!sweepTask) return false;
  if (control.runRequested()) return true;
  // A hold keeps the device awake for HOLD_AWAKE_MS from its start, then the normal idle timeout applies (a forgotten
  // hold must not keep the CPU up for good; auto-sleep then ends the run as any leave does).
  return heldForPhoto.load() && millis() - heldSinceMs.load() < HOLD_AWAKE_MS;
}

bool TransitionTestActivity::handleHomeGesture() {
  // Called from ActivityManager::loop without the render mutex, before goHome's Replace runs onExit.
  stopSweep();
  return false;
}

void TransitionTestActivity::buildRows() {
  if (notice) {
    fui::ListItem item;
    item.label = notice;
    item.actionValue = 0;
    rowItems[0] = item;
    return;
  }
  const auto set = [this](const Row row, const char* label, const char* subtitle) {
    fui::ListItem item;
    item.label = label;
    item.subtitle = subtitle;
    item.actionValue = static_cast<int16_t>(row);
    rowItems[static_cast<size_t>(row)] = item;
  };
  set(Row::Run, tr(STR_GAMES_DLAB_RUN), status);
  set(Row::Mode, tr(STR_GAMES_DLAB_MODE), modeText(sweepMode));
  set(Row::Hold, tr(STR_GAMES_DLAB_HOLD_PHOTOS), holdText(holdOn));
  set(Row::Clean, tr(STR_GAMES_DLAB_CLEAN), tr(STR_GAMES_DLAB_CLEAN_DESC));
  set(Row::Stats, tr(STR_GAMES_DLAB_SPEED), statsText);
  set(Row::Controls, tr(STR_GAMES_DLAB_CONTROLS), tr(STR_GAMES_DLAB_TRANSITION_CONTROLS));
}

void TransitionTestActivity::render(RenderLock&& lock) {
  // While the test view is up the panel shows only what the scans drive (D4): a routed tap or a swipe requests a
  // repaint on its own, and none may run. Leaving the view clears the flag and asks again, with a FULL refresh.
  if (shownOnPanel.load()) return;
  UiListActivity::render(std::move(lock));
}

void TransitionTestActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the header band GUI.drawHeader paints.
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  fui::ListProps props;
  props.items = rowItems;
  props.count = static_cast<uint16_t>(listCount());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.maxLines = 3;
  syncListViewport(screen, props);
  screen.list(props);
}

void TransitionTestActivity::activateIndex(const int index) {
  if (shownOnPanel.load()) {
    // A tap over the rows' area (the only taps the list routes, where touch routes while the view is up) pauses or
    // resumes.
    app.clearTapFlash();
    togglePause();
    return;
  }
  if (notice) return;
  switch (static_cast<Row>(index)) {
    case Row::Run:
      startRun();  // a replay: the task cleans first
      return;
    case Row::Mode:
      stepMode();
      return;
    case Row::Hold:
      toggleHold();
      return;
    case Row::Clean:
      cleanFromRows();
      return;
    case Row::Stats:
    case Row::Controls:
      return;  // text only
  }
}

bool TransitionTestActivity::handleCustomInput() {
  if (!shownOnPanel.load()) return false;
  // The X4 Pro has no front buttons and its touch routing is dead while the view is up (D12), so the side buttons
  // carry the controls. Back first: the left-edge swipe is Back (wasBackGesture), and the swipe guard below would
  // swallow it. NavPrevious stops on its release, as Back does, so neither half of the press reaches the rows.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::NavPrevious)) {
    stopSweep();
    leaveView(endText());
    return true;
  }
  // Any other swipe would scroll the hidden list: consumed while the test view is up.
  if (mappedInput.wasSwipe() != MappedInputManager::SwipeDir::None) return true;
  if (mappedInput.wasPressed(MappedInputManager::Button::NavNext) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    togglePause();
    return true;
  }
  // A tap pauses only where touch routes while the view is up (it reaches activateIndex through the base's touch
  // routing); the X4 Pro's does not (D12), so its side buttons are the controls there.
  return false;
}

void TransitionTestActivity::navigateButtons() {
  if (shownOnPanel.load()) return;
  UiListActivity::navigateButtons();
}

void TransitionTestActivity::onBackButton() {
  app.clearTapFlash();
  finish();  // pops to the lab menu; leaving the lab cleans the panel
}

// ---- loop task ----------------------------------------------------------------------------------------------------

void TransitionTestActivity::loop() {
  if (shownOnPanel.load()) {
    // Another screen pushed over this one (the light panel's top-edge swipe) gets no onExit here, stops this loop, and
    // draws over the drive; a gap that long means it happened (the task has refused its scans since), and the view
    // ends with the rows' FULL refresh. The view also ends when the task ended on its own (a failed scan, the cover),
    // or was ended by prepareForSleep.
    const bool covered = control.heartbeatAgeUs(micros()) > COVERED_GAP_US;
    if (covered) LOG_INF("DLAB", "gray sweep: covered by another screen");
    if (covered || !sweepTask || sweepDone.load(std::memory_order_acquire)) {
      stopSweep();
      // A cover the loop saw first (the task paused, or ended by the stop above) reads as one too.
      if (covered && sweepEnd == SweepEnd::Stopped) sweepEnd = SweepEnd::Covered;
      leaveView(endText());
      // This pass's input belonged to the test view: a Back or Confirm release here must not pop the screen or
      // start a run. The next pass serves the rows.
      control.heartbeat(micros());
      return;
    }
  }
  UiListActivity::loop();
  // The sweep task refuses its next scan once this is older than COVERED_GAP_US.
  control.heartbeat(micros());
}

void TransitionTestActivity::startRun() {
  if (sweepTask) return;
  app.clearTapFlash();
  // The task's fields, written before it is created (its creation orders them before its first read).
  sweepDone.store(false);
  heldForPhoto.store(false);
  sweepEnd = SweepEnd::Stopped;
  failedScan = DisplayLabActivity::Scan::Done;
  sweeping = false;
  sceneStale = false;
  elapsedUs = 0;
  sceneMs = 0;
  leastScanHeadroom = UINT32_MAX;
  // The Mode row's mode. Slow's dwell is the preset's longest dose run in slots; its crossing waits for the run's own
  // measurement (TransitionScene::slotDone).
  scene.setMode(sweepMode, lab.preset().longestRunSlots());
  scene.setHold(holdOn);
  // A run that ends before a report over delivered scans keeps the Speed line as it was.
  statsReported = false;
  reportedTenths = 0;
  reportedMsPerScan = 0;
  scheduler.stop();
  control.reset(micros());
  {
    RenderLock lock(*this);
    shownOnPanel = true;  // from here the render task draws nothing; no task is alive yet
  }
  const BaseType_t created = xTaskCreatePinnedToCore(&TransitionTestActivity::sweepEntry, "DlabSweep",
                                                     SWEEP_STACK_BYTES, this, SWEEP_PRIORITY, &sweepTask, SWEEP_CORE);
  if (created == pdPASS && sweepTask) return;
  sweepTask = nullptr;
  LOG_ERR("DLAB", "Cannot create the sweep task (%u byte stack)", static_cast<unsigned>(SWEEP_STACK_BYTES));
  {
    RenderLock lock(*this);
    shownOnPanel = false;  // nothing was sent: the rows stay as they are, with the reason
    status = tr(STR_GAMES_DLAB_NO_TASK);
    buildRows();
  }
  requestUpdate();
}

void TransitionTestActivity::togglePause() {
  if (!sweepTask) return;
  control.requestRun(!control.runRequested());
  notifySweep();
}

void TransitionTestActivity::stepMode() {
  app.clearTapFlash();
  {
    RenderLock lock(*this);
    sweepMode = GameDisplayLab::nextMode(sweepMode);
    rowItems[static_cast<size_t>(Row::Mode)].subtitle = modeText(sweepMode);
  }
  LOG_INF("DLAB", "transition test mode: %s", modeName(sweepMode));
  requestUpdate();
}

void TransitionTestActivity::toggleHold() {
  app.clearTapFlash();
  {
    RenderLock lock(*this);
    holdOn = !holdOn;
    rowItems[static_cast<size_t>(Row::Hold)].subtitle = holdText(holdOn);
  }
  LOG_INF("DLAB", "transition test hold: %s", holdOn ? "on" : "off");
  requestUpdate();
}

void TransitionTestActivity::cleanFromRows() {
  // The rows view has no sweep task (startRun creates it with the view, and the view ends only after the join), so
  // the panel is this task's.
  if (sweepTask) return;
  app.clearTapFlash();
  {
    // The lab's clean sends the framebuffer with a FULL refresh: white first. The rows' repaint follows it.
    RenderLock lock(*this);
    renderer.clearScreen(0xFF);
    lab.cleanNow();
  }
  LOG_INF("DLAB", "transition test: panel cleaned to white");
  requestUpdate();
}

void TransitionTestActivity::notifySweep() {
  // The task is deleted only by this screen, so a live handle is a live task (it may be parked after its end).
  if (sweepTask) xTaskNotifyGive(sweepTask);
}

void TransitionTestActivity::stopSweep() {
  if (!sweepTask) return;
  control.requestStop();
  notifySweep();
  // Bounded by the scan or the clean in flight (the run's first clean or the session cap's is one FULL refresh; a scan
  // is bounded by DisplayLabActivity::SCAN_TIMEOUT_MS, the SDK's own BUSY limit beyond it), as the inline scans were.
  while (!sweepDone.load(std::memory_order_acquire)) vTaskDelay(pdMS_TO_TICKS(STOP_POLL_MS));
  vTaskDelete(sweepTask);  // parked in vTaskSuspend
  sweepTask = nullptr;
}

void TransitionTestActivity::endSweepLocked() {
  if (!sweepTask) return;
  control.requestStop();
  notifySweep();
  while (!sweepDone.load(std::memory_order_acquire)) {
    // Inside the panel the task blocks without the render mutex only while taking it (in DisplayLabActivity::scan or
    // cleanToWhite): blocked there, it waits for the mutex this caller holds and holds nothing, so it is deleted
    // where it waits (GameVM::abandon deletes a task it has seen safe the same way). The scan lambda checks
    // DisplayLabActivity::scan's refusals (and logs them) before entering the panel, so no log call, which can block
    // on the USB serial's lock, runs inside it; the one window left is the display being inverted between that
    // check and scan()'s own, whose LOG_ERR then runs inside the panel. The mutex wait cannot end while this caller
    // holds the mutex, the task runs on SWEEP_CORE, the loop task's core, and the sequence from the check to the
    // delete makes no blocking call, so the task cannot run between them; the logging comes after the delete.
    if (control.insidePanel() && eTaskGetState(sweepTask) == eBlocked) {
      const auto freeBytes = static_cast<uint32_t>(uxTaskGetStackHighWaterMark(sweepTask));
      vTaskSuspend(sweepTask);
      vTaskDelete(sweepTask);
      sweepTask = nullptr;
      logStackEnd(freeBytes);
      LOG_INF("DLAB", "gray sweep: task deleted while it waited for the render lock");
      if (sweeping) logStats(micros());
      sweeping = false;
      sweepEnd = SweepEnd::Stopped;
      return;
    }
    vTaskDelay(1);
  }
  vTaskDelete(sweepTask);  // parked in vTaskSuspend
  sweepTask = nullptr;
}

const char* TransitionTestActivity::endText() const {
  switch (sweepEnd) {
    case SweepEnd::Stopped:
      return tr(STR_GAMES_DLAB_STOPPED);
    case SweepEnd::Covered:
      return tr(STR_GAMES_DLAB_COVERED);
    case SweepEnd::ScanFailed:
      return DisplayLabActivity::outcomeText(failedScan);
    case SweepEnd::Finished:
      return tr(STR_GAMES_DLAB_FINISHED);
  }
  return tr(STR_GAMES_DLAB_STOPPED);
}

void TransitionTestActivity::leaveView(const char* outcome) {
  app.clearTapFlash();
  {
    RenderLock lock(*this);
    if (statsReported) {
      snprintf(statsText, sizeof(statsText), tr(STR_GAMES_DLAB_STATS), static_cast<unsigned>(reportedTenths / 10),
               static_cast<unsigned>(reportedTenths % 10), static_cast<unsigned>(reportedMsPerScan));
    }
    status = outcome;
    rowItems[static_cast<size_t>(Row::Run)].subtitle = status;
    rowItems[static_cast<size_t>(Row::Stats)].subtitle = statsText;
    // The rows come back over the drive with a FULL refresh, which cleans the panel; promoted before the flag
    // clears, so no repaint can slip in without it.
    renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
    shownOnPanel = false;
  }
  LOG_INF("DLAB", "gray sweep stopped: %s", outcome);
  requestUpdate();
}

// ---- sweep task ---------------------------------------------------------------------------------------------------

void TransitionTestActivity::sweepEntry(void* param) {
  static_cast<TransitionTestActivity*>(param)->sweepRun();
  // sweepRun has published sweepDone, after which the screen may delete this task at any time: touch nothing of it,
  // and wait here to be deleted (a task that deleted itself could be notified after it was freed).
  for (;;) vTaskSuspend(nullptr);
}

void TransitionTestActivity::sweepRun() {
  stackFloor = reinterpret_cast<uintptr_t>(pxTaskGetStackStart(nullptr));
  while (true) {
    const GameDisplayLab::SweepControl::Requests requests = control.take();
    if (requests.stop) {
      sweepEnd = SweepEnd::Stopped;
      break;
    }
    if (!serve(requests)) break;
    if (!sweeping) {
      // Paused: a control's notification wakes it.
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      continue;
    }
    const uint32_t nowUs = micros();
    if (!scheduler.due(nowUs)) {
      // Until the next slot, or a control's notification; at least one tick.
      const TickType_t ticks = pdMS_TO_TICKS((scheduler.nextUs() - nowUs) / 1000);
      ulTaskNotifyTake(pdTRUE, ticks > 0 ? ticks : 1);
      continue;
    }
    if (!runSlot(nowUs)) break;
  }
  if (sweeping) reportStats(micros());
  sweeping = false;
  heldForPhoto.store(false);
  scheduler.stop();
  logStackEnd(static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr)));
  sweepDone.store(true, std::memory_order_release);
}

bool TransitionTestActivity::serve(const GameDisplayLab::SweepControl::Requests& requests) {
  const bool cleaning = requests.replay || requests.clean;
  if (sweeping && (!requests.run || cleaning)) {
    // A pause, or a clean, which is a pause of the clock: the speed up to here, the clock stopped.
    const uint32_t nowUs = micros();
    advanceClock(nowUs);
    reportStats(nowUs);
    sweeping = false;
    scheduler.stop();
    if (!requests.run) {
      LOG_INF("DLAB", "gray sweep paused at %lu ms", static_cast<unsigned long>(elapsedUs / 1000));
      // A pause mid-stage puts a gap in the Ladder's one-pulse-a-slot rhythm: its settle line counts them.
      if (scene.ladderPulsing()) ++ladderPauses;
    }
  }
  const GrayPreset& preset = lab.preset();
  if (requests.replay) {
    if (!cleanToWhite()) return false;
    elapsedUs = 0;
    sceneMs = 0;
    scene.restartFlips();
    scene.restartLadder();
    ladderPauses = 0;
    ladderSlotUs = 0;
    scene.applyAll(model, 0, preset);
    sceneStale = false;
    // Slow's crossing is sized once the paint has settled (`gray slow:` says it); Static's rectangles do not cross,
    // and the Ladder has none (its layout line follows).
    const bool ladder = scene.mode() == SweepMode::Ladder;
    char crossing[24];
    char rects[16];
    if (scene.slowPending()) {
      snprintf(crossing, sizeof(crossing), "pending measurement");
    } else if (ladder) {
      snprintf(crossing, sizeof(crossing), "none");
    } else {
      snprintf(crossing, sizeof(crossing), "%lu ms", static_cast<unsigned long>(scene.sweepMs()));
    }
    if (ladder) {
      snprintf(rects, sizeof(rects), "no rectangles");
    } else {
      snprintf(rects, sizeof(rects), "%d px", scene.rectW());
    }
    // The Ladder holds at both its photos whatever the Hold row says (TransitionScene::holdsAtSpots).
    const char* hold = scene.holdOn() ? "on" : "off";
    if (ladder) hold = scene.holdOn() ? "on (the ladder always holds)" : "off, but the ladder always holds";
    LOG_INF("DLAB",
            "gray sweep: %u pixels to drive, mode %s, %s, crossing %s, hold %s, rate %u, swap %u, retarget dark "
            "%u light %u, history %u, weak row %u",
            static_cast<unsigned>(model.activeCount()), modeName(scene.mode()), rects, crossing, hold,
            static_cast<unsigned>(preset.rateIndex), static_cast<unsigned>(preset.swapPolarity),
            static_cast<unsigned>(preset.retargetDarkPercent), static_cast<unsigned>(preset.retargetLightPercent),
            static_cast<unsigned>(historyBuffer ? preset.historyMode : 0), static_cast<unsigned>(weakRow));
    // With history on, a flip back to a pixel's origin is a completed return or a reversal, budgeted by the route's
    // return_delta or reverse_percent rather than the table dose.
    if (scene.mode() == SweepMode::Static && historyBuffer) {
      LOG_INF("DLAB", "gray static: history %u is on, so the flips' residues include its route adjustments",
              static_cast<unsigned>(preset.historyMode));
    }
    if (scene.mode() == SweepMode::Ladder) logLadderLayout();
  } else if (requests.clean) {
    if (!cleanToWhite()) return false;  // the next slot reapplies the scene at the paused clock
    LOG_INF("DLAB", "gray sweep cleaned at %lu ms", static_cast<unsigned long>(elapsedUs / 1000));
  }
  if (requests.run && !sweeping) {
    const uint32_t nowUs = micros();
    clockUs = nowUs;
    statsSinceUs = nowUs;
    scheduler.start(nowUs);
    sweeping = true;
    if (!requests.replay) LOG_INF("DLAB", "gray sweep resumed at %lu ms", static_cast<unsigned long>(elapsedUs / 1000));
    // A run held at a photo spot: the marker goes back to its band's shade before anything else moves (the scene does
    // nothing unless it holds).
    if (scene.resumeHold(model, preset)) {
      heldForPhoto.store(false);
      LOG_INF("DLAB", "gray hold: resumed; marker back to %c", GameDisplayLab::shadeLetter(scene.markerShade()));
    }
  }
  return true;
}

bool TransitionTestActivity::admitted(const GameDisplayLab::SweepControl::Gate gate) {
  switch (gate) {
    case GameDisplayLab::SweepControl::Gate::Enter:
      return true;
    case GameDisplayLab::SweepControl::Gate::Stop:
      sweepEnd = SweepEnd::Stopped;
      return false;
    case GameDisplayLab::SweepControl::Gate::Covered:
      LOG_INF("DLAB", "gray sweep: loop silent for over %lu ms, a screen covers the view; ending",
              static_cast<unsigned long>(COVERED_GAP_US / 1000));
      sweepEnd = SweepEnd::Covered;
      return false;
  }
  return false;
}

bool TransitionTestActivity::cleanToWhite() {
  if (!admitted(control.enterPanel(micros(), COVERED_GAP_US))) return false;
  {
    // The lab's clean sends the framebuffer with a FULL refresh: white first, so the panel and the model agree.
    RenderLock lock(*this);
    renderer.clearScreen(0xFF);
    lab.cleanNow();
  }
  control.leavePanel();
  model.reset(Shade::W);
  sceneStale = true;
  return true;
}

void TransitionTestActivity::advanceClock(const uint32_t nowUs) {
  if (sweeping && !scene.clockStill()) elapsedUs += nowUs - clockUs;
  clockUs = nowUs;
}

bool TransitionTestActivity::runSlot(const uint32_t nowUs) {
  advanceClock(nowUs);
  uint32_t slotStartUs = nowUs;
  // A slot is two scans; the lab cleans on its own at the session cap, which must not fall between them.
  if (lab.scansBeforeClean() < SCANS_PER_SLOT) {
    LOG_INF("DLAB", "gray sweep: session cap near, cleaning first");
    reportStats(nowUs);  // the speed up to the clean
    if (!cleanToWhite()) {
      sweeping = false;  // reported already: sweepRun must not report the empty window after it
      return false;
    }
    // A hold in progress is dropped: repainted from white, the marker would settle before the pattern it marks.
    const std::optional<GameDisplayLab::DroppedHold> dropped = scene.cancelHold();
    if (dropped) {
      LOG_INF("DLAB", "gray hold: %s dropped by the session-cap clean after %lu marker slots: %s",
              GameDisplayLab::holdSpotName(dropped->spot), static_cast<unsigned long>(dropped->markerSlots),
              GameDisplayLab::holdDropName(dropped->drop));
    }
    // Static repaints from white: the sequence starts again at flip 0, so no settle line counts the repaint.
    if (scene.mode() == SweepMode::Static) {
      scene.restartFlips();
      LOG_INF("DLAB", "gray static: sequence restarted at flip 0 after the session-cap clean");
    }
    // The Ladder's columns are measured from a settled base: the run starts again from the base, which the reapplied
    // scene paints, unless the clean cut off the marker's way back from `ladder 2`: both photos are taken, and the run
    // ends as a finished one does.
    if (scene.mode() == SweepMode::Ladder) {
      ladderPauses = 0;
      ladderSlotUs = 0;
      if (scene.ladderCleaned(dropped)) {
        LOG_INF("DLAB", "gray ladder: done (the session-cap clean came after both photos)");
        sweepEnd = SweepEnd::Finished;
        sweeping = false;  // reported already: sweepRun must not report the empty window after it
        return false;
      }
      LOG_INF("DLAB", "gray ladder: restarted from the base after the session-cap clean");
    }
    // The clean's FULL refresh is a pause of the sweep: the clock, the slot and the speed window restart after it.
    slotStartUs = micros();
    clockUs = slotStartUs;
    scheduler.resetWindow(slotStartUs);
    statsSinceUs = slotStartUs;
  }
  const uint32_t ms = static_cast<uint32_t>(elapsedUs / 1000);
  const GrayPreset& preset = lab.preset();
  if (sceneStale) {
    scene.applyAll(model, ms, preset);
    sceneStale = false;
  } else {
    scene.update(model, sceneMs, ms, preset);
  }
  sceneMs = ms;

  DisplayLabActivity::Scan failure = DisplayLabActivity::Scan::Done;
  bool refused = false;
  // The delivered (Done) scans and their busy time, for the speed window.
  uint32_t doneScans = 0;
  uint32_t doneBusyUs = 0;
  const auto scan = [this, &preset, &failure, &refused, &doneScans, &doneBusyUs](const MaskPlanes& masks,
                                                                                 uint32_t& busyUs) {
    // A stop asked for ends the run as Stopped, whatever the refusal checks below would say.
    if (control.stopRequested()) {
      sweepEnd = SweepEnd::Stopped;
      refused = true;
      return false;
    }
    // DisplayLabActivity::scan's refusals, checked (and logged) here, outside the panel: its own LOG_ERR runs before
    // its render lock, and a task deleted inside a log call would keep the USB serial's lock (endSweepLocked).
    ScanBackend* backend = lab.backend();
    bool refusedHere = true;
    if (!backend || !lab.masks().valid()) {
      LOG_ERR("DLAB", "gray sweep: scan refused: no lab session (backend or masks)");
    } else if (display.isInverted()) {
      LOG_ERR("DLAB", "gray sweep: scan refused: display inverted");
    } else if (preset.rateIndex >= backend->rateCount()) {
      LOG_ERR("DLAB", "gray sweep: scan refused: rate index %u of %u", static_cast<unsigned>(preset.rateIndex),
              static_cast<unsigned>(backend->rateCount()));
    } else {
      refusedHere = false;
    }
    if (refusedHere) {
      failure = DisplayLabActivity::Scan::Refused;
      return false;
    }
    // Stop or a cover cuts the slot here: the follow-up is skipped, and runGraySlot commits what was delivered.
    if (!admitted(control.enterPanel(micros(), COVERED_GAP_US))) {
      refused = true;
      return false;
    }
    // The room left to the scan's call chain (the backend and the SDK), for the run's stack log line.
    const uintptr_t here = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
    const uint32_t headroom = here > stackFloor ? static_cast<uint32_t>(here - stackFloor) : 0;
    if (headroom < leastScanHeadroom) leastScanHeadroom = headroom;
    // One dose: a one-frame activation of each driven pixel.
    const ScanRequest request{.blackMask = masks.black(),
                              .whiteMask = masks.white(),
                              .frames = 1,
                              .rate = preset.rateIndex,
                              .swapPolarity = preset.swapPolarity};
    const DisplayLabActivity::Scan result = lab.scan(request, busyUs);
    control.leavePanel();
    if (result != DisplayLabActivity::Scan::Done) {
      failure = result;
      return false;
    }
    ++doneScans;
    doneBusyUs += busyUs;
    return true;
  };
  // A Ladder stage's pulses go one a slot, each one scan (the held rhythm leaves no follow-up); every other slot, the
  // base and the marker's included, runs on the preset as it is.
  const GrayPreset& slotPreset = scene.slotPreset(preset);
  const GameDisplayLab::SlotResult slot =
      GameDisplayLab::runGraySlot(model, lab.masks(), follow, slotPreset, weakRow, scan);
  const uint32_t endUs = micros();
  scheduler.slotDone(slotStartUs, endUs, slot.busyUs);
  // Only delivered scans and their busy time count: a refused scan was never sent, and a TimedOut or Dropped scan's
  // time is no measurement.
  scheduler.record(doneScans, doneBusyUs);
  if (refused) return false;
  if (failure != DisplayLabActivity::Scan::Done) {
    // The lab logged why (and cleaned the panel after a timeout); the run ends here and the rows say how.
    failedScan = failure;
    sweepEnd = SweepEnd::ScanFailed;
    return false;
  }
  // The slot's real length, from before the scene update to after the commit: the scans plus the CPU work over every
  // pixel, which the scheduler's period (busy time + MARGIN_US) leaves out.
  const uint32_t periodUs = scheduler.periodUs() > endUs - slotStartUs ? scheduler.periodUs() : endUs - slotStartUs;
  if (!afterSlot(slot.driven, slot.driveDone, periodUs, ms)) return false;
  if (endUs - statsSinceUs >= STATS_EVERY_US) reportStats(endUs);
  return true;
}

bool TransitionTestActivity::afterSlot(const size_t driven, const bool driveDone, const uint32_t periodUs,
                                       const uint32_t elapsedMs) {
  // A Ladder stage's longest pulse slot, read before slotDone moves the stage on.
  if (scene.ladderPulsing() && driven > 0 && periodUs > ladderSlotUs) ladderSlotUs = periodUs;
  const GameDisplayLab::SceneStep step = scene.slotDone(model, lab.preset(), driven, driveDone, periodUs, elapsedMs);
  if (step.settled) {
    LOG_INF("DLAB", "gray static: flip %lu settled after %lu slots (offset +%u, next +%u)",
            static_cast<unsigned long>(step.settled->flip), static_cast<unsigned long>(step.settled->slots),
            static_cast<unsigned>(step.settled->offset),
            static_cast<unsigned>(GameDisplayLab::TransitionScene::flipOffset(step.settled->flip + 1)));
  }
  if (step.ladderSettled) {
    logLadderSettled(*step.ladderSettled);
    ladderPauses = 0;
    ladderSlotUs = 0;
  }
  if (step.measured) {
    // The paint's longest slot, which the crossing is sized from.
    const uint32_t slowUs = scene.slowPeriodUs();
    LOG_INF("DLAB", "gray slow: slot period %lu ms, crossing %lu ms, dwell %lu slots%s",
            static_cast<unsigned long>(slowUs / 1000), static_cast<unsigned long>(scene.sweepMs()),
            static_cast<unsigned long>(scene.dwellSlotsAt(slowUs)),
            scene.sweepCapped() ? " (capped at 120 s, under the longest dose run)" : "");
  }
  if (step.skipped) {
    LOG_INF("DLAB", "gray hold: %s %lu skipped (%s)", GameDisplayLab::holdSpotName(step.skipped->spot),
            static_cast<unsigned long>(step.skipped->count), GameDisplayLab::spotSkipName(step.skipped->reason));
  }
  if (step.hold) {
    // A photo spot: the scene put the marker out and stopped the sweep clock; the next slots drive the marker.
    LOG_INF("DLAB", "gray hold: %s %lu, marker %c over %c", GameDisplayLab::holdSpotName(step.hold->spot),
            static_cast<unsigned long>(step.hold->count), GameDisplayLab::shadeLetter(scene.markerShade()),
            GameDisplayLab::shadeLetter(scene.markerBandShade()));
  }
  if (step.held) {
    // Held for a photo: nothing is driven until Next (or Confirm) resumes, which drives the marker back first. The
    // task clears Run itself so one press resumes. heldForPhoto (and its start) first: preventAutoSleep reads Run, then
    // the hold, so with Run cleared first it could see neither and let auto-sleep's FULL refresh end the hold.
    const uint32_t nowUs = micros();
    advanceClock(nowUs);
    reportStats(nowUs);
    sweeping = false;
    scheduler.stop();
    heldSinceMs.store(millis());
    heldForPhoto.store(true);
    control.holdRun();
    LOG_INF("DLAB", "gray hold: marker settled after %lu slots; holding, Next resumes",
            static_cast<unsigned long>(step.markerSlots));
  }
  if (step.resumed) {
    LOG_INF("DLAB", "gray hold: marker back after %lu slots; %s", static_cast<unsigned long>(step.markerSlots),
            step.finished ? "the ladder is done" : "the run goes on");
  }
  // A Static flip retargets every pixel at the next slot.
  if (step.reapply) sceneStale = true;
  if (!step.finished) return true;
  // The Ladder's return settled (and was held and resumed with Hold on): the run ends as Finished, and the rows come
  // back with their FULL refresh.
  LOG_INF("DLAB", "gray ladder: done");
  sweepEnd = SweepEnd::Finished;
  return false;
}

void TransitionTestActivity::logLadderLayout() const {
  const GameDisplayLab::LadderLayout layout = scene.ladderLayout();
  const int rows = scene.ladderHeight();
  // Each half's rows, or "none" for a half without any (a screen under the marker's height).
  const auto range = [](char* out, const size_t size, const int first, const int count) {
    if (count <= 0) {
      snprintf(out, size, "none");
    } else {
      snprintf(out, size, "%d-%d", first, first + count - 1);
    }
  };
  char top[24];
  char bottom[24];
  range(top, sizeof(top), 0, rows / 2);
  range(bottom, sizeof(bottom), rows / 2, rows - rows / 2);
  LOG_INF("DLAB",
          "gray ladder: %d columns of %d px, gutter %d px; rows %s from white, %s from black, the marker's rows below "
          "black; painting the base",
          layout.columns, layout.width, layout.gutter, top, bottom);
  // A logical width under 48: 1 px columns, no gutter, as many as fit.
  if (layout.columns < GameDisplayLab::TransitionScene::LADDER_STEPS || layout.gutter == 0) {
    LOG_INF("DLAB", "gray ladder: narrow screen: %d of %d columns, %d px each, no gutter", layout.columns,
            GameDisplayLab::TransitionScene::LADDER_STEPS, layout.width);
  }
}

void TransitionTestActivity::logLadderSettled(const GameDisplayLab::LadderSettled& settled) const {
  const GrayPreset& preset = lab.preset();
  switch (settled.stage) {
    case GameDisplayLab::LadderStage::Base:
      LOG_INF("DLAB", "gray ladder: base painted after %lu slots; stage 1 (column n: n pulses from its origin)",
              static_cast<unsigned long>(settled.slots));
      return;
    case GameDisplayLab::LadderStage::Ladder:
    case GameDisplayLab::LadderStage::Return: {
      // What the photo needs beside it: the rate, the rhythm (and the pauses that broke it), the slot length, the
      // base's black and the columns.
      ScanBackend* backend = lab.backend();
      const char* rate = backend ? backend->rateLabel(preset.rateIndex) : nullptr;
      const GameDisplayLab::LadderLayout layout = scene.ladderLayout();
      const unsigned baseDose =
          preset.dose[static_cast<size_t>(Shade::W)][static_cast<size_t>(Shade::K)];  // W>K, which painted the base
      char paused[32] = "";
      if (ladderPauses > 0) {
        snprintf(paused, sizeof(paused), ", paused %lu times", static_cast<unsigned long>(ladderPauses));
      }
      LOG_INF("DLAB",
              "gray ladder: stage %u settled after %lu slots: rate %u (%s), swap %u, one pulse per slot%s, slots up to "
              "%lu ms, base W>K dose %u, column n %s n pulses (end-aligned), %d px columns, gutter %d px",
              settled.stage == GameDisplayLab::LadderStage::Ladder ? 1u : 2u, static_cast<unsigned long>(settled.slots),
              static_cast<unsigned>(preset.rateIndex), rate ? rate : "?", static_cast<unsigned>(preset.swapPolarity),
              paused, static_cast<unsigned long>(ladderSlotUs / 1000), baseDose,
              settled.stage == GameDisplayLab::LadderStage::Ladder ? "has had" : "has been driven back", layout.width,
              layout.gutter);
      return;
    }
    case GameDisplayLab::LadderStage::Done:
      return;  // never settles: the run has ended
  }
}

void TransitionTestActivity::logStackEnd(const uint32_t freeBytes) const {
  LOG_INF("DLAB", "gray sweep task ended: stack %u bytes, high-water %u bytes free, least before a scan %u bytes",
          static_cast<unsigned>(SWEEP_STACK_BYTES), static_cast<unsigned>(freeBytes),
          static_cast<unsigned>(leastScanHeadroom == UINT32_MAX ? 0 : leastScanHeadroom));
  if (freeBytes < SWEEP_STACK_MARGIN_BYTES) {
    LOG_ERR("DLAB", "gray sweep task stack: %u bytes free, under the %u byte margin", static_cast<unsigned>(freeBytes),
            static_cast<unsigned>(SWEEP_STACK_MARGIN_BYTES));
  }
}

void TransitionTestActivity::logStats(const uint32_t nowUs) const {
  LOG_INF("DLAB", "gray scans_s=%u.%u ms_per_scan=%u skipped=%lu",
          static_cast<unsigned>(scheduler.scansPerSecondTenths(nowUs) / 10),
          static_cast<unsigned>(scheduler.scansPerSecondTenths(nowUs) % 10),
          static_cast<unsigned>(scheduler.msPerScan()), static_cast<unsigned long>(scheduler.skipped()));
}

void TransitionTestActivity::reportStats(const uint32_t nowUs) {
  logStats(nowUs);
  // The loop task writes the Speed line from these once the run is over (leaveView), under the render lock. A window
  // without a delivered scan (a report just after the last one, or before a refused clean) keeps the last real speed.
  if (scheduler.windowScans() > 0) {
    reportedTenths = scheduler.scansPerSecondTenths(nowUs);
    reportedMsPerScan = scheduler.msPerScan();
    statsReported = true;
  }
  scheduler.resetWindow(nowUs);
  statsSinceUs = nowUs;
}

#endif  // CROSSHATCH_DISPLAY_LAB
