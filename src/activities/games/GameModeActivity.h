#pragma once

#include <Manifest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "GameConfirmDialog.h"
#include "activities/UiListActivity.h"
#include "games/GameRegistry.h"

namespace GameCore {
struct Roster;
}

// A game's title screen (Home → Games → a game; spine AD-22), headed by the game's name. Every launcher row of a game
// this host can start pushes it, so Back pops to the launcher as it was. Its rows, top to bottom: Continue ("Go on with
// the saved game") when GameSaveStore::peek(game, pkgHash, host) finds a save it can start, or one it could not check
// (Unreadable: a card or heap fault, so the screen never offers only New over what may be a good save); then one New
// row for each mode Manifest::check leaves for this host, in the order solo, pass and play, play nearby. A save this
// host cannot start (Unstartable: a mode or seat count it cannot start, or an unknown mode byte) gets no Continue, but
// it is a save all the same. The save is peeked once, when the screen opens, never while a row is drawn. The selection
// starts on the first row, so a Confirm with nothing moved resumes when there is a save to continue.
//
// A tap or Confirm on a row replaces this screen with the game's match. Continue resumes the save (Start::Resume): the
// match plays the roster the save records (GameMatchActivity::seedResume), and one it cannot read or start stops in its
// error view with the file kept. The roster Continue passes is played only when the match finds no usable save (a new
// match): the first New row's (rosterFor), so solo for a game that starts solo. A New row with no save starts that mode
// at once: solo; pass as an open pass match with the fewest seats it can have (passSeats; the seat choice is deferred,
// deferred-work.md ## e5-inception); nearby solo until epic-play-nearby. A New row over any save (Unstartable included)
// asks first, Cancel focused ("Start a new game?" / "This replaces the saved game."), since the new match replaces the
// one save the game has; Cancel or Back keeps it, and New game starts the match.
class GameModeActivity final : public UiListActivity {
 public:
  // The activity's name, which ActivityManager::goHome maps to Home's Games row (ledger row 5): one constant, so the
  // mapping and the constructor cannot disagree.
  static constexpr const char* NAME = "GameMode";

  // The Manifest::Mode bits there are, and so the most New rows; with the Continue row, the most rows.
  static constexpr size_t MAX_MODES = 3;
  static constexpr size_t MAX_ROWS = 1 + MAX_MODES;

  // `game` is a launcher entry this host can start (check.ok()); its manifest, check modes, and package hash are
  // copied.
  GameModeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const GameRegistry::Entry& game);

 private:
  // What a row starts: a New match in one of the three modes (in the cpp's mode table's order), or the save.
  enum class RowKind : uint8_t { Solo, Pass, Nearby, Continue };

  int listCount() const override { return static_cast<int>(rowCount); }
  const char* headerTitle() const override { return manifest.name; }
  void onEnter() override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  bool handleCustomInput() override;
  void onRowAction(const freeink::ui::ActionEvent& event) override;

  // The rows for the save peek() found and the modes check left.
  void buildRows();
  // The roster a match in the mode of row kind `kind` (not Continue) plays: solo; pass with the fewest seats it can
  // have; nearby solo until epic-play-nearby. False when that mode cannot start: Continue, or pass with no seat count
  // this host fits. Logs nothing: Continue's search tries rows that may not start, so the callers log.
  bool rosterFor(RowKind kind, GameCore::Roster& roster) const;
  // Starts a New match in the mode of row kind `kind` (not Continue).
  void startNew(RowKind kind);
  // Starts the match from the save.
  void startResume();
  // Replaces this screen with the match; repaints this screen when the activity cannot be allocated.
  void startMatch(const GameCore::Roster& roster, bool resume);

  // The New-over-a-save confirmation: opened on a New row, answered by Cancel (Back), or New game.
  void openConfirm(int row);
  void closeConfirm();
  void confirmNew();
  bool handleConfirmInput();
  void answerConfirm(GameConfirmDialog::Answer answer);
  // The question about New row `row`, as buildScreen read it.
  void buildConfirmDialog(UiScreen& screen, int row);
  static void onConfirmChoice(const freeink::ui::ActionEvent& event, void* user);

  static constexpr freeink::ui::ActionId ACTION_CONFIRM_CHOICE = ACTION_USER;

  GameCore::Manifest manifest;
  uint8_t modes = 0;  // Manifest::Mode bits: CheckResult::modes of the game's check on this host
  uint8_t pkgHash[GamePkg::HASH_BYTES] = {};
  // What peek() found when the screen opened: a save to continue (Valid or Unreadable: the Continue row), and any save
  // at all (those or Unstartable: a New row asks before it replaces the file).
  bool canContinue = false;
  bool hasSave = false;
  // The rows, built in onEnter, and what each one starts.
  RowKind rowKind[MAX_ROWS] = {};
  freeink::ui::ListItem rowItems[MAX_ROWS] = {};
  size_t rowCount = 0;
  // The New row the open confirmation asks about (-1: none), and the question (its focus: 0 Cancel, 1 New game).
  // Written by the loop task, read once per build by the render task. Orders: openConfirm stores focus before it
  // publishes the row with release, and render loads the row once with acquire; closers and loop-task reads may use
  // the default order.
  std::atomic<int8_t> confirmRow{-1};
  GameConfirmDialog confirm;
};
