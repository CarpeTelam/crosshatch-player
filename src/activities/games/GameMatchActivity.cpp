#if FREEINK_CAP_GAMES

#include "GameMatchActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <utility>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "games/GameAssets.h"
#include "games/GameIconDraw.h"
#include "games/GameVM.h"
#include "games/GameViewIcons.h"

namespace fui = freeink::ui;

namespace {

using GameCore::MatchEvent;
using GameCore::MatchLifecycle;
using GameCore::MatchState;

// Why a load failed, as the error view says it (AD-14).
StrId loadFailureReason(const GameAssets::LoadResult result) {
  switch (result) {
    case GameAssets::LoadResult::FolderMissing:
      return StrId::STR_GAMES_FOLDER_MISSING;
    case GameAssets::LoadResult::NoSources:
      return StrId::STR_GAMES_NO_SOURCES;
    case GameAssets::LoadResult::BadSourceName:
      return StrId::STR_GAMES_BAD_SOURCE_NAME;
    case GameAssets::LoadResult::TooLarge:
      return StrId::STR_GAMES_SOURCES_TOO_LARGE;
    case GameAssets::LoadResult::OutOfMemory:
      return StrId::STR_GAMES_OUT_OF_MEMORY;
    case GameAssets::LoadResult::CannotRead:
      return StrId::STR_GAMES_CANNOT_READ;
    case GameAssets::LoadResult::BadImage:
      return StrId::STR_GAMES_BAD_IMAGE;
    case GameAssets::LoadResult::Ok:
      break;  // not a failure; never shown
  }
  return StrId::STR_GAMES_START_FAILED;
}

// The error view's detail for a failed VM: tr() text for the host's own failures,
// Lua's message for the script's (AD-14).
const char* vmFailureText(const GameVM& vm) {
  GameVM::HostFailureTexts texts;
  texts.outOfMemory = tr(STR_GAMES_OUT_OF_MEMORY);
  texts.notLoaded = tr(STR_GAMES_NOT_LOADED);
  return vm.failureDetail(texts);
}

// A menu choice's label.
StrId optionLabel(const MatchEvent event) {
  switch (event) {
    case MatchEvent::Resume:
      return StrId::STR_GAMES_RESUME;
    case MatchEvent::Leave:
      return StrId::STR_GAMES_LEAVE;
    case MatchEvent::PlayAgain:
      return StrId::STR_GAMES_PLAY_AGAIN;
    case MatchEvent::Back:
      return StrId::STR_BACK;
    case MatchEvent::Started:
    case MatchEvent::Home:
    case MatchEvent::RoundOver:
    case MatchEvent::ScriptError:
    case MatchEvent::ForcedExit:
      break;  // never a menu choice (MatchLifecycle::menuFor)
  }
  return StrId::STR_BACK;
}

}  // namespace

GameMatchActivity::GameMatchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     const GameCore::Manifest& manifest)
    : Activity("GameMatch", renderer, mappedInput), UiAppHost(renderer), manifest(manifest) {}

// Out of line so unique_ptr<GameVM> sees the complete type.
GameMatchActivity::~GameMatchActivity() {
  if (!slotLeaked) return;
  // A leaked VM task may still call ch.store.set; its slot must outlive it.
  store.leak();
}

void GameMatchActivity::onEnter() {
  Activity::onEnter();
  resetUi();
  app.setScreen(&GameMatchActivity::viewScreen, this);
  viewport = GameViewport::forRenderer(renderer);

  if (!store.allocate(manifest.id, millis())) {
    LOG_ERR("GAME", "OOM: ch.store slot");
    fail(StrId::STR_GAMES_START_FAILED, tr(STR_GAMES_OUT_OF_MEMORY));
    return;
  }
  GameAssets assets;
  // GameAssets restores store.bin into the slot.
  const GameAssets::LoadResult loaded = assets.load(manifest.id, store.saves(), store.slot());
  if (loaded != GameAssets::LoadResult::Ok) {
    fail(StrId::STR_GAMES_START_FAILED, I18N.get(loadFailureReason(loaded)));
    return;
  }
  replay.loadFonts(renderer);
  auto created = GameVM::create(std::move(assets), viewport, replay, manifest.id, store.slot());
  // Both failures are logged with their cause; each is memory (PSRAM, or the task's stack).
  if (!created || !created->start()) {
    fail(StrId::STR_GAMES_START_FAILED, tr(STR_GAMES_OUT_OF_MEMORY));
    return;
  }
  {
    // render() reads vm on the render task.
    RenderLock lock(*this);
    vm = std::move(created);
  }
  LOG_INF("GAME", "Started %s", manifest.id);
  handle(MatchEvent::Started);
}

void GameMatchActivity::onExit() {
  Activity::onExit();
  // ActivityManager holds RenderLock here: never take it again (12cc816). The VM
  // never takes it, so waiting for it cannot deadlock, and render cannot be reading
  // the frames an abandon frees. After a user exit the match is Leaving already
  // and the VM is gone; the store is flushed again only if a set landed since.
  handle(MatchEvent::ForcedExit);
  stopVm();
  flushStore();
}

bool GameMatchActivity::handleHomeGesture() {
  handle(MatchEvent::Home);
  // A match that has already let go (Leaving, e.g. when goToGames() ran out of
  // memory) lets Home go Home as any screen does.
  return lifecycle.state() != MatchState::Leaving;
}

void GameMatchActivity::handle(const MatchEvent event) {
  const MatchState from = lifecycle.state();
  if (!lifecycle.apply(event)) {
    LOG_DBG("GAME", "%s ignored in %s", MatchLifecycle::name(event), MatchLifecycle::name(from));
    return;
  }
  const MatchState to = lifecycle.state();
  LOG_INF("GAME", "%s: %s -> %s on %s", manifest.id, MatchLifecycle::name(from), MatchLifecycle::name(to),
          MatchLifecycle::name(event));
  // The next view registers its own options; the old table must not route.
  closeRouting();
  selected.store(0);
  // Before shown: a render already queued must not see Playing with the old count.
  if (event == MatchEvent::PlayAgain) roundsStartedAwaited.store(vm->roundsStarted() + 1);
  shown.store(to);
  switch (to) {
    case MatchState::Playing:
      if (event == MatchEvent::PlayAgain) {
        // Frames the last round drew after it ended are never shown, one from a
        // step still running when Play again came included: the loop asks for no
        // render until the new round's first frame is published.
        // roundsStartedAwaited was stored above, before shown.
        shownFrame = vm->frameGen();
        vm->playAgain();
      }
      // A new round's first frame asks for its own render; a resumed one is redrawn,
      // unless it resumes into the Play-again gap, where renderCanvas keeps the view
      // on screen until the new round's first frame.
      if (event != MatchEvent::Resume && event != MatchEvent::Back) return;
      break;
    case MatchState::Over:
      flushStore();
      break;
    case MatchState::Leaving:
      // onExit() stops and flushes itself after a forced exit.
      if (event != MatchEvent::ForcedExit) leave();
      return;
    case MatchState::Starting:
    case MatchState::Paused:
    case MatchState::Error:
      break;
  }
  requestUpdate();
}

void GameMatchActivity::fail(const StrId headline, const char* detail) {
  if (!lifecycle.allows(MatchEvent::ScriptError)) return;
  errorHeadline = headline;
  snprintf(errorDetail, sizeof(errorDetail), "%s", detail);
  LOG_ERR("GAME", "%s stopped: %s", manifest.id, errorDetail);
  handle(MatchEvent::ScriptError);
}

void GameMatchActivity::leave() {
  {
    RenderLock lock(*this);
    stopVm();
  }
  flushStore();
  activityManager.goToGames();
}

void GameMatchActivity::stopVm() {
  if (!vm) return;
  if (vm->stop(STOP_TIMEOUT_MS)) {
    vm.reset();
    return;
  }
  abandonVm();
}

void GameMatchActivity::flushStore() {
  if (!store.ready()) return;  // the match never got that far
  // Safe with a leaked task too: the slot's mutex is held only for a copy inside
  // a locked binding, which an abandon never deletes nor leaves suspended, so the
  // flush waits at most one copy, and it saves every set until the leak.
  store.flush(millis());
}

void GameMatchActivity::abandonVm() {
  LOG_ERR("GAME", "VM did not stop within %u ms of cancel; abandoning it", static_cast<unsigned>(STOP_TIMEOUT_MS));
  if (GameVM::abandon(std::move(vm))) return;
  // The leaked task may still call ch.store.set; the destructor leaks the slot.
  slotLeaked = true;
  LOG_ERR("GAME", "The ch.store slot stays with the leaked VM");
}

void GameMatchActivity::stopStuckVm() {
  static_assert(WATCHDOG_MS == 3000, "STR_GAMES_NOT_RESPONDING names the limit: 3 seconds");
  LOG_ERR("GAME", "%s: a call ran over %u ms; stopping the VM", manifest.id, static_cast<unsigned>(WATCHDOG_MS));
  char detail[GameVM::ERROR_CAPACITY];
  snprintf(detail, sizeof(detail), "%s", tr(STR_GAMES_NOT_RESPONDING));
  {
    // render() reads vm and the frames abandon may free.
    RenderLock lock(*this);
    if (vm->stop(STOP_TIMEOUT_MS)) {
      // It may have ended on its own error meanwhile; that message says more.
      if (vm->failed()) snprintf(detail, sizeof(detail), "%s", vmFailureText(*vm));
      vm.reset();
    } else {
      abandonVm();
    }
  }
  fail(StrId::STR_GAMES_ERROR, detail);
}

bool GameMatchActivity::vmHealthy() {
  if (vm->failure() != GameVM::Failure::None) {
    // Every host failure (the Session or LuaGame::load not fitting, or a call before
    // the load) comes before any game code ran: the game could not start (AD-14, as
    // amended 2026-09-28). Only the script's own error says it stopped.
    fail(vm->failedToStart() ? StrId::STR_GAMES_START_FAILED : StrId::STR_GAMES_ERROR, vmFailureText(*vm));
    return false;
  }
  // A C loop runs no Lua instructions, so neither the budget nor the cancel flag
  // can end it; the wall clock can (AD-5).
  if (vm->runningForMs(millis()) > WATCHDOG_MS) {
    stopStuckVm();
    return false;
  }
  return true;
}

void GameMatchActivity::loop() {
  switch (lifecycle.state()) {
    case MatchState::Playing:
      loopPlaying();
      return;
    case MatchState::Paused:
    case MatchState::Over:
    case MatchState::Error:
      loopView();
      return;
    case MatchState::Starting:  // onEnter leaves it before the first loop
    case MatchState::Leaving:
      return;
  }
}

void GameMatchActivity::loopPlaying() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    handle(MatchEvent::Back);
    return;
  }
  if (!vmHealthy()) return;
  const uint32_t ended = vm->roundsEnded();
  if (ended != roundsSeen) {
    roundsSeen = ended;
    handle(MatchEvent::RoundOver);
    return;
  }

  // After Play again, until the new round's first frame is published, the screen
  // still shows the end-of-round menu or the last round's frame: a tap there is not
  // aimed at the new round, so it is read (which consumes the contact) and dropped.
  const bool awaitingRound = vm->roundsStarted() < roundsStartedAwaited.load();
  // Edge gestures never get here as game input: Back is Button::Back above,
  // ActivityManager takes Home (handleHomeGesture) and the light panel first, and
  // GameTouch drops every edge swipe that is left.
  const GameTouch::Gesture gesture = readGesture();
  GameCore::GameEvent event;
  if (!awaitingRound &&
      GameTouch::toEvent(gesture, renderer.getScreenWidth(), renderer.getScreenHeight(), viewport, event)) {
    vm->postInput(event);
  }
  vm->pollTimer();
  store.flushIfDue(millis());

  // Any frame before the new round's first is the last round's; once the count
  // moves, coalescing shows the newest frame.
  if (awaitingRound) return;
  const uint32_t frame = vm->frameGen();
  if (frame != shownFrame && frame != renderedFrame.load(std::memory_order_acquire)) {
    shownFrame = frame;
    requestUpdate();
  }
}

void GameMatchActivity::loopView() {
  const MatchState state = lifecycle.state();
  // Paused and Over keep the VM, which may still fail, hang, or set ch.store in a
  // call that was running when the view opened; neither posts input or timers.
  if (state != MatchState::Error) {
    if (!vmHealthy()) return;
    store.flushIfDue(millis());
  }
  // Back resumes from the pause menu, leaves from the error view, and does
  // nothing in the end-of-round menu (MatchLifecycle).
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    handle(MatchEvent::Back);
    return;
  }
  const GameCore::MatchMenu menu = MatchLifecycle::menuFor(state);
  const auto route = routeTouch(mappedInput);
  if (route && route.event.action == ACTION_OPTION) {
    choose(menu, route.event.value);
    return;
  }
  if (route.routed && app.invalidated()) requestUpdate();
  // The error view's one control is Back, above.
  if (menu.count < 2) return;
  const uint8_t current = selected.load();
  if (mappedInput.wasPressed(MappedInputManager::Button::NavPrevious)) {
    selected.store(static_cast<uint8_t>((current + menu.count - 1) % menu.count));
    requestUpdate();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::NavNext)) {
    selected.store(static_cast<uint8_t>((current + 1) % menu.count));
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    choose(menu, current);
  }
}

void GameMatchActivity::choose(const GameCore::MatchMenu& menu, const int index) {
  if (index < 0 || index >= menu.count) return;
  app.clearTapFlash();  // the choice leaves this view
  handle(menu.events[index]);
}

GameTouch::Gesture GameMatchActivity::readGesture() const {
  GameTouch::Gesture gesture;
  // Canvas taps and long presses bypass the FreeInkUI interaction table (AD-20).
  // Consuming a long press suppresses the rest of the contact, so its lift is no tap.
  const auto snap = touchSnapshotFrom(mappedInput, /*withLongPress=*/true);
  if (snap.touchReleased && snap.touchX >= 0) {
    gesture.kind = snap.longPress ? GameTouch::Kind::LongPress : GameTouch::Kind::Tap;
    gesture.x = snap.touchX;
    gesture.y = snap.touchY;
    return gesture;
  }
  // A swipe needs its start point, which MappedInputManager::wasSwipe drops; the
  // HAL reports the same per-frame swipe, mapped to logical pixels the same way.
  float startX = 0.0f;
  float startY = 0.0f;
  float endX = 0.0f;
  float endY = 0.0f;
  if (gpio.wasSwipe(startX, startY, endX, endY)) {
    gesture.kind = GameTouch::Kind::Swipe;
    renderer.tapToLogical(startX, startY, gesture.x, gesture.y);
    renderer.tapToLogical(endX, endY, gesture.endX, gesture.endY);
  }
  return gesture;
}

void GameMatchActivity::render(RenderLock&&) {
  const MatchState state = shown.load();
  switch (state) {
    case MatchState::Playing:
      renderCanvas();
      return;
    case MatchState::Paused:
    case MatchState::Over:
    case MatchState::Error:
      renderView(state);
      return;
    case MatchState::Starting:  // the previous screen stays until the first frame
    case MatchState::Leaving:
      return;
  }
}

void GameMatchActivity::renderCanvas() {
  if (!vm) return;
  // In the Play-again gap every frame is the last round's, and the loop drops every
  // tap: the view (or an overlay's pixels) stays on screen, and the new round's
  // first frame, which is no repaint, is drawn on a cleared screen. Nothing else
  // here runs, so the skip changes no replay state.
  if (vm->roundsStarted() < roundsStartedAwaited.load()) {
    viewOnScreen = true;
    return;
  }
  if (viewOnScreen) {
    // A view covered the canvas; a game that never clears would keep its pixels.
    renderer.clearScreen();
    replay.forceFull();
    viewOnScreen = false;
  }
  // The match asks for a render only for a frame no render has taken yet, so a
  // render without one is a repaint after something else drew (an overlay such
  // as the light panel closed): the screen no longer shows the frame.
  const uint32_t frame = vm->frameGen();
  if (frame == renderedFrame.load(std::memory_order_relaxed)) replay.forceFull();
  renderedFrame.store(frame, std::memory_order_release);
  // Lock order: RenderLock (held), then the frame mutex inside drawFront; the
  // refresh runs after the mutex is released so the VM can publish during it.
  if (!vm->drawFront(renderer, viewport, replay)) return;
  renderer.displayBuffer(replay.refreshMode());
}

void GameMatchActivity::renderView(const MatchState state) {
  renderer.clearScreen();
  // The menus sit over the last frame; the error view stands alone.
  if (state != MatchState::Error && vm) {
    replay.forceFull();
    renderedFrame.store(vm->frameGen(), std::memory_order_release);
    vm->drawFront(renderer, viewport, replay);
  }
  // buildView draws this state's dialog, even if the loop moves on meanwhile.
  viewState = state;
  renderUi();
  // handle() may have closed routing after this render read its state; the
  // table just published is the old view's and must not route taps.
  if (shown.load() != state) closeRouting();
  viewOnScreen = true;
  const bool menu = state != MatchState::Error;
  const char* back = "";  // Back does nothing in the end-of-round menu
  if (state == MatchState::Paused) back = tr(STR_GAMES_RESUME);
  if (state == MatchState::Error) back = tr(STR_BACK);
  const auto labels =
      mappedInput.mapLabels(back, menu ? tr(STR_SELECT) : "", menu ? tr(STR_DIR_UP) : "", menu ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(menu ? HalDisplay::FAST_REFRESH : HalDisplay::FULL_REFRESH);
}

void GameMatchActivity::viewScreen(UiScreen& screen, void* user) {
  static_cast<GameMatchActivity*>(user)->buildView(screen);
}

const char* GameMatchActivity::viewHeadline(const MatchState state) const {
  switch (state) {
    case MatchState::Paused:
      return tr(STR_GAMES_PAUSED);
    case MatchState::Over:
      return tr(STR_GAMES_OVER);
    case MatchState::Error:
      return I18N.get(errorHeadline);
    case MatchState::Starting:
    case MatchState::Playing:
    case MatchState::Leaving:
      break;  // no view
  }
  return nullptr;
}

void GameMatchActivity::buildView(UiScreen& screen) {
  const MatchState state = viewState;
  const GameCore::MatchMenu menu = MatchLifecycle::menuFor(state);
  const char* headline = viewHeadline(state);
  if (menu.count == 0 || !headline) return;
  const uint8_t count = menu.count < MAX_OPTIONS ? menu.count : MAX_OPTIONS;
  const uint8_t focused = selected.load();
  fui::DialogOption options[MAX_OPTIONS];
  for (uint8_t i = 0; i < count; ++i) {
    options[i].label = I18N.get(optionLabel(menu.events[i]));
    options[i].action = ACTION_OPTION;
    options[i].value = static_cast<int16_t>(i);
    options[i].state = i == focused ? fui::StateFocused : fui::StateNormal;
  }

  const auto& theme = screen.theme();
  fui::OptionDialogProps& props = dialogProps;
  props.title = manifest.name;
  props.titleText = theme.smallText;
  props.titleText.align = fui::TextAlign::Center;
  props.headline = headline;
  props.headlineText = theme.titleText;
  props.headlineText.align = fui::TextAlign::Center;
  props.headlineText.maxLines = 2;
  // The error view adds Lua's message in small type, wrapped (AD-14); its
  // ERROR_CAPACITY bytes fit in 8 lines.
  props.message = state == MatchState::Error ? errorDetail : nullptr;
  props.messageText = theme.smallText;
  props.messageText.maxLines = 8;
  props.buttonText = theme.bodyText;
  props.buttonStyles = theme.button;
  props.options = options;
  props.optionCount = count;
  props.verticalOptions = true;
  // The view's library icon sits in the content band, between the text and the rows.
  props.contentHeight = GameViewIcons::forView(state) ? static_cast<int16_t>(GameViewIcons::VIEW_PIXELS) : 0;
  // Touch only: the buttons are read in loopView().
  props.inputMask = fui::InputTouch;
  // A framed panel, as OptionPopup draws it, so it stands out over the game.
  const auto& metrics = UITheme::getInstance().getMetrics();
  fui::BoxStyle& panel = props.styles.normal;
  panel.background = fui::Paint::solid(fui::Color::White);
  panel.foreground = fui::Paint::solid(fui::Color::Black);
  panel.border = fui::Paint::solid(fui::Color::Black);
  panel.borderWidth = static_cast<uint8_t>(metrics.popupFrameThickness);
  panel.radius = static_cast<uint8_t>(metrics.popupCornerRadius);
  props.styles.selected = panel;
  props.styles.focused = panel;
  props.styles.active = panel;
  props.styles.disabled = panel;
  props.styles.explicitlySet = true;

  const fui::Rect safe = screen.frame().safeRect();
  const auto width = static_cast<int16_t>(safe.width * 4 / 5);
  const int16_t height = fui::optionDialogHeight(screen.target(), props, width);
  const fui::Rect band = fui::optionDialog(screen.frame(), fui::centeredRect(safe, fui::Size{width, height}), props);
  drawViewIcons(screen, band, state, menu, count);
}

void GameMatchActivity::drawViewIcons(UiScreen& screen, const fui::Rect band, const MatchState state,
                                      const GameCore::MatchMenu& menu, const uint8_t count) const {
  // The dialog has no icon field, so the icons are drawn over the finished dialog.
  if (band.empty()) return;
  const fui::OptionDialogProps& props = dialogProps;
  const char* viewIcon = GameViewIcons::forView(state);
  if (viewIcon) {
    drawGameIcon(renderer, viewIcon, band.x + (band.width - GameViewIcons::VIEW_PIXELS) / 2, band.y,
                 GameViewIcons::VIEW_PIXELS, true);
  }
  // optionDialog stacks the rows directly below the band (verticalOptions).
  const int inset = GameViewIcons::rowIconInset(props.buttonHeight);
  for (uint8_t i = 0; i < count; ++i) {
    const char* rowIcon = GameViewIcons::forOption(menu.events[i]);
    const char* label = props.options[i].label;
    if (!rowIcon || !label) continue;
    const int labelWidth = screen.target().measureText(props.buttonText.font, label, props.buttonText).width;
    if (!GameViewIcons::rowIconFits(band.width, props.buttonHeight, labelWidth, props.gap)) continue;
    const int rowY = GameViewIcons::rowTop(band.bottom(), i, props.buttonHeight, props.gap);
    const fui::State rowState = screen.frame().stateFor(ACTION_OPTION, static_cast<int16_t>(i), props.options[i].state);
    const bool black = props.buttonStyles.resolve(rowState).foreground.color != fui::Color::White;
    drawGameIcon(renderer, rowIcon, band.x + inset, rowY + inset, GameViewIcons::ROW_PIXELS, black);
  }
}

#endif  // FREEINK_CAP_GAMES
