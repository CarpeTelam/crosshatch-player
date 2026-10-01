#pragma once

#include <I18n.h>
#include <Manifest.h>
#include <MatchLifecycle.h>
#include <Roster.h>

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

// One match, solo or an open pass match (AD-20): owns the GameVM task and, through it, the game's assets,
// arena, and frame buffers, and owns ch.store (MatchStore): its slot, which
// outlives the VM, and the GameSaveStore that restores it and writes it to
// store.bin. It reaches lib/GameScript only through src/games (the spine's layer
// table; scripts/check_layers.py). The canvas is
// drawn by FrameReplay and fed by taps, long presses, and swipes mapped through
// GameViewport (GameTouch.h) and by due ch.timer timers; the UiAppHost draws the
// runtime's own views: the pause menu, the end-of-round menu, and the error view
// (docs/crosshatch/game-canvas.md).
//
// A pass match plays the turn seat's frame and input on this one device, and seat 0's frame (everyone's) once the
// round is over (GameCore::seatShown); it keeps no resume.bin until pass saves (epic-pass-and-play entry 9).
//
// A solo match with a .pkg also saves its latest snapshot as resume.bin (GameSaveStore):
// the VM hands each committed one to the loop through GameVM::committed(), the loop
// writes it every pass in Playing and Paused, the forced exit and Leave write the last
// pending one, and entering Over deletes the file. Start::Resume restores it.
//
// The match follows AD-21's solo states (GameCore::MatchLifecycle), changed only by
// handle(). The VM exists from Playing until Leaving, or until a stuck VM is
// stopped on the way to Error, so Playing, Paused, and Over always have one.
class GameMatchActivity final : public Activity, private UiAppHost {
 public:
  // New starts a round with setup; Resume continues from the game's resume.bin when
  // GameSaveStore can use it, starts a new match (logged) when there is no usable save, and stops in the error view,
  // the save untouched, when there is one that cannot be read or that the VM refuses.
  enum class Start : uint8_t { New, Resume };

  // The forced exit's SD steps (the resume write, the resume.bin delete retry, the
  // ch.store flush) start only within this long of the start of onExit(); a step that
  // would start later is skipped and logged. A step that starts in time is one tmp write
  // and rename, whose time is the card's. Leave has no deadline.
  static constexpr uint32_t FORCED_EXIT_DEADLINE_MS = 1500;

  // `roster` is who plays: GameCore::Roster::solo(), or Roster::pass(n) for an open pass match.
  GameMatchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const GameCore::Manifest& manifest,
                    const GameCore::Roster& roster, Start start = Start::New);
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
  // How long a stop waits after cancel for the VM to end before abandoning it (AD-5),
  // counted in millis() from when it began. Independent of GameVM::ABANDON_WAIT_MS, the
  // abandon's own wait that may follow, counted the same way; equal today by choice, not
  // by rule. The forced exit's worst case for the VM is the two in turn plus one late
  // iteration each, about 1,030 ms, plus teardown, which is not counted; then only the SD
  // steps that start before FORCED_EXIT_DEADLINE_MS (docs/crosshatch/game-canvas.md, The
  // forced exit; the epic-script-runtime retro's AI-4).
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
  // Cancels the VM, joins it or abandons it (AD-5), and writes its last pending
  // snapshot before it is freed. The caller holds RenderLock, since render reads vm
  // and the frames an abandon frees.
  void stopVm();
  // Writes a dirty ch.store now (round end, Leave, onExit; AD-17).
  void flushStore();
  // Playing and Paused: writes the VM's latest committed snapshot as resume.bin (not
  // again until FLUSH_INTERVAL_MS after a failed write, Leave and the forced exit
  // included). Nothing in Over or Error, or without a .pkg.
  void flushResume();
  // The same for `from`, which may be a VM that is no longer vm (abandonVm's hook).
  void flushResumeOf(GameVM& from);
  // Removes resume.bin again after Over's delete failed (resumeDeletePending), at most
  // every FLUSH_INTERVAL_MS from the last failed try unless `forced` (Leave, the forced exit).
  void retryResumeDelete(bool forced);
  // False, with one log line naming `what`, for an SD step of the forced exit that would
  // start past FORCED_EXIT_DEADLINE_MS; true for every step outside a forced exit.
  bool sdStepAllowed(const char* what);
  // Start::Resume, before the VM starts: seeds it with the saved snapshot. True also when there
  // is no usable save (logged: the match starts new, since nothing is lost). False when a save is
  // there and cannot be used now, because it would not read or the VM refused it: the caller
  // shows the error view and leaves the file, which a new match would replace.
  bool seedResume(GameVM& created);
  // Cancels a VM past WATCHDOG_MS, abandons it if it does not join, and shows the error view.
  void stopStuckVm();
  // For a VM that did not join: abandons it (GameVM::abandon), writing its last snapshot
  // first if it ends within the wait after all; a task that may still run keeps the
  // store slot.
  void abandonVm();

  void renderCanvas();
  void renderView(MatchState state);
  static void viewScreen(UiScreen& screen, void* user);
  void buildView(UiScreen& screen);
  // Draws the view's library icon centred in the dialog's content band `band`, and
  // each of the `count` rows' icons at the row's left when its label leaves room
  // (GameViewIcons), in the ink of the row's label. Nothing for an empty band.
  void drawViewIcons(UiScreen& screen, freeink::ui::Rect band, MatchState state, const GameCore::MatchMenu& menu,
                     uint8_t count) const;
  // The view's translated headline; null for a state with no view.
  const char* viewHeadline(MatchState state) const;
  // This loop pass's touch gesture on the logical screen, if any.
  GameTouch::Gesture readGesture() const;

  GameCore::Manifest manifest;
  GameCore::Roster roster;
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

  Start start;
  // The state allows resume.bin to be written: Playing or Paused, not yet Over or Error
  // (loop task; handle() keeps it).
  bool resumeWritable = false;
  // Entering Over could not delete resume.bin (the card refused): the loop retries until it
  // can, so a finished round's save does not survive one failed remove. Play again keeps it pending until the new
  // round's first snapshot is written over the file (flushResumeOf), so a Leave before that still removes the finished
  // round's save. Loop task.
  bool resumeDeletePending = false;
  uint32_t resumeDeleteTriedMs = 0;  // millis() of the last failed delete
  // onExit() is running (a forced exit), from millis() forcedExitBeganMs.
  bool forcedExit = false;
  uint32_t forcedExitBeganMs = 0;

  GameCore::MatchLifecycle lifecycle;  // loop task
  // lifecycle's state for render, stored by handle() after each transition.
  std::atomic<MatchState> shown{MatchState::Starting};
  // The focused option of the view on screen (buttons move it; loop task writes).
  std::atomic<uint8_t> selected{0};
  // GameVM::roundsEnded() when the match last entered Over (loop task).
  uint32_t roundsSeen = 0;
  uint32_t shownFrame = 0;  // loop task: the frameGen it last asked to render
  // The GameVM::roundsStarted() the match waits for: 1 at first (the match's own first round), and Play again's
  // roundsStarted() plus one after it (loop task writes). Until the count reaches it, the loop asks for no render,
  // since every frame published meanwhile is the last round's (or, at first, there is none), drops every gesture,
  // since none was aimed at the new round, and renderCanvas draws nothing, so a Resume in the gap keeps the pause menu
  // rather than a board whose taps are dropped. Atomic since render reads it.
  std::atomic<uint32_t> roundsStartedAwaited{1};
  // Written by render: the roundsStarted() it saw when it last got through the gate and had drawn (and handed to the
  // panel with displayBuffer) the frame. The loop drops gestures until this reaches roundsStartedAwaited, so a tap
  // made while the panel is still refreshing to the round's first frame is not aimed at that round either.
  // displayBuffer returns once the driver has the frame: how much of the panel's own refresh is behind that is the
  // driver's, and unmeasured here.
  std::atomic<uint32_t> roundsDisplayed{0};
  // Written by render: the frameGen its last render saw. The loop does not ask
  // again for a frame a render already took, so a render that sees no new frame
  // is always a repaint someone else asked for.
  std::atomic<uint32_t> renderedFrame{0};
  // Render task only: the screen does not hold the canvas (a view was drawn over
  // it, or a render skipped it while awaiting a round), so the next frame is drawn
  // on a cleared screen with a full refresh.
  bool viewOnScreen = false;
  // Render task only: the state whose view renderView is drawing.
  MatchState viewState = MatchState::Starting;
  // Render task only: the view's dialog, a member since it is over 1 KB.
  freeink::ui::OptionDialogProps dialogProps;
  // The error view's text, written once before the match enters Error.
  StrId errorHeadline = StrId::STR_GAMES_ERROR;
  char errorDetail[GameVM::ERROR_CAPACITY] = {};
};
