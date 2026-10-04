#pragma once

#include <Manifest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "GameConfirmDialog.h"
#include "GamePicture.h"
#include "activities/UiListActivity.h"
#include "games/GameRegistry.h"
#include "games/GameSaveStore.h"

namespace GameCore {
struct Roster;
}

// A game's title screen (Home → Games → a game; spine AD-22 as amended 2026-10-02; DESIGN.md and EXPERIENCE.md,
// Title-screen splash and menu rows), headed by the game's name. Every launcher row of a game this host can start
// pushes it, so Back pops to the launcher as it was. Under the header, a 480 x 480 splash band shows the game's
// title.bmp (at most 480 x 480, centred and clipped), else its icon at 128 px centred (GamePicture); it is not a
// target. Under the band, the rows, top to bottom: Continue ("Load the previous game") when GameSaveStore::peek(game,
// pkgHash, host) finds a save it can start, or one it could not check (Unreadable: a card or heap fault, so the screen
// never offers only New over what may be a good save); New game, whose second line is the current mode and each
// setting's chosen value joined by " · "; and Options, only when the host can start more than one of the game's modes
// or the game declares settings. A save this host cannot start (Unstartable: a mode or seat count it cannot start, or
// an unknown mode byte) gets no Continue, but it is a save all the same. The save, the remembered choices (prefs.bin),
// the manifest's settings, and the pictures are read once, when the screen opens, never while a row is drawn. The
// selection starts on the first row, so a Confirm with nothing moved resumes when there is a save to continue.
//
// The current mode is the remembered one when this host can start it, else the manifest's default_mode when it can,
// else the first it can in solo, pass, nearby order (Manifest::startMode; pass counts only with a pass seat count this
// host fits, onEnter), and each setting's value the remembered one when the manifest still declares it, else its
// default (GameSaveStore::resolvePrefs). Options (GameOptionsActivity, pushed for its result) cycles them in place;
// when it closes with a change, prefs.bin is written, and the New game line shows the new choice with the Options row
// selected. Options left by a Replace (the Home gesture, sleep) runs no result handler, so onExit() writes the change
// still unwritten (optionsChanged; AD-17 as amended 2026-10-04, e6pre-11), once, and nothing when Back already wrote it
// or nothing changed. prefs.bin is written too when New game
// starts a mode other than the one the file holds (a missing file holds none; one that would not read is left alone). A
// failed write is logged, and the choice lasts until the screen closes. Nothing is written, and no New match starts
// (the New game row says why), while the manifest's settings could not be read: a write would wipe their values, and a
// match would lack the ctx.settings its manifest declares. Continue still starts (a resume runs no setup); its match
// ends in the error view at a Play again (GameMatchActivity::handle). An Options change after a prefs.bin that would
// not read reads it again before writing, so the choices the player did not touch keep its values when it reads now.
// New game does not write over a remembered mode this host does not offer.
//
// A tap or Confirm on Continue or New game replaces this screen with the game's match, given the chosen settings
// (GameCore::SettingValues, ctx.settings). Continue resumes the save (Start::Resume): the match plays the roster the
// save records (GameMatchActivity::seedResume), and one it cannot read or start stops in its error view with the file
// kept. The roster Continue passes is played only when the match finds no usable save (a new match): the first mode
// this host can start in solo, pass, nearby order (rosterFor), so solo for a game that starts solo. New game with no
// save starts the current mode at once: solo; pass as an open pass match with the fewest seats it can have
// (passSeats; the seat choice is deferred, deferred-work.md ## e5-inception); nearby solo until epic-play-nearby. New
// game over any save (Unstartable included) asks first, Cancel focused ("Start a new game?" / the current mode's name
// / "This replaces the saved game."), since the new match replaces the one save the game has; Cancel or Back keeps
// it, and New game starts the match.
class GameModeActivity final : public UiListActivity {
 public:
  // The activity's name, which ActivityManager::goHome maps to Home's Games row (ledger row 5): one constant, so the
  // mapping and the constructor cannot disagree.
  static constexpr const char* NAME = "GameMode";

  // The rows there can be: Continue, New game, Options.
  static constexpr size_t MAX_ROWS = 3;
  // What joins the names in a modes line and the values in New game's line.
  static constexpr const char* JOINER = " \xC2\xB7 ";
  // Room for New game's second line: a mode's name and every setting's value, each after the joiner.
  static constexpr size_t NEW_GAME_LINE_BYTES = 160;

  // The name of the mode whose Manifest::Mode bit is `modeBit` ("Solo", "Pass and play", "Play nearby"), as tr()
  // gives it; empty for any other value.
  static const char* modeName(uint8_t modeBit);
  // Writes the names of the modes `modes` (Manifest::Mode bits) holds, in solo, pass, nearby order, joined by JOINER,
  // into `out` (cut to `size`; empty for none). The launcher's modes line.
  static void writeModesLine(uint8_t modes, char* out, size_t size);
  // The next of `modes`' bits after `current` in solo, pass, nearby order, wrapping; the first when `current` is not
  // one of them; 0 when `modes` has none. What Options' Mode row cycles to.
  static uint8_t nextMode(uint8_t current, uint8_t modes);

  // `game` is a launcher entry this host can start (check.ok()); its manifest, check modes, and package hash are
  // copied.
  GameModeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const GameRegistry::Entry& game);

 private:
  // What a row does.
  enum class RowKind : uint8_t { Continue, NewGame, Options };

  int listCount() const override { return static_cast<int>(rowCount); }
  const char* headerTitle() const override { return manifest.name; }
  void onEnter() override;
  // Writes an Options change no result handler saw (a Replace); RenderLock is already held by the caller.
  void onExit() override;
  void drawChrome() override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  bool handleCustomInput() override;
  void onRowAction(const freeink::ui::ActionEvent& event) override;

  // Reads the settings from the installed manifest (the registry's entry keeps only their count); none, logged, when
  // it cannot, and then no match starts (startMatch).
  void loadSettings();
  // The rows for the save peek() found, and New game's line for the current choices.
  void buildRows();
  void writeNewGameLine();
  // The roster a match in mode `modeBit` (a Manifest::Mode bit) plays: solo; pass with the fewest seats it can have;
  // nearby solo until epic-play-nearby. False when that mode cannot start: no mode, or pass with no seat count this
  // host fits. Logs nothing: Continue's search tries modes that may not start, so the callers log.
  bool rosterFor(uint8_t modeBit, GameCore::Roster& roster) const;
  // Says on the New game row that the settings were not read, so nothing started. Takes the render lock.
  void showSettingsNotice();
  // Starts a New match in the current mode.
  void startNew();
  // Starts the match from the save.
  void startResume();
  // Replaces this screen with the match; repaints this screen, logged, when the activity cannot be allocated, or, for a
  // New match only, when the manifest's settings were not read (loadSettings; showSettingsNotice). Continue starts. A
  // New match in a mode other than the one prefs.bin holds writes it first.
  void startMatch(const GameCore::Roster& roster, bool resume);
  // Writes the current choices as prefs.bin (logged when it cannot, and skipped, logged, when the manifest's settings
  // were not read); `saved` follows a write that succeeds. A prefs.bin that was Unreadable is read again first, and
  // when it reads, the choices still as the screen opened them take its values.
  void rememberChoices();
  // Pushes Options for the current choices, and handles its close (onOptionsClosed).
  void openOptions();
  void onOptionsClosed();

  // The New-over-a-save confirmation: opened on New game, answered by Cancel (Back), or New game.
  void openConfirm(int row);
  void closeConfirm();
  void confirmNew();
  bool handleConfirmInput();
  void answerConfirm(GameConfirmDialog::Answer answer);
  // The question about New game in the current mode, as buildScreen read it.
  void buildConfirmDialog(UiScreen& screen);
  static void onConfirmChoice(const freeink::ui::ActionEvent& event, void* user);

  static constexpr freeink::ui::ActionId ACTION_CONFIRM_CHOICE = ACTION_USER;

  GameCore::Manifest manifest;
  // Manifest::Mode bits: CheckResult::modes of the game's check on this host, less pass (onEnter) when this host fits
  // no pass seat count, unless pass is the only one. What the current mode may be and what Options offers.
  uint8_t modes = 0;
  uint8_t pkgHash[GamePkg::HASH_BYTES] = {};
  // What peek() found when the screen opened: a save to continue (Valid or Unreadable: the Continue row), and any save
  // at all (those or Unstartable: New game asks before it replaces the file).
  bool canContinue = false;
  bool hasSave = false;
  // New game was tapped while the settings were unread: its row's line says so (showSettingsNotice).
  bool settingsNotice = false;
  // The manifest's settings (read in onEnter), what prefs.bin held when the screen opened (or last wrote; empty for no
  // usable file), and the choices New game starts with. Options edits `choices` and sets `optionsChanged`; this screen
  // outlives it. `optionsChanged` stays set until a write is tried (onOptionsClosed, else onExit()).
  GameCore::ManifestSettings settings;
  GameSaveStore::Prefs saved;
  GameSaveStore::PrefsState prefsState = GameSaveStore::PrefsState::None;  // loadPrefs' answer, Loaded after a write
  GameSaveStore::Choices choices;
  bool optionsChanged = false;
  // The splash band's picture: title.bmp, or the icon.
  GamePicture picture;
  // The rows, built in onEnter and after Options closes, and what each one does.
  RowKind rowKind[MAX_ROWS] = {};
  freeink::ui::ListItem rowItems[MAX_ROWS] = {};
  size_t rowCount = 0;
  char newGameLine[NEW_GAME_LINE_BYTES] = {};
  // The New game row the open confirmation asks about (-1: none), and the question (its focus: 0 Cancel, 1 New game).
  // Written by the loop task, read once per build by the render task. Orders: openConfirm stores focus before it
  // publishes the row with release, and render loads the row once with acquire; closers and loop-task reads may use
  // the default order.
  std::atomic<int8_t> confirmRow{-1};
  // confirmRow as this render read it (drawChrome, then buildScreen from it). Render task only.
  int8_t renderedConfirmRow = -1;
  GameConfirmDialog confirm;
};
