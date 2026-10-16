#include "GrayRampActivity.h"

#if CROSSHATCH_DISPLAY_LAB

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>
#include <MaskPlanes.h>
#include <StockBench.h>

#include <cstdio>
#include <utility>

#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

using GameDisplayLab::BandAxis;

namespace {

constexpr uint32_t TIMEOUT_US = DisplayLabActivity::SCAN_TIMEOUT_MS * 1000UL;

unsigned msOf(const uint32_t us) { return static_cast<unsigned>(us / 1000); }

// True, having logged it, when the call `what` took longer than the lab's timeout.
bool timedOut(const char* what, const uint32_t us) {
  if (us <= TIMEOUT_US) return false;
  LOG_ERR("DLAB", "timeout: %s took %lu us", what, static_cast<unsigned long>(us));
  return true;
}

// The panel axis and direction that run left to right as the user holds the device in `orientation`: the logical x
// is H - 1 - py in Portrait, py in PortraitInverted, W - 1 - px in LandscapeClockwise, px in LandscapeCounterClockwise.
void bandsFor(const GfxRenderer::Orientation orientation, BandAxis& axis, bool& reversed) {
  switch (orientation) {
    case GfxRenderer::Portrait:
      axis = BandAxis::Y;
      reversed = true;
      return;
    case GfxRenderer::PortraitInverted:
      axis = BandAxis::Y;
      reversed = false;
      return;
    case GfxRenderer::LandscapeClockwise:
      axis = BandAxis::X;
      reversed = true;
      return;
    case GfxRenderer::LandscapeCounterClockwise:
      axis = BandAxis::X;
      reversed = false;
      return;
  }
  axis = BandAxis::X;
  reversed = false;
}

}  // namespace

GrayRampActivity::GrayRampActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, DisplayLabActivity& /*lab*/)
    : UiListActivity(NAME, renderer, mappedInput) {}

const char* GrayRampActivity::headerTitle() const { return tr(STR_GAMES_DLAB_GRAY_RAMP); }

void GrayRampActivity::onEnter() {
  UiListActivity::onEnter();
  // The render task may draw this screen while onEnter runs, and it reads the row.
  RenderLock lock(*this);
  drovePanel = false;
  held = false;
  rowItem = fui::ListItem{};
  rowItem.label = tr(STR_GAMES_DLAB_GRAY_RAMP);
  rowItem.actionValue = 0;
  setStatus(tr(STR_GAMES_DLAB_NOT_RUN));
  // Allocated once per visit, never per pass: PSRAM only (no internal RAM fallback, by allocatePsram's contract).
  const size_t bytes = display.getBufferSize();
  lsbPlane = HalMemory::allocatePsram(bytes);
  msbPlane = HalMemory::allocatePsram(bytes);
  if (!lsbPlane || !msbPlane) {
    LOG_ERR("DLAB", "OOM: two %u byte PSRAM gray planes", static_cast<unsigned>(bytes));
    lsbPlane.reset();
    msbPlane.reset();
    setStatus(tr(STR_GAMES_OUT_OF_MEMORY));
  }
}

void GrayRampActivity::onExit() {
  // ActivityManager holds the render mutex here (Home or sleep; Back already promoted its FULL and cleared the flag):
  // take no RenderLock (12cc816).
  if (drovePanel) {
    renderer.displayBuffer(HalDisplay::FULL_REFRESH);
    drovePanel = false;
  }
  lsbPlane.reset();
  msbPlane.reset();
  Activity::onExit();
}

void GrayRampActivity::setStatus(const char* text) {
  snprintf(status, sizeof(status), "%s", text);
  rowItem.subtitle = status;
}

void GrayRampActivity::render(RenderLock&& lock) {
  // A routed tap and a selection move both request a repaint; while the panel holds the ramp, none may run (the
  // repaint carries the cleaning FULL refresh). The next activation clears the flag and asks again.
  if (held.load()) return;
  UiListActivity::render(std::move(lock));
}

void GrayRampActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the header band GUI.drawHeader paints.
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  fui::ListProps props;
  props.items = &rowItem;
  props.count = 1;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void GrayRampActivity::activateIndex(const int index) {
  if (index != 0) return;
  if (held.load()) {
    // The user has looked: this press only brings the screen back, with a FULL refresh that cleans the panel; its tap
    // flash must not be drawn into that repaint.
    app.clearTapFlash();
    RenderLock lock(*this);
    held = false;
    renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
    requestUpdate();
    return;
  }
  // The row's tap flash would otherwise be drawn into the repaint that follows the pass.
  app.clearTapFlash();
  paint();
}

void GrayRampActivity::paint() {
  // The whole pass: the render task must not push the framebuffer between the ramp fill and the pass, and it reads the
  // row.
  RenderLock lock(*this);
  // Checked per activation, not only in the lab's onEnter: the control-center panel can invert the display meanwhile.
  if (display.isInverted()) {
    LOG_ERR("DLAB", "gray refused: display inverted");
    setStatus(tr(STR_GAMES_DLAB_INVERTED));
    requestUpdate();
    return;
  }
  if (!lsbPlane || !msbPlane) {
    LOG_ERR("DLAB", "gray refused: no gray planes");
    setStatus(tr(STR_GAMES_OUT_OF_MEMORY));
    requestUpdate();
    return;
  }
  if (!display.grayscaleCapabilities(HalDisplay::GrayscaleMode::Overlay).supported()) {
    LOG_INF("DLAB", "gray unsupported");
    setStatus(tr(STR_GAMES_DLAB_GRAY_UNSUPPORTED));
    requestUpdate();
    return;
  }
  uint8_t* const frame = display.getFrameBuffer();
  const uint16_t width = display.getDisplayWidth();
  const uint16_t height = display.getDisplayHeight();
  const size_t bytes = display.getBufferSize();
  if (frame == nullptr || GameDisplayLab::MaskPlanes::bytesFor(width, height) != bytes) {
    LOG_ERR("DLAB", "gray refused: framebuffer of %u bytes is not %ux%u at 1 bpp", static_cast<unsigned>(bytes),
            static_cast<unsigned>(width), static_cast<unsigned>(height));
    setStatus(tr(STR_GAMES_DLAB_REFUSED));
    requestUpdate();
    return;
  }
  BandAxis axis = BandAxis::X;
  bool reversed = false;
  bandsFor(renderer.getOrientation(), axis, reversed);
  GameDisplayLab::fillGrayRamp(frame, lsbPlane.get(), msbPlane.get(), width, height, axis, reversed);
  drovePanel = true;

  // The reader's per-page order: the B/W base (black under the three darker bands), then the two masks and the pass.
  uint32_t start = micros();
  display.displayGrayscaleBase(HalDisplay::FAST_REFRESH, false);
  const uint32_t baseUs = micros() - start;  // unsigned: right across a micros() wrap
  bool overTime = timedOut("gray base", baseUs);
  uint32_t grayUs = 0;
  if (!overTime) {
    start = micros();
    display.copyGrayscaleBuffers(lsbPlane.get(), msbPlane.get());
    display.displayGrayBuffer(false);
    grayUs = micros() - start;
    overTime = timedOut("gray pass", grayUs);
  }
  // Restores the B/W frame in the controller, as the reader does after every gray pass (and after a base that timed
  // out, before any plane went up).
  display.cleanupGrayscaleBuffers(frame);
  if (overTime) {
    setStatus(tr(STR_GAMES_DLAB_TIMED_OUT));
    // Nothing to look at: the screen comes back now, cleaning the panel.
    renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
    requestUpdate();
    return;
  }
  LOG_INF("DLAB", "gray base_ms=%u gray_ms=%u", msOf(baseUs), msOf(grayUs));
  snprintf(status, sizeof(status), tr(STR_GAMES_DLAB_GRAY_RESULT), msOf(baseUs), msOf(grayUs));
  rowItem.subtitle = status;
  // The panel keeps the ramp until the next Confirm or tap; that activation repaints with a FULL refresh.
  held = true;
}

void GrayRampActivity::onBackButton() {
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
