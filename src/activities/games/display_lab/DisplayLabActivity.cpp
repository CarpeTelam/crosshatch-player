#include "DisplayLabActivity.h"

#if CROSSHATCH_DISPLAY_LAB

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <Ssd1677ScanLut.h>

#include <cstring>

#include "FramePeriodActivity.h"
#include "MappedInputManager.h"
#include "PolarityActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

bool DisplayLabActivity::available() {
  // BoardConfig comes with HalDisplay.h (HalDisplay::Controller is its DisplayController).
  return BoardConfig::isX4Pro() && display.getController() == BoardConfig::DisplayController::SSD1677;
}

DisplayLabActivity::DisplayLabActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity(NAME, renderer, mappedInput) {}

int DisplayLabActivity::listCount() const { return notice ? 1 : static_cast<int>(ROW_COUNT); }

const char* DisplayLabActivity::headerTitle() const { return tr(STR_GAMES_DLAB_TITLE); }

void DisplayLabActivity::onEnter() {
  UiListActivity::onEnter();
  // The render task may draw this screen while onEnter runs, and it reads the rows and the notice.
  RenderLock lock(*this);
  notice = nullptr;
  budget = GameDisplayLab::ScanBudget{SESSION_CAP, SCAN_TIMEOUT_MS * 1000UL, DROPPED_BELOW_US};
  if (!available()) {
    // The launcher never offers the row elsewhere; a scan on another panel is never sent.
    LOG_ERR("DLAB", "Not an X4 Pro with an SSD1677; the lab runs nothing");
    notice = tr(STR_GAMES_DLAB_NOT_SSD1677);
  } else if (display.isInverted()) {
    LOG_ERR("DLAB", "Display inverted; the lab runs nothing");
    notice = tr(STR_GAMES_DLAB_INVERTED);
  } else {
    // Two panel-sized mask planes for the whole session: never per scan. PSRAM only, as the game arena (no internal
    // RAM fallback, by allocatePsram's contract).
    planeBytes = display.getBufferSize();
    bwPlane = HalMemory::allocatePsram(planeBytes);
    redPlane = HalMemory::allocatePsram(planeBytes);
    if (!bwPlane || !redPlane) {
      LOG_ERR("DLAB", "OOM: two %u byte PSRAM planes", static_cast<unsigned>(planeBytes));
      bwPlane.reset();
      redPlane.reset();
      notice = tr(STR_GAMES_OUT_OF_MEMORY);
    }
  }
  buildRows();
}

void DisplayLabActivity::onExit() {
  // ActivityManager holds the render mutex here (a pop, or a Replace by Home or sleep): take no RenderLock (12cc816).
  // One cleaning refresh on the way out when a scan reached the panel since the last clean (it may have left the panel
  // driven, and the driver flagged a gray clear); none from the notice paths or a visit that ran nothing.
  if (budget.sinceClean() > 0) cleanPanel();
  LOG_INF("DLAB", "exit: %lu scans this session", static_cast<unsigned long>(budget.total()));
  bwPlane.reset();
  redPlane.reset();
  Activity::onExit();
}

void DisplayLabActivity::cleanPanel() {
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
  budget.cleaned();
}

void DisplayLabActivity::buildRows() {
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
  set(Row::FramePeriod, tr(STR_GAMES_DLAB_FRAME_PERIOD), tr(STR_GAMES_DLAB_FRAME_PERIOD_DESC));
  set(Row::Polarity, tr(STR_GAMES_DLAB_POLARITY), tr(STR_GAMES_DLAB_POLARITY_DESC));
  set(Row::Exit, tr(STR_GAMES_DLAB_EXIT), nullptr);
}

void DisplayLabActivity::buildScreen(UiScreen& screen) {
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
  props.subtitleText.maxLines = 1;
  syncListViewport(screen, props);
  screen.list(props);
}

template <typename T>
void DisplayLabActivity::openScreen() {
  auto screen = makeUniqueNoThrow<T>(renderer, mappedInput, *this);
  if (!screen) {
    LOG_ERR("DLAB", "OOM: %u byte %s screen", static_cast<unsigned>(sizeof(T)), T::NAME);
    requestUpdate();  // the tap flash was cleared; repaint rather than leave a stale frame
    return;
  }
  // Pushed for its result so Back pops to this menu; the handler has nothing to do (the base repaints after a pop).
  startActivityForResult(std::move(screen), [](const ActivityResult&) {});
}

void DisplayLabActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();  // every row leaves this screen
  if (notice) {
    finish();
    return;
  }
  switch (static_cast<Row>(index)) {
    case Row::FramePeriod:
      openScreen<FramePeriodActivity>();
      return;
    case Row::Polarity:
      openScreen<PolarityActivity>();
      return;
    case Row::Exit:
      finish();
      return;
  }
}

void DisplayLabActivity::onBackButton() {
  app.clearTapFlash();
  finish();  // onExit cleans the panel and frees the planes
}

DisplayLabActivity::Scan DisplayLabActivity::scan(const uint8_t* lut, const uint8_t bwFill, const uint8_t redFill,
                                                  uint32_t& busyUs) {
  busyUs = 0;
  if (!bwPlane || !redPlane) {
    LOG_ERR("DLAB", "scan refused: no mask planes");  // the lab shows its notice row instead of the experiments
    return Scan::Refused;
  }
  // Checked per scan, not only in onEnter: the control-center panel can invert the display while the lab is open, and
  // the facade then drops both calls below without a word.
  if (display.isInverted()) {
    LOG_ERR("DLAB", "scan refused: display inverted");
    return Scan::Refused;
  }
  // The render task must not push the framebuffer between the plane writes and the activation.
  RenderLock lock(*this);
  if (budget.cleanDue()) {
    LOG_INF("DLAB", "session cap: %lu scans since the last clean", static_cast<unsigned long>(budget.sinceClean()));
    cleanPanel();
  }
  memset(bwPlane.get(), bwFill, planeBytes);
  memset(redPlane.get(), redFill, planeBytes);
  EInkDisplay& eink = display.eink();
  // Raw masks into both RAM planes (overlay mode writes them as given), then the caller's LUT, activation and the
  // BUSY wait, timed as one call: the frame period is the slope of this time against TP, which cancels the fixed
  // upload and activation overhead.
  eink.copyGrayscaleBuffers(bwPlane.get(), redPlane.get());
  const unsigned long began = micros();
  eink.displayGrayBuffer(false, lut, true);
  busyUs = static_cast<uint32_t>(micros() - began);
  switch (budget.record(busyUs)) {
    case GameDisplayLab::ScanBudget::Outcome::Done:
      return Scan::Done;
    case GameDisplayLab::ScanBudget::Outcome::TimedOut:
      LOG_ERR("DLAB", "timeout: scan %lu took %lu us", static_cast<unsigned long>(budget.total()),
              static_cast<unsigned long>(busyUs));
      cleanPanel();
      return Scan::TimedOut;
    case GameDisplayLab::ScanBudget::Outcome::Dropped:
      // The facade returned without driving the panel (a failed gray pass, or an inversion it saw and this check
      // missed): nothing to clean, and the time is not a measurement.
      LOG_ERR("DLAB", "dropped: scan %lu came back in %lu us, under the %lu us floor",
              static_cast<unsigned long>(budget.total()), static_cast<unsigned long>(busyUs),
              static_cast<unsigned long>(DROPPED_BELOW_US));
      return Scan::Dropped;
  }
  return Scan::Done;
}

#endif  // CROSSHATCH_DISPLAY_LAB
