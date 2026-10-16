#include "TransitionTestActivity.h"

#if CROSSHATCH_DISPLAY_LAB

#include <Arduino.h>
#include <GfxRenderer.h>
#include <GraySlot.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <utility>

#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

using GameDisplayLab::GrayModel;
using GameDisplayLab::GrayPreset;
using GameDisplayLab::MaskPlanes;
using GameDisplayLab::PanelTurn;
using GameDisplayLab::Shade;

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
  running = false;
  notice = nullptr;
  status = tr(STR_GAMES_DLAB_NOT_RUN);
  snprintf(statsText, sizeof(statsText), "%s", tr(STR_GAMES_DLAB_NOT_RUN));
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
  // The working set of one visit, never per slot: the model (2 B a pixel) and the follow-up mask pair, PSRAM only
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
  // ActivityManager holds the render mutex here (a pop, or a Replace by Home or sleep): take no RenderLock (12cc816),
  // so the speed is logged only, not written to the row.
  if (running) logStats(micros());
  running = false;
  scheduler.stop();
  if (shownOnPanel.load()) {
    // The next screen's first frame cleans the panel; the lab's own exit cleans it too when it is left.
    renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
    shownOnPanel = false;
  }
  release();
  Activity::onExit();
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
    // A tap over the rows' area (the only taps the list routes) pauses or resumes.
    app.clearTapFlash();
    togglePause();
    return;
  }
  if (notice || index != static_cast<int>(Row::Run)) return;
  startRun();
}

bool TransitionTestActivity::handleCustomInput() {
  if (!shownOnPanel.load()) return false;
  // A swipe would scroll the hidden list: consumed while the test view is up.
  if (mappedInput.wasSwipe() != MappedInputManager::SwipeDir::None) return true;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    leaveView(tr(STR_GAMES_DLAB_STOPPED));
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    togglePause();
    return true;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::NavNext)) {
    startRun();
    return true;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::NavPrevious)) {
    cleanAndPause();
    return true;
  }
  return false;  // a tap over the rows' area reaches activateIndex through the base's touch routing
}

void TransitionTestActivity::navigateButtons() {
  if (shownOnPanel.load()) return;
  UiListActivity::navigateButtons();
}

void TransitionTestActivity::onBackButton() {
  app.clearTapFlash();
  finish();  // pops to the lab menu; leaving the lab cleans the panel
}

void TransitionTestActivity::loop() {
  // Another screen pushed over this one (the light panel's top-edge swipe) gets no onExit here, stops this loop, and
  // draws over the drive; a gap that long means it happened, and the view ends with the rows' FULL refresh.
  if (shownOnPanel.load() && micros() - lastLoopUs > COVERED_GAP_US) {
    LOG_INF("DLAB", "gray sweep: covered by another screen");
    leaveView(tr(STR_GAMES_DLAB_STOPPED));
  }
  UiListActivity::loop();
  if (running) {
    const uint32_t nowUs = micros();
    if (scheduler.due(nowUs)) runSlot(nowUs);
  }
  lastLoopUs = micros();
}

void TransitionTestActivity::cleanToWhite() {
  // The lab's clean sends the framebuffer with a FULL refresh: white first, so the panel and the model agree.
  renderer.clearScreen(0xFF);
  lab.cleanNow();
  model.reset(Shade::W);
  sceneStale = true;
}

void TransitionTestActivity::startRun() {
  app.clearTapFlash();
  {
    RenderLock lock(*this);
    shownOnPanel = true;  // from here the render task draws nothing
    cleanToWhite();
  }
  const GrayPreset& preset = lab.preset();
  elapsedUs = 0;
  sceneMs = 0;
  scene.applyAll(model, 0, preset);
  sceneStale = false;
  const uint32_t nowUs = micros();
  clockUs = nowUs;
  statsSinceUs = nowUs;
  scheduler.start(nowUs);
  running = true;
  LOG_INF("DLAB", "gray sweep: %u pixels to drive, rate %u, swap %u, retarget %u, history %u, weak row %u",
          static_cast<unsigned>(model.activeCount()), static_cast<unsigned>(preset.rateIndex),
          static_cast<unsigned>(preset.swapPolarity), static_cast<unsigned>(preset.retargetPercent),
          static_cast<unsigned>(historyBuffer ? preset.historyMode : 0), static_cast<unsigned>(weakRow));
}

void TransitionTestActivity::advanceClock(const uint32_t nowUs) {
  if (running) elapsedUs += nowUs - clockUs;
  clockUs = nowUs;
}

void TransitionTestActivity::togglePause() {
  const uint32_t nowUs = micros();
  if (running) {
    advanceClock(nowUs);
    reportStats(nowUs);
    running = false;
    scheduler.stop();
    LOG_INF("DLAB", "gray sweep paused at %lu ms", static_cast<unsigned long>(elapsedUs / 1000));
    return;
  }
  clockUs = nowUs;
  statsSinceUs = nowUs;
  scheduler.start(nowUs);
  running = true;
  LOG_INF("DLAB", "gray sweep resumed at %lu ms", static_cast<unsigned long>(elapsedUs / 1000));
}

void TransitionTestActivity::cleanAndPause() {
  const uint32_t nowUs = micros();
  if (running) {
    advanceClock(nowUs);
    reportStats(nowUs);
  }
  running = false;
  scheduler.stop();
  RenderLock lock(*this);
  cleanToWhite();  // the next slot reapplies the scene at the paused clock
  LOG_INF("DLAB", "gray sweep cleaned and paused at %lu ms", static_cast<unsigned long>(elapsedUs / 1000));
}

void TransitionTestActivity::leaveView(const char* outcome) {
  const uint32_t nowUs = micros();
  if (running) reportStats(nowUs);
  running = false;
  scheduler.stop();
  app.clearTapFlash();
  {
    RenderLock lock(*this);
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

void TransitionTestActivity::runSlot(const uint32_t nowUs) {
  advanceClock(nowUs);
  uint32_t slotStartUs = nowUs;
  // A slot is two scans; the lab cleans on its own at the session cap, which must not fall between them.
  if (lab.scansBeforeClean() < SCANS_PER_SLOT) {
    LOG_INF("DLAB", "gray sweep: session cap near, cleaning first");
    reportStats(nowUs);  // the speed up to the clean
    {
      RenderLock lock(*this);
      cleanToWhite();
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
  const auto scan = [this, &preset, &failure](const MaskPlanes& masks, uint32_t& busyUs) {
    // One dose: a one-frame activation of each driven pixel.
    const ScanRequest request{.blackMask = masks.black(),
                              .whiteMask = masks.white(),
                              .frames = 1,
                              .rate = preset.rateIndex,
                              .swapPolarity = preset.swapPolarity};
    const DisplayLabActivity::Scan result = lab.scan(request, busyUs);
    if (result != DisplayLabActivity::Scan::Done) failure = result;
    return result == DisplayLabActivity::Scan::Done;
  };
  const GameDisplayLab::SlotResult slot =
      GameDisplayLab::runGraySlot(model, lab.masks(), follow, preset, weakRow, scan);
  const uint32_t endUs = micros();
  scheduler.slotDone(slotStartUs, endUs, slot.busyUs);
  scheduler.record(slot.scans, slot.busyUs);
  if (failure != DisplayLabActivity::Scan::Done) {
    // The lab logged why (and cleaned the panel after a timeout); the sweep ends here and the rows say how.
    leaveView(DisplayLabActivity::outcomeText(failure));
    return;
  }
  if (endUs - statsSinceUs >= STATS_EVERY_US) reportStats(endUs);
}

void TransitionTestActivity::logStats(const uint32_t nowUs) const {
  LOG_INF("DLAB", "gray scans_s=%u.%u ms_per_scan=%u skipped=%lu",
          static_cast<unsigned>(scheduler.scansPerSecondTenths(nowUs) / 10),
          static_cast<unsigned>(scheduler.scansPerSecondTenths(nowUs) % 10),
          static_cast<unsigned>(scheduler.msPerScan()), static_cast<unsigned long>(scheduler.skipped()));
}

void TransitionTestActivity::reportStats(const uint32_t nowUs) {
  logStats(nowUs);
  const uint32_t tenths = scheduler.scansPerSecondTenths(nowUs);
  const uint32_t msPerScan = scheduler.msPerScan();
  {
    // The render task reads the line once the rows are back.
    RenderLock lock(*this);
    snprintf(statsText, sizeof(statsText), tr(STR_GAMES_DLAB_STATS), static_cast<unsigned>(tenths / 10),
             static_cast<unsigned>(tenths % 10), static_cast<unsigned>(msPerScan));
  }
  scheduler.resetWindow(nowUs);
  statsSinceUs = nowUs;
}

#endif  // CROSSHATCH_DISPLAY_LAB
