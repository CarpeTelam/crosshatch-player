#pragma once

#include <I18n.h>
#include <Manifest.h>
#include <MatchLifecycle.h>

#include <atomic>
#include <cstdint>
#include <memory>

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "games/FrameReplay.h"
#include "games/GameTouch.h"
#include "games/GameVM.h"
#include "games/GameViewport.h"
#include "games/MatchStore.h"

// One solo match (AD-20): owns the GameVM task and, through it, the game's assets,
// arena, and frame buffers, and owns ch.store (MatchStore): its slot, which
// outlives the VM, and the GameSaveStore that restores it and writes it to
// store.bin. It reaches lib/GameScript only through src/games (the spine's layer
// table; scripts/check_layers.py). The canvas is
// drawn by FrameReplay and fed by taps, long presses, and swipes mapped through
// GameViewport (GameTouch.h) and by due ch.timer timers; the UiAppHost draws the
// runtime's own views: the pause menu, the end-of-round menu, and the error view
// (docs/crosshatch/game-canvas.md).
//
// The match follows AD-21's solo states (GameCore::MatchLifecycle), changed only by
// handle(). The VM exists from Playing until Leaving, or until a stuck VM is
// stopped on the way to Error, so Playing, Paused, and Over always have one.
class GameMatchActivity final : public Activity, private UiAppHost {
 public:
  GameMatchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const GameCore::Manifest& manifest);
  ~GameMatchActivity() override;

  void onEnter() override;
  // The forced exit (AD-20) when the match did not leave on its own (sleep, any
  // Replace): stops the VM and flushes ch.store under the RenderLock
  // ActivityManager holds, which it never takes again (12cc816).
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  // True while a callback runs, so the CPU stays at full clock for the budget (AD-5).
  bool skipLoopDelay() override { return vm && vm->busy(); }
  // Home never leaves a match directly (AD-21): it pauses a round in play and is
  // ignored otherwise, until the match is Leaving.
  bool handleHomeGesture() override;

 private:
  using MatchState = GameCore::MatchState;
  using MatchEvent = GameCore::MatchEvent;
  // How long a stop waits after cancel for the VM to end before abandoning it (AD-5).
  static constexpr uint32_t STOP_TIMEOUT_MS = 500;
  // A call into Lua still running after this long is a stuck script (AD-5).
  static constexpr uint32_t WATCHDOG_MS = 3000;
  static constexpr freeink::ui::ActionId ACTION_OPTION = 1;
  // The most options a view offers (the pause and end-of-round menus).
  static constexpr uint8_t MAX_OPTIONS = 2;

  // Takes the transition `event` names and does what entering the new state
  // requires: Over flushes ch.store, a user's Leaving stops the VM, flushes, and
  // goes to Games. An event that is no transition from this state is ignored.
  void handle(MatchEvent event);
  // Shows the error view (a ScriptError, AD-14) with a translated headline and a
  // detail: Lua's message or a translated reason. The first error view stays.
  void fail(StrId headline, const char* detail);

  void loopPlaying();
  // The pause menu, the end-of-round menu, and the error view.
  void loopView();
  // Playing, Paused, Over: false (now in Error) when the VM failed or is stuck.
  bool vmHealthy();
  void choose(const GameCore::MatchMenu& menu, int index);
  // A user exit (Leave, or Back from the error view), from loop().
  void leave();
  // Cancels the VM, joins it or abandons it (AD-5). The caller holds RenderLock,
  // since render reads vm and the frames an abandon frees.
  void stopVm();
  // Writes a dirty ch.store now (round end, Leave, onExit; AD-17).
  void flushStore();
  // Cancels a VM past WATCHDOG_MS, abandons it if it does not join, and shows the error view.
  void stopStuckVm();
  // For a VM that did not join: abandons it (GameVM::abandon); a task that may
  // still run keeps the store slot.
  void abandonVm();

  void renderCanvas();
  void renderView(MatchState state);
  static void viewScreen(UiScreen& screen, void* user);
  void buildView(UiScreen& screen);
  // The view's translated headline; null for a state with no view.
  const char* viewHeadline(MatchState state) const;
  // This loop pass's touch gesture on the logical screen, if any.
  GameTouch::Gesture readGesture() const;

  GameCore::Manifest manifest;
  GameViewport viewport;
  FrameReplay replay;
  // ch.store's slot and store.bin's reader and writer in one PSRAM block (AD-17):
  // periodic flushes in loop(), and flushStore() at round end, on Leave, and in
  // onExit(). Declared before vm, so it is destroyed after the VM that posts to
  // the slot.
  MatchStore store;
  std::unique_ptr<GameVM> vm;
  // An abandon left the VM task alive: the slot is still flushed on Leave and in
  // onExit, and the destructor leaks it (and saves' buffer) with the task.
  bool slotLeaked = false;

  GameCore::MatchLifecycle lifecycle;  // loop task
  // lifecycle's state for render, stored by handle() after each transition.
  std::atomic<MatchState> shown{MatchState::Starting};
  // The focused option of the view on screen (buttons move it; loop task writes).
  std::atomic<uint8_t> selected{0};
  // GameVM::roundsEnded() when the match last entered Over (loop task).
  uint32_t roundsSeen = 0;
  uint32_t shownFrame = 0;  // loop task: the frameGen it last asked to render
  // Written by render: the frameGen its last render saw. The loop does not ask
  // again for a frame a render already took, so a render that sees no new frame
  // is always a repaint someone else asked for.
  std::atomic<uint32_t> renderedFrame{0};
  // Render task only: a view was drawn over the canvas, so the next frame is
  // drawn on a cleared screen with a full refresh.
  bool viewOnScreen = false;
  // Render task only: the state whose view renderView is drawing.
  MatchState viewState = MatchState::Starting;
  // Render task only: the view's dialog, a member since it is over 1 KB.
  freeink::ui::OptionDialogProps dialogProps;
  // The error view's text, written once before the match enters Error.
  StrId errorHeadline = StrId::STR_GAMES_ERROR;
  char errorDetail[GameVM::ERROR_CAPACITY] = {};
};
