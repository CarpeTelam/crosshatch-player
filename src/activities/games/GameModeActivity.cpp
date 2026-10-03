#if FREEINK_CAP_GAMES

#include "GameModeActivity.h"

#include <GameImages.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>

#include "GameMatchActivity.h"
#include "GameOptionsActivity.h"
#include "GameSplashLayout.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "games/GameHostCaps.h"

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
  const char* log;  // the mode's name in the log
};

// The modes in the order every list of them follows: solo, pass, nearby.
constexpr ModeText MODE_TEXTS[] = {
    {Manifest::MODE_SOLO, GameCore::Mode::Solo, StrId::STR_GAMES_MODE_SOLO, "solo"},
    {Manifest::MODE_PASS, GameCore::Mode::Pass, StrId::STR_GAMES_MODE_PASS, "pass"},
    {Manifest::MODE_NEARBY, GameCore::Mode::Nearby, StrId::STR_GAMES_MODE_NEARBY, "nearby"},
};

const ModeText* modeTextOf(const uint8_t bit) {
  for (const ModeText& mode : MODE_TEXTS)
    if (mode.bit == bit) return &mode;
  return nullptr;
}

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

// Appends `text` to the NUL-terminated `out` of `size` bytes, cut to fit.
void append(char* out, const size_t size, const char* text) {
  const size_t used = std::strlen(out);
  if (used + 1 >= size) return;
  snprintf(out + used, size - used, "%s", text);
}

// The installed manifest read again, for its settings: the reader holds the JSON token buffer (about 1.4 KB).
struct ManifestRead {
  GameCore::ManifestReader reader;
  GameRegistry::Entry entry;
};

}  // namespace

const char* GameModeActivity::modeName(const uint8_t modeBit) {
  const ModeText* mode = modeTextOf(modeBit);
  return mode ? I18N.get(mode->name) : "";
}

void GameModeActivity::writeModesLine(const uint8_t modes, char* out, const size_t size) {
  if (size == 0) return;
  out[0] = '\0';
  for (const ModeText& mode : MODE_TEXTS) {
    if ((modes & mode.bit) == 0) continue;
    if (out[0] != '\0') append(out, size, JOINER);
    append(out, size, I18N.get(mode.name));
  }
}

uint8_t GameModeActivity::nextMode(const uint8_t current, const uint8_t modes) {
  constexpr size_t COUNT = sizeof(MODE_TEXTS) / sizeof(MODE_TEXTS[0]);
  size_t at = COUNT - 1;  // the search starts after this: after `current`, or (from nearby) at solo when it is none
  for (size_t i = 0; i < COUNT; ++i)
    if (MODE_TEXTS[i].bit == current && (modes & current) != 0) at = i;
  for (size_t step = 1; step <= COUNT; ++step) {
    const ModeText& mode = MODE_TEXTS[(at + step) % COUNT];
    if ((modes & mode.bit) != 0) return mode.bit;
  }
  return 0;
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
  // The screen's reads: the rows say whether there is a save and what New game starts, and no row is drawn from the
  // card.
  const SaveState found = GameSaveStore::peek(manifest, pkgHash, gameHostCaps());
  LOG_DBG("GAME", "Title screen of %s: save %s", manifest.id, saveName(found));
  // The render task may draw this screen while onEnter runs, on a render notification left from the previous screen
  // (ActivityManager has already made this one current), and it reads the rows, the choices, and the picture.
  RenderLock lock(*this);
  loadSettings();
  saved = GameSaveStore::Prefs{};
  prefsState = GameSaveStore::loadPrefs(manifest.id, saved);  // anything but Loaded leaves it empty: the defaults
  // Pass counts only with a pass seat count this host fits (rosterFor), unless it is the only mode (New game then names
  // it and logs why it cannot): the current mode falls back from it, and Options does not offer it.
  GameCore::Roster roster;
  const auto others = static_cast<uint8_t>(modes & ~Manifest::MODE_PASS);
  if (others != 0 && !rosterFor(Manifest::MODE_PASS, roster)) modes = others;
  choices = GameSaveStore::resolvePrefs(saved, manifest, settings, modes);
  optionsChanged = false;
  picture.loadPage(manifest.id, "title", GameCore::TITLE_IMAGE_WIDTH, GameCore::TITLE_IMAGE_HEIGHT);
  picture.loadIcon(manifest);
  // A save this host cannot start gets no Continue, but New game still asks before it replaces the file.
  canContinue = found == SaveState::Valid || found == SaveState::Unreadable;
  hasSave = found != SaveState::None;
  buildRows();
}

void GameModeActivity::loadSettings() {
  settings = GameCore::ManifestSettings{};
  if (manifest.settingsCount == 0) return;  // nothing to read
  auto read = makeUniqueNoThrow<ManifestRead>();
  if (!read) {
    LOG_ERR("GAME", "OOM: %u B to read the settings of %s; it does not start",
            static_cast<unsigned>(sizeof(ManifestRead)), manifest.id);
    return;
  }
  // The same manifest the launcher listed, unless it changed on the card since: then its settings are not this
  // screen's, and no match starts (startMatch) rather than one with another manifest's settings or none.
  if (!GameRegistry::readGame(manifest.id, read->reader, read->entry) ||
      read->entry.manifest.settingsCount != manifest.settingsCount ||
      std::memcmp(read->entry.pkgHash, pkgHash, sizeof(pkgHash)) != 0) {
    LOG_ERR("GAME", "Cannot read the settings of %s; it does not start", manifest.id);
    return;
  }
  settings = read->reader.settings();
}

void GameModeActivity::showSettingsNotice() {
  // The New game row's line says why nothing started, until the screen closes; the render task reads the rows.
  app.clearTapFlash();  // the row did not leave this screen, as startMatch's callers expect
  RenderLock lock(*this);
  settingsNotice = true;
  buildRows();
  requestUpdate();
}

void GameModeActivity::buildRows() {
  // Static text and fixed arrays: built once a visit (and when Options closes) rather than on every buildScreen().
  rowCount = 0;
  const auto add = [this](const RowKind kind, const char* label, const char* subtitle) {
    fui::ListItem item;
    item.label = label;
    item.subtitle = subtitle;
    item.actionValue = static_cast<int16_t>(rowCount);
    rowKind[rowCount] = kind;
    rowItems[rowCount] = item;
    ++rowCount;
  };
  // An Unreadable save gets its row too: hiding it would offer only New game, over what may be a good save.
  if (canContinue) add(RowKind::Continue, tr(STR_GAMES_CONTINUE), tr(STR_GAMES_CONTINUE_DESC));
  writeNewGameLine();
  add(RowKind::NewGame, tr(STR_GAMES_NEW_GAME), settingsNotice ? tr(STR_GAMES_SETTINGS_NOT_READ) : newGameLine);
  // Options only when it has a choice to offer: a second mode this host can start, or a setting.
  const bool severalModes = (modes & (modes - 1)) != 0;
  if (severalModes || settings.count > 0) add(RowKind::Options, tr(STR_GAMES_OPTIONS), nullptr);
}

void GameModeActivity::writeNewGameLine() {
  snprintf(newGameLine, sizeof(newGameLine), "%s", modeName(choices.mode));
  for (size_t i = 0; i < settings.count; ++i) {
    const GameCore::ManifestSetting& setting = settings.settings[i];
    const uint8_t chosen = choices.valueIndex[i] < setting.count ? choices.valueIndex[i] : setting.defaultIndex;
    append(newGameLine, sizeof(newGameLine), JOINER);
    append(newGameLine, sizeof(newGameLine), setting.values[chosen]);
  }
}

void GameModeActivity::drawChrome() {
  UiListActivity::drawChrome();
  // The loop task opens and closes the question while this render runs: its row is read once, here, before the app
  // builds the screen from it (buildScreen), so the band and the dialog never disagree within one render.
  renderedConfirmRow = confirmRow.load(std::memory_order_acquire);
  // The confirmation is drawn as it is on a plain list screen, with no band behind it.
  if (renderedConfirmRow >= 0) return;
  // The splash band: title.bmp centred and clipped, else the icon centred.
  GameSplashLayout::drawBand(renderer, picture);
}

void GameModeActivity::buildScreen(UiScreen& screen) {
  fui::Insets margin = GameSplashLayout::menuMargin(renderer);
  if (renderedConfirmRow >= 0) {
    // Content: the safe area minus the header band GUI.drawHeader paints, as on a plain list screen.
    margin.top = static_cast<int16_t>(GameSplashLayout::bandTop(renderer));
    screen.setContentMarginFromScreen(margin);
    screen.spacer(static_cast<int16_t>(UITheme::getInstance().getMetrics().verticalSpacing));
    buildConfirmDialog(screen);  // the list is not built under it, so none of its rows takes a touch
    return;
  }
  // Content: the safe area under the splash band; the rows follow directly under it (GameSplashLayout::rowRect).
  screen.setContentMarginFromScreen(margin);
  fui::ListProps props;
  props.items = rowItems;
  props.count = static_cast<uint16_t>(rowCount);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  GameSplashLayout::styleMenu(screen, props);
  syncListViewport(screen, props);
  screen.list(props);
}

void GameModeActivity::activateIndex(const int index) {
  if (index < 0 || static_cast<size_t>(index) >= rowCount) return;
  switch (rowKind[index]) {
    case RowKind::Continue:
      startResume();
      break;
    case RowKind::NewGame:
      if (settings.count != manifest.settingsCount) {
        // Before the question about replacing a save: nothing would start (startMatch).
        LOG_ERR("GAME", "Cannot start %s: its settings were not read", manifest.id);
        showSettingsNotice();
      } else if (hasSave) {
        openConfirm(index);  // a New match replaces the save: ask first
      } else {
        startNew();
      }
      break;
    case RowKind::Options:
      openOptions();
      break;
  }
}

bool GameModeActivity::rosterFor(const uint8_t modeBit, GameCore::Roster& roster) const {
  const ModeText* mode = modeTextOf(modeBit);
  if (!mode) return false;
  roster = GameCore::Roster::solo();
  switch (mode->mode) {
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
      // GameMatchActivity cannot run nearby until epic-play-nearby, so this mode starts a solo match.
      return true;
  }
  return false;
}

void GameModeActivity::startNew() {
  GameCore::Roster roster;
  const ModeText* mode = modeTextOf(choices.mode);
  if (!mode) {
    LOG_ERR("GAME", "Cannot start %s: no mode this host can start", manifest.id);
    requestUpdate();  // the tap moved the selection here (or the confirmation closed); show it
    return;
  }
  if (!rosterFor(choices.mode, roster)) {  // the mode has no seat count this host fits (only pass, today)
    LOG_ERR("GAME", "Cannot start %s in %s: seats %d..%d leave no %s match on this host", manifest.id, mode->log,
            static_cast<int>(manifest.seatsMin), static_cast<int>(manifest.seatsMax), mode->log);
    requestUpdate();
    return;
  }
  switch (mode->mode) {
    case GameCore::Mode::Solo:
      LOG_INF("GAME", "Mode solo picked for %s", manifest.id);
      break;
    case GameCore::Mode::Pass:
      LOG_INF("GAME", "Mode pass picked for %s: %u seats", manifest.id, static_cast<unsigned>(roster.seats));
      break;
    case GameCore::Mode::Nearby:
      LOG_INF("GAME", "Mode %s picked for %s: the match plays solo until it can run %s", mode->log, manifest.id,
              mode->log);
      break;
  }
  startMatch(roster, false);
}

void GameModeActivity::startResume() {
  // The match plays the roster the save records, whatever this passes, and stops in its error view, the file kept, on
  // a save it cannot read or this host cannot start (GameMatchActivity::seedResume). This roster is played when the
  // match's load finds no usable save (it went bad or was removed after the screen peeked), as a new match: the first
  // mode this host can start, in solo, pass, nearby order, so a game that starts solo starts solo, never a pass match
  // (or a hidden game's hand-off) nobody chose, whatever the current mode; with none (a pass-only game with no pass
  // seats) nothing.
  GameCore::Roster roster;
  bool found = false;
  for (const ModeText& mode : MODE_TEXTS) {
    if ((modes & mode.bit) != 0 && rosterFor(mode.bit, roster)) {
      found = true;
      break;
    }
  }
  if (!found) {
    LOG_ERR("GAME", "Cannot continue %s: no mode this host can start", manifest.id);
    requestUpdate();  // the tap moved the selection here; show it
    return;
  }
  LOG_INF("GAME", "Continue %s: a %s roster of %u seat(s) unless the save says otherwise", manifest.id,
          GameCore::modeName(roster.mode), static_cast<unsigned>(roster.seats));
  startMatch(roster, true);
}

void GameModeActivity::startMatch(const GameCore::Roster& roster, const bool resume) {
  app.clearTapFlash();  // the row leaves this screen
  // Settings that were not read (loadSettings: no memory, a card fault, or a manifest changed since the launcher read
  // it) would start a New game without the ctx.settings its manifest declares: it starts nothing, writes nothing, and
  // says why on the New game row. Continue goes on: a resume runs no setup (AD-8), and the match itself ends in its
  // error view at a Play again, which would (GameMatchActivity::handle).
  if (!resume && settings.count != manifest.settingsCount) {
    LOG_ERR("GAME", "Cannot start %s: its settings were not read", manifest.id);
    showSettingsNotice();
    return;
  }
  // Continue passes the current settings too: a resume runs no setup (AD-8), so they reach only a Play again after it.
  auto match = makeUniqueNoThrow<GameMatchActivity>(
      renderer, mappedInput, manifest, roster,
      resume ? GameMatchActivity::Start::Resume : GameMatchActivity::Start::New, settings.valuesAt(choices.valueIndex));
  if (!match) {
    LOG_ERR("GAME", "OOM: %u byte match activity", static_cast<unsigned>(sizeof(GameMatchActivity)));
    requestUpdate();  // the tap flash was cleared; repaint this screen rather than leave a stale frame
    return;
  }
  // A New match in a mode other than the one prefs.bin holds makes it the remembered one (a missing file holds none),
  // unless the file's mode is one this host does not offer (a remembered pass here with no pass seats): it is kept for
  // a host that can. A file that would not read (a card fault) may be a good one: New game leaves it for an Options
  // change to replace.
  if (!resume && prefsState != GameSaveStore::PrefsState::Unreadable && choices.mode != saved.mode &&
      (saved.mode & ~modes) == 0) {
    rememberChoices();
  }
  activityManager.replaceActivity(std::move(match));
}

void GameModeActivity::rememberChoices() {
  // Settings that could not be read (loadSettings) would write no value at all, wiping every remembered one.
  if (settings.count != manifest.settingsCount) {
    LOG_ERR("GAME", "The choices for %s are not remembered: its settings were not read", manifest.id);
    return;
  }
  // A prefs.bin that would not read when the screen opened (a card fault, which may have passed) is read again first:
  // when it reads now, the choices the player left as the screen opened them (the defaults, `saved` being empty) take
  // its values, so the write keeps them; still unreadable, the choices are written as they are.
  if (prefsState == GameSaveStore::PrefsState::Unreadable) {
    const GameSaveStore::Choices opened = GameSaveStore::resolvePrefs(saved, manifest, settings, modes);
    prefsState = GameSaveStore::loadPrefs(manifest.id, saved);
    if (prefsState == GameSaveStore::PrefsState::Loaded) {
      const GameSaveStore::Choices read = GameSaveStore::resolvePrefs(saved, manifest, settings, modes);
      if (choices.mode == opened.mode) choices.mode = read.mode;
      for (size_t i = 0; i < settings.count; ++i) {
        if (choices.valueIndex[i] == opened.valueIndex[i]) choices.valueIndex[i] = read.valueIndex[i];
      }
    }
  }
  const GameSaveStore::Prefs prefs = GameSaveStore::prefsOf(choices, settings);
  if (!GameSaveStore::savePrefs(manifest.id, prefs)) {
    // savePrefs logged why; the choices stay in this screen until it closes.
    LOG_ERR("GAME", "The choices for %s are not remembered", manifest.id);
    return;
  }
  saved = prefs;
  prefsState = GameSaveStore::PrefsState::Loaded;
}

void GameModeActivity::openOptions() {
  app.clearTapFlash();  // the row leaves this screen
  optionsChanged = false;
  auto options =
      makeUniqueNoThrow<GameOptionsActivity>(renderer, mappedInput, settings, modes, choices, optionsChanged);
  if (!options) {
    LOG_ERR("GAME", "OOM: %u byte Options screen", static_cast<unsigned>(sizeof(GameOptionsActivity)));
    requestUpdate();
    return;
  }
  // The handler runs on the loop task once Options pops (Back). A Replace (the Home gesture, sleep) runs no handler,
  // and the change is not written: prefs.bin is never written from onExit() (AD-17).
  startActivityForResult(std::move(options), [this](const ActivityResult&) { onOptionsClosed(); });
}

void GameModeActivity::onOptionsClosed() {
  // The card is written here, on the loop task, once Options has closed; never from Options' onExit(). Under the lock:
  // rememberChoices may merge a re-read prefs.bin into the choices the render task reads.
  RenderLock lock(*this);
  if (optionsChanged) rememberChoices();
  optionsChanged = false;
  buildRows();  // New game's line names the new choice
  for (size_t row = 0; row < rowCount; ++row) {
    if (rowKind[row] == RowKind::Options) activeNav().requestSelection(static_cast<int>(row));
  }
  requestUpdate();
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
  if (row < 0 || static_cast<size_t>(row) >= rowCount || rowKind[row] != RowKind::NewGame) {
    app.clearTapFlash();
    requestUpdate();
    return;
  }
  startNew();
}

void GameModeActivity::buildConfirmDialog(UiScreen& screen) {
  // The second line is the mode the new game is in.
  confirm.build(screen, renderer, ACTION_CONFIRM_CHOICE, tr(STR_GAMES_NEW_OVER_SAVE_TITLE), modeName(choices.mode),
                tr(STR_GAMES_NEW_OVER_SAVE), tr(STR_GAMES_NEW_GAME));
}

#endif  // FREEINK_CAP_GAMES
