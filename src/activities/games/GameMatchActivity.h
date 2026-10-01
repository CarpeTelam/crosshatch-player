#pragma once

#include <I18n.h>
#include <Manifest.h>
#include <MatchLifecycle.h>
#include <Roster.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "games/FrameReplay.h"
#include "games/GameTouch.h"
#include "games/GameVM.h"
#include "games/GameViewport.h"
#include "games/MatchStore.h"

// One match, solo or pass, open or hidden (AD-20): owns the GameVM task and, through it, the game's assets,
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
// round is over (GameCore::seatShown). It saves and resumes as a solo match does (below), its save recording the pass
// roster.
//
// A hidden pass match (a pass roster and manifest.hidden) runs the hidden pass machine (MatchLifecycle's hiddenPass)
// with a GameVM that draws only the seat it is asked for: each round begins on the blank hand-off screen (HandOff:
// FrameReplay::drawBlank, a full refresh), whose tap asks the VM for the turn seat (GameVM::showTurnSeat) and shows
// no canvas until that seat's frame is published; a move that passes the turn shows the mover's own frame with the
// "Tap to pass" banner (Result), whose tap goes back to the blank. A forced exit (sleep, any Replace) pushes the blank
// too, after the VM's stop and before the SD steps, and so does a Leave whose panel holds a seat's frame, before it
// goes to Games; a forced exit after the VM is gone (a Leave whose Games screen ran out of memory, a stuck VM stopped
// on the way to Error) pushes it while a seat's frame is still on the panel. So no seat's frame stays on the panel.
// The hand-off screen is pushed with a full refresh when it replaces anything else, and repainted without one.
// docs/crosshatch/game-canvas.md has the states.
//
// A touch reaches the VM with the frame the panel showed when it was made (GameVM::postInput), so one made under a
// seat's frame never reaches another seat (or seat 0) after the move that passed the turn or ended the round.
//
// A solo or pass match with a .pkg also saves its latest snapshot as resume.bin (GameSaveStore), recording its roster:
// the VM hands each committed one to the loop through GameVM::committed(), the loop
// writes it every pass in Playing, Paused, Result, and HandOff, the forced exit and Leave write the last
// pending one, and entering Over deletes the file. Start::Resume restores it with the roster the save records, whatever
// roster the caller passed: a hidden pass save resumes on the blank hand-off screen (HandOff), whose tap shows the
// saved turn seat; any other at Playing, on that seat's frame.
//
// The match follows AD-21's solo states, or the hidden pass ones (GameCore::MatchLifecycle), changed only by
// handle(). The VM exists from Playing (or a hidden match's first HandOff) until Leaving, or until a stuck VM is
// stopped on the way to Error, so Playing, Paused, Over, Result, and HandOff always have one.
class GameMatchActivity final : public Activity, private UiAppHost {
 public:
  // New starts a round with setup; Resume continues from the game's resume.bin, solo or pass, with the save's roster
  // when GameSaveStore can use it, starts a new match with the caller's roster (logged) when there is no usable save,
  // and stops in the error view, the save untouched, when there is one that cannot be read, that this host cannot start
  // (GameSaveStore's Unstartable: its own reason), or that the VM refuses.
  enum class Start : uint8_t { New, Resume };

  // The forced exit's SD steps (the resume write, the resume.bin delete retry, the
  // ch.store flush) start only within this long of the start of onExit(); a step that
  // would start later is skipped and logged. A step that starts in time is one tmp write
  // and rename, whose time is the card's. Leave has no deadline.
  static constexpr uint32_t FORCED_EXIT_DEADLINE_MS = 1500;

  // `roster` is who plays a New match: GameCore::Roster::solo(), or Roster::pass(n) for a pass match. A Resume that
  // loads a save plays the save's roster instead (and a new match with this one when there is no usable save).
  GameMatchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const GameCore::Manifest& manifest,
                    const GameCore::Roster& roster, Start start = Start::New);
  ~GameMatchActivity() override;

  void onEnter() override;
  // The forced exit (AD-20) when the match did not leave on its own (sleep, any
  // Replace): stops the VM, in a hidden pass match pushes the blank hand-off screen,
  // and flushes ch.store under the RenderLock ActivityManager holds, which it never
  // takes again (12cc816).
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
  // A tap anywhere on the Result banner's screen or the hand-off screen.
  static constexpr freeink::ui::ActionId ACTION_PASS = 2;
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
  // A hidden pass match's Result and HandOff: a tap (or Confirm) once that screen is on the panel passes the device on.
  void loopHandOff();
  // Play again's steps on the way into the new round (Playing, or a hidden match's HandOff).
  void startNextRound();
  // Playing, Paused, Over, Result, HandOff: false (now in Error) when the VM failed or is stuck.
  bool vmHealthy();
  void choose(const GameCore::MatchMenu& menu, int index);
  // A user exit (Leave, or Back from the error view), from loop().
  void leave();
  // Cancels the VM, joins it or abandons it (AD-5), and writes its last pending
  // snapshot before it is freed; in a hidden pass match's forced exit it pushes the
  // blank between the wait and that write (pushForcedExitBlank). The caller holds
  // RenderLock, since render reads vm and the frames an abandon frees. Nothing without a VM.
  void stopVm();
  // A hidden pass match's forced exit only (AD-12, AD-20): pushBlank, so no seat's frame stays on the panel while the
  // device sleeps. Nothing otherwise; a user Leave pushes its own (leave).
  void pushForcedExitBlank();
  // Draws the blank hand-off screen (FrameReplay::drawBlank) and pushes it with a half refresh; logged with `when`.
  // The caller holds RenderLock (its own, or ActivityManager's in onExit, never taken again: 12cc816), so the render
  // task is idle and replay, the framebuffer, and `panel` are this task's for now.
  void pushBlank(const char* when);
  // Writes a dirty ch.store now (round end, Leave, onExit; AD-17).
  void flushStore();
  // Playing, Paused, Result, and HandOff: writes the VM's latest committed snapshot as resume.bin (not
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
  // Start::Resume, after the assets load and before the VM is created: reads the save into `snapshot` and `ver` (in the
  // store's buffer; valid until the store's next call), and makes its roster the match's (roster, lifecycle; logged).
  // True with `snapshot` empty when there is no usable save (logged: the match starts new with the caller's roster,
  // since nothing is lost). False, with the error view's reason in `refusal`, when a save is there and cannot be used
  // now, because it would not read or this host cannot start it (GameSaveStore::peekResume, which reads through the
  // store's buffer and leaves its roster): the caller shows the error view and leaves the file, which a new match
  // would replace.
  bool seedResume(std::span<const uint8_t>& snapshot, uint16_t& ver, StrId& refusal);
  // Cancels a VM past WATCHDOG_MS, abandons it if it does not join, and shows the error view.
  void stopStuckVm();
  // For a VM that did not join: abandons it (GameVM::abandon), writing its last snapshot
  // first if it ends within the wait after all; a task that may still run keeps the
  // store slot.
  void abandonVm();

  void renderCanvas();
  void renderView(MatchState state);
  // The blank hand-off screen, pushed before its tap zone is published: with a full refresh when it replaces anything
  // else on the panel, and a fast one when it repaints the blank already there (panel).
  void renderHandOff();
  // Render task: whether a view in `state` sits over the canvas. Not the error view, nor a pause menu entered from
  // HandOff, nor anything while the seat the match awaits has not published its frame (a hidden match).
  bool canvasUnderView(MatchState state) const;
  static void viewScreen(UiScreen& screen, void* user);
  void buildView(UiScreen& screen);
  // Result and HandOff: a tap zone over the whole safe area, and in Result the framed "Tap to pass" banner.
  void buildHandOffView(UiScreen& screen, MatchState state);
  // Render task: a pause menu that returns to play while the new round has not published its first frame (the
  // Play-again gap: after a Play again, never before a match's first frame), which loopView redraws (gapWhenPaused)
  // once it has.
  bool pauseInGap() const;
  // Draws the view's library icon centred in the dialog's content band `band`, and
  // each of the `count` rows' icons at the row's left when its label leaves room
  // (GameViewIcons), in the ink of the row's label. Nothing for an empty band.
  void drawViewIcons(UiScreen& screen, freeink::ui::Rect band, MatchState state, const GameCore::MatchMenu& menu,
                     uint8_t count) const;
  // The view's translated headline; null for a state with no view.
  const char* viewHeadline(MatchState state) const;
  // This loop pass's touch gesture on the logical screen, if any; latches frameDisplayed at the first pass that sees a
  // finger down, and notes whether a finger is still down.
  GameTouch::Gesture readGesture();
  // Loop task: the frame on the panel at millis() `atMs`, as far as the last push tells: the frame before it when
  // `atMs` is before that push completed, else frameDisplayed.
  uint32_t frameAt(uint32_t atMs) const;

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
  // The state allows resume.bin to be written: Playing, Paused, Result, or HandOff, not yet Over or Error
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
  // A hidden pass match's seat gate, the same two counts for the hand-off: the GameVM::showTurnSeat() request the
  // match waits for (stored by handle() before it shows Playing), and the GameVM::seatShownRequest() render saw when
  // it last got through the gate and had pushed the frame. Until the VM has served the request the loop asks for no
  // render and drops gestures, and renderCanvas draws nothing, so no frame of the last seat follows the blank; until
  // render has displayed it, gestures are still dropped. Both 0 (the gate open) for any other match.
  std::atomic<uint32_t> seatAwaited{0};
  std::atomic<uint32_t> seatDisplayed{0};
  // The seat the last turn-passing move passed to, for the Result banner (loop task writes, render reads).
  std::atomic<uint8_t> passTo{0};
  // MatchLifecycle::resumesTo() as handle() last stored it: render's view of where a pause menu returns.
  std::atomic<MatchState> resumesTo{MatchState::Playing};
  // Written by render after it has pushed Result's or HandOff's own screen (that state), and reset to Starting by
  // handle() on every transition: loopHandOff passes the device on only when it equals the state the match is in, so
  // a tap or Confirm made before that screen is on the panel is dropped, and a render that pushed the last state's
  // screen after a transition never opens the next one.
  std::atomic<MatchState> passScreenShown{MatchState::Starting};
  // Loop task: GameVM::turnsPassed() when the match last entered Result.
  uint32_t turnsSeen = 0;
  // Loop task: the pause menu was opened in the Play-again gap (its "Starting the next round" line is on screen), so
  // it is redrawn once the new round's first frame is published.
  bool gapWhenPaused = false;
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
  // What the last push left on the panel: a seat's frame (Seat: the canvas, or a view over a seat's frame), the blank
  // hand-off screen (Blank), or anything else (Other: nothing yet, a view with no canvas under it, the error view, or
  // the Over menu over seat 0's frame, which is everyone's). Every push sets it under RenderLock (render, and the
  // loop task's pushBlank under the lock it holds), and the loop task reads it under the lock: Leave and the forced
  // exit push the blank only over a seat's frame, and the hand-off screen is refreshed in full only when it replaces
  // something else.
  enum class Panel : uint8_t { Other, Seat, Blank };
  Panel panel = Panel::Other;
  // Written by render after renderCanvas's push: the number of the frame it drew (GameVM::drawFront's, read under the
  // frame mutex, so a frame the VM published while render ran is named when it is the one drawn), which loopPlaying
  // posts each touch with (GameVM::postInput), so the VM drops one made under another seat's frame. Stored before
  // roundsDisplayed and seatDisplayed, so a loop that sees those sees this frame's number.
  std::atomic<uint32_t> frameDisplayed{0};
  // Loop task: frameDisplayed at the first Playing pass that saw the finger down (isScreenTouchHeld), kept until a pass
  // with no finger down, so a contact held from under one seat's frame until after the next seat's was pushed carries
  // the first. A contact the loop never saw down (it lifted while the loop was blocked) is tagged when its lift is
  // read, and a tap is also back-dated (frameAt).
  uint32_t touchDownFrame = 0;
  bool touchDownLatched = false;
  // Loop task: readGesture saw a finger down on this pass (isScreenTouchHeld).
  bool contactHeld = false;
  // The last canvas push that changed the panel, written by render after displayBuffer returned (which it does once
  // the panel's refresh has completed) and read by the loop task (frameAt), under pushMutex: the frame on the panel
  // before it, and millis() when it completed. Lock order: RenderLock, then pushMutex; the loop takes pushMutex alone.
  struct LastPush {
    uint32_t before = 0;
    uint32_t doneMs = 0;
  };
  mutable std::mutex pushMutex;
  LastPush lastPush;
  // Render task only: the view's dialog, a member since it is over 1 KB.
  freeink::ui::OptionDialogProps dialogProps;
  // Render task only: Result's banner and its text, members since the props are over 256 bytes.
  freeink::ui::ButtonProps bannerProps;
  char bannerText[96] = {};
  // The error view's text, written once before the match enters Error.
  StrId errorHeadline = StrId::STR_GAMES_ERROR;
  char errorDetail[GameVM::ERROR_CAPACITY] = {};
};
