#if FREEINK_CAP_GAMES

#include "GameModeActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstring>

#include "GameMatchActivity.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "games/GameHostCaps.h"
#include "games/GameSaveStore.h"

namespace fui = freeink::ui;

namespace {

using GameCore::Manifest;
using SaveState = GameSaveStore::SaveState;

static_assert(GamePkg::HASH_BYTES == GameSaveStore::PACKAGE_HASH_BYTES,
              "the title screen peeks the save with the package hash the registry read");

struct ModeText {
  uint8_t bit;
  GameCore::Mode mode;
  StrId name;
  StrId description;
  const char* log;  // the mode's name in the log
};

// The New rows' order; RowKind's first three values index it.
constexpr ModeText MODE_TEXTS[GameModeActivity::MAX_MODES] = {
    {Manifest::MODE_SOLO, GameCore::Mode::Solo, StrId::STR_GAMES_MODE_SOLO, StrId::STR_GAMES_MODE_SOLO_DESC, "solo"},
    {Manifest::MODE_PASS, GameCore::Mode::Pass, StrId::STR_GAMES_MODE_PASS, StrId::STR_GAMES_MODE_PASS_DESC, "pass"},
    {Manifest::MODE_NEARBY, GameCore::Mode::Nearby, StrId::STR_GAMES_MODE_NEARBY, StrId::STR_GAMES_MODE_NEARBY_DESC,
     "nearby"},
};

// What peek() found, in the log.
const char* saveName(const SaveState state) {
  switch (state) {
    case SaveState::None:
      return "none";
    case SaveState::Valid:
      return "valid";
    case SaveState::Unreadable:
      return "unreadable";
    case SaveState::Unstartable:
      return "unstartable";
  }
  return "?";
}

}  // namespace

GameModeActivity::GameModeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                   const GameRegistry::Entry& game)
    : UiListActivity(NAME, renderer, mappedInput), manifest(game.manifest), modes(game.check.modes) {
  std::memcpy(pkgHash, game.pkgHash, sizeof(pkgHash));
}

void GameModeActivity::onEnter() {
  UiListActivity::onEnter();
  app.on(ACTION_CONFIRM_CHOICE, &GameModeActivity::onConfirmChoice, this);
  confirmRow = -1;
  // The screen's peek: the rows say whether there is a save, and no row is drawn from the card.
  const SaveState found = GameSaveStore::peek(manifest, pkgHash, gameHostCaps());
  LOG_DBG("GAME", "Title screen of %s: save %s", manifest.id, saveName(found));
  // The render task may draw this screen while onEnter runs, on a render notification left from the previous screen
  // (ActivityManager has already made this one current), and it reads the rows.
  RenderLock lock(*this);
  // A save this host cannot start gets no Continue, but New still asks before it replaces the file.
  canContinue = found == SaveState::Valid || found == SaveState::Unreadable;
  hasSave = found != SaveState::None;
  buildRows();
}

void GameModeActivity::buildRows() {
  // Static text and fixed arrays: built once a visit rather than on every buildScreen().
  rowCount = 0;
  if (canContinue) {
    // An Unreadable save gets its row too: hiding it would offer only New rows, over what may be a good save.
    fui::ListItem item;
    item.label = tr(STR_GAMES_CONTINUE);
    item.subtitle = tr(STR_GAMES_CONTINUE_DESC);
    item.actionValue = static_cast<int16_t>(rowCount);
    rowKind[rowCount] = RowKind::Continue;
    rowItems[rowCount] = item;
    ++rowCount;
  }
  for (size_t kind = 0; kind < MAX_MODES; ++kind) {
    const ModeText& mode = MODE_TEXTS[kind];
    if ((modes & mode.bit) == 0) continue;
    fui::ListItem item;
    item.label = I18N.get(mode.name);
    item.subtitle = I18N.get(mode.description);
    item.actionValue = static_cast<int16_t>(rowCount);
    rowKind[rowCount] = static_cast<RowKind>(kind);
    rowItems[rowCount] = item;
    ++rowCount;
  }
}

void GameModeActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the header band GUI.drawHeader paints.
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // The loop task opens and closes the question while this runs on the render task: read the row once.
  const int confirming = confirmRow.load(std::memory_order_acquire);
  if (confirming >= 0) {
    buildConfirmDialog(screen, confirming);  // the list is not built under it, so none of its rows takes a touch
    return;
  }
  fui::ListProps props;
  props.items = rowItems;
  props.count = static_cast<uint16_t>(rowCount);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.subtitleText = screen.theme().smallText;
  props.subtitleText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void GameModeActivity::activateIndex(const int index) {
  if (index < 0 || static_cast<size_t>(index) >= rowCount) return;
  const RowKind kind = rowKind[index];
  if (kind == RowKind::Continue) {
    startResume();
  } else if (hasSave) {
    openConfirm(index);  // a New match replaces the save: ask first
  } else {
    startNew(kind);
  }
}

bool GameModeActivity::rosterFor(const RowKind kind, GameCore::Roster& roster) const {
  // RowKind's mode values index MODE_TEXTS, and Continue is past its end.
  static_assert(static_cast<size_t>(RowKind::Continue) == MAX_MODES, "Continue follows the three modes");
  static_assert(MODE_TEXTS[static_cast<size_t>(RowKind::Solo)].bit == Manifest::MODE_SOLO, "RowKind::Solo");
  static_assert(MODE_TEXTS[static_cast<size_t>(RowKind::Pass)].bit == Manifest::MODE_PASS, "RowKind::Pass");
  static_assert(MODE_TEXTS[static_cast<size_t>(RowKind::Nearby)].bit == Manifest::MODE_NEARBY, "RowKind::Nearby");
  if (kind == RowKind::Continue) return false;
  roster = GameCore::Roster::solo();
  switch (MODE_TEXTS[static_cast<size_t>(kind)].mode) {
    case GameCore::Mode::Solo:
      return true;
    case GameCore::Mode::Pass: {
      // The fewest seats a pass match can have; the seat choice is deferred (deferred-work.md, ## e5-inception).
      const uint8_t seats = GameCore::passSeats(manifest.seatsMin, manifest.seatsMax, gameHostCaps().maxSeats);
      if (seats == 0) return false;
      roster = GameCore::Roster::pass(seats);
      return true;
    }
    case GameCore::Mode::Nearby:
      // GameMatchActivity cannot run nearby until epic-play-nearby, so this row starts a solo match.
      return true;
  }
  return false;
}

void GameModeActivity::startNew(const RowKind kind) {
  GameCore::Roster roster;
  if (!rosterFor(kind, roster)) {
    if (kind == RowKind::Continue) {
      LOG_ERR("GAME", "Not a New row of %s: Continue", manifest.id);
    } else {  // only pass can fail (rosterFor)
      LOG_ERR("GAME", "Cannot start %s in pass: seats %d..%d leave no pass match on this host", manifest.id,
              static_cast<int>(manifest.seatsMin), static_cast<int>(manifest.seatsMax));
    }
    requestUpdate();  // the tap moved the selection here (or the confirmation closed); show it
    return;
  }
  const ModeText& mode = MODE_TEXTS[static_cast<size_t>(kind)];
  switch (mode.mode) {
    case GameCore::Mode::Solo:
      LOG_INF("GAME", "Mode solo picked for %s", manifest.id);
      break;
    case GameCore::Mode::Pass:
      LOG_INF("GAME", "Mode pass picked for %s: %u seats", manifest.id, static_cast<unsigned>(roster.seats));
      break;
    case GameCore::Mode::Nearby:
      LOG_INF("GAME", "Mode %s picked for %s: the match plays solo until it can run %s", mode.log, manifest.id,
              mode.log);
      break;
  }
  startMatch(roster, false);
}

void GameModeActivity::startResume() {
  // The match plays the roster the save records, whatever this passes, and stops in its error view, the file kept, on
  // a save it cannot read or this host cannot start (GameMatchActivity::seedResume). This roster is played when the
  // match's load finds no usable save (it went bad or was removed after the screen peeked), as a new match: the first
  // New row's that can start, in the rows' order (solo, pass, nearby), so a game that starts solo starts solo, never a
  // pass match (or a hidden game's hand-off) nobody chose; with none (a pass-only game with no pass seats) nothing.
  GameCore::Roster roster;
  size_t row = 0;
  while (row < rowCount && (rowKind[row] == RowKind::Continue || !rosterFor(rowKind[row], roster))) ++row;
  if (row == rowCount) {
    LOG_ERR("GAME", "Cannot continue %s: no New row this host can start", manifest.id);
    requestUpdate();  // the tap moved the selection here; show it
    return;
  }
  LOG_INF("GAME", "Continue %s: a %s roster of %u seat(s) unless the save says otherwise", manifest.id,
          GameCore::modeName(roster.mode), static_cast<unsigned>(roster.seats));
  startMatch(roster, true);
}

void GameModeActivity::startMatch(const GameCore::Roster& roster, const bool resume) {
  app.clearTapFlash();  // the row leaves this screen
  auto match =
      makeUniqueNoThrow<GameMatchActivity>(renderer, mappedInput, manifest, roster,
                                           resume ? GameMatchActivity::Start::Resume : GameMatchActivity::Start::New);
  if (!match) {
    LOG_ERR("GAME", "OOM: %u byte match activity", static_cast<unsigned>(sizeof(GameMatchActivity)));
    requestUpdate();  // the tap flash was cleared; repaint this screen rather than leave a stale frame
    return;
  }
  activityManager.replaceActivity(std::move(match));
}

void GameModeActivity::onBackButton() {
  app.clearTapFlash();
  // Pops to the launcher that pushed this screen (its selection and listing are as they were).
  finish();
}

bool GameModeActivity::handleCustomInput() { return confirmRow >= 0 && handleConfirmInput(); }

// The hit rects are the last render's: after the confirmation opens they are still the list's rows until the next
// render, and a tap on one must not start a match or move the selection.
void GameModeActivity::onRowAction(const fui::ActionEvent& event) {
  if (confirmRow >= 0) return;
  UiListActivity::onRowAction(event);
}

void GameModeActivity::openConfirm(const int row) {
  app.clearTapFlash();
  // Cancel first, so a stray Confirm keeps the save; then the row, released after it, so a render that sees the
  // question open draws Cancel focused.
  confirm.focus.store(0);
  confirmRow.store(static_cast<int8_t>(row), std::memory_order_release);
  requestUpdate();
}

void GameModeActivity::closeConfirm() {
  app.clearTapFlash();
  confirmRow = -1;
  requestUpdate();
}

bool GameModeActivity::handleConfirmInput() {
  // Touch: render() registered the dialog's buttons; onConfirmChoice runs for a tap on one.
  const auto route = UiAppHost::routeTouch(mappedInput);
  if (route.routed && app.invalidated()) requestUpdate();
  if (route) return true;
  answerConfirm(confirm.readButtons(mappedInput));
  return true;  // the confirmation owns every pass while it is open
}

void GameModeActivity::answerConfirm(const GameConfirmDialog::Answer answer) {
  switch (answer) {
    case GameConfirmDialog::Answer::None:
      break;
    case GameConfirmDialog::Answer::Repaint:
      requestUpdate();
      break;
    case GameConfirmDialog::Answer::Cancel:
      closeConfirm();
      break;
    case GameConfirmDialog::Answer::Confirm:
      confirmNew();
      break;
  }
}

void GameModeActivity::onConfirmChoice(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<GameModeActivity*>(user);
  if (self->confirmRow < 0) return;  // closed: a tap routed by the table built while it was open
  self->answerConfirm(self->confirm.answerTap(event));
}

void GameModeActivity::confirmNew() {
  const int row = confirmRow;
  // Closed before the start: a start that cannot go ahead (no pass seats, no memory) repaints the list, not the dialog.
  confirmRow = -1;
  if (row < 0 || static_cast<size_t>(row) >= rowCount || rowKind[row] == RowKind::Continue) {
    app.clearTapFlash();
    requestUpdate();
    return;
  }
  startNew(rowKind[row]);
}

void GameModeActivity::buildConfirmDialog(UiScreen& screen, const int row) {
  if (row < 0 || static_cast<size_t>(row) >= rowCount) return;
  // The headline is the mode the new game is in.
  confirm.build(screen, renderer, ACTION_CONFIRM_CHOICE, tr(STR_GAMES_NEW_OVER_SAVE_TITLE), rowItems[row].label,
                tr(STR_GAMES_NEW_OVER_SAVE), tr(STR_GAMES_NEW_GAME));
}

#endif  // FREEINK_CAP_GAMES
