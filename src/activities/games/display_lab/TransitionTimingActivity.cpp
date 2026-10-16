#include "TransitionTimingActivity.h"

#if CROSSHATCH_DISPLAY_LAB

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>
#include <MaskPlanes.h>
#include <StockBench.h>

#include <cstdio>
#include <cstring>

#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {

constexpr uint32_t TIMEOUT_US = DisplayLabActivity::SCAN_TIMEOUT_MS * 1000UL;

unsigned msOf(const uint32_t us) { return static_cast<unsigned>(us / 1000); }

// True, having logged it, when the call `what` took longer than the lab's timeout.
bool timedOut(const char* what, const uint32_t us) {
  if (us <= TIMEOUT_US) return false;
  LOG_ERR("DLAB", "timeout: %s took %lu us", what, static_cast<unsigned long>(us));
  return true;
}

}  // namespace

TransitionTimingActivity::TransitionTimingActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                   DisplayLabActivity& /*lab*/)
    : UiListActivity(NAME, renderer, mappedInput) {}

const char* TransitionTimingActivity::headerTitle() const { return tr(STR_GAMES_DLAB_TRANSITION); }

void TransitionTimingActivity::onEnter() {
  UiListActivity::onEnter();
  // The render task may draw this screen while onEnter runs, and it reads the rows.
  RenderLock lock(*this);
  drovePanel = false;
  status = tr(STR_GAMES_DLAB_NOT_RUN);
  // Allocated once per visit, never per run: PSRAM only (no internal RAM fallback, by allocatePsram's contract).
  const size_t bytes = display.getBufferSize();
  zeroPlane = HalMemory::allocatePsram(bytes);
  if (zeroPlane) {
    memset(zeroPlane.get(), 0, bytes);
  } else {
    LOG_ERR("DLAB", "OOM: %u byte PSRAM zero plane", static_cast<unsigned>(bytes));
    status = tr(STR_GAMES_OUT_OF_MEMORY);
  }
  for (size_t i = 0; i < FLIPS; ++i) {
    snprintf(flipLabels[i], sizeof(flipLabels[i]), tr(STR_GAMES_DLAB_TRANSITION_N), static_cast<unsigned>(i + 1));
  }
  resetResults();
  buildRows();
}

void TransitionTimingActivity::onExit() {
  // ActivityManager holds the render mutex here (Home or sleep; Back already promoted its FULL and cleared the flag):
  // take no RenderLock (12cc816).
  if (drovePanel) {
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    drovePanel = false;
  }
  zeroPlane.reset();
  Activity::onExit();
}

void TransitionTimingActivity::resetResults() {
  for (size_t i = 0; i < ROW_COUNT; ++i) snprintf(results[i], sizeof(results[i]), "%s", tr(STR_GAMES_DLAB_NOT_RUN));
  flipCount = 0;
}

void TransitionTimingActivity::setStatus(const char* text) {
  status = text;
  rowItems[ROW_RUN].subtitle = status;
}

void TransitionTimingActivity::buildRows() {
  const auto set = [this](const size_t row, const char* label, const char* subtitle) {
    fui::ListItem item;
    item.label = label;
    item.subtitle = subtitle;
    item.actionValue = static_cast<int16_t>(row);
    rowItems[row] = item;
  };
  set(ROW_RUN, tr(STR_GAMES_DLAB_RUN), status);
  set(ROW_REFERENCE, tr(STR_GAMES_DLAB_REFERENCE), results[ROW_REFERENCE]);
  set(ROW_ARM, tr(STR_GAMES_DLAB_ARM), results[ROW_ARM]);
  for (size_t i = 0; i < FLIPS; ++i) set(ROW_FIRST_FLIP + i, flipLabels[i], results[ROW_FIRST_FLIP + i]);
  set(ROW_SUMMARY, tr(STR_GAMES_DLAB_SUMMARY), results[ROW_SUMMARY]);
}

void TransitionTimingActivity::buildScreen(UiScreen& screen) {
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
  props.count = static_cast<uint16_t>(ROW_COUNT);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.maxLines = 1;
  syncListViewport(screen, props);
  screen.list(props);
}

void TransitionTimingActivity::activateIndex(const int index) {
  // Only the Run row does anything; the others are table lines.
  if (index != static_cast<int>(ROW_RUN)) return;
  run();
}

void TransitionTimingActivity::run() {
  // The run replaces the screen with the checker, and its repaint must not carry the row's flash.
  app.clearTapFlash();
  // The whole run: the render task must not push the framebuffer between a checker fill and its refresh, and it reads
  // the rows.
  RenderLock lock(*this);
  // Checked per run, not only in the lab's onEnter: the control-center panel can invert the display meanwhile.
  if (display.isInverted()) {
    LOG_ERR("DLAB", "transition refused: display inverted");
    setStatus(tr(STR_GAMES_DLAB_INVERTED));
    requestUpdate();
    return;
  }
  if (!zeroPlane) {
    LOG_ERR("DLAB", "transition refused: no zero plane");
    setStatus(tr(STR_GAMES_OUT_OF_MEMORY));
    requestUpdate();
    return;
  }
  uint8_t* const frame = display.getFrameBuffer();
  const uint16_t width = display.getDisplayWidth();
  const uint16_t height = display.getDisplayHeight();
  const size_t bytes = display.getBufferSize();
  if (frame == nullptr || GameDisplayLab::MaskPlanes::bytesFor(width, height) != bytes) {
    LOG_ERR("DLAB", "transition refused: framebuffer of %u bytes is not %ux%u at 1 bpp", static_cast<unsigned>(bytes),
            static_cast<unsigned>(width), static_cast<unsigned>(height));
    setStatus(tr(STR_GAMES_DLAB_REFUSED));
    requestUpdate();
    return;
  }
  resetResults();  // only now: a refused run keeps the previous table
  drovePanel = true;
  setStatus(measure(frame, width, height));
  const GameDisplayLab::TimingSummary summary = GameDisplayLab::summarizeTimings(flipUs, flipCount);
  if (summary.count > 0) {
    snprintf(results[ROW_SUMMARY], sizeof(results[ROW_SUMMARY]), tr(STR_GAMES_DLAB_SUMMARY_MS), msOf(summary.min),
             msOf(summary.median), msOf(summary.max));
  }
  // The framebuffer holds the last checker: the table comes back with a FULL refresh, which also cleans the panel.
  renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
  requestUpdate();
}

const char* TransitionTimingActivity::measure(uint8_t* const frame, const uint16_t width, const uint16_t height) {
  // The reference first: after a gray pass the driver would turn this FAST into the transition itself.
  GameDisplayLab::fillChecker(frame, width, height, false);
  uint32_t start = micros();
  display.displayBuffer(HalDisplay::FAST_REFRESH, false);
  const uint32_t referenceUs = micros() - start;  // unsigned: right across a micros() wrap
  LOG_INF("DLAB", "transition ref ms=%u", msOf(referenceUs));
  snprintf(results[ROW_REFERENCE], sizeof(results[ROW_REFERENCE]), tr(STR_GAMES_DLAB_MS), msOf(referenceUs));
  if (timedOut("reference refresh", referenceUs)) return tr(STR_GAMES_DLAB_TIMED_OUT);

  // Arming: only a gray pass sets the drivers' "gray refreshed once" state that the transition needs, so one pass of
  // all-B/W masks (every pixel (0, 0)) over the same checker; the panel keeps showing it.
  bool armed = false;
  if (display.grayscaleCapabilities(HalDisplay::GrayscaleMode::Overlay).supported()) {
    // Two timed steps, each checked before the next panel call; the cleanup stays outside both, as in Gray ramp.
    start = micros();
    display.displayGrayscaleBase(HalDisplay::FAST_REFRESH, false);
    const uint32_t baseUs = micros() - start;
    if (timedOut("arm base", baseUs)) {
      snprintf(results[ROW_ARM], sizeof(results[ROW_ARM]), tr(STR_GAMES_DLAB_MS), msOf(baseUs));
      return tr(STR_GAMES_DLAB_TIMED_OUT);
    }
    start = micros();
    display.copyGrayscaleBuffers(zeroPlane.get(), zeroPlane.get());
    display.displayGrayBuffer(false);
    const uint32_t grayUs = micros() - start;
    display.cleanupGrayscaleBuffers(frame);
    const uint32_t armUs = baseUs + grayUs;  // each under the timeout, so no wrap
    LOG_INF("DLAB", "arm ms=%u", msOf(armUs));
    snprintf(results[ROW_ARM], sizeof(results[ROW_ARM]), tr(STR_GAMES_DLAB_MS), msOf(armUs));
    if (timedOut("arm gray pass", grayUs)) return tr(STR_GAMES_DLAB_TIMED_OUT);
    armed = true;
  } else {
    // Unarmed, every flip below is a plain refresh, which transitionFellBack reports as fell back.
    LOG_INF("DLAB", "arm skipped: no overlay grayscale");
    snprintf(results[ROW_ARM], sizeof(results[ROW_ARM]), "%s", tr(STR_GAMES_DLAB_GRAY_UNSUPPORTED));
  }

  for (size_t i = 0; i < FLIPS; ++i) {
    const unsigned n = static_cast<unsigned>(i + 1);
    GameDisplayLab::fillChecker(frame, width, height, n % 2 == 1);
    start = micros();
    display.displayGrayscaleBase(HalDisplay::FAST_REFRESH, false);
    const uint32_t us = micros() - start;
    const bool flashed = GameDisplayLab::transitionFellBack(us, referenceUs, armed);
    LOG_INF("DLAB", "transition n=%u ms=%u flashed=%s", n, msOf(us), flashed ? "yes" : "no");
    snprintf(results[ROW_FIRST_FLIP + i], sizeof(results[ROW_FIRST_FLIP + i]),
             flashed ? tr(STR_GAMES_DLAB_MS_FELL_BACK) : tr(STR_GAMES_DLAB_MS), msOf(us));
    // A flip over the timeout is no measurement: its row shows the time, the summary leaves it out.
    if (timedOut("transition", us)) return tr(STR_GAMES_DLAB_TIMED_OUT);
    flipUs[i] = us;
    flipCount = i + 1;
  }
  return tr(STR_GAMES_DLAB_DONE);
}

void TransitionTimingActivity::onBackButton() {
  app.clearTapFlash();
  if (drovePanel) {
    // The leaving FULL refresh rides on the menu's repaint, so it shows the menu.
    RenderLock lock(*this);
    renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
    drovePanel = false;
  }
  finish();  // pops to the lab menu
}

#endif  // CROSSHATCH_DISPLAY_LAB
