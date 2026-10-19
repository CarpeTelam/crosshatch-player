#include "DisplayLabActivity.h"

#if CROSSHATCH_DISPLAY_LAB

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include "FramePeriodActivity.h"
#include "GrayPresetFile.h"
#include "GrayRampActivity.h"
#include "GrayViewerActivity.h"
#include "MappedInputManager.h"
#include "PolarityActivity.h"
#include "ScanBackends.h"
#include "StockBenchGate.h"
#include "TransitionTestActivity.h"
#include "TransitionTimingActivity.h"
#include "TuningActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

bool DisplayLabActivity::available() {
  // BoardConfig comes with HalDisplay.h (HalDisplay::Controller is its DisplayController).
  return BoardConfig::isX4Pro() && labMenuFor(display.getController()) != GameDisplayLab::LabMenu::None;
}

DisplayLabActivity::DisplayLabActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity(NAME, renderer, mappedInput) {}

int DisplayLabActivity::listCount() const {
  if (notice) return 1;
  return static_cast<int>(rowCount);
}

const char* DisplayLabActivity::headerTitle() const { return tr(STR_GAMES_DLAB_TITLE); }

void DisplayLabActivity::onEnter() {
  UiListActivity::onEnter();
  // The gray preset: the file's, over the controller's defaults (the epic's D12: the owner's UltraChip table on a
  // UC8179 or UC8279, InkBoy's on the SSD1677), or those defaults alone (GrayPresetFile logs which). Read before the
  // render lock, so the render task never waits on the SD card; openSession takes it.
  const GameDisplayLab::PresetBase base =
      isUltraChip(display.getController()) ? GameDisplayLab::PresetBase::UltraChip : GameDisplayLab::PresetBase::InkBoy;
  GameDisplayLab::GrayPreset loaded = GameDisplayLab::presetDefaults(base);
  // The loop task's alone: the panel's base, and the one the session's values carry (a loaded file's own, which Save
  // writes back, so a file from another controller keeps saying so until Reset to defaults).
  panelBase = base;
  presetBase = base;
  if (available()) GrayPresetFile::load(base, loaded, presetBase);
  // The render task may draw this screen while onEnter runs, and it reads the rows and the notice.
  RenderLock lock(*this);
  notice = nullptr;
  saveStatus = nullptr;
  budget = GameDisplayLab::ScanBudget{SESSION_CAP, SCAN_TIMEOUT_MS * 1000UL, DROPPED_BELOW_US};
  grayPreset = GameDisplayLab::presetDefaults(base);
  const bool supported = available();
  menu = supported ? labMenuFor(display.getController()) : GameDisplayLab::LabMenu::None;
  const bool stockBench = menu == GameDisplayLab::LabMenu::StockBench;
  if (stockBench) LOG_INF("DLAB", "UltraChip stock-path bench");
  if (!supported) {
    // The launcher never offers the row elsewhere; a scan on another panel is never sent.
    LOG_ERR("DLAB", "Not an X4 Pro with a scan backend or an UltraChip stock bench; the lab runs nothing");
    notice = tr(STR_GAMES_DLAB_NOT_SUPPORTED);
  } else if (display.isInverted()) {
    LOG_ERR("DLAB", "Display inverted; the lab runs nothing");
    notice = tr(STR_GAMES_DLAB_INVERTED);
  } else if (!stockBench && !openSession(loaded)) {
    closeSession();
    notice = tr(STR_GAMES_OUT_OF_MEMORY);
  }
  buildRows();
}

bool DisplayLabActivity::openSession(const GameDisplayLab::GrayPreset& loaded) {
  // A request's masks are full-screen bitmaps in the framebuffer's byte order, as many bytes as the framebuffer.
  const size_t bytes = display.getBufferSize();
  const uint16_t width = display.getDisplayWidth();
  const uint16_t height = display.getDisplayHeight();
  if (GameDisplayLab::MaskPlanes::bytesFor(width, height) != bytes) {
    LOG_ERR("DLAB", "framebuffer of %u bytes is not %ux%u at 1 bpp", static_cast<unsigned>(bytes),
            static_cast<unsigned>(width), static_cast<unsigned>(height));
    return false;
  }
  // Two panel-sized masks for the whole session, shared by every screen: never per scan. PSRAM only, as the game
  // arena (no internal RAM fallback, by allocatePsram's contract).
  blackMask = HalMemory::allocatePsram(bytes);
  whiteMask = HalMemory::allocatePsram(bytes);
  if (!blackMask || !whiteMask) {
    LOG_ERR("DLAB", "OOM: two %u byte PSRAM masks", static_cast<unsigned>(bytes));
    return false;
  }
  scanBackend = makeScanBackend(display.getController(), renderer);
  if (!scanBackend) {
    LOG_ERR("DLAB", "OOM: scan backend");
    return false;
  }
  if (!scanBackend->available()) return false;  // it logged what did not fit
  maskPlanes = GameDisplayLab::MaskPlanes(blackMask.get(), whiteMask.get(), width, height);
  maskPlanes.clear();
  LOG_INF("DLAB", "backend %s, %u rates", scanBackend->name(), static_cast<unsigned>(scanBackend->rateCount()));
  // The preset onEnter read; a rate index written for another backend's list falls back to the first rate.
  grayPreset = loaded;
  if (grayPreset.rateIndex >= scanBackend->rateCount()) {
    LOG_ERR("DLAB", "preset rate index %u not below the backend's %u rates: rate 0",
            static_cast<unsigned>(grayPreset.rateIndex), static_cast<unsigned>(scanBackend->rateCount()));
    grayPreset.rateIndex = 0;
  }
  return true;
}

void DisplayLabActivity::closeSession() {
  maskPlanes = GameDisplayLab::MaskPlanes();
  scanBackend.reset();
  blackMask.reset();
  whiteMask.reset();
}

void DisplayLabActivity::onExit() {
  // ActivityManager holds the render mutex here (a pop, or a Replace by Home or sleep): take no RenderLock (12cc816).
  // One cleaning refresh on the way out when a scan reached the panel since the last clean (it may have left the panel
  // driven, and the driver flagged a gray clear); none from the notice paths or a visit that ran nothing.
  if (budget.sinceClean() > 0) cleanPanel();
  LOG_INF("DLAB", "exit: %lu scans this session", static_cast<unsigned long>(budget.total()));
  closeSession();
  Activity::onExit();
}

void DisplayLabActivity::cleanPanel() {
  scanBackend->clean();
  budget.cleaned();
}

void DisplayLabActivity::cleanNow() {
  if (scanBackend) cleanPanel();
}

GameDisplayLab::PresetBase DisplayLabActivity::resetPreset() {
  grayPreset = GameDisplayLab::presetDefaults(panelBase);
  presetBase = panelBase;
  return panelBase;
}

bool DisplayLabActivity::savePreset() {
  // The SD write runs outside the render lock; only the row's line is shared with the render task.
  const bool saved = GrayPresetFile::save(grayPreset, presetBase);
  RenderLock lock(*this);
  saveStatus = saved ? tr(STR_GAMES_DLAB_SAVED) : tr(STR_GAMES_DLAB_SAVE_FAILED);
  // The Save preset row, found by its Row (only the scan menus have it; Tuning, its other caller, is on them too).
  if (!notice) {
    for (size_t i = 0; i < rowCount; ++i) {
      if (rowItems[i].actionValue == static_cast<int16_t>(Row::SavePreset)) rowItems[i].subtitle = saveStatus;
    }
  }
  return saved;
}

const char* DisplayLabActivity::outcomeText(const Scan result) {
  switch (result) {
    case Scan::Done:
      return tr(STR_GAMES_DLAB_DONE);
    case Scan::TimedOut:
      return tr(STR_GAMES_DLAB_TIMED_OUT);
    case Scan::Dropped:
      return tr(STR_GAMES_DLAB_DROPPED);
    case Scan::Refused:
      return tr(STR_GAMES_DLAB_REFUSED);  // no session, an inverted display, or a bad request: the log says which
  }
  return "";
}

void DisplayLabActivity::buildRows() {
  rowCount = 0;
  if (notice) {
    fui::ListItem item;
    item.label = notice;
    item.actionValue = 0;
    rowItems[0] = item;
    return;
  }
  Row rows[GameDisplayLab::LAB_ROW_COUNT] = {};
  rowCount = GameDisplayLab::labMenuRows(menu, rows);
  for (size_t i = 0; i < rowCount; ++i) {
    fui::ListItem item;
    item.actionValue = static_cast<int16_t>(rows[i]);
    switch (rows[i]) {
      case Row::FramePeriod:
        item.label = tr(STR_GAMES_DLAB_FRAME_PERIOD);
        item.subtitle = tr(STR_GAMES_DLAB_FRAME_PERIOD_DESC);
        break;
      case Row::Polarity:
        item.label = tr(STR_GAMES_DLAB_POLARITY);
        item.subtitle = tr(STR_GAMES_DLAB_POLARITY_DESC);
        break;
      case Row::TransitionTest:
        item.label = tr(STR_GAMES_DLAB_TRANSITION_TEST);
        item.subtitle = tr(STR_GAMES_DLAB_TRANSITION_TEST_DESC);
        break;
      case Row::GrayViewer:
        item.label = tr(STR_GAMES_DLAB_GRAY_VIEWER);
        item.subtitle = tr(STR_GAMES_DLAB_GRAY_VIEWER_DESC);
        break;
      case Row::Tuning:
        item.label = tr(STR_GAMES_DLAB_TUNING);
        item.subtitle = tr(STR_GAMES_DLAB_TUNING_DESC);
        break;
      case Row::SavePreset:
        item.label = tr(STR_GAMES_DLAB_SAVE_PRESET);
        item.subtitle = saveStatus ? saveStatus : tr(STR_GAMES_DLAB_SAVE_PRESET_DESC);
        break;
      case Row::TransitionTiming:
        item.label = tr(STR_GAMES_DLAB_TRANSITION);
        item.subtitle = tr(STR_GAMES_DLAB_TRANSITION_DESC);
        break;
      case Row::GrayRamp:
        item.label = tr(STR_GAMES_DLAB_GRAY_RAMP);
        item.subtitle = tr(STR_GAMES_DLAB_GRAY_RAMP_DESC);
        break;
      case Row::Exit:
        item.label = tr(STR_GAMES_DLAB_EXIT);
        item.subtitle = nullptr;
        break;
    }
    rowItems[i] = item;
  }
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
  app.clearTapFlash();  // every row but Save preset leaves this screen, which repaints its line
  if (notice) {
    finish();
    return;
  }
  // The row's own Row, not its index: the menus place the rows differently.
  switch (static_cast<Row>(rowItems[index].actionValue)) {
    case Row::FramePeriod:
      openScreen<FramePeriodActivity>();
      return;
    case Row::Polarity:
      openScreen<PolarityActivity>();
      return;
    case Row::TransitionTest:
      openScreen<TransitionTestActivity>();
      return;
    case Row::GrayViewer:
      openScreen<GrayViewerActivity>();
      return;
    case Row::Tuning:
      openScreen<TuningActivity>();
      return;
    case Row::SavePreset:
      savePreset();
      requestUpdate();
      return;
    case Row::TransitionTiming:
      openScreen<TransitionTimingActivity>();
      return;
    case Row::GrayRamp:
      openScreen<GrayRampActivity>();
      return;
    case Row::Exit:
      finish();
      return;
  }
}

void DisplayLabActivity::onBackButton() {
  app.clearTapFlash();
  finish();  // onExit cleans the panel and frees the backend and the masks
}

DisplayLabActivity::Scan DisplayLabActivity::scan(const ScanRequest& request, uint32_t& busyUs) {
  busyUs = 0;
  // openSession gives the session its backend and both masks, or none of them.
  if (!scanBackend || !maskPlanes.valid()) {
    LOG_ERR("DLAB", "scan refused: no mask planes");  // the lab shows its notice row instead of the experiments
    return Scan::Refused;
  }
  // Checked per scan, not only in onEnter: the control-center panel can invert the display while the lab is open, and
  // the facade then drops the backend's calls without a word.
  if (display.isInverted()) {
    LOG_ERR("DLAB", "scan refused: display inverted");
    return Scan::Refused;
  }
  // Before the lock and a due cap clean, so a bad request sends nothing to the panel.
  if (request.rate >= scanBackend->rateCount()) {
    LOG_ERR("DLAB", "scan refused: rate index %u of %u", static_cast<unsigned>(request.rate),
            static_cast<unsigned>(scanBackend->rateCount()));
    return Scan::Refused;
  }
  // The render task must not push the framebuffer between the backend's plane writes and its activation.
  RenderLock lock(*this);
  if (budget.cleanDue()) {
    LOG_INF("DLAB", "session cap: %lu scans since the last clean", static_cast<unsigned long>(budget.sinceClean()));
    cleanPanel();
  }
  const Scan sent = scanBackend->scan(request, busyUs);
  // Anything but Done is the backend's refusal (logged there): nothing reached the panel, so nothing is counted.
  if (sent != Scan::Done) return sent;
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
