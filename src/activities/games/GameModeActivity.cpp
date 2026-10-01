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
  }
  return "?";
}

}  // namespace

GameConfirmDialog::Answer GameConfirmDialog::readButtons(const MappedInputManager& input) {
  using Button = MappedInputManager::Button;
  if (input.wasReleased(Button::Back)) return Answer::Cancel;
  if (input.wasReleased(Button::Up) || input.wasReleased(Button::Left) || input.wasReleased(Button::NavPrevious)) {
    focus = 0;
    return Answer::Repaint;
  }
  if (input.wasReleased(Button::Down) || input.wasReleased(Button::Right) || input.wasReleased(Button::NavNext)) {
    focus = 1;
    return Answer::Repaint;
  }
  if (input.wasReleased(Button::Confirm)) return focus == 1 ? Answer::Confirm : Answer::Cancel;
  return Answer::None;
}

GameConfirmDialog::Answer GameConfirmDialog::answerTap(const fui::ActionEvent& event) {
  focus = event.value == 1 ? 1 : 0;
  return focus == 1 ? Answer::Confirm : Answer::Cancel;
}

void GameConfirmDialog::build(UiAppHost::UiScreen& screen, const GfxRenderer& renderer, const fui::ActionId action,
                              const char* title, const char* headline, const char* message, const char* actionLabel) {
  fui::DialogOption options[2];
  options[0].label = tr(STR_CANCEL);
  options[1].label = actionLabel;
  for (int i = 0; i < 2; ++i) {
    options[i].action = action;
    options[i].value = static_cast<int16_t>(i);
    options[i].state = focus == i ? fui::StateFocused : fui::StateNormal;
  }
  props.title = title;
  props.headline = headline;
  props.message = message;
  props.options = options;
  props.optionCount = 2;
  props.verticalOptions = true;
  props.titleText = screen.theme().smallText;
  props.titleText.bold = true;
  props.headlineText = screen.theme().bodyText;
  props.headlineText.maxLines = 2;  // a long name wraps; the dialog grows to fit
  props.messageText = screen.theme().smallText;
  props.messageText.maxLines = 2;
  props.buttonText = screen.theme().smallText;
  props.inputMask = fui::InputTouch;  // physical buttons stay in readButtons()
  // A framed panel, as OptionPopup draws it.
  const auto& metrics = UITheme::getInstance().getMetrics();
  props.styles = fui::defaultPopupStyles();
  props.styles.normal.border = fui::Paint::solid(fui::Color::Black);
  props.styles.normal.borderWidth = static_cast<uint8_t>(metrics.popupFrameThickness);
  props.styles.normal.radius = static_cast<uint8_t>(metrics.popupCornerRadius);
  props.styles.selected = props.styles.normal;
  props.styles.focused = props.styles.normal;
  props.styles.active = props.styles.normal;
  props.styles.disabled = props.styles.normal;

  const fui::Rect body = screen.body();
  int16_t width = static_cast<int16_t>(renderer.getScreenWidth() * 3 / 4);
  if (width > body.width) width = body.width;
  const int16_t height = fui::optionDialogHeight(screen.target(), props, width);
  fui::optionDialog(screen.frame(), fui::centeredRect(body, fui::Size{width, height}), props);
}

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
  hasSave = found != SaveState::None;
  buildRows();
}

void GameModeActivity::buildRows() {
  // Static text and fixed arrays: built once a visit rather than on every buildScreen().
  rowCount = 0;
  if (hasSave) {
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

  if (confirmRow >= 0) {
    buildConfirmDialog(screen);  // the list is not built under it, so none of its rows takes a touch
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

void GameModeActivity::startNew(const RowKind kind) {
  // RowKind's mode values index MODE_TEXTS, and Continue is past its end.
  static_assert(static_cast<size_t>(RowKind::Continue) == MAX_MODES, "Continue follows the three modes");
  static_assert(MODE_TEXTS[static_cast<size_t>(RowKind::Solo)].bit == Manifest::MODE_SOLO, "RowKind::Solo");
  static_assert(MODE_TEXTS[static_cast<size_t>(RowKind::Pass)].bit == Manifest::MODE_PASS, "RowKind::Pass");
  static_assert(MODE_TEXTS[static_cast<size_t>(RowKind::Nearby)].bit == Manifest::MODE_NEARBY, "RowKind::Nearby");
  if (kind == RowKind::Continue) {
    LOG_ERR("GAME", "Not a New row of %s: Continue", manifest.id);
    requestUpdate();
    return;
  }
  const ModeText& mode = MODE_TEXTS[static_cast<size_t>(kind)];
  GameCore::Roster roster = GameCore::Roster::solo();
  switch (mode.mode) {
    case GameCore::Mode::Solo:
      LOG_INF("GAME", "Mode solo picked for %s", manifest.id);
      break;
    case GameCore::Mode::Pass: {
      // The fewest seats a pass match can have; the seat choice is deferred (deferred-work.md, ## e5-inception).
      const uint8_t seats = GameCore::passSeats(manifest.seatsMin, manifest.seatsMax, gameHostCaps().maxSeats);
      if (seats == 0) {
        LOG_ERR("GAME", "Cannot start %s in pass: seats %d..%d leave no pass match on this host", manifest.id,
                static_cast<int>(manifest.seatsMin), static_cast<int>(manifest.seatsMax));
        requestUpdate();  // the tap moved the selection here (or the confirmation closed); show it
        return;
      }
      roster = GameCore::Roster::pass(seats);
      LOG_INF("GAME", "Mode pass picked for %s: %u seats", manifest.id, static_cast<unsigned>(seats));
      break;
    }
    case GameCore::Mode::Nearby:
      // GameMatchActivity cannot run nearby until epic-play-nearby, so this row starts a solo match.
      LOG_INF("GAME", "Mode %s picked for %s: the match plays solo until it can run %s", mode.log, manifest.id,
              mode.log);
      break;
  }
  startMatch(roster, false);
}

void GameModeActivity::startResume() {
  // The match plays the roster the save records, whatever this passes, and stops in its error view, the file kept, on
  // a save it cannot read or only another host can start (GameMatchActivity::seedResume). This roster is played when
  // the match's load finds no usable save (it went bad or was removed after the screen peeked), as a new match: pass
  // with the fewest seats for a game that starts pass; solo for a game that starts only solo, or that also starts solo
  // when no pass match fits this host; and a pass-only game with no pass seats starts nothing.
  GameCore::Roster roster = GameCore::Roster::solo();
  if ((modes & Manifest::MODE_PASS) != 0) {
    const uint8_t seats = GameCore::passSeats(manifest.seatsMin, manifest.seatsMax, gameHostCaps().maxSeats);
    if (seats > 0) {
      roster = GameCore::Roster::pass(seats);
    } else if ((modes & Manifest::MODE_SOLO) == 0) {
      LOG_ERR("GAME", "Cannot continue %s in pass: seats %d..%d leave no pass match on this host", manifest.id,
              static_cast<int>(manifest.seatsMin), static_cast<int>(manifest.seatsMax));
      requestUpdate();  // the tap moved the selection here; show it
      return;
    }
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
  confirmRow = static_cast<int8_t>(row);
  confirm.focus = 0;  // Cancel: a stray Confirm keeps the save
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

void GameModeActivity::buildConfirmDialog(UiScreen& screen) {
  // The input task can close the dialog (confirmRow = -1) while this runs on the render task: read it once.
  const int row = confirmRow;
  if (row < 0 || static_cast<size_t>(row) >= rowCount) return;
  // The headline is the mode the new game is in.
  confirm.build(screen, renderer, ACTION_CONFIRM_CHOICE, tr(STR_GAMES_NEW_OVER_SAVE_TITLE), rowItems[row].label,
                tr(STR_GAMES_NEW_OVER_SAVE), tr(STR_GAMES_NEW_GAME));
}

#endif  // FREEINK_CAP_GAMES
