#include "GrayViewerActivity.h"

#if CROSSHATCH_DISPLAY_LAB

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <TransitionScene.h>  // shadeName, the levels' names (D15)

#include <array>
#include <cstdio>
#include <cstring>
#include <utility>

#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

using GameDisplayLab::Dither;
using GameDisplayLab::GrayModel;
using GameDisplayLab::GrayPreset;
using GameDisplayLab::ImageKind;
using GameDisplayLab::PanelTurn;
using GameDisplayLab::Shade;
using GameDisplayLab::SHADE_COUNT;

namespace {

// GfxRenderer's four orientations and the panel turns name the same rotations in the same order (the transition test's
// asserts pin each pair by name).
PanelTurn turnOf(const GfxRenderer::Orientation orientation) { return static_cast<PanelTurn>(orientation); }

const char* ditherText(const Dither dither) {
  return dither == Dither::Plain ? tr(STR_GAMES_DLAB_DITHER_PLAIN) : tr(STR_GAMES_DLAB_DITHER_ORDERED);
}

const char* ditherName(const Dither dither) { return dither == Dither::Plain ? "plain" : "ordered"; }

}  // namespace

GrayViewerActivity::GrayViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, DisplayLabActivity& lab)
    : UiListActivity(NAME, renderer, mappedInput),
      lab(lab),
      runner(renderer, lab, *this, "gray viewer", VIEWER_STACK_BYTES) {}

const char* GrayViewerActivity::headerTitle() const { return tr(STR_GAMES_DLAB_GRAY_VIEWER); }

void GrayViewerActivity::onEnter() {
  UiListActivity::onEnter();
  // The SD card is read before the render lock, so the render task never waits on it.
  listImages();
  // The render task may draw this screen while onEnter runs, and it reads the rows.
  RenderLock lock(*this);
  shownOnPanel = false;
  notice = nullptr;
  status = tr(STR_GAMES_DLAB_NOT_RUN);
  dither = Dither::Plain;
  requested.store(0);
  if (!allocate()) {
    release();
    notice = tr(STR_GAMES_OUT_OF_MEMORY);
  }
  buildRows();
}

void GrayViewerActivity::listImages() {
  images = GameDisplayLab::ImageList();
  imageNames.reset();
  HalFile dir = Storage.open(IMAGES_DIR);
  if (!dir || !dir.isDirectory()) {
    LOG_INF("DLAB", "gray viewer: no %s, the test chart only", IMAGES_DIR);
    return;
  }
  imageNames = makeUniqueNoThrow<GameDisplayLab::ImageName[]>(MAX_IMAGES);
  // A name longer than NAME_BYTES - 1 is read whole here, to be told apart from one that fits.
  constexpr size_t READ_BYTES = 256;
  auto name = makeUniqueNoThrow<char[]>(READ_BYTES);
  if (!imageNames || !name) {
    LOG_ERR("DLAB", "OOM: gray viewer file list");
    imageNames.reset();
    dir.close();
    return;
  }
  images = GameDisplayLab::ImageList(imageNames.get(), MAX_IMAGES);
  size_t unlisted = 0;
  for (HalFile file = dir.openNextFile(); file; file = dir.openNextFile()) {
    const bool isDir = file.isDirectory();
    name[0] = '\0';
    file.getName(name.get(), READ_BYTES);
    file.close();
    switch (images.add(name.get(), isDir)) {
      case GameDisplayLab::ImageList::Add::Listed:
      case GameDisplayLab::ImageList::Add::Ignored:
        break;
      case GameDisplayLab::ImageList::Add::TooLong:
        LOG_ERR("DLAB", "gray viewer: name over %u characters, skipped: %s", static_cast<unsigned>(NAME_BYTES - 1),
                name.get());
        break;
      case GameDisplayLab::ImageList::Add::Full:
        ++unlisted;
        break;
    }
  }
  dir.close();
  images.sort();
  LOG_INF("DLAB", "gray viewer: %u pictures in %s", static_cast<unsigned>(images.files()), IMAGES_DIR);
  if (unlisted > 0) {
    LOG_INF("DLAB", "gray viewer: %u more pictures past the first %u by name not listed",
            static_cast<unsigned>(unlisted), static_cast<unsigned>(MAX_IMAGES));
  }
}

bool GrayViewerActivity::allocate() {
  const int logicalW = renderer.getScreenWidth();
  const int logicalH = renderer.getScreenHeight();
  if (!runner.allocate(logicalW, logicalH)) return false;
  grayRow = makeUniqueNoThrow<uint8_t[]>(static_cast<size_t>(logicalW));
  shadeRow = makeUniqueNoThrow<uint8_t[]>(static_cast<size_t>(logicalW));
  if (!grayRow || !shadeRow) {
    LOG_ERR("DLAB", "OOM: two %d byte gray viewer rows", logicalW);
    return false;
  }
  const GameDisplayLab::MaskPlanes& drive = lab.masks();
  picture = GameDisplayLab::GrayPicture(GameDisplayLab::LogicalScreen{turnOf(renderer.getOrientation()), logicalW,
                                                                      logicalH, static_cast<int>(drive.width()),
                                                                      static_cast<int>(drive.height())});
  return true;
}

void GrayViewerActivity::release() {
  runner.release();
  grayRow.reset();
  shadeRow.reset();
}

void GrayViewerActivity::onExit() {
  // ActivityManager holds the render mutex here (a pop, or a Replace by Home or sleep, which also drains a covered
  // screen from the stack): take no RenderLock (12cc816), and end the task without waiting on that mutex.
  runner.endLocked();
  if (shownOnPanel.load()) {
    // The next screen's first frame cleans the panel; the lab's own exit cleans it too when it is left.
    renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
    shownOnPanel = false;
  }
  release();
  images = GameDisplayLab::ImageList();
  imageNames.reset();
  Activity::onExit();
}

void GrayViewerActivity::prepareForSleep() {
  // ActivityManager::prepareForSleep holds the render mutex, for this screen as the current one or as a covered one.
  runner.endLocked();
}

bool GrayViewerActivity::handleHomeGesture() {
  // Called from ActivityManager::loop without the render mutex, before goHome's Replace runs onExit.
  runner.stop();
  return false;
}

const char* GrayViewerActivity::entryName(const size_t index) const {
  const char* file = index == 0 ? nullptr : images.file(index - 1);
  return file ? file : tr(STR_GAMES_DLAB_CHART);
}

void GrayViewerActivity::formatImageText() {
  const size_t index = requested.load();
  snprintf(imageText, sizeof(imageText), tr(STR_GAMES_DLAB_IMAGE_OF), entryName(index),
           static_cast<unsigned>(index + 1), static_cast<unsigned>(images.entries()));
}

void GrayViewerActivity::buildRows() {
  if (notice) {
    fui::ListItem item;
    item.label = notice;
    item.actionValue = 0;
    rowItems[0] = item;
    return;
  }
  formatImageText();
  // The engine's levels by their names (D15's numbers): "0, 2, 4, 6" today, seven after dlab-g2.
  char names[3 * SHADE_COUNT + 1] = {};
  GameDisplayLab::levelNames(names, sizeof(names));
  snprintf(levelsText, sizeof(levelsText), tr(STR_GAMES_DLAB_LEVELS_FMT), static_cast<unsigned>(SHADE_COUNT), names);
  const auto set = [this](const Row row, const char* label, const char* subtitle) {
    fui::ListItem item;
    item.label = label;
    item.subtitle = subtitle;
    item.actionValue = static_cast<int16_t>(row);
    rowItems[static_cast<size_t>(row)] = item;
  };
  set(Row::Image, tr(STR_GAMES_DLAB_IMAGE), imageText);
  set(Row::Dither, tr(STR_GAMES_DLAB_DITHER), ditherText(dither));
  set(Row::Run, tr(STR_GAMES_DLAB_RUN), tr(STR_GAMES_DLAB_VIEWER_RUN_DESC));
  set(Row::Levels, tr(STR_GAMES_DLAB_LEVELS), levelsText);
  set(Row::Controls, tr(STR_GAMES_DLAB_CONTROLS), tr(STR_GAMES_DLAB_VIEWER_CONTROLS));
  set(Row::Status, tr(STR_GAMES_DLAB_STATUS), status);
}

void GrayViewerActivity::render(RenderLock&& lock) {
  // While the view is up the panel shows only what the scans drive (D4): a routed tap or a swipe requests a repaint
  // on its own, and none may run. Leaving the view clears the flag and asks again, with a FULL refresh.
  if (shownOnPanel.load()) return;
  UiListActivity::render(std::move(lock));
}

void GrayViewerActivity::buildScreen(UiScreen& screen) {
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

void GrayViewerActivity::activateIndex(const int index) {
  if (shownOnPanel.load()) {
    // A tap over the rows' area (the only taps the list routes, where touch routes while the view is up).
    app.clearTapFlash();
    nextOrPause();
    return;
  }
  if (notice) return;
  switch (static_cast<Row>(index)) {
    case Row::Image:
      stepImage();
      return;
    case Row::Dither:
      toggleDither();
      return;
    case Row::Run:
      startRun();
      return;
    case Row::Levels:
    case Row::Controls:
    case Row::Status:
      return;  // text only
  }
}

bool GrayViewerActivity::handleCustomInput() {
  if (!shownOnPanel.load()) return false;
  // As the transition test (D12): the side buttons carry the controls, Back first (the left-edge swipe is Back, and the
  // swipe guard below would swallow it); NavPrevious stops on its release, as Back does.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::NavPrevious)) {
    runner.stop();
    leaveView(endText());
    return true;
  }
  // Any other swipe would scroll the hidden list: consumed while the view is up.
  if (mappedInput.wasSwipe() != MappedInputManager::SwipeDir::None) return true;
  if (mappedInput.wasPressed(MappedInputManager::Button::NavNext) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    nextOrPause();
    return true;
  }
  return false;
}

void GrayViewerActivity::navigateButtons() {
  if (shownOnPanel.load()) return;
  UiListActivity::navigateButtons();
}

void GrayViewerActivity::onBackButton() {
  app.clearTapFlash();
  finish();  // pops to the lab menu; leaving the lab cleans the panel
}

// ---- loop task ------------------------------------------------------------------------------------------------------

void GrayViewerActivity::loop() {
  // While the view is up it ends when another screen covered it, or when the task ended on its own (a failed scan or
  // read, the cover) or was ended by prepareForSleep; the rows come back with their FULL refresh.
  if (shownOnPanel.load() && runner.viewEnds(micros())) {
    leaveView(endText());
    // This pass's input belonged to the view: a Back or Confirm release here must not pop the screen or start a run.
    runner.heartbeat(micros());
    return;
  }
  UiListActivity::loop();
  runner.heartbeat(micros());
}

void GrayViewerActivity::startRun() {
  if (runner.running()) return;
  app.clearTapFlash();
  runner.prepare();
  paintError = ImageRead::Done;
  {
    RenderLock lock(*this);
    shownOnPanel = true;  // from here the render task draws nothing; no task is alive yet
  }
  LOG_INF("DLAB", "gray viewer: run, %s, dither %s", entryName(requested.load()), ditherName(dither));
  if (runner.launch()) return;
  {
    RenderLock lock(*this);
    shownOnPanel = false;  // nothing was sent: the rows stay as they are, with the reason
    status = tr(STR_GAMES_DLAB_NO_TASK);
    buildRows();
  }
  requestUpdate();
}

void GrayViewerActivity::nextOrPause() {
  if (!runner.running()) return;
  // A next picture asked and not yet painted (its clean and decode take seconds): further presses are ignored, so a
  // second press can neither skip a picture nor pause the one being painted.
  if (runner.replayPending()) return;
  if (runner.heldForPhoto()) {
    // Held: the next entry, after the last the chart. The task reads `requested` only when it serves a replay, and the
    // run's first one was served before this hold, so the entry is written first and the replay asked after; the
    // replay is asked in one step on the request word only while the task still holds.
    const uint16_t current = requested.load();
    const auto next = static_cast<uint16_t>(GameDisplayLab::ImageList::nextEntry(current, images.entries()));
    requested.store(next);
    if (runner.replayFromHold()) {
      LOG_INF("DLAB", "gray viewer: next, %s", entryName(next));
      return;
    }
    requested.store(current);  // the hold ended meanwhile (a resume): no replay was asked
  }
  runner.togglePause();
}

void GrayViewerActivity::stepImage() {
  app.clearTapFlash();
  {
    RenderLock lock(*this);
    requested.store(static_cast<uint16_t>(GameDisplayLab::ImageList::nextEntry(requested.load(), images.entries())));
    formatImageText();
  }
  LOG_INF("DLAB", "gray viewer image: %s", entryName(requested.load()));
  requestUpdate();
}

void GrayViewerActivity::toggleDither() {
  app.clearTapFlash();
  {
    RenderLock lock(*this);
    dither = dither == Dither::Plain ? Dither::Ordered : Dither::Plain;
    rowItems[static_cast<size_t>(Row::Dither)].subtitle = ditherText(dither);
  }
  LOG_INF("DLAB", "gray viewer dither: %s", ditherName(dither));
  requestUpdate();
}

const char* GrayViewerActivity::endText() const {
  switch (runner.end()) {
    case GrayRunner::End::Stopped:
      return tr(STR_GAMES_DLAB_STOPPED);
    case GrayRunner::End::Covered:
      return tr(STR_GAMES_DLAB_COVERED);
    case GrayRunner::End::ScanFailed:
      return DisplayLabActivity::outcomeText(runner.failedScan());
    case GrayRunner::End::Finished:
      return tr(STR_GAMES_DLAB_FINISHED);  // never: a picture holds until stopped
    case GrayRunner::End::PaintFailed:
      break;
  }
  switch (paintError) {
    case ImageRead::Open:
      return tr(STR_GAMES_DLAB_IMAGE_OPEN_FAILED);
    case ImageRead::Format:
      return tr(STR_GAMES_DLAB_IMAGE_FORMAT);
    case ImageRead::Memory:
      return tr(STR_GAMES_DLAB_IMAGE_NO_MEMORY);
    case ImageRead::Decode:
      return tr(STR_GAMES_DLAB_IMAGE_DECODE_FAILED);
    case ImageRead::Done:
    case ImageRead::Stopped:
      break;
  }
  return tr(STR_GAMES_DLAB_STOPPED);
}

void GrayViewerActivity::leaveView(const char* outcome) {
  app.clearTapFlash();
  {
    RenderLock lock(*this);
    status = outcome;
    // Next while held may have moved on: the Image row shows the entry the run ended on.
    formatImageText();
    rowItems[static_cast<size_t>(Row::Image)].subtitle = imageText;
    rowItems[static_cast<size_t>(Row::Status)].subtitle = status;
    // The rows come back over the drive with a FULL refresh, which cleans the panel; promoted before the flag
    // clears, so no repaint can slip in without it.
    renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
    shownOnPanel = false;
  }
  LOG_INF("DLAB", "gray viewer stopped: %s", outcome);
  requestUpdate();
}

// ---- sweep task: the runner's client --------------------------------------------------------------------------------

void GrayViewerActivity::RowSink::row(const int y, const int x0, const uint8_t* gray, const int count) {
  viewer.picture.paintGrayRow(model, preset, y, x0, gray, count, viewer.dither, viewer.shadeRow.get());
}

bool GrayViewerActivity::paintEntry(GrayModel& model, const GrayPreset& preset) {
  const size_t index = painted;
  const char* name = entryName(index);
  if (index == 0 || images.file(index - 1) == nullptr) {
    picture.paintChart(model, preset, dither, grayRow.get(), shadeRow.get());
    LOG_INF("DLAB", "gray viewer: %s, dither %s, %u pixels to drive", name, ditherName(dither),
            static_cast<unsigned>(model.activeCount()));
    return true;
  }
  char path[sizeof(IMAGES_DIR) + NAME_BYTES + 1];
  snprintf(path, sizeof(path), "%s/%s", IMAGES_DIR, name);
  RowSink sink(*this, model, preset);
  ImageReadInfo info;
  const ImageRead read = readGrayImage(path, GameDisplayLab::imageKindOf(name), picture.screen().logicalW,
                                       picture.screen().logicalH, sink, info);
  if (read != ImageRead::Done) {
    paintError = read;
    LOG_ERR("DLAB", "gray viewer: %s: %s", name, imageReadName(read));
    return false;
  }
  LOG_INF("DLAB",
          "gray viewer: %s %dx%d at %d,%d %dx%d (jpeg scale 1/%d%s), decoded in %lu ms, dither %s, %u pixels "
          "to drive",
          name, info.width, info.height, info.fit.x, info.fit.y, info.fit.w, info.fit.h, info.jpegScale,
          info.progressive ? ", progressive" : "", static_cast<unsigned long>(info.ms), ditherName(dither),
          static_cast<unsigned>(model.activeCount()));
  return true;
}

bool GrayViewerActivity::onReplay(GrayModel& model, const GrayPreset& preset) {
  // The entry the loop asked for (Run's, or Next's from a hold), kept for the repaints and the logs.
  painted = requested.load();
  picture.restart();
  return paintEntry(model, preset);
}

bool GrayViewerActivity::onResume(GrayModel& model, const GrayPreset& preset) {
  (void)model;
  (void)preset;
  // A pause press that landed as the hold began (Run set again) finds a finished picture: nothing to resume, so the run
  // holds again, its hold's ten-minute bound unchanged; Next is the way on.
  if (picture.phase() == GameDisplayLab::PicturePhase::Held) {
    runner.rehold();
    LOG_INF("DLAB", "gray viewer: still held; Next paints the next picture");
  }
  return false;
}

bool GrayViewerActivity::onCapClean() {
  const bool holding = picture.phase() != GameDisplayLab::PicturePhase::Painting;
  picture.restart();
  LOG_INF("DLAB", "gray viewer: session-cap clean%s; painting %s again", holding ? ", the marker dropped" : "",
          entryName(painted));
  return true;
}

bool GrayViewerActivity::prepareSlot(GrayModel& model, const uint32_t prevMs, const uint32_t nowMs, const bool stale,
                                     const GrayPreset& preset) {
  (void)prevMs;
  (void)nowMs;
  if (!stale) return true;
  return paintEntry(model, preset);
}

GrayRunner::Client::SlotVerdict GrayViewerActivity::afterSlot(const GameDisplayLab::SlotResult& slot,
                                                              const uint32_t slotStartUs, const uint32_t periodUs,
                                                              const uint32_t elapsedMs) {
  (void)slotStartUs;
  (void)periodUs;
  (void)elapsedMs;
  GrayModel& model = runner.model();
  const GameDisplayLab::PictureStep step = picture.slotDone(model, lab.preset(), slot.driven);
  if (step.painted) {
    // The pixels each level holds, by the levels' names: "[0:N 2:N 4:N 6:N]".
    const std::array<uint32_t, SHADE_COUNT> counts = GameDisplayLab::levelCounts(model);
    char perLevel[16 * SHADE_COUNT] = {};
    int used = 0;
    for (uint8_t level = 0; level < SHADE_COUNT; ++level) {
      if (used < 0 || static_cast<size_t>(used) >= sizeof(perLevel)) break;
      used += snprintf(perLevel + used, sizeof(perLevel) - static_cast<size_t>(used), "%s%c:%lu", level > 0 ? " " : "",
                       GameDisplayLab::shadeName(static_cast<Shade>(level)), static_cast<unsigned long>(counts[level]));
    }
    LOG_INF("DLAB", "gray viewer: %s painted after %lu slots, pixels per level [%s]; marker %c", entryName(painted),
            static_cast<unsigned long>(*step.painted), perLevel, GameDisplayLab::shadeName(picture.markerShade()));
  }
  if (step.held) {
    // Held for the photo: nothing is driven until Next paints the next picture (or a stop).
    runner.holdForPhoto();
    LOG_INF("DLAB", "gray hold: marker settled after %lu slots; holding, Next paints the next picture",
            static_cast<unsigned long>(step.markerSlots));
  }
  return SlotVerdict{};
}

#endif  // CROSSHATCH_DISPLAY_LAB
