#if FREEINK_CAP_GAMES

#include "GameMatchActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <utility>

#include "GameMatchView.h"
#include "GameSplashLayout.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "games/GameAssets.h"
#include "games/GameHostCaps.h"
#include "games/GameRegistry.h"
#include "games/GameVM.h"
#include "games/MatchResume.h"

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

// A hidden pass match: a pass roster of a game whose manifest says hidden (MatchLifecycle's hiddenPass).
bool isHiddenPass(const GameCore::Roster& roster, const GameCore::Manifest& manifest) {
  return roster.mode == GameCore::Mode::Pass && manifest.hidden;
}

}  // namespace

static_assert(GameVM::STOP_POLL_MS == 5, "game-canvas.md's forced-exit bound (about 1,030 ms) assumes a 5 ms poll");
static_assert(GameMatchActivity::FORCED_EXIT_DEADLINE_MS == 1500,
              "game-canvas.md states the forced exit's 1,500 ms bound; change the doc with this deadline");
static_assert(GameSaveStore::PACKAGE_HASH_BYTES == GamePkg::HASH_BYTES,
              "resume.bin records the package hash .pkg holds (GameSaveStore builds without GameHash.h)");

GameMatchActivity::GameMatchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     const GameCore::Manifest& manifest, const GameCore::Roster& roster,
                                     const Start start, const GameCore::SettingValues& settings)
    : Activity("GameMatch", renderer, mappedInput),
      UiAppHost(renderer),
      manifest(manifest),
      roster(roster),
      settings(settings),
      start(start),
      lifecycle(isHiddenPass(roster, manifest)) {
  persistence.bind(store, this->manifest.id, [] { return static_cast<uint32_t>(millis()); });
}

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
  // resume.bin is saved and read only for an installed package: its hash says whether the
  // save is this package's (AD-16). A game without a valid .pkg plays without one.
  uint8_t pkgHash[GamePkg::HASH_BYTES] = {};
  if (GameRegistry::readPackageHash(manifest.id, pkgHash)) {
    store.saves().setPackageHash(pkgHash);
    // A save records who plays (its mode, and a pass match's seats); a resumed match's load replaces this roster.
    store.saves().setRoster(roster);
  } else if (start == Start::Resume) {
    // Continue was offered for a save of this package, so its .pkg was readable a moment ago. A match started new here
    // would play without a hash, and could not tell the save from any other file: stop, and leave the save alone.
    LOG_ERR("GAME", "%s: cannot read .pkg; not starting a new match over a save", manifest.id);
    fail(StrId::STR_GAMES_START_FAILED, tr(STR_GAMES_RESUME_FAILED));
    return;
  } else {
    LOG_INF("GAME", "%s: no valid .pkg; no resume.bin", manifest.id);
  }
  GameAssets assets;
  // GameAssets restores store.bin into the slot.
  const GameAssets::LoadResult loaded = assets.load(manifest.id, store.saves(), store.slot());
  if (loaded != GameAssets::LoadResult::Ok) {
    fail(StrId::STR_GAMES_START_FAILED, I18N.get(loadFailureReason(loaded)));
    return;
  }
  replay.loadFonts(renderer);
  // Read after assets.load, which restores store.bin through the store's buffer that the snapshot is read into, and
  // before create, since the VM's Session and the lifecycle are built from the roster the save records. Nothing between
  // here and setResume calls the store, so the snapshot stays valid.
  std::span<const uint8_t> snapshot;
  uint16_t ver = 0;
  if (start == Start::Resume) {
    const ResumeSeed seed = seedResume(store.saves(), manifest, gameHostCaps());
    if (seed.outcome == ResumeSeed::Outcome::Refused) {
      // The save is on the card and unchanged. Starting a new match would replace it with its first snapshot, so the
      // match stops here instead, with the persistence still not writable (Error never writes resume.bin).
      fail(StrId::STR_GAMES_START_FAILED,
           I18N.get(seed.refusal == ResumeSeed::Refusal::NotHere ? StrId::STR_GAMES_RESUME_NOT_HERE
                                                                 : StrId::STR_GAMES_RESUME_FAILED));
      return;
    }
    snapshot = seed.snapshot;
    ver = seed.version;
    if (seed.outcome == ResumeSeed::Outcome::Resume) {
      // The save's roster is the match's, whatever the caller passed: the VM's Session, the lifecycle, and the store's
      // writes (loadResume adopted it) all follow it.
      roster = seed.roster;
      // Still Starting and before the VM exists: no event has been applied, so the flag is fixed from here on.
      lifecycle = MatchLifecycle(isHiddenPass(seed.roster, manifest));
    }
  }
  // A Continue whose save is gone by now (seedResume's New: nothing to lose) starts a new round, which runs setup: with
  // the settings the title screen could not read, it would run without ctx.settings.
  if (start == Start::Resume && snapshot.empty() && settings.count != manifest.settingsCount) {
    LOG_ERR("GAME", "%s: no save to resume and the settings its manifest declares were not read; not starting",
            manifest.id);
    fail(StrId::STR_GAMES_START_FAILED, tr(STR_GAMES_SETTINGS_NOT_READ));
    return;
  }
  // A hidden pass match's hand-off art, read now on the loop task (never in render), once the roster is known: the
  // band's picture, in order the game's handoff.bmp, else its title.bmp (each in PSRAM; none, or one it cannot use,
  // logged, goes to the next), else its icon. No read calls the store, so the snapshot stays valid.
  if (lifecycle.hiddenPass()) {
    picture.loadIcon(manifest);
    if (!picture.loadPage(manifest.id, "handoff", GameCore::HANDOFF_IMAGE_WIDTH, GameCore::HANDOFF_IMAGE_HEIGHT)) {
      picture.loadPage(manifest.id, "title", GameCore::TITLE_IMAGE_WIDTH, GameCore::TITLE_IMAGE_HEIGHT);
    }
  }
  auto created = GameVM::create(std::move(assets), viewport, replay, manifest.id, store.slot(), roster,
                                lifecycle.hiddenPass(), settings);
  if (created && !snapshot.empty() && !created->setResume(snapshot, ver)) {
    // GameVM::setResume refuses only an empty or oversized snapshot, which GameSaveStore's checks already exclude, so
    // this is not reachable today; if it ever is, the first snapshot of a new match would replace the save.
    LOG_ERR("GAME", "%s: the VM refused a %u-byte snapshot; not starting a new match over it", manifest.id,
            static_cast<unsigned>(snapshot.size()));
    fail(StrId::STR_GAMES_START_FAILED, tr(STR_GAMES_RESUME_FAILED));
    return;
  }
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
  persistence.beginForcedExit();
  Activity::onExit();
  // ActivityManager holds RenderLock here: never take it again (12cc816). The VM
  // never takes it, so waiting for it cannot deadlock, and render cannot be reading
  // the frames an abandon frees. After a user exit the match is Leaving already
  // and the VM is gone; the store is flushed again only if a set landed since. The
  // order: cancel and join, the last snapshot written while the VM that holds it still exists (stopVm), abandon if it
  // did not join, a resume.bin delete Over could not finish, the store, and last a hidden pass match's plain white
  // blank when a seat's frame may be on the panel (pushForcedExitBlank). The SD steps stop starting once the deadline
  // has passed; the blank is no SD step, so a skipped or failed step never keeps it from running (its own panel guards
  // aside).
  handle(MatchEvent::ForcedExit);
  stopVm();
  retryResumeDelete(true);
  flushStore();
  pushForcedExitBlank();
}

bool GameMatchActivity::handleHomeGesture() {
  handle(MatchEvent::Home);
  // A match that has already let go (Leaving, e.g. when goToGames() ran out of
  // memory) lets Home go Home as any screen does.
  return lifecycle.state() != MatchState::Leaving;
}

void GameMatchActivity::handle(const MatchEvent event) {
  const MatchState from = lifecycle.state();
  // The title screen lets a Continue through with settings it could not read: a resume runs no setup, but a Play again
  // does, so it ends in the error view rather than start a round with a missing ctx.settings
  // (GameModeActivity::startMatch).
  if (event == MatchEvent::PlayAgain && from == MatchState::Over && settings.count != manifest.settingsCount) {
    LOG_ERR("GAME", "%s: no Play again, the settings its manifest declares were not read", manifest.id);
    fail(StrId::STR_GAMES_START_FAILED, tr(STR_GAMES_SETTINGS_NOT_READ));
    return;
  }
  if (!lifecycle.apply(event)) {
    LOG_DBG("GAME", "%s ignored in %s", MatchLifecycle::name(event), MatchLifecycle::name(from));
    return;
  }
  const MatchState to = lifecycle.state();
  LOG_INF("GAME", "%s: %s -> %s on %s", manifest.id, MatchLifecycle::name(from), MatchLifecycle::name(to),
          MatchLifecycle::name(event));
  // The next view registers its own options; the old table must not route.
  closeRouting();
  // Only the first seat frame after "I'm ready" lets a move through while it is being pushed (loopPlaying).
  playingSinceMs = static_cast<uint32_t>(millis());
  firstFramePushing.store(from == MatchState::HandOff && event == MatchEvent::Tap, std::memory_order_release);
  selected.store(0);
  // Before shown: a render already queued must not see Playing with the old count. A hidden pass match's hand-off
  // likewise waits for the new round's first turn seat, read before playAgain() (startNextRound) can begin it.
  if (event == MatchEvent::PlayAgain) {
    roundsStartedAwaited.store(vm->roundsStarted() + 1);
    if (lifecycle.hiddenPass()) announcementAwaited.store(vm->turnAnnouncements() + 1);
  }
  // The hand-off's tap asks the VM for the next seat, and the canvas waits for that request's frame; stored before
  // shown for the same reason as the count above.
  if (from == MatchState::HandOff && event == MatchEvent::Tap) seatAwaited.store(vm->showTurnSeat());
  resumesTo.store(lifecycle.resumesTo());
  shown.store(to);
  switch (to) {
    case MatchState::Playing:
      persistence.setWritable(true);
      if (event == MatchEvent::PlayAgain) startNextRound();
      // A new round's first frame asks for its own render, as does the next seat's after the hand-off; a resumed one
      // is redrawn, unless it resumes into the Play-again gap, where renderCanvas keeps the view on screen until the
      // new round's first frame.
      if (event != MatchEvent::Resume && event != MatchEvent::Back) return;
      break;
    case MatchState::Result:
      persistence.setWritable(true);
      break;
    case MatchState::HandOff:
      persistence.setWritable(true);
      if (event == MatchEvent::PlayAgain) startNextRound();
      break;
    case MatchState::Over:
      persistence.setWritable(false);
      flushStore();
      // A finished round never resumes (MatchPersistence::onOver).
      persistence.onOver();
      break;
    case MatchState::Leaving:
      // onExit() stops and flushes itself after a forced exit.
      if (event != MatchEvent::ForcedExit) leave();
      return;
    case MatchState::Paused:
      persistence.setWritable(true);
      // A pause menu opened in the Play-again gap says so (pauseInGap; resumesTo was stored above). loopView redraws
      // it once the round starts.
      gapWhenPaused = pauseInGap();
      break;
    case MatchState::Error:
      persistence.setWritable(false);
      break;
    case MatchState::Starting:
      break;
  }
  requestUpdate();
}

void GameMatchActivity::startNextRound() {
  // A save Over could not delete stays pending: it is the finished round's, which must not survive a Leave, and
  // flushResumeOf clears the pending delete once the new round's first snapshot has replaced the file.
  // A delete or write that failed for the finished round must not hold back the new round's first snapshot.
  store.saves().clearResumeBackoff();
  // Frames the last round drew after it ended are never shown, one from a
  // step still running when Play again came included: the loop asks for no
  // render until the new round's first frame is published.
  // roundsStartedAwaited was stored in handle(), before shown.
  shownFrame = vm->frameGen();
  vm->playAgain();
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
    // A hidden pass match's Leave from a pause menu over a seat's frame blanks the panel before Games draws: if
    // goToGames() runs out of memory, this match stays current with no VM, and the frame would stay on the panel.
    if (lifecycle.hiddenPass() && panel == Panel::Seat) pushBlank("leave");
  }
  // Also when a stuck VM took the match to Error first and vm is gone.
  retryResumeDelete(true);
  flushStore();
  activityManager.goToGames();
}

void GameMatchActivity::stopVm() {
  if (!vm) return;
  const bool joined = vm->stop(STOP_TIMEOUT_MS);
  // After the wait, before the VM is freed or abandoned (the snapshot is in its memory).
  // A task that did not join may still publish one on its way out; abandonVm looks again.
  flushResume();
  if (joined) {
    vm.reset();
    return;
  }
  abandonVm();
}

void GameMatchActivity::pushForcedExitBlank() {
  // Last in the forced exit, after the SD steps (e6pre-10): the steps start inside the deadline whatever the half
  // refresh costs, and the blank is not gated by it, so a slow card cannot cost the blank. With or without a VM.
  // Only a hidden pass match's forced exit: a solo or open pass match shows nothing private, and a user Leave pushes
  // its own after the stop (leave). And only when a seat's frame may be on the panel (panel is Seat: Playing, Result's
  // mover frame, a menu over a seat's frame, and any new state whose screen is not drawn yet, which leaves the last
  // frame there): on the hand-off screen, the blank, Over (seat 0's public frame), a drawn error view, or a menu on no
  // frame nothing private shows, and the push would only spend the SD steps' window.
  if (!persistence.forcedExit() || !lifecycle.hiddenPass() || panel != Panel::Seat) return;
  // Not an SD step: the deadline does not gate it, and nothing follows it that it could cost time.
  pushBlank("forced exit");
}

void GameMatchActivity::pushBlank(const char* when) {
  // The caller holds RenderLock (in onExit ActivityManager's, never taken here: 12cc816), so the render task is not
  // inside render() and replay, the framebuffer, and panel are this task's for now.
  // Plain white: nothing private, and no picture, so a sleep screen drawn over it shows nothing of the match.
  replay.drawBlank(renderer);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  panel = Panel::Blank;
  LOG_INF("GAME", "%s: %s: blank screen pushed (half refresh)", manifest.id, when);
}

void GameMatchActivity::retryResumeDelete(const bool forced) { persistence.retryResumeDelete(forced); }

void GameMatchActivity::flushResume() {
  if (vm) flushResumeOf(*vm);
}

void GameMatchActivity::flushResumeOf(GameVM& from) { persistence.flushResumeOf(from.committed()); }

void GameMatchActivity::flushStore() { persistence.flushStore(); }

void GameMatchActivity::abandonVm() {
  LOG_ERR("GAME", "VM did not stop within %u ms of cancel; abandoning it", static_cast<unsigned>(STOP_TIMEOUT_MS));
  const auto writeLast = [](GameVM& ended, void* self) { static_cast<GameMatchActivity*>(self)->flushResumeOf(ended); };
  if (GameVM::abandon(std::move(vm), writeLast, this)) return;
  // The leaked task may still call ch.store.set; the destructor leaks the slot.
  slotLeaked = true;
  LOG_ERR("GAME", "The ch.store slot stays with the leaked VM");
}

void GameMatchActivity::stopStuckVm() {
  static_assert(WATCHDOG_MS == 3000, "STR_GAMES_NOT_RESPONDING names the limit: 3 seconds");
  LOG_ERR("GAME", "%s: a call ran over %u ms; stopping the VM", manifest.id, static_cast<unsigned>(WATCHDOG_MS));
  char detail[GameVM::ERROR_CAPACITY];
  snprintf(detail, sizeof(detail), "%s", tr(STR_GAMES_NOT_RESPONDING));
  persistence.setWritable(false);  // the match is going to Error, which never writes resume.bin
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
  const MatchState current = lifecycle.state();
  // A latch set in Playing outlives the pass that left it (a pass returning early, or the match leaving Playing, never
  // reaches loopPlaying's own release): free it on the first pass elsewhere that sees no finger down, so the next
  // contact in Playing latches its own. A finger still down across the transition keeps its latch: it began before the
  // hand-off.
  if (current != MatchState::Playing && touchDownLatched) {
    int heldX = 0;
    int heldY = 0;
    if (!mappedInput.isScreenTouchHeld(heldX, heldY)) touchDownLatched = false;
  }
  switch (current) {
    case MatchState::Playing:
      loopPlaying();
      return;
    case MatchState::Paused:
    case MatchState::Over:
    case MatchState::Error:
      loopView();
      return;
    case MatchState::Result:
    case MatchState::HandOff:
      loopHandOff();
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
  // A hidden pass match's move passed the turn: the VM has drawn the mover's frame again, which Result shows under
  // the banner naming the next seat (stored first). Only a hidden match's VM counts these.
  const uint32_t passed = vm->turnsPassed();
  if (passed != turnsSeen) {
    turnsSeen = passed;
    passTo.store(vm->passedTo());
    handle(MatchEvent::TurnChanged);
    // Result writes resume.bin as any state does (R8): not left to the next pass, which a Back, Home, or sleep may
    // beat.
    flushResume();
    return;
  }

  // Until a round's first frame is published, and after that until the render task has handed
  // it to the panel (roundsDisplayed), the screen still shows what came before: the title screen
  // before the match's first round, the end-of-round menu or the last round's frame after Play
  // again. A tap there is not aimed at the round, so it is read (which consumes the contact)
  // and dropped. Only the first of the two also holds the frame back: once the count has moved,
  // the frame is asked for and drawn.
  // A hidden pass match's next seat is held back the same way: until the VM has served the hand-off's request, the
  // front frame may still be the last seat's, and until render has pushed the new one the hand-off screen is on the
  // panel.
  const uint32_t awaited = roundsStartedAwaited.load();
  const uint32_t seat = seatAwaited.load();
  const bool awaitingRound = vm->roundsStarted() < awaited || vm->seatShownRequest() < seat;
  const bool awaitingDisplay = awaitingRound || roundsDisplayed.load(std::memory_order_acquire) < awaited ||
                               seatDisplayed.load(std::memory_order_acquire) < seat;
  // Edge gestures never get here as game input: Back is Button::Back above,
  // ActivityManager takes Home (handleHomeGesture) and the light panel first, and
  // GameTouch drops every edge swipe that is left.
  const GameTouch::Gesture gesture = readGesture();
  GameCore::GameEvent event;
  const bool aimed =
      GameTouch::toEvent(gesture, renderer.getScreenWidth(), renderer.getScreenHeight(), viewport, event);
  // The first seat frame after "I'm ready" accepts a touch made during its push (the owner's decision of 2026-10-03:
  // the hand-off needs one tap per step): once the VM has published the frame (!awaitingRound), a touch whose
  // touch-down came at or after the hand-off passed (playingSinceMs) is that seat's move, whatever the push has
  // reached. It skips touchDownFrame and frameAt and is posted with the frame the VM has now, so
  // GameVM::madeUnderAnotherSeat still keeps it for that seat. A contact whose touch-down preceded the transition (the
  // press that passed the hand-off, lifting late) is dropped. Open pass and solo never set the flag.
  bool firstFrameTouch = false;  // handled here: posted, or dropped and logged
  if (aimed && !awaitingRound && firstFramePushing.load(std::memory_order_acquire)) {
    firstFrameTouch = true;
    const auto nowMs = static_cast<uint32_t>(millis());
    uint32_t began = touchDownLatched ? touchDownMs : nowMs;
    if (gesture.kind == GameTouch::Kind::Tap) {
      const auto backDated = static_cast<uint32_t>(nowMs - gpio.lastTouchHeldMs());
      if (static_cast<int32_t>(backDated - began) < 0) began = backDated;
    }
    if (static_cast<int32_t>(began - playingSinceMs) >= 0) {
      const uint32_t gen = vm->frameGen();
      // As the tag below: UNTAGGED is never dropped by the VM, one less can only drop.
      vm->postInput(event, gen == GameVM::UNTAGGED ? gen - 1 : gen);
    } else {
      LOG_INF("GAME", "%s: dropped a touch that began %d ms before the hand-off passed", manifest.id,
              -static_cast<int>(static_cast<int32_t>(began - playingSinceMs)));
    }
  }
  if (aimed && awaitingDisplay && !firstFrameTouch) {
    LOG_INF("GAME", "%s: dropped a touch before the frame was on the panel", manifest.id);
  }
  if (!awaitingDisplay && aimed && !firstFrameTouch) {
    // With the frame on the panel when the loop first saw the finger down (or now, for a contact it never saw down), so
    // the VM drops it if another seat has been drawn since (a pass match). A tap is also back-dated by its touch-only
    // held time (HalGPIO::lastTouchHeldMs; MappedInputManager::getHeldTime answers a button's hold on a pass with a
    // button edge) to its first touch sample against the last push (frameAt), and the older frame is posted. What this
    // cannot reach is in game-canvas.md. A frame number equal to GameVM::UNTAGGED, which the VM never drops, is posted
    // one less, which can only drop.
    uint32_t shown = touchDownLatched ? touchDownFrame : frameDisplayed.load(std::memory_order_acquire);
    if (gesture.kind == GameTouch::Kind::Tap) {
      const uint32_t atTouchDown = frameAt(static_cast<uint32_t>(millis() - gpio.lastTouchHeldMs()));
      if (static_cast<int32_t>(atTouchDown - shown) < 0) shown = atTouchDown;
    }
    vm->postInput(event, shown == GameVM::UNTAGGED ? shown - 1 : shown);
  }
  // No finger down: the contact is over, posted, dropped, or no gesture at all, and the next one latches its own.
  if (!contactHeld) touchDownLatched = false;
  vm->pollTimer();
  store.flushIfDue(millis());
  flushResume();
  retryResumeDelete(false);  // pending only after a Play again over an Over delete that failed

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
    flushResume();
    retryResumeDelete(false);
  }
  // Back resumes from the pause menu, leaves from the error view, and does
  // nothing in the end-of-round menu (MatchLifecycle).
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    handle(MatchEvent::Back);
    return;
  }
  // The pause menu opened in the Play-again gap says the round is starting; once it has, it is drawn without the line.
  if (state == MatchState::Paused && gapWhenPaused && vm && vm->roundsStarted() >= roundsStartedAwaited.load()) {
    gapWhenPaused = false;
    requestUpdate();
  }
  const GameCore::MatchMenu menu = MatchLifecycle::menuFor(state);
  const auto route = routeTouch(mappedInput);
  if (route && route.event.action == GameMatchView::ACTION_OPTION) {
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

void GameMatchActivity::loopHandOff() {
  const MatchState state = lifecycle.state();
  const auto now = static_cast<uint32_t>(millis());
  // As in a menu: the VM may still fail, hang, or set ch.store in a call that was running when the screen changed.
  if (!vmHealthy()) return;
  store.flushIfDue(now);
  flushResume();
  retryResumeDelete(false);
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    handle(MatchEvent::Back);
    return;
  }
  // A timer that falls due here is held by the VM for the next seat (GameVM::stepHandOff).
  vm->pollTimer();
  // The hand-off screen render held back for the round's first turn seat: the VM has named it now.
  if (state == MatchState::HandOff && handOffHeldBack.load(std::memory_order_acquire) &&
      vm->turnAnnouncements() >= announcementAwaited.load()) {
    handOffHeldBack.store(false);
    requestUpdate();
  }
  // A tap on the banner or the "I'm ready" button (the screen's one tap target; a tap elsewhere routes nothing), or
  // Confirm, passes the device on at its release, as the title screen's button does, even while the screen is being
  // pushed (its routing is published before the push). A tap in the instant between handle() and the new screen's
  // renderUi routes nothing (closeRouting), and one before the hand-off names the turn seat finds no routing yet. The
  // banner and the button overlap, so a stray second tap on the banner's spot can pass the hand-off too (the owner
  // accepts that, 2026-10-03).
  const auto route = routeTouch(mappedInput);
  // Nothing passes a hand-off screen that has not been pushed for want of the turn seat (renderHandOff pushes nothing
  // until the VM has named it, and the loop asks again), so neither does Confirm.
  const bool named = state != MatchState::HandOff || vm->turnAnnouncements() >= announcementAwaited.load();
  const bool confirmed = named && mappedInput.wasReleased(MappedInputManager::Button::Confirm);
  const bool tapped = route && route.event.action == GameMatchView::ACTION_PASS;
  if (tapped || confirmed) {
    app.clearTapFlash();  // the tap leaves this screen
    handle(MatchEvent::Tap);
    return;
  }
  if (state != MatchState::Result) return;
  // Result shows the mover's own frame, which a tap queued behind the move may still change.
  const uint32_t frame = vm->frameGen();
  if (frame != shownFrame && frame != renderedFrame.load(std::memory_order_acquire)) {
    shownFrame = frame;
    requestUpdate();
  }
}

void GameMatchActivity::choose(const GameCore::MatchMenu& menu, const int index) {
  if (index < 0 || index >= menu.count) return;
  app.clearTapFlash();  // the choice leaves this view
  handle(menu.events[index]);
}

uint32_t GameMatchActivity::frameAt(const uint32_t atMs) const {
  std::lock_guard<std::mutex> lock(pushMutex);
  // Wrap-safe, as millis() is: before the last push completed, the frame before it was on the panel.
  if (lastPush.doneMs != 0 && static_cast<int32_t>(atMs - lastPush.doneMs) < 0) return lastPush.before;
  return frameDisplayed.load(std::memory_order_acquire);
}

GameTouch::Gesture GameMatchActivity::readGesture() {
  GameTouch::Gesture gesture;
  // Canvas taps and long presses bypass the FreeInkUI interaction table (AD-20).
  // Consuming a long press suppresses the rest of the contact, so its lift is no tap.
  const auto snap = touchSnapshotFrom(mappedInput, /*withLongPress=*/true);
  // The first pass that sees a finger down (isScreenTouchHeld, from the contact's first sample; MappedInputManager's
  // touch-down waits 90 ms and drops a contact that slid) latches the frame on the panel, which the touch is posted
  // with when it ends.
  if ((snap.touchHeld || snap.touchPressed) && !touchDownLatched) {
    touchDownFrame = frameDisplayed.load(std::memory_order_acquire);
    touchDownMs = static_cast<uint32_t>(millis());
    touchDownLatched = true;
  }
  // Whether a finger is still down on this pass; the latch is freed on any pass without one (loopPlaying reads this
  // pass's latch first): the lift, with a gesture or none, a long press that suppressed the rest of the contact, or a
  // contact that ended where this loop did not see it (the light panel's swipe, a state the loop left).
  contactHeld = snap.touchHeld;
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
    case MatchState::Result:
      renderView(state);
      return;
    case MatchState::HandOff:
      renderHandOff();
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
  // `started` is read once, before anything is drawn: the frame drawn below is that round's or a later one's.
  // `served` likewise, for a hidden pass match: until the VM has served the hand-off's request, the front frame may be
  // the last seat's, which must never follow the hand-off screen; once it has, the frame is the next seat's or a later
  // one of that seat's.
  const uint32_t started = vm->roundsStarted();
  const uint32_t served = vm->seatShownRequest();
  if (started < roundsStartedAwaited.load() || served < seatAwaited.load()) {
    viewOnScreen = true;
    return;
  }
  // The match asks for a render only for a frame no render has taken yet, so a
  // render without one is a repaint after something else drew (an overlay such
  // as the light panel closed): the screen no longer shows the frame. The VM may publish a newer one before drawFront
  // takes the front, which is why the frame on the panel is the number drawFront reports, not this. Read before the
  // view's clearScreen below on purpose: that clear is the seam where PassMatchTest's frame-race test publishes, so
  // moving this read below it would leave the test passing whatever frameDisplayed stored.
  const uint32_t frame = vm->frameGen();
  if (viewOnScreen) {
    // A view covered the canvas; a game that never clears would keep its pixels.
    renderer.clearScreen();
    replay.forceFull();
    viewOnScreen = false;
  }
  if (frame == renderedFrame.load(std::memory_order_relaxed)) replay.forceFull();
  renderedFrame.store(frame, std::memory_order_release);
  // Lock order: RenderLock (held), then the frame mutex inside drawFront; the
  // refresh runs after the mutex is released so the VM can publish during it.
  uint32_t taken = frame;  // drawFront's number of the frame it took; this one when there is none
  if (vm->drawFront(renderer, viewport, replay, &taken)) {
    const auto pushBegan = static_cast<uint32_t>(millis());
    renderer.displayBuffer(replay.refreshMode());
    LOG_INF("GAME", "%s: frame %u pushed in %u ms", manifest.id, static_cast<unsigned>(taken),
            static_cast<unsigned>(millis()) - pushBegan);
    panel = Panel::Seat;
    // displayBuffer returns once the panel's refresh has completed (the blocking push), so a touch down before now was
    // made under the frame before this one (frameAt).
    std::lock_guard<std::mutex> lock(pushMutex);
    lastPush = LastPush{frameDisplayed.load(std::memory_order_relaxed), static_cast<uint32_t>(millis())};
  }
  // Every way out of here past the gate stores these, a frame replay skipped as identical to the one on screen included
  // (which the first frame of a round never is, on a screen cleared for it): the loop drops gestures until it does, so
  // a return above this line would drop them for good. Release: the frame's drawing is behind it for the loop task
  // that acquires it. The frame's number first, so a loop that sees the counts posts its touches with it.
  frameDisplayed.store(taken, std::memory_order_release);
  roundsDisplayed.store(started, std::memory_order_release);
  seatDisplayed.store(served, std::memory_order_release);
  // The first seat frame after "I'm ready" is on the panel: a touch is tagged by the frame it was made under again.
  firstFramePushing.store(false, std::memory_order_release);
}

bool GameMatchActivity::canvasUnderView(const MatchState state) const {
  // The error view stands alone.
  if (state == MatchState::Error || !vm) return false;
  // Paused from the hand-off: the device is between players, and no seat's frame may show.
  if (state == MatchState::Paused && resumesTo.load() == MatchState::HandOff) return false;
  // In the Play-again gap (every mode) every frame is the last round's: a pause menu opened there sits on a cleared
  // screen, as renderCanvas leaves the screen in the gap. Once the round's first frame is published, the loop redraws
  // the menu (gapWhenPaused) with the canvas back under it.
  // Only after a Play again (awaited passes 1, as in pauseInGap): a menu over the match's first round is redrawn by no
  // one.
  const uint32_t awaited = roundsStartedAwaited.load();
  if (awaited > 1 && vm->roundsStarted() < awaited) return false;
  // Before the next seat's frame is published the front frame may be the last seat's.
  return vm->seatShownRequest() >= seatAwaited.load();
}

void GameMatchActivity::renderHandOff() {
  if (!vm) return;
  // The screen names the round's first turn seat, which the VM knows once the round has begun: until then nothing is
  // pushed, the screen before stays, and no routing is published, so nothing passes (the loop asks again).
  if (vm->turnAnnouncements() < announcementAwaited.load()) {
    handOffHeldBack.store(true, std::memory_order_release);
    return;
  }
  handOffHeldBack.store(false);
  handOffSeat = vm->passedTo();
  const auto pushBegan = static_cast<uint32_t>(millis());
  // No game command: the runtime's own screen on a cleared one (no header, no status strip), so none of the last seat's
  // frame stays on the panel.
  replay.drawBlank(renderer);
  // The title screen's splash band, drawn as the title screen draws it: the page centred and clipped, else the icon.
  GameSplashLayout::drawBand(renderer, picture);
  // "Player N's turn" and the "I'm ready" button under it (buildHandOffView). Its routing is published before the push,
  // as Result's is, so a tap made during the push passes the screen.
  viewState = MatchState::HandOff;
  renderUi();
  // handle() may have closed routing after this render read its state.
  if (shown.load() != MatchState::HandOff) closeRouting();
  // In full when the hand-off screen replaces anything else (R4); a repaint of the one already on the panel (after the
  // light panel closed, or any other render in HandOff) has nothing of a seat's frame left to clear.
  renderer.displayBuffer(panel == Panel::Blank ? HalDisplay::FAST_REFRESH : HalDisplay::FULL_REFRESH);
  panel = Panel::Blank;
  LOG_INF("GAME", "%s: hand-off screen pushed in %u ms", manifest.id, static_cast<unsigned>(millis()) - pushBegan);
  // The next seat's frame is drawn on a cleared screen in full.
  viewOnScreen = true;
}

void GameMatchActivity::renderView(const MatchState state) {
  renderer.clearScreen();
  // The menus and the Result banner sit over the last frame (canvasUnderView says when there is none to show).
  const bool canvas = canvasUnderView(state);
  if (canvas) {
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
  const bool menu = state != MatchState::Error && state != MatchState::Result;
  // The Result banner has no button hints: its whole screen is the tap, and Confirm does the same.
  if (state != MatchState::Result) {
    const char* back = "";  // Back does nothing in the end-of-round menu
    if (state == MatchState::Paused) back = tr(STR_GAMES_RESUME);
    if (state == MatchState::Error) back = tr(STR_BACK);
    const auto labels = mappedInput.mapLabels(back, menu ? tr(STR_SELECT) : "", menu ? tr(STR_DIR_UP) : "",
                                              menu ? tr(STR_DIR_DOWN) : "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  const auto pushBegan = static_cast<uint32_t>(millis());
  renderer.displayBuffer(state == MatchState::Error ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
  LOG_INF("GAME", "%s: %s screen pushed in %u ms", manifest.id, MatchLifecycle::name(state),
          static_cast<unsigned>(millis()) - pushBegan);
  // Seat 0's frame under the Over menu is everyone's, so only a view over another seat's frame counts as one.
  panel = canvas && state != MatchState::Over ? Panel::Seat : Panel::Other;
}

void GameMatchActivity::viewScreen(UiScreen& screen, void* user) {
  auto* self = static_cast<GameMatchActivity*>(user);
  // Read here, at the callback, as the build used to: viewState, selected, passTo, handOffSeat, and the gap.
  GameMatchView::Input input;
  input.state = self->viewState;
  input.title = self->manifest.name;
  input.focused = self->selected.load();
  input.errorHeadline = self->errorHeadline;
  input.errorDetail = self->errorDetail;
  input.pauseInGap = input.state == MatchState::Paused && self->pauseInGap();
  input.passTo = self->passTo.load();
  input.handOffSeat = self->handOffSeat;
  self->views.build(screen, self->renderer, input);
}

bool GameMatchActivity::pauseInGap() const {
  // roundsStartedAwaited passes 1 only with a Play again: before a match's first frame no round is "next".
  const uint32_t awaited = roundsStartedAwaited.load();
  return vm && awaited > 1 && resumesTo.load() == MatchState::Playing && vm->roundsStarted() < awaited;
}

#endif  // FREEINK_CAP_GAMES
