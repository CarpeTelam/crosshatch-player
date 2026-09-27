#if FREEINK_CAP_GAMES

#include "GameMatchActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
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

  GameAssets assets;
  if (const char* problem = assets.load(manifest.id)) {
    showError(problem);
    return;
  }
  auto created = GameVM::create(std::move(assets));
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
    GameVM::abandon(std::move(vm));
    return;
  }
  vm.reset();
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
      GameVM::abandon(std::move(vm));
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

  // Canvas taps bypass the FreeInkUI interaction table (AD-20).
  const auto snap = touchSnapshotFrom(mappedInput);
  int16_t x = 0;
  int16_t y = 0;
  if (snap.touchReleased && snap.touchX >= 0 && viewport.toCanvas(snap.touchX, snap.touchY, x, y)) {
    vm->postTap(x, y);
  }

  const uint32_t frame = vm->frameGen();
  if (frame != shownFrame) {
    shownFrame = frame;
    requestUpdate();
  }
}

void GameMatchActivity::render(RenderLock&&) {
  if (state == State::Error) {
    renderError();
    return;
  }
  // Lock order: RenderLock (held), then the frame mutex inside drawFront; the
  // refresh runs after the mutex is released so the VM can publish during it.
  if (!vm || !vm->drawFront(renderer, viewport, replay)) return;
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
