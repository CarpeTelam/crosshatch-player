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

#include "GameMatchView.h"
#include "GamePicture.h"
#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "games/FrameReplay.h"
#include "games/GameTouch.h"
#include "games/GameVM.h"
#include "games/GameViewport.h"
#include "games/MatchPersistence.h"
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
// with a GameVM that draws only the seat it is asked for: each round begins on the hand-off screen (HandOff: laid out
// as the title screen, GameSplashLayout: the game's handoff.bmp, else its title.bmp, else its icon in the splash band,
// "Player N's turn" in the first row's place and the "I'm ready" button in the second's, drawn by the runtime alone on
// a cleared screen, a full refresh), whose button asks the VM for the turn seat (GameVM::showTurnSeat) and shows no
// canvas until that seat's frame is published; a move that passes the turn shows the mover's own frame with the "Tap to
// pass" banner (Result), whose tap goes to the hand-off screen. Each is passed by its button or Confirm only, never a
// tap elsewhere, as a plain tap target acts: on release, even while its screen is being pushed (the owner's decision of
// 2026-10-03), and the first move tap on a seat's frame after "I'm ready" is accepted during that frame's push
// (firstFramePushing). A forced exit (sleep, any Replace) pushes a plain white blank (FrameReplay::drawBlank), after
// the VM's stop and before the SD steps, and so does a Leave whose panel holds a seat's frame, before it goes to Games;
// a forced exit after the VM is gone (a Leave whose Games screen ran out of memory, a stuck VM stopped on the way to
// Error) pushes it while a seat's frame is still on the panel. So no seat's frame stays on the panel. The hand-off
// screen is pushed with a full refresh when it replaces anything else, and repainted without one.
// docs/crosshatch/game-canvas.md has the states.
//
// A touch reaches the VM with the frame the panel showed when it was made (GameVM::postInput), so one made under a
// seat's frame never reaches another seat (or seat 0) after the move that passed the turn or ended the round.
//
// A solo or pass match with a .pkg also saves its latest snapshot as resume.bin (GameSaveStore), recording its roster:
// the VM hands each committed one to the loop through GameVM::committed(), the loop
// writes it every pass in Playing, Paused, Result, and HandOff, the forced exit and Leave write the last
// pending one, and entering Over deletes the file. Start::Resume restores it with the roster the save records, whatever
// roster the caller passed: a hidden pass save resumes on the hand-off screen (HandOff), whose button shows the saved
// turn seat; any other at Playing, on that seat's frame.
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

  // The forced exit's SD steps start only within this long of the start of onExit() (MatchPersistence).
  static constexpr uint32_t FORCED_EXIT_DEADLINE_MS = MatchPersistence::FORCED_EXIT_DEADLINE_MS;

  // `roster` is who plays a New match: GameCore::Roster::solo(), or Roster::pass(n) for a pass match. A Resume that
  // loads a save plays the save's roster instead (and a new match with this one when there is no usable save).
  // `settings` (copied) is the chosen value of each setting the manifest declares, which the VM gives the game as
  // ctx.settings at every setup (a resumed match runs none until Play again, which ends in the error view when these
  // are fewer than the manifest declares).
  GameMatchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const GameCore::Manifest& manifest,
                    const GameCore::Roster& roster, Start start = Start::New,
                    const GameCore::SettingValues& settings = {});
  ~GameMatchActivity() override;

  void onEnter() override;
  // The forced exit (AD-20) when the match did not leave on its own (sleep, any
  // Replace): stops the VM, in a hidden pass match pushes the plain white blank,
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
  // A hidden pass match's Result and HandOff: a tap on the banner or the button (or Confirm) passes the device on.
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
  // A hidden pass match's forced exit with a seat's frame possibly on the panel (panel is Seat; AD-12, AD-20):
  // pushBlank, so no seat's frame stays on the panel while the device sleeps. Nothing otherwise; a user Leave pushes
  // its own (leave).
  void pushForcedExitBlank();
  // Draws the plain white blank (FrameReplay::drawBlank) and pushes it with a half refresh; logged with `when`.
  // The caller holds RenderLock (its own, or ActivityManager's in onExit, never taken again: 12cc816), so the render
  // task is idle and replay, the framebuffer, and `panel` are this task's for now.
  void pushBlank(const char* when);
  // Writes a dirty ch.store now (round end, Leave, onExit; AD-17): MatchPersistence::flushStore.
  void flushStore();
  // Playing, Paused, Result, and HandOff: writes the VM's latest committed snapshot as resume.bin (not
  // again until FLUSH_INTERVAL_MS after a failed write, Leave and the forced exit
  // included). Nothing in Over or Error, or without a .pkg.
  void flushResume();
  // The same for `from`, which may be a VM that is no longer vm (abandonVm's hook).
  void flushResumeOf(GameVM& from);
  // Removes resume.bin again after Over's delete failed (MatchPersistence::deletePending), at most
  // every FLUSH_INTERVAL_MS from the last failed try unless `forced` (Leave, the forced exit).
  void retryResumeDelete(bool forced);
  // Cancels a VM past WATCHDOG_MS, abandons it if it does not join, and shows the error view.
  void stopStuckVm();
  // For a VM that did not join: abandons it (GameVM::abandon), writing its last snapshot
  // first if it ends within the wait after all; a task that may still run keeps the
  // store slot.
  void abandonVm();

  void renderCanvas();
  void renderView(MatchState state);
  // The hand-off screen (the splash band with the game's page or its icon, "Player N's turn", and the "I'm ready"
  // button, where the title screen has its band and its first two rows: GameSplashLayout), pushed with a full refresh
  // when it replaces anything else on the panel, and a fast one when it repaints the hand-off screen already there
  // (panel). Nothing, the screen before it staying, until the VM has named the round's first turn seat
  // (announcementAwaited).
  void renderHandOff();
  // Render task: whether a view in `state` sits over the canvas. Not the error view, nor a pause menu entered from
  // HandOff, nor anything while the seat the match awaits has not published its frame (a hidden match), nor anything
  // in the Play-again gap (the round the match awaits has not published its first frame), in every mode.
  bool canvasUnderView(MatchState state) const;
  static void viewScreen(UiScreen& screen, void* user);
  // Render task: a pause menu that returns to play while the new round has not published its first frame (the
  // Play-again gap: after a Play again, never before a match's first frame), which loopView redraws (gapWhenPaused)
  // once it has.
  bool pauseInGap() const;
  // This loop pass's touch gesture on the logical screen, if any; latches frameDisplayed at the first pass that sees a
  // finger down, and notes whether a finger is still down.
  GameTouch::Gesture readGesture();
  // Loop task: the frame on the panel at millis() `atMs`, as far as the last push tells: the frame before it when
  // `atMs` is before that push completed, else frameDisplayed.
  uint32_t frameAt(uint32_t atMs) const;

  GameCore::Manifest manifest;
  GameCore::Roster roster;
  GameCore::SettingValues settings;  // ctx.settings, handed to GameVM::create
  GameViewport viewport;
  FrameReplay replay;
  // A hidden pass match's hand-off art, the band's picture: handoff.bmp, else title.bmp, else the game's icon. Loaded
  // by onEnter (loop task, before the VM), drawn by renderHandOff; nothing for any other match.
  GamePicture picture;
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
  // resume.bin and ch.store's writes, the Over delete's retry, and the forced exit's deadline over them.
  MatchPersistence persistence;

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
  // render and drops gestures, and renderCanvas draws nothing, so no frame of the last seat follows the hand-off; until
  // render has displayed it, gestures are still dropped. Both 0 (the gate open) for any other match.
  std::atomic<uint32_t> seatAwaited{0};
  std::atomic<uint32_t> seatDisplayed{0};
  // The seat the last turn-passing move passed to, for the Result banner (loop task writes, render reads).
  std::atomic<uint8_t> passTo{0};
  // A hidden pass match's hand-off names the round's first turn seat, which the VM knows only once the round has begun
  // (setup, or the restored save): the GameVM::turnAnnouncements() count the hand-off screen waits for, 1 at first and
  // Play again's count plus one after it (loop task writes, before shown). Until it is reached renderHandOff pushes
  // nothing and the screen before stays (the title screen at the match's start, the end-of-round menu after Play
  // again, a pause menu opened meanwhile until the seat is named), so no tap passes it; the loop asks for a render once
  // it is (handOffHeldBack). Result's hand-off needs no wait: the VM stores the next seat before it
  // counts the turn change.
  std::atomic<uint32_t> announcementAwaited{1};
  // Written by render: it skipped the hand-off screen for want of the announcement, so the loop asks for a render when
  // the count arrives; cleared by a render that drew it.
  std::atomic<bool> handOffHeldBack{false};
  // MatchLifecycle::resumesTo() as handle() last stored it: render's view of where a pause menu returns.
  std::atomic<MatchState> resumesTo{MatchState::Playing};
  // Set by handle() on HandOff -> Playing (the first seat frame after "I'm ready"), cleared by renderCanvas once that
  // frame is on the panel and by any other transition: while set, loopPlaying lets a touch made after that transition
  // (playingSinceMs) through as that seat's move even though the frame is still being pushed.
  std::atomic<bool> firstFramePushing{false};
  // Loop task: millis() at the last transition handle() took, which dates the hand-off's pass when firstFramePushing.
  uint32_t playingSinceMs = 0;
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
  // What the last push left on the panel: a seat's frame (Seat: the canvas, or a view over a seat's frame), no seat's
  // (Blank: the hand-off screen, or the plain white blank of a Leave or the forced exit), or anything else (Other:
  // nothing yet, a view with no canvas under it, the error view, or the Over menu over seat 0's frame, which is
  // everyone's). Every push sets it under RenderLock (render, and the loop task's pushBlank under the lock it holds),
  // and the loop task reads it under the lock: Leave and the forced exit push the blank only over a seat's frame, and
  // the hand-off screen is refreshed in full only when it replaces something else.
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
  uint32_t touchDownMs = 0;  // loop task: millis() at the same latch
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
  // Render task only: the seat the hand-off screen names (GameVM::passedTo when renderHandOff drew it).
  uint8_t handOffSeat = 0;
  // Render task only: builds the runtime's own views (the dialog's props are members of it).
  GameMatchView views;
  // The error view's text, written once before the match enters Error.
  StrId errorHeadline = StrId::STR_GAMES_ERROR;
  char errorDetail[GameVM::ERROR_CAPACITY] = {};
};
