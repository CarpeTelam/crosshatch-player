#if FREEINK_CAP_GAMES

#include "GameMatchActivity.h"

#include <Arduino.h>
#include <Codec.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <span>
#include <utility>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "games/GameAssets.h"
#include "games/GameVM.h"

GameMatchActivity::GameMatchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     const GameCore::Manifest& manifest)
    : Activity("GameMatch", renderer, mappedInput), UiAppHost(renderer), manifest(manifest) {}

// Out of line so unique_ptr<GameVM> sees the complete type.
GameMatchActivity::~GameMatchActivity() = default;

void GameMatchActivity::onEnter() {
  Activity::onEnter();
  resetUi();
  viewport = GameViewport::forRenderer(renderer);

  // The slot, then saves' buffer; GameAssets restores store.bin into the slot.
  constexpr size_t slotBytes = GameScript::Codec::STORE_LIMIT;
  storeStorage = HalMemory::allocatePsram(slotBytes + GameSaveStore::BUFFER_BYTES);
  if (storeStorage) {
    store = makeUniqueNoThrow<GameScript::StoreSlot>(storeStorage.get(), slotBytes);
    saves = makeUniqueNoThrow<GameSaveStore>(
        manifest.id, std::span<uint8_t>(storeStorage.get() + slotBytes, GameSaveStore::BUFFER_BYTES), millis());
  }
  if (!store || !saves) {
    LOG_ERR("GAME", "OOM: ch.store slot");
    showError("out of memory");
    return;
  }
  GameAssets assets;
  if (const char* problem = assets.load(manifest.id, *saves, *store)) {
    showError(problem);
    return;
  }
  replay.loadFonts(renderer);
  const GameScript::Canvas canvas{static_cast<int16_t>(viewport.width()), static_cast<int16_t>(viewport.height()),
                                  replay.textMetrics()};
  auto created = GameVM::create(std::move(assets), canvas, manifest.id, *store);
  if (!created) {
    showError("out of memory");
    return;
  }
  if (!created->start()) {
    showError("cannot start the game task");
    return;
  }
  {
    // render() reads vm on the render task.
    RenderLock lock(*this);
    vm = std::move(created);
  }
  state = State::Playing;
  LOG_INF("GAME", "Started %s", manifest.id);
  // No requestUpdate: the previous screen stays until the first frame arrives.
}

void GameMatchActivity::onExit() {
  Activity::onExit();
  // ActivityManager holds RenderLock here; the VM never takes it, so waiting cannot
  // deadlock, and render cannot be reading the frames abandon frees.
  if (vm && !vm->stop(STOP_TIMEOUT_MS)) {
    LOG_ERR("GAME", "VM did not stop within %u ms of cancel; abandoning it", static_cast<unsigned>(STOP_TIMEOUT_MS));
    abandonVm();
    return;
  }
  vm.reset();
}

void GameMatchActivity::abandonVm() {
  if (GameVM::abandon(std::move(vm))) return;
  // The leaked task may still call ch.store.set; its slot must outlive it.
  static_cast<void>(store.release());
  static_cast<void>(storeStorage.release());
  LOG_ERR("GAME", "Leaked the ch.store slot with the VM");
}

void GameMatchActivity::stopStuckVm() {
  LOG_ERR("GAME", "%s: a call ran over %u ms; stopping the VM", manifest.id, static_cast<unsigned>(WATCHDOG_MS));
  char detail[GameVM::ERROR_CAPACITY];
  snprintf(detail, sizeof(detail), "stopped responding: one call ran over %u s",
           static_cast<unsigned>(WATCHDOG_MS / 1000));
  {
    // render() reads vm and the frames abandon may free.
    RenderLock lock(*this);
    if (vm->stop(STOP_TIMEOUT_MS)) {
      // It may have ended on its own error meanwhile; that message says more.
      if (vm->failed()) snprintf(detail, sizeof(detail), "%s", vm->errorMessage());
      vm.reset();
    } else {
      LOG_ERR("GAME", "VM did not stop within %u ms of cancel; abandoning it", static_cast<unsigned>(STOP_TIMEOUT_MS));
      abandonVm();
    }
  }
  showError(detail);
}

void GameMatchActivity::showError(const char* detail) {
  snprintf(errorDetail, sizeof(errorDetail), "%s", detail);
  LOG_ERR("GAME", "%s stopped: %s", manifest.id, errorDetail);
  state = State::Error;
  requestUpdate();
}

void GameMatchActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToGames();
    return;
  }
  if (state != State::Playing) return;
  if (vm->failed()) {
    showError(vm->errorMessage());
    return;
  }
  // A C loop runs no Lua instructions, so neither the budget nor the cancel flag
  // can end it; the wall clock can (AD-5).
  if (vm->runningForMs(millis()) > WATCHDOG_MS) {
    stopStuckVm();
    return;
  }

  // Edge gestures never get here as game input: Back is Button::Back above,
  // ActivityManager takes Home and the light panel first, and GameTouch drops
  // every edge swipe that is left.
  GameScript::InputEvent event;
  if (GameTouch::toEvent(readGesture(), renderer.getScreenWidth(), renderer.getScreenHeight(), viewport, event)) {
    vm->postInput(event);
  }
  vm->pollTimer();
  saves->flushIfDue(*store, millis());

  const uint32_t frame = vm->frameGen();
  if (frame != shownFrame && frame != renderedFrame.load(std::memory_order_acquire)) {
    shownFrame = frame;
    requestUpdate();
  }
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
  if (state == State::Error) {
    renderError();
    return;
  }
  if (!vm) return;
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

void GameMatchActivity::renderError() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, manifest.name);
  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing * 2;
  const int left = metrics.contentSidePadding;
  renderer.drawText(UI_12_FONT_ID, left, top, tr(STR_GAMES_ERROR));
  renderer.drawText(SMALL_FONT_ID, left, top + renderer.getLineHeight(UI_12_FONT_ID) + metrics.verticalSpacing,
                    errorDetail);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
}

#endif  // FREEINK_CAP_GAMES
